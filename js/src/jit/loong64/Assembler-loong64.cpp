/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/loong64/Assembler-loong64.h"

#include "mozilla/DebugOnly.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/Maybe.h"
#include "mozilla/Sprintf.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

#include "gc/Marking.h"
#include "jit/AutoWritableJitCode.h"
#include "jit/IonTypes.h"
#include "jit/loong64/disasm/Disasm-loong64.h"
#include "wasm/WasmConstants.h"

using mozilla::DebugOnly;

using namespace js;
using namespace js::jit;

ABIArg ABIArgGenerator::next(MIRType type) {
  switch (type) {
    case MIRType::Int32:
    case MIRType::Int64:
    case MIRType::Pointer:
    case MIRType::WasmAnyRef:
    case MIRType::WasmArrayData:
    case MIRType::StackResults: {
      if (intRegIndex_ == NumIntArgRegs) {
        current_ = ABIArg(stackOffset_);
        stackOffset_ += sizeof(uintptr_t);
        break;
      }
      current_ = ABIArg(Register::FromCode(intRegIndex_ + a0.encoding()));
      intRegIndex_++;
      break;
    }
    case MIRType::Float32:
    case MIRType::Double: {
      if (floatRegIndex_ == NumFloatArgRegs) {
        // LoongArch floating-point parameters calling convention:
        //   The first 8 floating-point parameters should be passed in f0-f7,
        //   and the rest will be passed like integer parameters.
        if (kind_ == ABIKind::System && intRegIndex_ != NumIntArgRegs) {
          current_ = ABIArg(Register::FromCode(intRegIndex_ + a0.encoding()));
          intRegIndex_++;
          break;
        }
        current_ = ABIArg(stackOffset_);
        stackOffset_ += sizeof(double);
        break;
      }
      current_ = ABIArg(FloatRegister(
          FloatRegisters::Encoding(floatRegIndex_ + f0.encoding()),
          type == MIRType::Double ? FloatRegisters::Double
                                  : FloatRegisters::Single));
      floatRegIndex_++;
      break;
    }
#ifdef ENABLE_JIT_SIMD
    case MIRType::Simd128: {
      MOZ_ASSERT(kind_ != ABIKind::System, "psABI vector arg-passing NYI");
      if (floatRegIndex_ == NumFloatArgRegs) {
        stackOffset_ = AlignBytes(stackOffset_, SimdMemoryAlignment);
        current_ = ABIArg(stackOffset_);
        stackOffset_ += FloatRegister::SizeOfSimd128;
        break;
      }
      current_ = ABIArg(FloatRegister(
          FloatRegisters::Encoding(floatRegIndex_ + f0.encoding()),
          FloatRegisters::Simd128));
      floatRegIndex_++;
      break;
    }
#endif
    default:
      MOZ_CRASH("Unexpected argument type");
  }
  return current_;
}

// Encode a standard register when it is being used as rd, the rj, and
// an extra register(rk). These should never be called with an InvalidReg.
uint32_t js::jit::RJ(Register r) {
  MOZ_ASSERT(r != InvalidReg);
  return r.encoding() << RJShift;
}

uint32_t js::jit::RK(Register r) {
  MOZ_ASSERT(r != InvalidReg);
  return r.encoding() << RKShift;
}

uint32_t js::jit::RD(Register r) {
  MOZ_ASSERT(r != InvalidReg);
  return r.encoding() << RDShift;
}

uint32_t js::jit::FJ(FloatRegister r) { return r.encoding() << RJShift; }

uint32_t js::jit::FK(FloatRegister r) { return r.encoding() << RKShift; }

uint32_t js::jit::FD(FloatRegister r) { return r.encoding() << RDShift; }

uint32_t js::jit::FA(FloatRegister r) { return r.encoding() << FAShift; }

uint32_t js::jit::VJ(VRegister r) { return r.encoding() << RJShift; }

uint32_t js::jit::VK(VRegister r) { return r.encoding() << RKShift; }

uint32_t js::jit::VD(VRegister r) { return r.encoding() << RDShift; }

uint32_t js::jit::VA(VRegister r) { return r.encoding() << FAShift; }

uint32_t js::jit::SA2(uint32_t value) {
  MOZ_ASSERT(value < 4);
  return (value & SA2Mask) << SAShift;
}

uint32_t js::jit::SA3(uint32_t value) {
  MOZ_ASSERT(value < 8);
  return (value & SA3Mask) << SAShift;
}

Register js::jit::toRK(Instruction& i) {
  return Register::FromCode(((i.encode() >> RKShift) & RKMask));
}

Register js::jit::toRJ(Instruction& i) {
  return Register::FromCode(((i.encode() >> RJShift) & RJMask));
}

Register js::jit::toRD(Instruction& i) {
  return Register::FromCode(((i.encode() >> RDShift) & RDMask));
}

Register js::jit::toR(Instruction& i) {
  return Register::FromCode(i.encode() & RegMask);
}

void AssemblerLOONG64::finish() {
  MOZ_ASSERT(!isFinished);
  isFinished = true;
}

bool AssemblerLOONG64::appendRawCode(const uint8_t* code, size_t numBytes) {
  return m_buffer.appendRawCode(code, numBytes);
}

bool AssemblerLOONG64::reserve(size_t size) {
  // This buffer uses fixed-size chunks so there's no point in reserving
  // now vs. on-demand.
  return !oom();
}

bool AssemblerLOONG64::swapBuffer(wasm::Bytes& bytes) {
  // For now, specialize to the one use case. As long as wasm::Bytes is a
  // Vector, not a linked-list of chunks, there's not much we can do other
  // than copy.
  MOZ_ASSERT(bytes.empty());
  if (!bytes.resize(bytesNeeded())) {
    return false;
  }
  m_buffer.executableCopy(bytes.begin());
  return true;
}

void AssemblerLOONG64::copyJumpRelocationTable(uint8_t* dest) {
  if (jumpRelocations_.length()) {
    memcpy(dest, jumpRelocations_.buffer(), jumpRelocations_.length());
  }
}

void AssemblerLOONG64::copyDataRelocationTable(uint8_t* dest) {
  if (dataRelocations_.length()) {
    memcpy(dest, dataRelocations_.buffer(), dataRelocations_.length());
  }
}

AssemblerLOONG64::Condition AssemblerLOONG64::InvertCondition(Condition cond) {
  switch (cond) {
    case Equal:
      return NotEqual;
    case NotEqual:
      return Equal;
    case Zero:
      return NonZero;
    case NonZero:
      return Zero;
    case LessThan:
      return GreaterThanOrEqual;
    case LessThanOrEqual:
      return GreaterThan;
    case GreaterThan:
      return LessThanOrEqual;
    case GreaterThanOrEqual:
      return LessThan;
    case Above:
      return BelowOrEqual;
    case AboveOrEqual:
      return Below;
    case Below:
      return AboveOrEqual;
    case BelowOrEqual:
      return Above;
    case Signed:
      return NotSigned;
    case NotSigned:
      return Signed;
    default:
      MOZ_CRASH("unexpected condition");
  }
}

AssemblerLOONG64::DoubleCondition AssemblerLOONG64::InvertCondition(
    DoubleCondition cond) {
  switch (cond) {
    case DoubleOrdered:
      return DoubleUnordered;
    case DoubleEqual:
      return DoubleNotEqualOrUnordered;
    case DoubleNotEqual:
      return DoubleEqualOrUnordered;
    case DoubleGreaterThan:
      return DoubleLessThanOrEqualOrUnordered;
    case DoubleGreaterThanOrEqual:
      return DoubleLessThanOrUnordered;
    case DoubleLessThan:
      return DoubleGreaterThanOrEqualOrUnordered;
    case DoubleLessThanOrEqual:
      return DoubleGreaterThanOrUnordered;
    case DoubleUnordered:
      return DoubleOrdered;
    case DoubleEqualOrUnordered:
      return DoubleNotEqual;
    case DoubleNotEqualOrUnordered:
      return DoubleEqual;
    case DoubleGreaterThanOrUnordered:
      return DoubleLessThanOrEqual;
    case DoubleGreaterThanOrEqualOrUnordered:
      return DoubleLessThan;
    case DoubleLessThanOrUnordered:
      return DoubleGreaterThanOrEqual;
    case DoubleLessThanOrEqualOrUnordered:
      return DoubleGreaterThan;
    default:
      MOZ_CRASH("unexpected condition");
  }
}

AssemblerLOONG64::Condition AssemblerLOONG64::SwapCmpOperandsCondition(
    Condition cond) {
  switch (cond) {
    case Equal:
    case NotEqual:
      return cond;
    case LessThan:
      return GreaterThan;
    case LessThanOrEqual:
      return GreaterThanOrEqual;
    case GreaterThan:
      return LessThan;
    case GreaterThanOrEqual:
      return LessThanOrEqual;
    case Above:
      return Below;
    case AboveOrEqual:
      return BelowOrEqual;
    case Below:
      return Above;
    case BelowOrEqual:
      return AboveOrEqual;
    default:
      MOZ_CRASH("no meaningful swapped-operand condition");
  }
}

BOffImm16::BOffImm16(InstImm inst)
    : data(inst.extractBitField(Imm16Shift + Imm16Bits - 1, Imm16Shift)) {
  MOZ_ASSERT(inst.hasBOffImm16());
}

BOffImm21::BOffImm21(InstImm inst)
    : data(inst.extractBitField(Imm16Shift + Imm16Bits - 1, Imm16Shift) |
           (inst.extractBitField(Imm5Bits - 1, 0) << Imm16Bits)) {
  MOZ_ASSERT(inst.hasBOffImm21());
}

Instruction* BOffImm16::getDest(Instruction* src) const {
  return &src[(((int32_t)(data << 16)) >> 16) + 1];
}

bool AssemblerLOONG64::oom() const {
  return AssemblerShared::oom() || m_buffer.oom() || jumpRelocations_.oom() ||
         dataRelocations_.oom();
}

// Size of the instruction stream, in bytes.
size_t AssemblerLOONG64::size() const { return m_buffer.size(); }

// Returns the size of the buffer we can currently read.
size_t AssemblerLOONG64::readableSize() const { return m_buffer.size(); }

// Size of the relocation table, in bytes.
size_t AssemblerLOONG64::jumpRelocationTableBytes() const {
  return jumpRelocations_.length();
}

size_t AssemblerLOONG64::dataRelocationTableBytes() const {
  return dataRelocations_.length();
}

// Size of the data table, in bytes.
size_t AssemblerLOONG64::bytesNeeded() const {
  return size() + jumpRelocationTableBytes() + dataRelocationTableBytes();
}

// write a blob of binary into the instruction stream
BufferOffset AssemblerLOONG64::writeInst(uint32_t x, uint32_t* dest) {
  MOZ_ASSERT(hasCreator());
  if (dest == nullptr) {
    return m_buffer.putInt(x);
  }

  WriteInstStatic(x, dest);
  return BufferOffset();
}

void AssemblerLOONG64::WriteInstStatic(uint32_t x, uint32_t* dest) {
  MOZ_ASSERT(dest != nullptr);
  *dest = x;
}

BufferOffset AssemblerLOONG64::emit(uint32_t x) {
  BufferOffset offset = writeInst(x);
#ifdef JS_DISASM_LOONG64
  spew(offset, m_buffer.getInstOrNull(offset));
#endif
  return offset;
}

BufferOffset AssemblerLOONG64::emit(uint32_t x, LabelDoc target) {
  BufferOffset offset = writeInst(x);
#ifdef JS_DISASM_LOONG64
  spewBranch(offset, m_buffer.getInstOrNull(offset), target);
#endif
  return offset;
}

#ifdef JS_DISASM_LOONG64
class NameConverterWithLabelDoc final : public disasm::NameConverter {
  LabelDoc target_;
  mutable char targetBuffer_[32];
  mutable bool usedAddress_ = false;

 public:
  explicit NameConverterWithLabelDoc(LabelDoc target) : target_(target) {}

  const char* nameOfAddress(const uint8_t*) const override {
    usedAddress_ = true;
    if (target_.valid) {
      SprintfLiteral(targetBuffer_, "%u%s", target_.doc,
                     !target_.bound ? "f" : "");
    } else {
      SprintfLiteral(targetBuffer_, "(link-time target)");
    }
    return targetBuffer_;
  }

  bool usedAddress() const { return usedAddress_; }
};

void AssemblerLOONG64::spew(BufferOffset offset, Instruction* instruction) {
  if (spew_.isDisabled() || !instruction) {
    return;
  }

  char buffer[disasm::ReasonableBufferSize];
  disasm::NameConverter converter;
  disasm::Disassembler disassembler(converter);
  disassembler.disassemble(mozilla::Span<char>(buffer), instruction);

  spew_.spew("%06" PRIx32 " %08" PRIx32 "  %s", uint32_t(offset.getOffset()),
             instruction->encode(), buffer);
}

void AssemblerLOONG64::spewBranch(BufferOffset offset, Instruction* instruction,
                                  LabelDoc target) {
  if (spew_.isDisabled() || !instruction) {
    return;
  }

  char buffer[disasm::ReasonableBufferSize];
  NameConverterWithLabelDoc converter(target);
  disasm::Disassembler disassembler(converter);
  disassembler.disassemble(mozilla::Span<char>(buffer), instruction);

  char targetBuffer[32] = {};
  if (!converter.usedAddress() && !target.valid) {
    SprintfLiteral(targetBuffer, " -> (link-time target)");
  }

  spew_.spew("%06" PRIx32 " %08" PRIx32 "  %s%s", uint32_t(offset.getOffset()),
             instruction->encode(), buffer, targetBuffer);

  if (!converter.usedAddress() && target.valid) {
    spew_.spewRef(target);
  }
}

DisassemblerSpew::LabelDoc AssemblerLOONG64::refLabel(Label* label) {
  if (spew_.isDisabled()) {
    return LabelDoc();
  }
  return spew_.refLabel(label);
}
#endif

BufferOffset AssemblerLOONG64::haltingAlign(int alignment) {
  // TODO(loong64): Implement a proper halting align.
  return nopAlign(alignment);
}

BufferOffset AssemblerLOONG64::nopAlign(int alignment) {
  BufferOffset ret;
  MOZ_ASSERT(m_buffer.isAligned(4));
  if (alignment == 8) {
    if (!m_buffer.isAligned(alignment)) {
      BufferOffset tmp = as_nop();
      if (!ret.assigned()) {
        ret = tmp;
      }
    }
  } else {
    MOZ_ASSERT((alignment & (alignment - 1)) == 0);
    while (size() & (alignment - 1)) {
      BufferOffset tmp = as_nop();
      if (!ret.assigned()) {
        ret = tmp;
      }
    }
  }
  return ret;
}

