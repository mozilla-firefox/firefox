/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2021 the V8 project authors. All rights reserved.
#include "jit/riscv64/Assembler-riscv64.h"

#include "mozilla/DebugOnly.h"
#include "mozilla/Maybe.h"

#include "gc/Marking.h"
#include "jit/AutoWritableJitCode.h"
#include "jit/riscv64/base/Integer.h"
#include "jit/riscv64/disasm/Disasm-riscv64.h"

using mozilla::DebugOnly;
namespace js {
namespace jit {

bool Assembler::FLAG_riscv_debug = false;

// Size of the instruction stream, in bytes.  Note this doesn't take
// into account the size of any un-flushed constant pools.
size_t Assembler::size() const { return m_buffer.size(); }

// Returns the size of the buffer we can currently read, hence ignoring any
// un-flushed data in currently-under-construction constant pool(s).
size_t Assembler::readableSize() const { return m_buffer.size(); }

bool Assembler::swapBuffer(wasm::Bytes& bytes) {
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

// Size of the relocation table, in bytes.
size_t Assembler::jumpRelocationTableBytes() const {
  return jumpRelocations_.length();
}

size_t Assembler::dataRelocationTableBytes() const {
  return dataRelocations_.length();
}
// Size of the data table, in bytes.
size_t Assembler::bytesNeeded() const {
  return size() + jumpRelocationTableBytes() + dataRelocationTableBytes();
}

void Assembler::executableCopy(uint8_t* buffer) {
  MOZ_ASSERT(isFinished);
  m_buffer.executableCopy(buffer);
}

void Assembler::processCodeLabels(uint8_t* rawCode) {
  for (const CodeLabel& label : codeLabels_) {
    Bind(rawCode, label);
  }
}

void Assembler::WritePoolGuard(BufferOffset branch, Instruction* inst,
                               BufferOffset dest) {
  DEBUG_PRINTF("\tWritePoolGuard\n");

  int32_t offset = dest.getOffset() - branch.getOffset();

  inst->SetJFormat(RO_JAL, zero_reg.code(), offset);

  DEBUG_PRINTF("%p(%x): ", inst, branch.getOffset());
#ifdef JS_DISASM_RISCV64
  if (JitSpewEnabled(JitSpew_Codegen)) {
    disassembleInstr(branch, inst);
    inst += kInstrSize;

    // Disassemble veneer branches after spewing the pool guard, so all
    // instructions appear in order.
    BufferOffset bo(branch.getOffset() + kInstrSize);
    while (bo < dest) {
      disassembleInstr(bo, inst);
      inst += kInstrSize;
      bo = BufferOffset(bo.getOffset() + kInstrSize);
    }
  }
#endif
}

void Assembler::copyJumpRelocationTable(uint8_t* dest) {
  if (jumpRelocations_.length()) {
    memcpy(dest, jumpRelocations_.buffer(), jumpRelocations_.length());
  }
}

void Assembler::copyDataRelocationTable(uint8_t* dest) {
  if (dataRelocations_.length()) {
    memcpy(dest, dataRelocations_.buffer(), dataRelocations_.length());
  }
}

struct ImmPtrParts {
  int32_t high_20;  // Bits 47:29, 19 bits.
  int16_t low_12;   // Bits 28:17, 12 bits.
  int16_t b11;      // Bits 16:6, 11 bits.
  int16_t a6;       // Bits 5:0, 6 bits.
};

static constexpr auto ToImmPtrParts(int64_t imm) {
  MOZ_ASSERT((imm & 0xffff'0000'0000'0000ll) == 0, "pointers are 48 bits");

  int64_t high_31 = (imm >> 17) & 0x7fffffff;  // 31 bits

  return ImmPtrParts{
      .high_20 = int32_t((high_31 + 0x800) >> 12),
      .low_12 = int16_t(high_31 & 0xfff),
      .b11 = int16_t((imm >> 6) & 0x7ff),
      .a6 = int16_t(imm & 0x3f),
  };
}

// Read or write to an instruction sequence written by |li_ptr|.
class LiPtr {
 public:
  // li_ptr emits a six instruction sequence.
  static constexpr size_t Length = 6;

 private:
  Instruction* start_;

  Instruction* at(size_t index) {
    MOZ_ASSERT(index < Length);
    return start_ + index * kInstrSize;
  }

  const Instruction* at(size_t index) const {
    MOZ_ASSERT(index < Length);
    return start_ + index * kInstrSize;
  }

 public:
  explicit LiPtr(Instruction* start) : start_(start) {}

  /**
   * Return true iff this is a li_ptr instruction sequence.
   */
  bool isValid() const {
    return at(0)->IsLui() && at(1)->IsAddi() && at(2)->IsSlli() &&
           at(3)->IsOri() && at(4)->IsSlli() && at(5)->IsOri();
  }

  /**
   * Disassemble the li_ptr instruction sequence.
   */
  void disassemble() {
    Assembler::disassembleInstr(at(0));
    Assembler::disassembleInstr(at(1));
    Assembler::disassembleInstr(at(2));
    Assembler::disassembleInstr(at(3));
    Assembler::disassembleInstr(at(4));
    Assembler::disassembleInstr(at(5));
  }

  /**
   * Return the register to which this li_ptr instruction sequence writes to.
   */
  int target() const {
    MOZ_ASSERT(isValid());

    // All instructions must write to the same register.
    MOZ_ASSERT(at(0)->RdValue() == at(1)->RdValue());
    MOZ_ASSERT(at(0)->RdValue() == at(2)->RdValue());
    MOZ_ASSERT(at(0)->RdValue() == at(3)->RdValue());
    MOZ_ASSERT(at(0)->RdValue() == at(4)->RdValue());
    MOZ_ASSERT(at(0)->RdValue() == at(5)->RdValue());

    return at(0)->RdValue();
  }

  /**
   * Load the constant encoded in the li_ptr instruction sequence.
   */
  uintptr_t load() const {
    MOZ_ASSERT(isValid());

    // lui(rd, high_20);
    int64_t imm = int64_t(at(0)->Imm20UValue() << kImm20Shift);

    // addi(rd, rd, low_12);  // 31 bits in rd.
    imm += int64_t(at(1)->Imm12Value());

    // slli(rd, rd, 11);  // Space for next 11 bits
    MOZ_ASSERT(at(2)->Imm12Value() == 11);
    imm <<= 11;

    // ori(rd, rd, b11);  // 11 bits are added, 42 bit in rd.
    imm |= int64_t(at(3)->Imm12Value());

    // slli(rd, rd, 6);  // Space for next 6 bits
    MOZ_ASSERT(at(4)->Imm12Value() == 6);
    imm <<= 6;

    // ori(rd, rd, a6);  // 6 bits are added, 48 bit in rd.
    imm |= int64_t(at(5)->Imm12Value());

    MOZ_ASSERT((imm & 0xffff'0000'0000'0000ll) == 0, "pointers are 48 bits");
    return static_cast<uintptr_t>(imm);
  }

  /**
   * Update the constant embedded in the li_ptr instruction sequence.
   */
  void update(uintptr_t value) {
    MOZ_ASSERT(isValid());

    auto [high_20, low_12, b11, a6] = ToImmPtrParts(value);

    // lui(rd, high_20);
    at(0)->SetImm20UValue(high_20);

    // addi(rd, rd, low_12);  // 31 bits in rd.
    at(1)->SetImm12Value(low_12);

    // slli(rd, rd, 11);  // Space for next 11 bits
    MOZ_ASSERT(at(2)->Imm12Value() == 11);

    // ori(rd, rd, b11);  // 11 bits are added, 42 bit in rd.
    at(3)->SetImm12Value(b11);

    // slli(rd, rd, 6);  // Space for next 6 bits
    MOZ_ASSERT(at(4)->Imm12Value() == 6);

    // ori(rd, rd, a6);  // 6 bits are added, 48 bit in rd.
    at(5)->SetImm12Value(a6);

    MOZ_ASSERT(load() == value);
  }

  /**
   * Write the li_ptr instruction sequence, overwriting any previous
   * instructions.
   */
  void write(Register rd, uintptr_t value) {
    auto [high_20, low_12, b11, a6] = ToImmPtrParts(value);

    // lui(rd, high_20);
    at(0)->SetUFormat(RO_LUI, rd.code(), high_20);

    // addi(rd, rd, low_12);  // 31 bits in rd.
    at(1)->SetIFormat(RO_ADDI, rd.code(), rd.code(), low_12);

    // slli(rd, rd, 11);  // Space for next 11 bits
    at(2)->SetIShiftFormat(RO_SLLI, rd.code(), rd.code(), 11);

    // ori(rd, rd, b11);  // 11 bits are added, 42 bit in rd.
    at(3)->SetIFormat(RO_ORI, rd.code(), rd.code(), b11);

    // slli(rd, rd, 6);  // Space for next 6 bits
    at(4)->SetIShiftFormat(RO_SLLI, rd.code(), rd.code(), 6);

    // ori(rd, rd, a6);  // 6 bits are added, 48 bit in rd.
    at(5)->SetIFormat(RO_ORI, rd.code(), rd.code(), a6);

    MOZ_ASSERT(load() == value);
  }
};

uintptr_t Assembler::LoadLiPtrInstructions(Instruction* instr) {
  LiPtr ptr(instr);
  if (!ptr.isValid()) {
    // Dump the faulty instruction sequence before crashing.
    ptr.disassemble();
  }
  MOZ_RELEASE_ASSERT(ptr.isValid());

  return ptr.load();
}

void Assembler::UpdateLiPtrInstructions(Instruction* instr, uintptr_t value) {
  LiPtr ptr(instr);
  if (!ptr.isValid()) {
    // Dump the faulty instruction sequence before crashing.
    ptr.disassemble();
  }
  MOZ_RELEASE_ASSERT(ptr.isValid());

  ptr.update(value);
}

void Assembler::WriteLiPtrInstructions(Instruction* instr, Register reg,
                                       uintptr_t value) {
  // Forcibly overwrites whatever was written at |instr| with |value|.
  LiPtr ptr(instr);
  ptr.write(reg, value);
}

BufferOffset Assembler::li_ptr(Register rd, int64_t imm) {
  AutoForbidPoolsAndNops afp(this, 6);
  BufferOffset offset = nextOffset();

  // Initialize rd with an address
  // Pointers are 48 bits
  // 6 fixed instructions are generated
  DEBUG_PRINTF("li_ptr(%d, %" PRIx64 " <%" PRId64 ">)\n", ToNumber(rd), imm,
               imm);

  auto [high_20, low_12, b11, a6] = ToImmPtrParts(imm);

  lui(rd, high_20);
  addi(rd, rd, low_12);  // 31 bits in rd.
  slli(rd, rd, 11);      // Space for next 11 bits
  ori(rd, rd, b11);      // 11 bits are put in. 42 bit in rd
  slli(rd, rd, 6);       // Space for next 6 bits
  ori(rd, rd, a6);       // 6 bits are put in. 48 bits in rd

  MOZ_ASSERT_IF(!oom(), LiPtr(getInstructionAt(offset)).isValid());

  return offset;
}

struct Imm64Parts {
  int32_t high_20;  // Bits 63:48, 16 bits.
  int16_t d12;      // Bits 47:36, 12 bits.
  int16_t c12;      // Bits 35:24, 12 bits.
  int16_t b12;      // Bits 23:12, 12 bits.
  int16_t a12;      // Bits 11:0, 12 bits.
};

static constexpr auto ToImm64Parts(int64_t imm) {
  return Imm64Parts{
      .high_20 = int32_t(
          (imm + (1LL << 47) + (1LL << 35) + (1LL << 23) + (1LL << 11)) >> 48),
      .d12 =
          int16_t((imm + (1LL << 35) + (1LL << 23) + (1LL << 11)) << 16 >> 52),
      .c12 = int16_t((imm + (1LL << 23) + (1LL << 11)) << 28 >> 52),
      .b12 = int16_t((imm + (1LL << 11)) << 40 >> 52),
      .a12 = int16_t(imm << 52 >> 52),
  };
}

// Read or write to an instruction sequence written by |li_constant|.
class LiConstant {
 public:
  // li_constant emits an eight instruction sequence.
  static constexpr size_t Length = 8;

 private:
  Instruction* start_;

  Instruction* at(size_t index) {
    MOZ_ASSERT(index < Length);
    return start_ + index * kInstrSize;
  }

  const Instruction* at(size_t index) const {
    MOZ_ASSERT(index < Length);
    return start_ + index * kInstrSize;
  }

 public:
  explicit LiConstant(Instruction* start) : start_(start) {}

  /**
   * Return true iff this is a li_constant instruction sequence.
   */
  bool isValid() const {
    return at(0)->IsLui() && at(1)->IsAddiw() && at(2)->IsSlli() &&
           at(3)->IsAddi() && at(4)->IsSlli() && at(5)->IsAddi() &&
           at(6)->IsSlli() && at(7)->IsAddi();
  }

  /**
   * Disassemble the li_constant instruction sequence.
   */
  void disassemble() {
    Assembler::disassembleInstr(at(0));
    Assembler::disassembleInstr(at(1));
    Assembler::disassembleInstr(at(2));
    Assembler::disassembleInstr(at(3));
    Assembler::disassembleInstr(at(4));
    Assembler::disassembleInstr(at(5));
    Assembler::disassembleInstr(at(6));
    Assembler::disassembleInstr(at(7));
  }

  /**
   * Load the constant encoded in the li_constant instruction sequence.
   */
  int64_t load() const {
    MOZ_ASSERT(isValid());

    // lui(rd, high_20);  // Bits 63:48
    int64_t imm = int64_t(at(0)->Imm20UValue() << kImm20Shift);

    // addiw(rd, rd, d12);  // Bits 47:36
    imm += int64_t(at(1)->Imm12Value());

    // slli(rd, rd, 12);
    MOZ_ASSERT(at(2)->Imm12Value() == 12);
    imm <<= 12;

    // addi(rd, rd, c12);  // Bits 35:24
    imm += int64_t(at(3)->Imm12Value());

    // slli(rd, rd, 12);
    MOZ_ASSERT(at(4)->Imm12Value() == 12);
    imm <<= 12;

    // addi(rd, rd, b12);  // Bits 23:12
    imm += int64_t(at(5)->Imm12Value());

    // slli(rd, rd, 12);
    MOZ_ASSERT(at(6)->Imm12Value() == 12);
    imm <<= 12;

    // addi(rd, rd, a12);  // Bits 11:0
    imm += int64_t(at(7)->Imm12Value());

    return imm;
  }

  /**
   * Update the constant embedded in the li_constant instruction sequence.
   */
  void update(int64_t value) {
    MOZ_ASSERT(isValid());

    auto [high_20, d12, c12, b12, a12] = ToImm64Parts(value);

    // lui(rd, high_20);  // Bits 63:48
    at(0)->SetImm20UValue(high_20);

    // addiw(rd, rd, d12);  // Bits 47:36
    at(1)->SetImm12Value(d12);

    // slli(rd, rd, 12);
    MOZ_ASSERT(at(2)->Shamt() == 12);

    // addi(rd, rd, c12);  // Bits 35:24
    at(3)->SetImm12Value(c12);

    // slli(rd, rd, 12);
    MOZ_ASSERT(at(4)->Shamt() == 12);

    // addi(rd, rd, b12);  // Bits 23:12
    at(5)->SetImm12Value(b12);

    // slli(rd, rd, 12);
    MOZ_ASSERT(at(6)->Shamt() == 12);

    // addi(rd, rd, a12);  // Bits 11:0
    at(7)->SetImm12Value(a12);

    MOZ_ASSERT(load() == value);
  }
};

int64_t Assembler::LoadLiConstantInstructions(Instruction* instr) {
  LiConstant cst(instr);
  if (!cst.isValid()) {
    // Dump the faulty instruction sequence before crashing.
    cst.disassemble();
  }
  MOZ_RELEASE_ASSERT(cst.isValid());

  return cst.load();
}

void Assembler::UpdateLiConstantInstructions(Instruction* instr,
                                             int64_t value) {
  LiConstant cst(instr);
  if (!cst.isValid()) {
    // Dump the faulty instruction sequence before crashing.
    cst.disassemble();
  }
  MOZ_RELEASE_ASSERT(cst.isValid());

  cst.update(value);
}

BufferOffset Assembler::li_constant(Register rd, int64_t imm) {
  AutoForbidPoolsAndNops afp(this, 8);
  BufferOffset offset = nextOffset();

  DEBUG_PRINTF("li_constant(%d, %" PRIx64 " <%" PRId64 ">)\n", ToNumber(rd),
               imm, imm);

  auto [high_20, d12, c12, b12, a12] = ToImm64Parts(imm);

  lui(rd, high_20);    // Bits 63:48
  addiw(rd, rd, d12);  // Bits 47:36
  slli(rd, rd, 12);
  addi(rd, rd, c12);  // Bits 35:24
  slli(rd, rd, 12);
  addi(rd, rd, b12);  // Bits 23:12
  slli(rd, rd, 12);
  addi(rd, rd, a12);  // Bits 11:0

  MOZ_ASSERT_IF(!oom(), LiConstant(getInstructionAt(offset)).isValid());

  return offset;
}

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
        // A real floating-point argument is passed in a floating-point
        // argument register if [...] at least one floating-point argument
        // register is available. Otherwise, it is passed according to the
        // integer calling convention.
        //
        // <https://riscv-non-isa.github.io/riscv-elf-psabi-doc/#_hardware_floating_point_calling_convention>
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
          FloatRegisters::Encoding(floatRegIndex_ + fa0.encoding()),
          type == MIRType::Double ? FloatRegisters::Double
                                  : FloatRegisters::Single));
      floatRegIndex_++;
      break;
    }
    case MIRType::Simd128: {
      MOZ_CRASH("RISCV64 does not support simd yet.");
      break;
    }
    default:
      MOZ_CRASH("Unexpected argument type");
  }
  return current_;
}

bool Assembler::oom() const {
  return AssemblerShared::oom() || m_buffer.oom() || jumpRelocations_.oom() ||
         dataRelocations_.oom();
}

#ifdef JS_DISASM_RISCV64
class NameConverterWithInstruction : public disasm::NameConverter {
  BufferOffset offs_;
  Instruction* instr_;

