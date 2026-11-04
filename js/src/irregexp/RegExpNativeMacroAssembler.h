/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements the NativeRegExpMacroAssembler interface for
// SpiderMonkey. It provides the same interface as each of V8's
// architecture-specific implementations.

#ifndef RegexpMacroAssemblerArch_h
#define RegexpMacroAssemblerArch_h

#include "irregexp/imported/regexp-macro-assembler.h"
#include "jit/MacroAssembler.h"

/*
 * [SMDOC] RegExp backtrack stack
 *
 * Irregexp is a backtracking engine. To keep track of possible backtrack
 * points, it maintains a backtrack stack, where the engine keeps information
 * about other possible matches to consider if the current match fails. For
 * example, the engine may push a code location (represented as an offset from
 * the code base address) that can be jumped to if the current attempted match
 * fails. The engine may also push a position in the input string: see
 * PushCurrentPosition and CheckFixedLengthLoop. The backtrack stack grows
 * downward in memory, like the hardware stack.
 *
 * This stack can be quite deep, so instead of using the native stack, we
 * allocate space on the heap. This is managed by regexp::Stack (implemented in
 * imported/regexp_stack.cc). To avoid allocation overhead for simple regexps,
 * regexp::Stack owns a 1KB static buffer that is not freed between executions.
 * If the current buffer fills up, the regexp will call GrowBacktrackStack to
 * grow it.
 *
 * The backtrack stack is allocated per-context. If a regexp is interrupted,
 * and the interrupt handler executes another regexp, we have to ensure that
 * the backtrack stack is reentrant-safe. To accomplish this, regexp::Stack
 * has a `stack_pointer` field that represents the base of the stack for the
 * purposes of the next regexp to execute. This will normally be the base of
 * the backtrack stack itself, but if a regexp is interrupted, it will store
 * the top of its current backtrack stack, ensuring that it won't be clobbered
 * by reentrant regexp execution in the interrupt handler.
 *
 * We track three backtrack-stack-related values in regexp jitcode:
 * 1. backtrack_stack_pointer_: the top of the backtrack stack, pinned to a
 *     register. This corresponds to regexp::Stack::stack_pointer().
 * 2. FrameData::backtrackStackBase: a pointer to the base of the backing buffer
 *     for the stack. This corresponds to regexp::Stack::memory_top(). It is
 *     used in WriteStackPointerToRegister / ReadStackPointerFromRegister, which
 *     store a position in the backtrack stack to a Register (irregexp's
 *     confusing name for a local stack slot). By storing the position as an
 *     offset from the base instead of a raw pointer, we avoid corruption when
 *     the stack is reallocated.
 * 3. FrameData::initialBacktrackStackPointer: the top of the backtrack stack
 *     before we were invoked, stored as a byte offset from backtrackStackBase.
 *     This is initialized during the prologue. In the epilogue, we use it to
 *     restore the correct value. By storing it as an offset instead of a raw
 *     pointer, we ensure that if the backtrack stack has been reallocated,
 *     we will store a pointer into the current buffer, not the old one.
 *
 * On platforms with enough registers, we also pin a copy of the
 * regexp::Stack's limit to backtrack_stack_limit_, because it is checked on
 * most pushes. On x86, we load the limit from memory instead.
 *
 * Whenever we call into C++ that can reallocate the backtrack stack (that is,
 * GrowBacktrackStack or HandleRegExpInterrupt), we update the stack pointer
 * in the regexp::Stack with the current value of backtrack_stack_pointer_.
 * When we return, we reload backtrack_stack_pointer_, backtrackStackBase, and
 * (if available) backtrack_stack_limit_, and store the stack base into our
 * local FrameData.
 *
 */

namespace v8 {
namespace internal {
namespace regexp {

struct FrameData {
  // Character position at the start of the input, stored as a
  // negative offset from the end of the string (input_end_pointer_).
  size_t inputStart;

  // `backtrackStackBase` points to the bottom of the backtrack stack.
  // The backtrack_stack_pointer_ register points to the top.
  // `initialBacktrackStackPointer` stores the initial offset between
  // those values, which may be non-zero if one regexp is interrupted and
  // another regexp is executed by the interrupt handler.
  void* backtrackStackBase;
  size_t initialBacktrackStackPointer;

  // Copy of the input MatchPairs.
  int32_t* matches;    // pointer to capture array
  int32_t numMatches;  // size of capture array

  // Whether this regexp can resume after being interrupted. True
  // iff we were invoked from C++.
  uint32_t canResume;