// Logical operations.
BufferOffset AssemblerLOONG64::as_and(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_and, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_or(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_or, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_xor(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_xor, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_nor(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_nor, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_andn(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_andn, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_orn(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_orn, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_andi(Register rd, Register rj, int32_t ui12) {
  MOZ_ASSERT(is_uintN(ui12, 12));
  return emit(InstImm(op_andi, ui12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_ori(Register rd, Register rj, int32_t ui12) {
  MOZ_ASSERT(is_uintN(ui12, 12));
  return emit(InstImm(op_ori, ui12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_xori(Register rd, Register rj, int32_t ui12) {
  MOZ_ASSERT(is_uintN(ui12, 12));
  return emit(InstImm(op_xori, ui12, rj, rd, 12).encode());
}

// Branch and jump instructions
BufferOffset AssemblerLOONG64::as_b(JOffImm26 off) {
  return emit(InstJump(op_b, off).encode());
}

BufferOffset AssemblerLOONG64::as_bl(JOffImm26 off) {
  return emit(InstJump(op_bl, off).encode());
}

BufferOffset AssemblerLOONG64::as_jirl(Register rd, Register rj,
                                       BOffImm16 off) {
  return emit(InstImm(op_jirl, off, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_jirl(Register rd, Register rj, BOffImm16 off,
                                       LabelDoc doc) {
  return emit(InstImm(op_jirl, off, rj, rd).encode(), doc);
}

InstImm AssemblerLOONG64::getBranchCode(JumpOrCall jumpOrCall) {
  // jirl or beq
  if (jumpOrCall == BranchIsCall) {
    return InstImm(op_jirl, BOffImm16(0), zero, ra);
  }

  return InstImm(op_beq, BOffImm16(0), zero, zero);
}

InstImm AssemblerLOONG64::getBranchCode(Register rj, Register rd, Condition c) {
  switch (c) {
    case AssemblerLOONG64::Equal:
      return InstImm(op_beq, BOffImm16(0), rj, rd);
    case AssemblerLOONG64::NotEqual:
      return InstImm(op_bne, BOffImm16(0), rj, rd);
    case AssemblerLOONG64::Above:
      // LHS >u RHS -> RHS <u LHS
      return InstImm(op_bltu, BOffImm16(0), rd, rj);
    case AssemblerLOONG64::AboveOrEqual:
      return InstImm(op_bgeu, BOffImm16(0), rj, rd);
    case AssemblerLOONG64::Below:
      return InstImm(op_bltu, BOffImm16(0), rj, rd);
    case AssemblerLOONG64::BelowOrEqual:
      // LHS <=u RHS -> RHS >=u LHS
      return InstImm(op_bgeu, BOffImm16(0), rd, rj);
    case AssemblerLOONG64::GreaterThan:
      // LHS >s RHS -> RHS <s LHS
      return InstImm(op_blt, BOffImm16(0), rd, rj);
    case AssemblerLOONG64::GreaterThanOrEqual:
      return InstImm(op_bge, BOffImm16(0), rj, rd);
    case AssemblerLOONG64::LessThan:
      return InstImm(op_blt, BOffImm16(0), rj, rd);
    case AssemblerLOONG64::LessThanOrEqual:
      // LHS <=s RHS -> RHS >=s LHS
      return InstImm(op_bge, BOffImm16(0), rd, rj);
    default:
      MOZ_CRASH("Condition not supported.");
  }
}

InstImm AssemblerLOONG64::getBranchCode(Register rj, Condition c) {
  switch (c) {
    case AssemblerLOONG64::Equal:
    case AssemblerLOONG64::Zero:
    case AssemblerLOONG64::BelowOrEqual:
      return InstImm(op_beqz, 0, rj, /* NotHasRd= */ true);
    case AssemblerLOONG64::NotEqual:
    case AssemblerLOONG64::NonZero:
    case AssemblerLOONG64::Above:
      return InstImm(op_bnez, 0, rj, /* NotHasRd= */ true);
    case AssemblerLOONG64::GreaterThan:
      return InstImm(op_blt, BOffImm16(0), zero, rj);
    case AssemblerLOONG64::GreaterThanOrEqual:
    case AssemblerLOONG64::NotSigned:
      return InstImm(op_bge, BOffImm16(0), rj, zero);
    case AssemblerLOONG64::LessThan:
    case AssemblerLOONG64::Signed:
      return InstImm(op_blt, BOffImm16(0), rj, zero);
    case AssemblerLOONG64::LessThanOrEqual:
      return InstImm(op_bge, BOffImm16(0), zero, rj);
    default:
      MOZ_CRASH("Condition not supported.");
  }
}

// Code semantics must conform to compareFloatingPoint
InstImm AssemblerLOONG64::getBranchCode(FPConditionBit cj, Condition c) {
  MOZ_ASSERT(c == Zero || c == NonZero);
  return InstImm(op_bcz, 0, cj, c == NonZero);
}

// Arithmetic instructions
BufferOffset AssemblerLOONG64::as_add_w(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_add_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_add_d(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_add_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_sub_w(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_sub_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_sub_d(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_sub_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_addi_w(Register rd, Register rj,
                                         int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_addi_w, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_addi_d(Register rd, Register rj,
                                         int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_addi_d, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_addu16i_d(Register rd, Register rj,
                                            int32_t si16) {
  MOZ_ASSERT(Imm16::IsInSignedRange(si16));
  return emit(InstImm(op_addu16i_d, Imm16(si16), rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_alsl_w(Register rd, Register rj, Register rk,
                                         uint32_t sa2) {
  MOZ_ASSERT(sa2 < 4);
  return emit(InstReg(op_alsl_w, sa2, rk, rj, rd, 2).encode());
}

BufferOffset AssemblerLOONG64::as_alsl_wu(Register rd, Register rj, Register rk,
                                          uint32_t sa2) {
  MOZ_ASSERT(sa2 < 4);
  return emit(InstReg(op_alsl_wu, sa2, rk, rj, rd, 2).encode());
}

BufferOffset AssemblerLOONG64::as_alsl_d(Register rd, Register rj, Register rk,
                                         uint32_t sa2) {
  MOZ_ASSERT(sa2 < 4);
  return emit(InstReg(op_alsl_d, sa2, rk, rj, rd, 2).encode());
}

BufferOffset AssemblerLOONG64::as_lu12i_w(Register rd, int32_t si20) {
  return emit(InstImm(op_lu12i_w, si20, rd, false).encode());
}

BufferOffset AssemblerLOONG64::as_lu32i_d(Register rd, int32_t si20) {
  return emit(InstImm(op_lu32i_d, si20, rd, false).encode());
}

BufferOffset AssemblerLOONG64::as_lu52i_d(Register rd, Register rj,
                                          int32_t si12) {
  MOZ_ASSERT(is_uintN(si12, 12));
  return emit(InstImm(op_lu52i_d, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_slt(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_slt, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_sltu(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_sltu, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_slti(Register rd, Register rj, int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_slti, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_sltui(Register rd, Register rj,
                                        int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_sltui, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_pcaddi(Register rd, int32_t si20) {
  return emit(InstImm(op_pcaddi, si20, rd, false).encode());
}

BufferOffset AssemblerLOONG64::as_pcaddu12i(Register rd, int32_t si20) {
  return emit(InstImm(op_pcaddu12i, si20, rd, false).encode());
}

BufferOffset AssemblerLOONG64::as_pcaddu18i(Register rd, int32_t si20) {
  return emit(InstImm(op_pcaddu18i, si20, rd, false).encode());
}

BufferOffset AssemblerLOONG64::as_pcalau12i(Register rd, int32_t si20) {
  return emit(InstImm(op_pcalau12i, si20, rd, false).encode());
}

BufferOffset AssemblerLOONG64::as_mul_w(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_mul_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_mulh_w(Register rd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_mulh_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_mulh_wu(Register rd, Register rj,
                                          Register rk) {
  return emit(InstReg(op_mulh_wu, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_mul_d(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_mul_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_mulh_d(Register rd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_mulh_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_mulh_du(Register rd, Register rj,
                                          Register rk) {
  return emit(InstReg(op_mulh_du, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_mulw_d_w(Register rd, Register rj,
                                           Register rk) {
  return emit(InstReg(op_mulw_d_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_mulw_d_wu(Register rd, Register rj,
                                            Register rk) {
  return emit(InstReg(op_mulw_d_wu, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_div_w(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_div_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_mod_w(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_mod_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_div_wu(Register rd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_div_wu, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_mod_wu(Register rd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_mod_wu, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_div_d(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_div_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_mod_d(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_mod_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_div_du(Register rd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_div_du, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_mod_du(Register rd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_mod_du, rk, rj, rd).encode());
}

// Shift instructions
BufferOffset AssemblerLOONG64::as_sll_w(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_sll_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_srl_w(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_srl_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_sra_w(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_sra_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_rotr_w(Register rd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_rotr_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_slli_w(Register rd, Register rj,
                                         int32_t ui5) {
  MOZ_ASSERT(is_uintN(ui5, 5));
  return emit(InstImm(op_slli_w, ui5, rj, rd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_srli_w(Register rd, Register rj,
                                         int32_t ui5) {
  MOZ_ASSERT(is_uintN(ui5, 5));
  return emit(InstImm(op_srli_w, ui5, rj, rd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_srai_w(Register rd, Register rj,
                                         int32_t ui5) {
  MOZ_ASSERT(is_uintN(ui5, 5));
  return emit(InstImm(op_srai_w, ui5, rj, rd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_rotri_w(Register rd, Register rj,
                                          int32_t ui5) {
  MOZ_ASSERT(is_uintN(ui5, 5));
  return emit(InstImm(op_rotri_w, ui5, rj, rd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_sll_d(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_sll_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_srl_d(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_srl_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_sra_d(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_sra_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_rotr_d(Register rd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_rotr_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_slli_d(Register rd, Register rj,
                                         int32_t ui6) {
  MOZ_ASSERT(is_uintN(ui6, 6));
  return emit(InstImm(op_slli_d, ui6, rj, rd, 6).encode());
}

BufferOffset AssemblerLOONG64::as_srli_d(Register rd, Register rj,
                                         int32_t ui6) {
  MOZ_ASSERT(is_uintN(ui6, 6));
  return emit(InstImm(op_srli_d, ui6, rj, rd, 6).encode());
}

BufferOffset AssemblerLOONG64::as_srai_d(Register rd, Register rj,
                                         int32_t ui6) {
  MOZ_ASSERT(is_uintN(ui6, 6));
  return emit(InstImm(op_srai_d, ui6, rj, rd, 6).encode());
}

BufferOffset AssemblerLOONG64::as_rotri_d(Register rd, Register rj,
                                          int32_t ui6) {
  MOZ_ASSERT(is_uintN(ui6, 6));
  return emit(InstImm(op_rotri_d, ui6, rj, rd, 6).encode());
}

// Bit operation instrucitons
BufferOffset AssemblerLOONG64::as_ext_w_b(Register rd, Register rj) {
  return emit(InstReg(op_ext_w_b, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ext_w_h(Register rd, Register rj) {
  return emit(InstReg(op_ext_w_h, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_clo_w(Register rd, Register rj) {
  return emit(InstReg(op_clo_w, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_clz_w(Register rd, Register rj) {
  return emit(InstReg(op_clz_w, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_cto_w(Register rd, Register rj) {
  return emit(InstReg(op_cto_w, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ctz_w(Register rd, Register rj) {
  return emit(InstReg(op_ctz_w, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_clo_d(Register rd, Register rj) {
  return emit(InstReg(op_clo_d, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_clz_d(Register rd, Register rj) {
  return emit(InstReg(op_clz_d, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_cto_d(Register rd, Register rj) {
  return emit(InstReg(op_cto_d, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ctz_d(Register rd, Register rj) {
  return emit(InstReg(op_ctz_d, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_bytepick_w(Register rd, Register rj,
                                             Register rk, int32_t sa2) {
  MOZ_ASSERT(sa2 < 4);
  return emit(InstReg(op_bytepick_w, sa2, rk, rj, rd, 2).encode());
}

BufferOffset AssemblerLOONG64::as_bytepick_d(Register rd, Register rj,
                                             Register rk, int32_t sa3) {
  MOZ_ASSERT(sa3 < 8);
  return emit(InstReg(op_bytepick_d, sa3, rk, rj, rd, 3).encode());
}

BufferOffset AssemblerLOONG64::as_revb_2h(Register rd, Register rj) {
  return emit(InstReg(op_revb_2h, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_revb_4h(Register rd, Register rj) {
  return emit(InstReg(op_revb_4h, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_revb_2w(Register rd, Register rj) {
  return emit(InstReg(op_revb_2w, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_revb_d(Register rd, Register rj) {
  return emit(InstReg(op_revb_d, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_revh_2w(Register rd, Register rj) {
  return emit(InstReg(op_revh_2w, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_revh_d(Register rd, Register rj) {
  return emit(InstReg(op_revh_d, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_bitrev_4b(Register rd, Register rj) {
  return emit(InstReg(op_bitrev_4b, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_bitrev_8b(Register rd, Register rj) {
  return emit(InstReg(op_bitrev_8b, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_bitrev_w(Register rd, Register rj) {
  return emit(InstReg(op_bitrev_w, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_bitrev_d(Register rd, Register rj) {
  return emit(InstReg(op_bitrev_d, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_bstrins_w(Register rd, Register rj,
                                            int32_t msbw, int32_t lsbw) {
  MOZ_ASSERT(lsbw <= msbw);
  return emit(InstImm(op_bstr_w, msbw, lsbw, rj, rd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_bstrins_d(Register rd, Register rj,
                                            int32_t msbd, int32_t lsbd) {
  MOZ_ASSERT(lsbd <= msbd);
  return emit(InstImm(op_bstrins_d, msbd, lsbd, rj, rd, 6).encode());
}

BufferOffset AssemblerLOONG64::as_bstrpick_w(Register rd, Register rj,
                                             int32_t msbw, int32_t lsbw) {
  MOZ_ASSERT(lsbw <= msbw);
  return emit(InstImm(op_bstr_w, msbw, lsbw, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_bstrpick_d(Register rd, Register rj,
                                             int32_t msbd, int32_t lsbd) {
  MOZ_ASSERT(lsbd <= msbd);
  return emit(InstImm(op_bstrpick_d, msbd, lsbd, rj, rd, 6).encode());
}

BufferOffset AssemblerLOONG64::as_maskeqz(Register rd, Register rj,
                                          Register rk) {
  return emit(InstReg(op_maskeqz, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_masknez(Register rd, Register rj,
                                          Register rk) {
  return emit(InstReg(op_masknez, rk, rj, rd).encode());
}

// Load and store instructions
BufferOffset AssemblerLOONG64::as_ld_b(Register rd, Register rj, int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_ld_b, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_ld_h(Register rd, Register rj, int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_ld_h, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_ld_w(Register rd, Register rj, int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_ld_w, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_ld_d(Register rd, Register rj, int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_ld_d, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_ld_bu(Register rd, Register rj,
                                        int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_ld_bu, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_ld_hu(Register rd, Register rj,
                                        int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_ld_hu, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_ld_wu(Register rd, Register rj,
                                        int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_ld_wu, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_st_b(Register rd, Register rj, int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_st_b, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_st_h(Register rd, Register rj, int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_st_h, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_st_w(Register rd, Register rj, int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_st_w, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_st_d(Register rd, Register rj, int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_st_d, si12, rj, rd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_ldx_b(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_ldx_b, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ldx_h(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_ldx_h, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ldx_w(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_ldx_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ldx_d(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_ldx_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ldx_bu(Register rd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_ldx_bu, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ldx_hu(Register rd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_ldx_hu, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ldx_wu(Register rd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_ldx_wu, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_stx_b(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_stx_b, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_stx_h(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_stx_h, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_stx_w(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_stx_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_stx_d(Register rd, Register rj, Register rk) {
  return emit(InstReg(op_stx_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ldptr_w(Register rd, Register rj,
                                          int32_t si14) {
  MOZ_ASSERT(is_intN(si14, 16) && ((si14 & 0x3) == 0));
  return emit(InstImm(op_ldptr_w, si14 >> 2, rj, rd, 14).encode());
}

BufferOffset AssemblerLOONG64::as_ldptr_d(Register rd, Register rj,
                                          int32_t si14) {
  MOZ_ASSERT(is_intN(si14, 16) && ((si14 & 0x3) == 0));
  return emit(InstImm(op_ldptr_d, si14 >> 2, rj, rd, 14).encode());
}

BufferOffset AssemblerLOONG64::as_stptr_w(Register rd, Register rj,
                                          int32_t si14) {
  MOZ_ASSERT(is_intN(si14, 16) && ((si14 & 0x3) == 0));
  return emit(InstImm(op_stptr_w, si14 >> 2, rj, rd, 14).encode());
}

BufferOffset AssemblerLOONG64::as_stptr_d(Register rd, Register rj,
                                          int32_t si14) {
  MOZ_ASSERT(is_intN(si14, 16) && ((si14 & 0x3) == 0));
  return emit(InstImm(op_stptr_d, si14 >> 2, rj, rd, 14).encode());
}

BufferOffset AssemblerLOONG64::as_preld(int32_t hint, Register rj,
                                        int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_preld, si12, rj, hint).encode());
}

// Atomic instructions
BufferOffset AssemblerLOONG64::as_amswap_w(Register rd, Register rj,
                                           Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amswap_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amswap_d(Register rd, Register rj,
                                           Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amswap_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amadd_w(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amadd_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amadd_d(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amadd_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amand_w(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amand_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amand_d(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amand_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amor_w(Register rd, Register rj,
                                         Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amor_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amor_d(Register rd, Register rj,
                                         Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amor_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amxor_w(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amxor_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amxor_d(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amxor_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammax_w(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammax_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammax_d(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammax_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammin_w(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammin_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammin_d(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammin_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammax_wu(Register rd, Register rj,
                                           Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammax_wu, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammax_du(Register rd, Register rj,
                                           Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammax_du, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammin_wu(Register rd, Register rj,
                                           Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammin_wu, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammin_du(Register rd, Register rj,
                                           Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammin_du, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amswap_db_w(Register rd, Register rj,
                                              Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amswap_db_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amswap_db_d(Register rd, Register rj,
                                              Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amswap_db_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amadd_db_w(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amadd_db_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amadd_db_d(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amadd_db_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amand_db_w(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amand_db_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amand_db_d(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amand_db_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amor_db_w(Register rd, Register rj,
                                            Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amor_db_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amor_db_d(Register rd, Register rj,
                                            Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amor_db_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amxor_db_w(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amxor_db_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amxor_db_d(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amxor_db_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammax_db_w(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammax_db_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammax_db_d(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammax_db_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammin_db_w(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammin_db_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammin_db_d(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammin_db_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammax_db_wu(Register rd, Register rj,
                                              Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammax_db_wu, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammax_db_du(Register rd, Register rj,
                                              Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammax_db_du, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammin_db_wu(Register rd, Register rj,
                                              Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammin_db_wu, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ammin_db_du(Register rd, Register rj,
                                              Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_ammin_db_du, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_ll_w(Register rd, Register rj, int32_t si14) {
  MOZ_ASSERT(is_intN(si14, 16) && ((si14 & 0x3) == 0));
  return emit(InstImm(op_ll_w, si14 >> 2, rj, rd, 14).encode());
}

BufferOffset AssemblerLOONG64::as_ll_d(Register rd, Register rj, int32_t si14) {
  MOZ_ASSERT(is_intN(si14, 16) && ((si14 & 0x3) == 0));
  return emit(InstImm(op_ll_d, si14 >> 2, rj, rd, 14).encode());
}

BufferOffset AssemblerLOONG64::as_sc_w(Register rd, Register rj, int32_t si14) {
  MOZ_ASSERT(is_intN(si14, 16) && ((si14 & 0x3) == 0));
  return emit(InstImm(op_sc_w, si14 >> 2, rj, rd, 14).encode());
}

BufferOffset AssemblerLOONG64::as_sc_d(Register rd, Register rj, int32_t si14) {
  MOZ_ASSERT(is_intN(si14, 16) && ((si14 & 0x3) == 0));
  return emit(InstImm(op_sc_d, si14 >> 2, rj, rd, 14).encode());
}

// Atomic instructions from LAM_BH extension
BufferOffset AssemblerLOONG64::as_amswap_b(Register rd, Register rj,
                                           Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amswap_b, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amswap_h(Register rd, Register rj,
                                           Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amswap_h, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amadd_b(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amadd_b, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amadd_h(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amadd_h, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amswap_db_b(Register rd, Register rj,
                                              Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amswap_db_b, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amswap_db_h(Register rd, Register rj,
                                              Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amswap_db_h, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amadd_db_b(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amadd_db_b, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amadd_db_h(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amadd_db_h, rk, rj, rd).encode());
}

// Atomic instructions from LAMCAS extension
BufferOffset AssemblerLOONG64::as_amcas_b(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amcas_b, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amcas_h(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amcas_h, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amcas_w(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amcas_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amcas_d(Register rd, Register rj,
                                          Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amcas_d, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amcas_db_b(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amcas_db_b, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amcas_db_h(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amcas_db_h, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amcas_db_w(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amcas_db_w, rk, rj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_amcas_db_d(Register rd, Register rj,
                                             Register rk) {
  MOZ_ASSERT(rd != rj);
  MOZ_ASSERT(rd != rk);
  return emit(InstReg(op_amcas_db_d, rk, rj, rd).encode());
}

// Barrier instructions
BufferOffset AssemblerLOONG64::as_dbar(int32_t hint) {
  MOZ_ASSERT(is_uintN(hint, 15));
  return emit(InstImm(op_dbar, hint).encode());
}

BufferOffset AssemblerLOONG64::as_ibar(int32_t hint) {
  MOZ_ASSERT(is_uintN(hint, 15));
  return emit(InstImm(op_ibar, hint).encode());
}

/* =============================================================== */

// FP Arithmetic instructions
BufferOffset AssemblerLOONG64::as_fadd_s(FloatRegister fd, FloatRegister fj,
                                         FloatRegister fk) {
  return emit(InstReg(op_fadd_s, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fadd_d(FloatRegister fd, FloatRegister fj,
                                         FloatRegister fk) {
  return emit(InstReg(op_fadd_d, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fsub_s(FloatRegister fd, FloatRegister fj,
                                         FloatRegister fk) {
  return emit(InstReg(op_fsub_s, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fsub_d(FloatRegister fd, FloatRegister fj,
                                         FloatRegister fk) {
  return emit(InstReg(op_fsub_d, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmul_s(FloatRegister fd, FloatRegister fj,
                                         FloatRegister fk) {
  return emit(InstReg(op_fmul_s, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmul_d(FloatRegister fd, FloatRegister fj,
                                         FloatRegister fk) {
  return emit(InstReg(op_fmul_d, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fdiv_s(FloatRegister fd, FloatRegister fj,
                                         FloatRegister fk) {
  return emit(InstReg(op_fdiv_s, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fdiv_d(FloatRegister fd, FloatRegister fj,
                                         FloatRegister fk) {
  return emit(InstReg(op_fdiv_d, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmadd_s(FloatRegister fd, FloatRegister fj,
                                          FloatRegister fk, FloatRegister fa) {
  return emit(InstReg(op_fmadd_s, fa, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmadd_d(FloatRegister fd, FloatRegister fj,
                                          FloatRegister fk, FloatRegister fa) {
  return emit(InstReg(op_fmadd_d, fa, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmsub_s(FloatRegister fd, FloatRegister fj,
                                          FloatRegister fk, FloatRegister fa) {
  return emit(InstReg(op_fmsub_s, fa, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmsub_d(FloatRegister fd, FloatRegister fj,
                                          FloatRegister fk, FloatRegister fa) {
  return emit(InstReg(op_fmsub_d, fa, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fnmadd_s(FloatRegister fd, FloatRegister fj,
                                           FloatRegister fk, FloatRegister fa) {
  return emit(InstReg(op_fnmadd_s, fa, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fnmadd_d(FloatRegister fd, FloatRegister fj,
                                           FloatRegister fk, FloatRegister fa) {
  return emit(InstReg(op_fnmadd_d, fa, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fnmsub_s(FloatRegister fd, FloatRegister fj,
                                           FloatRegister fk, FloatRegister fa) {
  return emit(InstReg(op_fnmsub_s, fa, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fnmsub_d(FloatRegister fd, FloatRegister fj,
                                           FloatRegister fk, FloatRegister fa) {
  return emit(InstReg(op_fnmsub_d, fa, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmax_s(FloatRegister fd, FloatRegister fj,
                                         FloatRegister fk) {
  return emit(InstReg(op_fmax_s, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmax_d(FloatRegister fd, FloatRegister fj,
                                         FloatRegister fk) {
  return emit(InstReg(op_fmax_d, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmin_s(FloatRegister fd, FloatRegister fj,
                                         FloatRegister fk) {
  return emit(InstReg(op_fmin_s, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmin_d(FloatRegister fd, FloatRegister fj,
                                         FloatRegister fk) {
  return emit(InstReg(op_fmin_d, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmaxa_s(FloatRegister fd, FloatRegister fj,
                                          FloatRegister fk) {
  return emit(InstReg(op_fmaxa_s, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmaxa_d(FloatRegister fd, FloatRegister fj,
                                          FloatRegister fk) {
  return emit(InstReg(op_fmaxa_d, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmina_s(FloatRegister fd, FloatRegister fj,
                                          FloatRegister fk) {
  return emit(InstReg(op_fmina_s, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmina_d(FloatRegister fd, FloatRegister fj,
                                          FloatRegister fk) {
  return emit(InstReg(op_fmina_d, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fabs_s(FloatRegister fd, FloatRegister fj) {
  return emit(InstReg(op_fabs_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fabs_d(FloatRegister fd, FloatRegister fj) {
  return emit(InstReg(op_fabs_d, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fneg_s(FloatRegister fd, FloatRegister fj) {
  return emit(InstReg(op_fneg_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fneg_d(FloatRegister fd, FloatRegister fj) {
  return emit(InstReg(op_fneg_d, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fsqrt_s(FloatRegister fd, FloatRegister fj) {
  return emit(InstReg(op_fsqrt_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fsqrt_d(FloatRegister fd, FloatRegister fj) {
  return emit(InstReg(op_fsqrt_d, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fcopysign_s(FloatRegister fd,
                                              FloatRegister fj,
                                              FloatRegister fk) {
  return emit(InstReg(op_fcopysign_s, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fcopysign_d(FloatRegister fd,
                                              FloatRegister fj,
                                              FloatRegister fk) {
  return emit(InstReg(op_fcopysign_d, fk, fj, fd).encode());
}

// FP compare instructions
// fcmp.cond.s and fcmp.cond.d instructions
BufferOffset AssemblerLOONG64::as_fcmp_cor(FloatFormat fmt, FloatRegister fj,
                                           FloatRegister fk,
                                           FPConditionBit cd) {
  if (fmt == DoubleFloat) {
    return emit(InstReg(op_fcmp_cond_d, COR, fk, fj, cd).encode());
  } else {
    return emit(InstReg(op_fcmp_cond_s, COR, fk, fj, cd).encode());
  }
}

BufferOffset AssemblerLOONG64::as_fcmp_ceq(FloatFormat fmt, FloatRegister fj,
                                           FloatRegister fk,
                                           FPConditionBit cd) {
  if (fmt == DoubleFloat) {
    return emit(InstReg(op_fcmp_cond_d, CEQ, fk, fj, cd).encode());
  } else {
    return emit(InstReg(op_fcmp_cond_s, CEQ, fk, fj, cd).encode());
  }
}

BufferOffset AssemblerLOONG64::as_fcmp_cne(FloatFormat fmt, FloatRegister fj,
                                           FloatRegister fk,
                                           FPConditionBit cd) {
  if (fmt == DoubleFloat) {
    return emit(InstReg(op_fcmp_cond_d, CNE, fk, fj, cd).encode());
  } else {
    return emit(InstReg(op_fcmp_cond_s, CNE, fk, fj, cd).encode());
  }
}

BufferOffset AssemblerLOONG64::as_fcmp_cle(FloatFormat fmt, FloatRegister fj,
                                           FloatRegister fk,
                                           FPConditionBit cd) {
  if (fmt == DoubleFloat) {
    return emit(InstReg(op_fcmp_cond_d, CLE, fk, fj, cd).encode());
  } else {
    return emit(InstReg(op_fcmp_cond_s, CLE, fk, fj, cd).encode());
  }
}

BufferOffset AssemblerLOONG64::as_fcmp_clt(FloatFormat fmt, FloatRegister fj,
                                           FloatRegister fk,
                                           FPConditionBit cd) {
  if (fmt == DoubleFloat) {
    return emit(InstReg(op_fcmp_cond_d, CLT, fk, fj, cd).encode());
  } else {
    return emit(InstReg(op_fcmp_cond_s, CLT, fk, fj, cd).encode());
  }
}

BufferOffset AssemblerLOONG64::as_fcmp_cun(FloatFormat fmt, FloatRegister fj,
                                           FloatRegister fk,
                                           FPConditionBit cd) {
  if (fmt == DoubleFloat) {
    return emit(InstReg(op_fcmp_cond_d, CUN, fk, fj, cd).encode());
  } else {
    return emit(InstReg(op_fcmp_cond_s, CUN, fk, fj, cd).encode());
  }
}

BufferOffset AssemblerLOONG64::as_fcmp_cueq(FloatFormat fmt, FloatRegister fj,
                                            FloatRegister fk,
                                            FPConditionBit cd) {
  if (fmt == DoubleFloat) {
    return emit(InstReg(op_fcmp_cond_d, CUEQ, fk, fj, cd).encode());
  } else {
    return emit(InstReg(op_fcmp_cond_s, CUEQ, fk, fj, cd).encode());
  }
}

BufferOffset AssemblerLOONG64::as_fcmp_cune(FloatFormat fmt, FloatRegister fj,
                                            FloatRegister fk,
                                            FPConditionBit cd) {
  if (fmt == DoubleFloat) {
    return emit(InstReg(op_fcmp_cond_d, CUNE, fk, fj, cd).encode());
  } else {
    return emit(InstReg(op_fcmp_cond_s, CUNE, fk, fj, cd).encode());
  }
}

BufferOffset AssemblerLOONG64::as_fcmp_cule(FloatFormat fmt, FloatRegister fj,
                                            FloatRegister fk,
                                            FPConditionBit cd) {
  if (fmt == DoubleFloat) {
    return emit(InstReg(op_fcmp_cond_d, CULE, fk, fj, cd).encode());
  } else {
    return emit(InstReg(op_fcmp_cond_s, CULE, fk, fj, cd).encode());
  }
}

BufferOffset AssemblerLOONG64::as_fcmp_cult(FloatFormat fmt, FloatRegister fj,
                                            FloatRegister fk,
                                            FPConditionBit cd) {
  if (fmt == DoubleFloat) {
    return emit(InstReg(op_fcmp_cond_d, CULT, fk, fj, cd).encode());
  } else {
    return emit(InstReg(op_fcmp_cond_s, CULT, fk, fj, cd).encode());
  }
}

// FP conversion instructions
BufferOffset AssemblerLOONG64::as_fcvt_s_d(FloatRegister fd, FloatRegister fj) {
  return emit(InstReg(op_fcvt_s_d, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fcvt_d_s(FloatRegister fd, FloatRegister fj) {
  return emit(InstReg(op_fcvt_d_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ffint_s_w(FloatRegister fd,
                                            FloatRegister fj) {
  return emit(InstReg(op_ffint_s_w, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ffint_s_l(FloatRegister fd,
                                            FloatRegister fj) {
  return emit(InstReg(op_ffint_s_l, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ffint_d_w(FloatRegister fd,
                                            FloatRegister fj) {
  return emit(InstReg(op_ffint_d_w, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ffint_d_l(FloatRegister fd,
                                            FloatRegister fj) {
  return emit(InstReg(op_ffint_d_l, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftint_w_s(FloatRegister fd,
                                            FloatRegister fj) {
  return emit(InstReg(op_ftint_w_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftint_w_d(FloatRegister fd,
                                            FloatRegister fj) {
  return emit(InstReg(op_ftint_w_d, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftint_l_s(FloatRegister fd,
                                            FloatRegister fj) {
  return emit(InstReg(op_ftint_l_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftint_l_d(FloatRegister fd,
                                            FloatRegister fj) {
  return emit(InstReg(op_ftint_l_d, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrm_w_s(FloatRegister fd,
                                              FloatRegister fj) {
  return emit(InstReg(op_ftintrm_w_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrm_w_d(FloatRegister fd,
                                              FloatRegister fj) {
  return emit(InstReg(op_ftintrm_w_d, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrm_l_s(FloatRegister fd,
                                              FloatRegister fj) {
  return emit(InstReg(op_ftintrm_l_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrm_l_d(FloatRegister fd,
                                              FloatRegister fj) {
  return emit(InstReg(op_ftintrm_l_d, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrp_w_s(FloatRegister fd,
                                              FloatRegister fj) {
  return emit(InstReg(op_ftintrp_w_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrp_w_d(FloatRegister fd,
                                              FloatRegister fj) {
  return emit(InstReg(op_ftintrp_w_d, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrp_l_s(FloatRegister fd,
                                              FloatRegister fj) {
  return emit(InstReg(op_ftintrp_l_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrp_l_d(FloatRegister fd,
                                              FloatRegister fj) {
  return emit(InstReg(op_ftintrp_l_d, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrz_w_s(FloatRegister fd,
                                              FloatRegister fj) {
  return emit(InstReg(op_ftintrz_w_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrz_w_d(FloatRegister fd,
                                              FloatRegister fj) {
  return emit(InstReg(op_ftintrz_w_d, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrz_l_s(FloatRegister fd,
                                              FloatRegister fj) {
  return emit(InstReg(op_ftintrz_l_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrz_l_d(FloatRegister fd,
                                              FloatRegister fj) {
  return emit(InstReg(op_ftintrz_l_d, fj, fd).encode());
}
BufferOffset AssemblerLOONG64::as_ftintrne_w_s(FloatRegister fd,
                                               FloatRegister fj) {
  return emit(InstReg(op_ftintrne_w_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrne_w_d(FloatRegister fd,
                                               FloatRegister fj) {
  return emit(InstReg(op_ftintrne_w_d, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrne_l_s(FloatRegister fd,
                                               FloatRegister fj) {
  return emit(InstReg(op_ftintrne_l_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_ftintrne_l_d(FloatRegister fd,
                                               FloatRegister fj) {
  return emit(InstReg(op_ftintrne_l_d, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_frint_s(FloatRegister fd, FloatRegister fj) {
  return emit(InstReg(op_frint_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_frint_d(FloatRegister fd, FloatRegister fj) {
  return emit(InstReg(op_frint_d, fj, fd).encode());
}

// FP mov instructions
BufferOffset AssemblerLOONG64::as_fmov_s(FloatRegister fd, FloatRegister fj) {
  return emit(InstReg(op_fmov_s, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fmov_d(FloatRegister fd, FloatRegister fj) {
  return emit(InstReg(op_fmov_d, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fsel(FloatRegister fd, FloatRegister fj,
                                       FloatRegister fk, FPConditionBit ca) {
  return emit(InstReg(op_fsel, ca, fk, fj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_movgr2fr_w(FloatRegister fd, Register rj) {
  return emit(InstReg(op_movgr2fr_w, rj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_movgr2fr_d(FloatRegister fd, Register rj) {
  return emit(InstReg(op_movgr2fr_d, rj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_movgr2frh_w(FloatRegister fd, Register rj) {
  return emit(InstReg(op_movgr2frh_w, rj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_movfr2gr_s(Register rd, FloatRegister fj) {
  return emit(InstReg(op_movfr2gr_s, fj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_movfr2gr_d(Register rd, FloatRegister fj) {
  return emit(InstReg(op_movfr2gr_d, fj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_movfrh2gr_s(Register rd, FloatRegister fj) {
  return emit(InstReg(op_movfrh2gr_s, fj, rd).encode());
}

BufferOffset AssemblerLOONG64::as_movgr2fcsr(Register rj) {
  return emit(InstReg(op_movgr2fcsr, rj, FCSR).encode());
}

BufferOffset AssemblerLOONG64::as_movfcsr2gr(Register rd) {
  return emit(InstReg(op_movfcsr2gr, FCSR, rd).encode());
}

BufferOffset AssemblerLOONG64::as_movfr2cf(FPConditionBit cd,
                                           FloatRegister fj) {
  return emit(InstReg(op_movfr2cf, fj, cd).encode());
}

BufferOffset AssemblerLOONG64::as_movcf2fr(FloatRegister fd,
                                           FPConditionBit cj) {
  return emit(InstReg(op_movcf2fr, cj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_movgr2cf(FPConditionBit cd, Register rj) {
  return emit(InstReg(op_movgr2cf, rj, cd).encode());
}

BufferOffset AssemblerLOONG64::as_movcf2gr(Register rd, FPConditionBit cj) {
  return emit(InstReg(op_movcf2gr, cj, rd).encode());
}

// FP load/store instructions
BufferOffset AssemblerLOONG64::as_fld_s(FloatRegister fd, Register rj,
                                        int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_fld_s, si12, rj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fld_d(FloatRegister fd, Register rj,
                                        int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_fld_d, si12, rj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fst_s(FloatRegister fd, Register rj,
                                        int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_fst_s, si12, rj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fst_d(FloatRegister fd, Register rj,
                                        int32_t si12) {
  MOZ_ASSERT(is_intN(si12, 12));
  return emit(InstImm(op_fst_d, si12, rj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fldx_s(FloatRegister fd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_fldx_s, rk, rj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fldx_d(FloatRegister fd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_fldx_d, rk, rj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fstx_s(FloatRegister fd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_fstx_s, rk, rj, fd).encode());
}

BufferOffset AssemblerLOONG64::as_fstx_d(FloatRegister fd, Register rj,
                                         Register rk) {
  return emit(InstReg(op_fstx_d, rk, rj, fd).encode());
}

/* ========================================================================= */

/* LSX (128-bit SIMD) instructions. */

BufferOffset AssemblerLOONG64::as_vfmadd_s(VRegister vd, VRegister vj,
                                           VRegister vk, VRegister va) {
  return emit(InstReg(op_vfmadd_s, va, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfmadd_d(VRegister vd, VRegister vj,
                                           VRegister vk, VRegister va) {
  return emit(InstReg(op_vfmadd_d, va, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfnmsub_s(VRegister vd, VRegister vj,
                                            VRegister vk, VRegister va) {
  return emit(InstReg(op_vfnmsub_s, va, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfnmsub_d(VRegister vd, VRegister vj,
                                            VRegister vk, VRegister va) {
  return emit(InstReg(op_vfnmsub_d, va, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfcmp_cond_s(FPUCondition cond, VRegister vd,
                                               VRegister vj, VRegister vk) {
  return emit(InstReg(op_vfcmp_cond_s, cond, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfcmp_cond_d(FPUCondition cond, VRegister vd,
                                               VRegister vj, VRegister vk) {
  return emit(InstReg(op_vfcmp_cond_d, cond, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vbitsel_v(VRegister vd, VRegister vj,
                                            VRegister vk, VRegister va) {
  return emit(InstReg(op_vbitsel_v, va, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vshuf_b(VRegister vd, VRegister vj,
                                          VRegister vk, VRegister va) {
  return emit(InstReg(op_vshuf_b, va, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vld(VRegister vd, Register rj,
                                      uint32_t imm12) {
  return emit(InstImm(op_vld, imm12, rj, vd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_vst(VRegister vd, Register rj,
                                      uint32_t imm12) {
  return emit(InstImm(op_vst, imm12, rj, vd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_vldrepl_d(VRegister vd, Register rj,
                                            uint32_t imm9) {
  return emit(InstImm(op_vldrepl_d, imm9, rj, vd, 9).encode());
}

BufferOffset AssemblerLOONG64::as_vldrepl_w(VRegister vd, Register rj,
                                            uint32_t imm10) {
  return emit(InstImm(op_vldrepl_w, imm10, rj, vd, 10).encode());
}

BufferOffset AssemblerLOONG64::as_vldrepl_h(VRegister vd, Register rj,
                                            uint32_t imm11) {
  return emit(InstImm(op_vldrepl_h, imm11, rj, vd, 11).encode());
}

BufferOffset AssemblerLOONG64::as_vldrepl_b(VRegister vd, Register rj,
                                            uint32_t imm12) {
  return emit(InstImm(op_vldrepl_b, imm12, rj, vd, 12).encode());
}

BufferOffset AssemblerLOONG64::as_vstelm_d(VRegister vd, Register rj,
                                           int32_t si8, uint32_t lane) {
  return emit(InstImm(op_vstelm_d, si8, lane, 1, rj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vstelm_w(VRegister vd, Register rj,
                                           int32_t si8, uint32_t lane) {
  return emit(InstImm(op_vstelm_w, si8, lane, 2, rj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vstelm_h(VRegister vd, Register rj,
                                           int32_t si8, uint32_t lane) {
  return emit(InstImm(op_vstelm_h, si8, lane, 3, rj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vstelm_b(VRegister vd, Register rj,
                                           int32_t si8, uint32_t lane) {
  return emit(InstImm(op_vstelm_b, si8, lane, 4, rj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vldx(VRegister vd, Register rj, Register rk) {
  return emit(InstReg(op_vldx, rk, rj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vstx(VRegister vd, Register rj, Register rk) {
  return emit(InstReg(op_vstx, rk, rj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vseq_b(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vseq_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vseq_h(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vseq_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vseq_w(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vseq_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vseq_d(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vseq_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsle_b(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsle_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsle_h(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsle_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsle_w(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsle_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsle_d(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsle_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsle_bu(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vsle_bu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsle_hu(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vsle_hu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsle_wu(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vsle_wu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vslt_b(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vslt_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vslt_h(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vslt_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vslt_w(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vslt_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vslt_d(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vslt_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vslt_bu(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vslt_bu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vslt_hu(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vslt_hu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vslt_wu(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vslt_wu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vadd_b(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vadd_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vadd_h(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vadd_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vadd_w(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vadd_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vadd_d(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vadd_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsub_b(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsub_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsub_h(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsub_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsub_w(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsub_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsub_d(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsub_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsadd_b(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vsadd_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsadd_h(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vsadd_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vssub_b(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vssub_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vssub_h(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vssub_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsadd_bu(VRegister vd, VRegister vj,
                                           VRegister vk) {
  return emit(InstReg(op_vsadd_bu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsadd_hu(VRegister vd, VRegister vj,
                                           VRegister vk) {
  return emit(InstReg(op_vsadd_hu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vssub_bu(VRegister vd, VRegister vj,
                                           VRegister vk) {
  return emit(InstReg(op_vssub_bu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vssub_hu(VRegister vd, VRegister vj,
                                           VRegister vk) {
  return emit(InstReg(op_vssub_hu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vhaddw_h_b(VRegister vd, VRegister vj,
                                             VRegister vk) {
  return emit(InstReg(op_vhaddw_h_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vhaddw_w_h(VRegister vd, VRegister vj,
                                             VRegister vk) {
  return emit(InstReg(op_vhaddw_w_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vhaddw_hu_bu(VRegister vd, VRegister vj,
                                               VRegister vk) {
  return emit(InstReg(op_vhaddw_hu_bu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vhaddw_wu_hu(VRegister vd, VRegister vj,
                                               VRegister vk) {
  return emit(InstReg(op_vhaddw_wu_hu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vadda_b(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vadda_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vadda_h(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vadda_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vadda_w(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vadda_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vadda_d(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vadda_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vavgr_bu(VRegister vd, VRegister vj,
                                           VRegister vk) {
  return emit(InstReg(op_vavgr_bu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vavgr_hu(VRegister vd, VRegister vj,
                                           VRegister vk) {
  return emit(InstReg(op_vavgr_hu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmax_b(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vmax_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmax_h(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vmax_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmax_w(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vmax_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmin_b(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vmin_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmin_h(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vmin_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmin_w(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vmin_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmax_bu(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vmax_bu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmax_hu(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vmax_hu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmax_wu(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vmax_wu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmin_bu(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vmin_bu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmin_hu(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vmin_hu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmin_wu(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vmin_wu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmul_h(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vmul_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmul_w(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vmul_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmul_d(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vmul_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmulwev_h_b(VRegister vd, VRegister vj,
                                              VRegister vk) {
  return emit(InstReg(op_vmulwev_h_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmulwev_w_h(VRegister vd, VRegister vj,
                                              VRegister vk) {
  return emit(InstReg(op_vmulwev_w_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmulwev_d_w(VRegister vd, VRegister vj,
                                              VRegister vk) {
  return emit(InstReg(op_vmulwev_d_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmulwod_h_b(VRegister vd, VRegister vj,
                                              VRegister vk) {
  return emit(InstReg(op_vmulwod_h_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmulwod_w_h(VRegister vd, VRegister vj,
                                              VRegister vk) {
  return emit(InstReg(op_vmulwod_w_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmulwod_d_w(VRegister vd, VRegister vj,
                                              VRegister vk) {
  return emit(InstReg(op_vmulwod_d_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmulwev_h_bu(VRegister vd, VRegister vj,
                                               VRegister vk) {
  return emit(InstReg(op_vmulwev_h_bu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmulwev_w_hu(VRegister vd, VRegister vj,
                                               VRegister vk) {
  return emit(InstReg(op_vmulwev_w_hu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmulwev_d_wu(VRegister vd, VRegister vj,
                                               VRegister vk) {
  return emit(InstReg(op_vmulwev_d_wu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmulwod_h_bu(VRegister vd, VRegister vj,
                                               VRegister vk) {
  return emit(InstReg(op_vmulwod_h_bu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmulwod_w_hu(VRegister vd, VRegister vj,
                                               VRegister vk) {
  return emit(InstReg(op_vmulwod_w_hu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmulwod_d_wu(VRegister vd, VRegister vj,
                                               VRegister vk) {
  return emit(InstReg(op_vmulwod_d_wu, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmaddwev_w_h(VRegister vd, VRegister vj,
                                               VRegister vk) {
  return emit(InstReg(op_vmaddwev_w_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmaddwod_w_h(VRegister vd, VRegister vj,
                                               VRegister vk) {
  return emit(InstReg(op_vmaddwod_w_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsll_b(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsll_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsll_h(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsll_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsll_w(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsll_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsll_d(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsll_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsrl_b(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsrl_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsrl_h(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsrl_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsrl_w(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsrl_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsrl_d(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsrl_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsra_b(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsra_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsra_h(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsra_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsra_w(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsra_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsra_d(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vsra_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpackev_b(VRegister vd, VRegister vj,
                                            VRegister vk) {
  return emit(InstReg(op_vpackev_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpackev_h(VRegister vd, VRegister vj,
                                            VRegister vk) {
  return emit(InstReg(op_vpackev_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpackev_w(VRegister vd, VRegister vj,
                                            VRegister vk) {
  return emit(InstReg(op_vpackev_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpackod_b(VRegister vd, VRegister vj,
                                            VRegister vk) {
  return emit(InstReg(op_vpackod_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpackod_h(VRegister vd, VRegister vj,
                                            VRegister vk) {
  return emit(InstReg(op_vpackod_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpackod_w(VRegister vd, VRegister vj,
                                            VRegister vk) {
  return emit(InstReg(op_vpackod_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vilvl_b(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vilvl_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vilvl_h(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vilvl_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vilvl_w(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vilvl_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vilvl_d(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vilvl_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vilvh_b(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vilvh_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vilvh_h(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vilvh_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vilvh_w(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vilvh_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vilvh_d(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vilvh_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpickev_b(VRegister vd, VRegister vj,
                                            VRegister vk) {
  return emit(InstReg(op_vpickev_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpickev_h(VRegister vd, VRegister vj,
                                            VRegister vk) {
  return emit(InstReg(op_vpickev_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpickev_w(VRegister vd, VRegister vj,
                                            VRegister vk) {
  return emit(InstReg(op_vpickev_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpickod_b(VRegister vd, VRegister vj,
                                            VRegister vk) {
  return emit(InstReg(op_vpickod_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpickod_h(VRegister vd, VRegister vj,
                                            VRegister vk) {
  return emit(InstReg(op_vpickod_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpickod_w(VRegister vd, VRegister vj,
                                            VRegister vk) {
  return emit(InstReg(op_vpickod_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vand_v(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vand_v, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vor_v(VRegister vd, VRegister vj,
                                        VRegister vk) {
  return emit(InstReg(op_vor_v, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vxor_v(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vxor_v, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vnor_v(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vnor_v, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vandn_v(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vandn_v, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vorn_v(VRegister vd, VRegister vj,
                                         VRegister vk) {
  return emit(InstReg(op_vorn_v, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsigncov_b(VRegister vd, VRegister vj,
                                             VRegister vk) {
  return emit(InstReg(op_vsigncov_b, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsigncov_h(VRegister vd, VRegister vj,
                                             VRegister vk) {
  return emit(InstReg(op_vsigncov_h, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsigncov_w(VRegister vd, VRegister vj,
                                             VRegister vk) {
  return emit(InstReg(op_vsigncov_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsigncov_d(VRegister vd, VRegister vj,
                                             VRegister vk) {
  return emit(InstReg(op_vsigncov_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfadd_s(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vfadd_s, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfadd_d(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vfadd_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfsub_s(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vfsub_s, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfsub_d(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vfsub_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfmul_s(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vfmul_s, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfmul_d(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vfmul_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfdiv_s(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vfdiv_s, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfdiv_d(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vfdiv_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfmax_s(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vfmax_s, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfmax_d(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vfmax_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfmin_s(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vfmin_s, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfmin_d(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vfmin_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfcvt_s_d(VRegister vd, VRegister vj,
                                            VRegister vk) {
  return emit(InstReg(op_vfcvt_s_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vftintrz_w_d(VRegister vd, VRegister vj,
                                               VRegister vk) {
  return emit(InstReg(op_vftintrz_w_d, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vshuf_w(VRegister vd, VRegister vj,
                                          VRegister vk) {
  return emit(InstReg(op_vshuf_w, vk, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vslei_b(VRegister vd, VRegister vj,
                                          int32_t imm5) {
  return emit(InstImm(op_vslei_b, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslei_h(VRegister vd, VRegister vj,
                                          int32_t imm5) {
  return emit(InstImm(op_vslei_h, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslei_w(VRegister vd, VRegister vj,
                                          int32_t imm5) {
  return emit(InstImm(op_vslei_w, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslei_d(VRegister vd, VRegister vj,
                                          int32_t imm5) {
  return emit(InstImm(op_vslei_d, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslei_bu(VRegister vd, VRegister vj,
                                           uint32_t imm5) {
  return emit(InstImm(op_vslei_bu, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslei_hu(VRegister vd, VRegister vj,
                                           uint32_t imm5) {
  return emit(InstImm(op_vslei_hu, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslei_wu(VRegister vd, VRegister vj,
                                           uint32_t imm5) {
  return emit(InstImm(op_vslei_wu, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslei_du(VRegister vd, VRegister vj,
                                           uint32_t imm5) {
  return emit(InstImm(op_vslei_du, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vaddi_bu(VRegister vd, VRegister vj,
                                           uint32_t imm5) {
  return emit(InstImm(op_vaddi_bu, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vseqi_b(VRegister vd, VRegister vj,
                                          int32_t imm5) {
  return emit(InstImm(op_vseqi_b, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vseqi_h(VRegister vd, VRegister vj,
                                          int32_t imm5) {
  return emit(InstImm(op_vseqi_h, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vseqi_w(VRegister vd, VRegister vj,
                                          int32_t imm5) {
  return emit(InstImm(op_vseqi_w, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vseqi_d(VRegister vd, VRegister vj,
                                          int32_t imm5) {
  return emit(InstImm(op_vseqi_d, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslti_b(VRegister vd, VRegister vj,
                                          int32_t imm5) {
  return emit(InstImm(op_vslti_b, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslti_h(VRegister vd, VRegister vj,
                                          int32_t imm5) {
  return emit(InstImm(op_vslti_h, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslti_w(VRegister vd, VRegister vj,
                                          int32_t imm5) {
  return emit(InstImm(op_vslti_w, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslti_d(VRegister vd, VRegister vj,
                                          int32_t imm5) {
  return emit(InstImm(op_vslti_d, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslti_bu(VRegister vd, VRegister vj,
                                           uint32_t imm5) {
  return emit(InstImm(op_vslti_bu, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslti_hu(VRegister vd, VRegister vj,
                                           uint32_t imm5) {
  return emit(InstImm(op_vslti_hu, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslti_wu(VRegister vd, VRegister vj,
                                           uint32_t imm5) {
  return emit(InstImm(op_vslti_wu, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslti_du(VRegister vd, VRegister vj,
                                           uint32_t imm5) {
  return emit(InstImm(op_vslti_du, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vaddi_hu(VRegister vd, VRegister vj,
                                           uint32_t imm5) {
  return emit(InstImm(op_vaddi_hu, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vaddi_wu(VRegister vd, VRegister vj,
                                           uint32_t imm5) {
  return emit(InstImm(op_vaddi_wu, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vaddi_du(VRegister vd, VRegister vj,
                                           uint32_t imm5) {
  return emit(InstImm(op_vaddi_du, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vbsll_v(VRegister vd, VRegister vj,
                                          uint32_t imm5) {
  return emit(InstImm(op_vbsll_v, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vbsrl_v(VRegister vd, VRegister vj,
                                          uint32_t imm5) {
  return emit(InstImm(op_vbsrl_v, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vpcnt_b(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vpcnt_b, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpcnt_h(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vpcnt_h, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpcnt_w(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vpcnt_w, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vpcnt_d(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vpcnt_d, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vneg_b(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vneg_b, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vneg_h(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vneg_h, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vneg_w(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vneg_w, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vneg_d(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vneg_d, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmskltz_b(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vmskltz_b, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmskltz_h(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vmskltz_h, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmskltz_w(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vmskltz_w, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmskltz_d(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vmskltz_d, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmskgez_b(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vmskgez_b, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vmsknz_b(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vmsknz_b, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vsetnez_v(FPConditionBit cd, VRegister vj) {
  return emit(InstReg(op_vsetnez_v, vj, cd).encode());
}

BufferOffset AssemblerLOONG64::as_vsetallnez_b(FPConditionBit cd,
                                               VRegister vj) {
  return emit(InstReg(op_vsetallnez_b, vj, cd).encode());
}

BufferOffset AssemblerLOONG64::as_vsetallnez_h(FPConditionBit cd,
                                               VRegister vj) {
  return emit(InstReg(op_vsetallnez_h, vj, cd).encode());
}

BufferOffset AssemblerLOONG64::as_vsetallnez_w(FPConditionBit cd,
                                               VRegister vj) {
  return emit(InstReg(op_vsetallnez_w, vj, cd).encode());
}

BufferOffset AssemblerLOONG64::as_vsetallnez_d(FPConditionBit cd,
                                               VRegister vj) {
  return emit(InstReg(op_vsetallnez_d, vj, cd).encode());
}

BufferOffset AssemblerLOONG64::as_vfsqrt_s(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vfsqrt_s, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfsqrt_d(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vfsqrt_d, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfrecip_s(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vfrecip_s, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfrsqrt_s(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vfrsqrt_s, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfrintrm_s(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vfrintrm_s, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfrintrm_d(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vfrintrm_d, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfrintrp_s(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vfrintrp_s, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfrintrp_d(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vfrintrp_d, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfrintrz_s(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vfrintrz_s, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfrintrz_d(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vfrintrz_d, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfrintrne_s(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vfrintrne_s, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfrintrne_d(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vfrintrne_d, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vfcvtl_d_s(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vfcvtl_d_s, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vffint_s_w(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vffint_s_w, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vffint_s_wu(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vffint_s_wu, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vffint_d_l(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vffint_d_l, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vffint_d_lu(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vffint_d_lu, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vffintl_d_w(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vffintl_d_w, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vftintrz_w_s(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vftintrz_w_s, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vftintrz_l_d(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vftintrz_l_d, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vftintrz_wu_s(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vftintrz_wu_s, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vftintrz_lu_d(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vftintrz_lu_d, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vexth_h_b(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vexth_h_b, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vexth_w_h(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vexth_w_h, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vexth_d_w(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vexth_d_w, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vexth_hu_bu(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vexth_hu_bu, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vexth_wu_hu(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vexth_wu_hu, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vexth_du_wu(VRegister vd, VRegister vj) {
  return emit(InstReg(op_vexth_du_wu, vj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vreplgr2vr_b(VRegister vd, Register rj) {
  return emit(InstReg(op_vreplgr2vr_b, rj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vreplgr2vr_h(VRegister vd, Register rj) {
  return emit(InstReg(op_vreplgr2vr_h, rj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vreplgr2vr_w(VRegister vd, Register rj) {
  return emit(InstReg(op_vreplgr2vr_w, rj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vreplgr2vr_d(VRegister vd, Register rj) {
  return emit(InstReg(op_vreplgr2vr_d, rj, vd).encode());
}

BufferOffset AssemblerLOONG64::as_vinsgr2vr_b(VRegister vd, Register rj,
                                              uint32_t imm4) {
  return emit(InstImm(op_vinsgr2vr_b, imm4, rj, vd, 4).encode());
}

BufferOffset AssemblerLOONG64::as_vinsgr2vr_h(VRegister vd, Register rj,
                                              uint32_t imm3) {
  return emit(InstImm(op_vinsgr2vr_h, imm3, rj, vd, 3).encode());
}

BufferOffset AssemblerLOONG64::as_vinsgr2vr_w(VRegister vd, Register rj,
                                              uint32_t imm2) {
  return emit(InstImm(op_vinsgr2vr_w, imm2, rj, vd, 2).encode());
}

BufferOffset AssemblerLOONG64::as_vinsgr2vr_d(VRegister vd, Register rj,
                                              uint32_t imm1) {
  return emit(InstImm(op_vinsgr2vr_d, imm1, rj, vd, 1).encode());
}

BufferOffset AssemblerLOONG64::as_vpickve2gr_b(Register rd, VRegister vj,
                                               uint32_t imm4) {
  return emit(InstImm(op_vpickve2gr_b, imm4, vj, rd, 4).encode());
}

BufferOffset AssemblerLOONG64::as_vpickve2gr_h(Register rd, VRegister vj,
                                               uint32_t imm3) {
  return emit(InstImm(op_vpickve2gr_h, imm3, vj, rd, 3).encode());
}

BufferOffset AssemblerLOONG64::as_vpickve2gr_w(Register rd, VRegister vj,
                                               uint32_t imm2) {
  return emit(InstImm(op_vpickve2gr_w, imm2, vj, rd, 2).encode());
}

BufferOffset AssemblerLOONG64::as_vpickve2gr_d(Register rd, VRegister vj,
                                               uint32_t imm1) {
  return emit(InstImm(op_vpickve2gr_d, imm1, vj, rd, 1).encode());
}

BufferOffset AssemblerLOONG64::as_vpickve2gr_bu(Register rd, VRegister vj,
                                                uint32_t imm4) {
  return emit(InstImm(op_vpickve2gr_bu, imm4, vj, rd, 4).encode());
}

BufferOffset AssemblerLOONG64::as_vpickve2gr_hu(Register rd, VRegister vj,
                                                uint32_t imm3) {
  return emit(InstImm(op_vpickve2gr_hu, imm3, vj, rd, 3).encode());
}

BufferOffset AssemblerLOONG64::as_vpickve2gr_wu(Register rd, VRegister vj,
                                                uint32_t imm2) {
  return emit(InstImm(op_vpickve2gr_wu, imm2, vj, rd, 2).encode());
}

BufferOffset AssemblerLOONG64::as_vreplvei_b(VRegister vd, VRegister vj,
                                             uint32_t imm4) {
  return emit(InstImm(op_vreplvei_b, imm4, vj, vd, 4).encode());
}

BufferOffset AssemblerLOONG64::as_vreplvei_h(VRegister vd, VRegister vj,
                                             uint32_t imm3) {
  return emit(InstImm(op_vreplvei_h, imm3, vj, vd, 3).encode());
}

BufferOffset AssemblerLOONG64::as_vreplvei_w(VRegister vd, VRegister vj,
                                             uint32_t imm2) {
  return emit(InstImm(op_vreplvei_w, imm2, vj, vd, 2).encode());
}

BufferOffset AssemblerLOONG64::as_vreplvei_d(VRegister vd, VRegister vj,
                                             uint32_t imm1) {
  return emit(InstImm(op_vreplvei_d, imm1, vj, vd, 1).encode());
}

BufferOffset AssemblerLOONG64::as_vsllwil_h_b(VRegister vd, VRegister vj,
                                              uint32_t imm3) {
  return emit(InstImm(op_vsllwil_h_b, imm3, vj, vd, 3).encode());
}

BufferOffset AssemblerLOONG64::as_vsllwil_w_h(VRegister vd, VRegister vj,
                                              uint32_t imm4) {
  return emit(InstImm(op_vsllwil_w_h, imm4, vj, vd, 4).encode());
}

BufferOffset AssemblerLOONG64::as_vsllwil_d_w(VRegister vd, VRegister vj,
                                              uint32_t imm5) {
  return emit(InstImm(op_vsllwil_d_w, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vsllwil_hu_bu(VRegister vd, VRegister vj,
                                                uint32_t imm3) {
  return emit(InstImm(op_vsllwil_hu_bu, imm3, vj, vd, 3).encode());
}

BufferOffset AssemblerLOONG64::as_vsllwil_wu_hu(VRegister vd, VRegister vj,
                                                uint32_t imm4) {
  return emit(InstImm(op_vsllwil_wu_hu, imm4, vj, vd, 4).encode());
}

BufferOffset AssemblerLOONG64::as_vsllwil_du_wu(VRegister vd, VRegister vj,
                                                uint32_t imm5) {
  return emit(InstImm(op_vsllwil_du_wu, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vbitclri_w(VRegister vd, VRegister vj,
                                             uint32_t imm5) {
  return emit(InstImm(op_vbitclri_w, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vbitclri_d(VRegister vd, VRegister vj,
                                             uint32_t imm6) {
  return emit(InstImm(op_vbitclri_d, imm6, vj, vd, 6).encode());
}

BufferOffset AssemblerLOONG64::as_vbitseti_w(VRegister vd, VRegister vj,
                                             uint32_t imm5) {
  return emit(InstImm(op_vbitseti_w, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vbitrevi_w(VRegister vd, VRegister vj,
                                             uint32_t imm5) {
  return emit(InstImm(op_vbitrevi_w, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vbitrevi_d(VRegister vd, VRegister vj,
                                             uint32_t imm6) {
  return emit(InstImm(op_vbitrevi_d, imm6, vj, vd, 6).encode());
}

BufferOffset AssemblerLOONG64::as_vsat_h(VRegister vd, VRegister vj,
                                         uint32_t imm4) {
  return emit(InstImm(op_vsat_h, imm4, vj, vd, 4).encode());
}

BufferOffset AssemblerLOONG64::as_vsat_w(VRegister vd, VRegister vj,
                                         uint32_t imm5) {
  return emit(InstImm(op_vsat_w, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vsat_d(VRegister vd, VRegister vj,
                                         uint32_t imm6) {
  return emit(InstImm(op_vsat_d, imm6, vj, vd, 6).encode());
}

BufferOffset AssemblerLOONG64::as_vsat_hu(VRegister vd, VRegister vj,
                                          uint32_t imm4) {
  return emit(InstImm(op_vsat_hu, imm4, vj, vd, 4).encode());
}

BufferOffset AssemblerLOONG64::as_vsat_wu(VRegister vd, VRegister vj,
                                          uint32_t imm5) {
  return emit(InstImm(op_vsat_wu, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vsat_du(VRegister vd, VRegister vj,
                                          uint32_t imm6) {
  return emit(InstImm(op_vsat_du, imm6, vj, vd, 6).encode());
}

BufferOffset AssemblerLOONG64::as_vslli_b(VRegister vd, VRegister vj,
                                          uint32_t imm3) {
  return emit(InstImm(op_vslli_b, imm3, vj, vd, 3).encode());
}

BufferOffset AssemblerLOONG64::as_vslli_h(VRegister vd, VRegister vj,
                                          uint32_t imm4) {
  return emit(InstImm(op_vslli_h, imm4, vj, vd, 4).encode());
}

BufferOffset AssemblerLOONG64::as_vslli_w(VRegister vd, VRegister vj,
                                          uint32_t imm5) {
  return emit(InstImm(op_vslli_w, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vslli_d(VRegister vd, VRegister vj,
                                          uint32_t imm6) {
  return emit(InstImm(op_vslli_d, imm6, vj, vd, 6).encode());
}

BufferOffset AssemblerLOONG64::as_vsrli_b(VRegister vd, VRegister vj,
                                          uint32_t imm3) {
  return emit(InstImm(op_vsrli_b, imm3, vj, vd, 3).encode());
}

BufferOffset AssemblerLOONG64::as_vsrli_h(VRegister vd, VRegister vj,
                                          uint32_t imm4) {
  return emit(InstImm(op_vsrli_h, imm4, vj, vd, 4).encode());
}

BufferOffset AssemblerLOONG64::as_vsrli_w(VRegister vd, VRegister vj,
                                          uint32_t imm5) {
  return emit(InstImm(op_vsrli_w, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vsrli_d(VRegister vd, VRegister vj,
                                          uint32_t imm6) {
  return emit(InstImm(op_vsrli_d, imm6, vj, vd, 6).encode());
}

BufferOffset AssemblerLOONG64::as_vsrai_b(VRegister vd, VRegister vj,
                                          uint32_t imm3) {
  return emit(InstImm(op_vsrai_b, imm3, vj, vd, 3).encode());
}

BufferOffset AssemblerLOONG64::as_vsrai_h(VRegister vd, VRegister vj,
                                          uint32_t imm4) {
  return emit(InstImm(op_vsrai_h, imm4, vj, vd, 4).encode());
}

BufferOffset AssemblerLOONG64::as_vsrai_w(VRegister vd, VRegister vj,
                                          uint32_t imm5) {
  return emit(InstImm(op_vsrai_w, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vsrai_d(VRegister vd, VRegister vj,
                                          uint32_t imm6) {
  return emit(InstImm(op_vsrai_d, imm6, vj, vd, 6).encode());
}

BufferOffset AssemblerLOONG64::as_vssrani_b_h(VRegister vd, VRegister vj,
                                              uint32_t imm4) {
  return emit(InstImm(op_vssrani_b_h, imm4, vj, vd, 4).encode());
}

BufferOffset AssemblerLOONG64::as_vssrani_h_w(VRegister vd, VRegister vj,
                                              uint32_t imm5) {
  return emit(InstImm(op_vssrani_h_w, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vssrani_bu_h(VRegister vd, VRegister vj,
                                               uint32_t imm4) {
  return emit(InstImm(op_vssrani_bu_h, imm4, vj, vd, 4).encode());
}

BufferOffset AssemblerLOONG64::as_vssrani_hu_w(VRegister vd, VRegister vj,
                                               uint32_t imm5) {
  return emit(InstImm(op_vssrani_hu_w, imm5, vj, vd, 5).encode());
}

BufferOffset AssemblerLOONG64::as_vextrins_d(VRegister vd, VRegister vj,
                                             uint32_t imm8) {
  return emit(InstImm(op_vextrins_d, imm8, vj, vd, 8).encode());
}

BufferOffset AssemblerLOONG64::as_vextrins_w(VRegister vd, VRegister vj,
                                             uint32_t imm8) {
  return emit(InstImm(op_vextrins_w, imm8, vj, vd, 8).encode());
}

BufferOffset AssemblerLOONG64::as_vextrins_h(VRegister vd, VRegister vj,
                                             uint32_t imm8) {
  return emit(InstImm(op_vextrins_h, imm8, vj, vd, 8).encode());
}

BufferOffset AssemblerLOONG64::as_vextrins_b(VRegister vd, VRegister vj,
                                             uint32_t imm8) {
  return emit(InstImm(op_vextrins_b, imm8, vj, vd, 8).encode());
}

BufferOffset AssemblerLOONG64::as_vshuf4i_b(VRegister vd, VRegister vj,
                                            uint32_t imm8) {
  return emit(InstImm(op_vshuf4i_b, imm8, vj, vd, 8).encode());
}

BufferOffset AssemblerLOONG64::as_vshuf4i_h(VRegister vd, VRegister vj,
                                            uint32_t imm8) {
  return emit(InstImm(op_vshuf4i_h, imm8, vj, vd, 8).encode());
}

BufferOffset AssemblerLOONG64::as_vshuf4i_w(VRegister vd, VRegister vj,
                                            uint32_t imm8) {
  return emit(InstImm(op_vshuf4i_w, imm8, vj, vd, 8).encode());
}

BufferOffset AssemblerLOONG64::as_vshuf4i_d(VRegister vd, VRegister vj,
                                            uint32_t imm8) {
  return emit(InstImm(op_vshuf4i_d, imm8, vj, vd, 8).encode());
}

BufferOffset AssemblerLOONG64::as_vori_b(VRegister vd, VRegister vj,
                                         uint32_t imm8) {
  return emit(InstImm(op_vori_b, imm8, vj, vd, 8).encode());
}

BufferOffset AssemblerLOONG64::as_vldi(VRegister vd, int32_t imm13) {
  return emit(InstImm(op_vldi, imm13, vd).encode());
}

namespace {

template <typename LanesT>
static bool LanesAllEqual(const LanesT& lanes) {
  return std::all_of(std::begin(lanes), std::end(lanes),
                     [&](const auto& lane) { return lane == lanes[0]; });
}

constexpr uint32_t Data10Mask = (1 << 10) - 1;
constexpr uint32_t Data32Bits = 32;
constexpr uint32_t Imm6Mask = (1 << 6) - 1;
constexpr uint32_t Imm9Mask = (1 << 9) - 1;
constexpr uint32_t Imm6Shift = 6;
constexpr uint32_t Imm7Shift = 7;
constexpr uint32_t SignBit32Shift = 31;
constexpr uint32_t SignBit64Shift = 63;

// Minifloat is a small-magnitude, 8-bit FP format that can be packed into the
// vldi immediate. It has 1 sign bit, 3 exponent bits (bias=2), and 4 mantissa
// bits. Zeros are not supported. The following constants are for determining
// if a "normal" IEEE 754 binary32 or binary64 can be represented as a
// Minifloat.
constexpr uint32_t MinifloatF32ExponentShift = 25;
constexpr uint32_t MinifloatF32MantissaShift = 19;
constexpr uint32_t MinifloatF32TrailingMask =
    (1 << MinifloatF32MantissaShift) - 1;
constexpr uint32_t MinifloatF32ExponentImm6Clear = 0b100000;
constexpr uint32_t MinifloatF32ExponentImm6Set = 0b011111;
constexpr uint32_t MinifloatF64ExponentShift = 54;
constexpr uint32_t MinifloatF64MantissaShift = 48;
constexpr uint64_t MinifloatF64TrailingMask =
    (UINT64_C(1) << MinifloatF64MantissaShift) - 1;
constexpr uint32_t MinifloatF64ExponentImm6Clear = 0b100000000;
constexpr uint32_t MinifloatF64ExponentImm6Set = 0b011111111;
// BroadcastVldiMinifloatToEvenF32 repeats (payload, 0) f32 pairs, so its high
// word must be zero, and the f32 minifloat pattern sits in the low word.
constexpr uint64_t EvenF32HighWordMask = ((UINT64_C(1) << Data32Bits) - 1)
                                         << Data32Bits;

constexpr uint8_t ByteAt(uint64_t value, uint32_t index) {
  return static_cast<uint8_t>((value >> (8 * index)) & 0xFF);
}

constexpr std::optional<uint16_t> MinifloatF32Data(uint32_t word) {
  const uint32_t exponent = (word >> MinifloatF32ExponentShift) & Imm6Mask;
  if ((word & MinifloatF32TrailingMask) != 0 ||
      (exponent != MinifloatF32ExponentImm6Clear &&
       exponent != MinifloatF32ExponentImm6Set)) {
    return std::nullopt;
  }
  const uint32_t imm6 = exponent == MinifloatF32ExponentImm6Set ? 1 : 0;
  return static_cast<uint16_t>(
      ((word >> SignBit32Shift) << Imm7Shift) | (imm6 << Imm6Shift) |
      ((word >> MinifloatF32MantissaShift) & Imm6Mask));
}

template <uint8_t ModeImm, typename Derived>
class VldiMode {
 public:
  template <typename ViewT>
  static std::optional<uint16_t> encode(const ViewT& lanes) {
    if (const auto data = Derived::tryEncode(lanes)) {
      return static_cast<uint16_t>((ModeImm << 8) | *data);
    }
    return std::nullopt;
  }
};

// Function names and patterns follow the "VLDI Helper" from "AreWeLoongYet?".
// <https://areweloongyet.com/asmdb/vldiHelper>
// <https://github.com/loongson-community/areweloongyet/blob/49a0f6d22506c1fbebc97da24d4c34a9cc116ee5/src/components/AsmDB/vldi.ts>

// BroadcastU8To8: Simple case where all bytes are equal.
class VldiModeBroadcastU8To8
    : public VldiMode<0b00000, VldiModeBroadcastU8To8> {
  friend class VldiMode<0b00000, VldiModeBroadcastU8To8>;

  using ViewT = SimdConstant::I8x16;

  static std::optional<uint16_t> tryEncode(const ViewT& bytes) {
    if (!LanesAllEqual(bytes)) {
      return std::nullopt;
    }
    return static_cast<uint8_t>(bytes[0]);
  }
};

// BroadcastS10To64: This mode sign-extends simm10 to 64-bit, then broadcasts.
class VldiModeBroadcastS10To64
    : public VldiMode<0b01100, VldiModeBroadcastS10To64> {
  friend class VldiMode<0b01100, VldiModeBroadcastS10To64>;

  using ViewT = SimdConstant::I64x2;

  static std::optional<uint16_t> tryEncode(const ViewT& lanes) {
    if (!LanesAllEqual(lanes) || !is_intN(lanes[0], 10)) {
      return std::nullopt;
    }
    return static_cast<uint64_t>(lanes[0]) & Data10Mask;
  }
};

// BroadcastBitExpandedU8To64: This mode unpacks payload bit |i| into byte |i|,
// so every byte is 0x00 or 0xFF.
class VldiModeBroadcastBitExpandedU8To64
    : public VldiMode<0b11001, VldiModeBroadcastBitExpandedU8To64> {
  friend class VldiMode<0b11001, VldiModeBroadcastBitExpandedU8To64>;

  using ViewT = SimdConstant::I64x2;

  static std::optional<uint16_t> tryEncode(const ViewT& lanes) {
    if (!LanesAllEqual(lanes)) {
      return std::nullopt;
    }
    const uint64_t dword = static_cast<uint64_t>(lanes[0]);
    bool expandable = true;
    uint32_t data = 0;
    for (uint8_t i = 0; i < 8; i++) {
      const uint8_t byte = ByteAt(dword, i);
      const bool set = byte == 0xFF;
      expandable &= (set || byte == 0);
      data |= (static_cast<uint32_t>(set) << i);
    }
    return expandable ? std::make_optional(static_cast<uint16_t>(data))
                      : std::nullopt;
  }
};

// BroadcastVldiMinifloatToF64: This mode unpacks and broadcasts binary64 from a
// Minifloat.
class VldiModeBroadcastVldiMinifloatToF64
    : public VldiMode<0b11100, VldiModeBroadcastVldiMinifloatToF64> {
  friend class VldiMode<0b11100, VldiModeBroadcastVldiMinifloatToF64>;

  using ViewT = SimdConstant::I64x2;

  static std::optional<uint16_t> tryEncode(const ViewT& lanes) {
    if (!LanesAllEqual(lanes)) {
      return std::nullopt;
    }
    const uint64_t dword = static_cast<uint64_t>(lanes[0]);
    const uint64_t exponent = (dword >> MinifloatF64ExponentShift) & Imm9Mask;
    if ((dword & MinifloatF64TrailingMask) != 0 ||
        (exponent != MinifloatF64ExponentImm6Clear &&
         exponent != MinifloatF64ExponentImm6Set)) {
      return std::nullopt;
    }
    const uint32_t imm6 = exponent == MinifloatF64ExponentImm6Set ? 1 : 0;
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(dword >> SignBit64Shift) << Imm7Shift) |
        (imm6 << Imm6Shift) |
        static_cast<uint32_t>((dword >> MinifloatF64MantissaShift) & Imm6Mask));
  }
};

// BroadcastVldiMinifloatToEvenF32: This mode unpacks a Minifloat into binary32,
// then broadcast 64-bit (value.f32, 0.f32) pairs. The net effect is lane 2*N
// being the unpacked value, and lane 2*N+1 being zero.
class VldiModeBroadcastVldiMinifloatToEvenF32
    : public VldiMode<0b11011, VldiModeBroadcastVldiMinifloatToEvenF32> {
  friend class VldiMode<0b11011, VldiModeBroadcastVldiMinifloatToEvenF32>;

  using ViewT = SimdConstant::I64x2;

  static std::optional<uint16_t> tryEncode(const ViewT& lanes) {
    if (!LanesAllEqual(lanes)) {
      return std::nullopt;
    }
    const uint64_t dword = static_cast<uint64_t>(lanes[0]);
    if ((dword & EvenF32HighWordMask) != 0) {
      return std::nullopt;
    }
    return MinifloatF32Data(static_cast<uint32_t>(dword));
  }
};

// BroadcastS10To32: This mode sign-extends simm10 to 32-bit, then broadcasts.
class VldiModeBroadcastS10To32
    : public VldiMode<0b01000, VldiModeBroadcastS10To32> {
  friend class VldiMode<0b01000, VldiModeBroadcastS10To32>;

  using ViewT = SimdConstant::I32x4;

  static std::optional<uint16_t> tryEncode(const ViewT& lanes) {
    if (!LanesAllEqual(lanes) || !is_intN(lanes[0], 10)) {
      return std::nullopt;
    }
    return static_cast<uint32_t>(lanes[0]) & Data10Mask;
  }
};

// BroadcastU8Shl24To32: This mode unpacks words with only 1 non-zero aligned
// byte, i.e. patterned /0x(00)(00)(00)(..)/, then broadcasts.
class VldiModeBroadcastU8Shl24To32
    : public VldiMode<0b10011, VldiModeBroadcastU8Shl24To32> {
  friend class VldiMode<0b10011, VldiModeBroadcastU8Shl24To32>;

  using ViewT = SimdConstant::I32x4;

  static std::optional<uint16_t> tryEncode(const ViewT& lanes) {
    if (!LanesAllEqual(lanes)) {
      return std::nullopt;
    }
    const uint32_t word = static_cast<uint32_t>(lanes[0]);
    if (ByteAt(word, 0) != 0 || ByteAt(word, 1) != 0 || ByteAt(word, 2) != 0) {
      return std::nullopt;
    }
    return ByteAt(word, 3);
  }
};

// BroadcastU8Shl16To32: This mode unpacks words with only 1 non-zero aligned
// byte, i.e. patterned /0x(00)(00)(..)(00)/, then broadcasts.
class VldiModeBroadcastU8Shl16To32
    : public VldiMode<0b10010, VldiModeBroadcastU8Shl16To32> {
  friend class VldiMode<0b10010, VldiModeBroadcastU8Shl16To32>;

  using ViewT = SimdConstant::I32x4;

  static std::optional<uint16_t> tryEncode(const ViewT& lanes) {
    if (!LanesAllEqual(lanes)) {
      return std::nullopt;
    }
    const uint32_t word = static_cast<uint32_t>(lanes[0]);
    if (ByteAt(word, 0) != 0 || ByteAt(word, 1) != 0 || ByteAt(word, 3) != 0) {
      return std::nullopt;
    }
    return ByteAt(word, 2);
  }
};

// BroadcastU8Shl8To32: This mode unpacks words with only 1 non-zero aligned
// byte, i.e. patterned /0x(00)(..)(00)(00)/, then broadcasts.
class VldiModeBroadcastU8Shl8To32
    : public VldiMode<0b10001, VldiModeBroadcastU8Shl8To32> {
  friend class VldiMode<0b10001, VldiModeBroadcastU8Shl8To32>;

  using ViewT = SimdConstant::I32x4;

  static std::optional<uint16_t> tryEncode(const ViewT& lanes) {
    if (!LanesAllEqual(lanes)) {
      return std::nullopt;
    }
    const uint32_t word = static_cast<uint32_t>(lanes[0]);
    if (ByteAt(word, 0) != 0 || ByteAt(word, 2) != 0 || ByteAt(word, 3) != 0) {
      return std::nullopt;
    }
    return ByteAt(word, 1);
  }
};

// BroadcastU8FFFFTo32: This mode unpacks words patterned /0x(00)(..)(FF)(FF)/,
// then broadcasts.
class VldiModeBroadcastU8FFFFTo32
    : public VldiMode<0b10111, VldiModeBroadcastU8FFFFTo32> {
  friend class VldiMode<0b10111, VldiModeBroadcastU8FFFFTo32>;

  using ViewT = SimdConstant::I32x4;

  static std::optional<uint16_t> tryEncode(const ViewT& lanes) {
    if (!LanesAllEqual(lanes)) {
      return std::nullopt;
    }
    const uint32_t word = static_cast<uint32_t>(lanes[0]);
    if (ByteAt(word, 3) != 0 || ByteAt(word, 1) != 0xFF ||
        ByteAt(word, 0) != 0xFF) {
      return std::nullopt;
    }
    return ByteAt(word, 2);
  }
};

// BroadcastU8FFTo32: This mode unpacks words patterned /0x(00)(00)(..)(FF)/,
// then broadcasts.
class VldiModeBroadcastU8FFTo32
    : public VldiMode<0b10110, VldiModeBroadcastU8FFTo32> {
  friend class VldiMode<0b10110, VldiModeBroadcastU8FFTo32>;

  using ViewT = SimdConstant::I32x4;

  static std::optional<uint16_t> tryEncode(const ViewT& lanes) {
    if (!LanesAllEqual(lanes)) {
      return std::nullopt;
    }
    const uint32_t word = static_cast<uint32_t>(lanes[0]);
    if (ByteAt(word, 3) != 0 || ByteAt(word, 2) != 0 ||
        ByteAt(word, 0) != 0xFF) {
      return std::nullopt;
    }
    return ByteAt(word, 1);
  }
};

// BroadcastVldiMinifloatToF32: This mode unpacks and broadcasts binary32 from a
// Minifloat.
class VldiModeBroadcastVldiMinifloatToF32
    : public VldiMode<0b11010, VldiModeBroadcastVldiMinifloatToF32> {
  friend class VldiMode<0b11010, VldiModeBroadcastVldiMinifloatToF32>;

  using ViewT = SimdConstant::I32x4;

  static std::optional<uint16_t> tryEncode(const ViewT& lanes) {
    if (!LanesAllEqual(lanes)) {
      return std::nullopt;
    }
    return MinifloatF32Data(static_cast<uint32_t>(lanes[0]));
  }
};

// BroadcastS10To16: This mode sign-extends simm10 to 16-bit, then broadcasts.
class VldiModeBroadcastS10To16
    : public VldiMode<0b00100, VldiModeBroadcastS10To16> {
  friend class VldiMode<0b00100, VldiModeBroadcastS10To16>;

  using ViewT = SimdConstant::I16x8;

  static std::optional<uint16_t> tryEncode(const ViewT& lanes) {
    if (!LanesAllEqual(lanes) || !is_intN(lanes[0], 10)) {
      return std::nullopt;
    }
    return static_cast<uint16_t>(lanes[0]) & Data10Mask;
  }
};

// BroadcastU8Shl8To16: This mode unpacks and broadcasts half-words like
// /0x(..)(00)/.
class VldiModeBroadcastU8Shl8To16
    : public VldiMode<0b10101, VldiModeBroadcastU8Shl8To16> {
  friend class VldiMode<0b10101, VldiModeBroadcastU8Shl8To16>;

  using ViewT = SimdConstant::I16x8;

  static std::optional<uint16_t> tryEncode(const ViewT& lanes) {
    if (!LanesAllEqual(lanes)) {
      return std::nullopt;
    }
    const uint16_t half = static_cast<uint16_t>(lanes[0]);
    if ((half & 0xFF) != 0) {
      return std::nullopt;
    }
    return half >> 8;
  }
};

// BroadcastU8To16: This mode unpacks and broadcasts half-words like
// /0x(00)(..)/.
class VldiModeBroadcastU8To16
    : public VldiMode<0b10100, VldiModeBroadcastU8To16> {
  friend class VldiMode<0b10100, VldiModeBroadcastU8To16>;

  using ViewT = SimdConstant::I16x8;

  static std::optional<uint16_t> tryEncode(const ViewT& lanes) {
    if (!LanesAllEqual(lanes)) {
      return std::nullopt;
    }
    const uint16_t half = static_cast<uint16_t>(lanes[0]);
    return half <= 0xFF ? std::make_optional(half) : std::nullopt;
  }
};

}  // namespace

/* static */
std::optional<uint16_t> AssemblerLOONG64::EncodeVldiImmediate(
    const SimdConstant& v) {
  const SimdConstant shape64 =
      SimdConstant::CreateX2(reinterpret_cast<const int64_t*>(v.bytes()));
  const SimdConstant shape32 =
      SimdConstant::CreateX4(reinterpret_cast<const int32_t*>(v.bytes()));
  const SimdConstant shape16 =
      SimdConstant::CreateX8(reinterpret_cast<const int16_t*>(v.bytes()));
  const SimdConstant shape8 =
      SimdConstant::CreateX16(reinterpret_cast<const int8_t*>(v.bytes()));
  const auto& lanes64 = shape64.asInt64x2();
  const auto& lanes32 = shape32.asInt32x4();
  const auto& lanes16 = shape16.asInt16x8();
  const auto& bytes = shape8.asInt8x16();

  // === 8-BIT MODES ===

  if (auto imm = VldiModeBroadcastU8To8::encode(bytes)) {
    return *imm;
  }

  // Every remaining function broadcasts one 64-bit pattern, so fast-fail if the
  // upper and lower 64-bit lanes don't match.
  if (lanes64[0] != lanes64[1]) {
    return std::nullopt;
  }

  // === 64-BIT MODES ===

  if (auto imm = VldiModeBroadcastS10To64::encode(lanes64)) {
    return *imm;
  }
  if (auto imm = VldiModeBroadcastBitExpandedU8To64::encode(lanes64)) {
    return *imm;
  }
  if (auto imm = VldiModeBroadcastVldiMinifloatToF64::encode(lanes64)) {
    return *imm;
  }
  if (auto imm = VldiModeBroadcastVldiMinifloatToEvenF32::encode(lanes64)) {
    return *imm;
  }

  // === 32-BIT MODES ===

  if (auto imm = VldiModeBroadcastS10To32::encode(lanes32)) {
    return *imm;
  }
  if (auto imm = VldiModeBroadcastU8Shl24To32::encode(lanes32)) {
    return *imm;
  }
  if (auto imm = VldiModeBroadcastU8Shl16To32::encode(lanes32)) {
    return *imm;
  }
  if (auto imm = VldiModeBroadcastU8Shl8To32::encode(lanes32)) {
    return *imm;
  }
  if (auto imm = VldiModeBroadcastU8FFFFTo32::encode(lanes32)) {
    return *imm;
  }
  if (auto imm = VldiModeBroadcastU8FFTo32::encode(lanes32)) {
    return *imm;
  }
  if (auto imm = VldiModeBroadcastVldiMinifloatToF32::encode(lanes32)) {
    return *imm;
  }

  // === 16-BIT MODES ===

  if (auto imm = VldiModeBroadcastS10To16::encode(lanes16)) {
    return *imm;
  }
  if (auto imm = VldiModeBroadcastU8Shl8To16::encode(lanes16)) {
    return *imm;
  }
  if (auto imm = VldiModeBroadcastU8To16::encode(lanes16)) {
    return *imm;
  }

  return std::nullopt;
}

/* static */ bool AssemblerLOONG64::CanEncodeSimdCompareImmediateFor(
    wasm::SimdOp op, const SimdConstant& value) {
  // vseqi/vslei/vslti encode a 5-bit lane immediate, where .[bhwd] variants are
  // sign-extended and .[bhwd]u are zero-extended. Must match the isel in
  // CodeGenerator::visitWasmBinarySimd128WithConstant().

  // (signed, width)
  std::optional<std::tuple<bool, uint8_t> > instrInfo;
  switch (op) {
    case wasm::SimdOp::I8x16LtU:
    case wasm::SimdOp::I8x16GtU:
    case wasm::SimdOp::I8x16LeU:
    case wasm::SimdOp::I8x16GeU:
      instrInfo = std::make_tuple(false, 8);
      break;
    case wasm::SimdOp::I8x16Eq:
    case wasm::SimdOp::I8x16Ne:
    case wasm::SimdOp::I8x16LtS:
    case wasm::SimdOp::I8x16GtS:
    case wasm::SimdOp::I8x16LeS:
    case wasm::SimdOp::I8x16GeS:
      instrInfo = std::make_tuple(true, 8);
      break;
    case wasm::SimdOp::I16x8LtU:
    case wasm::SimdOp::I16x8GtU:
    case wasm::SimdOp::I16x8LeU:
    case wasm::SimdOp::I16x8GeU:
      instrInfo = std::make_tuple(false, 16);
      break;
    case wasm::SimdOp::I16x8Eq:
    case wasm::SimdOp::I16x8Ne:
    case wasm::SimdOp::I16x8LtS:
    case wasm::SimdOp::I16x8GtS:
    case wasm::SimdOp::I16x8LeS:
    case wasm::SimdOp::I16x8GeS:
      instrInfo = std::make_tuple(true, 16);
      break;
    case wasm::SimdOp::I32x4LtU:
    case wasm::SimdOp::I32x4GtU:
    case wasm::SimdOp::I32x4LeU:
    case wasm::SimdOp::I32x4GeU:
      instrInfo = std::make_tuple(false, 32);
      break;
    case wasm::SimdOp::I32x4Eq:
    case wasm::SimdOp::I32x4Ne:
    case wasm::SimdOp::I32x4LtS:
    case wasm::SimdOp::I32x4GtS:
    case wasm::SimdOp::I32x4LeS:
    case wasm::SimdOp::I32x4GeS:
      instrInfo = std::make_tuple(true, 32);
      break;
    case wasm::SimdOp::I64x2Eq:
    case wasm::SimdOp::I64x2Ne:
    case wasm::SimdOp::I64x2LtS:
    case wasm::SimdOp::I64x2GtS:
    case wasm::SimdOp::I64x2LeS:
    case wasm::SimdOp::I64x2GeS:
      instrInfo = std::make_tuple(true, 64);
      break;
    default:
      return false;
  }

  const auto& [isSigned, width] = *instrInfo;
  mozilla::Maybe<int64_t> splat = mozilla::Nothing();
  switch (width) {
    case 8:
      splat = value.splatIntValue<int8_t>().map(
          [](int8_t v) { return static_cast<int64_t>(v); });
      break;
    case 16:
      splat = value.splatIntValue<int16_t>().map(
          [](int16_t v) { return static_cast<int64_t>(v); });
      break;
    case 32:
      splat = value.splatIntValue<int32_t>().map(
          [](int32_t v) { return static_cast<int64_t>(v); });
      break;
    case 64:
      splat = value.splatIntValue<int64_t>();
      break;
    default:
      break;
  }
  if (!splat) {
    return false;
  }
  // [us]i5 immediate range.
  return isSigned ? is_intN(*splat, 5) : is_uintN(*splat, 5);
}

template <typename T>
/* static */ std::optional<int64_t>
AssemblerLOONG64::EncodeSimdCompareImmediate(const SimdConstant& rhs,
                                             Assembler::Condition cond) {
  const mozilla::Maybe<int64_t> splat =
      rhs.splatIntValue<T>().map([](T v) { return static_cast<int64_t>(v); });
  if (!splat) {
    return std::nullopt;
  }
  switch (cond) {
    case Assembler::Equal:
    case Assembler::NotEqual:
    case Assembler::LessThan:
    case Assembler::LessThanOrEqual:
    case Assembler::GreaterThan:
    case Assembler::GreaterThanOrEqual:
      return is_intN(*splat, 5) ? std::make_optional(*splat) : std::nullopt;
    case Assembler::Below:
    case Assembler::BelowOrEqual:
    case Assembler::Above:
    case Assembler::AboveOrEqual:
      return is_uintN(*splat, 5) ? std::make_optional(*splat) : std::nullopt;
    default:
      MOZ_CRASH("unexpected condition");
  }
}
template std::optional<int64_t>
AssemblerLOONG64::EncodeSimdCompareImmediate<int8_t>(const SimdConstant& rhs,
                                                     Condition cond);
template std::optional<int64_t>
AssemblerLOONG64::EncodeSimdCompareImmediate<int16_t>(const SimdConstant& rhs,
                                                      Condition cond);
template std::optional<int64_t>
AssemblerLOONG64::EncodeSimdCompareImmediate<int32_t>(const SimdConstant& rhs,
                                                      Condition cond);
template std::optional<int64_t>
AssemblerLOONG64::EncodeSimdCompareImmediate<int64_t>(const SimdConstant& rhs,
                                                      Condition cond);

template <uint32_t Scale>
/* static */ std::optional<int32_t> AssemblerLOONG64::EncodeVldreplImmediate(
    int64_t offset) {
  static_assert(Scale <= 3);
  constexpr int64_t kUnit = INT64_C(1) << Scale;
  if (offset % kUnit != 0) {
    return std::nullopt;
  }
  const int64_t value = offset / kUnit;
  return is_intN(value, 12 - Scale)
             ? std::make_optional(static_cast<int32_t>(value))
             : std::nullopt;
}
template std::optional<int32_t> AssemblerLOONG64::EncodeVldreplImmediate<0>(
    int64_t offset);
template std::optional<int32_t> AssemblerLOONG64::EncodeVldreplImmediate<1>(
    int64_t offset);
template std::optional<int32_t> AssemblerLOONG64::EncodeVldreplImmediate<2>(
    int64_t offset);
template std::optional<int32_t> AssemblerLOONG64::EncodeVldreplImmediate<3>(
    int64_t offset);

template <uint32_t Scale>
/* static */ std::optional<int32_t>
AssemblerLOONG64::EncodeVstelmOffsetImmediate(int64_t offset) {
  static_assert(Scale <= 3);
  constexpr int64_t kUnit = INT64_C(1) << Scale;
  if (offset % kUnit != 0) {
    return std::nullopt;
  }
  const int64_t value = offset / kUnit;
  return is_intN(value, 8) ? std::make_optional(static_cast<int32_t>(value))
                           : std::nullopt;
}
template std::optional<int32_t>
AssemblerLOONG64::EncodeVstelmOffsetImmediate<0>(int64_t offset);
template std::optional<int32_t>
AssemblerLOONG64::EncodeVstelmOffsetImmediate<1>(int64_t offset);
template std::optional<int32_t>
AssemblerLOONG64::EncodeVstelmOffsetImmediate<2>(int64_t offset);
template std::optional<int32_t>
AssemblerLOONG64::EncodeVstelmOffsetImmediate<3>(int64_t offset);

/* ========================================================================= */

void AssemblerLOONG64::bind(Label* label, BufferOffset boff) {
#ifdef JS_DISASM_LOONG64
  spew_.spewBind(label);
#endif
  // If our caller didn't give us an explicit target to bind to
  // then we want to bind to the location of the next instruction
  BufferOffset dest = boff.assigned() ? boff : nextOffset();
  if (label->used()) {
    int32_t next;

    // A used label holds a link to branch that uses it.
    BufferOffset b(label);
    do {
      // Even a 0 offset may be invalid if we're out of memory.
      if (oom()) {
        return;
      }

      Instruction* inst = editSrc(b);

      // Second word holds a pointer to the next branch in label's chain.
      next = inst[1].encode();
      bind(reinterpret_cast<InstImm*>(inst), b.getOffset(), dest.getOffset());

      b = BufferOffset(next);
    } while (next != LabelBase::INVALID_OFFSET);
  }
  label->bind(dest.getOffset());
}

void AssemblerLOONG64::retarget(Label* label, Label* target) {
#ifdef JS_DISASM_LOONG64
  spew_.spewRetarget(label, target);
#endif
  if (label->used() && !oom()) {
    if (target->bound()) {
      bind(label, BufferOffset(target));
    } else if (target->used()) {
      // The target is not bound but used. Prepend label's branch list
      // onto target's.
      int32_t next;
      BufferOffset labelBranchOffset(label);

      // Find the head of the use chain for label.
      do {
        Instruction* inst = editSrc(labelBranchOffset);

        // Second word holds a pointer to the next branch in chain.
        next = inst[1].encode();
        labelBranchOffset = BufferOffset(next);
      } while (next != LabelBase::INVALID_OFFSET);

      // Then patch the head of label's use chain to the tail of
      // target's use chain, prepending the entire use chain of target.
      Instruction* inst = editSrc(labelBranchOffset);
      int32_t prev = target->offset();
      target->use(label->offset());
      inst[1].setData(prev);
    } else {
      // The target is unbound and unused.  We can just take the head of
      // the list hanging off of label, and dump that into target.
      target->use(label->offset());
    }
  }
  label->reset();
}

void dbg_break() {}

void AssemblerLOONG64::as_break(uint32_t code) {
  MOZ_ASSERT(code <= MAX_BREAK_CODE);
  emit(InstImm(op_break, code).encode());
}

// This just stomps over memory with 32 bits of raw data. Its purpose is to
// overwrite the call of JITed code with 32 bits worth of an offset. This will
// is only meant to function on code that has been invalidated, so it should
// be totally safe. Since that instruction will never be executed again, a
// ICache flush should not be necessary
void AssemblerLOONG64::PatchWrite_Imm32(CodeLocationLabel label, Imm32 imm) {
  // Raw is going to be the return address.
  uint32_t* raw = (uint32_t*)label.raw();
  // Overwrite the 4 bytes before the return address, which will
  // end up being the call instruction.
  *(raw - 1) = imm.value;
}

uint8_t* AssemblerLOONG64::NextInstruction(uint8_t* inst_, uint32_t* count) {
  Instruction* inst = reinterpret_cast<Instruction*>(inst_);
  if (count != nullptr) {
    *count += sizeof(Instruction);
  }
  return reinterpret_cast<uint8_t*>(inst->next());
}

void AssemblerLOONG64::ToggleToJmp(CodeLocationLabel inst_) {
  InstImm* inst = (InstImm*)inst_.raw();

  MOZ_ASSERT(inst->extractBitField(31, 26) == (uint32_t)op_addu16i_d >> 26);
  // We converted beq to addu16i_d, so now we restore it.
  inst->setOpcode(op_beq, 6);
}

void AssemblerLOONG64::ToggleToCmp(CodeLocationLabel inst_) {
  InstImm* inst = (InstImm*)inst_.raw();

  // toggledJump is allways used for short jumps.
  MOZ_ASSERT(inst->extractBitField(31, 26) == (uint32_t)op_beq >> 26);
  // Replace "beq $zero, $zero, offset" with "addu16i_d $zero, $zero, offset"
  inst->setOpcode(op_addu16i_d, 6);
}

// Since there are no pools in LoongArch64 implementation, this should be
// simple.
Instruction* Instruction::next() { return this + 1; }

InstImm AssemblerLOONG64::invertBranch(InstImm branch, int32_t skipOffset) {
  uint32_t rj = 0;
  const OpcodeField opcode =
      static_cast<OpcodeField>((branch.extractBitField(31, 26)) << 26);
  switch (opcode) {
    case op_beq:
      branch.setBOffImm16(BOffImm16(skipOffset));
      branch.setOpcode(op_bne, 6);
      return branch;
    case op_bne:
      branch.setBOffImm16(BOffImm16(skipOffset));
      branch.setOpcode(op_beq, 6);
      return branch;
    case op_bge:
      branch.setBOffImm16(BOffImm16(skipOffset));
      branch.setOpcode(op_blt, 6);
      return branch;
    case op_bgeu:
      branch.setBOffImm16(BOffImm16(skipOffset));
      branch.setOpcode(op_bltu, 6);
      return branch;
    case op_blt:
      branch.setBOffImm16(BOffImm16(skipOffset));
      branch.setOpcode(op_bge, 6);
      return branch;
    case op_bltu:
      branch.setBOffImm16(BOffImm16(skipOffset));
      branch.setOpcode(op_bgeu, 6);
      return branch;
    case op_beqz:
      branch.setBOffImm21(BOffImm21(skipOffset));
      branch.setOpcode(op_bnez, 6);
      return branch;
    case op_bnez:
      branch.setBOffImm21(BOffImm21(skipOffset));
      branch.setOpcode(op_beqz, 6);
      return branch;
    case op_bcz:
      branch.setBOffImm21(BOffImm21(skipOffset));
      rj = branch.extractRJ();
      if (rj & 0x8) {
        branch.setRJ(rj & 0x17);
      } else {
        branch.setRJ(rj | 0x8);
      }
      return branch;
    default:
      MOZ_CRASH("Error creating long branch.");
  }
}

void Assembler::executableCopy(uint8_t* buffer) {
  MOZ_ASSERT(isFinished);
  m_buffer.executableCopy(buffer);

  for (const RelativePatch& rp : jumps_) {
    if (rp.kind != RelocationKind::JITCODE) {
      continue;
    }

    InstImm* const inst = (InstImm*)(buffer + rp.offset.getOffset());

    MOZ_ASSERT(inst[0].extractBitField(31, 25) ==
               (static_cast<uint32_t>(op_pcaddu18i) >> 25));

    const Register scratch = Register::FromCode(inst[0].extractRD());

    const int64_t offset =
        reinterpret_cast<int64_t>(rp.target) -
        reinterpret_cast<int64_t>(buffer + rp.offset.getOffset());
    const auto [si20, offs16] = SplitJump36Offset(offset);
    inst[0] = InstImm(op_pcaddu18i, si20, scratch, false);
    if (inst[1].extractBitField(31, 26) ==
        (static_cast<uint32_t>(op_bne) >> 26)) {
      // Toggled-off call. Keep the residual in the never-taken branch.
      inst[1] = InstImm(op_bne, BOffImm16(offs16), zero, zero);
    } else {
      MOZ_ASSERT(inst[1].extractBitField(31, 26) ==
                 (static_cast<uint32_t>(op_jirl) >> 26));
      const Register rd = Register::FromCode(inst[1].extractRD());
      inst[1] = InstImm(op_jirl, BOffImm16(offs16), scratch, rd);
    }
  }
}

uintptr_t Assembler::GetPointer(uint8_t* instPtr) {
  Instruction* inst = (Instruction*)instPtr;
  return Assembler::ExtractLoad64Value(inst);
}

static JitCode* CodeFromJump(Instruction* jump) {
  // The pair is [pcaddu18i][jirl], or [pcaddu18i][bne] when the call is
  // toggled off; both carry the residual in their immediate field.
  InstImm* const i0 = (InstImm*)jump;
  InstImm* const i1 = (InstImm*)i0->next();
  MOZ_ASSERT((i0->extractBitField(31, 25)) ==
             (static_cast<uint32_t>(op_pcaddu18i) >> 25));
  const int32_t si20 =
      (static_cast<int32_t>(i0->extractBitField(24, 5)) << 12) >> 12;
  const int64_t target = reinterpret_cast<int64_t>(jump) +
                         (static_cast<int64_t>(si20) << 18) +
                         BOffImm16(*i1).decode();
  return JitCode::FromExecutable(reinterpret_cast<uint8_t*>(target));
}

void Assembler::TraceJumpRelocations(JSTracer* trc, JitCode* code,
                                     CompactBufferReader& reader) {
  while (reader.more()) {
    JitCode* child =
        CodeFromJump((Instruction*)(code->raw() + reader.readUnsigned()));
    TraceManuallyBarrieredEdge(trc, &child, "rel32");
  }
}

static void TraceOneDataRelocation(JSTracer* trc,
                                   mozilla::Maybe<AutoWritableJitCode>& awjc,
                                   JitCode* code, Instruction* inst) {
  void* ptr = (void*)Assembler::ExtractLoad64Value(inst);
  void* prior = ptr;

  // Data relocations can be for Values or for raw pointers. If a Value is
  // zero-tagged, we can trace it as if it were a raw pointer. If a Value
  // is not zero-tagged, we have to interpret it as a Value to ensure that the
  // tag bits are masked off to recover the actual pointer.
  uintptr_t word = reinterpret_cast<uintptr_t>(ptr);
  if (word >> JSVAL_TAG_SHIFT) {
    // This relocation is a Value with a non-zero tag.
    Value v = Value::fromRawBits(word);
    TraceManuallyBarrieredEdge(trc, &v, "jit-masm-value");
    ptr = (void*)v.bitsAsPunboxPointer();
  } else {
    // This relocation is a raw pointer or a Value with a zero tag.
    // No barrier needed since these are constants.
    TraceManuallyBarrieredGenericPointerEdge(
        trc, reinterpret_cast<gc::Cell**>(&ptr), "jit-masm-ptr");
  }

  if (ptr != prior) {
    if (awjc.isNothing()) {
      awjc.emplace(code);
    }
    Assembler::UpdateLoad64Value(inst, uint64_t(ptr));
  }
}

/* static */
void Assembler::TraceDataRelocations(JSTracer* trc, JitCode* code,
                                     CompactBufferReader& reader) {
  mozilla::Maybe<AutoWritableJitCode> awjc;
  while (reader.more()) {
    size_t offset = reader.readUnsigned();
    Instruction* inst = (Instruction*)(code->raw() + offset);
    TraceOneDataRelocation(trc, awjc, code, inst);
  }
}

void Assembler::Bind(uint8_t* rawCode, const CodeLabel& label) {
  if (label.patchAt().bound()) {
    auto mode = label.linkMode();
    intptr_t offset = label.patchAt().offset();
    intptr_t target = label.target().offset();

    if (mode == CodeLabel::RawPointer) {
      *reinterpret_cast<const void**>(rawCode + offset) = rawCode + target;
    } else {
      MOZ_ASSERT(mode == CodeLabel::MoveImmediate ||
                 mode == CodeLabel::JumpImmediate);
      Instruction* inst = (Instruction*)(rawCode + offset);
      Assembler::UpdateLoad64Value(inst, (uint64_t)(rawCode + target));
    }
  }
}

void Assembler::bind(InstImm* inst, uintptr_t branch, uintptr_t target) {
  int64_t offset = target - branch;
  InstImm inst_beq = InstImm(op_beq, BOffImm16(0), zero, zero);

  // If encoded offset is 4, then the jump must be short.
  if (inst[0].isShortJump() && inst[0].extractShortJumpOffset() == 4) {
    MOZ_ASSERT(inst[0].isShortJumpInRange(offset));
    inst[0].setShortJumpOffset(offset);
    inst[1].makeNop();  // because before set INVALID_OFFSET
    return;
  }

  UseScratchRegisterScope temps(*this);

  // A call is reserved as [pcaddu18i ?, 0][chain]. See also the last arm of
  // MacroAssemblerLOONG64::ma_bl(Label* label).
  if (inst[0].extractBitField(31, 25) == ((uint32_t)op_pcaddu18i >> 25)) {
    MOZ_ASSERT(inst[0].extractBitField(24, 5) == 0);

    Register scratch = temps.Acquire();
    const auto [si20, offs16] = SplitJump36Offset(offset);
    inst[0] = InstImm(op_pcaddu18i, si20, scratch, false);
    inst[1] = InstImm(op_jirl, BOffImm16(offs16), scratch, ra);
    return;
  }

  if (inst[0].isShortJumpInRange(offset)) {
    bool conditional = inst[0].encode() != inst_beq.encode();

    inst[0].setShortJumpOffset(offset);
    if (conditional) {
      // Skip the trailing nop on the fallthrough path.
      inst[1] = InstImm(op_bge, BOffImm16(2 * sizeof(uint32_t)), zero, zero);
    } else {
      inst[1].makeNop();
    }
    return;
  }

  if (inst[0].encode() == inst_beq.encode()) {
    Register scratch = temps.Acquire();
    const auto [si20, offs16] = SplitJump36Offset(offset);
    inst[0] = InstImm(op_pcaddu18i, si20, scratch, false);
    inst[1] = InstImm(op_jirl, BOffImm16(offs16), scratch, zero);
  } else {
    inst[0] = invertBranch(inst[0], 3 * sizeof(uint32_t));
    Register scratch = temps.Acquire();
    const auto [si20, offs16] = SplitJump36Offset(offset - sizeof(uint32_t));
    inst[1] = InstImm(op_pcaddu18i, si20, scratch, false);
    inst[2] = InstImm(op_jirl, BOffImm16(offs16), scratch, zero);
  }
}

void Assembler::processCodeLabels(uint8_t* rawCode) {
  for (const CodeLabel& label : codeLabels_) {
    Bind(rawCode, label);
  }
}

uint32_t Assembler::PatchWrite_NearCallSize() {
  // Load an address needs 3 instructions, and a jump.
  return (3 + 1) * sizeof(uint32_t);
}

void Assembler::PatchWrite_NearCall(CodeLocationLabel start,
                                    CodeLocationLabel toCall) {
  Instruction* inst = (Instruction*)start.raw();
  uint8_t* dest = toCall.raw();

  // Overwrite whatever instruction used to be here with a call.
  // Always use long jump for two reasons:
  // - Jump has to be the same size because of PatchWrite_NearCallSize.
  // - Return address has to be at the end of replaced block.
  // Short jump wouldn't be more efficient.
  Assembler::WriteLoad64Instructions(inst, SavedScratchRegister,
                                     (uint64_t)dest);
  inst[3] = InstImm(op_jirl, BOffImm16(0), SavedScratchRegister, ra);
}

uint64_t Assembler::ExtractLoad64Value(Instruction* inst0) {
  InstImm* i0 = (InstImm*)inst0;
  InstImm* i1 = (InstImm*)i0->next();
  InstImm* i2 = (InstImm*)i1->next();
  InstImm* i3 = (InstImm*)i2->next();

  const uint32_t rd_code = i0->extractRD();

  MOZ_ASSERT((i0->extractBitField(31, 25)) == ((uint32_t)op_lu12i_w >> 25));
  MOZ_ASSERT((i1->extractBitField(31, 22)) == ((uint32_t)op_ori >> 22) &&
             i1->extractRJ() == rd_code && i1->extractRD() == rd_code);
  MOZ_ASSERT((i2->extractBitField(31, 25)) == ((uint32_t)op_lu32i_d >> 25) &&
             i2->extractRD() == rd_code);

  if ((i3->extractBitField(31, 22)) == ((uint32_t)op_lu52i_d >> 22) &&
      i3->extractRJ() == rd_code && i3->extractRD() == rd_code) {
    // Li64
    uint64_t value =
        (uint64_t(i0->extractBitField(Imm20Bits + Imm20Shift - 1, Imm20Shift))
         << 12) |
        (uint64_t(
            i1->extractBitField(Imm12Bits + Imm12Shift - 1, Imm12Shift))) |
        (uint64_t(i2->extractBitField(Imm20Bits + Imm20Shift - 1, Imm20Shift))
         << 32) |
        (uint64_t(i3->extractBitField(Imm12Bits + Imm12Shift - 1, Imm12Shift))
         << 52);
    return value;
  } else {
    // Li48
    uint64_t value =
        (uint64_t(i0->extractBitField(Imm20Bits + Imm20Shift - 1, Imm20Shift))
         << 12) |
        (uint64_t(
            i1->extractBitField(Imm12Bits + Imm12Shift - 1, Imm12Shift))) |
        (uint64_t(i2->extractBitField(Imm20Bits + Imm20Shift - 1, Imm20Shift))
         << 32);

    return uint64_t((int64_t(value) << 16) >> 16);
  }
}

void Assembler::UpdateLoad64Value(Instruction* inst0, uint64_t value) {
  // Sync with ma_liPatchable()
  InstImm* i0 = (InstImm*)inst0;
  InstImm* i1 = (InstImm*)i0->next();
  InstImm* i2 = (InstImm*)i1->next();
  InstImm* i3 = (InstImm*)i2->next();

  const uint32_t rd_code = i0->extractRD();

  MOZ_ASSERT((i0->extractBitField(31, 25)) == ((uint32_t)op_lu12i_w >> 25));
  MOZ_ASSERT((i1->extractBitField(31, 22)) == ((uint32_t)op_ori >> 22) &&
             i1->extractRJ() == rd_code && i1->extractRD() == rd_code);
  MOZ_ASSERT((i2->extractBitField(31, 25)) == ((uint32_t)op_lu32i_d >> 25) &&
             i2->extractRD() == rd_code);

  if ((i3->extractBitField(31, 22)) == ((uint32_t)op_lu52i_d >> 22) &&
      i3->extractRJ() == rd_code && i3->extractRD() == rd_code) {
    // Li64
    *i0 = InstImm(op_lu12i_w, (int32_t)((value >> 12) & 0xfffff),
                  Register::FromCode(i0->extractRD()), false);
    *i1 = InstImm(op_ori, (int32_t)(value & 0xfff),
                  Register::FromCode(i1->extractRJ()),
                  Register::FromCode(i1->extractRD()), 12);
    *i2 = InstImm(op_lu32i_d, (int32_t)((value >> 32) & 0xfffff),
                  Register::FromCode(i2->extractRD()), false);
    *i3 = InstImm(op_lu52i_d, (int32_t)((value >> 52) & 0xfff),
                  Register::FromCode(i3->extractRJ()),
                  Register::FromCode(i3->extractRD()), 12);
  } else {
    // Li48
    *i0 = InstImm(op_lu12i_w, (int32_t)((value >> 12) & 0xfffff),
                  Register::FromCode(i0->extractRD()), false);
    *i1 = InstImm(op_ori, (int32_t)(value & 0xfff),
                  Register::FromCode(i1->extractRJ()),
                  Register::FromCode(i1->extractRD()), 12);
    *i2 = InstImm(op_lu32i_d, (int32_t)((value >> 32) & 0xfffff),
                  Register::FromCode(i2->extractRD()), false);
  }
}

void Assembler::WriteLoad64Instructions(Instruction* inst0, Register reg,
                                        uint64_t value) {
  Instruction* inst1 = inst0->next();
  Instruction* inst2 = inst1->next();
  *inst0 = InstImm(op_lu12i_w, (int32_t)((value >> 12) & 0xfffff), reg, false);
  *inst1 = InstImm(op_ori, (int32_t)(value & 0xfff), reg, reg, 12);
  *inst2 = InstImm(op_lu32i_d, (int32_t)((value >> 32) & 0xfffff), reg, false);
}

void Assembler::PatchDataWithValueCheck(CodeLocationLabel label,
                                        ImmPtr newValue, ImmPtr expectedValue) {
  PatchDataWithValueCheck(label, PatchedImmPtr(newValue.value),
                          PatchedImmPtr(expectedValue.value));
}

void Assembler::PatchDataWithValueCheck(CodeLocationLabel label,
                                        PatchedImmPtr newValue,
                                        PatchedImmPtr expectedValue) {
  Instruction* inst = (Instruction*)label.raw();

  // Extract old Value
  DebugOnly<uint64_t> value = Assembler::ExtractLoad64Value(inst);
  MOZ_ASSERT(value == uint64_t(expectedValue.value));

  // Replace with new value
  Assembler::UpdateLoad64Value(inst, uint64_t(newValue.value));
}

uint64_t Assembler::ExtractInstructionImmediate(uint8_t* code) {
  InstImm* inst = (InstImm*)code;
  return Assembler::ExtractLoad64Value(inst);
}

void Assembler::ToggleCall(CodeLocationLabel inst_, bool enabled) {
  InstImm* const i0 = (InstImm*)inst_.raw();  // pcaddu18i
  InstImm* const i1 = (InstImm*)i0->next();  // jirl (enabled) or bne (disabled)

  MOZ_ASSERT((i0->extractBitField(31, 25)) == ((uint32_t)op_pcaddu18i >> 25));
  const BOffImm16 offs = BOffImm16(*i1);
  const Register scratch = Register::FromCode(i0->extractRD());
  const mozilla::DebugOnly<uint32_t> i1_op = i1->extractBitField(31, 26);

  if (enabled) {
    MOZ_ASSERT_IF(i1_op != ((uint32_t)op_bne >> 26),
                  i1_op == ((uint32_t)op_jirl >> 26));
    *i1 = InstImm(op_jirl, offs, scratch, ra);
  } else {
    MOZ_ASSERT_IF(i1_op != ((uint32_t)op_jirl >> 26),
                  i1_op == ((uint32_t)op_bne >> 26));
    *i1 = InstImm(op_bne, offs, zero, zero);
  }
}

UseScratchRegisterScope::UseScratchRegisterScope(Assembler& assembler)
    : available_(assembler.GetScratchRegisterList()),
      old_available_(*available_) {}

UseScratchRegisterScope::UseScratchRegisterScope(Assembler* assembler)
    : available_(assembler->GetScratchRegisterList()),
      old_available_(*available_) {}

UseScratchRegisterScope::~UseScratchRegisterScope() {
  *available_ = old_available_;
}

Register UseScratchRegisterScope::Acquire() {
  MOZ_ASSERT(available_ != nullptr);
  MOZ_ASSERT(!available_->empty());
  Register index = GeneralRegisterSet::FirstRegister(available_->bits());
  available_->takeRegisterIndex(index);
  return index;
}

void UseScratchRegisterScope::Release(const Register& reg) {
  MOZ_ASSERT(available_ != nullptr);
  MOZ_ASSERT(old_available_.hasRegisterIndex(reg));
  MOZ_ASSERT(!available_->hasRegisterIndex(reg));
  Include(GeneralRegisterSet(1 << reg.code()));
}

bool UseScratchRegisterScope::hasAvailable() const {
  return (available_->size()) != 0;
}

uint32_t UseScratchRegisterScope::countAvailable() const {
  return available_->size();
}
