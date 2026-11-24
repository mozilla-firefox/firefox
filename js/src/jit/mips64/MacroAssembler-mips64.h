/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_mips64_MacroAssembler_mips64_h
#define jit_mips64_MacroAssembler_mips64_h

#include <type_traits>

#include "jit/mips-shared/MacroAssembler-mips-shared.h"
#include "jit/MoveResolver.h"
#include "vm/BytecodeUtil.h"
#include "wasm/WasmBuiltins.h"

using js::wasm::FaultingCodeRange;
using js::wasm::FaultingCodeRangePair;

namespace js {
namespace jit {

enum LiFlags {
  Li64 = 0,
  Li48 = 1,
};

struct ImmShiftedTag : public ImmWord {
  explicit ImmShiftedTag(JSValueType type)
      : ImmWord(uintptr_t(JSValueShiftedTag(JSVAL_TYPE_TO_SHIFTED_TAG(type)))) {
  }
};

struct ImmTag : public Imm32 {
  explicit ImmTag(JSValueTag mask) : Imm32(int32_t(mask)) {}
};

static constexpr ValueOperand JSReturnOperand{JSReturnReg};

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

class MacroAssemblerMIPS64 : public MacroAssemblerMIPSShared {
 public:
  using MacroAssemblerMIPSShared::ma_b;
  using MacroAssemblerMIPSShared::ma_cmp_set;
  using MacroAssemblerMIPSShared::ma_ld;
  using MacroAssemblerMIPSShared::ma_li;
  using MacroAssemblerMIPSShared::ma_liPatchable;
  using MacroAssemblerMIPSShared::ma_load;
  using MacroAssemblerMIPSShared::ma_ls;
  using MacroAssemblerMIPSShared::ma_sd;
  using MacroAssemblerMIPSShared::ma_ss;
  using MacroAssemblerMIPSShared::ma_store;
  using MacroAssemblerMIPSShared::ma_sub32TestOverflow;

  void ma_li(Register dest, CodeLabel* label);
  void ma_li(Register dest, ImmWord imm);
  void ma_liPatchable(Register dest, ImmPtr imm);
  void ma_liPatchable(Register dest, ImmWord imm, LiFlags flags = Li48);

  // Negate
  void ma_dnegu(Register rd, Register rs);

  // Shift operations
  void ma_dsll(Register rd, Register rt, Imm32 shift);
  void ma_dsrl(Register rd, Register rt, Imm32 shift);
  void ma_dsra(Register rd, Register rt, Imm32 shift);
  void ma_dror(Register rd, Register rt, Imm32 shift);
  void ma_drol(Register rd, Register rt, Imm32 shift);

  void ma_dsll(Register rd, Register rt, Register shift);
  void ma_dsrl(Register rd, Register rt, Register shift);
  void ma_dsra(Register rd, Register rt, Register shift);
  void ma_dror(Register rd, Register rt, Register shift);
  void ma_drol(Register rd, Register rt, Register shift);

  void ma_dins(Register rt, Register rs, Imm32 pos, Imm32 size);
  void ma_dext(Register rt, Register rs, Imm32 pos, Imm32 size);

  // doubleword swap bytes
  void ma_dsbh(Register rd, Register rt);
  void ma_dshd(Register rd, Register rt);

  void ma_dctz(Register rd, Register rs);

  // load
  FaultingCodeRange ma_load(Register dest, Address address,
                            LoadStoreSize size = SizeWord,
                            LoadStoreExtension extension = SignExtend);

