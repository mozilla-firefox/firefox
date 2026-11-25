/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef jit_riscv64_MacroAssembler_riscv64_h
#define jit_riscv64_MacroAssembler_riscv64_h

#include "mozilla/Maybe.h"

#include "jit/MoveResolver.h"
#include "jit/riscv64/Assembler-riscv64.h"
#include "wasm/WasmTypeDecls.h"

namespace js {
namespace jit {

static Register CallReg = t6;

class CompactBufferReader;
enum LoadStoreSize {
  SizeByte = 8,
  SizeHalfWord = 16,
  SizeWord = 32,
  SizeDouble = 64
};

enum LoadStoreExtension { ZeroExtend = 0, SignExtend = 1 };
enum JumpKind { LongJump = 0, ShortJump = 1 };

class ScratchTagScope {
  UseScratchRegisterScope temps_;
  Register scratch_;
  bool owned_;
  mozilla::DebugOnly<bool> released_;

 public:
  ScratchTagScope(Assembler& masm, const ValueOperand&)
      : temps_(masm), owned_(true), released_(false) {
    scratch_ = temps_.Acquire();
  }

  operator Register() {
    MOZ_ASSERT(!released_);
    return scratch_;
  }

  void release() {
    MOZ_ASSERT(!released_);
    released_ = true;
    if (owned_) {
      temps_.Release(scratch_);
      owned_ = false;
    }
  }

  void reacquire() {
    MOZ_ASSERT(released_);
    released_ = false;
    if (!owned_) {
      scratch_ = temps_.Acquire();
      owned_ = true;
    }
  }
};

class ScratchTagScopeRelease {
  ScratchTagScope* ts_;

 public:
  explicit ScratchTagScopeRelease(ScratchTagScope* ts) : ts_(ts) {
    ts_->release();
  }
  ~ScratchTagScopeRelease() { ts_->reacquire(); }
};

struct ImmShiftedTag : public ImmWord {
  explicit ImmShiftedTag(JSValueType type)
      : ImmWord(uintptr_t(JSValueShiftedTag(JSVAL_TYPE_TO_SHIFTED_TAG(type)))) {
  }
};

struct ImmTag : public Imm32 {
  explicit ImmTag(JSValueTag mask) : Imm32(int32_t(mask)) {}
};

struct ImmTagSignExt : public Imm32 {
  static constexpr int32_t signExtend(JSValueTag mask) {
    int64_t tag = (int64_t(mask) << JSVAL_TAG_SHIFT) >> JSVAL_TAG_SHIFT;
    MOZ_ASSERT(is_int12(tag));
    return tag;
  }

  explicit ImmTagSignExt(JSValueTag mask) : Imm32(signExtend(mask)) {}
};

class MacroAssemblerRiscv64 : public Assembler {
 public:
  MacroAssemblerRiscv64() {}

#ifdef JS_SIMULATOR_RISCV64
  // See riscv64/base-constants-riscv.h DebugParameters.
  void Debug(uint32_t parameters) { break_(parameters, false); }
#endif

  // Perform a downcast. Should be removed by Bug 996602.
  MacroAssembler& asMasm();
  const MacroAssembler& asMasm() const;

  MoveResolver moveResolver_;

  static bool SupportsFloatingPoint() { return true; }
  static bool SupportsUnalignedAccesses() { return true; }
  static bool SupportsFastUnalignedFPAccesses() { return true; }
  static bool SupportsFloat64To16() { return HasZfhminExtension(); }
  static bool SupportsFloat32To16() { return HasZfhminExtension(); }

  void haltingAlign(int alignment) {
    // TODO(loong64): Implement a proper halting align.
    nopAlign(alignment);
  }

  int32_t GetOffset(Label* L, OffsetSize bits) {
    return Assembler::branchOffsetHelper(L, bits);
  }

  std::pair<Register, int16_t> computeAddress(Address address,
                                              UseScratchRegisterScope& temps);

  // load
  FaultingCodeOffset ma_load(Register dest, Address address,
                             LoadStoreSize size = SizeWord,
                             LoadStoreExtension extension = SignExtend);
  FaultingCodeOffset ma_load(Register dest, const BaseIndex& src,
                             LoadStoreSize size = SizeWord,
                             LoadStoreExtension extension = SignExtend);
  FaultingCodeOffset ma_loadDouble(FloatRegister dest, Address address);
  FaultingCodeOffset ma_loadDouble(FloatRegister dest, const BaseIndex& src);
  FaultingCodeOffset ma_loadFloat(FloatRegister dest, Address address);
  FaultingCodeOffset ma_loadFloat(FloatRegister dest, const BaseIndex& src);
  FaultingCodeOffset ma_loadFloat16(FloatRegister dest, Address address);
  FaultingCodeOffset ma_loadFloat16(FloatRegister dest, const BaseIndex& src);

  // store
  FaultingCodeOffset ma_store(Register data, Address address,
                              LoadStoreSize size = SizeWord,
                              LoadStoreExtension extension = SignExtend);
  FaultingCodeOffset ma_store(Register data, const BaseIndex& dest,
                              LoadStoreSize size = SizeWord,
                              LoadStoreExtension extension = SignExtend);
  FaultingCodeOffset ma_store(Imm32 imm, const BaseIndex& dest,
                              LoadStoreSize size = SizeWord,
                              LoadStoreExtension extension = SignExtend);
  FaultingCodeOffset ma_store(Imm32 imm, Address address,
                              LoadStoreSize size = SizeWord,
                              LoadStoreExtension extension = SignExtend);
  FaultingCodeOffset ma_storeDouble(FloatRegister src, Address address);
  FaultingCodeOffset ma_storeDouble(FloatRegister src, const BaseIndex& dest);
  FaultingCodeOffset ma_storeFloat(FloatRegister src, Address address);
  FaultingCodeOffset ma_storeFloat(FloatRegister src, const BaseIndex& dest);
  FaultingCodeOffset ma_storeFloat16(FloatRegister src, Address address);
  FaultingCodeOffset ma_storeFloat16(FloatRegister src, const BaseIndex& dest);