 public:
  NameConverterWithInstruction(BufferOffset offs, Instruction* instr)
      : offs_(offs), instr_(instr) {}

  const char* NameOfAddress(uint8_t* addr) const {
    if (instr_->IsJal()) {
      // Print target buffer offset.
      SNPrintF(tmp_buffer_, "%06" PRIx32,
               offs_.getOffset() + instr_->Imm20JValue());
    } else {
      SNPrintF(tmp_buffer_, "(unknown)");
    }
    return tmp_buffer_.start();
  }
};

class NameConverterWithLabelDoc : public disasm::NameConverter {
  DisassemblerSpew::LabelDoc target_;

 public:
  explicit NameConverterWithLabelDoc(DisassemblerSpew::LabelDoc target)
      : target_(target) {}

  const char* NameOfAddress(uint8_t* addr) const {
    if (target_.valid) {
      // Print target label identifier, with optional "f" for forward branches.
      SNPrintF(tmp_buffer_, "%d%s", target_.doc, !target_.bound ? "f" : "");
    } else {
      SNPrintF(tmp_buffer_, "(link-time target)");
    }
    return tmp_buffer_.start();
  }
};

void Assembler::disassembleInstr(Instruction* instr) {
  if (!FLAG_riscv_debug) {
    return;
  }
  disasm::NameConverter converter;
  disasm::Disassembler disasm(converter);
  EmbeddedVector<char, disasm::ReasonableBufferSize> buffer;

  disasm.InstructionDecode(buffer, instr);
  DEBUG_PRINTF("%s\n", buffer.start());
}

void Assembler::disassembleInstr(BufferOffset offs, Instruction* instr) {
  NameConverterWithInstruction converter(offs, instr);
  disasm::Disassembler disasm(converter);
  EmbeddedVector<char, disasm::ReasonableBufferSize> buffer;

  disasm.InstructionDecode(buffer, instr);

  JitSpew(JitSpew_Codegen, "[?] %06" PRIx32 " %s", uint32_t(offs.getOffset()),
          buffer.start());
}

void Assembler::spew(BufferOffset offs, Instruction* instr) {
  if (spew_.isDisabled() || !instr) {
    return;
  }

  disasm::NameConverter converter;
  disasm::Disassembler disasm(converter);
  EmbeddedVector<char, disasm::ReasonableBufferSize> buffer;

  disasm.InstructionDecode(buffer, instr);

  spew_.spew("%06" PRIx32 " %s", uint32_t(offs.getOffset()), buffer.start());
}

void Assembler::spewBranch(BufferOffset offs, Instruction* instr,
                           LabelDoc target) {
  if (spew_.isDisabled() || !instr) {
    return;
  }

  NameConverterWithLabelDoc converter(target);
  disasm::Disassembler disasm(converter);
  EmbeddedVector<char, disasm::ReasonableBufferSize> buffer;

  disasm.InstructionDecode(buffer, instr);

  spew_.spew("%06" PRIx32 " %s", uint32_t(offs.getOffset()), buffer.start());
}

DisassemblerSpew::LabelDoc Assembler::refLabel(Label* label) {
  if (spew_.isDisabled()) {
    return LabelDoc();
  }
  return spew_.refLabel(label);
}
#endif /* JS_DISASM_RISCV64 */

void Assembler::PatchDataWithValueCheck(CodeLocationLabel label,
                                        ImmPtr newValue, ImmPtr expectedValue) {
  PatchDataWithValueCheck(label, PatchedImmPtr(newValue.value),
                          PatchedImmPtr(expectedValue.value));
}

void Assembler::PatchDataWithValueCheck(CodeLocationLabel label,
                                        PatchedImmPtr newValue,
                                        PatchedImmPtr expectedValue) {
  Instruction* inst = Instruction::At(label.raw());

  // Check the previous value matches |expectedValue|.
  DebugOnly<uint64_t> value = Assembler::ExtractLoad64Value(inst);
  MOZ_ASSERT(value == uint64_t(expectedValue.value));

  // Update the instruction with |newValue|.
  Assembler::UpdateLoad64Value(inst, uint64_t(newValue.value));
}

uint64_t Assembler::ExtractLoad64Value(Instruction* inst0) {
  DEBUG_PRINTF("\tExtractLoad64Value: \tpc:%p ", inst0);
  MOZ_ASSERT(!inst0->IsJal(), "unexpected pool guard");

  // This method is called for 48-bit (li_ptr) and 64-bit (li_constant)
  // patchable immediates.
  //
  // The li_ptr and li_constant instruction sequences both start with the "lui"
  // instruction, so we have to inspect the second instruction to determine
  // which instruction sequence to read:
  // - li_constant starts with the instruction sequence "lui; addiw ...".
  // - li_ptr starts with the instruction sequence "lui; addi ...".

  Instruction* instr1 = inst0 + kInstrSize;
  if (instr1->IsAddiw()) {
    return LoadLiConstantInstructions(inst0);
  } else {
    return LoadLiPtrInstructions(inst0);
  }
}

void Assembler::UpdateLoad64Value(Instruction* inst0, uint64_t value) {
  DEBUG_PRINTF("\tUpdateLoad64Value: pc: %p\tvalue: %" PRIx64 "\n", inst0,
               value);
  MOZ_ASSERT(!inst0->IsJal(), "unexpected pool guard");

  // This method is called for 48-bit (li_ptr) and 64-bit (li_constant)
  // patchable immediates.
  //
  // The li_ptr and li_constant instruction sequences both start with the "lui"
  // instruction, so we have to inspect the second instruction to determine
  // which instruction sequence to patch:
  // - li_constant starts with the instruction sequence "lui; addiw ...".
  // - li_ptr starts with the instruction sequence "lui; addi ...".

  Instruction* instr1 = inst0 + kInstrSize;
  if (instr1->IsAddiw()) {
    UpdateLiConstantInstructions(inst0, value);
  } else {
    UpdateLiPtrInstructions(inst0, value);
  }
}

// This just stomps over memory with 32 bits of raw data. Its purpose is to
// overwrite the call of JITed code with 32 bits worth of an offset. This will
// is only meant to function on code that has been invalidated, so it should
// be totally safe. Since that instruction will never be executed again, a
// ICache flush should not be necessary
void Assembler::PatchWrite_Imm32(CodeLocationLabel label, Imm32 imm) {
  // Raw is going to be the return address.
  uint32_t* raw = (uint32_t*)label.raw();
  // Overwrite the 4 bytes before the return address, which will
  // end up being the call instruction.
  *(raw - 1) = imm.value;
}

// Unbound Label Representation.
//
// We can have multiple branches using the same label before it is bound.
// Assembler::bind() must then be able to enumerate all the branches and patch
// them to target the final label location.
//
// When a Label is unbound with uses, its offset is pointing to the tip of a
// linked list of uses. The uses can be branch, jal, or auipc+jalr instructions.
//
// The end of the list is encoded as a 0 pc offset, i.e. the tail is pointing to
// itself.

static constexpr int32_t kEndOfJumpChain = 0;

static int32_t ImmPCRawOffset(const Instruction* instr) {
  if (instr->IsBranch()) {
    return instr->BranchOffset();
  }
  if (instr->IsJal()) {
    return instr->Imm20JValue();
  }
  MOZ_CRASH("unexpected jump instruction");
}

static int32_t ImmPCRawOffset(const Instruction* auipc,
                              const Instruction* jalr) {
  MOZ_ASSERT(auipc->IsAuipc());
  MOZ_ASSERT(jalr->IsJalr());

  int32_t imm_auipc = auipc->Imm20UValue() << kImm20Shift;
  int32_t imm12 = jalr->Imm12Value();
  return imm_auipc + imm12;
}

BufferOffset Assembler::jumpChainGetNextLink(BufferOffset pos) {
  if (oom()) {
    return BufferOffset();
  }

  Instruction* link = getInstructionAt(pos);
  MOZ_ASSERT(link->IsBranch() || link->IsJal() || link->IsAuipc());

  // Raw encoded offset.
  int32_t offset;
  if (link->IsAuipc()) {
    Instruction* instr1 =
        getInstructionAt(BufferOffset(pos.getOffset() + kInstrSize));
    offset = ImmPCRawOffset(link, instr1);
  } else {
    offset = ImmPCRawOffset(link);
  }

  // End of the list is encoded as 0.
  if (offset == kEndOfJumpChain) {
    return BufferOffset();
  }

  // The encoded offset is the number of instructions to move.
  return BufferOffset(pos.getOffset() + offset);
}

void Assembler::jumpChainPutTargetAt(BufferOffset pos,
                                     BufferOffset target_pos) {
  if (oom()) {
    return;
  }

  int32_t offset = target_pos.getOffset() - pos.getOffset();
  MOZ_ASSERT((offset & 1) == 0);

  Instruction* instruction = getInstructionAt(pos);
  DEBUG_PRINTF("\tjumpChainPutTargetAt: %p (%d) to %p (%d)\n", instruction,
               pos.getOffset(), instruction + offset, target_pos.getOffset());

  switch (instruction->InstructionOpcodeType()) {
    case BRANCH: {
      MOZ_ASSERT(is_intn(offset, kBranchOffsetBits));
      instruction->SetBranchOffset(offset);
      break;
    }
    case JAL: {
      MOZ_ASSERT(is_intn(offset, kJumpOffsetBits));
      instruction->SetImm20JValue(offset);
      break;
    }
    case AUIPC: {
      Instruction* jalr =
          getInstructionAt(BufferOffset(pos.getOffset() + kInstrSize));
      MOZ_ASSERT(jalr->IsJalr());
      MOZ_ASSERT(instruction->RdValue() == jalr->Rs1Value());

      auto [Hi20, Lo12] = ToHigh20Low12(offset);
      instruction->SetImm20UValue(Hi20);
      jalr->SetImm12Value(Lo12);
      break;
    }
    default:
      MOZ_CRASH("unexpected jump instruction");
  }
}

void Assembler::bind(Label* label, BufferOffset boff) {
#ifdef JS_DISASM_RISCV64
  spew_.spewBind(label);
#endif
  DEBUG_PRINTF(".set Llabel %p %u\n", label, currentOffset());

  // If our caller didn't give us an explicit target to bind to
  // then we want to bind to the location of the next instruction
  BufferOffset targetOffset = boff.assigned() ? boff : nextOffset();

  // Nothing has seen the label yet: just mark the location.
  //
  // If we've run out of memory, don't attempt to modify the buffer which may
  // not be there. Just mark the label as bound to the (possibly bogus)
  // targetOffset.
  if (!label->used() || oom()) {
    label->bind(targetOffset.getOffset());
    return;
  }

  // Get the most recent instruction that used the label, as stored in the
  // label. This instruction is the head of an implicit linked list of label
  // uses.
  BufferOffset branchOffset(label);

  while (branchOffset.assigned()) {
    // Before overwriting the offset in this instruction, get the offset of
    // the next link in the implicit branch list.
    BufferOffset nextOffset = jumpChainGetNextLink(branchOffset);

    // Linking against the actual (Instruction*) would be invalid, since that
    // Instruction could be anywhere in memory. Instead, just link against the
    // correct relative offset, assuming no constant pools, which will be taken
    // into consideration during finalization.
    ptrdiff_t relativeByteOffset =
        targetOffset.getOffset() - branchOffset.getOffset();

    Instruction* link = getInstructionAt(branchOffset);
    OffsetSize offsetSize;
    if (link->IsBranch() || link->IsJal()) {
      offsetSize = link->GetOffsetSize();
    } else {
      MOZ_ASSERT(link->IsAuipc());
      offsetSize = OffsetSize::kOffset32;
    }

    // This branch may still be registered for callbacks. Stop tracking it.
    if (offsetSize < OffsetSize::kOffset32) {
      ImmBranchRangeType branchRange =
          OffsetSizeToImmBranchRangeType(offsetSize);
      BufferOffset deadline(branchOffset.getOffset() +
                            ImmBranchMaxForwardOffset(branchRange));
      m_buffer.unregisterBranchDeadline(branchRange, deadline);
    }

    // Is link able to reach the label?
    if (is_intn(relativeByteOffset, offsetSize)) {
      // Write a new relative offset into the instruction.
      jumpChainPutTargetAt(branchOffset, targetOffset);
    } else {
      // This is a short-range branch, and it can't reach the label directly.
      // Verify that it branches to a veneer: an unconditional branch.
      MOZ_ASSERT(offsetSize < OffsetSize::kOffset32);

      // |nextOffset| is a veneer branch (auipc; jalr).
      MOZ_ASSERT(nextOffset.assigned());
      MOZ_ASSERT(getInstructionAt(nextOffset)->IsAuipc());
      MOZ_ASSERT(
          getInstructionAt(BufferOffset(nextOffset.getOffset() + kInstrSize))
              ->IsJalr());

      // The veneer must be reachable from the branch.
      MOZ_RELEASE_ASSERT(is_intn(
          nextOffset.getOffset() - branchOffset.getOffset(), offsetSize));
    }

    branchOffset = nextOffset;
  }

  // Bind the label, so that future uses may encode the offset immediately.
  label->bind(targetOffset.getOffset());
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
      Instruction* inst = Instruction::At(rawCode + offset);
      Assembler::UpdateLoad64Value(inst, uint64_t(rawCode + target));
    }
  }
}

