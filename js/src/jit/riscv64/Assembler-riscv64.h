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

#ifndef jit_riscv64_Assembler_riscv64_h
#define jit_riscv64_Assembler_riscv64_h

#include "mozilla/Assertions.h"
#include "mozilla/Sprintf.h"

#include <stdint.h>

#include "jit/CompactBuffer.h"
#include "jit/JitCode.h"
#include "jit/JitSpewer.h"
#include "jit/Registers.h"
#include "jit/RegisterSets.h"
#include "jit/riscv64/Architecture-riscv64.h"
#include "jit/riscv64/base/base-assembler-riscv.h"
#include "jit/riscv64/base/base-riscv-i.h"
#include "jit/riscv64/constant/Constant-riscv64.h"
#include "jit/riscv64/extension/extension-riscv-a.h"
#include "jit/riscv64/extension/extension-riscv-b.h"
#include "jit/riscv64/extension/extension-riscv-c.h"
#include "jit/riscv64/extension/extension-riscv-d.h"
#include "jit/riscv64/extension/extension-riscv-f.h"
#include "jit/riscv64/extension/extension-riscv-m.h"
#include "jit/riscv64/extension/extension-riscv-v.h"
#include "jit/riscv64/extension/extension-riscv-zfa.h"
#include "jit/riscv64/extension/extension-riscv-zfh.h"
#include "jit/riscv64/extension/extension-riscv-zicond.h"
#include "jit/riscv64/extension/extension-riscv-zicsr.h"
#include "jit/riscv64/extension/extension-riscv-zifencei.h"
#include "jit/riscv64/Register-riscv64.h"
#include "jit/shared/Assembler-shared.h"
#include "jit/shared/Disassembler-shared.h"
#include "jit/shared/IonAssemblerBufferWithConstantPools.h"
#include "js/HashTable.h"
#include "wasm/WasmTypeDecls.h"
namespace js {
namespace jit {

struct ScratchFloat32Scope : public AutoFloatRegisterScope {
  explicit ScratchFloat32Scope(MacroAssembler& masm)
      : AutoFloatRegisterScope(masm, ScratchFloat32Reg) {}
};

struct ScratchDoubleScope : public AutoFloatRegisterScope {
  explicit ScratchDoubleScope(MacroAssembler& masm)
      : AutoFloatRegisterScope(masm, ScratchDoubleReg) {}
};

struct ScratchFloat32Scope2 : public AutoFloatRegisterScope {
  explicit ScratchFloat32Scope2(MacroAssembler& masm)
      : AutoFloatRegisterScope(masm, ScratchFloat32Reg2) {}
};

struct ScratchDoubleScope2 : public AutoFloatRegisterScope {
  explicit ScratchDoubleScope2(MacroAssembler& masm)
      : AutoFloatRegisterScope(masm, ScratchDoubleReg2) {}
};

class MacroAssembler;

static constexpr uint32_t ABIStackAlignment = 16;
static constexpr uint32_t CodeAlignment = 16;
static constexpr uint32_t JitStackAlignment = 16;
static constexpr uint32_t JitStackValueAlignment =
    JitStackAlignment / sizeof(Value);
static const uint32_t WasmStackAlignment = 16;
static const uint32_t WasmTrapInstructionLength = kInstrSize;
// See comments in wasm::GenerateFunctionPrologue.  The difference between these
// is the size of the largest callable prologue on the platform.
static constexpr uint32_t WasmCheckedCallEntryOffset = 0u;
static constexpr uint32_t WasmCheckedTailEntryOffset = 20u;

static const Scale ScalePointer = TimesEight;

class Assembler;

using Buffer = js::jit::AssemblerBufferWithConstantPools<
    Instruction, Assembler,
    js::jit::AssemblerBufferSettings{
        .instSize = kInstrSize,
        // Guard around veneer branches uses a single 'jal' instruction.
        .guardSize = 1,
        // Constant pools not used, so no header needed.
        .headerSize = 0,
        // Veneer branches use an 'auipc + jalr' instruction pair.
        .veneerSize = 2,
        // No bias needed when constant pool not used.
        .pcBias = 0,
        .alignFillInst = kNopByte,
        .nopFillInst = kNopByte,
        .numShortBranchRanges = NumShortBranchRangeTypes,
    }>;

class Assembler : public AssemblerShared,
                  public AssemblerRISCVI,
                  public AssemblerRISCVA,
                  public AssemblerRISCVB,
                  public AssemblerRISCVF,
                  public AssemblerRISCVD,
                  public AssemblerRISCVM,
                  public AssemblerRISCVC,
                  public AssemblerRISCVZfa,
                  public AssemblerRISCVZfh,
                  public AssemblerRISCVZicond,
                  public AssemblerRISCVZicsr,
                  public AssemblerRISCVZifencei {
  // No maximum pool offset necessary when constant pool not used.
  static constexpr size_t BufferMaxPoolOffset = 0;

  // Number of nop instructions to add before instructions. Can be set to a non-
  // zero value to check instruction locations are correctly referenced.
  static constexpr unsigned BufferNumDebugNopsToInsert = 0;

  GeneralRegisterSet scratch_register_list_;

#ifdef JS_DISASM_RISCV64
  static constexpr const char* const LabelIndent = "                 ";
  static constexpr const char* const TargetIndent = "                    ";

  DisassemblerSpew spew_;
#endif

 protected:
  CompactBufferWriter jumpRelocations_;
  CompactBufferWriter dataRelocations_;
  Buffer m_buffer;
  bool isFinished = false;

  struct RelativePatch {
    // the offset within the code buffer where the value is loaded that
    // we want to fix-up
    BufferOffset offset;
    void* target;
    RelocationKind kind;

    RelativePatch(BufferOffset offset, void* target, RelocationKind kind)
        : offset(offset), target(target), kind(kind) {}
  };

  js::Vector<RelativePatch, 8, SystemAllocPolicy> jumps_;

  void addPendingJump(BufferOffset src, ImmPtr target, RelocationKind kind) {
    enoughMemory_ &= jumps_.append(RelativePatch(src, target.value, kind));
    if (kind == RelocationKind::JITCODE) {
      jumpRelocations_.writeUnsigned(src.getOffset());
    }
  }

  // "auipc" instructions, each followed by an I-type instruction, whose
  // pc-relative offset to |target| is written by executableCopy once the
  // final code address is known.
  js::Vector<RelativePatch, 8, SystemAllocPolicy> pcRelativePatches_;

  void addPcRelativePatch(BufferOffset src, void* target) {
    enoughMemory_ &= pcRelativePatches_.append(
        RelativePatch(src, target, RelocationKind::HARDCODED));
  }

  void addLongJump(BufferOffset src, BufferOffset dst) {
    CodeLabel cl;
    cl.patchAt()->bind(src.getOffset());
    cl.target()->bind(dst.getOffset());
    cl.setLinkMode(CodeLabel::JumpImmediate);
    addCodeLabel(std::move(cl));
  }

 public:
  static bool FLAG_riscv_debug;

  Assembler()
      : scratch_register_list_((1 << t5.code()) | (1 << t4.code()) |
                               (1 << t6.code())),
        m_buffer(BufferMaxPoolOffset, BufferNumDebugNopsToInsert),
        isFinished(false) {
#ifdef JS_DISASM_RISCV64
    spew_.setLabelIndent(LabelIndent);
    spew_.setTargetIndent(TargetIndent);
#endif
  }

  ~Assembler() {
#ifdef JS_DISASM_RISCV64
    spew_.spewOrphans();
#endif
  }

  bool reserve(size_t size);
  bool oom() const;

  void setPrinter(Sprinter* sp) {
#ifdef JS_DISASM_RISCV64
    spew_.setPrinter(sp);
#endif
  }

  void finish() {
    MOZ_ASSERT(!isFinished);
    flush();
    isFinished = true;
  }

  void flush() { m_buffer.flushPool(); }
  void flushBuffer() { m_buffer.flushPool(); }

  void enterNoPool(size_t maxInst, size_t maxNewDeadlines = 0) {
    m_buffer.enterNoPool(maxInst, maxNewDeadlines);
  }
  void leaveNoPool() { m_buffer.leaveNoPool(); }

  void enterNoNops() { m_buffer.enterNoNops(); }
  void leaveNoNops() { m_buffer.leaveNoNops(); }

  bool swapBuffer(wasm::Bytes& bytes);
  // Size of the instruction stream, in bytes.  Note this doesn't take
  // into account the size of any un-flushed constant pools.
  size_t size() const;
  // Returns the size of the buffer we can currently read, hence ignoring any
  // un-flushed data in currently-under-construction constant pool(s).
  size_t readableSize() const;
  // Size of the data table, in bytes.
  size_t bytesNeeded() const;
  // Size of the jump relocation table, in bytes.
  size_t jumpRelocationTableBytes() const;
  size_t dataRelocationTableBytes() const;
  void copyJumpRelocationTable(uint8_t* dest);
  void copyDataRelocationTable(uint8_t* dest);
  // Copy the assembly code to the given buffer, and perform any pending
  // relocations relying on the target address.
  void executableCopy(uint8_t* buffer);

  static void PatchShortRangeBranchToVeneer(Buffer*, unsigned rangeIdx,
                                            BufferOffset deadline,
                                            BufferOffset veneer);
  static void WritePoolGuard(BufferOffset branch, Instruction* inst,
                             BufferOffset dest);

  void processCodeLabels(uint8_t* rawCode);

  // Return the Instruction at a given byte offset.
  Instruction* getInstructionAt(BufferOffset offset) {
    return m_buffer.getInst(offset);
  }

  // Get the next usable buffer offset. Note that a constant pool may be placed
  // here before the next instruction is emitted.
  BufferOffset nextOffset() const { return m_buffer.nextOffset(); }

  // Get the buffer offset of the next inserted instruction. This may flush
  // constant pools and emit veneers.
  BufferOffset nextInstrOffset(unsigned numInsts, unsigned numNewDeadlines) {
    return m_buffer.nextInstrOffset(numInsts, numNewDeadlines);
  }

 protected:
#ifdef JS_DISASM_RISCV64
  void spew(BufferOffset offs, Instruction* instr);
  void spewBranch(BufferOffset offs, Instruction* instr, LabelDoc target);
  LabelDoc refLabel(Label* label);
#else
  LabelDoc refLabel(Label* label) { return {}; }
#endif

 public:
  void comment(const char* msg) {
#ifdef JS_DISASM_RISCV64
    spew_.spew("; %s", msg);
#endif
  }

  enum Condition {
    Overflow = overflow,
    Below = Uless,
    BelowOrEqual = Uless_equal,
    Above = Ugreater,
    AboveOrEqual = Ugreater_equal,
    Equal = equal,
    NotEqual = not_equal,
    GreaterThan = greater,
    GreaterThanOrEqual = greater_equal,
    LessThan = less,
    LessThanOrEqual = less_equal,
    Always = cc_always,
    CarrySet,
    CarryClear,
    Signed,
    NotSigned,
    Zero,
    NonZero,
  };

  enum DoubleCondition {
    // These conditions will only evaluate to true if the comparison is ordered
    // - i.e. neither operand is NaN.
    DoubleOrdered,
    DoubleEqual,
    DoubleNotEqual,
    DoubleGreaterThan,
    DoubleGreaterThanOrEqual,
    DoubleLessThan,
    DoubleLessThanOrEqual,
    // If either operand is NaN, these conditions always evaluate to true.
    DoubleUnordered,
    DoubleEqualOrUnordered,
    DoubleNotEqualOrUnordered,
    DoubleGreaterThanOrUnordered,
    DoubleGreaterThanOrEqualOrUnordered,
    DoubleLessThanOrUnordered,
    DoubleLessThanOrEqualOrUnordered,
  };

  Register getStackPointer() const { return StackPointer; }

#ifdef JS_DISASM_RISCV64
  static void disassembleInstr(Instruction* instr);
  static void disassembleInstr(BufferOffset offs, Instruction* instr);
#else
  static void disassembleInstr(Instruction* instr) {}
#endif

 private:
  BufferOffset jumpChainGetNextLink(BufferOffset pos);

  void jumpChainPutTargetAt(BufferOffset pos, BufferOffset target_pos);

 public:
  // Branch offset for short or long branches.
  int32_t branchOffset(Label* L, OffsetSize bits,
                       BufferOffset next_instr_offset);

  // Branch offset for long branches (auipc + jalr).
  int32_t branchOffset(Label* L);

  // Register branch deadline for forward branches.
  void registerBranchDeadline(Label* L, OffsetSize bits,
                              BufferOffset next_instr_offset);

  void nopAlign(int m) { m_buffer.align(m); }

  BufferOffset emit(Instr x) override final {
    MOZ_ASSERT(hasCreator());
    BufferOffset offset = m_buffer.putInt(x);
#ifdef JS_DISASM_RISCV64
    spew(offset, m_buffer.getInstOrNull(offset));
#endif
    return offset;
  }
  BufferOffset emit(Instr x, LabelDoc doc) override final {
    MOZ_ASSERT(hasCreator());
    BufferOffset offset = m_buffer.putInt(x);
#ifdef JS_DISASM_RISCV64
    spewBranch(offset, m_buffer.getInstOrNull(offset), doc);
#endif
    return offset;
  }
  BufferOffset emit(ShortInstr x) override final { MOZ_CRASH(); }
  BufferOffset emit(uint64_t x) override final { MOZ_CRASH(); }
  BufferOffset emit(uint32_t x) { return m_buffer.putInt(x); }

  static Condition InvertCondition(Condition);

  static DoubleCondition InvertCondition(DoubleCondition);

  static uint64_t ExtractLoad64Value(Instruction* inst0);
  static void UpdateLoad64Value(Instruction* inst0, uint64_t value);
  static void PatchDataWithValueCheck(CodeLocationLabel label, ImmPtr newValue,
                                      ImmPtr expectedValue);
  static void PatchDataWithValueCheck(CodeLocationLabel label,
                                      PatchedImmPtr newValue,
                                      PatchedImmPtr expectedValue);
  static void PatchWrite_Imm32(CodeLocationLabel label, Imm32 imm);

  static void PatchWrite_NearCall(CodeLocationLabel start,
                                  CodeLocationLabel toCall) {
    Instruction* inst = Instruction::At(start.raw());
    uint8_t* dest = toCall.raw();

    // Overwrite whatever instruction used to be here with a call.
    // Always use long jump for two reasons:
    // - Jump has to be the same size because of PatchWrite_NearCallSize.
    // - Return address has to be at the end of replaced block.
    // Short jump wouldn't be more efficient.

    // WriteLiPtrInstructions writes 6 instructions to load an address.
    Assembler::WriteLiPtrInstructions(inst, SavedScratchRegister,
                                      uintptr_t(dest));

    Instruction* jalr = (inst + 6 * kInstrSize);
    jalr->SetIFormat(RO_JALR, ra.code(), SavedScratchRegister.code(), 0);
  }

  static uint32_t PatchWrite_NearCallSize() { return 7 * kInstrSize; }

  static void TraceJumpRelocations(JSTracer* trc, JitCode* code,
                                   CompactBufferReader& reader);
  static void TraceDataRelocations(JSTracer* trc, JitCode* code,
                                   CompactBufferReader& reader);

  static void ToggleToJmp(CodeLocationLabel inst_);
  static void ToggleToCmp(CodeLocationLabel inst_);
  static void ToggleCall(CodeLocationLabel inst_, bool enable);

  static void Bind(uint8_t* rawCode, const CodeLabel& label);
  // label operations
  void bind(Label* label, BufferOffset boff = BufferOffset());
  void bind(CodeLabel* label) { label->target()->bind(currentOffset()); }
  uint32_t currentOffset() override final { return nextOffset().getOffset(); }
  void retarget(Label* label, Label* target);
  static uint32_t NopSize() { return kInstrSize; }

  static uintptr_t GetPointer(uint8_t* instPtr) {
    Instruction* inst = Instruction::At(instPtr);
    return Assembler::ExtractLoad64Value(inst);
  }

  static bool HasRoundInstruction(RoundingMode mode) {
    switch (mode) {
      case RoundingMode::Up:
      case RoundingMode::Down:
      case RoundingMode::NearestTiesToEven:
      case RoundingMode::TowardsZero:
        return true;
    }
    MOZ_CRASH("unexpected mode");
  }

  static bool HasZbaExtension() { return RVFlags::HasZbaExtension(); }

  static bool HasZbbExtension() { return RVFlags::HasZbbExtension(); }

  static bool HasZbsExtension() { return RVFlags::HasZbsExtension(); }

  static bool HasZfhminExtension() { return RVFlags::HasZfhminExtension(); }

  static bool HasZfaExtension() { return RVFlags::HasZfaExtension(); }

  static bool HasZicondExtension() { return RVFlags::HasZicondExtension(); }

  void verifyHeapAccessDisassembly(uint32_t begin, uint32_t end,
                                   const Disassembler::HeapAccess& heapAccess) {
    MOZ_CRASH();
  }

  void setUnlimitedBuffer() { m_buffer.setUnlimited(); }

  GeneralRegisterSet* GetScratchRegisterList() {
    return &scratch_register_list_;
  }

  void writeDataRelocation(ImmGCPtr ptr, BufferOffset offset) {
    // Raw GC pointer relocations and Value relocations both end up in
    // TraceOneDataRelocation.
    if (ptr.value) {
      if (gc::IsInsideNursery(ptr.value)) {
        embedsNurseryPointers_ = true;
      }
      dataRelocations_.writeUnsigned(offset.getOffset());
    }
  }

  bool appendRawCode(const uint8_t* code, size_t numBytes);

  void assertNoGCThings() const {
#ifdef DEBUG
    MOZ_ASSERT(dataRelocations_.length() == 0);
    for (const auto& j : jumps_) {
      MOZ_ASSERT(j.kind == RelocationKind::HARDCODED);
    }
#endif
  }

  // Assembler Pseudo Instructions (Tables 25.2, 25.3, RISC-V Unprivileged ISA)
  void break_(uint32_t code, bool break_as_stop = false);

  // Load an immediate. A variable number of instructions is generated.
  //
  // In most cases the immediate is 32-bit and 2 instructions are generated. If
  // a temporary register is available, in the worst case, 6 instructions are
  // generated for a full 64-bit immediate. If temporay register isn't available
  // the maximum will be 8 instructions.
  void RV_li(Register rd, int64_t imm);

  // Loads an immediate, always using 8 instructions, regardless of the value,
  // so that it can be modified later.
  BufferOffset li_constant(Register rd, int64_t imm);

  // Loads a sign-extended 48-bit immediate, such as a pointer, always using 6
  // instructions, regardless of the value, so that it can be modified later.
  BufferOffset li_ptr(Register rd, int64_t imm);

  static constexpr bool IsLiPtrImmediate(int64_t imm) {
    return imm == (imm << 16) >> 16;
  }

  void SignExtendByte(Register rd, Register rs) {
    if (HasZbbExtension()) {
      sext_b(rd, rs);
      return;
    }
    slli(rd, rs, 56);
    srai(rd, rd, 56);
  }
  void ZeroExtendByte(Register rd, Register rs) { zext_b(rd, rs); }

  void SignExtendShort(Register rd, Register rs) {
    if (HasZbbExtension()) {
      sext_h(rd, rs);
      return;
    }
    slli(rd, rs, 48);
    srai(rd, rd, 48);
  }
  void ZeroExtendShort(Register rd, Register rs) {
    if (HasZbbExtension()) {
      zext_h(rd, rs);
      return;
    }
    slli(rd, rs, 48);
    srli(rd, rd, 48);
  }

  void SignExtendWord(Register rd, Register rs) { sext_w(rd, rs); }
  void ZeroExtendWord(Register rd, Register rs) {
    if (HasZbaExtension()) {
      zext_w(rd, rs);
      return;
    }
    slli(rd, rs, 32);
    srli(rd, rd, 32);
  }

 protected:
  // Load the value from the six instruction sequence starting at |instr|.
  //
  // Also see Assembler::li_ptr.
  static uintptr_t LoadLiPtrInstructions(Instruction* instr);

  // Updates the six instruction sequence to load |value| into a register.
  //
  // Also see Assembler::li_ptr.
  static void UpdateLiPtrInstructions(Instruction* instr, uintptr_t value);

  // Write the six instruction sequence to load |value| into |reg|.
  //
  // The instruction sequence at |instr| must either be an existing li_ptr
  // immediate or a sequence of six nop instructions.
  //
  // Also see Assembler::li_ptr.
  static void WriteLiPtrInstructions(Instruction* instr, Register reg,
                                     uintptr_t value);

  // Load the value from the eight instruction sequence starting at |instr|.
  //
  // Also see Assembler::li_constant.
  static int64_t LoadLiConstantInstructions(Instruction* instr);

  // Updates the eight instruction sequence to load |value| into a register.
  //
  // Also see Assembler::li_constant.
  static void UpdateLiConstantInstructions(Instruction* instr, int64_t value);
};

class ABIArgGenerator : public ABIArgGeneratorShared {
 public:
  explicit ABIArgGenerator(ABIKind kind)
      : ABIArgGeneratorShared(kind),
        intRegIndex_(0),
        floatRegIndex_(0),
        current_() {}