  // immediates
  BufferOffset ma_liPatchable(Register dest, Imm32 imm);
  BufferOffset ma_liPatchable(Register dest, ImmPtr imm) {
    return li_ptr(dest, uintptr_t(imm.value));
  }
  BufferOffset ma_liPatchable(Register dest, ImmWord imm) {
    return li_constant(dest, imm.value);
  }
  void ma_li(Register dest, ImmGCPtr ptr);
  void ma_li(Register dest, Imm32 imm);
  void ma_li(Register dest, Imm64 imm);
  void ma_li(Register dest, intptr_t imm) { RV_li(dest, imm); }
  void ma_li(Register dest, CodeLabel* label);
  void ma_li(Register dest, ImmWord imm);

  void patchLi32(CodeOffset offset, Imm32 imm);

#define DEFINE_INSTRUCTION(instr)                     \
  void instr(Register rd, Register rs, Imm64 imm);    \
  void instr(Register rd, Register rs, Imm32 imm) {   \
    instr(rd, rs, Imm64(imm.value));                  \
  }                                                   \
  void instr(Register rd, Register rs, ImmWord imm) { \
    instr(rd, rs, Imm64(imm.value));                  \
  }

#define DEFINE_INSTRUCTION_I32(instr) \
  void instr(Register rd, Register rs, Imm32 imm);

  DEFINE_INSTRUCTION(ma_and)
  DEFINE_INSTRUCTION(ma_or)
  DEFINE_INSTRUCTION(ma_xor)
  DEFINE_INSTRUCTION_I32(ma_sub32)
  DEFINE_INSTRUCTION(ma_sub64)
  DEFINE_INSTRUCTION_I32(ma_add32)
  DEFINE_INSTRUCTION(ma_add64)
  DEFINE_INSTRUCTION_I32(ma_mul32)
  DEFINE_INSTRUCTION_I32(ma_mulhu32)
  DEFINE_INSTRUCTION(ma_mul64)

#undef DEFINE_INSTRUCTION
#undef DEFINE_INSTRUCTION_I32

  // arithmetic based ops
  void ma_add32TestOverflow(Register rd, Register rj, Register rk,
                            Label* overflow);
  void ma_add32TestOverflow(Register rd, Register rj, Imm32 imm,
                            Label* overflow);
  void ma_addPtrTestOverflow(Register rd, Register rj, Register rk,
                             Label* overflow);
  void ma_addPtrTestOverflow(Register rd, Register rj, Imm32 imm,
                             Label* overflow);
  void ma_addPtrTestOverflow(Register rd, Register rj, ImmWord imm,
                             Label* overflow);
  void ma_addPtrTestCarry(Condition cond, Register rd, Register rj, Register rk,
                          Label* overflow);
  void ma_addPtrTestCarry(Condition cond, Register rd, Register rj, Imm32 imm,
                          Label* overflow);
  void ma_addPtrTestCarry(Condition cond, Register rd, Register rj, ImmWord imm,
                          Label* overflow);
  void ma_addPtrTestSigned(Condition cond, Register rd, Register rj,
                           Register rk, Label* taken);
  void ma_addPtrTestSigned(Condition cond, Register rd, Register rj, Imm32 imm,
                           Label* taken);
  void ma_addPtrTestSigned(Condition cond, Register rd, Register rj,
                           ImmWord imm, Label* taken);

  // subtract
  void ma_sub32TestOverflow(Register rd, Register rj, Register rk,
                            Label* overflow);
  void ma_subPtrTestOverflow(Register rd, Register rj, Register rk,
                             Label* overflow);
  void ma_subPtrTestOverflow(Register rd, Register rj, Imm32 imm,
                             Label* overflow);

  // multiplies.  For now, there are only few that we care about.
  void ma_mulPtrTestOverflow(Register rd, Register rj, Register rk,
                             Label* overflow);

  // branches when done from within la-specific code
  void ma_b(Register lhs, Register rhs, Label* l, Condition c,
            JumpKind jumpKind = LongJump);
  void ma_b(Register lhs, Imm32 imm, Label* l, Condition c,
            JumpKind jumpKind = LongJump);
  void ma_b(Register lhs, ImmWord imm, Label* l, Condition c,
            JumpKind jumpKind = LongJump);
  void ma_b(Register lhs, ImmPtr imm, Label* l, Condition c,
            JumpKind jumpKind = LongJump) {
    ma_b(lhs, ImmWord(uintptr_t(imm.value)), l, c, jumpKind);
  }
  void ma_b(Register lhs, ImmGCPtr imm, Label* l, Condition c,
            JumpKind jumpKind = LongJump) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    ma_b(lhs, scratch, l, c, jumpKind);
  }

 private:
  void ma_branch(Label* target, Condition cond, Register r1, const Operand& r2,
                 JumpKind jumpKind);

  void ma_branch(Label* target, JumpKind jumpKind) {
    ma_branch(target, Always, zero, Operand(zero), jumpKind);
  }

 public:
  // fp instructions
  void ma_lid(FloatRegister dest, double value);
  void ma_lis(FloatRegister dest, float value);

  void ma_fmv_d(FloatRegister src, ValueOperand dest);
  void ma_fmv_d(ValueOperand src, FloatRegister dest);

  void ma_fmv_w(FloatRegister src, ValueOperand dest);
  void ma_fmv_w(ValueOperand src, FloatRegister dest);

  // stack
  void ma_pop(Register r);
  void ma_push(Register r);
  void ma_pop(FloatRegister f);
  void ma_push(FloatRegister f);

  void ma_cmp_set(Register dst, Register lhs, ImmWord imm, Condition c);
  void ma_cmp_set(Register dst, Register lhs, ImmPtr imm, Condition c);
  void ma_cmp_set(Register dst, Register lhs, ImmGCPtr imm, Condition c);
  void ma_cmp_set(Register dst, Address address, Register rhs, Condition c);
  void ma_cmp_set(Register dst, Address address, Imm32 imm, Condition c);
  void ma_cmp_set(Register dst, Address address, ImmWord imm, Condition c);

