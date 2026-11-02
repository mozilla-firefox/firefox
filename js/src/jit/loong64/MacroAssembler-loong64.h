/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_loong64_MacroAssembler_loong64_h
#define jit_loong64_MacroAssembler_loong64_h

#include <type_traits>

#include "jit/loong64/Assembler-loong64.h"
#include "jit/MoveResolver.h"
#include "wasm/WasmBuiltins.h"

using js::wasm::FaultingCodeRange;
using js::wasm::FaultingCodeRangePair;

namespace js {
namespace jit {

enum LoadStoreSize {
  SizeByte = 8,
  SizeHalfWord = 16,
  SizeWord = 32,
  SizeDouble = 64
};

enum LoadStoreExtension { ZeroExtend = 0, SignExtend = 1 };

enum JumpKind { LongJump = 0, ShortJump = 1 };

static Register CallReg = t8;

enum LiFlags {
  Li64 = 0,
  Li48 = 1,
};

struct ImmShiftedTag : public ImmWord {
  explicit ImmShiftedTag(JSValueType type)
      : ImmWord(uintptr_t(JSValueShiftedTag(JSVAL_TYPE_TO_SHIFTED_TAG(type)))) {
  }
};

struct ImmTagSignExt : public Imm32 {
  static constexpr int32_t signExtend(JSValueTag mask) {
    int64_t tag =
        static_cast<int64_t>(static_cast<uint64_t>(mask) << JSVAL_TAG_SHIFT) >>
        JSVAL_TAG_SHIFT;
    MOZ_ASSERT(is_intN(tag, 12));
    return static_cast<int32_t>(tag);
  }

  explicit ImmTagSignExt(JSValueTag mask) : Imm32(signExtend(mask)) {}
};

static const int defaultShift = 3;
static_assert(1 << defaultShift == sizeof(JS::Value),
              "The defaultShift is wrong");

// See documentation for ScratchTagScope and ScratchTagScopeRelease in
// MacroAssembler-x64.h.

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

class MacroAssemblerLOONG64 : public Assembler {
 protected:
  // Perform a downcast. Should be removed by Bug 996602.
  MacroAssembler& asMasm();
  const MacroAssembler& asMasm() const;

  Condition ma_cmp(Register rd, Register lhs, Register rhs, Condition c);
  Condition ma_cmp(Register rd, Register lhs, Imm32 imm, Condition c);

  void compareFloatingPoint(FloatFormat fmt, FloatRegister lhs,
                            FloatRegister rhs, DoubleCondition c,
                            FPConditionBit fcc = FCC0);

 public:
  void ma_move(Register dest, Register src);

  void ma_li(Register dest, CodeLabel* label);
  void ma_li(Register dest, ImmWord imm);
  void ma_liPatchable(Register dest, ImmPtr imm);
  void ma_liPatchable(Register dest, ImmWord imm, LiFlags flags = Li48);

  // load
  FaultingCodeRange ma_ld_b(Register dest, Address address);
  FaultingCodeRange ma_ld_h(Register dest, Address address);
  FaultingCodeRange ma_ld_w(Register dest, Address address);
  FaultingCodeRange ma_ld_d(Register dest, Address address);
  FaultingCodeRange ma_ld_bu(Register dest, Address address);
  FaultingCodeRange ma_ld_hu(Register dest, Address address);
  FaultingCodeRange ma_ld_wu(Register dest, Address address);
  FaultingCodeRange ma_load(Register dest, Address address,
                            LoadStoreSize size = SizeWord,
                            LoadStoreExtension extension = SignExtend);

  // store
  FaultingCodeRange ma_st_b(Register src, Address address);
  FaultingCodeRange ma_st_h(Register src, Address address);
  FaultingCodeRange ma_st_w(Register src, Address address);
  FaultingCodeRange ma_st_d(Register src, Address address);
  FaultingCodeRange ma_store(Register data, Address address,
                             LoadStoreSize size = SizeWord,
                             LoadStoreExtension extension = SignExtend);
  FaultingCodeRange ma_store(Imm32 imm, Address address,
                             LoadStoreSize size = SizeWord,
                             LoadStoreExtension extension = SignExtend);

  // arithmetic based ops
  // add
  void ma_add_d(Register rd, Register rj, Imm32 imm);
  void ma_add_d(Register rd, Register rj, ImmWord imm);
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
  void ma_sub_d(Register rd, Register rj, Imm32 imm);
  void ma_sub_d(Register rd, Register rj, ImmWord imm);
  void ma_sub32TestOverflow(Register rd, Register rj, Register rk,
                            Label* overflow);
  void ma_subPtrTestOverflow(Register rd, Register rj, Register rk,
                             Label* overflow);
  void ma_subPtrTestOverflow(Register rd, Register rj, Imm32 imm,
                             Label* overflow);