  // store
  FaultingCodeRange ma_store(Register data, Address address,
                             LoadStoreSize size = SizeWord,
                             LoadStoreExtension extension = SignExtend);
  FaultingCodeRange ma_store(ImmWord imm, const BaseIndex& dest,
                             LoadStoreSize size = SizeWord,
                             LoadStoreExtension extension = SignExtend);
  FaultingCodeRange ma_store(ImmWord imm, Address address,
                             LoadStoreSize size = SizeWord,
                             LoadStoreExtension extension = SignExtend);
  // arithmetic based ops
  // add
  void ma_daddu(Register rd, Register rs, Imm32 imm);
  void ma_daddu(Register rd, Register rs, ImmWord imm);
  void ma_daddu(Register rd, Register rs);
  void ma_daddu(Register rd, Imm32 imm);
  void ma_add32TestOverflow(Register rd, Register rs, Register rt,
                            Label* overflow);
  void ma_add32TestOverflow(Register rd, Register rs, Imm32 imm,
                            Label* overflow);
  void ma_addPtrTestOverflow(Register rd, Register rs, Register rt,
                             Label* overflow);
  void ma_addPtrTestOverflow(Register rd, Register rs, Imm32 imm,
                             Label* overflow);
  void ma_addPtrTestOverflow(Register rd, Register rs, ImmWord imm,
                             Label* overflow);
  void ma_addPtrTestCarry(Condition cond, Register rd, Register rs, Register rt,
                          Label* overflow);
  void ma_addPtrTestCarry(Condition cond, Register rd, Register rs, Imm32 imm,
                          Label* overflow);
  void ma_addPtrTestCarry(Condition cond, Register rd, Register rs, ImmWord imm,
                          Label* overflow);
  void ma_addPtrTestSigned(Condition cond, Register rd, Register rj,
                           Register rk, Label* taken);
  void ma_addPtrTestSigned(Condition cond, Register rd, Register rj, Imm32 imm,
                           Label* taken);
  void ma_addPtrTestSigned(Condition cond, Register rd, Register rj,
                           ImmWord imm, Label* taken);
  // subtract
  void ma_dsubu(Register rd, Register rs, Imm32 imm);
  void ma_dsubu(Register rd, Register rs, ImmWord imm);
  void ma_dsubu(Register rd, Register rs);
  void ma_dsubu(Register rd, Imm32 imm);
  void ma_sub32TestOverflow(Register rd, Register rs, Register rt,
                            Label* overflow);
  void ma_subPtrTestOverflow(Register rd, Register rs, Register rt,
                             Label* overflow);
  void ma_subPtrTestOverflow(Register rd, Register rs, Imm32 imm,
                             Label* overflow);

  // multiplies.  For now, there are only few that we care about.
  void ma_dmulu(Register rd, Register rs, Register rt);
  void ma_dmulu(Register rd, Register rs, ImmWord imm);
  void ma_mulPtrTestOverflow(Register rd, Register rs, Register rt,
                             Label* overflow);

  // stack
  void ma_pop(Register r);
  void ma_push(Register r);

  void branchWithCode(InstImm code, Label* label, JumpKind jumpKind,
                      Register branchCodeScratch = InvalidReg);
  // branches when done from within mips-specific code
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
    ma_load(scratch, addr, SizeDouble);
    ma_b(scratch, rhs, l, c, jumpKind);
  }

  void ma_bal(Label* l, DelaySlotFill delaySlotFill = FillDelaySlot);

  // fp instructions
  void ma_lid(FloatRegister dest, double value);

  void ma_mv(FloatRegister src, ValueOperand dest);
  void ma_mv(ValueOperand src, FloatRegister dest);

  FaultingCodeRange ma_ls(FloatRegister ft, Address address);
  FaultingCodeRange ma_ld(FloatRegister ft, Address address);
  FaultingCodeRange ma_sd(FloatRegister ft, Address address);
  FaultingCodeRange ma_ss(FloatRegister ft, Address address);

  void ma_pop(FloatRegister f);
  void ma_push(FloatRegister f);

  void ma_cmp_set(Register dst, Register lhs, ImmWord imm, Condition c);
  void ma_cmp_set(Register dst, Register lhs, ImmPtr imm, Condition c);
  void ma_cmp_set(Register dst, Register lhs, ImmGCPtr imm, Condition c);
  void ma_cmp_set(Register dst, Address address, Register rhs, Condition c);
  void ma_cmp_set(Register dst, Address address, ImmWord imm, Condition c);
  void ma_cmp_set(Register dst, Address address, Imm32 imm, Condition c);

  // These functions abstract the access to high part of the double precision
  // float register. They are intended to work on both 32 bit and 64 bit
  // floating point coprocessor.
  void moveToDoubleHi(Register src, FloatRegister dest) { as_mthc1(src, dest); }
  void moveFromDoubleHi(FloatRegister src, Register dest) {
    as_mfhc1(dest, src);
  }

  void moveToDouble(Register src, FloatRegister dest) { as_dmtc1(src, dest); }
  void moveFromDouble(FloatRegister src, Register dest) { as_dmfc1(dest, src); }
};

class MacroAssembler;

class MacroAssemblerMIPS64Compat : public MacroAssemblerMIPS64 {
 public:
  using MacroAssemblerMIPS64::call;

  MacroAssemblerMIPS64Compat() {}

  void convertBoolToInt32(Register source, Register dest);
  void convertInt32ToDouble(Register src, FloatRegister dest);
  void convertInt32ToDouble(const Address& src, FloatRegister dest);
  void convertInt32ToDouble(const BaseIndex& src, FloatRegister dest);
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
    ma_daddu(dest, address.base, Imm32(address.offset));
  }