  // arithmetic based ops
  void ma_add32TestCarry(Condition cond, Register rd, Register rj, Register rk,
                         Label* overflow);
  void ma_add32TestCarry(Condition cond, Register rd, Register rj, Imm32 imm,
                         Label* overflow);

  // subtract
  void ma_sub32TestOverflow(Register rd, Register rj, Imm32 imm,
                            Label* overflow);

  // multiplies.  For now, there are only few that we care about.
  void ma_mul32TestOverflow(Register rd, Register rj, Register rk,
                            Label* overflow);
  void ma_mul32TestOverflow(Register rd, Register rj, Imm32 imm,
                            Label* overflow);

  // fast mod, uses scratch registers, and thus needs to be in the assembler
  // implicitly assumes that we can overwrite dest at the beginning of the
  // sequence
  void ma_mod_mask(Register src, Register dest, Register hold, Register remain,
                   int32_t shift, Label* negZero = nullptr);

  // FP branches
  void ma_compareF32(Register rd, DoubleCondition cc, FloatRegister cmp1,
                     FloatRegister cmp2);
  void ma_compareF64(Register rd, DoubleCondition cc, FloatRegister cmp1,
                     FloatRegister cmp2);

  void CompareIsNotNanF32(Register rd, FPURegister cmp1, FPURegister cmp2);
  void CompareIsNotNanF64(Register rd, FPURegister cmp1, FPURegister cmp2);
  void CompareIsNanF32(Register rd, FPURegister cmp1, FPURegister cmp2);
  void CompareIsNanF64(Register rd, FPURegister cmp1, FPURegister cmp2);

  BufferOffset ma_call(ImmPtr dest);

  BufferOffset ma_jump(ImmPtr dest);

  void jump(Label* label) { ma_branch(label, ShortJump); }
  void jump(Register reg) { jr(reg); }

  void ma_cmp_set(Register dst, Register lhs, Register rhs, Condition c);
  void ma_cmp_set(Register dst, Register lhs, Imm32 imm, Condition c);

  // Conditional moves.
  void ma_cmp_mv(Register dst, Register lhs, Register rhs, Register src,
                 Condition c);
  void ma_cmp_mv(Register dst, Register lhs, Imm32 rhs, Register src,
                 Condition c);

  // Conditional select.
  void ma_cselz(Register rd, Register rs1, Register rs2, Register rc,
                Register rtmp);
  void ma_cselnz(Register rd, Register rs1, Register rs2, Register rc,
                 Register rtmp);

  void computeScaledAddress(const BaseIndex& address, Register dest);
  void computeScaledAddress32(const BaseIndex& address, Register dest);

 private:
  bool UseShortBranch(Label* L, JumpKind jumpKind, OffsetSize bits,
                      mozilla::Maybe<AutoForbidNops>& maybeAfn);

  void Branch(Label* L, JumpKind jumpKind);
  void Branch(Label* L, Condition cond, Register rs, const Operand& rt,
              JumpKind jumpKind);

  void BranchShort(Label* L, Condition cond, Register rs, Register rt);
  void BranchLong(Label* L);

 protected:
  BufferOffset BranchShort(Label* L);
  CodeOffset BranchAndLink(Label* label);

 public:
  // Floating point branches
  void BranchFloat32(DoubleCondition cc, FloatRegister frs1, FloatRegister frs2,
                     Label* label, JumpKind jumpKind);
  void BranchFloat64(DoubleCondition cc, FloatRegister frs1, FloatRegister frs2,
                     Label* label, JumpKind jumpKind);

  void moveFromDoubleHi(FloatRegister src, Register dest) {
    fmv_x_d(dest, src);
    srli(dest, dest, 32);
  }

  // Bit field starts at bit pos and extending for size bits is extracted from
  // rs and stored zero-extended and right-justified in rd.
  void ExtractBits(Register rd, Register rs, uint16_t pos, uint16_t size);

  template <typename F_TYPE>
  void RoundHelper(FPURegister dst, FPURegister src, FPURoundingMode mode);

  template <typename CvtFunc>
  void RoundFloatingPointToInteger(Register rd, FPURegister fs, Register result,
                                   CvtFunc fcvt_generator,
                                   bool Inexact = false);

  void Clear_if_nan_d(Register rd, FPURegister fs);
  void Clear_if_nan_s(Register rd, FPURegister fs);

  // Convert double to unsigned word.
  void Trunc_uw_d(Register rd, FPURegister fs, Register result = InvalidReg,
                  bool Inexact = false);

  // Convert double to signed word.
  void Trunc_w_d(Register rd, FPURegister fs, Register result = InvalidReg,
                 bool Inexact = false);

  // Convert double to unsigned long.
  void Trunc_ul_d(Register rd, FPURegister fs, Register result = InvalidReg,
                  bool Inexact = false);

  // Convert single to signed long.
  void Trunc_l_d(Register rd, FPURegister fs, Register result = InvalidReg,
                 bool Inexact = false);

  // Convert single to signed word.
  void Trunc_w_s(Register rd, FPURegister fs, Register result = InvalidReg,
                 bool Inexact = false);

  // Convert single to unsigned word.
  void Trunc_uw_s(Register rd, FPURegister fs, Register result = InvalidReg,
                  bool Inexact = false);

  // Convert single to unsigned long.
  void Trunc_ul_s(Register rd, FPURegister fs, Register result = InvalidReg,
                  bool Inexact = false);

  // Convert single to signed long.
  void Trunc_l_s(Register rd, FPURegister fs, Register result = InvalidReg,
                 bool Inexact = false);

  // Round double functions
  void Trunc_d_d(FPURegister fd, FPURegister fs);
  void Round_d_d(FPURegister fd, FPURegister fs);
  void Floor_d_d(FPURegister fd, FPURegister fs);
  void Ceil_d_d(FPURegister fd, FPURegister fs);

  // Round float functions
  void Trunc_s_s(FPURegister fd, FPURegister fs);
  void Round_s_s(FPURegister fd, FPURegister fs);
  void Floor_s_s(FPURegister fd, FPURegister fs);
  void Ceil_s_s(FPURegister fd, FPURegister fs);