// A common implementation for the public branchOffset methods.
//
// If the label is bound, returns the offset to the label. Otherwise, links the
// instruction to the label and returns |kEndOfJumpChain|.
//
// The offset is calculated by the difference between the PC and the label
// address.
//
// For an unbound label, the returned offset will be encodable in the provided
// branch range. If the label is already bound, the caller is expected to make
// sure that it is in range, and emit the necessary branch instructions if it
// isn't.
int32_t Assembler::branchOffset(Label* L, OffsetSize bits,
                                BufferOffset next_instr_offset) {
  if (oom()) {
    return kEndOfJumpChain;
  }

  // Prevent nop sequences in branch instructions.
  AutoForbidNops afn(this);

  DEBUG_PRINTF("\branchOffset: %p to %d\n", L, next_instr_offset.getOffset());

  if (L->bound()) {
    // The label is bound: all uses are already linked.
    int32_t offset = L->offset() - next_instr_offset.getOffset();
    MOZ_ASSERT(is_intn(offset, bits));
    MOZ_ASSERT((offset & 1) == 0);
    MOZ_ASSERT_IF(bits == OffsetSize::kOffset32, (offset & 3) == 0);
    return offset;
  }

  // The label is unbound and previously unused: Store the offset in the label
  // itself for patching by bind().
  if (!L->used()) {
    L->use(next_instr_offset.getOffset());
    DEBUG_PRINTF("\tLabel  %p added to link: %d\n", L,
                 next_instr_offset.getOffset());
    return kEndOfJumpChain;
  }

  // The label is unbound and has multiple users. Create a linked list between
  // the branches, and update the linked list head in the label struct. This is
  // not always trivial since the branches in the linked list have limited
  // ranges.

  // What is the earliest buffer offset that would be reachable by the branch
  // we're about to add?
  int32_t earliestReachable =
      next_instr_offset.getOffset() + ImmBranchMinBackwardOffset(bits);

  // If the existing instruction at the head of the list is within reach of the
  // new branch, we can simply insert the new branch at the front of the list.
  if (L->offset() >= earliestReachable) {
    int32_t offset = L->offset() - next_instr_offset.getOffset();
    MOZ_ASSERT(offset != kEndOfJumpChain);
    MOZ_ASSERT(is_intn(offset, bits));
    MOZ_ASSERT((offset & 1) == 0);

    L->use(next_instr_offset.getOffset());
    return offset;
  }

  // The label already has a linked list of uses, but we can't reach the head
  // of the list with the allowed branch range. Insert this branch at a
  // different position in the list. We need to find an existing branch
  // `exbr`.
  //
  // In particular, the end of the list is always a viable candidate, so we'll
  // just get that.
  //
  // See also vixl::MozBaseAssembler::LinkAndGetOffsetTo.

  BufferOffset next(L);
  BufferOffset exbr;
  do {
    exbr = next;
    next = jumpChainGetNextLink(next);
  } while (next.assigned());
  jumpChainPutTargetAt(exbr, next_instr_offset);

  return kEndOfJumpChain;
}