  void computeEffectiveAddress(const BaseIndex& address, Register dest);

  void computeEffectiveAddress32(const Address& address, Register dest) {
    ma_addu(dest, address.base, Imm32(address.offset));
  }

  void computeEffectiveAddress32(const BaseIndex& address, Register dest) {
    computeScaledAddress32(address, dest);
    if (address.offset) {
      ma_addu(dest, dest, Imm32(address.offset));
    }
  }

  void j(Label* dest) { ma_b(dest); }

  void mov(Register src, Register dest) { as_ori(dest, src, 0); }
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
    BufferOffset bo = m_buffer.nextOffset();
    addPendingJump(bo, ImmPtr(c->raw()), RelocationKind::JITCODE);
    Register scratch = temps.Acquire();
    ma_liPatchable(scratch, ImmPtr(c->raw()));
    as_jr(scratch);
    as_nop();
  }
  void branch(const Register reg) {
    as_jr(reg);
    as_nop();
  }
  void nop() { as_nop(); }
  BufferOffset ret() {
    ma_pop(ra);
    BufferOffset offset = as_jr(ra);
    as_nop();
    return offset;
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
    ma_dsubu(StackPointer, StackPointer, Imm32(offset));
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
    ma_daddu(StackPointer, StackPointer,
             Imm32(int32_t(sizeof...(Regs) * sizeof(intptr_t))));
  }

  // Emit a branch that can be toggled to a non-operation. On MIPS64 we use
  // "andi" instruction to toggle the branch.
  // See ToggleToJmp(), ToggleToCmp().
  CodeOffset toggledJump(Label* label);

  // Emit a "jalr" or "nop" instruction. ToggleCall can be used to patch
  // this instruction.
  CodeOffset toggledCall(JitCode* target, bool enabled);

  static size_t ToggledCallSize(uint8_t* code) {
    // Six instructions used in: MacroAssemblerMIPS64Compat::toggledCall
    return 6 * sizeof(uint32_t);
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
  void jump(Register reg) {
    as_jr(reg);
    as_nop();
  }
  void jump(const Address& address) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    loadPtr(address, scratch);
    as_jr(scratch);
    as_nop();
  }

  void jump(JitCode* code) { branch(code); }

  void jump(ImmPtr ptr) {
    BufferOffset bo = m_buffer.nextOffset();
    addPendingJump(bo, ptr, RelocationKind::HARDCODED);
    ma_jump(ptr);
  }

  void jump(TrampolinePtr code) { jump(ImmPtr(code.value)); }

  void splitTag(Register src, Register dest) {
    ma_dsrl(dest, src, Imm32(JSVAL_TAG_SHIFT));
  }

  void splitTag(const ValueOperand& operand, Register dest) {
    splitTag(operand.valueReg(), dest);
  }

  void splitTagForTest(const ValueOperand& value, ScratchTagScope& tag) {
    splitTag(value, tag);
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
    UseScratchRegisterScope temps(*this);
    MOZ_ASSERT(type != JSVAL_TYPE_DOUBLE);
    if (type == JSVAL_TYPE_INT32 || type == JSVAL_TYPE_BOOLEAN) {
      ma_sll(dest, src, Imm32(0));
      return;
    }
    Register scratch = temps.Acquire();
    MOZ_ASSERT(scratch != src);
    mov(ImmShiftedTag(type), scratch);
    as_xor(dest, src, scratch);
  }

  void unboxGCThingForGCBarrier(const Address& src, Register dest) {
    loadPtr(src, dest);
    ma_dext(dest, dest, Imm32(0), Imm32(JSVAL_TAG_SHIFT));
  }
  void unboxGCThingForGCBarrier(const ValueOperand& src, Register dest) {
    ma_dext(dest, src.valueReg(), Imm32(0), Imm32(JSVAL_TAG_SHIFT));
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
    splitTag(value, scratch);
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

  void storeValue(ValueOperand val, Operand dst);
  void storeValue(ValueOperand val, const BaseIndex& dest);
  void storeValue(JSValueType type, Register reg, BaseIndex dest);
  void storeValue(ValueOperand val, const Address& dest);
  void storeValue(JSValueType type, Register reg, Address dest);
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
  void loadValue(Operand dest, ValueOperand val) {
    loadValue(dest.toAddress(), val);
  }
  void loadValue(const BaseIndex& addr, ValueOperand val);

  void loadUnalignedValue(const Address& src, ValueOperand dest) {
    loadValue(src, dest);
  }

  void tagValue(JSValueType type, Register payload, ValueOperand dest);

  void pushValue(ValueOperand val);
  void popValue(ValueOperand val);
  void pushValue(const Value& val) {
    if (val.isGCThing()) {
      UseScratchRegisterScope temps(*this);
      writeDataRelocation(val);
      Register scratch = temps.Acquire();
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
    ma_load_unaligned(dest, src, SizeHalfWord, SignExtend);
  }

  FaultingCodeRange load16ZeroExtend(const Address& address, Register dest);
  FaultingCodeRange load16ZeroExtend(const BaseIndex& src, Register dest);

  template <typename S>
  void load16UnalignedZeroExtend(const S& src, Register dest) {
    ma_load_unaligned(dest, src, SizeHalfWord, ZeroExtend);
  }

  FaultingCodeRange load32(const Address& address, Register dest);
  FaultingCodeRange load32(const BaseIndex& address, Register dest);
  void load32(AbsoluteAddress address, Register dest);
  void load32(wasm::SymbolicAddress address, Register dest);

  template <typename S>
  void load32Unaligned(const S& src, Register dest) {
    ma_load_unaligned(dest, src, SizeWord, SignExtend);
  }

  FaultingCodeRange load64(const Address& address, Register64 dest) {
    return loadPtr(address, dest.reg);
  }
  FaultingCodeRange load64(const BaseIndex& address, Register64 dest) {
    return loadPtr(address, dest.reg);
  }

  template <typename S>
  void load64Unaligned(const S& src, Register64 dest) {
    ma_load_unaligned(dest.reg, src, SizeDouble, ZeroExtend);
  }

  FaultingCodeRange loadPtr(const Address& address, Register dest);
  FaultingCodeRange loadPtr(const BaseIndex& src, Register dest);
  void loadPtr(AbsoluteAddress address, Register dest);
  void loadPtr(wasm::SymbolicAddress address, Register dest);

  void loadPrivate(const Address& address, Register dest);

  FaultingCodeRange loadUnalignedDouble(const wasm::MemoryAccessDesc& access,
                                        const BaseIndex& src, Register temp,
                                        FloatRegister dest);
  FaultingCodeRange loadUnalignedFloat32(const wasm::MemoryAccessDesc& access,
                                         const BaseIndex& src, Register temp,
                                         FloatRegister dest);

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
    ma_store_unaligned(src, dest, SizeHalfWord);
  }

  FaultingCodeRange store32(Register src, const Address& address);
  FaultingCodeRange store32(Register src, const BaseIndex& address);
  void store32(Register src, AbsoluteAddress address);
  FaultingCodeRange store32(Imm32 src, const Address& address);
  void store32(Imm32 src, const BaseIndex& address);

  template <typename T>
  void store32Unaligned(Register src, const T& dest) {
    ma_store_unaligned(src, dest, SizeWord);
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
    ma_store_unaligned(src.reg, dest, SizeDouble);
  }

  template <typename T>
  FaultingCodeRange storePtr(ImmWord imm, T address);
  template <typename T>
  void storePtr(ImmPtr imm, T address);
  template <typename T>
  void storePtr(ImmGCPtr imm, T address);
  FaultingCodeRange storePtr(Register src, const Address& address);
  FaultingCodeRange storePtr(Register src, const BaseIndex& address);
  void storePtr(Register src, AbsoluteAddress dest);

  FaultingCodeRange storeUnalignedFloat32(const wasm::MemoryAccessDesc& access,
                                          FloatRegister src, Register temp,
                                          const BaseIndex& dest);
  FaultingCodeRange storeUnalignedDouble(const wasm::MemoryAccessDesc& access,
                                         FloatRegister src, Register temp,
                                         const BaseIndex& dest);

  void moveDouble(FloatRegister src, FloatRegister dest) { as_movd(dest, src); }

  void zeroDouble(FloatRegister reg) { moveToDouble(zero, reg); }

  void convertUInt64ToDouble(Register src, FloatRegister dest);

  void breakpoint();
  void crash();

  void checkStackAlignment();

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
    ma_daddu(dest, addr.baseReg(), Imm32(addr.disp()));
  }

  void abiret() {
    as_jr(ra);
    as_nop();
  }

  void moveFloat32(FloatRegister src, FloatRegister dest) {
    as_movs(dest, src);
  }

  // Instrumentation for entering and leaving the profiler.
  void profilerEnterFrame(Register framePtr, Register scratch);
  void profilerExitFrame();
};

typedef MacroAssemblerMIPS64Compat MacroAssemblerSpecific;

}  // namespace jit
}  // namespace js

#endif /* jit_mips64_MacroAssembler_mips64_h */