  // Round single to signed word.
  void Round_w_s(Register rd, FPURegister fs, Register result = InvalidReg,
                 bool Inexact = false);

  // Round double to signed word.
  void Round_w_d(Register rd, FPURegister fs, Register result = InvalidReg,
                 bool Inexact = false);

  // Ceil single to signed word.
  void Ceil_w_s(Register rd, FPURegister fs, Register result = InvalidReg,
                bool Inexact = false);

  // Ceil double to signed word.
  void Ceil_w_d(Register rd, FPURegister fs, Register result = InvalidReg,
                bool Inexact = false);

  // Ceil single to signed long.
  void Ceil_l_s(Register rd, FPURegister fs, Register result = InvalidReg,
                bool Inexact = false);

  // Ceil double to signed long.
  void Ceil_l_d(Register rd, FPURegister fs, Register result = InvalidReg,
                bool Inexact = false);

  // Floor single to signed word.
  void Floor_w_s(Register rd, FPURegister fs, Register result = InvalidReg,
                 bool Inexact = false);

  // Floor double to signed word.
  void Floor_w_d(Register rd, FPURegister fs, Register result = InvalidReg,
                 bool Inexact = false);

  // Floor single to signed long.
  void Floor_l_s(Register rd, FPURegister fs, Register result = InvalidReg,
                 bool Inexact = false);

  // Floor double to signed long.
  void Floor_l_d(Register rd, FPURegister fs, Register result = InvalidReg,
                 bool Inexact = false);

  // Round single to signed long, ties to max magnitude (or away from zero).
  void RoundMaxMag_l_s(Register rd, FPURegister fs,
                       Register result = InvalidReg, bool Inexact = false);

  // Round double to signed long, ties to max magnitude (or away from zero).
  void RoundMaxMag_l_d(Register rd, FPURegister fs,
                       Register result = InvalidReg, bool Inexact = false);

  void Clz32(Register rd, Register rs);
  void Ctz32(Register rd, Register rs);
  void Popcnt32(Register rd, Register rs, Register scratch);

  void Popcnt64(Register rd, Register rs, Register scratch);
  void Ctz64(Register rd, Register rs);
  void Clz64(Register rd, Register rs);

  // Change endianness
  void ByteSwap(Register dest, Register src, int operand_size,
                bool zeroExtend = false);

  void Rol(Register rd, Register rs, Imm32 rt);
  void Rol(Register rd, Register rs, Register rt);

  void Drol(Register rd, Register rs, Imm32 rt);
  void Drol(Register rd, Register rs, Register rt);

  void Ror(Register rd, Register rs, Imm32 rt);
  void Ror(Register rd, Register rs, Register rt);

  void Dror(Register rd, Register rs, Imm32 rt);
  void Dror(Register rd, Register rs, Register rt);

  void Float32Max(FPURegister dst, FPURegister src1, FPURegister src2);
  void Float32Min(FPURegister dst, FPURegister src1, FPURegister src2);
  void Float64Max(FPURegister dst, FPURegister src1, FPURegister src2);
  void Float64Min(FPURegister dst, FPURegister src1, FPURegister src2);

  template <typename F>
  void FloatMinMaxHelper(FPURegister dst, FPURegister src1, FPURegister src2,
                         MaxMinKind kind);

  inline void NegateBool(Register rd, Register rs) { xori(rd, rs, 1); }

 protected:
  void wasmLoadImpl(const wasm::MemoryAccessDesc& access, Register memoryBase,
                    Register ptr, AnyRegister output);
  void wasmStoreImpl(const wasm::MemoryAccessDesc& access, AnyRegister value,
                     Register memoryBase, Register ptr);
};

class MacroAssemblerRiscv64Compat : public MacroAssemblerRiscv64 {
 public:
  using MacroAssemblerRiscv64::call;

  MacroAssemblerRiscv64Compat() {}