  // multiplies.  For now, there are only few that we care about.
  void ma_mul_d(Register rd, Register rj, Imm32 imm);
  void ma_mul_d(Register rd, Register rj, ImmWord imm);
  void ma_mulh_d(Register rd, Register rj, Imm32 imm);
  void ma_mulPtrTestOverflow(Register rd, Register rj, Register rk,
                             Label* overflow);

  // stack
  void ma_pop(Register r);
  void ma_push(Register r);

  void branchWithCode(InstImm code, Label* label, JumpKind jumpKind,
                      Register scratch = Register::Invalid());
  // branches when done from within la-specific code
  void ma_b(Register lhs, ImmWord imm, Label* l, Condition c,
            JumpKind jumpKind = LongJump);
  void ma_b(Register lhs, Address addr, Label* l, Condition c,
            JumpKind jumpKind = LongJump);
  void ma_b(Address addr, Imm32 imm, Label* l, Condition c,
            JumpKind jumpKind = LongJump);
  void ma_b(Address addr, ImmGCPtr imm, Label* l, Condition c,
            JumpKind jumpKind = LongJump);
  void ma_b(Address addr, Register rhs, Label* l, Condition c,
            JumpKind jumpKind = LongJump) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    MOZ_ASSERT(rhs != scratch);
    ma_ld_d(scratch, addr);
    ma_b(scratch, rhs, l, c, jumpKind);
  }

  void ma_bl(Label* l);

  void ma_jump36(int32_t offset, Register scratch);
  void ma_call36(int32_t offset, Register scratch);

  // fp instructions
  void ma_lid(FloatRegister dest, double value);

  void ma_mv(FloatRegister src, ValueOperand dest);
  void ma_mv(ValueOperand src, FloatRegister dest);

  FaultingCodeRange ma_fld_s(FloatRegister ft, Address address);
  FaultingCodeRange ma_fld_d(FloatRegister ft, Address address);
  FaultingCodeRange ma_fst_d(FloatRegister ft, Address address);
  FaultingCodeRange ma_fst_s(FloatRegister ft, Address address);
  FaultingCodeRange ma_vld(FloatRegister vd, Address address);
  FaultingCodeRange ma_vst(FloatRegister vd, Address address);

  void ma_pop(FloatRegister f);
  void ma_push(FloatRegister f);

  void ma_cmp_set(Register dst, Register lhs, ImmWord imm, Condition c);
  void ma_cmp_set(Register dst, Register lhs, ImmPtr imm, Condition c);
  void ma_cmp_set(Register dst, Register lhs, ImmGCPtr imm, Condition c);
  void ma_cmp_set(Register dst, Address address, Register rhs, Condition c);
  void ma_cmp_set(Register dst, Address address, Imm32 imm, Condition c);
  void ma_cmp_set(Register dst, Address address, ImmWord imm, Condition c);

  void ma_cselz(Register rd, Register rs1, Register rs2, Register rc,
                Register rtmp);
  void ma_cselnz(Register rd, Register rs1, Register rs2, Register rc,
                 Register rtmp);

  // These functions abstract the access to high part of the double precision
  // float register. They are intended to work on both 32 bit and 64 bit
  // floating point coprocessor.
  void moveToDoubleHi(Register src, FloatRegister dest) {
    as_movgr2frh_w(dest, src);
  }
  void moveFromDoubleHi(FloatRegister src, Register dest) {
    as_movfrh2gr_s(dest, src);
  }

  void moveToDouble(Register src, FloatRegister dest) {
    as_movgr2fr_d(dest, src);
  }
  void moveFromDouble(FloatRegister src, Register dest) {
    as_movfr2gr_d(dest, src);
  }

 public:
  void ma_li(Register dest, ImmGCPtr ptr);

  void ma_li(Register dest, Imm32 imm);
  void ma_liPatchable(Register dest, Imm32 imm);

  void ma_rotr_w(Register rd, Register rj, Imm32 shift);

  void ma_fmovz(FloatFormat fmt, FloatRegister fd, FloatRegister fj,
                Register rk);
  void ma_fmovn(FloatFormat fmt, FloatRegister fd, FloatRegister fj,
                Register rk);

