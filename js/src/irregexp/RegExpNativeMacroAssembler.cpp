/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "irregexp/imported/regexp-macro-assembler-arch.h"
#include "irregexp/imported/regexp-stack.h"
#include "jit/JitZone.h"
#include "jit/Linker.h"
#include "jit/PerfSpewer.h"
#include "vm/MatchPairs.h"
#include "vm/Realm.h"
#ifdef MOZ_VTUNE
#  include "vtune/VTuneWrapper.h"
#endif

#include "jit/ABIFunctionList-inl.h"
#include "jit/MacroAssembler-inl.h"

namespace v8 {
namespace internal {
namespace regexp {

using js::MatchPairs;
using js::jit::ABIType;
using js::jit::AbsoluteAddress;
using js::jit::Address;
using js::jit::AllocatableFloatRegisterSet;
using js::jit::AllocatableGeneralRegisterSet;
using js::jit::Assembler;
using js::jit::BaseIndex;
using js::jit::CheckUnsafeCallWithABI;
using js::jit::CodeLocationLabel;
using js::jit::FloatRegister;
using js::jit::FloatRegisterSet;
using js::jit::GeneralRegisterBackwardIterator;
using js::jit::GeneralRegisterForwardIterator;
using js::jit::GeneralRegisterSet;
using js::jit::Imm32;
using js::jit::ImmPtr;
using js::jit::ImmWord;
using js::jit::JitCode;
using js::jit::Linker;
using js::jit::LiveGeneralRegisterSet;
using js::jit::Register;
using js::jit::Registers;
using js::jit::SimdConstant;
using js::jit::StackMacroAssembler;

SMRegExpMacroAssembler::SMRegExpMacroAssembler(JSContext* cx,
                                               StackMacroAssembler& masm,
                                               Zone* zone, Mode mode,
                                               uint32_t num_capture_registers)
    : NativeRegExpMacroAssembler(cx->isolate.ref(), zone, mode),
      cx_(cx),
      masm_(masm),
      mode_(mode),
      num_registers_(num_capture_registers),
      num_capture_registers_(num_capture_registers) {
  // Each capture has a start and an end register
  MOZ_ASSERT(num_capture_registers_ % 2 == 0);

  AllocatableGeneralRegisterSet regs(GeneralRegisterSet::All());

  input_end_pointer_ = regs.takeAny();
  current_character_ = regs.takeAny();
  current_position_ = regs.takeAny();
  backtrack_stack_pointer_ = regs.takeAny();
  temp0_ = regs.takeAny();
  temp1_ = regs.takeAny();
  if (!regs.empty()) {
    // Not enough registers on x86.
    temp2_ = regs.takeAny();
  }
  if (!regs.empty()) {
    // Not enough registers on x86.
    backtrack_stack_limit_ = regs.takeAny();
  }
  savedRegisters_ = js::jit::SavedNonVolatileRegisters(regs);

  masm_.jump(&entry_label_);  // We'll generate the entry code later
  masm_.bind(&start_label_);  // and continue from here.
}

int SMRegExpMacroAssembler::stack_limit_slack_slot_count() {
  return Stack::kStackLimitSlackSlotCount;
}

void SMRegExpMacroAssembler::AdvanceCurrentPosition(int by) {
  if (by != 0) {
    masm_.addPtr(Imm32(by * char_size()), current_position_);
  }
}

void SMRegExpMacroAssembler::AdvanceRegister(int reg, int by) {
  MOZ_ASSERT(reg >= 0 && reg < num_registers_);
  if (by != 0) {
    masm_.addPtr(Imm32(by), register_location(reg));
  }
}

void SMRegExpMacroAssembler::Backtrack() { masm_.jump(&backtrack_label_); }

void SMRegExpMacroAssembler::Bind(Label* label) {
  masm_.bind(label->inner());
  if (label->patchOffset_.bound()) {
    AddLabelPatch(label->patchOffset_, label->pos());
  }
}

// Check if current_position + cp_offset is the input start
void SMRegExpMacroAssembler::CheckAtStartImpl(int cp_offset, Label* on_cond,
                                              Assembler::Condition cond) {
  Address addr(current_position_, cp_offset * char_size());
  masm_.computeEffectiveAddress(addr, temp0_);

  masm_.branchPtr(cond, inputStart(), temp0_, LabelOrBacktrack(on_cond));
}

void SMRegExpMacroAssembler::CheckAtStart(int cp_offset, Label* on_at_start) {
  CheckAtStartImpl(cp_offset, on_at_start, Assembler::Equal);
}

void SMRegExpMacroAssembler::CheckNotAtStart(int cp_offset,
                                             Label* on_not_at_start) {
  CheckAtStartImpl(cp_offset, on_not_at_start, Assembler::NotEqual);
}

void SMRegExpMacroAssembler::CheckCharacterImpl(Imm32 c, Label* on_cond,
                                                Assembler::Condition cond) {
  masm_.branch32(cond, current_character_, c, LabelOrBacktrack(on_cond));
}

void SMRegExpMacroAssembler::CheckCharacter(uint32_t c, Label* on_equal) {
  CheckCharacterImpl(Imm32(c), on_equal, Assembler::Equal);
}

void SMRegExpMacroAssembler::CheckNotCharacter(uint32_t c,
                                               Label* on_not_equal) {
  CheckCharacterImpl(Imm32(c), on_not_equal, Assembler::NotEqual);
}

void SMRegExpMacroAssembler::CheckCharacterGT(base::uc16 limit,
                                              Label* on_greater) {
  CheckCharacterImpl(Imm32(limit), on_greater, Assembler::GreaterThan);
}

void SMRegExpMacroAssembler::CheckCharacterLT(base::uc16 limit,
                                              Label* on_less) {
  CheckCharacterImpl(Imm32(limit), on_less, Assembler::LessThan);
}

// Bitwise-and the current character with mask and then check for a
// match with c.
void SMRegExpMacroAssembler::CheckCharacterAfterAndImpl(uint32_t c,
                                                        uint32_t mask,
                                                        Label* on_cond,
                                                        bool is_not) {
  if (c == 0) {
    Assembler::Condition cond = is_not ? Assembler::NonZero : Assembler::Zero;
    masm_.branchTest32(cond, current_character_, Imm32(mask),
                       LabelOrBacktrack(on_cond));
  } else {
    Assembler::Condition cond = is_not ? Assembler::NotEqual : Assembler::Equal;
    masm_.and32(Imm32(mask), current_character_, temp0_);
    masm_.branch32(cond, temp0_, Imm32(c), LabelOrBacktrack(on_cond));
  }
}

void SMRegExpMacroAssembler::CheckCharacterAfterAnd(uint32_t c, uint32_t mask,
                                                    Label* on_equal) {
  CheckCharacterAfterAndImpl(c, mask, on_equal, /*is_not =*/false);
}

void SMRegExpMacroAssembler::CheckNotCharacterAfterAnd(uint32_t c,
                                                       uint32_t mask,
                                                       Label* on_not_equal) {
  CheckCharacterAfterAndImpl(c, mask, on_not_equal, /*is_not =*/true);
}

// Subtract minus from the current character, then bitwise-and the
// result with mask, then check for a match with c.
void SMRegExpMacroAssembler::CheckNotCharacterAfterMinusAnd(
    base::uc16 c, base::uc16 minus, base::uc16 mask, Label* on_not_equal) {
  masm_.computeEffectiveAddress(Address(current_character_, -minus), temp0_);
  if (c == 0) {
    masm_.branchTest32(Assembler::NonZero, temp0_, Imm32(mask),
                       LabelOrBacktrack(on_not_equal));
  } else {
    masm_.and32(Imm32(mask), temp0_);
    masm_.branch32(Assembler::NotEqual, temp0_, Imm32(c),
                   LabelOrBacktrack(on_not_equal));
  }
}

// If the current position matches the position stored on top of the backtrack
// stack, pops the backtrack stack and branches to the given label.
void SMRegExpMacroAssembler::CheckFixedLengthLoop(Label* on_equal) {
  js::jit::Label fallthrough;
  masm_.load32SignExtendToPtr(Address(backtrack_stack_pointer_, 0), temp0_);
  masm_.branchPtr(Assembler::NotEqual, temp0_, current_position_, &fallthrough);
  masm_.addPtr(Imm32(sizeof(int32_t)), backtrack_stack_pointer_);  // Pop.
  JumpOrBacktrack(on_equal);
  masm_.bind(&fallthrough);
}

void SMRegExpMacroAssembler::CheckCharacterInRangeImpl(
    base::uc16 from, base::uc16 to, Label* on_cond, Assembler::Condition cond) {
  // x is in [from,to] if unsigned(x - from) <= to - from
  masm_.computeEffectiveAddress(Address(current_character_, -from), temp0_);
  masm_.branch32(cond, temp0_, Imm32(to - from), LabelOrBacktrack(on_cond));
}

void SMRegExpMacroAssembler::CheckCharacterInRange(base::uc16 from,
                                                   base::uc16 to,
                                                   Label* on_in_range) {
  CheckCharacterInRangeImpl(from, to, on_in_range, Assembler::BelowOrEqual);
}

void SMRegExpMacroAssembler::CheckCharacterNotInRange(base::uc16 from,
                                                      base::uc16 to,
                                                      Label* on_not_in_range) {
  CheckCharacterInRangeImpl(from, to, on_not_in_range, Assembler::Above);
}

/* static */
bool SMRegExpMacroAssembler::IsCharacterInRangeArray(uint32_t c,
                                                     ByteArrayData* ranges) {
  js::AutoUnsafeCallWithABI unsafe;
  MOZ_ASSERT(ranges->length() % sizeof(uint16_t) == 0);
  uint32_t length = ranges->length() / sizeof(uint16_t);
  MOZ_ASSERT(length > 0);

  // Fast paths.
  if (c < ranges->getTyped<uint16_t>(0)) {
    // |c| is lower than the start of the first range.
    // It is not in the range array.
    return false;
  }
  if (c >= ranges->getTyped<uint16_t>(length - 1)) {
    // |c| is higher than the last entry. If the table contains an odd
    // number of entries, the last range is open-ended, so |c| is in
    // the range array iff |length| is odd.
    return (length % 2) != 0;
  }

  // |ranges| is stored as an interval list: an ordered list of
  // starting points, where every even index marks the beginning of a
  // range of characters that are included, and every odd index marks
  // the beginning of a range of characters that are excluded. For
  // example, the set [1,2,3,7,8,9] would be represented as the
  // range array [1,4,7,10]. If |ranges| has an odd number of entries,
  // the last included range is open-ended (so the set containing
  // every character would be represented as [0]).
  //
  // Because of the symmetry between included and excluded ranges, we
  // can do a binary search for the index in |ranges| with the value
  // closest to but not exceeding |c|. If that index is even, |c| is
  // in an included range. If that index is odd, |c| is in an excluded
  // range.
  uint32_t lower = 0;
  uint32_t upper = length;
  uint32_t mid = 0;
  do {
    mid = lower + (upper - lower) / 2;
    const base::uc16 elem = ranges->getTyped<uint16_t>(mid);
    if (c < elem) {
      upper = mid;
    } else if (c > elem) {
      lower = mid + 1;
    } else {
      break;
    }
  } while (lower < upper);
  uint32_t rangeIndex = c < ranges->getTyped<uint16_t>(mid) ? mid - 1 : mid;

  // Included ranges start at even indices and end at odd indices.
  return rangeIndex % 2 == 0;
}

void SMRegExpMacroAssembler::CallIsCharacterInRangeArray(
    const ZoneList<CharacterRange>* ranges) {
  Handle<ByteArray> rangeArray = GetOrAddRangeArray(ranges);
  masm_.movePtr(ImmPtr(rangeArray->inner()), temp0_);

  // Save volatile regs. Temp regs don't need to be saved.
  LiveGeneralRegisterSet volatileRegs(GeneralRegisterSet::Volatile());
  volatileRegs.takeUnchecked(temp0_);
  volatileRegs.takeUnchecked(temp1_);
  if (temp2_ != js::jit::InvalidReg) {
    volatileRegs.takeUnchecked(temp2_);
  }
  masm_.PushRegsInMask(volatileRegs);

  using Fn = bool (*)(uint32_t, ByteArrayData*);
  masm_.setupUnalignedABICall(temp1_);
  masm_.passABIArg(current_character_);
  masm_.passABIArg(temp0_);

  masm_.callWithABI<Fn, ::js::irregexp::IsCharacterInRangeArray>();
  masm_.storeCallBoolResult(temp1_);
  masm_.PopRegsInMask(volatileRegs);

  // GetOrAddRangeArray caches previously seen range arrays to reduce
  // memory usage, so this may not be the first time we've seen this
  // range array. We only need to transfer ownership from the
  // HandleScope to the |tables_| vector once.
  PseudoHandle<ByteArrayData> rawRangeArray =
      rangeArray->maybeTakeOwnership(isolate());
  if (rawRangeArray) {
    AddTable(std::move(rawRangeArray));
  }
}

bool SMRegExpMacroAssembler::CheckCharacterInRangeArray(
    const ZoneList<CharacterRange>* ranges, Label* on_in_range) {
  CallIsCharacterInRangeArray(ranges);
  masm_.branchTest32(Assembler::NonZero, temp1_, temp1_,
                     LabelOrBacktrack(on_in_range));
  return true;
}

bool SMRegExpMacroAssembler::CheckCharacterNotInRangeArray(
    const ZoneList<CharacterRange>* ranges, Label* on_not_in_range) {
  CallIsCharacterInRangeArray(ranges);
  masm_.branchTest32(Assembler::Zero, temp1_, temp1_,
                     LabelOrBacktrack(on_not_in_range));
  return true;
}

void SMRegExpMacroAssembler::CheckBitInTable(Handle<ByteArray> table,
                                             Label* on_bit_set) {
  // Claim ownership of the ByteArray from the current HandleScope.
  // ByteArrays are allocated on the C++ heap and are (eventually)
  // owned by the RegExpShared.
  PseudoHandle<ByteArrayData> rawTable = table->takeOwnership(isolate());

  masm_.movePtr(ImmPtr(rawTable->data()), temp0_);

  masm_.and32(Imm32(kTableMask), current_character_, temp1_);

  masm_.load8ZeroExtend(BaseIndex(temp0_, temp1_, js::jit::TimesOne), temp0_);
  masm_.branchTest32(Assembler::NonZero, temp0_, temp0_,
                     LabelOrBacktrack(on_bit_set));

  // Transfer ownership of |rawTable| to the |tables_| vector.
  AddTable(std::move(rawTable));
}

void SMRegExpMacroAssembler::SkipUntilBitInTable(
    int cp_offset, Handle<ByteArray> table, Handle<ByteArray> nibble_table,
    int advance_by, int bounds_check_offset, Label* on_match,
    Label* on_no_match) {
  // Claim ownership of the ByteArray from the current HandleScope.
  // ByteArrays are allocated on the C++ heap and are (eventually)
  // owned by the RegExpShared.
  PseudoHandle<ByteArrayData> rawTable = table->takeOwnership(isolate());

  bool useSimd = SkipUntilBitInTableUseSimd(advance_by);
  if (useSimd) {
    PseudoHandle<ByteArrayData> rawNibbleTable =
        nibble_table->takeOwnership(isolate());

    MOZ_ASSERT(advance_by == 1);
    EmitSkipUntilBitInTableSimd(cp_offset, rawNibbleTable.get(), on_match);
    AddTable(std::move(rawNibbleTable));
    // Fall through and handle the remaining characters using
    // the scalar version.
  }

  // Scalar version.
  Register tableReg = temp0_;
  masm_.movePtr(ImmPtr(rawTable->data()), tableReg);

  js::jit::Label scalarRepeat;
  masm_.bind(&scalarRepeat);
  LoadCurrentCharacter(cp_offset, on_no_match, true, 1, bounds_check_offset);

  Register index = current_character_;
  if (mode_ != LATIN1 || kTableMask != String::kMaxOneByteCharCode) {
    index = temp1_;
    masm_.and32(Imm32(kTableMask), current_character_, index);
  }

  masm_.load8ZeroExtend(BaseIndex(tableReg, index, js::jit::TimesOne), index);
  masm_.branchTest32(Assembler::NonZero, index, index,
                     LabelOrBacktrack(on_match));
  AdvanceCurrentPosition(advance_by);
  masm_.jump(&scalarRepeat);

  // Transfer ownership of |rawTable| to the |tables_| vector.
  AddTable(std::move(rawTable));
}

void SMRegExpMacroAssembler::EmitSkipUntilBitInTableSimd(
    int cp_offset, ByteArrayData* nibble_table, Label* on_match) {
#ifdef ENABLE_JIT_SIMD
  // The nibble table is only large enough to cover latin-1.
  MOZ_ASSERT(mode_ == LATIN1);

  static constexpr int32_t VectorSize = 16;
  const int32_t CharsPerVector = VectorSize / char_size();

  // Hoist the bounds check.
  // We fall back to the scalar version once there are less than CharsPerVector
  // chars left in the input. We subtract 1 because CheckPosition already
  // assumes we're reading 1 char.
  Label scalarFallback;
  static const int32_t ExtraCharsPerVector = CharsPerVector - 1;
  CheckPosition(cp_offset + ExtraCharsPerVector, &scalarFallback);

  // Allocate SIMD scratch registers from the volatile set.
  AllocatableFloatRegisterSet floatRegs(FloatRegisterSet::Volatile());
  FloatRegister nibbleTable = floatRegs.takeAny().asSimd128();
  FloatRegister nibbleMask = floatRegs.takeAny().asSimd128();
  FloatRegister hiLookup = floatRegs.takeAny().asSimd128();
  FloatRegister inputVec = floatRegs.takeAny().asSimd128();
  FloatRegister loNibbles = floatRegs.takeAny().asSimd128();
  FloatRegister bitmask = floatRegs.takeAny().asSimd128();

  // Hoist constants outside the loop.
  // nibbleTable: 16-byte Boyer-Moore nibble table from regexp data.
  masm_.movePtr(ImmPtr(nibble_table->data()), temp0_);
  masm_.loadUnalignedSimd128(Address(temp0_, 0), nibbleTable);
  // nibbleMask: 0x0f repeated 16 times.
  masm_.loadConstantSimd128(SimdConstant::SplatX16(int8_t(0x0f)), nibbleMask);
  // hiLookup: bit-position table {0x01,0x02,0x04,...,0x80} repeated twice.
  masm_.loadConstantSimd128(
      SimdConstant::SplatX2(int64_t(0x8040201008040201LL)), hiLookup);

  // We loop over the input, 16 bytes at a time. For each byte, we use the low
  // nibble to index into the nibble table, then use the high nibble to index
  // into the resulting byte. For example, 'b' (0x62) loads byte 2 from the
  // table, then checks bit 6. See BoyerMooreLookahead::GetSkipTable. We
  // continue until we find a matching character. Note that there are 256
  // latin-1 characters, but only 128 bits of table. We ignore the high bit of
  // each character (hence the repetition in `hiLookup`), meaning that (for
  // example) 'b' (0x62) and 'â' (0xe2) share a bit. This isn't a correctness
  // issue because the code following this SkipUntilBitInTable will check for a
  // match separately.

  js::jit::Label simdLoop, advanceVector;
  masm_.bind(&simdLoop);

  // Load the next 16 input bytes.
  BaseIndex inputAddr(input_end_pointer_, current_position_, js::jit::TimesOne,
                      cp_offset);
  masm_.loadUnalignedSimd128(inputAddr, inputVec);

  // loNibbles = inputVec & 0x0f
  masm_.bitwiseAndSimd128(nibbleMask, inputVec, loNibbles);

  // hiNibbles = inputVec >> 4
  FloatRegister hiNibbles = inputVec;
  masm_.unsignedRightShiftInt8x16(Imm32(4), inputVec, hiNibbles);

  // For each byte, load the correct row of the table.
  // row = nibbleTable[loNibbles]
  // bitmask = hiLookup[hiNibbles]
  FloatRegister row = loNibbles;
  masm_.swizzleInt8x16Relaxed(nibbleTable, loNibbles, row);
  masm_.swizzleInt8x16Relaxed(hiLookup, hiNibbles, bitmask);

  // Check whether the corresponding bit is set.
  // result = (row & bitmask)) == bitmask
  FloatRegister result = loNibbles;
  masm_.bitwiseAndSimd128(bitmask, row, result);
  masm_.compareInt8x16(Assembler::Equal, result, bitmask, result);

  // Compress the matching bytes into a scalar mask in temp1. If no byte
  // matched, advance to the next chunk. Otherwise, store the index of the
  // lowest matching byte in temp0. On arm64 the mask has four bits per byte,
  // so the trailing zero count is divided by four.
#  if defined(JS_CODEGEN_ARM64)
  masm_.nibbleMaskInt8x16(result, temp1_, /*temp=*/bitmask);
  masm_.branchTestPtr(Assembler::Zero, temp1_, temp1_, &advanceVector);
  masm_.ctz64(js::jit::Register64(temp1_), js::jit::Register64(temp0_));
  masm_.rshiftPtr(Imm32(2), temp0_);
#  elif defined(JS_CODEGEN_X86) || defined(JS_CODEGEN_X64) || \
      defined(JS_CODEGEN_LOONG64)
  masm_.bitmaskInt8x16(result, temp1_);
  masm_.branchTest32(Assembler::Zero, temp1_, temp1_, &advanceVector);
  masm_.ctz32(temp1_, temp0_, /*knownNotZero=*/true);
#  else
#    error Unsupported SIMD architecture
#  endif

  // Found a match in this 16-byte chunk. Advance current_position_ by the
  // index of the lowest matching byte, then jump to on_match.
  masm_.addPtr(temp0_, current_position_);
  masm_.jump(LabelOrBacktrack(on_match));

  masm_.bind(&advanceVector);
  masm_.addPtr(Imm32(VectorSize), current_position_);

  CheckPosition(cp_offset + ExtraCharsPerVector, &scalarFallback);
  masm_.jump(&simdLoop);

  masm_.bind(scalarFallback.inner());
#else
  MOZ_CRASH("SIMD not supported");
#endif  // ENABLE_JIT_SIMD
}

bool SMRegExpMacroAssembler::SkipUntilBitInTableUseSimd(int advance_by) {
#if defined(ENABLE_JIT_SIMD)
#  if defined(JS_CODEGEN_X86) || defined(JS_CODEGEN_X64)
  // SSSE3 is required for pshufb (used to implement swizzleInt8x16).
  if (!js::jit::Assembler::HasSSSE3()) {
    return false;
  }
#  elif defined(JS_CODEGEN_LOONG64)
  if (!js::jit::Assembler::HasLSX()) {
    return false;
  }
#  endif
  // V8 found that using SIMD instead of the scalar version was only
  // faster when we are advancing by 1 byte per iteration.
  return advance_by * char_size() == 1;
#else
  return false;
#endif
}

void SMRegExpMacroAssembler::CheckNotBackReferenceImpl(int start_reg,
                                                       bool read_backward,
                                                       bool unicode,
                                                       Label* on_no_match,
                                                       bool ignore_case) {
  js::jit::Label fallthrough;

  // Captures are stored as a sequential pair of registers.
  // Find the length of the back-referenced capture and load the
  // capture's start index into current_character_.
  masm_.loadPtr(register_location(start_reg),  // index of start
                current_character_);
  masm_.loadPtr(register_location(start_reg + 1), temp0_);  // index of end
  masm_.subPtr(current_character_, temp0_);                 // length of capture

  // Capture registers are either both set or both cleared.
  // If the capture length is zero, then the capture is either empty or cleared.
  // Fall through in both cases.
  masm_.branchPtr(Assembler::Equal, temp0_, ImmWord(0), &fallthrough);

  // Check that there are sufficient characters left in the input.
  if (read_backward) {
    // If start + len > current, there isn't enough room for a
    // lookbehind backreference.
    masm_.loadPtr(inputStart(), temp1_);
    masm_.addPtr(temp0_, temp1_);
    masm_.branchPtr(Assembler::GreaterThan, temp1_, current_position_,
                    LabelOrBacktrack(on_no_match));
  } else {
    // current_position_ is the negative offset from the end.
    // If current + len > 0, there isn't enough room for a backreference.
    masm_.movePtr(current_position_, temp1_);
    masm_.addPtr(temp0_, temp1_);
    masm_.branchPtr(Assembler::GreaterThan, temp1_, ImmWord(0),
                    LabelOrBacktrack(on_no_match));
  }

  if (mode_ == UC16 && ignore_case) {
    // We call a helper function for case-insensitive non-latin1 strings.

    // Save volatile regs. temp1_, temp2_, and current_character_
    // don't need to be saved.  current_position_ needs to be saved
    // even if it's non-volatile, because we modify it to use as an argument.
    LiveGeneralRegisterSet volatileRegs(GeneralRegisterSet::Volatile());
    volatileRegs.addUnchecked(current_position_);
    volatileRegs.takeUnchecked(temp1_);
    if (temp2_ != js::jit::InvalidReg) {
      volatileRegs.takeUnchecked(temp2_);
    }
    volatileRegs.takeUnchecked(current_character_);
    masm_.PushRegsInMask(volatileRegs);

    // Parameters are
    //   Address captured - Address of captured substring's start.
    //   Address current - Address of current character position.
    //   size_t byte_length - length of capture (in bytes)

    // Compute |captured|
    masm_.addPtr(input_end_pointer_, current_character_);

    // Compute |current|
    masm_.addPtr(input_end_pointer_, current_position_);
    if (read_backward) {
      // Offset by length when matching backwards.
      masm_.subPtr(temp0_, current_position_);
    }

    using Fn = uint32_t (*)(const char16_t*, const char16_t*, size_t);
    masm_.setupUnalignedABICall(temp1_);
    masm_.passABIArg(current_character_);
    masm_.passABIArg(current_position_);
    masm_.passABIArg(temp0_);

    if (unicode) {
      masm_.callWithABI<Fn, ::js::irregexp::CaseInsensitiveCompareUnicode>();
    } else {
      masm_.callWithABI<Fn, ::js::irregexp::CaseInsensitiveCompareNonUnicode>();
    }
    masm_.storeCallInt32Result(temp1_);
    masm_.PopRegsInMask(volatileRegs);
    masm_.branchTest32(Assembler::Zero, temp1_, temp1_,
                       LabelOrBacktrack(on_no_match));

    // On success, advance position by length of capture
    if (read_backward) {
      masm_.subPtr(temp0_, current_position_);
    } else {
      masm_.addPtr(temp0_, current_position_);
    }

    masm_.bind(&fallthrough);
    return;
  }

  // We will be modifying current_position_. Save it in case the match fails.
  masm_.push(current_position_);

  // Compute start of capture string
  masm_.addPtr(input_end_pointer_, current_character_);

  // Compute start of match string
  masm_.addPtr(input_end_pointer_, current_position_);
  if (read_backward) {
    // Offset by length when matching backwards.
    masm_.subPtr(temp0_, current_position_);
  }

  // Compute end of match string
  masm_.addPtr(current_position_, temp0_);

  Register nextCaptureChar = temp1_;
  Register nextMatchChar = temp2_;

  if (temp2_ == js::jit::InvalidReg) {
    masm_.push(backtrack_stack_pointer_);
    nextMatchChar = backtrack_stack_pointer_;
  }

  js::jit::Label success;
  js::jit::Label fail;
  js::jit::Label loop;
  masm_.bind(&loop);

  // Load next character from each string.
  if (mode_ == LATIN1) {
    masm_.load8ZeroExtend(Address(current_character_, 0), nextCaptureChar);
    masm_.load8ZeroExtend(Address(current_position_, 0), nextMatchChar);
  } else {
    masm_.load16ZeroExtend(Address(current_character_, 0), nextCaptureChar);
    masm_.load16ZeroExtend(Address(current_position_, 0), nextMatchChar);
  }

  if (ignore_case) {
    MOZ_ASSERT(mode_ == LATIN1);
    // Try exact match.
    js::jit::Label loop_increment;
    masm_.branch32(Assembler::Equal, nextCaptureChar, nextMatchChar,
                   &loop_increment);

    // Mismatch. Try case-insensitive match.
    // Force the capture character to lower case (by setting bit 0x20)
    // then check to see if it is a letter.
    js::jit::Label convert_match;
    masm_.or32(Imm32(0x20), nextCaptureChar);

    // Check if it is in [a,z].
    masm_.computeEffectiveAddress(Address(nextCaptureChar, -'a'),
                                  nextMatchChar);
    masm_.branch32(Assembler::BelowOrEqual, nextMatchChar, Imm32('z' - 'a'),
                   &convert_match);
    // Check for values in range [224,254].
    // Exclude 247 (U+00F7 DIVISION SIGN).
    masm_.sub32(Imm32(224 - 'a'), nextMatchChar);
    masm_.branch32(Assembler::Above, nextMatchChar, Imm32(254 - 224), &fail);
    masm_.branch32(Assembler::Equal, nextMatchChar, Imm32(247 - 224), &fail);

    // Capture character is lower case. Convert match character
    // to lower case and compare.
    masm_.bind(&convert_match);
    masm_.load8ZeroExtend(Address(current_position_, 0), nextMatchChar);
    masm_.or32(Imm32(0x20), nextMatchChar);
    masm_.branch32(Assembler::NotEqual, nextCaptureChar, nextMatchChar, &fail);

    masm_.bind(&loop_increment);
  } else {
    // Fail if characters do not match.
    masm_.branch32(Assembler::NotEqual, nextCaptureChar, nextMatchChar, &fail);
  }

  // Increment pointers into match and capture strings.
  masm_.addPtr(Imm32(char_size()), current_character_);
  masm_.addPtr(Imm32(char_size()), current_position_);

  // Loop if we have not reached the end of the match string.
  masm_.branchPtr(Assembler::Below, current_position_, temp0_, &loop);
  masm_.jump(&success);

  // If we fail, restore current_position_ and branch.
  masm_.bind(&fail);
  if (temp2_ == js::jit::InvalidReg) {
    // Restore backtrack_stack_pointer_ when it was used as a temp register.
    masm_.pop(backtrack_stack_pointer_);
  }
  masm_.pop(current_position_);
  JumpOrBacktrack(on_no_match);

  masm_.bind(&success);

  if (temp2_ == js::jit::InvalidReg) {
    // Restore backtrack_stack_pointer_ when it was used as a temp register.
    masm_.pop(backtrack_stack_pointer_);
  }
  // Drop saved value of current_position_
  masm_.addToStackPtr(Imm32(sizeof(uintptr_t)));

  // current_position_ is a pointer. Convert it back to an offset.
  masm_.subPtr(input_end_pointer_, current_position_);
  if (read_backward) {
    // Subtract match length if we matched backward
    masm_.addPtr(register_location(start_reg), current_position_);
    masm_.subPtr(register_location(start_reg + 1), current_position_);
  }

  masm_.bind(&fallthrough);
}

// Branch if a back-reference does not match a previous capture.
void SMRegExpMacroAssembler::CheckNotBackReference(int start_reg,
                                                   bool read_backward,
                                                   Label* on_no_match) {
  CheckNotBackReferenceImpl(start_reg, read_backward, /*unicode = */ false,
                            on_no_match, /*ignore_case = */ false);
}

void SMRegExpMacroAssembler::CheckNotBackReferenceIgnoreCase(
    int start_reg, bool read_backward, bool unicode, Label* on_no_match) {
  CheckNotBackReferenceImpl(start_reg, read_backward, unicode, on_no_match,
                            /*ignore_case = */ true);
}

// Checks whether the given offset from the current position is
// inside the input string.
void SMRegExpMacroAssembler::CheckPosition(int cp_offset,
                                           Label* on_outside_input) {
  // Note: current_position_ is a (negative) byte offset relative to
  // the end of the input string.
  if (cp_offset >= 0) {
    //      end + current + offset >= end
    // <=>        current + offset >= 0
    // <=>        current          >= -offset
    masm_.branchPtr(Assembler::GreaterThanOrEqual, current_position_,
                    ImmWord(-cp_offset * char_size()),
                    LabelOrBacktrack(on_outside_input));
  } else {
    masm_.loadPtr(inputStart(), temp0_);
    masm_.subPtr(Imm32(cp_offset * char_size()), temp0_);
    masm_.branchPtr(Assembler::LessThan, current_position_, temp0_,
                    LabelOrBacktrack(on_outside_input));
  }
}

// This function generates special case code for character classes.
void SMRegExpMacroAssembler::CheckSpecialClassRanges(StandardCharacterSet type,
                                                     Label* on_no_match) {
  MOZ_ASSERT(CanOptimizeSpecialClassRanges(type));
  js::jit::Label* no_match = LabelOrBacktrack(on_no_match);

  // Note: throughout this function, range checks (c in [min, max])
  // are implemented by an unsigned (c - min) <= (max - min) check.
  switch (type) {
    case StandardCharacterSet::kWhitespace: {
      // Match space-characters
      MOZ_ASSERT(mode_ == LATIN1);
      js::jit::Label success;
      // One byte space characters are ' ', '\t'..'\r', and '\u00a0' (NBSP).

      // Check ' '
      masm_.branch32(Assembler::Equal, current_character_, Imm32(' '),
                     &success);

      // Check '\t'..'\r'
      masm_.computeEffectiveAddress(Address(current_character_, -'\t'), temp0_);
      masm_.branch32(Assembler::BelowOrEqual, temp0_, Imm32('\r' - '\t'),
                     &success);

      // Check \u00a0.
      masm_.branch32(Assembler::NotEqual, temp0_, Imm32(0x00a0 - '\t'),
                     no_match);

      masm_.bind(&success);
      break;
    }
    case StandardCharacterSet::kNotWhitespace:
      // The emitted code for generic character classes is good enough.
      MOZ_CRASH("unreachable");
    case StandardCharacterSet::kDigit:
      // Match latin1 digits ('0'-'9')
      masm_.computeEffectiveAddress(Address(current_character_, -'0'), temp0_);
      masm_.branch32(Assembler::Above, temp0_, Imm32('9' - '0'), no_match);
      break;
    case StandardCharacterSet::kNotDigit:
      // Match anything except latin1 digits ('0'-'9')
      masm_.computeEffectiveAddress(Address(current_character_, -'0'), temp0_);
      masm_.branch32(Assembler::BelowOrEqual, temp0_, Imm32('9' - '0'),
                     no_match);
      break;
    case StandardCharacterSet::kNotLineTerminator:
      // Match non-newlines. This excludes '\n' (0x0a), '\r' (0x0d),
      // U+2028 LINE SEPARATOR, and U+2029 PARAGRAPH SEPARATOR.
      // See https://tc39.es/ecma262/#prod-LineTerminator

      // To test for 0x0a and 0x0d efficiently, we XOR the input with 1.
      // This converts 0x0a to 0x0b, and 0x0d to 0x0c, allowing us to
      // test for the contiguous range 0x0b..0x0c.
      masm_.xor32(Imm32(0x01), current_character_, temp0_);
      masm_.sub32(Imm32(0x0b), temp0_);
      masm_.branch32(Assembler::BelowOrEqual, temp0_, Imm32(0x0c - 0x0b),
                     no_match);

      if (mode_ == UC16) {
        // Compare original value to 0x2028 and 0x2029, using the already
        // computed (current_char ^ 0x01 - 0x0b). I.e., check for
        // 0x201d (0x2028 - 0x0b) or 0x201e.
        masm_.sub32(Imm32(0x2028 - 0x0b), temp0_);
        masm_.branch32(Assembler::BelowOrEqual, temp0_, Imm32(0x2029 - 0x2028),
                       no_match);
      }
      break;
    case StandardCharacterSet::kWord:
      // \w matches the set of 63 characters defined in Runtime Semantics:
      // WordCharacters. We use a static lookup table, which is defined in
      // regexp-macro-assembler.cc.
      // Note: if both Unicode and IgnoreCase are true, \w matches a
      // larger set of characters. That case is handled elsewhere.
      if (mode_ != LATIN1) {
        masm_.branch32(Assembler::Above, current_character_, Imm32('z'),
                       no_match);
      }
      static_assert(arraysize(word_character_map_) > unibrow::Latin1::kMaxChar);
      masm_.movePtr(ImmPtr(&word_character_map_), temp0_);
      masm_.load8ZeroExtend(
          BaseIndex(temp0_, current_character_, js::jit::TimesOne), temp0_);
      masm_.branchTest32(Assembler::Zero, temp0_, temp0_, no_match);
      break;
    case StandardCharacterSet::kNotWord: {
      // See 'w' above.
      js::jit::Label done;
      if (mode_ != LATIN1) {
        masm_.branch32(Assembler::Above, current_character_, Imm32('z'), &done);
      }
      static_assert(arraysize(word_character_map_) > unibrow::Latin1::kMaxChar);
      masm_.movePtr(ImmPtr(&word_character_map_), temp0_);
      masm_.load8ZeroExtend(
          BaseIndex(temp0_, current_character_, js::jit::TimesOne), temp0_);
      masm_.branchTest32(Assembler::NonZero, temp0_, temp0_, no_match);
      if (mode_ != LATIN1) {
        masm_.bind(&done);
      }
      break;
    }
      ////////////////////////////////////////////////////////////////////////
      // Non-standard classes (with no syntactic shorthand) used internally //
      ////////////////////////////////////////////////////////////////////////
    case StandardCharacterSet::kEverything:
      break;
    case StandardCharacterSet::kLineTerminator:
      // Match newlines. The opposite of '.'. See '.' above.
      masm_.xor32(Imm32(0x01), current_character_, temp0_);
      masm_.sub32(Imm32(0x0b), temp0_);
      if (mode_ == LATIN1) {
        masm_.branch32(Assembler::Above, temp0_, Imm32(0x0c - 0x0b), no_match);
      } else {
        MOZ_ASSERT(mode_ == UC16);
        js::jit::Label done;
        masm_.branch32(Assembler::BelowOrEqual, temp0_, Imm32(0x0c - 0x0b),
                       &done);

        // Compare original value to 0x2028 and 0x2029, using the already
        // computed (current_char ^ 0x01 - 0x0b). I.e., check for
        // 0x201d (0x2028 - 0x0b) or 0x201e.
        masm_.sub32(Imm32(0x2028 - 0x0b), temp0_);
        masm_.branch32(Assembler::Above, temp0_, Imm32(0x2029 - 0x2028),
                       no_match);
        masm_.bind(&done);
      }
      break;
  }
}

void SMRegExpMacroAssembler::Fail() {
  masm_.movePtr(ImmWord(int32_t(js::RegExpRunStatus::Success_NotFound)),
                temp0_);
  masm_.jump(&exit_label_);
}

void SMRegExpMacroAssembler::GoTo(Label* to) {
  masm_.jump(LabelOrBacktrack(to));
}

void SMRegExpMacroAssembler::IfRegisterGE(int reg, int comparand,
                                          Label* if_ge) {
  masm_.branchPtr(Assembler::GreaterThanOrEqual, register_location(reg),
                  ImmWord(comparand), LabelOrBacktrack(if_ge));
}

void SMRegExpMacroAssembler::IfRegisterLT(int reg, int comparand,
                                          Label* if_lt) {
  masm_.branchPtr(Assembler::LessThan, register_location(reg),
                  ImmWord(comparand), LabelOrBacktrack(if_lt));
}

void SMRegExpMacroAssembler::IfRegisterEqPos(int reg, Label* if_eq) {
  masm_.branchPtr(Assembler::Equal, register_location(reg), current_position_,
                  LabelOrBacktrack(if_eq));
}

// Load the character (or characters) at the specified offset from the
// current position. Zero-extend to 32 bits.
void SMRegExpMacroAssembler::LoadCurrentCharacterUnchecked(int cp_offset,
                                                           int characters) {
  BaseIndex address(input_end_pointer_, current_position_, js::jit::TimesOne,
                    cp_offset * char_size());
  if (mode_ == LATIN1) {
    if (characters == 4) {
      masm_.load32(address, current_character_);
    } else if (characters == 2) {
      masm_.load16ZeroExtend(address, current_character_);
    } else {
      MOZ_ASSERT(characters == 1);
      masm_.load8ZeroExtend(address, current_character_);
    }
  } else {
    MOZ_ASSERT(mode_ == UC16);
    if (characters == 2) {
      masm_.load32(address, current_character_);
    } else {
      MOZ_ASSERT(characters == 1);
      masm_.load16ZeroExtend(address, current_character_);
    }
  }
}

void SMRegExpMacroAssembler::PopCurrentPosition() { Pop(current_position_); }

void SMRegExpMacroAssembler::PopRegister(int register_index) {
  Pop(temp0_);
  masm_.storePtr(temp0_, register_location(register_index));
}

void SMRegExpMacroAssembler::PushBacktrack(Label* label) {
  MOZ_ASSERT(!label->is_bound());
  MOZ_ASSERT(!label->patchOffset_.bound());
  label->patchOffset_ = masm_.movWithPatch(ImmPtr(nullptr), temp0_);
  MOZ_ASSERT(label->patchOffset_.bound());

  Push(temp0_);

  CheckBacktrackStackLimit();
}

void SMRegExpMacroAssembler::PushCurrentPosition() { Push(current_position_); }

void SMRegExpMacroAssembler::PushRegister(int register_index,
                                          StackCheckFlag check_stack_limit) {
  masm_.loadPtr(register_location(register_index), temp0_);
  Push(temp0_);
  if (check_stack_limit == StackCheckFlag::kCheckStackLimit) {
    CheckBacktrackStackLimit();
  }
}

void SMRegExpMacroAssembler::ReadCurrentPositionFromRegister(int reg) {
  masm_.loadPtr(register_location(reg), current_position_);
}

void SMRegExpMacroAssembler::WriteCurrentPositionToRegister(int reg,
                                                            int cp_offset) {
  if (cp_offset == 0) {
    masm_.storePtr(current_position_, register_location(reg));
  } else {
    Address addr(current_position_, cp_offset * char_size());
    masm_.computeEffectiveAddress(addr, temp0_);
    masm_.storePtr(temp0_, register_location(reg));
  }
}

// Note: The backtrack stack pointer is stored in a register as an
// offset from the stack top, not as a bare pointer, so that it is not
// corrupted if the backtrack stack grows (and therefore moves).
void SMRegExpMacroAssembler::ReadStackPointerFromRegister(int reg) {
  masm_.loadPtr(register_location(reg), backtrack_stack_pointer_);
  masm_.addPtr(backtrackStackBase(), backtrack_stack_pointer_);
}
void SMRegExpMacroAssembler::WriteStackPointerToRegister(int reg) {
  masm_.movePtr(backtrack_stack_pointer_, temp0_);
  masm_.subPtr(backtrackStackBase(), temp0_);
  masm_.storePtr(temp0_, register_location(reg));
}

// When matching a regexp that is anchored at the end, this operation
// is used to try skipping the beginning of long strings. If the
// maximum length of a match is less than the length of the string, we
// can skip the initial len - max_len bytes.
void SMRegExpMacroAssembler::SetCurrentPositionFromEnd(int by) {
  js::jit::Label after_position;
  masm_.branchPtr(Assembler::GreaterThanOrEqual, current_position_,
                  ImmWord(-by * char_size()), &after_position);
  masm_.movePtr(ImmWord(-by * char_size()), current_position_);

  // On RegExp code entry (where this operation is used), the character before
  // the current position is expected to be already loaded.
  // We have advanced the position, so it's safe to read backwards.
  LoadCurrentCharacterUnchecked(-1, 1);
  masm_.bind(&after_position);
}

void SMRegExpMacroAssembler::SetRegister(int register_index, int to) {
  MOZ_ASSERT(register_index >= num_capture_registers_);
  masm_.storePtr(ImmWord(to), register_location(register_index));
}

// Returns true if a regexp match can be restarted (aka the regexp is global).
// The return value is not used anywhere, but we implement it to be safe.
bool SMRegExpMacroAssembler::Succeed() {
  masm_.jump(&success_label_);
  return global();
}

// Capture registers are initialized to input[-1]
void SMRegExpMacroAssembler::ClearRegisters(int reg_from, int reg_to) {
  MOZ_ASSERT(reg_from <= reg_to);
  masm_.loadPtr(inputStart(), temp0_);
  masm_.subPtr(Imm32(char_size()), temp0_);
  for (int reg = reg_from; reg <= reg_to; reg++) {
    masm_.storePtr(temp0_, register_location(reg));
  }
}

void SMRegExpMacroAssembler::Push(Register source) {
  MOZ_ASSERT(source != backtrack_stack_pointer_);

#ifdef JS_CODEGEN_ARM64
  masm_.Str(js::jit::ARMRegister(source, 32),
            vixl::MemOperand(js::jit::ARMRegister(backtrack_stack_pointer_, 64),
                             -int32_t(sizeof(int32_t)), vixl::PreIndex));
#else
  masm_.subPtr(Imm32(sizeof(int32_t)), backtrack_stack_pointer_);
  masm_.store32(source, Address(backtrack_stack_pointer_, 0));
#endif
}

void SMRegExpMacroAssembler::Pop(Register target) {
  MOZ_ASSERT(target != backtrack_stack_pointer_);

#ifdef JS_CODEGEN_ARM64
  masm_.Ldrsw(
      js::jit::ARMRegister(target, 64),
      vixl::MemOperand(js::jit::ARMRegister(backtrack_stack_pointer_, 64),
                       int32_t(sizeof(int32_t)), vixl::PostIndex));
#else
  masm_.load32SignExtendToPtr(Address(backtrack_stack_pointer_, 0), target);
  masm_.addPtr(Imm32(sizeof(int32_t)), backtrack_stack_pointer_);
#endif
}

void SMRegExpMacroAssembler::JumpOrBacktrack(Label* to) {
  masm_.jump(to ? to->inner() : &backtrack_label_);
}

// Generate a quick inline test for backtrack stack overflow.
// If the test fails, call an OOL handler to try growing the stack.
void SMRegExpMacroAssembler::CheckBacktrackStackLimit() {
  js::jit::Label no_stack_overflow;
  if (backtrack_stack_limit_ != js::jit::InvalidReg) {
    masm_.branchPtr(Assembler::Below, backtrack_stack_limit_,
                    backtrack_stack_pointer_, &no_stack_overflow);
  } else {
    masm_.branchPtr(
        Assembler::Below,
        AbsoluteAddress(ExternalReference::RegexpStackLimit(isolate())),
        backtrack_stack_pointer_, &no_stack_overflow);
  }

  masm_.call(&stack_overflow_label_);

  // Exit with an exception if the call failed
  masm_.branchTest32(Assembler::Zero, temp0_, temp0_,
                     &exit_with_exception_label_);

  masm_.bind(&no_stack_overflow);
}

// This is used to sneak an OOM through the V8 layer.
static Handle<HeapObject> DummyCode() {
  return Handle<HeapObject>::fromHandleValue(JS::UndefinedHandleValue);
}

// Finalize code. This is called last, so that we know how many
// registers we need.
Handle<HeapObject> SMRegExpMacroAssembler::GetCode(Handle<RegExpData> data,
                                                   Flags flags) {
  if (!cx_->zone()->ensureJitZoneExists(cx_)) {
    return DummyCode();
  }

  masm_.bind(&entry_label_);

  createStackFrame();
  initFrameAndRegs();

  masm_.jump(&start_label_);

  backtrackHandler();
  stackOverflowHandler();
  successHandler();
  exitHandler();

  Linker linker(masm_);
  JitCode* code = linker.newCode(cx_, js::jit::CodeKind::RegExp);
  if (!code) {
    return DummyCode();
  }

  for (LabelPatch& lp : labelPatches_) {
    Assembler::PatchDataWithValueCheck(CodeLocationLabel(code, lp.patchOffset_),
                                       ImmPtr((void*)lp.labelOffset_),
                                       ImmPtr(nullptr));
  }

  for (js::jit::CodeOffset& offset : backtrackCodeOffsetPatches_) {
    Assembler::PatchDataWithValueCheck(CodeLocationLabel(code, offset),
                                       ImmPtr(code->raw()), ImmPtr(nullptr));
  }

  CollectPerfSpewerJitCodeProfile(code, "RegExp");

#ifdef MOZ_VTUNE
  js::vtune::MarkStub(code, "RegExp");
#endif

  return Handle<HeapObject>(JS::PrivateGCThingValue(code), isolate());
}

/*
 * The stack will have the following structure:
 *  sp-> - FrameData
 *         - inputStart
 *         - backtrack stack base
 *         - matches
 *         - numMatches
 *       - Registers
 *         - Capture positions
 *         - Scratch registers
 *       --- frame alignment ---
 *       - Saved register area
 *  fp-> - Frame pointer
 *       - Return address
 */
void SMRegExpMacroAssembler::createStackFrame() {
#ifdef JS_CODEGEN_ARM64
  // ARM64 communicates stack address via SP, but uses a pseudo-sp (PSP) for
  // addressing.  The register we use for PSP may however also be used by
  // calling code, and it is nonvolatile, so save it.  Do this as a special
  // case first because the generic save/restore code needs the PSP to be
  // initialized already.
  MOZ_ASSERT(js::jit::PseudoStackPointer64.Is(masm_.GetStackPointer64()));
  masm_.Str(js::jit::PseudoStackPointer64,
            vixl::MemOperand(js::jit::sp, -16, vixl::PreIndex));

  // Initialize the PSP from the SP.
  masm_.initPseudoStackPtr();
#endif

  masm_.Push(js::jit::FramePointer);
  masm_.moveStackPtrTo(js::jit::FramePointer);

  // Push non-volatile registers which might be modified by jitcode.
  for (GeneralRegisterForwardIterator iter(savedRegisters_); iter.more();
       ++iter) {
    masm_.Push(*iter);
  }

  // The pointer to InputOutputData is passed as the first argument.
  // On x86 we have to load it off the stack into temp0_.
  // On other platforms it is already in a register.
#ifdef JS_CODEGEN_X86
  Address ioDataAddr(js::jit::FramePointer, 2 * sizeof(void*));
  masm_.loadPtr(ioDataAddr, temp0_);
#else
  if (js::jit::IntArgReg0 != temp0_) {
    masm_.movePtr(js::jit::IntArgReg0, temp0_);
  }
#endif

  // Start a new stack frame.
  size_t frameBytes = sizeof(FrameData) + num_registers_ * sizeof(void*);
  frameSize_ = js::jit::StackDecrementForCall(js::jit::ABIStackAlignment,
                                              masm_.framePushed(), frameBytes);
  masm_.reserveStack(frameSize_);
  masm_.checkStackAlignment();

  // Check if we have space on the stack. Use the *NoInterrupt stack limit to
  // avoid failing repeatedly when the regex code is called from Ion JIT code.
  // (See bug 1208819)
  js::jit::Label stack_ok;
  AbsoluteAddress limit_addr(cx_->addressOfJitStackLimitNoInterrupt());
  masm_.branchStackPtrRhs(Assembler::Below, limit_addr, &stack_ok);

  masm_.loadJSContext(temp0_);
  masm_.store8(Imm32(1),
               Address(temp0_, JSContext::offsetOfHasDelayedOverRecursed()));

  // There is not enough space on the stack. Exit with an exception.
  // We haven't initialized our stack frame yet, so we must jump to
  // a special label that won't try to update the RegExp::Stack.
  masm_.movePtr(ImmWord(int32_t(js::RegExpRunStatus::Error)),
                js::jit::ReturnReg);
  masm_.jump(&exit_overrecursed_label_);

  masm_.bind(&stack_ok);
}

void SMRegExpMacroAssembler::initFrameAndRegs() {
  // At this point, an uninitialized stack frame has been created,
  // and the address of the InputOutputData is in temp0_.
  Register ioDataReg = temp0_;

  Register matchesReg = temp1_;
  masm_.loadPtr(Address(ioDataReg, InputOutputData::offsetOfMatches()),
                matchesReg);

  // Initialize output registers
  // Use |backtrack_stack_pointer_| as an additional temp register. This is safe
  // because we haven't yet written any data to |backtrack_stack_pointer_|.
  Register extraTemp = backtrack_stack_pointer_;

  masm_.loadPtr(Address(matchesReg, MatchPairs::offsetOfPairs()), extraTemp);
  masm_.storePtr(extraTemp, matches());
  masm_.load32(Address(matchesReg, MatchPairs::offsetOfPairCount()), extraTemp);
  masm_.store32(extraTemp, numMatches());

#ifdef DEBUG
  // Bounds-check numMatches. Note that callers that won't look at the captures
  // can always pass numMatches == 1.
  js::jit::Label enoughRegisters;
  masm_.branchPtr(Assembler::Equal, extraTemp, ImmWord(1), &enoughRegisters);
  masm_.branchPtr(Assembler::GreaterThanOrEqual, extraTemp,
                  ImmWord(num_capture_registers_ / 2), &enoughRegisters);
  masm_.assumeUnreachable("Not enough output pairs for RegExp");
  masm_.bind(&enoughRegisters);
#endif

  // Load input and store a copy in the FrameData.
  Register inputReg = temp1_;
  masm_.loadPtr(Address(ioDataReg, InputOutputData::offsetOfInput()), inputReg);
  masm_.storePtr(inputReg, inputString());

  // Load length and chars
  masm_.loadStringLength(inputReg, input_end_pointer_);
  masm_.loadStringChars(inputReg, current_position_, encoding());

  // Compute the input end pointer.
  BaseIndex endAddr(current_position_, input_end_pointer_, factor());
  masm_.computeEffectiveAddress(endAddr, input_end_pointer_);

  // Set up input position to be negative offset from string end.
  masm_.subPtr(input_end_pointer_, current_position_);

  // Store inputStart
  masm_.storePtr(current_position_, inputStart());

  // Load start index
  Register startIndexReg = temp1_;
  masm_.loadPtr(Address(ioDataReg, InputOutputData::offsetOfStartIndex()),
                startIndexReg);
  masm_.computeEffectiveAddress(
      BaseIndex(current_position_, startIndexReg, factor()), current_position_);

  // Store canResume in the FrameData
  masm_.load32(Address(ioDataReg, InputOutputData::offsetOfCanResume()),
               temp0_);
  masm_.store32(temp0_, canResume());

  // Initialize current_character_.
  // Load newline if index is at start, or previous character otherwise.
  js::jit::Label start_regexp;
  js::jit::Label load_previous_character;
  masm_.branchPtr(Assembler::NotEqual, startIndexReg, ImmWord(0),
                  &load_previous_character);
  masm_.movePtr(ImmWord('\n'), current_character_);
  masm_.jump(&start_regexp);

  masm_.bind(&load_previous_character);
  LoadCurrentCharacterUnchecked(-1, 1);
  masm_.bind(&start_regexp);

  // Initialize captured registers with inputStart - 1
  MOZ_ASSERT(num_capture_registers_ > 0);
  Register inputStartMinusOneReg = temp0_;
  masm_.loadPtr(inputStart(), inputStartMinusOneReg);
  masm_.subPtr(Imm32(char_size()), inputStartMinusOneReg);
  if (num_capture_registers_ > 8) {
    masm_.movePtr(ImmWord(register_offset(0)), temp1_);
    js::jit::Label init_loop;
    masm_.bind(&init_loop);
    masm_.storePtr(inputStartMinusOneReg, BaseIndex(masm_.getStackPointer(),
                                                    temp1_, js::jit::TimesOne));
    masm_.addPtr(ImmWord(sizeof(void*)), temp1_);
    masm_.branchPtr(Assembler::LessThanOrEqual, temp1_,
                    ImmWord(register_offset(num_capture_registers_ - 1)),
                    &init_loop);
  } else {
    // Unroll the loop
    for (int i = 0; i < num_capture_registers_; i++) {
      masm_.storePtr(inputStartMinusOneReg, register_location(i));
    }
  }

  // Initialize backtrack stack data.
  // Load the base of the stack and store it in the stack frame.
  masm_.loadPtr(AbsoluteAddress(ExternalReference::TopOfRegexpStack(isolate())),
                temp1_);
  masm_.storePtr(temp1_, backtrackStackBase());
  // Load the current top of the stack. This will usually be the same as the
  // base, but may be different if we are executing one regexp while another is
  // interrupted. Also load the stack limit into a register, if we have enough.
  LoadBacktrackStackRegsFromMemory();
  // Compute the difference between the two, and store it in the stack frame.
  // This is usually 0.
  masm_.subPtr(backtrack_stack_pointer_, temp1_);
  masm_.storePtr(temp1_, initialBacktrackStackPointer());
}

// Called when we find a match. May not be generated if we can
// determine ahead of time that a regexp cannot match: for example,
// when compiling /\u1e9e/ for latin-1 inputs.
void SMRegExpMacroAssembler::successHandler() {
  if (!success_label_.used()) {
    return;
  }
  masm_.bind(&success_label_);

  // Copy captures to the MatchPairs pointed to by the InputOutputData.
  // Captures are stored as positions, which are negative byte offsets
  // from the end of the string.  We must convert them to actual
  // indices.
  //
  // Index:        [ 0 ][ 1 ][ 2 ][ 3 ][ 4 ][ 5 ][END]
  // Pos (1-byte): [-6 ][-5 ][-4 ][-3 ][-2 ][-1 ][ 0 ] // IS = -6
  // Pos (2-byte): [-12][-10][-8 ][-6 ][-4 ][-2 ][ 0 ] // IS = -12
  //
  // To convert a position to an index, we subtract InputStart, and
  // divide the result by char_size.
  Register matchesReg = temp1_;
  masm_.loadPtr(matches(), matchesReg);

  // Use |backtrack_stack_pointer_| as an additional temp register. This is safe
  // because we don't read from |backtrack_stack_pointer_| after this point.
  Register extraTemp = backtrack_stack_pointer_;

  Register inputStartReg = extraTemp;
  masm_.loadPtr(inputStart(), inputStartReg);

  auto copyRegister = [&](int reg) {
    masm_.loadPtr(register_location(reg), temp0_);
    masm_.subPtr(inputStartReg, temp0_);
    if (mode_ == UC16) {
      masm_.rshiftPtrArithmetic(Imm32(1), temp0_);
    }
    masm_.store32(temp0_, Address(matchesReg, reg * sizeof(int32_t)));
  };

  // Copy first match pair.
  MOZ_ASSERT(num_capture_registers_ >= 2);
  copyRegister(0);
  copyRegister(1);

  if (num_capture_registers_ > 2) {
    // We always need the first match pair to update the `lastIndex` slot,
    // but we can skip copying the capture groups if we won't look at them.
    // This also allows our caller to avoid allocating space for unused results.
    js::jit::Label earlyExitForTest;
    masm_.branch32(Assembler::Equal, numMatches(), Imm32(1), &earlyExitForTest);

    for (int i = 2; i < num_capture_registers_; i++) {
      copyRegister(i);
    }

    masm_.bind(&earlyExitForTest);
  }

  masm_.movePtr(ImmWord(int32_t(js::RegExpRunStatus::Success)), temp0_);
  // This falls through to the exit handler.
}

void SMRegExpMacroAssembler::exitHandler() {
  masm_.bind(&exit_label_);

  if (temp0_ != js::jit::ReturnReg) {
    masm_.movePtr(temp0_, js::jit::ReturnReg);
  }

  // Restore the RegExpStack's stack pointer in case we were interrupted
  // and modified it.
  masm_.loadPtr(backtrackStackBase(), backtrack_stack_pointer_);
  masm_.subPtr(initialBacktrackStackPointer(), backtrack_stack_pointer_);
  masm_.storePtr(
      backtrack_stack_pointer_,
      AbsoluteAddress(ExternalReference::RegexpStackPointer(isolate())));

  masm_.bind(&exit_overrecursed_label_);

  masm_.freeStack(frameSize_);

  // Restore registers which were saved on entry
  for (GeneralRegisterBackwardIterator iter(savedRegisters_); iter.more();
       ++iter) {
    masm_.Pop(*iter);
  }

  masm_.Pop(js::jit::FramePointer);

#ifdef JS_CODEGEN_ARM64
  // Now restore the value that was in the PSP register on entry, and return.

  // Obtain the correct SP from the PSP.
  masm_.Mov(js::jit::sp, js::jit::PseudoStackPointer64);

  // Restore the saved value of the PSP register, this value is whatever the
  // caller had saved in it, not any actual SP value, and it must not be
  // overwritten subsequently.
  masm_.Ldr(js::jit::PseudoStackPointer64,
            vixl::MemOperand(js::jit::sp, 16, vixl::PostIndex));

  // Perform a plain Ret(), as abiret() will move SP <- PSP and that is wrong.
  masm_.Ret(vixl::lr);
#else
  masm_.abiret();
#endif

  if (exit_with_exception_label_.used()) {
    masm_.bind(&exit_with_exception_label_);

    // Exit with an error result to signal thrown exception
    masm_.movePtr(ImmWord(int32_t(js::RegExpRunStatus::Error)), temp0_);
    masm_.jump(&exit_label_);
  }
}

void SMRegExpMacroAssembler::backtrackHandler() {
  if (!backtrack_label_.used()) {
    return;
  }
  masm_.bind(&backtrack_label_);
  js::jit::Label interrupt, backtrack;
#ifdef DEBUG
  // Check for simulating interrupt
  masm_.branch32(Assembler::NotEqual,
                 AbsoluteAddress(&cx_->isolate->shouldSimulateInterrupt_),
                 Imm32(0), &interrupt);
#endif
  // Check for an interrupt. We may have to restart from the beginning if we
  // are interrupted, so we only check for urgent interrupts.
  masm_.branchTest32(
      Assembler::NonZero, AbsoluteAddress(cx_->addressOfInterruptBits()),
      Imm32(uint32_t(js::InterruptReason::CallbackUrgent)), &interrupt);

  // Pop code offset from backtrack stack, add to code base address, and jump to
  // location.
  masm_.bind(&backtrack);
  Pop(temp0_);
  PushBacktrackCodeOffsetPatch(masm_.movWithPatch(ImmPtr(nullptr), temp1_));
  masm_.addPtr(temp1_, temp0_);
  masm_.jump(temp0_);

  // We are being interrupted. First, check if we support resumption,
  // and return an error if we don't.
  masm_.bind(&interrupt);
  masm_.branch32(Assembler::Equal, canResume(), Imm32(0),
                 &exit_with_exception_label_);

  StoreBacktrackStackToMemory();

  // Load FrameData* into temp1.
  masm_.moveStackPtrTo(temp1_);

  // Save registers before calling C function
  LiveGeneralRegisterSet volatileRegs(GeneralRegisterSet::Volatile());
  volatileRegs.takeUnchecked(temp0_);
  volatileRegs.takeUnchecked(temp1_);
  volatileRegs.takeUnchecked(backtrack_stack_pointer_);
  masm_.PushRegsInMask(volatileRegs);

  using Fn = bool (*)(JSContext*, FrameData*);
  masm_.setupUnalignedABICall(temp0_);
  masm_.loadJSContext(temp0_);
  masm_.passABIArg(temp0_);
  masm_.passABIArg(temp1_);
  masm_.callWithABI<Fn, ::js::irregexp::HandleRegExpInterrupt>(
      ABIType::General, CheckUnsafeCallWithABI::DontCheckOther);
  masm_.storeCallBoolResult(temp0_);

  masm_.PopRegsInMask(volatileRegs);

  // If the call threw an exception, return it.  We update the
  // backtrack stack first so that it's correct when we read it in the
  // epilogue.
  LoadBacktrackStackFromMemory(backtrackStackBase());
  masm_.branchTest32(Assembler::Zero, temp0_, temp0_,
                     &exit_with_exception_label_);

  // Reload input_end_pointer_ in case the string moved.
  masm_.loadPtr(inputString(), temp0_);
  masm_.loadStringLength(temp0_, input_end_pointer_);
  masm_.loadStringChars(temp0_, temp1_, encoding());
  masm_.computeEffectiveAddress(BaseIndex(temp1_, input_end_pointer_, factor()),
                                input_end_pointer_);

  // Now jump back up to the actual backtrack code.
  masm_.jump(&backtrack);
}

void SMRegExpMacroAssembler::stackOverflowHandler() {
  if (!stack_overflow_label_.used()) {
    return;
  }

  js::jit::AutoCreatedBy acb(masm_,
                             "SMRegExpMacroAssembler::stackOverflowHandler");

  // Called if the backtrack-stack limit has been hit.
  masm_.bind(&stack_overflow_label_);

  // Load argument
  masm_.movePtr(ImmPtr(isolate()->regexp_stack()), temp1_);

  // Save registers before calling C function
  LiveGeneralRegisterSet volatileRegs(GeneralRegisterSet::Volatile());

#ifdef JS_USE_LINK_REGISTER
  masm_.pushReturnAddress();
#endif

  StoreBacktrackStackToMemory();

  volatileRegs.takeUnchecked(temp0_);
  volatileRegs.takeUnchecked(temp1_);
  masm_.PushRegsInMask(volatileRegs);

  using Fn = bool (*)(Stack* regexp_stack);
  masm_.setupUnalignedABICall(temp0_);
  masm_.passABIArg(temp1_);
  masm_.callWithABI<Fn, ::js::irregexp::GrowBacktrackStack>();
  masm_.storeCallBoolResult(temp0_);

  masm_.PopRegsInMask(volatileRegs);

  // If GrowBacktrackStack returned false, we have failed to grow the
  // stack, and must exit with a stack-overflow exception. Do this in
  // the caller so that the stack is adjusted by our return instruction.
  js::jit::Label overflow_return;
  masm_.branchTest32(Assembler::Zero, temp0_, temp0_, &overflow_return);

  // Adjust for the return address on the stack.
  size_t frameOffset = sizeof(void*);
  Address bsbAddress(masm_.getStackPointer(),
                     offsetof(FrameData, backtrackStackBase) + frameOffset);
  LoadBacktrackStackFromMemory(bsbAddress);

  // Resume execution in calling code.
  masm_.bind(&overflow_return);
  masm_.ret();
}

void SMRegExpMacroAssembler::StoreBacktrackStackToMemory() {
  masm_.storePtr(
      backtrack_stack_pointer_,
      AbsoluteAddress(ExternalReference::RegexpStackPointer(isolate())));
}

void SMRegExpMacroAssembler::LoadBacktrackStackFromMemory(
    Address backtrackStackBaseAddr) {
  masm_.loadPtr(AbsoluteAddress(ExternalReference::TopOfRegexpStack(isolate())),
                backtrack_stack_pointer_);
  masm_.storePtr(backtrack_stack_pointer_, backtrackStackBaseAddr);
  LoadBacktrackStackRegsFromMemory();
}

void SMRegExpMacroAssembler::LoadBacktrackStackRegsFromMemory() {
  masm_.loadPtr(
      AbsoluteAddress(ExternalReference::RegexpStackPointer(isolate())),
      backtrack_stack_pointer_);
  if (backtrack_stack_limit_ != js::jit::InvalidReg) {
    masm_.loadPtr(
        AbsoluteAddress(ExternalReference::RegexpStackLimit(isolate())),
        backtrack_stack_limit_);
  }
}

// This is only used by tracing code.
// The return value doesn't matter.
RegExpMacroAssembler::IrregexpImplementation
SMRegExpMacroAssembler::Implementation() {
  return kBytecodeImplementation;
}

// Compare two strings in `/i` mode (ignoreCase, but not unicode).
/*static */
uint32_t SMRegExpMacroAssembler::CaseInsensitiveCompareNonUnicode(
    const char16_t* substring1, const char16_t* substring2, size_t byteLength) {
  js::AutoUnsafeCallWithABI unsafe;

  MOZ_ASSERT(byteLength % sizeof(char16_t) == 0);
  // V8's implementation only needs the isolate for the non-ICU implementation.
#ifdef JS_HAS_INTL_API
  Isolate* isolate = nullptr;
#else
  Isolate* isolate = js::TlsContext.get()->isolate;
#endif
  v8::Address lhs = reinterpret_cast<uintptr_t>(substring1);
  v8::Address rhs = reinterpret_cast<uintptr_t>(substring2);
  return RegExpMacroAssembler::CaseInsensitiveCompareNonUnicode(
      lhs, rhs, byteLength, isolate);
}

// Compare two strings in `/iu` mode (ignoreCase and unicode).
/*static */
uint32_t SMRegExpMacroAssembler::CaseInsensitiveCompareUnicode(
    const char16_t* substring1, const char16_t* substring2, size_t byteLength) {
  js::AutoUnsafeCallWithABI unsafe;

  MOZ_ASSERT(byteLength % sizeof(char16_t) == 0);
  // V8's implementation only needs the isolate for the non-ICU implementation.
#ifdef JS_HAS_INTL_API
  Isolate* isolate = nullptr;
#else
  Isolate* isolate = js::TlsContext.get()->isolate;
#endif
  v8::Address lhs = reinterpret_cast<uintptr_t>(substring1);
  v8::Address rhs = reinterpret_cast<uintptr_t>(substring2);
  return RegExpMacroAssembler::CaseInsensitiveCompareUnicode(
      lhs, rhs, byteLength, isolate);
}

/* static */
bool SMRegExpMacroAssembler::GrowBacktrackStack(Stack* regexp_stack) {
  js::AutoUnsafeCallWithABI unsafe;
  size_t size = regexp_stack->memory_size();
  bool result = !!regexp_stack->EnsureCapacity(size * 2);
  if (!result) {
    js::TlsContext.get()->noteDelayedOverRecursed();
  }
  return result;
}

bool SMRegExpMacroAssembler::CanReadUnaligned() const {
#if defined(JS_CODEGEN_ARM)
  return !js::jit::ARMFlags::HasAlignmentFault();
#elif defined(JS_CODEGEN_MIPS64)
  return false;
#else
  return true;
#endif
}

}  // namespace regexp
}  // namespace internal
}  // namespace v8