  void convertBoolToInt32(Register src, Register dest) {
    andi(dest, src, 0xff);
  };
  void convertInt32ToDouble(Register src, FloatRegister dest) {
    fcvt_d_w(dest, src);
  };
  void convertInt32ToDouble(const Address& src, FloatRegister dest) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ma_load(scratch, src, SizeWord, SignExtend);
    fcvt_d_w(dest, scratch);
  };
  void convertInt32ToDouble(const BaseIndex& src, FloatRegister dest) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    MOZ_ASSERT(scratch != src.base);
    MOZ_ASSERT(scratch != src.index);
    computeScaledAddress(src, scratch);
    convertInt32ToDouble(Address(scratch, src.offset), dest);
  };
  void convertUInt32ToDouble(Register src, FloatRegister dest);
  void convertUInt32ToFloat32(Register src, FloatRegister dest);
  void convertDoubleToFloat32(FloatRegister src, FloatRegister dest);
  void convertDoubleToInt32(FloatRegister src, Register dest, Label* fail,
                            bool negativeZeroCheck = true);
  void convertDoubleToPtr(FloatRegister src, Register dest, Label* fail,
                          bool negativeZeroCheck = true);
  void convertFloat32ToInt32(FloatRegister src, Register dest, Label* fail,
                             bool negativeZeroCheck = true);

  void convertFloat32ToDouble(FloatRegister src, FloatRegister dest);
  void convertInt32ToFloat32(Register src, FloatRegister dest);
  void convertInt32ToFloat32(const Address& src, FloatRegister dest);

  void convertDoubleToFloat16(FloatRegister src, FloatRegister dest) {
    MOZ_ASSERT(HasZfhminExtension());
    fcvt_h_d(dest, src);
  }
  void convertFloat16ToDouble(FloatRegister src, FloatRegister dest) {
    MOZ_ASSERT(HasZfhminExtension());
    fcvt_d_h(dest, src);
  }
  void convertFloat32ToFloat16(FloatRegister src, FloatRegister dest) {
    MOZ_ASSERT(HasZfhminExtension());
    fcvt_h_s(dest, src);
  }
  void convertFloat16ToFloat32(FloatRegister src, FloatRegister dest) {
    MOZ_ASSERT(HasZfhminExtension());
    fcvt_s_h(dest, src);
  }
  void convertInt32ToFloat16(Register src, FloatRegister dest) {
    MOZ_ASSERT(HasZfhminExtension());
    // `fcvt.h.w` requires full Zfh support, not just Zfhmin. Therefore we need
    // to perform the sequence `fcvt.d.w` followed by `fcvt.h.d`.
    fcvt_d_w(dest, src);
    fcvt_h_d(dest, dest);
  }

  void truncateFloat32ModUint32(FloatRegister src, Register dest);

  void computeEffectiveAddress(const Address& address, Register dest) {
    ma_add64(dest, address.base, Imm32(address.offset));
  }

  void computeEffectiveAddress(const BaseIndex& address, Register dest) {
    computeScaledAddress(address, dest);
    if (address.offset) {
      ma_add64(dest, dest, Imm32(address.offset));
    }
  }

  void computeEffectiveAddress32(const Address& address, Register dest) {
    ma_add32(dest, address.base, Imm32(address.offset));
  }

  void computeEffectiveAddress32(const BaseIndex& address, Register dest) {
    computeScaledAddress32(address, dest);
    if (address.offset) {
      ma_add32(dest, dest, Imm32(address.offset));
    }
  }

  void j(Label* dest) { jump(dest); }

  void mov(Register src, Register dest) { mv(dest, src); }
  void mov(ImmWord imm, Register dest) { ma_li(dest, imm); }
  void mov(ImmPtr imm, Register dest) {
    mov(ImmWord(uintptr_t(imm.value)), dest);
  }
  void mov(CodeLabel* label, Register dest) { ma_li(dest, label); }
  void mov(Register src, Address dest) { MOZ_CRASH("NYI-IC"); }
  void mov(Address src, Register dest) { MOZ_CRASH("NYI-IC"); }

  void writeDataRelocation(const Value& val, CodeOffset offset) {
    MOZ_ASSERT(val.isGCThing(), "only called for gc-things");

    // Raw GC pointer relocations and Value relocations both end up in
    // TraceOneDataRelocation.
    gc::Cell* cell = val.toGCThing();
    if (cell && gc::IsInsideNursery(cell)) {
      embedsNurseryPointers_ = true;
    }
    dataRelocations_.writeUnsigned(offset.offset());
  }

  void branch(JitCode* c) {
    // 6 instruction to materialize the constant.
    // + 1 instruction for jr.
    AutoForbidPoolsAndNops afp(this, 7);

    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    BufferOffset bo = ma_liPatchable(scratch, ImmPtr(c->raw()));
    addPendingJump(bo, ImmPtr(c->raw()), RelocationKind::JITCODE);
    jr(scratch);
  }
  void branch(const Register reg) { jr(reg); }
  BufferOffset ret() {
    ma_pop(ra);
    return jalr(zero_reg, ra, 0);
  }
  inline void retn(Imm32 n);
  void push(Imm32 imm) {
    if (imm.value == 0) {
      ma_push(zero_reg);
    } else {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      ma_li(scratch, imm);
      ma_push(scratch);
    }
  }
  void push(ImmWord imm) {
    if (imm.value == 0) {
      ma_push(zero_reg);
    } else {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      ma_li(scratch, imm);
      ma_push(scratch);
    }
  }
  void push(ImmGCPtr imm) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    ma_push(scratch);
  }
  void push(const Address& address) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    loadPtr(address, scratch);
    ma_push(scratch);
  }
  void push(Register reg) { ma_push(reg); }
  void push(FloatRegister reg) { ma_push(reg); }
  void pop(Register reg) { ma_pop(reg); }
  void pop(FloatRegister reg) { ma_pop(reg); }

  // Emit a branch that can be toggled to a non-operation. On LOONG64 we use
  // "andi" instruction to toggle the branch.
  // See ToggleToJmp(), ToggleToCmp().
  CodeOffset toggledJump(Label* label);

  // Emit a "jalr" or "nop" instruction. ToggleCall can be used to patch
  // this instruction.
  CodeOffset toggledCall(JitCode* target, bool enabled);

  static size_t ToggledCallSize(uint8_t* code) {
    // Seven instructions used in: MacroAssemblerRiscv64Compat::toggledCall
    return 7 * kInstrSize;
  }

  CodeOffset pushWithPatch(ImmWord imm) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    CodeOffset offset = movWithPatch(imm, scratch);
    ma_push(scratch);
    return offset;
  }

  CodeOffset movWithPatch(ImmWord imm, Register dest) {
    BufferOffset offset = ma_liPatchable(dest, imm);
    return CodeOffset(offset.getOffset());
  }
  CodeOffset movWithPatch(ImmPtr imm, Register dest) {
    BufferOffset offset = ma_liPatchable(dest, imm);
    return CodeOffset(offset.getOffset());
  }

  void writeCodePointer(CodeLabel* label) {
    m_buffer.assertNoPoolAndNoNops();

    label->patchAt()->bind(currentOffset());
    label->setLinkMode(CodeLabel::RawPointer);
    emit(uint32_t(-1));
    emit(uint32_t(-1));
  }

  void jump(Label* label) { MacroAssemblerRiscv64::jump(label); }
  void jump(Register reg) { MacroAssemblerRiscv64::jump(reg); }
  void jump(const Address& address) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    loadPtr(address, scratch);
    jr(scratch);
  }

  void jump(JitCode* code) { branch(code); }

  void jump(ImmPtr ptr) {
    BufferOffset bo = ma_jump(ptr);
    addPendingJump(bo, ptr, RelocationKind::HARDCODED);
  }

  void jump(TrampolinePtr code) { jump(ImmPtr(code.value)); }

  void splitSignExtTag(Register src, Register dest) {
    // As opposed to other architectures, splitTag is replaced by
    // splitSignExtTag which extracts the tag with sign extension. This happens
    // because a tag value is too large to fit in a 12-bit immediate value, and
    // would require to add an extra instruction and require an extra scratch
    // register to load the tag value.
    //
    // Instead, we compare with the sign-extended tag. The sign-extended tag is
    // a negative value near zero and fits in 12 bits.

    srai(dest, src, JSVAL_TAG_SHIFT);
  }

  void splitSignExtTag(const ValueOperand& operand, Register dest) {
    splitSignExtTag(operand.valueReg(), dest);
  }

  void splitTagForTest(const ValueOperand& value, ScratchTagScope& tag) {
    splitSignExtTag(value, tag);
  }

  void moveIfZero(Register dst, Register src, Register cond) {
    if (HasZicondExtension()) {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();

      ma_cselz(dst, src, dst, cond, scratch);
      return;
    }

    Label done;
    ma_b(cond, cond, &done, NonZero, ShortJump);
    mv(dst, src);
    bind(&done);
  }

  void moveIfNotZero(Register dst, Register src, Register cond) {
    if (HasZicondExtension()) {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();

      ma_cselnz(dst, src, dst, cond, scratch);
      return;
    }

    Label done;
    ma_b(cond, cond, &done, Zero, ShortJump);
    mv(dst, src);
    bind(&done);
  }

  // unboxing code
  void unboxNonDouble(const ValueOperand& operand, Register dest,
                      JSValueType type) {
    unboxNonDouble(operand.valueReg(), dest, type);
  }

  template <typename T>
  void unboxNonDouble(T src, Register dest, JSValueType type) {
    MOZ_ASSERT(type != JSVAL_TYPE_DOUBLE);
    if (type == JSVAL_TYPE_INT32 || type == JSVAL_TYPE_BOOLEAN) {
      load32(src, dest);
      return;
    }
    loadPtr(src, dest);
    unboxNonDouble(dest, dest, type);
  }

  void unboxNonDouble(Register src, Register dest, JSValueType type) {
    MOZ_ASSERT(type != JSVAL_TYPE_DOUBLE);
    if (type == JSVAL_TYPE_INT32 || type == JSVAL_TYPE_BOOLEAN) {
      SignExtendWord(dest, src);
      return;
    }
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    MOZ_ASSERT(scratch != src);
    mov(ImmShiftedTag(type), scratch);
    xor_(dest, src, scratch);
  }

  void unboxGCThingForGCBarrier(const Address& src, Register dest) {
    loadPtr(src, dest);
    ExtractBits(dest, dest, 0, JSVAL_TAG_SHIFT);
  }
  void unboxGCThingForGCBarrier(const ValueOperand& src, Register dest) {
    ExtractBits(dest, src.valueReg(), 0, JSVAL_TAG_SHIFT);
  }

  void unboxWasmAnyRefGCThingForGCBarrier(const Address& src, Register dest) {
    static_assert(is_int12(wasm::AnyRef::GCThingMask), "fits into andi");

    loadPtr(src, dest);
    andi(dest, dest, int16_t(wasm::AnyRef::GCThingMask));
  }

  void getWasmAnyRefGCThingChunk(Register src, Register dest) {
    MOZ_ASSERT(src != dest);
    movePtr(ImmWord(wasm::AnyRef::GCThingChunkMask), dest);
    and_(dest, dest, src);
  }

  // Like unboxGCThingForGCBarrier, but loads the GC thing's chunk base.
  void getGCThingValueChunk(const Address& src, Register dest) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    MOZ_ASSERT(scratch != dest);
    loadPtr(src, dest);
    movePtr(ImmWord(JS::detail::ValueGCThingPayloadChunkMask), scratch);
    and_(dest, dest, scratch);
  }
  void getGCThingValueChunk(const ValueOperand& src, Register dest) {
    MOZ_ASSERT(src.valueReg() != dest);
    movePtr(ImmWord(JS::detail::ValueGCThingPayloadChunkMask), dest);
    and_(dest, dest, src.valueReg());
  }

  void unboxInt32(const ValueOperand& operand, Register dest);
  void unboxInt32(Register src, Register dest);
  void unboxInt32(const Address& src, Register dest);
  void unboxInt32(const BaseIndex& src, Register dest);
  void unboxBoolean(const ValueOperand& operand, Register dest);
  void unboxBoolean(Register src, Register dest);
  void unboxBoolean(const Address& src, Register dest);
  void unboxBoolean(const BaseIndex& src, Register dest);
  void unboxDouble(const ValueOperand& operand, FloatRegister dest);
  void unboxDouble(Register src, Register dest);
  void unboxDouble(const Address& src, FloatRegister dest);
  void unboxDouble(const BaseIndex& src, FloatRegister dest);
  void unboxString(const ValueOperand& operand, Register dest);
  void unboxString(Register src, Register dest);
  void unboxString(const Address& src, Register dest);
  void unboxSymbol(const ValueOperand& operand, Register dest);
  void unboxSymbol(Register src, Register dest);
  void unboxSymbol(const Address& src, Register dest);
  void unboxBigInt(const ValueOperand& operand, Register dest);
  void unboxBigInt(Register src, Register dest);
  void unboxBigInt(const Address& src, Register dest);
  void unboxObject(const ValueOperand& operand, Register dest);
  void unboxObject(Register src, Register dest);
  void unboxObject(const Address& src, Register dest);
  void unboxObject(const BaseIndex& src, Register dest) {
    unboxNonDouble(src, dest, JSVAL_TYPE_OBJECT);
  }
  void unboxValue(const ValueOperand& operand, AnyRegister dest,
                  JSValueType type);

  void notBoolean(const ValueOperand& val) {
    NegateBool(val.valueReg(), val.valueReg());
  }

  // boxing code
  void boxDouble(FloatRegister src, const ValueOperand& dest, FloatRegister);
  void boxNonDouble(JSValueType type, Register src, const ValueOperand& dest) {
    boxValue(type, src, dest.valueReg());
  }
  void boxNonDouble(Register type, Register src, const ValueOperand& dest) {
    boxValue(type, src, dest.valueReg());
  }

  // Extended unboxing API. If the payload is already in a register, returns
  // that register. Otherwise, provides a move to the given scratch register,
  // and returns that.
  [[nodiscard]] Register extractObject(const Address& address,
                                       Register scratch);
  [[nodiscard]] Register extractObject(const ValueOperand& value,
                                       Register scratch) {
    unboxObject(value, scratch);
    return scratch;
  }
  [[nodiscard]] Register extractString(const ValueOperand& value,
                                       Register scratch) {
    unboxString(value, scratch);
    return scratch;
  }
  [[nodiscard]] Register extractSymbol(const ValueOperand& value,
                                       Register scratch) {
    unboxSymbol(value, scratch);
    return scratch;
  }
  [[nodiscard]] Register extractInt32(const ValueOperand& value,
                                      Register scratch) {
    unboxInt32(value, scratch);
    return scratch;
  }
  [[nodiscard]] Register extractBoolean(const ValueOperand& value,
                                        Register scratch) {
    unboxBoolean(value, scratch);
    return scratch;
  }
  [[nodiscard]] Register extractTag(const Address& address, Register scratch);
  [[nodiscard]] Register extractTag(const BaseIndex& address, Register scratch);
  [[nodiscard]] Register extractTag(const ValueOperand& value,
                                    Register scratch) {
    splitSignExtTag(value, scratch);
    return scratch;
  }

  void loadInt32OrDouble(const Address& src, FloatRegister dest);
  void loadInt32OrDouble(const BaseIndex& addr, FloatRegister dest);
  void loadConstantDouble(double dp, FloatRegister dest);
  void loadConstantFloat32(float f, FloatRegister dest);

  void testNullSet(Condition cond, const ValueOperand& value, Register dest);

  void testObjectSet(Condition cond, const ValueOperand& value, Register dest);

  void testUndefinedSet(Condition cond, const ValueOperand& value,
                        Register dest);

  // higher level tag testing code
  Address ToPayload(Address value) { return value; }

  template <typename T>
  void loadUnboxedValue(const T& address, MIRType type, AnyRegister dest) {
    if (dest.isFloat()) {
      loadInt32OrDouble(address, dest.fpu());
    } else {
      unboxNonDouble(address, dest.gpr(), ValueTypeFromMIRType(type));
    }
  }

  void boxValue(JSValueType type, Register src, Register dest);
  void boxValue(Register type, Register src, Register dest);

  void storeValue(ValueOperand val, const Address& dest);
  void storeValue(ValueOperand val, const BaseIndex& dest);
  void storeValue(JSValueType type, Register reg, Address dest);
  void storeValue(JSValueType type, Register reg, BaseIndex dest);
  void storeValue(const Value& val, Address dest);
  void storeValue(const Value& val, BaseIndex dest);
  void storeValue(const Address& src, const Address& dest, Register temp) {
    loadPtr(src, temp);
    storePtr(temp, dest);
  }

  void storePrivateValue(Register src, const Address& dest) {
    storePtr(src, dest);
  }
  void storePrivateValue(ImmGCPtr imm, const Address& dest) {
    storePtr(imm, dest);
  }

  void loadValue(Address src, ValueOperand val);
  void loadValue(const BaseIndex& src, ValueOperand val);

  void loadUnalignedValue(const Address& src, ValueOperand dest) {
    loadValue(src, dest);
  }

  void tagValue(JSValueType type, Register payload, ValueOperand dest);

  void pushValue(ValueOperand val);
  void popValue(ValueOperand val);
  void pushValue(const Value& val) {
    if (val.isGCThing()) {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      CodeOffset offset = movWithPatch(ImmWord(val.asRawBits()), scratch);
      writeDataRelocation(val, offset);
      push(scratch);
    } else {
      push(ImmWord(val.asRawBits()));
    }
  }
  void pushValue(JSValueType type, Register reg) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    boxValue(type, reg, scratch);
    push(scratch);
  }
  void pushValue(const Address& addr);
  void pushValue(const BaseIndex& addr, Register scratch) {
    loadValue(addr, ValueOperand(scratch));
    pushValue(ValueOperand(scratch));
  }

  void handleFailureWithHandlerTail(Label* profilerExitTail, Label* bailoutTail,
                                    uint32_t* returnValueCheckOffset);

  /////////////////////////////////////////////////////////////////
  // Common interface.
  /////////////////////////////////////////////////////////////////
 public:
  // The following functions are exposed for use in platform-shared code.

  inline void incrementInt32Value(const Address& addr);

  void move32(Imm32 imm, Register dest);
  void move32(Register src, Register dest);

  void movePtr(Register src, Register dest);
  void movePtr(ImmWord imm, Register dest);
  void movePtr(ImmPtr imm, Register dest);
  void movePtr(wasm::SymbolicAddress imm, Register dest);
  void movePtr(ImmGCPtr imm, Register dest);

  FaultingCodeOffset load8SignExtend(const Address& address, Register dest);
  FaultingCodeOffset load8SignExtend(const BaseIndex& src, Register dest);

  FaultingCodeOffset load8ZeroExtend(const Address& address, Register dest);
  FaultingCodeOffset load8ZeroExtend(const BaseIndex& src, Register dest);

  FaultingCodeOffset load16SignExtend(const Address& address, Register dest);
  FaultingCodeOffset load16SignExtend(const BaseIndex& src, Register dest);

  template <typename S>
  void load16UnalignedSignExtend(const S& src, Register dest) {
    load16SignExtend(src, dest);
  }

  FaultingCodeOffset load16ZeroExtend(const Address& address, Register dest);
  FaultingCodeOffset load16ZeroExtend(const BaseIndex& src, Register dest);

  template <typename S>
  void load16UnalignedZeroExtend(const S& src, Register dest) {
    load16ZeroExtend(src, dest);
  }

  FaultingCodeOffset load32(const Address& address, Register dest);
  FaultingCodeOffset load32(const BaseIndex& address, Register dest);
  FaultingCodeOffset load32(AbsoluteAddress address, Register dest);
  FaultingCodeOffset load32(wasm::SymbolicAddress address, Register dest);

  template <typename S>
  void load32Unaligned(const S& src, Register dest) {
    load32(src, dest);
  }

  FaultingCodeOffset load64(const Address& address, Register64 dest) {
    return loadPtr(address, dest.reg);
  }
  FaultingCodeOffset load64(const BaseIndex& address, Register64 dest) {
    return loadPtr(address, dest.reg);
  }

  FaultingCodeOffset loadDouble(const Address& addr, FloatRegister dest) {
    return ma_loadDouble(dest, addr);
  }
  FaultingCodeOffset loadDouble(const BaseIndex& src, FloatRegister dest) {
    return ma_loadDouble(dest, src);
  }

  FaultingCodeOffset loadFloat32(const Address& addr, FloatRegister dest) {
    return ma_loadFloat(dest, addr);
  }
  FaultingCodeOffset loadFloat32(const BaseIndex& src, FloatRegister dest) {
    return ma_loadFloat(dest, src);
  }

  FaultingCodeOffset loadFloat16(const Address& addr, FloatRegister dest,
                                 Register) {
    return ma_loadFloat16(dest, addr);
  }
  FaultingCodeOffset loadFloat16(const BaseIndex& src, FloatRegister dest,
                                 Register) {
    return ma_loadFloat16(dest, src);
  }

  template <typename S>
  FaultingCodeOffset load64Unaligned(const S& src, Register64 dest) {
    return load64(src, dest);
  }

  FaultingCodeOffset loadPtr(const Address& address, Register dest);
  FaultingCodeOffset loadPtr(const BaseIndex& src, Register dest);
  FaultingCodeOffset loadPtr(AbsoluteAddress address, Register dest);
  FaultingCodeOffset loadPtr(wasm::SymbolicAddress address, Register dest);

  FaultingCodeOffset loadPrivate(const Address& address, Register dest);

  FaultingCodeOffset store8(Register src, const Address& address);
  FaultingCodeOffset store8(Imm32 imm, const Address& address);
  FaultingCodeOffset store8(Register src, const BaseIndex& address);
  FaultingCodeOffset store8(Imm32 imm, const BaseIndex& address);

  FaultingCodeOffset store16(Register src, const Address& address);
  FaultingCodeOffset store16(Imm32 imm, const Address& address);
  FaultingCodeOffset store16(Register src, const BaseIndex& address);
  FaultingCodeOffset store16(Imm32 imm, const BaseIndex& address);

  template <typename T>
  FaultingCodeOffset store16Unaligned(Register src, const T& dest) {
    return store16(src, dest);
  }

  FaultingCodeOffset store32(Register src, AbsoluteAddress address);
  FaultingCodeOffset store32(Register src, const Address& address);
  FaultingCodeOffset store32(Register src, const BaseIndex& address);
  FaultingCodeOffset store32(Imm32 src, const Address& address);
  FaultingCodeOffset store32(Imm32 src, const BaseIndex& address);

  // NOTE: This will use second scratch on LOONG64. Only ARM needs the
  // implementation without second scratch.
  void store32_NoSecondScratch(Imm32 src, const Address& address) {
    store32(src, address);
  }

  template <typename T>
  void store32Unaligned(Register src, const T& dest) {
    store32(src, dest);
  }

  FaultingCodeOffset store64(Imm64 imm, Address address) {
    return storePtr(ImmWord(imm.value), address);
  }
  FaultingCodeOffset store64(Imm64 imm, const BaseIndex& address) {
    return storePtr(ImmWord(imm.value), address);
  }

  FaultingCodeOffset store64(Register64 src, Address address) {
    return storePtr(src.reg, address);
  }
  FaultingCodeOffset store64(Register64 src, const BaseIndex& address) {
    return storePtr(src.reg, address);
  }

  template <typename T>
  FaultingCodeOffset store64Unaligned(Register64 src, const T& dest) {
    return store64(src, dest);
  }

  template <typename T>
  FaultingCodeOffset storePtr(ImmWord imm, T address);
  template <typename T>
  FaultingCodeOffset storePtr(ImmPtr imm, T address);
  template <typename T>
  FaultingCodeOffset storePtr(ImmGCPtr imm, T address);
  FaultingCodeOffset storePtr(Register src, const Address& address);
  FaultingCodeOffset storePtr(Register src, const BaseIndex& address);
  FaultingCodeOffset storePtr(Register src, AbsoluteAddress dest);

  void moveDouble(FloatRegister src, FloatRegister dest) { fmv_d(dest, src); }

  void zeroDouble(FloatRegister reg) { fmv_d_x(reg, zero); }

  void convertUInt64ToDouble(Register src, FloatRegister dest);

  void breakpoint(uint32_t value = 0);
  void crash();

  void checkStackAlignment() {
#ifdef DEBUG
    Label aligned;
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    andi(scratch, sp, ABIStackAlignment - 1);
    ma_b(scratch, zero, &aligned, Equal, ShortJump);
    breakpoint();
    bind(&aligned);
#endif
  };

  static void calculateAlignedStackPointer(void** stackPointer);

  void minMax32(Register lhs, Register rhs, Register dest, bool isMax);
  void minMax32(Register lhs, Imm32 rhs, Register dest, bool isMax);

  void minMaxPtr(Register lhs, Register rhs, Register dest, bool isMax);
  void minMaxPtr(Register lhs, ImmWord rhs, Register dest, bool isMax);

  void cmpPtrSet(Assembler::Condition cond, Address lhs, ImmPtr rhs,
                 Register dest);
  void cmpPtrSet(Assembler::Condition cond, Register lhs, Address rhs,
                 Register dest);
  void cmpPtrSet(Assembler::Condition cond, Address lhs, Register rhs,
                 Register dest);

  void cmp32Set(Assembler::Condition cond, Register lhs, Address rhs,
                Register dest);

 protected:
  bool buildOOLFakeExitFrame(void* fakeReturnAddr);

 public:
  void abiret() { jr(ra); }

  void moveFloat32(FloatRegister src, FloatRegister dest) { fmv_s(dest, src); }

  // Instrumentation for entering and leaving the profiler.
  void profilerEnterFrame(Register framePtr, Register scratch);
  void profilerExitFrame();
};

typedef MacroAssemblerRiscv64Compat MacroAssemblerSpecific;

}  // namespace jit
}  // namespace js

#endif /* jit_riscv64_MacroAssembler_riscv64_h */