  // The input string itself. We store it here so that we can root it when
  // interrupted.
  JSString* inputString;
};

class SMRegExpMacroAssembler final : public NativeRegExpMacroAssembler {
 public:
  SMRegExpMacroAssembler(JSContext* cx, js::jit::StackMacroAssembler& masm,
                         Zone* zone, Mode mode, uint32_t num_capture_registers);
  virtual ~SMRegExpMacroAssembler() = default;

  virtual int stack_limit_slack_slot_count();
  virtual IrregexpImplementation Implementation();

  virtual bool Succeed();
  virtual void Fail();

  virtual void AdvanceCurrentPosition(int by);
  virtual void PopCurrentPosition();
  virtual void PushCurrentPosition();
  virtual void SetCurrentPositionFromEnd(int by);

  virtual void Backtrack();
  virtual void Bind(Label* label);
  virtual void GoTo(Label* label);
  virtual void PushBacktrack(Label* label);

  virtual void CheckCharacter(uint32_t c, Label* on_equal);
  virtual void CheckNotCharacter(uint32_t c, Label* on_not_equal);
  virtual void CheckCharacterGT(base::uc16 limit, Label* on_greater);
  virtual void CheckCharacterLT(base::uc16 limit, Label* on_less);
  virtual void CheckCharacterAfterAnd(uint32_t c, uint32_t mask,
                                      Label* on_equal);
  virtual void CheckNotCharacterAfterAnd(uint32_t c, uint32_t mask,
                                         Label* on_not_equal);
  virtual void CheckNotCharacterAfterMinusAnd(base::uc16 c, base::uc16 minus,
                                              base::uc16 mask,
                                              Label* on_not_equal);
  virtual void CheckFixedLengthLoop(Label* on_tos_equals_current_position);
  virtual void CheckCharacterInRange(base::uc16 from, base::uc16 to,
                                     Label* on_in_range);
  virtual void CheckCharacterNotInRange(base::uc16 from, base::uc16 to,
                                        Label* on_not_in_range);
  virtual bool CheckCharacterInRangeArray(
      const ZoneList<CharacterRange>* ranges, Label* on_in_range);
  virtual bool CheckCharacterNotInRangeArray(
      const ZoneList<CharacterRange>* ranges, Label* on_not_in_range);
  virtual void CheckAtStart(int cp_offset, Label* on_at_start);
  virtual void CheckNotAtStart(int cp_offset, Label* on_not_at_start);
  virtual void CheckPosition(int cp_offset, Label* on_outside_input);
  virtual void CheckBitInTable(Handle<ByteArray> table, Label* on_bit_set);
  virtual void SkipUntilBitInTable(int cp_offset, Handle<ByteArray> table,
                                   Handle<ByteArray> nibble_table,
                                   int advance_by, int bounds_check_offset,
                                   Label* on_match, Label* on_no_match);
  virtual bool SkipUntilBitInTableUseSimd(int advance_by);
  virtual void CheckSpecialClassRanges(StandardCharacterSet type,
                                       Label* on_no_match);
  virtual void CheckNotBackReference(int start_reg, bool read_backward,
                                     Label* on_no_match);
  virtual void CheckNotBackReferenceIgnoreCase(int start_reg,
                                               bool read_backward, bool unicode,
                                               Label* on_no_match);

  virtual void AdvanceRegister(int reg, int by);
  virtual void IfRegisterGE(int reg, int comparand, Label* if_ge);
  virtual void IfRegisterLT(int reg, int comparand, Label* if_lt);
  virtual void IfRegisterEqPos(int reg, Label* if_eq);
  virtual void PopRegister(int register_index);
  virtual void PushRegister(int register_index,
                            StackCheckFlag check_stack_limit);
  virtual void ReadCurrentPositionFromRegister(int reg);
  virtual void WriteCurrentPositionToRegister(int reg, int cp_offset);
  virtual void ReadStackPointerFromRegister(int reg);
  virtual void WriteStackPointerToRegister(int reg);
  virtual void SetRegister(int register_index, int to);
  virtual void ClearRegisters(int reg_from, int reg_to);

  virtual void RecordComment(std::string_view comment) {}
  virtual MacroAssembler* masm() { return &masm_; }

  virtual Handle<HeapObject> GetCode(Handle<RegExpData> data, Flags flags);

  virtual bool CanReadUnaligned() const;

 private:
  size_t frameSize_ = 0;

  void createStackFrame();
  void initFrameAndRegs();
  void successHandler();
  void exitHandler();
  void backtrackHandler();
  void stackOverflowHandler();