namespace js {
namespace irregexp {

bool HandleRegExpInterrupt(JSContext* cx,
                           v8::internal::regexp::FrameData* frameData) {
  // Poorly written RegExps may require exponential backtracking to match. If
  // the time taken to execute a RegExp exceeds the time between watchdog
  // interrupts in the browser (which are used to implement the slow script
  // dialog), then the only way for the execution to terminate successfully is
  // if we can resume jitcode after being interrupted. This code is carefully
  // designed to make that possible.
  //
  // The main challenge is GC: our RegExp jitcode, and the RegExp stubs that
  // call it, are all written with the assumption that we won't trigger a GC.
  // To keep the RegExp stubs simple and efficient, we don't support interrupts
  // when invoked from jitcode. (This is determined based on the `canResume`
  // flag in the InputOutputData / FrameData.) Instead, if an interrupt is
  // triggered while a regexp called from jitcode is executing, we exit with an
  // error and resume in C++. The C++ handler will restart execution with the
  // `canResume` flag enabled, preventing future restarts. It is simple to
  // support GC in the C++ caller.
  //
  // The remaining GC-related problem is the regexp code itself. Fortunately,
  // compiled regexps only touch two GC things: the input string, and the
  // JitCode itself. AutoInterruptingRegExp prevents RegExp code from being
  // discarded in Zone::discardJitCode. We read the string from the FrameData
  // here, root it, and update the frame before returning. The interrupt
  // handling code will reload fresh pointers to the string's characters
  // before resuming.
  //
  // In addition to the above, we also have to ensure that our RegExps support
  // reentrancy. In particular, the backtracking stack is shared across
  // executions. See `[SMDOC] RegExp backtrack stack` for more details.
  //
  // Note that unlike other cases where we invoke jitcode, this approach
  // does *not* create a JitActivation. JitFrameIter will not see the
  // regexp frame.
  MOZ_RELEASE_ASSERT(frameData->canResume);

  // Prevent discarding RegExp jitcode.
  js::jit::AutoInterruptingRegExp interrupting(cx->zone()->jitZone());

  // Root the string input for the regexp
  RootedString inputString(cx, frameData->inputString);

#ifdef DEBUG
  // If this is a simulated interrupt, trigger a real one.
  if (IsolateShouldSimulateInterrupt(cx->isolate)) {
    IsolateClearShouldSimulateInterrupt(cx->isolate);
    cx->requestInterrupt(InterruptReason::CallbackUrgent);
  }
#endif

  // Handle the interrupt.
  bool result = cx->handleInterrupt();

  // Copy the string back to the frame data in case it was moved by GC.
  frameData->inputString = inputString;

  return result;
}

}  // namespace irregexp
}  // namespace js