  void ma_and(Register rd, Register rj, Imm32 imm);
  void ma_and(Register rd, Register rj, ImmWord imm);

  void ma_or(Register rd, Register rj, Imm32 imm);
  void ma_or(Register rd, Register rj, ImmWord imm);

  void ma_xor(Register rd, Register rj, Imm32 imm);
  void ma_xor(Register rd, Register rj, ImmWord imm);

  void ma_nor(Register rd, Register rj, Imm32 imm);

  // load
  FaultingCodeRange ma_load(Register dest, const BaseIndex& src,
                            LoadStoreSize size = SizeWord,
                            LoadStoreExtension extension = SignExtend);

  // store
  FaultingCodeRange ma_store(Register data, const BaseIndex& dest,
                             LoadStoreSize size = SizeWord,
                             LoadStoreExtension extension = SignExtend);
  FaultingCodeRange ma_store(Imm32 imm, const BaseIndex& dest,
                             LoadStoreSize size = SizeWord,
                             LoadStoreExtension extension = SignExtend);

  // arithmetic based ops
  // add
  void ma_add_w(Register rd, Register rj, Imm32 imm);
  void ma_add32TestCarry(Condition cond, Register rd, Register rj, Register rk,
                         Label* overflow);
  void ma_add32TestCarry(Condition cond, Register rd, Register rj, Imm32 imm,
                         Label* overflow);

  // subtract
  void ma_sub_w(Register rd, Register rj, Imm32 imm);
  void ma_sub_w(Register rd, Register rj, Register rk);
  void ma_sub32TestOverflow(Register rd, Register rj, Imm32 imm,
                            Label* overflow);

  // multiplies.  For now, there are only few that we care about.
  void ma_mul(Register rd, Register rj, Imm32 imm);
  void ma_mul32TestOverflow(Register rd, Register rj, Register rk,
                            Label* overflow);
  void ma_mul32TestOverflow(Register rd, Register rj, Imm32 imm,
                            Label* overflow);

  // branches when done from within la-specific code
  void ma_b(Register lhs, Register rhs, Label* l, Condition c,
            JumpKind jumpKind = LongJump,
            Register scratch = Register::Invalid());
  void ma_b(Register lhs, Imm32 imm, Label* l, Condition c,
            JumpKind jumpKind = LongJump);
  void ma_b(Register lhs, ImmPtr imm, Label* l, Condition c,
            JumpKind jumpKind = LongJump);
  void ma_b(Register lhs, ImmGCPtr imm, Label* l, Condition c,
            JumpKind jumpKind = LongJump) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    MOZ_ASSERT(lhs != scratch);
    ma_li(scratch, imm);
    ma_b(lhs, scratch, l, c, jumpKind);
  }

  void ma_b(Label* l, JumpKind jumpKind = LongJump);

  // fp instructions
  void ma_lis(FloatRegister dest, float value);

  FaultingCodeRange ma_fst_d(FloatRegister src, BaseIndex address);
  FaultingCodeRange ma_fst_s(FloatRegister src, BaseIndex address);

  FaultingCodeRange ma_fld_d(FloatRegister dest, const BaseIndex& src);
  FaultingCodeRange ma_fld_s(FloatRegister dest, const BaseIndex& src);

  // FP branches
  void ma_bc_s(FloatRegister lhs, FloatRegister rhs, Label* label,
               DoubleCondition c, JumpKind jumpKind = LongJump,
               FPConditionBit fcc = FCC0);
  void ma_bc_d(FloatRegister lhs, FloatRegister rhs, Label* label,
               DoubleCondition c, JumpKind jumpKind = LongJump,
               FPConditionBit fcc = FCC0);

  void ma_call(ImmPtr dest);

  void ma_jump(ImmPtr dest);

  void ma_cmp_set(Register dst, Register lhs, Register rhs, Condition c);
  void ma_cmp_set(Register dst, Register lhs, Imm32 imm, Condition c);
  void ma_cmp_set_double(Register dst, FloatRegister lhs, FloatRegister rhs,
                         DoubleCondition c);
  void ma_cmp_set_double(FPConditionBit fcc, FloatRegister lhs,
                         FloatRegister rhs, DoubleCondition c);
  void ma_cmp_set_float32(Register dst, FloatRegister lhs, FloatRegister rhs,
                          DoubleCondition c);
  void ma_cmp_set_float32(FPConditionBit fcc, FloatRegister lhs,
                          FloatRegister rhs, DoubleCondition c);