  // Push a register on the backtrack stack.
  void Push(js::jit::Register value);

  // Pop a value from the backtrack stack.
  void Pop(js::jit::Register target);

  void CheckAtStartImpl(int cp_offset, Label* on_cond,
                        js::jit::Assembler::Condition cond);
  void CheckCharacterImpl(js::jit::Imm32 c, Label* on_cond,
                          js::jit::Assembler::Condition cond);
  void CheckCharacterAfterAndImpl(uint32_t c, uint32_t and_with, Label* on_cond,
                                  bool negate);
  void CheckCharacterInRangeImpl(base::uc16 from, base::uc16 to, Label* on_cond,
                                 js::jit::Assembler::Condition cond);
  void CheckNotBackReferenceImpl(int start_reg, bool read_backward,
                                 bool unicode, Label* on_no_match,
                                 bool ignore_case);
  void CallIsCharacterInRangeArray(const ZoneList<CharacterRange>* ranges);

  void LoadCurrentCharacterUnchecked(int cp_offset, int characters);

  void EmitSkipUntilBitInTableSimd(int cp_offset, ByteArrayData* nibble_table,
                                   Label* on_match);

  void JumpOrBacktrack(Label* to);

  // MacroAssembler methods that take a Label can be called with a
  // null label, which means that we should backtrack if we would jump
  // to that label. This is a helper to avoid writing out the same
  // logic a dozen times.
  inline js::jit::Label* LabelOrBacktrack(Label* to) {
    return to ? to->inner() : &backtrack_label_;
  }

  void CheckBacktrackStackLimit();

  void StoreBacktrackStackToMemory();
  void LoadBacktrackStackFromMemory(js::jit::Address backtrackStackBaseAddr);
  void LoadBacktrackStackRegsFromMemory();

 public:
  static bool GrowBacktrackStack(Stack* regexp_stack);

  static uint32_t CaseInsensitiveCompareNonUnicode(const char16_t* substring1,
                                                   const char16_t* substring2,
                                                   size_t byteLength);
  static uint32_t CaseInsensitiveCompareUnicode(const char16_t* substring1,
                                                const char16_t* substring2,
                                                size_t byteLength);
  static bool IsCharacterInRangeArray(uint32_t c, ByteArrayData* ranges);

 private:
  inline int char_size() { return static_cast<int>(mode_); }
  inline js::jit::Scale factor() {
    return mode_ == UC16 ? js::jit::TimesTwo : js::jit::TimesOne;
  }
  inline js::CharEncoding encoding() {
    return mode_ == UC16 ? js::CharEncoding::TwoByte : js::CharEncoding::Latin1;
  }

  js::jit::Address inputStart() {
    return js::jit::Address(masm_.getStackPointer(),
                            offsetof(FrameData, inputStart));
  }
  js::jit::Address backtrackStackBase() {
    return js::jit::Address(masm_.getStackPointer(),
                            offsetof(FrameData, backtrackStackBase));
  }
  js::jit::Address initialBacktrackStackPointer() {
    return js::jit::Address(masm_.getStackPointer(),
                            offsetof(FrameData, initialBacktrackStackPointer));
  }
  js::jit::Address matches() {
    return js::jit::Address(masm_.getStackPointer(),
                            offsetof(FrameData, matches));
  }
  js::jit::Address numMatches() {
    return js::jit::Address(masm_.getStackPointer(),
                            offsetof(FrameData, numMatches));
  }
  js::jit::Address canResume() {
    return js::jit::Address(masm_.getStackPointer(),
                            offsetof(FrameData, canResume));
  }
  js::jit::Address inputString() {
    return js::jit::Address(masm_.getStackPointer(),
                            offsetof(FrameData, inputString));
  }

  // The stack-pointer-relative location of a regexp register.
  js::jit::Address register_location(int register_index) {
    return js::jit::Address(masm_.getStackPointer(),
                            register_offset(register_index));
  }

  int32_t register_offset(int register_index) {
    MOZ_ASSERT(register_index >= 0 && register_index <= kMaxRegister);
    if (num_registers_ <= register_index) {
      num_registers_ = register_index + 1;
    }
    static_assert(alignof(uintptr_t) <= alignof(FrameData));
    return sizeof(FrameData) + register_index * sizeof(uintptr_t*);
  }

  JSContext* cx_;
  js::jit::StackMacroAssembler& masm_;