int32_t Assembler::branchOffset(Label* L) {
  // Two instructions (auipc + jalr), without any new deadlines.
  BufferOffset next_instr_offset = nextInstrOffset(2, 0);
  return branchOffset(L, OffsetSize::kOffset32, next_instr_offset);
}

void Assembler::registerBranchDeadline(Label* L, OffsetSize bits,
                                       BufferOffset next_instr_offset) {
  MOZ_ASSERT(bits < OffsetSize::kOffset32);

  // Keep track of short-range branches targeting unbound labels. We may need
  // to insert veneers in PatchShortRangeBranchToVeneer() below.
  if (!L->bound()) {
    // This is the last possible branch target.
    BufferOffset deadline(next_instr_offset.getOffset() +
                          ImmBranchMaxForwardOffset(bits));
    DEBUG_PRINTF("\tregisterBranchDeadline %d type %d\n", deadline.getOffset(),
                 OffsetSizeToImmBranchRangeType(bits));
    m_buffer.registerBranchDeadline(OffsetSizeToImmBranchRangeType(bits),
                                    deadline);
  }
}

Assembler::Condition Assembler::InvertCondition(Condition cond) {
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

Assembler::DoubleCondition Assembler::InvertCondition(DoubleCondition cond) {
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

// Break / Trap instructions.
void Assembler::break_(uint32_t code, bool break_as_stop) {
  // We need to invalidate breaks that could be stops as well because the
  // simulator expects a char pointer after the stop instruction.
  // See constants-mips.h for explanation.
  MOZ_ASSERT(
      (break_as_stop && code <= kMaxStopCode && code > kMaxTracepointCode) ||
      (!break_as_stop && (code > kMaxStopCode || code <= kMaxTracepointCode)));

  ebreak();
#ifdef JS_SIMULATOR_RISCV64
  // since ebreak does not allow additional immediate field, we use the
  // immediate field of lui instruction immediately following the ebreak to
  // encode the "code" info. Only the simulator reads it, so other builds
  // emit the ebreak alone.
  MOZ_ASSERT(is_uint20(code));
  lui(zero_reg, code);
#endif
}

void Assembler::ToggleToJmp(CodeLocationLabel inst_) {
  Instruction* inst = Instruction::At(inst_.raw());
  MOZ_ASSERT(inst->IsAddi());

  int32_t offset = inst->Imm12Value();
  MOZ_ASSERT(is_int12(offset));

  // jal(zero, offset);
  inst->SetJFormat(RO_JAL, zero_reg.code(), offset);
}

void Assembler::ToggleToCmp(CodeLocationLabel inst_) {
  Instruction* inst = Instruction::At(inst_.raw());

  // toggledJump is allways used for short jumps.
  MOZ_ASSERT(inst->IsJal());

  // Replace "jal zero_reg, offset" with "addi $zero, $zero, offset"
  int32_t offset = inst->Imm20JValue();
  MOZ_ASSERT(is_int12(offset));

  inst->SetIFormat(RO_ADDI, zero_reg.code(), zero_reg.code(), offset);
}

bool Assembler::reserve(size_t size) {
  // This buffer uses fixed-size chunks so there's no point in reserving
  // now vs. on-demand.
  return !oom();
}

static JitCode* CodeFromJump(Instruction* jump) {
  uint8_t* target = (uint8_t*)Assembler::ExtractLoad64Value(jump);
  return JitCode::FromExecutable(target);
}

void Assembler::TraceJumpRelocations(JSTracer* trc, JitCode* code,
                                     CompactBufferReader& reader) {
  while (reader.more()) {
    JitCode* child =
        CodeFromJump(Instruction::At(code->raw() + reader.readUnsigned()));
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
    Instruction* inst = Instruction::At(code->raw() + offset);
    TraceOneDataRelocation(trc, awjc, code, inst);
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

void UseScratchRegisterScope::Acquire(Register reg) {
  MOZ_ASSERT(available_ != nullptr);
  MOZ_ASSERT(available_->hasRegisterIndex(reg));
  available_->takeRegisterIndex(reg);
}

void UseScratchRegisterScope::Release(Register reg) {
  MOZ_ASSERT(available_ != nullptr);
  MOZ_ASSERT(old_available_.hasRegisterIndex(reg));
  MOZ_ASSERT(!available_->hasRegisterIndex(reg));
  available_->addRegisterIndex(reg);
}

bool UseScratchRegisterScope::hasAvailable() const {
  return (available_->size()) != 0;
}

uint32_t UseScratchRegisterScope::countAvailable() const {
  return available_->size();
}

void Assembler::retarget(Label* label, Label* target) {
#ifdef JS_DISASM_RISCV64
  spew_.spewRetarget(label, target);
#endif

  if (label->used()) {
    if (target->bound()) {
      bind(label, BufferOffset(target));
    } else if (target->used()) {
      // The target is not bound but used. Prepend label's branch list
      // onto target's.
      BufferOffset labelBranchOffset(label);

      // Find the head of the use chain for label.
      BufferOffset next = jumpChainGetNextLink(labelBranchOffset);
      while (next.assigned()) {
        labelBranchOffset = next;
        next = jumpChainGetNextLink(next);
      }

      // Then patch the head of label's use chain to the tail of
      // target's use chain, prepending the entire use chain of target.
      jumpChainPutTargetAt(labelBranchOffset, BufferOffset(target));
      target->use(label->offset());
    } else {
      // The target is unbound and unused.  We can just take the head of
      // the list hanging off of label, and dump that into target.
      target->use(label->offset());
    }
  }
  label->reset();
}

bool Assembler::appendRawCode(const uint8_t* code, size_t numBytes) {
  flush();
  return m_buffer.appendRawCode(code, numBytes);
}

void Assembler::ToggleCall(CodeLocationLabel inst_, bool enabled) {
  LiPtr ptr(Instruction::At(inst_.raw()));
  if (!ptr.isValid()) {
    ptr.disassemble();
  }
  MOZ_RELEASE_ASSERT(ptr.isValid());

  Instruction* next = Instruction::At(inst_.raw() + LiPtr::Length * kInstrSize);
  MOZ_ASSERT(next->IsJalr() || next->IsNop());

  if (enabled) {
    next->SetIFormat(RO_JALR, ra.code(), ptr.target(), 0);
  } else {
    next->SetNop();
  }
}

void Assembler::PatchShortRangeBranchToVeneer(Buffer* buffer, unsigned rangeIdx,
                                              BufferOffset deadline,
                                              BufferOffset veneer) {
  DEBUG_PRINTF("\tPatchShortRangeBranchToVeneer\n");

  // Reconstruct the position of the branch from (rangeIdx, deadline).
  ImmBranchRangeType branchRange = static_cast<ImmBranchRangeType>(rangeIdx);
  BufferOffset branch(deadline.getOffset() -
                      ImmBranchMaxForwardOffset(branchRange));
  Instruction* branchInst = buffer->getInst(branch);
  MOZ_ASSERT(branchInst->IsBranch() || branchInst->IsJal());

  Instruction* veneerInst_1 = buffer->getInst(veneer);
  Instruction* veneerInst_2 =
      buffer->getInst(BufferOffset(veneer.getOffset() + kInstrSize));

  DEBUG_PRINTF("\t%p(%x): ", branchInst, branch.getOffset());
  disassembleInstr(branchInst);
  DEBUG_PRINTF("\t insert veneer %x, branch: %x deadline: %x\n",
               veneer.getOffset(), branch.getOffset(), deadline.getOffset());

  // Verify that the branch range matches what's encoded.
  MOZ_ASSERT(branchRange <= UncondBranchRangeType);
  MOZ_ASSERT(branchInst->GetImmBranchRangeType() == branchRange);

  // We want to insert veneer after branch in the linked list of instructions
  // that use the same unbound label.
  // The veneer should be an unconditional branch.
  int32_t nextElemOffset = ImmPCRawOffset(branchInst);

  // If nextElemOffset is 0, this is the end of the linked list.
  if (nextElemOffset != kEndOfJumpChain) {
    // Make the offset relative to veneer so it targets the same instruction
    // as branchInst.
    nextElemOffset += branch.getOffset() - veneer.getOffset();
  }

  auto [Hi20, Lo12] = ToHigh20Low12(nextElemOffset);

  // Insert veneer as a long jump.
  veneerInst_1->SetUFormat(RO_AUIPC, t6.code(), Hi20);
  veneerInst_2->SetIFormat(RO_JALR, zero_reg.code(), t6.code(), Lo12);

  // Now link branchInst to veneer.
  int32_t offset = veneer.getOffset() - branch.getOffset();
  if (branchInst->IsBranch()) {
    branchInst->SetBranchOffset(offset);
  } else {
    MOZ_ASSERT(branchInst->IsJal());
    branchInst->SetImm20JValue(offset);
  }

  DEBUG_PRINTF("\tfix to veneer:");
  disassembleInstr(branchInst);
}
}  // namespace jit
}  // namespace js