  void moveToFloat32(Register src, FloatRegister dest) {
    as_movgr2fr_w(dest, src);
  }
  void moveFromFloat32(FloatRegister src, Register dest) {
    as_movfr2gr_s(dest, src);
  }

  void minMaxPtr(Register lhs, Register rhs, Register dest, bool isMax);
  void minMaxPtr(Register lhs, ImmWord rhs, Register dest, bool isMax);

  // Evaluate srcDest = minmax<isMax>{Float32,Double}(srcDest, other).
  // Handle NaN specially if handleNaN is true. Handle zeroes specially if
  // handleZero is true.
  void minMaxDouble(FloatRegister srcDest, FloatRegister other, bool handleNaN,
                    bool handleZero, bool isMax);
  void minMaxFloat32(FloatRegister srcDest, FloatRegister other, bool handleNaN,
                     bool handleZero, bool isMax);

  FaultingCodeRange loadDouble(const Address& addr, FloatRegister dest);
  FaultingCodeRange loadDouble(const BaseIndex& src, FloatRegister dest);

  FaultingCodeRange loadFloat32(const Address& addr, FloatRegister dest);
  FaultingCodeRange loadFloat32(const BaseIndex& src, FloatRegister dest);

  FaultingCodeRange loadFloat16(const Address& addr, FloatRegister dest,
                                Register) {
    MOZ_CRASH("Not supported for this target");
  }
  FaultingCodeRange loadFloat16(const BaseIndex& src, FloatRegister dest,
                                Register) {
    MOZ_CRASH("Not supported for this target");
  }

  void outOfLineWasmTruncateToInt32Check(
      FloatRegister input, Register output, MIRType fromType, TruncFlags flags,
      Label* rejoin, const wasm::TrapSiteDesc& trapSiteDesc,
      wasm::StackMap* stackMapForTraps,
      wasm::StackMapRegistry* stackMapRegistry);
  void outOfLineWasmTruncateToInt64Check(
      FloatRegister input, Register64 output, MIRType fromType,
      TruncFlags flags, Label* rejoin, const wasm::TrapSiteDesc& trapSiteDesc,
      wasm::StackMap* stackMapForTraps,
      wasm::StackMapRegistry* stackMapRegistry);

  // The complete address is in `address`, and `access` is used for its type
  // attributes only; its `offset` is ignored.
  void wasmLoadAbsolute(const wasm::MemoryAccessDesc& access,
                        Register memoryBase, uint64_t address,
                        AnyRegister output) {
    wasmLoadAbsoluteImpl(access, memoryBase, address, output);
  }
  void wasmLoadAbsoluteI64(const wasm::MemoryAccessDesc& access,
                           Register memoryBase, uint64_t address,
                           Register64 output) {
    wasmLoadAbsoluteImpl(access, memoryBase, address, AnyRegister(output.reg));
  }
  void wasmStoreAbsolute(const wasm::MemoryAccessDesc& access,
                         AnyRegister value, Register memoryBase,
                         uint64_t address) {
    wasmStoreAbsoluteImpl(access, value, memoryBase, address);
  }
  void wasmStoreAbsoluteI64(const wasm::MemoryAccessDesc& access,
                            Register64 value, Register memoryBase,
                            uint64_t address) {
    wasmStoreAbsoluteImpl(access, AnyRegister(value.reg), memoryBase, address);
  }

  FaultingCodeRange wasmStoreLane(const wasm::MemoryAccessDesc& access,
                                  FloatRegister src, Register memoryBase,
                                  Register ptr, uint32_t laneSize,
                                  uint32_t laneIndex);

 protected:
  void wasmLoadAbsoluteImpl(const wasm::MemoryAccessDesc& access,
                            Register memoryBase, uint64_t address,
                            AnyRegister output);
  void wasmStoreAbsoluteImpl(const wasm::MemoryAccessDesc& access,
                             AnyRegister value, Register memoryBase,
                             uint64_t address);

  FaultingCodeRange wasmLoadImpl(const wasm::MemoryAccessDesc& access,
                                 Register memoryBase, Register ptr,
                                 AnyRegister output);
  FaultingCodeRange wasmLoadImpl(const wasm::MemoryAccessDesc& access,
                                 Address address, AnyRegister output);
  FaultingCodeRange wasmLoadImpl(const wasm::MemoryAccessDesc& access,
                                 Register memoryBase, Register ptr,
                                 Register ptrScratch, AnyRegister output,
                                 Register tmp);
  FaultingCodeRange wasmStoreImpl(const wasm::MemoryAccessDesc& access,
                                  AnyRegister value, Register memoryBase,
                                  Register ptr);
  FaultingCodeRange wasmStoreImpl(const wasm::MemoryAccessDesc& access,
                                  AnyRegister value, Address address);
  FaultingCodeRange wasmStoreImpl(const wasm::MemoryAccessDesc& access,
                                  AnyRegister value, Register memoryBase,
                                  Register ptr, Register ptrScratch,
                                  Register tmp);