  /*
   * This assembler uses the following registers:
   *
   * - current_character_:
   *     Contains the character (or characters) currently being examined.
   *     Must be loaded using LoadCurrentCharacter before using any of the
   *     dispatch methods. After a matching pass for a global regexp,
   *     temporarily stores the index of capture start.
   * - current_position_:
   *     Current position in input *as negative byte offset from end of string*.
   * - input_end_pointer_:
   *     Points to byte after last character in the input. current_position_ is
   *     relative to this.
   * - backtrack_stack_pointer_:
   *     Points to tip of the (heap-allocated) backtrack stack. The stack grows
   *     downward (like the native stack).
   * - temp0_, temp1_, temp2_:
   *     Scratch registers.
   * - backtrack_stack_limit_:
   *     Copy of the regexp::Stack's stack limit. Not available on x86.
   *
   * The native stack pointer is used to access arguments (InputOutputData),
   * local variables (FrameData), and irregexp's internal virtual registers
   * (see register_location).
   */

  js::jit::Register current_character_;
  js::jit::Register current_position_;
  js::jit::Register input_end_pointer_;
  js::jit::Register backtrack_stack_pointer_;
  js::jit::Register temp0_, temp1_, temp2_;
  js::jit::Register backtrack_stack_limit_;

  // These labels are used in various API calls and bound (if used) in
  // GetCode. If we abort in the middle of a compilation, as may
  // happen if a regexp is too big, they may be used but not
  // bound.
  js::jit::NonAssertingLabel entry_label_;
  js::jit::NonAssertingLabel start_label_;
  js::jit::NonAssertingLabel backtrack_label_;
  js::jit::NonAssertingLabel success_label_;
  js::jit::NonAssertingLabel exit_label_;
  js::jit::NonAssertingLabel stack_overflow_label_;
  js::jit::NonAssertingLabel exit_with_exception_label_;
  js::jit::NonAssertingLabel exit_overrecursed_label_;

  // When we generate the code to push a backtrack label's address
  // onto the backtrack stack, we don't know its final address. We
  // have to patch it after linking. This is slightly delicate, as the
  // Label itself (which is allocated on the stack) may not exist by
  // the time we link. The approach is as follows:
  //
  // 1. When we push a label on the backtrack stack (PushBacktrack),
  //    we bind the label's patchOffset_ field to the offset within
  //    the code that should be overwritten. This works because each
  //    label is only pushed by a single instruction.
  //
  // 2. When we bind a label (Bind), we check to see if it has a
  //    bound patchOffset_. If it does, we create a LabelPatch mapping
  //    its patch offset to the offset of the label itself.
  //
  // 3. While linking the code, we walk the list of label patches
  //    and patch the code accordingly.
  //
  // 4. Finally, we patch in the code base address that is added to the
  //    label offset to calculate the actual address to jump to.
  class LabelPatch {
   public:
    LabelPatch(js::jit::CodeOffset patchOffset, size_t labelOffset)
        : patchOffset_(patchOffset), labelOffset_(labelOffset) {}

    js::jit::CodeOffset patchOffset_;
    size_t labelOffset_ = 0;
  };

  js::Vector<LabelPatch, 4, js::SystemAllocPolicy> labelPatches_;
  void AddLabelPatch(js::jit::CodeOffset patchOffset, size_t labelOffset) {
    js::AutoEnterOOMUnsafeRegion oomUnsafe;
    if (!labelPatches_.emplaceBack(patchOffset, labelOffset)) {
      oomUnsafe.crash("Irregexp label patch");
    }
  }

  js::Vector<js::jit::CodeOffset, 4, js::SystemAllocPolicy>
      backtrackCodeOffsetPatches_;
  void PushBacktrackCodeOffsetPatch(js::jit::CodeOffset offset) {
    js::AutoEnterOOMUnsafeRegion oomUnsafe;
    if (!backtrackCodeOffsetPatches_.append(offset)) {
      oomUnsafe.crash("Irregexp backtrack code offset patch");
    }
  }

  Mode mode_;
  int num_registers_;
  int num_capture_registers_;
  js::jit::LiveGeneralRegisterSet savedRegisters_;

 public:
  using TableVector =
      js::Vector<PseudoHandle<ByteArrayData>, 4, js::SystemAllocPolicy>;
  TableVector& tables() { return tables_; }

 private:
  TableVector tables_;
  void AddTable(PseudoHandle<ByteArrayData> table) {
    js::AutoEnterOOMUnsafeRegion oomUnsafe;
    if (!tables_.append(std::move(table))) {
      oomUnsafe.crash("Irregexp table append");
    }
  }
};

}  // namespace regexp
}  // namespace internal
}  // namespace v8

#endif  // RegexpMacroAssemblerArch_h