  ABIArg next(MIRType);
  ABIArg& current() { return current_; }

 protected:
  unsigned intRegIndex_;
  unsigned floatRegIndex_;
  ABIArg current_;
};

class UseScratchRegisterScope {
 public:
  explicit UseScratchRegisterScope(Assembler& assembler);
  explicit UseScratchRegisterScope(Assembler* assembler);
  ~UseScratchRegisterScope();

  Register Acquire();
  void Acquire(Register reg);
  void Release(Register reg);
  bool hasAvailable() const;
  uint32_t countAvailable() const;

 private:
  GeneralRegisterSet* available_;
  GeneralRegisterSet old_available_;
};

// Register or immediate operand.
class Operand {
  enum Tag { REG, IMM };

 public:
  explicit Operand(Register rm) : tag(REG), rm_(rm.code()) {}
  explicit Operand(int64_t immediate) : tag(IMM), value_(immediate) {}

  bool is_reg() const { return tag == REG; }
  bool is_imm() const { return tag == IMM; }

  int64_t immediate() const {
    MOZ_ASSERT(is_imm());
    return value_;
  }

  Register rm() const {
    MOZ_ASSERT(is_reg());
    return Register::FromCode(rm_);
  }

 private:
  Tag tag;
  union {
    uint32_t rm_;    // valid if tag == REG
    int64_t value_;  // valid if tag == IMM
  };
};

static const uint32_t NumIntArgRegs = 8;
static const uint32_t NumFloatArgRegs = 8;
static inline bool GetIntArgReg(uint32_t usedIntArgs, Register* out) {
  if (usedIntArgs < NumIntArgRegs) {
    *out = Register::FromCode(a0.code() + usedIntArgs);
    return true;
  }
  return false;
}

static inline bool GetFloatArgReg(uint32_t usedFloatArgs, FloatRegister* out) {
  if (usedFloatArgs < NumFloatArgRegs) {
    *out = FloatRegister::FromCode(fa0.encoding() + usedFloatArgs);
    return true;
  }
  return false;
}

// Get a register in which we plan to put a quantity that will be used as an
// integer argument. This differs from GetIntArgReg in that if we have no more
// actual argument registers to use we will fall back on using whatever
// CallTempReg* don't overlap the argument registers, and only fail once those
// run out too.
static inline bool GetTempRegForIntArg(uint32_t usedIntArgs,
                                       uint32_t usedFloatArgs, Register* out) {
  // NOTE: We can't properly determine which regs are used if there are
  // float arguments. If this is needed, we will have to guess.
  MOZ_ASSERT(usedFloatArgs == 0);

  if (GetIntArgReg(usedIntArgs, out)) {
    return true;
  }
  // Unfortunately, we have to assume things about the point at which
  // GetIntArgReg returns false, because we need to know how many registers it
  // can allocate.
  usedIntArgs -= NumIntArgRegs;
  if (usedIntArgs >= NumCallTempNonArgRegs) {
    return false;
  }
  *out = CallTempNonArgRegs[usedIntArgs];
  return true;
}

// Forbids nop filling for testing purposes. Nestable, but nested calls have
// no effect on the no-nops status; it is only the top level one that counts.
class AutoForbidNops {
  Assembler* asm_;

 public:
  explicit AutoForbidNops(Assembler* asm_) : asm_(asm_) { asm_->enterNoNops(); }
  ~AutoForbidNops() { asm_->leaveNoNops(); }

  AutoForbidNops(const AutoForbidNops&) = delete;
  AutoForbidNops& operator=(const AutoForbidNops&) = delete;
};

// Forbids pool generation during a specified interval. Nestable, but nested
// calls must imply a no-pool area of the assembler buffer that is completely
// contained within the area implied by the outermost level call.
class AutoForbidPoolsAndNops {
  Assembler* asm_;

 public:
  explicit AutoForbidPoolsAndNops(Assembler* assem, size_t margin,
                                  size_t maxBranches = 0)
      : asm_(assem) {
    asm_->enterNoPool(margin, maxBranches);
    asm_->enterNoNops();
  }
  ~AutoForbidPoolsAndNops() {
    asm_->leaveNoNops();
    asm_->leaveNoPool();
  }

  AutoForbidPoolsAndNops(const AutoForbidPoolsAndNops&) = delete;
  AutoForbidPoolsAndNops& operator=(const AutoForbidPoolsAndNops&) = delete;
};

}  // namespace jit
}  // namespace js
#endif /* jit_riscv64_Assembler_riscv64_h */