  template <typename T>
  void RoundHelper(RoundingMode mode, FloatRegister src, FloatRegister dest);
};

class MacroAssembler;

class MacroAssemblerLOONG64Compat : public MacroAssemblerLOONG64 {
 public:
  using MacroAssemblerLOONG64::call;

  MacroAssemblerLOONG64Compat() {}

  void convertBoolToInt32(Register src, Register dest) {
    ma_and(dest, src, Imm32(0xff));
  };
  void convertInt32ToDouble(Register src, FloatRegister dest) {
    as_movgr2fr_w(dest, src);
    as_ffint_d_w(dest, dest);
  };
  void convertInt32ToDouble(const Address& src, FloatRegister dest) {
    ma_fld_s(dest, src);
    as_ffint_d_w(dest, dest);
  };
  void convertInt32ToDouble(const BaseIndex& src, FloatRegister dest) {
    UseScratchRegisterScope temps(*this);
    Address address = computeScaledAddress(src, temps);
    convertInt32ToDouble(address, dest);
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
    MOZ_CRASH("Not supported for this target");
  }
  void convertFloat16ToDouble(FloatRegister src, FloatRegister dest) {
    MOZ_CRASH("Not supported for this target");
  }
  void convertFloat32ToFloat16(FloatRegister src, FloatRegister dest) {
    MOZ_CRASH("Not supported for this target");
  }
  void convertFloat16ToFloat32(FloatRegister src, FloatRegister dest) {
    MOZ_CRASH("Not supported for this target");
  }
  void convertInt32ToFloat16(Register src, FloatRegister dest) {
    MOZ_CRASH("Not supported for this target");
  }

  void computeScaledAddress(const BaseIndex& address, Register dest);
  void computeScaledAddress32(const BaseIndex& address, Register dest);

  void computeEffectiveAddress(const Address& address, Register dest) {
    ma_add_d(dest, address.base, Imm32(address.offset));
  }

  void computeEffectiveAddress(const BaseIndex& address, Register dest) {
    computeScaledAddress(address, dest);
    if (address.offset) {
      ma_add_d(dest, dest, Imm32(address.offset));
    }
  }

  void computeEffectiveAddress32(const Address& address, Register dest) {
    ma_add_w(dest, address.base, Imm32(address.offset));
  }

  void computeEffectiveAddress32(const BaseIndex& address, Register dest) {
    computeScaledAddress32(address, dest);
    if (address.offset) {
      ma_add_w(dest, dest, Imm32(address.offset));
    }
  }

  Address computeScaledAddress(const BaseIndex& address,
                               UseScratchRegisterScope& temps);

  void j(Label* dest) { ma_b(dest); }

  void mov(Register src, Register dest) { ma_move(dest, src); }
  void mov(ImmWord imm, Register dest) { ma_li(dest, imm); }
  void mov(ImmPtr imm, Register dest) {
    mov(ImmWord(uintptr_t(imm.value)), dest);
  }
  void mov(CodeLabel* label, Register dest) { ma_li(dest, label); }
  void mov(Register src, Address dest) { MOZ_CRASH("NYI-IC"); }
  void mov(Address src, Register dest) { MOZ_CRASH("NYI-IC"); }

  void writeDataRelocation(const Value& val) {
    MOZ_ASSERT(val.isGCThing(), "only called for gc-things");

    // Raw GC pointer relocations and Value relocations both end up in
    // TraceOneDataRelocation.
    gc::Cell* cell = val.toGCThing();
    if (cell && gc::IsInsideNursery(cell)) {
      embedsNurseryPointers_ = true;
    }
    dataRelocations_.writeUnsigned(currentOffset());
  }

  void branch(JitCode* c) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    BufferOffset bo = m_buffer.nextOffset();
    addPendingJump(bo, ImmPtr(c->raw()), RelocationKind::JITCODE);
    as_pcaddu18i(scratch, 0);
    as_jirl(zero, scratch, BOffImm16(0));
  }
  void branch(const Register reg) { as_jirl(zero, reg, BOffImm16(0)); }
  void nop() { as_nop(); }
  BufferOffset ret() {
    ma_pop(ra);
    return as_jirl(zero, ra, BOffImm16(0));
  }
  inline void retn(Imm32 n);
  void push(Imm32 imm) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    ma_push(scratch);
  }
  void push(ImmWord imm) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    ma_push(scratch);
  }
  void push(ImmGCPtr imm) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    ma_push(scratch);
  }
  void push(const Address& address) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    loadPtr(address, scratch);
    ma_push(scratch);
  }
  void push(Register reg) { ma_push(reg); }
  void push(FloatRegister reg) { ma_push(reg); }
  void pop(Register reg) { ma_pop(reg); }
  void pop(FloatRegister reg) { ma_pop(reg); }

  template <typename... Regs>
  void pushRegs(const Regs&... regs) {
    static_assert((std::is_convertible_v<Regs, Register> && ...));
    static_assert(sizeof...(Regs) > 0);

    if (((static_cast<Register>(regs) == StackPointer) || ...)) {
      (ma_push(regs), ...);
      return;
    }

    int32_t offset = int32_t(sizeof...(Regs) * sizeof(intptr_t));
    ma_sub_d(StackPointer, StackPointer, Imm32(offset));
    (storePtr(regs, Address(StackPointer, offset -= int32_t(sizeof(intptr_t)))),
     ...);
  }

  template <typename... Regs>
  void popRegs(const Regs&... regs) {
    static_assert((std::is_convertible_v<Regs, Register> && ...));
    static_assert(sizeof...(Regs) > 0);

    if (((static_cast<Register>(regs) == StackPointer) || ...)) {
      (ma_pop(regs), ...);
      return;
    }

    int32_t offset = -int32_t(sizeof(intptr_t));
    (loadPtr(Address(StackPointer, offset += int32_t(sizeof(intptr_t))), regs),
     ...);
    ma_add_d(StackPointer, StackPointer,
             Imm32(int32_t(sizeof...(Regs) * sizeof(intptr_t))));
  }

  // Emit a branch that can be toggled to a non-operation. On LOONG64 we use
  // "andi" instruction to toggle the branch.
  // See ToggleToJmp(), ToggleToCmp().
  CodeOffset toggledJump(Label* label);

  // Emit a "jalr" or "bne $zero, $zero, ..." instruction. ToggleCall can be
  // used to patch this instruction.
  CodeOffset toggledCall(JitCode* target, bool enabled);

  static size_t ToggledCallSize(uint8_t* code) {
    // Two instructions used in: MacroAssemblerLOONG64Compat::toggledCall
    return 2 * sizeof(uint32_t);
  }

  CodeOffset pushWithPatch(ImmWord imm) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    CodeOffset offset = movWithPatch(imm, scratch);
    ma_push(scratch);
    return offset;
  }

  CodeOffset movWithPatch(ImmWord imm, Register dest) {
    CodeOffset offset = CodeOffset(currentOffset());
    ma_liPatchable(dest, imm, Li64);
    return offset;
  }
  CodeOffset movWithPatch(ImmPtr imm, Register dest) {
    CodeOffset offset = CodeOffset(currentOffset());
    ma_liPatchable(dest, imm);
    return offset;
  }

  void writeCodePointer(CodeLabel* label) {
    label->patchAt()->bind(currentOffset());
    label->setLinkMode(CodeLabel::RawPointer);
    m_buffer.ensureSpace(sizeof(void*));
    writeInst(-1);
    writeInst(-1);
  }

  void jump(Label* label) { ma_b(label); }
  void jump(Register reg) { as_jirl(zero, reg, BOffImm16(0)); }
  void jump(const Address& address) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    loadPtr(address, scratch);
    as_jirl(zero, scratch, BOffImm16(0));
  }

  void jump(JitCode* code) { branch(code); }

  void jump(ImmPtr ptr) {
    BufferOffset bo = m_buffer.nextOffset();
    addPendingJump(bo, ptr, RelocationKind::HARDCODED);
    ma_jump(ptr);
  }

  void jump(TrampolinePtr code) { jump(ImmPtr(code.value)); }

  // See splitSignExtTag(Register, Register) in MacroAssembler-riscv64.h for
  // rationale.
  void splitSignExtTag(Register src, Register dest) {
    as_srai_d(dest, src, JSVAL_TAG_SHIFT);
  }

  void splitSignExtTag(const ValueOperand& operand, Register dest) {
    splitSignExtTag(operand.valueReg(), dest);
  }

  void splitTagForTest(const ValueOperand& value, ScratchTagScope& tag) {
    splitSignExtTag(value, tag);
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
      as_slli_w(dest, src, 0);
      return;
    }
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    MOZ_ASSERT(scratch != src);
    mov(ImmShiftedTag(type), scratch);
    as_xor(dest, src, scratch);
  }

  void unboxGCThingForGCBarrier(const Address& src, Register dest) {
    loadPtr(src, dest);
    as_bstrpick_d(dest, dest, JSVAL_TAG_SHIFT - 1, 0);
  }
  void unboxGCThingForGCBarrier(const ValueOperand& src, Register dest) {
    as_bstrpick_d(dest, src.valueReg(), JSVAL_TAG_SHIFT - 1, 0);
  }

  void unboxWasmAnyRefGCThingForGCBarrier(const Address& src, Register dest) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    MOZ_ASSERT(scratch != dest);
    movePtr(ImmWord(wasm::AnyRef::GCThingMask), scratch);
    loadPtr(src, dest);
    as_and(dest, dest, scratch);
  }

  // Like unboxGCThingForGCBarrier, but loads the GC thing's chunk base.
  void getGCThingValueChunk(const Address& src, Register dest) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    MOZ_ASSERT(scratch != dest);
    loadPtr(src, dest);
    movePtr(ImmWord(JS::detail::ValueGCThingPayloadChunkMask), scratch);
    as_and(dest, dest, scratch);
  }
  void getGCThingValueChunk(const ValueOperand& src, Register dest) {
    MOZ_ASSERT(src.valueReg() != dest);
    movePtr(ImmWord(JS::detail::ValueGCThingPayloadChunkMask), dest);
    as_and(dest, dest, src.valueReg());
  }

  void getWasmAnyRefGCThingChunk(Register src, Register dest) {
    MOZ_ASSERT(src != dest);
    movePtr(ImmWord(wasm::AnyRef::GCThingChunkMask), dest);
    as_and(dest, dest, src);
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
  void unboxSymbol(const ValueOperand& src, Register dest);
  void unboxSymbol(Register src, Register dest);
  void unboxSymbol(const Address& src, Register dest);
  void unboxBigInt(const ValueOperand& operand, Register dest);
  void unboxBigInt(Register src, Register dest);
  void unboxBigInt(const Address& src, Register dest);
  void unboxObject(const ValueOperand& src, Register dest);
  void unboxObject(Register src, Register dest);
  void unboxObject(const Address& src, Register dest);
  void unboxObject(const BaseIndex& src, Register dest) {
    unboxNonDouble(src, dest, JSVAL_TYPE_OBJECT);
  }
  void unboxValue(const ValueOperand& src, AnyRegister dest, JSValueType type);

  void notBoolean(const ValueOperand& val) {
    as_xori(val.valueReg(), val.valueReg(), 1);
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
      UseScratchRegisterScope temps(*this);
      Register scratch = temps.Acquire();
      writeDataRelocation(val);
      movWithPatch(ImmWord(val.asRawBits()), scratch);
      push(scratch);
    } else {
      push(ImmWord(val.asRawBits()));
    }
  }
  void pushValue(JSValueType type, Register reg) {
    UseScratchRegisterScope temps(*this);
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

  FaultingCodeRange load8SignExtend(const Address& address, Register dest);
  FaultingCodeRange load8SignExtend(const BaseIndex& src, Register dest);

  FaultingCodeRange load8ZeroExtend(const Address& address, Register dest);
  FaultingCodeRange load8ZeroExtend(const BaseIndex& src, Register dest);

  FaultingCodeRange load16SignExtend(const Address& address, Register dest);
  FaultingCodeRange load16SignExtend(const BaseIndex& src, Register dest);

  template <typename S>
  void load16UnalignedSignExtend(const S& src, Register dest) {
    load16SignExtend(src, dest);
  }

  FaultingCodeRange load16ZeroExtend(const Address& address, Register dest);
  FaultingCodeRange load16ZeroExtend(const BaseIndex& src, Register dest);

  template <typename S>
  void load16UnalignedZeroExtend(const S& src, Register dest) {
    load16ZeroExtend(src, dest);
  }

  FaultingCodeRange load32(const Address& address, Register dest);
  FaultingCodeRange load32(const BaseIndex& address, Register dest);
  void load32(AbsoluteAddress address, Register dest);
  void load32(wasm::SymbolicAddress address, Register dest);

  template <typename S>
  void load32Unaligned(const S& src, Register dest) {
    load32(src, dest);
  }

  FaultingCodeRange load64(const Address& address, Register64 dest) {
    return loadPtr(address, dest.reg);
  }
  FaultingCodeRange load64(const BaseIndex& address, Register64 dest) {
    return loadPtr(address, dest.reg);
  }

  template <typename S>
  void load64Unaligned(const S& src, Register64 dest) {
    load64(src, dest);
  }

  FaultingCodeRange loadPtr(const Address& address, Register dest);
  FaultingCodeRange loadPtr(const BaseIndex& src, Register dest);
  void loadPtr(AbsoluteAddress address, Register dest);
  void loadPtr(wasm::SymbolicAddress address, Register dest);

  void loadPrivate(const Address& address, Register dest);

  FaultingCodeRange store8(Register src, const Address& address);
  FaultingCodeRange store8(Register src, const BaseIndex& address);
  FaultingCodeRange store8(Imm32 imm, const Address& address);
  void store8(Imm32 imm, const BaseIndex& address);

  FaultingCodeRange store16(Register src, const Address& address);
  FaultingCodeRange store16(Register src, const BaseIndex& address);
  FaultingCodeRange store16(Imm32 imm, const Address& address);
  void store16(Imm32 imm, const BaseIndex& address);

  template <typename T>
  void store16Unaligned(Register src, const T& dest) {
    store16(src, dest);
  }

  FaultingCodeRange store32(Register src, const Address& address);
  FaultingCodeRange store32(Register src, const BaseIndex& address);
  void store32(Register src, AbsoluteAddress address);
  FaultingCodeRange store32(Imm32 src, const Address& address);
  void store32(Imm32 src, const BaseIndex& address);

  template <typename T>
  void store32Unaligned(Register src, const T& dest) {
    store32(src, dest);
  }

  FaultingCodeRange store64(Imm64 imm, Address address) {
    return storePtr(ImmWord(imm.value), address);
  }
  void store64(Imm64 imm, const BaseIndex& address) {
    storePtr(ImmWord(imm.value), address);
  }

  FaultingCodeRange store64(Register64 src, Address address) {
    return storePtr(src.reg, address);
  }

  FaultingCodeRange store64(Register64 src, const BaseIndex& address) {
    return storePtr(src.reg, address);
  }

  template <typename T>
  void store64Unaligned(Register64 src, const T& dest) {
    store64(src, dest);
  }

  template <typename T>
  FaultingCodeRange storePtr(ImmWord imm, T address);
  template <typename T>
  void storePtr(ImmPtr imm, T address);
  template <typename T>
  void storePtr(ImmGCPtr imm, T address);
  void storePtr(Register src, AbsoluteAddress dest);
  FaultingCodeRange storePtr(Register src, const Address& address);
  FaultingCodeRange storePtr(Register src, const BaseIndex& address);

  void moveDouble(FloatRegister src, FloatRegister dest) {
    as_fmov_d(dest, src);
  }

  void zeroDouble(FloatRegister reg) { moveToDouble(zero, reg); }

  void convertUInt64ToDouble(Register src, FloatRegister dest);

  void breakpoint(uint32_t value = 0);

  void checkStackAlignment() {
#ifdef DEBUG
    Label aligned;
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    as_andi(scratch, sp, ABIStackAlignment - 1);
    ma_b(scratch, scratch, &aligned, Zero, ShortJump);
    breakpoint();
    bind(&aligned);
#endif
  };

  static void calculateAlignedStackPointer(void** stackPointer);

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

  FaultingCodeRange wasmLoadI64Impl(const wasm::MemoryAccessDesc& access,
                                    Register memoryBase, Register ptr,
                                    Register ptrScratch, Register64 output,
                                    Register tmp);
  FaultingCodeRange wasmStoreI64Impl(const wasm::MemoryAccessDesc& access,
                                     Register64 value, Register memoryBase,
                                     Register ptr, Register ptrScratch,
                                     Register tmp);

 public:
  void lea(Operand addr, Register dest) {
    ma_add_d(dest, addr.baseReg(), Imm32(addr.disp()));
  }

  void abiret() { as_jirl(zero, ra, BOffImm16(0)); }

  void moveFloat32(FloatRegister src, FloatRegister dest) {
    as_fmov_s(dest, src);
  }

  // Instrumentation for entering and leaving the profiler.
  void profilerEnterFrame(Register framePtr, Register scratch);
  void profilerExitFrame();
};

typedef MacroAssemblerLOONG64Compat MacroAssemblerSpecific;

}  // namespace jit
}  // namespace js

#endif /* jit_loong64_MacroAssembler_loong64_h */
