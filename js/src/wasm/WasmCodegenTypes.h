/*
 * Copyright 2021 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef wasm_codegen_types_h
#define wasm_codegen_types_h

#include "mozilla/CheckedInt.h"
#include "mozilla/EnumeratedArray.h"
#include "mozilla/Maybe.h"
#include "mozilla/Span.h"

#include <stdint.h>

#include "jit/IonTypes.h"
#include "jit/PerfSpewer.h"
#include "threading/ExclusiveData.h"
#include "wasm/WasmBuiltins.h"
#include "wasm/WasmCodegenConstants.h"
#include "wasm/WasmConstants.h"
#include "wasm/WasmInstanceData.h"
#include "wasm/WasmSerialize.h"
#include "wasm/WasmShareable.h"
#include "wasm/WasmTypeDef.h"
#include "wasm/WasmUtility.h"

namespace js {

namespace jit {
template <class VecT>
class ABIArgIter;
}  // namespace jit

namespace wasm {

struct CodeMetadata;
struct StackMap;
struct TableDesc;
struct V128;

// ArgTypeVector type.
//
// Functions usually receive one ABI argument per WebAssembly argument.  However
// if a function has multiple results and some of those results go to the stack,
// then it additionally receives a synthetic ABI argument holding a pointer to
// the stack result area.
//
// Given the presence of synthetic arguments, sometimes we need a name for
// non-synthetic arguments.  We call those "natural" arguments.

enum class StackResults { HasStackResults, NoStackResults };

class ArgTypeVector {
  const ValTypeVector& args_;
  bool hasStackResults_;

  // To allow ABIArgIter<VecT, jit::ABIKind>, we define a private
  // length() method.  To prevent accidental errors, other users need to be
  // explicit and call lengthWithStackResults() or
  // lengthWithoutStackResults().
  size_t length() const { return args_.length() + size_t(hasStackResults_); }
  template <class VecT>
  friend class jit::ABIArgIter;

 public:
  ArgTypeVector(const ValTypeVector& args, StackResults stackResults)
      : args_(args),
        hasStackResults_(stackResults == StackResults::HasStackResults) {}
  explicit ArgTypeVector(const FuncType& funcType);

  bool hasSyntheticStackResultPointerArg() const { return hasStackResults_; }
  StackResults stackResults() const {
    return hasSyntheticStackResultPointerArg() ? StackResults::HasStackResults
                                               : StackResults::NoStackResults;
  }
  size_t lengthWithoutStackResults() const { return args_.length(); }
  bool isSyntheticStackResultPointerArg(size_t idx) const {
    // The pointer to stack results area, if present, is a synthetic argument
    // tacked on at the end.
    MOZ_ASSERT(idx < lengthWithStackResults());
    return idx == args_.length();
  }
  bool isNaturalArg(size_t idx) const {
    return !isSyntheticStackResultPointerArg(idx);
  }
  size_t naturalIndex(size_t idx) const {
    MOZ_ASSERT(isNaturalArg(idx));
    // Because the synthetic argument, if present, is tacked on the end, an
    // argument index that isn't synthetic is natural.
    return idx;
  }

  size_t lengthWithStackResults() const { return length(); }
  jit::MIRType operator[](size_t i) const {
    MOZ_ASSERT(i < lengthWithStackResults());
    if (isSyntheticStackResultPointerArg(i)) {
      return jit::MIRType::StackResults;
    }
    return args_[naturalIndex(i)].toMIRType();
  }
};

// A wrapper around the bytecode offset of a wasm instruction within a whole
// module, used for trap offsets or call offsets. These offsets should refer to
// the first byte of the instruction that triggered the trap / did the call and
// should ultimately derive from OpIter::bytecodeOffset.

class BytecodeOffset {
  static const uint32_t INVALID = UINT32_MAX;
  static_assert(INVALID > wasm::MaxModuleBytes);
  uint32_t offset_;

  WASM_CHECK_CACHEABLE_POD(offset_);

 public:
  BytecodeOffset() : offset_(INVALID) {}
  explicit BytecodeOffset(uint32_t offset) : offset_(offset) {}

  bool isValid() const { return offset_ != INVALID; }
  uint32_t offset() const {
    MOZ_ASSERT(isValid());
    return offset_;
  }
};

WASM_DECLARE_CACHEABLE_POD(BytecodeOffset);
using BytecodeOffsetVector =
    mozilla::Vector<BytecodeOffset, 4, SystemAllocPolicy>;
using BytecodeOffsetSpan = mozilla::Span<const BytecodeOffset>;
using ShareableBytecodeOffsetVector =
    ShareableVector<BytecodeOffset, 4, SystemAllocPolicy>;
using SharedBytecodeOffsetVector = RefPtr<const ShareableBytecodeOffsetVector>;
using MutableBytecodeOffsetVector = RefPtr<ShareableBytecodeOffsetVector>;

// A TrapMachineInsn describes roughly what kind of machine instruction has
// caused a trap.  This is used only for validation of trap placement in debug
// builds, in ModuleGenerator::finishMetadataTier, and is not necessary for
// execution of wasm code.
enum class TrapMachineInsn {
  // The "official" undefined insn for the target, or something equivalent
  // that we use for that purpose.  The key property is that it always raises
  // SIGILL when executed.  For example, UD2 on Intel.
  OfficialUD,
  // Loads and stores that move 8, 16, 32, 64 or 128 bits of data, regardless
  // of their type and how they are subsequently used (widened or duplicated).
  Load8,
  Load16,
  Load32,
  Load64,
  Load128,
  Store8,
  Store16,
  Store32,
  Store64,
  Store128,
  // Any kind of atomic r-m-w or CAS memory transaction, but not including
  // Load-Linked or Store-Checked style insns -- those count as plain LoadX
  // and StoreX.
  Atomic,
  INVALID
};
using TrapMachineInsnVector =
    mozilla::Vector<TrapMachineInsn, 0, SystemAllocPolicy>;

static inline TrapMachineInsn TrapMachineInsnForLoad(int byteSize) {
  switch (byteSize) {
    case 1:
      return TrapMachineInsn::Load8;
    case 2:
      return TrapMachineInsn::Load16;
    case 4:
      return TrapMachineInsn::Load32;
    case 8:
      return TrapMachineInsn::Load64;
    case 16:
      return TrapMachineInsn::Load128;
    default:
      MOZ_CRASH("TrapMachineInsnForLoad");
  }
}
static inline TrapMachineInsn TrapMachineInsnForLoadWord() {
  return TrapMachineInsnForLoad(sizeof(void*));
}

static inline TrapMachineInsn TrapMachineInsnForStore(int byteSize) {
  switch (byteSize) {
    case 1:
      return TrapMachineInsn::Store8;
    case 2:
      return TrapMachineInsn::Store16;
    case 4:
      return TrapMachineInsn::Store32;
    case 8:
      return TrapMachineInsn::Store64;
    case 16:
      return TrapMachineInsn::Store128;
    default:
      MOZ_CRASH("TrapMachineInsnForStore");
  }
}
static inline TrapMachineInsn TrapMachineInsnForStoreWord() {
  return TrapMachineInsnForStore(sizeof(void*));
}
#ifdef DEBUG
const char* ToString(Trap trap);
const char* ToString(TrapMachineInsn tmi);
#endif

// FaultingCodeRange records the assembler buffer offsets associated with a
// faulting instruction.  It is used for the construction of TrapSites and
// associated StackMaps.  It carries both the starting offset and the starting
// offset of the next instruction; hence, implicitly, the length of this
// instruction.
//
// For targets with fixed length instructions (everything except x86 and x64),
// the constructor takes only a single offset value, for the start of the
// instruction, since it would be pointless to (implicitly) specify the length
// at every call.
//
// For targets with variable length instructions (x86, x64) the constructor
// takes offsets both for the start and the end (+1) of the instruction.  This
// forces callers to (implicitly) specify length info at every call; it is
// literally impossible to create a FaultingCodeRange without that.
//
// The representation is a bit unusual because the most important requirement is
// that constructors `FaultingCodeRange(uint32_t, uint32_t)` and
// `FaultingCodeRange(uint32_t)` are simple enough that they can be optimized
// away in the case where the assembler is used to create an instruction but the
// resulting FaultingCodeRange is ignored.  Hence the burden of
// error/consistency checking is placed on the (infrequently used) access
// routines rather than the constructors.
//
// In typical use, there are two reasons why FaultingCodeRange may fail
// `isValid()`:
//
// * The generating assembler OOMd.  In this case, routines handling
//   FaultingCodeRanges will ignore the ranges, and at the end of compilation
//   the assembler's OOM flag is checked and as a result all code and metadata
//   resulting from the compilation is discarded.
//
// * or, the FaultingCodeRange was deliberately created as invalid, using
//   `FaultingCodeRange()`.  This can be used to signify to callers that "no
//   trap required here / no stackmap required here" and is detected and handled
//   accordingly.

class FaultingCodeRange {
  // Definitions that summarise the range of instruction lengths in the code we
  // generate.  This will need to be updated if we start creating
  // variable-length insns for one of the architectures currently defined here
  // as fixed-length.
 private:
#if defined(JS_CODEGEN_X86) || defined(JS_CODEGEN_X64)
  // These are variable-length-insn targets.
  static constexpr uint32_t kMinInsnLength = 1;
  static constexpr uint32_t kMaxInsnLength = 15;
#elif defined(JS_CODEGEN_ARM) || defined(JS_CODEGEN_ARM64) ||    \
    defined(JS_CODEGEN_RISCV64) || defined(JS_CODEGEN_MIPS64) || \
    defined(JS_CODEGEN_LOONG64)
  // These are fixed-length-insn targets, at least for the code we generate.  In
  // particular, ARM-Thumb2 has both insn lengths 2 and 4, but we don't generate
  // that.  Same for the RiscV compressed-instruction extension.
  static constexpr uint32_t kMinInsnLength = 4;
  static constexpr uint32_t kMaxInsnLength = 4;
#elif defined(JS_CODEGEN_NONE)
  // Bogus, but it doesn't matter.
  static constexpr uint32_t kMinInsnLength = 1;
  static constexpr uint32_t kMaxInsnLength = 1;
#else
#  error "Unknown architecture"
#endif
  static_assert(kMinInsnLength >= 1);
  static_assert(kMinInsnLength <= kMaxInsnLength);

 public:
  static constexpr uint32_t minInsnLength() { return kMinInsnLength; }
  static constexpr uint32_t maxInsnLength() { return kMaxInsnLength; }

  // The representation.  We store the start and end assembler offsets, with the
  // end offset not being part of the range.
 private:
  static constexpr uint32_t INVALID_OFFSET = UINT32_MAX;
  uint32_t startOffset_ = INVALID_OFFSET;
  uint32_t endOffset_ = INVALID_OFFSET;

 public:
  // Invariants
  bool isValid() const {
    return
        // Both offsets must be valid
        startOffset_ != INVALID_OFFSET && endOffset_ != INVALID_OFFSET &&
        // The length must be at least 1 ..
        startOffset_ < endOffset_ &&
        // .. and within the limits defined above
        endOffset_ - startOffset_ >= kMinInsnLength &&
        endOffset_ - startOffset_ <= kMaxInsnLength;
  }

  // Constructors.
  inline FaultingCodeRange() { MOZ_ASSERT(!isValid()); }

#if defined(JS_CODEGEN_X86) || defined(JS_CODEGEN_X64)
  // Make a FaultingCodeRange from two offsets.  This is only valid on targets
  // with variable-length instructions.
  inline FaultingCodeRange(uint32_t startOffset, uint32_t endOffset)
      : startOffset_(startOffset), endOffset_(endOffset) {
    static_assert(kMinInsnLength < kMaxInsnLength);
    // Requirements on the caller
    MOZ_ASSERT(startOffset != INVALID_OFFSET && endOffset != INVALID_OFFSET);
    // If the assembler OOMd, we may have endOffset <= startOffset, but we
    // tolerate that and later throw out any attempt to query the resulting
    // FaultingCodeRange.
  }
#else
  // Make a FaultingCodeRange from one offset.  This is only valid on targets
  // with fixed-length instructions.
  inline explicit FaultingCodeRange(uint32_t startOffset)
      : startOffset_(startOffset), endOffset_(startOffset + kMinInsnLength) {
    static_assert(kMinInsnLength == kMaxInsnLength);
    // Requirement on the caller
    MOZ_ASSERT(startOffset != INVALID_OFFSET);
  }
#endif

  // Accessors
  uint32_t offset() const {
    MOZ_ASSERT(isValid());
    return startOffset_;
  }
  uint32_t length() const {
    MOZ_ASSERT(isValid());
    return endOffset_ - startOffset_;
  }
  uint32_t resumeOffset() const {
    MOZ_ASSERT(isValid());
    return endOffset_;
  }

  // Be careful with these.  If the creating MacroAssembler OOM'd, they will
  // likely fail `isValid()`.
  uint32_t offsetUnchecked() const { return startOffset_; }
  uint32_t resumeOffsetUnchecked() const { return endOffset_; }

  // Shim to ease migration from FaultingCodeOffset.  This should be removed
  // eventually.
  uint32_t get() const { return offset(); }
};
static_assert(sizeof(FaultingCodeRange) == 8);

// And this holds two such ranges.  Needed for 64-bit integer transactions on
// 32-bit targets.
using FaultingCodeRangePair = std::pair<FaultingCodeRange, FaultingCodeRange>;
static_assert(sizeof(FaultingCodeRangePair) == 16);

// The bytecode offsets of all the callers of a function that has been inlined.
// See CallSiteDesc/TrapSiteDesc for uses of this.
using InlinedCallerOffsets = BytecodeOffsetVector;

// An index into InliningContext to get an InlinedCallerOffsets. This may be
// 'None' to indicate an empty InlinedCallerOffsets.
struct InlinedCallerOffsetIndex {
 private:
  // Sentinel value for an empty InlinedCallerOffsets.
  static constexpr uint32_t NONE = UINT32_MAX;

  uint32_t value_;

 public:
  // The maximum value allowed here, checked by assertions. InliningContext
  // will OOM if this value is exceeded.
  static constexpr uint32_t MAX = UINT32_MAX - 1;

  // Construct 'none'.
  InlinedCallerOffsetIndex() : value_(NONE) {}

  // Construct a non-'none' value. The value must be less than or equal to MAX.
  explicit InlinedCallerOffsetIndex(uint32_t index) : value_(index) {
    MOZ_RELEASE_ASSERT(index <= MAX);
  }

  // The value of this index, if it is not nothing.
  uint32_t value() const {
    MOZ_RELEASE_ASSERT(!isNone());
    return value_;
  }

  // Whether this value is none or not.
  bool isNone() const { return value_ == NONE; }
};
static_assert(sizeof(InlinedCallerOffsetIndex) == sizeof(uint32_t));

// A hash map from some index (either call site or trap site) to
// InlinedCallerOffsetIndex.
using InlinedCallerOffsetsIndexHashMap =
    mozilla::HashMap<uint32_t, InlinedCallerOffsetIndex,
                     mozilla::DefaultHasher<uint32_t>, SystemAllocPolicy>;

// A collection of InlinedCallerOffsets for a code block.
class InliningContext {
  using Storage = mozilla::Vector<InlinedCallerOffsets, 0, SystemAllocPolicy>;
  Storage storage_;
  bool mutable_ = true;

 public:
  InliningContext() = default;

  bool empty() const { return storage_.empty(); }
  uint32_t length() const { return storage_.length(); }

  void setImmutable() {
    MOZ_RELEASE_ASSERT(mutable_);
    mutable_ = false;
  }

  const InlinedCallerOffsets* operator[](InlinedCallerOffsetIndex index) const {
    // Don't give out interior pointers into the vector until we've
    // transitioned to immutable.
    MOZ_RELEASE_ASSERT(!mutable_);
    // Index must be in bounds.
    MOZ_RELEASE_ASSERT(index.value() < length());
    return &storage_[index.value()];
  }

  [[nodiscard]] bool append(InlinedCallerOffsets&& inlinedCallerOffsets,
                            InlinedCallerOffsetIndex* index) {
    MOZ_RELEASE_ASSERT(mutable_);

    // Skip adding an entry if the offset vector is empty and just return an
    // 'none' index.
    if (inlinedCallerOffsets.empty()) {
      *index = InlinedCallerOffsetIndex();
      return true;
    }

    // OOM if we'll be growing beyond the maximum index allowed, or if we
    // fail to append.
    if (storage_.length() == InlinedCallerOffsetIndex::MAX ||
        !storage_.append(std::move(inlinedCallerOffsets))) {
      return false;
    }
    *index = InlinedCallerOffsetIndex(storage_.length() - 1);
    return true;
  }

  [[nodiscard]] bool appendAll(InliningContext&& other) {
    MOZ_RELEASE_ASSERT(mutable_);
    if (!storage_.appendAll(std::move(other.storage_))) {
      return false;
    }

    // OOM if we just grew beyond the maximum index allowed.
    return storage_.length() <= InlinedCallerOffsetIndex::MAX;
  }

  void swap(InliningContext& other) {
    MOZ_RELEASE_ASSERT(mutable_);
    storage_.swap(other.storage_);
  }

  void shrinkStorageToFit() { storage_.shrinkStorageToFit(); }

  void clear() {
    MOZ_RELEASE_ASSERT(mutable_);
    storage_.clear();
  }

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const {
    return storage_.sizeOfExcludingThis(mallocSizeOf);
  }
};

// The fields of a TrapSite that do not depend on code generation.

struct TrapSiteDesc {
  explicit TrapSiteDesc(BytecodeOffset bytecodeOffset,
                        InlinedCallerOffsetIndex inlinedCallerOffsetsIndex =
                            InlinedCallerOffsetIndex())
      : bytecodeOffset(bytecodeOffset),
        inlinedCallerOffsetsIndex(inlinedCallerOffsetsIndex) {}
  TrapSiteDesc() : TrapSiteDesc(BytecodeOffset(0)) {};

  bool isValid() const { return bytecodeOffset.isValid(); }

  BytecodeOffset bytecodeOffset;
  InlinedCallerOffsetIndex inlinedCallerOffsetsIndex;
};

using MaybeTrapSiteDesc = mozilla::Maybe<TrapSiteDesc>;

// A TrapSite represents a wasm instruction at a given bytecode offset that
// can fault at the given pc offset.  When such a fault occurs, a signal/
// exception handler looks up the TrapSite to confirm the fault is intended/
// safe and redirects pc to the trap stub.

struct TrapSite : TrapSiteDesc {
  // If this trap site is in a function that was inlined, these are the call
  // site bytecode offsets of the caller functions that this trap site was
  // inlined into. The direct ancestor of this function (i.e. the one
  // directly above it on the stack) is the last entry in the vector.
  const InlinedCallerOffsets* inlinedCallerOffsets = nullptr;

  BytecodeOffsetSpan inlinedCallerOffsetsSpan() const {
    if (!inlinedCallerOffsets) {
      return BytecodeOffsetSpan();
    }
    return BytecodeOffsetSpan(inlinedCallerOffsets->begin(),
                              inlinedCallerOffsets->end());
  }
};

// A collection of TrapSite for a specific trap kind that is optimized for
// compact storage.
//
// The individual fields are split to be in their own vectors to minimize
// overhead due to alignment for small fields like TrapMachineInsn.
class TrapSitesForKind {
  // Define our own vectors without any inline storage so they can be used
  // with swap.
  using Uint32Vector = Vector<uint32_t, 0, SystemAllocPolicy>;
  using BytecodeOffsetVector =
      mozilla::Vector<BytecodeOffset, 0, SystemAllocPolicy>;

#ifdef DEBUG
  TrapMachineInsnVector machineInsns_;
#endif
  Uint32Vector pcOffsets_;
  BytecodeOffsetVector bytecodeOffsets_;
  InlinedCallerOffsetsIndexHashMap inlinedCallerOffsetsMap_;

 public:
  explicit TrapSitesForKind() = default;

  // We limit the maximum amount of trap sites to fit in a uint32_t for better
  // compaction of the sparse hash map. This is dynamically enforced, but
  // should be safe. The maximum executable memory in a process is at most
  // ~2GiB, a trapping machine instruction is at least a byte (realistically
  // much more), which would put the limit of trap sites far below UINT32_MAX.
  // We subtract one so that this check is not idempotent on 32-bit systems.
  static constexpr size_t MAX_LENGTH = UINT32_MAX - 1;

  uint32_t getPCoffset(uint32_t index) const { return pcOffsets_[index]; }

  uint32_t length() const {
    size_t result = pcOffsets_.length();
    // Enforced by dynamic checks in mutation functions.
    MOZ_ASSERT(result <= MAX_LENGTH);
    return (uint32_t)result;
  }

  bool empty() const { return pcOffsets_.empty(); }

  [[nodiscard]]
  bool reserve(size_t length) {
    // See comment on MAX_LENGTH for details.
    if (length > MAX_LENGTH) {
      return false;
    }

#ifdef DEBUG
    if (!machineInsns_.reserve(length)) {
      return false;
    }
#endif
    return pcOffsets_.reserve(length) && bytecodeOffsets_.reserve(length);
  }

  [[nodiscard]]
  bool append(TrapMachineInsn insn, FaultingCodeRange fcr,
              const TrapSiteDesc& desc) {
    MOZ_ASSERT(desc.bytecodeOffset.isValid());

    // Reserve space in all collections to avoid being in an inconsistent state
    // in case of failure.
#ifdef DEBUG
    if (!machineInsns_.reserve(machineInsns_.length() + 1)) {
      return false;
    }
#endif
    if (!pcOffsets_.reserve(pcOffsets_.length() + 1) ||
        !bytecodeOffsets_.reserve(bytecodeOffsets_.length() + 1)) {
      return false;
    }

    uint32_t index = length();

    // Add an entry in our map for the trap's inlined caller offsets.
    if (!desc.inlinedCallerOffsetsIndex.isNone() &&
        !inlinedCallerOffsetsMap_.putNew(index,
                                         desc.inlinedCallerOffsetsIndex)) {
      return false;
    }

#ifdef DEBUG
    machineInsns_.infallibleAppend(insn);
#endif
    pcOffsets_.infallibleAppend(fcr.offsetUnchecked());
    bytecodeOffsets_.infallibleAppend(desc.bytecodeOffset);

    return true;
  }

  [[nodiscard]]
  bool appendAll(TrapSitesForKind&& other, uint32_t baseCodeOffset,
                 InlinedCallerOffsetIndex baseInlinedCallerOffsetIndex) {
    // See comment on MAX_LENGTH for details.
    mozilla::CheckedUint32 newLength =
        mozilla::CheckedUint32(length()) + other.length();
    if (!newLength.isValid() || newLength.value() > MAX_LENGTH) {
      return false;
    }

    // Reserve space in all collections to avoid being in an inconsistent state
    // in case of failure.
#ifdef DEBUG
    if (!machineInsns_.reserve(newLength.value())) {
      return false;
    }
#endif
    if (!pcOffsets_.reserve(newLength.value()) ||
        !bytecodeOffsets_.reserve(newLength.value())) {
      return false;
    }
    if (!inlinedCallerOffsetsMap_.reserve(
            inlinedCallerOffsetsMap_.count() +
            other.inlinedCallerOffsetsMap_.count())) {
      return false;
    }

    // Copy over the map of `other`s inlined caller offsets. The keys are trap
    // site indices, and must be updated for the base index that `other` is
    // being inserted into. The values are inlined caller offsets and must be
    // updated for the base inlined caller offset that the associated inlining
    // context was added to. See ModuleGenerator::linkCompiledCode.
    uint32_t baseTrapSiteIndex = length();
    for (auto iter = other.inlinedCallerOffsetsMap_.modIter(); !iter.done();
         iter.next()) {
      uint32_t newTrapSiteIndex = baseTrapSiteIndex + iter.get().key();
      uint32_t newInlinedCallerOffsetIndex =
          iter.get().value().value() + baseInlinedCallerOffsetIndex.value();

      inlinedCallerOffsetsMap_.putNewInfallible(newTrapSiteIndex,
                                                newInlinedCallerOffsetIndex);
    }

    // Add the baseCodeOffset to the pcOffsets that we are adding to ourselves.
    for (uint32_t& pcOffset : other.pcOffsets_) {
      pcOffset += baseCodeOffset;
    }

#ifdef DEBUG
    machineInsns_.infallibleAppend(other.machineInsns_.begin(),
                                   other.machineInsns_.end());
#endif
    pcOffsets_.infallibleAppend(other.pcOffsets_.begin(),
                                other.pcOffsets_.end());
    bytecodeOffsets_.infallibleAppend(other.bytecodeOffsets_.begin(),
                                      other.bytecodeOffsets_.end());
    return true;
  }

  void clear() {
#ifdef DEBUG
    machineInsns_.clear();
#endif
    pcOffsets_.clear();
    bytecodeOffsets_.clear();
    inlinedCallerOffsetsMap_.clear();
  }

  void swap(TrapSitesForKind& other) {
#ifdef DEBUG
    machineInsns_.swap(other.machineInsns_);
#endif
    pcOffsets_.swap(other.pcOffsets_);
    bytecodeOffsets_.swap(other.bytecodeOffsets_);
    inlinedCallerOffsetsMap_.swap(other.inlinedCallerOffsetsMap_);
  }

  void shrinkStorageToFit() {
#ifdef DEBUG
    machineInsns_.shrinkStorageToFit();
#endif
    pcOffsets_.shrinkStorageToFit();
    bytecodeOffsets_.shrinkStorageToFit();
    inlinedCallerOffsetsMap_.compact();
  }

  bool lookup(uint32_t trapInstructionOffset,
              const InliningContext& inliningContext, TrapSite* trapOut) const;

  void checkInvariants(const uint8_t* codeBase) const;

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const {
    size_t result = 0;
#ifdef DEBUG
    result += machineInsns_.sizeOfExcludingThis(mallocSizeOf);
#endif
    ShareableBytecodeOffsetVector::SeenSet seen;
    return result + pcOffsets_.sizeOfExcludingThis(mallocSizeOf) +
           bytecodeOffsets_.sizeOfExcludingThis(mallocSizeOf) +
           inlinedCallerOffsetsMap_.shallowSizeOfExcludingThis(mallocSizeOf);
  }

  WASM_DECLARE_FRIEND_SERIALIZE(TrapSitesForKind);
};

// A collection of TrapSite for any kind of trap and optimized for
// compact storage.
class TrapSites {
  using TrapSiteVectorArray =
      mozilla::EnumeratedArray<Trap, TrapSitesForKind, size_t(Trap::Limit)>;

  TrapSiteVectorArray array_;

 public:
  explicit TrapSites() = default;

  const TrapSitesForKind& get(Trap trap) const { return array_[trap]; }

  bool empty() const {
    for (Trap trap : mozilla::MakeEnumeratedRange(Trap::Limit)) {
      if (!array_[trap].empty()) {
        return false;
      }
    }

    return true;
  }

  [[nodiscard]]
  bool reserve(Trap trap, size_t length) {
    return array_[trap].reserve(length);
  }

  [[nodiscard]]
  bool append(Trap trap, TrapMachineInsn insn, FaultingCodeRange fcr,
              const TrapSiteDesc& desc) {
    return array_[trap].append(insn, fcr, desc);
  }

  [[nodiscard]]
  bool appendAll(TrapSites&& other, uint32_t baseCodeOffset,
                 InlinedCallerOffsetIndex baseInlinedCallerOffsetIndex) {
    for (Trap trap : mozilla::MakeEnumeratedRange(Trap::Limit)) {
      if (!array_[trap].appendAll(std::move(other.array_[trap]), baseCodeOffset,
                                  baseInlinedCallerOffsetIndex)) {
        return false;
      }
    }
    return true;
  }

  void clear() {
    for (Trap trap : mozilla::MakeEnumeratedRange(Trap::Limit)) {
      array_[trap].clear();
    }
  }

  void swap(TrapSites& rhs) {
    for (Trap trap : mozilla::MakeEnumeratedRange(Trap::Limit)) {
      array_[trap].swap(rhs.array_[trap]);
    }
  }

  void shrinkStorageToFit() {
    for (Trap trap : mozilla::MakeEnumeratedRange(Trap::Limit)) {
      array_[trap].shrinkStorageToFit();
    }
  }

  size_t length(Trap trap) const { return array_[trap].length(); }

  [[nodiscard]]
  bool lookup(uint32_t trapInstructionOffset,
              const InliningContext& inliningContext, Trap* kindOut,
              TrapSite* trapOut) const {
    for (Trap trap : mozilla::MakeEnumeratedRange(Trap::Limit)) {
      const TrapSitesForKind& trapSitesForKind = array_[trap];
      if (trapSitesForKind.lookup(trapInstructionOffset, inliningContext,
                                  trapOut)) {
        *kindOut = trap;
        return true;
      }
    }
    return false;
  }

  void checkInvariants(const uint8_t* codeBase) const {
    for (Trap trap : mozilla::MakeEnumeratedRange(Trap::Limit)) {
      array_[trap].checkInvariants(codeBase);
    }
  }

  size_t sumOfLengths() const {
    size_t result = 0;
    for (Trap trap : mozilla::MakeEnumeratedRange(Trap::Limit)) {
      result += array_[trap].length();
    }
    return result;
  }

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const {
    size_t result = 0;
    for (Trap trap : mozilla::MakeEnumeratedRange(Trap::Limit)) {
      result += array_[trap].sizeOfExcludingThis(mallocSizeOf);
    }
    return result;
  }

  WASM_DECLARE_FRIEND_SERIALIZE(TrapSites);
};

struct CallFarJump {
  uint32_t targetFuncIndex;
  uint32_t jumpOffset;
  WASM_CHECK_CACHEABLE_POD(targetFuncIndex, jumpOffset);

  CallFarJump(uint32_t targetFuncIndex, uint32_t jumpOffset)
      : targetFuncIndex(targetFuncIndex), jumpOffset(jumpOffset) {}
};
WASM_DECLARE_CACHEABLE_POD(CallFarJump);

using CallFarJumpVector = Vector<CallFarJump, 0, SystemAllocPolicy>;

class CallRefMetricsPatch {
 private:
  // The offset of where to patch in the offset of the CallRefMetrics.
  uint32_t offsetOfOffsetPatch_;
  static constexpr uint32_t NO_OFFSET = UINT32_MAX;

  WASM_CHECK_CACHEABLE_POD(offsetOfOffsetPatch_);

 public:
  explicit CallRefMetricsPatch() : offsetOfOffsetPatch_(NO_OFFSET) {}

  bool hasOffsetOfOffsetPatch() const {
    return offsetOfOffsetPatch_ != NO_OFFSET;
  }
  uint32_t offsetOfOffsetPatch() const { return offsetOfOffsetPatch_; }
  void setOffset(uint32_t indexOffset) {
    MOZ_ASSERT(!hasOffsetOfOffsetPatch());
    MOZ_ASSERT(indexOffset != NO_OFFSET);
    offsetOfOffsetPatch_ = indexOffset;
  }
};

using CallRefMetricsPatchVector =
    Vector<CallRefMetricsPatch, 0, SystemAllocPolicy>;

class AllocSitePatch {
 private:
  uint32_t patchOffset_;
  static constexpr uint32_t NO_OFFSET = UINT32_MAX;

 public:
  explicit AllocSitePatch() : patchOffset_(NO_OFFSET) {}

  bool hasPatchOffset() const { return patchOffset_ != NO_OFFSET; }
  uint32_t patchOffset() const { return patchOffset_; }
  void setPatchOffset(uint32_t offset) {
    MOZ_ASSERT(!hasPatchOffset());
    MOZ_ASSERT(offset != NO_OFFSET);
    patchOffset_ = offset;
  }
};

using AllocSitePatchVector = Vector<AllocSitePatch, 0, SystemAllocPolicy>;

// On trap, the bytecode offset to be reported in callstacks is saved.

struct TrapData {
  // The resumePC indicates where, if the trap doesn't throw, the trap stub
  // should jump to after restoring all register state.
  void* resumePC;

  // The unwoundPC is the PC after adjustment by wasm::StartUnwinding(), which
  // basically unwinds partially-construted wasm::Frames when pc is in the
  // prologue/epilogue. Stack traces during a trap should use this PC since
  // it corresponds to the JitActivation::wasmExitFP.
  void* unwoundPC;

  Trap trap;
  TrapSite trapSite;

  // A return_call_indirect from the first function in an activation into
  // a signature mismatch may leave us with only one frame. This frame is
  // validly constructed, but has no debug frame yet.
  bool failedUnwindSignatureMismatch;

  // Indicates whether wasm::StartUnwinding() actually unwound the frame within
  // which the trap occurred. This is used to unwind the corresponding shadow
  // stack entry in wasm::HandleExceptionWasm.
  bool unwoundFrame;

  struct FaultInfo {
    uint32_t memoryIndex;
    uint64_t byteOffset;
  };

  // For Trap::OutOfBounds triggered by a memory fault, the memory index and
  // byte offset of the faulting address within the memory's mapped region.
  mozilla::Maybe<FaultInfo> faultInfo;
};

// A class that abstractifies the process of adding stackmaps to a collection
// thereof.  The idea is that an instantiation of this interface can perform any
// action it wants in `addMap`, and `addMap` will be called deep within the
// assembler stack, normally to add a stackmap corresponding to a trap site.
// This decouples the assembler stack from any knowledge of how baseline/Ion
// manage stackmaps.
class StackMapRegistry {
 public:
  [[nodiscard]]
  virtual bool addMap(StackMap* map, FaultingCodeRange insnRange) = 0;
};

// The (,Callable,Func)Offsets classes are used to record the offsets of
// different key points in a CodeRange during compilation.

struct Offsets {
  explicit Offsets(uint32_t begin = 0, uint32_t end = 0)
      : begin(begin), end(end) {}

  // These define a [begin, end) contiguous range of instructions compiled
  // into a CodeRange.
  uint32_t begin;
  uint32_t end;

  WASM_CHECK_CACHEABLE_POD(begin, end);
};

WASM_DECLARE_CACHEABLE_POD(Offsets);

struct CallableOffsets : Offsets {
  MOZ_IMPLICIT CallableOffsets(uint32_t ret = 0) : ret(ret) {}

  // The offset of the return instruction precedes 'end' by a variable number
  // of instructions due to out-of-line codegen.
  uint32_t ret;

  WASM_CHECK_CACHEABLE_POD_WITH_PARENT(Offsets, ret);
};

WASM_DECLARE_CACHEABLE_POD(CallableOffsets);

struct ImportOffsets : CallableOffsets {
  MOZ_IMPLICIT ImportOffsets() : afterFallbackCheck(0) {}

  // The entry point after initial prologue check.
  uint32_t afterFallbackCheck;

  WASM_CHECK_CACHEABLE_POD_WITH_PARENT(CallableOffsets, afterFallbackCheck);
};

WASM_DECLARE_CACHEABLE_POD(ImportOffsets);

struct FuncOffsets : CallableOffsets {
  MOZ_IMPLICIT FuncOffsets() : uncheckedCallEntry(0), tierEntry(0) {}

  // Function CodeRanges have a checked call entry which takes an extra
  // signature argument which is checked against the callee's signature before
  // falling through to the normal prologue. The checked call entry is thus at
  // the beginning of the CodeRange and the unchecked call entry is at some
  // offset after the checked call entry.
  //
  // Note that there won't always be a checked call entry because not all
  // functions require them. See GenerateFunctionPrologue.
  uint32_t uncheckedCallEntry;

  // The tierEntry is the point within a function to which the patching code
  // within a Tier-1 function jumps.  It could be the instruction following
  // the jump in the Tier-1 function, or the point following the standard
  // prologue within a Tier-2 function.
  uint32_t tierEntry;

  WASM_CHECK_CACHEABLE_POD_WITH_PARENT(CallableOffsets, uncheckedCallEntry,
                                       tierEntry);
};

WASM_DECLARE_CACHEABLE_POD(FuncOffsets);

using FuncOffsetsVector = Vector<FuncOffsets, 0, SystemAllocPolicy>;

// A CodeRange describes a single contiguous range of code within a wasm
// module's code segment. A CodeRange describes what the code does and, for
// function bodies, the name and source coordinates of the function.

class CodeRange {
 public:
  enum Kind {
    Function,                  // function definition
    InterpEntry,               // calls into wasm from C++
    JitEntry,                  // calls into wasm from jit code
    ImportInterpExit,          // slow-path calling from wasm into C++ interp
    ImportJitExit,             // fast-path calling from wasm into jit code
    BuiltinThunk,              // fast-path calling from wasm into a C++ native
    TrapExit,                  // calls C++ to report and jumps to throw stub
    DebugStub,                 // calls C++ to handle debug event
    RequestTierUpStub,         // calls C++ to request tier-2 compilation
    UpdateCallRefMetricsStub,  // updates a CallRefMetrics
#ifdef ENABLE_WASM_JSPI
    ContBaseFrame,  // base frame for a cont stack
#endif
    FarJumpIsland,  // inserted to connect otherwise out-of-range insns
    Throw           // special stack-unwinding stub jumped to by other stubs
  };

 private:
  // All fields are treated as cacheable POD:
  uint32_t begin_;
  uint32_t ret_;
  uint32_t end_;
  union {
    struct {
      uint32_t funcIndex_;
      union {
        struct {
          uint16_t beginToUncheckedCallEntry_;
          uint16_t beginToTierEntry_;
          bool hasUnwindInfo_;
        } func;
        uint16_t jitExitEntry_;
      };
    };
    Trap trap_;
  } u;
  Kind kind_ : 8;

  WASM_CHECK_CACHEABLE_POD(begin_, ret_, end_, u.funcIndex_,
                           u.func.beginToUncheckedCallEntry_,
                           u.func.beginToTierEntry_, u.func.hasUnwindInfo_,
                           u.trap_, kind_);

 public:
  CodeRange() = default;
  CodeRange(Kind kind, Offsets offsets);
  CodeRange(Kind kind, uint32_t funcIndex, Offsets offsets);
  CodeRange(Kind kind, CallableOffsets offsets);
  CodeRange(Kind kind, uint32_t funcIndex, CallableOffsets);
  CodeRange(Kind kind, uint32_t funcIndex, ImportOffsets offsets);
  CodeRange(uint32_t funcIndex, FuncOffsets offsets, bool hasUnwindInfo);

  void offsetBy(uint32_t offset) {
    begin_ += offset;
    end_ += offset;
    if (hasReturn()) {
      ret_ += offset;
    }
  }

  // All CodeRanges have a begin and end.

  uint32_t begin() const { return begin_; }
  uint32_t end() const { return end_; }

  // Other fields are only available for certain CodeRange::Kinds.

  Kind kind() const { return kind_; }

  bool isFunction() const { return kind() == Function; }
  bool isImportExit() const {
    return kind() == ImportJitExit || kind() == ImportInterpExit ||
           kind() == BuiltinThunk;
  }
  bool isImportInterpExit() const { return kind() == ImportInterpExit; }
  bool isImportJitExit() const { return kind() == ImportJitExit; }
  bool isTrapExit() const { return kind() == TrapExit; }
  bool isDebugStub() const { return kind() == DebugStub; }
  bool isRequestTierUpStub() const { return kind() == RequestTierUpStub; }
  bool isUpdateCallRefMetricsStub() const {
    return kind() == UpdateCallRefMetricsStub;
  }
  bool isThunk() const { return kind() == FarJumpIsland; }

  // Functions, import exits, debug stubs and JitEntry stubs have standard
  // callable prologues and epilogues. Asynchronous frame iteration needs to
  // know the offset of the return instruction to calculate the frame pointer.

  bool hasReturn() const {
    return isFunction() || isImportExit() || isDebugStub() ||
           isRequestTierUpStub() || isUpdateCallRefMetricsStub() ||
           isJitEntry();
  }
  uint32_t ret() const {
    MOZ_ASSERT(hasReturn());
    return ret_;
  }

  // Functions, export stubs and import stubs all have an associated function
  // index.

  bool isJitEntry() const { return kind() == JitEntry; }
  bool isInterpEntry() const { return kind() == InterpEntry; }
  bool isEntry() const { return isInterpEntry() || isJitEntry(); }
#ifdef ENABLE_WASM_JSPI
  bool isContBaseFrame() const { return kind() == ContBaseFrame; }
#endif
  bool hasFuncIndex() const {
    return isFunction() || isImportExit() || isEntry();
  }
  uint32_t funcIndex() const {
    MOZ_ASSERT(hasFuncIndex());
    return u.funcIndex_;
  }

  // TrapExit CodeRanges have a Trap field.

  Trap trap() const {
    MOZ_ASSERT(isTrapExit());
    return u.trap_;
  }

  // Function CodeRanges have two entry points: one for normal calls (with a
  // known signature) and one for table calls (which involves dynamic
  // signature checking).

  uint32_t funcCheckedCallEntry() const {
    MOZ_ASSERT(isFunction());
    // not all functions have the checked call prologue;
    // see GenerateFunctionPrologue
    MOZ_ASSERT(u.func.beginToUncheckedCallEntry_ != 0);
    return begin_;
  }
  uint32_t funcUncheckedCallEntry() const {
    MOZ_ASSERT(isFunction());
    return begin_ + u.func.beginToUncheckedCallEntry_;
  }
  uint32_t funcTierEntry() const {
    MOZ_ASSERT(isFunction());
    return begin_ + u.func.beginToTierEntry_;
  }
  bool funcHasUnwindInfo() const {
    MOZ_ASSERT(isFunction());
    return u.func.hasUnwindInfo_;
  }
  uint32_t importJitExitEntry() const {
    MOZ_ASSERT(isImportJitExit());
    return begin_ + u.jitExitEntry_;
  }

  // A sorted array of CodeRanges can be looked up via BinarySearch and
  // OffsetInCode.

  struct OffsetInCode {
    size_t offset;
    explicit OffsetInCode(size_t offset) : offset(offset) {}
    bool operator==(const CodeRange& rhs) const {
      return offset >= rhs.begin() && offset < rhs.end();
    }
    bool operator<(const CodeRange& rhs) const { return offset < rhs.begin(); }
  };
};

WASM_DECLARE_CACHEABLE_POD(CodeRange);
WASM_DECLARE_POD_VECTOR(CodeRange, CodeRangeVector)

extern const CodeRange* LookupInSorted(const CodeRangeVector& codeRanges,
                                       CodeRange::OffsetInCode target);

// While the frame-pointer chain allows the stack to be unwound without
// metadata, Error.stack still needs to know the line/column of every call in
// the chain. A CallSiteDesc describes a single callsite to which CallSite adds
// the metadata necessary to walk up to the next frame. Lastly CallSiteAndTarget
// adds the function index of the callee.

enum class CallSiteKind : uint8_t {
  Func,           // pc-relative call to a specific function
  Import,         // wasm import call
  Indirect,       // dynamic callee called via register, context on stack
  IndirectFast,   // dynamically determined to be same-instance
  FuncRef,        // call using direct function reference
  FuncRefFast,    // call using direct function reference within same-instance
  ReturnFunc,     // return call to a specific function
  Symbolic,       // call to a single symbolic callee
  EnterFrame,     // call to a enter frame handler
  LeaveFrame,     // call to a leave frame handler
  CollapseFrame,  // call to a leave frame handler during tail call
  StackSwitch,    // stack switch point
  Breakpoint,     // call to instruction breakpoint
  RequestTierUp,  // call to request tier-2 compilation of this function
  // return call made as a regular call
  ReturnCallFallback
};

WASM_DECLARE_CACHEABLE_POD(CallSiteKind);
WASM_DECLARE_POD_VECTOR(CallSiteKind, CallSiteKindVector)

class CallSiteDesc {
  // The line of bytecode offset that this call site is at.
  uint32_t bytecodeOffset_;
  // If this call site has been inlined into another function, the inlined
  // caller functions. The direct ancestor of this function (i.e. the one
  // directly above it on the stack) is the last entry in the vector.
  InlinedCallerOffsetIndex inlinedCallerOffsetsIndex_;
  CallSiteKind kind_;

 public:
  // Some call sites do not have a bytecode offset associated with them
  // (such as ones in import function wrappers). We represent them using '0' as
  // the bytecode offset. This should never be confused with a real offset,
  // because the binary format has overhead from the magic number and section
  // headers.
  static constexpr uint32_t NO_BYTECODE_OFFSET = 0;
  static constexpr uint32_t FIRST_VALID_BYTECODE_OFFSET =
      NO_BYTECODE_OFFSET + 1;
  static_assert(NO_BYTECODE_OFFSET < sizeof(wasm::MagicNumber));
  // Limit lines or bytecodes to the maximum module size.
  static constexpr uint32_t MAX_BYTECODE_OFFSET_VALUE = wasm::MaxModuleBytes;

  CallSiteDesc()
      : bytecodeOffset_(NO_BYTECODE_OFFSET), kind_(CallSiteKind::Func) {}
  explicit CallSiteDesc(CallSiteKind kind)
      : bytecodeOffset_(NO_BYTECODE_OFFSET), kind_(kind) {
    MOZ_ASSERT(kind == CallSiteKind(kind_));
  }
  CallSiteDesc(uint32_t bytecodeOffset, CallSiteKind kind)
      : bytecodeOffset_(bytecodeOffset), kind_(kind) {
    MOZ_ASSERT(kind == CallSiteKind(kind_));
    MOZ_ASSERT(bytecodeOffset == bytecodeOffset_);
  }
  CallSiteDesc(BytecodeOffset bytecodeOffset, CallSiteKind kind)
      : bytecodeOffset_(bytecodeOffset.offset()), kind_(kind) {
    MOZ_ASSERT(kind == CallSiteKind(kind_));
    MOZ_ASSERT(bytecodeOffset.offset() == bytecodeOffset_);
  }
  CallSiteDesc(uint32_t bytecodeOffset,
               InlinedCallerOffsetIndex inlinedCallerOffsetsIndex,
               CallSiteKind kind)
      : bytecodeOffset_(bytecodeOffset),
        inlinedCallerOffsetsIndex_(inlinedCallerOffsetsIndex),
        kind_(kind) {
    MOZ_ASSERT(kind == CallSiteKind(kind_));
    MOZ_ASSERT(bytecodeOffset == bytecodeOffset_);
  }
  CallSiteDesc(BytecodeOffset bytecodeOffset,
               uint32_t inlinedCallerOffsetsIndex, CallSiteKind kind)
      : bytecodeOffset_(bytecodeOffset.offset()),
        inlinedCallerOffsetsIndex_(inlinedCallerOffsetsIndex),
        kind_(kind) {
    MOZ_ASSERT(kind == CallSiteKind(kind_));
    MOZ_ASSERT(bytecodeOffset.offset() == bytecodeOffset_);
  }
  uint32_t bytecodeOffset() const { return bytecodeOffset_; }
  InlinedCallerOffsetIndex inlinedCallerOffsetsIndex() const {
    return inlinedCallerOffsetsIndex_;
  }
  TrapSiteDesc toTrapSiteDesc() const {
    return TrapSiteDesc(wasm::BytecodeOffset(bytecodeOffset()),
                        inlinedCallerOffsetsIndex_);
  }
  CallSiteKind kind() const { return kind_; }
  bool isImportCall() const { return kind() == CallSiteKind::Import; }
  bool isIndirectCall() const { return kind() == CallSiteKind::Indirect; }
  bool isFuncRefCall() const { return kind() == CallSiteKind::FuncRef; }
  bool isStackSwitch() const { return kind() == CallSiteKind::StackSwitch; }
  bool isReturnCallFallback() const {
    return kind() == CallSiteKind::ReturnCallFallback;
  }
  bool mightBeCrossInstance() const {
    return isImportCall() || isIndirectCall() || isFuncRefCall() ||
           isStackSwitch() || isReturnCallFallback();
  }
};

using CallSiteDescVector = mozilla::Vector<CallSiteDesc, 0, SystemAllocPolicy>;

class CallSite : public CallSiteDesc {
  uint32_t returnAddressOffset_;
  const InlinedCallerOffsets* inlinedCallerOffsets_;

  CallSite(const CallSiteDesc& desc, uint32_t returnAddressOffset,
           const InlinedCallerOffsets* inlinedCallerOffsets)
      : CallSiteDesc(desc),
        returnAddressOffset_(returnAddressOffset),
        inlinedCallerOffsets_(inlinedCallerOffsets) {}
  friend class CallSites;

 public:
  CallSite() : returnAddressOffset_(0), inlinedCallerOffsets_(nullptr) {}

  uint32_t returnAddressOffset() const { return returnAddressOffset_; }
  BytecodeOffsetSpan inlinedCallerOffsetsSpan() const {
    if (!inlinedCallerOffsets_) {
      return BytecodeOffsetSpan();
    }
    return BytecodeOffsetSpan(inlinedCallerOffsets_->begin(),
                              inlinedCallerOffsets_->end());
  }
  const InlinedCallerOffsets* inlinedCallerOffsets() const {
    return inlinedCallerOffsets_;
  }
};

// A collection of CallSite that is optimized for compact storage.
//
// The individual fields are split to be in their own vectors to minimize
// overhead due to alignment for small fields like CallSiteKind.
//
// The `inlinedCallerOffsets` field is split into a sparse hash map as it's
// expected that many call sites will not be in inlined functions.
class CallSites {
  // Define our own Uint32Vector without any inline storage so it can be used
  // with swap.
  using Uint32Vector = Vector<uint32_t, 0, SystemAllocPolicy>;

  CallSiteKindVector kinds_;
  Uint32Vector bytecodeOffsets_;
  Uint32Vector returnAddressOffsets_;
  InlinedCallerOffsetsIndexHashMap inlinedCallerOffsetsMap_;

 public:
  explicit CallSites() = default;

  // We limit the maximum amount of call sites to fit in a uint32_t for better
  // compaction of the sparse hash map. This is dynamically enforced, but
  // should be safe. The maximum executable memory in a process is at most
  // ~2GiB, a machine call instruction is at least two bytes (realistically
  // much more), which would put the limit of call sites far below UINT32_MAX.
  // We subtract one so that this check is not idempotent on 32-bit systems.
  static constexpr size_t MAX_LENGTH = UINT32_MAX - 1;

  uint32_t length() const {
    size_t result = kinds_.length();
    // Enforced by dynamic checks in mutation functions.
    MOZ_ASSERT(result <= MAX_LENGTH);
    return (uint32_t)result;
  }

  bool empty() const { return kinds_.empty(); }

  CallSiteKind kind(size_t index) const { return kinds_[index]; }
  BytecodeOffset bytecodeOffset(size_t index) const {
    return BytecodeOffset(bytecodeOffsets_[index]);
  }
  uint32_t returnAddressOffset(size_t index) const {
    return returnAddressOffsets_[index];
  }

  CallSite get(size_t index, const InliningContext& inliningContext) const {
    InlinedCallerOffsetIndex inlinedCallerOffsetsIndex;
    const InlinedCallerOffsets* inlinedCallerOffsets = nullptr;
    if (auto entry = inlinedCallerOffsetsMap_.readonlyThreadsafeLookup(index)) {
      inlinedCallerOffsetsIndex = entry->value();
      inlinedCallerOffsets = inliningContext[entry->value()];
    }
    return CallSite(CallSiteDesc(bytecodeOffsets_[index],
                                 inlinedCallerOffsetsIndex, kinds_[index]),
                    returnAddressOffsets_[index], inlinedCallerOffsets);
  }

  [[nodiscard]]
  bool lookup(uint32_t returnAddressOffset,
              const InliningContext& inliningContext, CallSite* callSite) const;

  [[nodiscard]]
  bool append(const CallSiteDesc& callSiteDesc, uint32_t returnAddressOffset) {
    // See comment on MAX_LENGTH for details.
    if (length() == MAX_LENGTH) {
      return false;
    }

    uint32_t index = length();

    // If there are inline caller offsets, then insert an entry in our hash map.
    InlinedCallerOffsetIndex inlinedCallerOffsetsIndex =
        callSiteDesc.inlinedCallerOffsetsIndex();

    // Reserve space in all collections to avoid being in an inconsistent state
    // in case of failure.
    if (!kinds_.reserve(kinds_.length() + 1) ||
        !bytecodeOffsets_.reserve(bytecodeOffsets_.length() + 1) ||
        !returnAddressOffsets_.reserve(returnAddressOffsets_.length() + 1)) {
      return false;
    }

    if (!inlinedCallerOffsetsIndex.isNone() &&
        !inlinedCallerOffsetsMap_.putNew(index, inlinedCallerOffsetsIndex)) {
      return false;
    }

    kinds_.infallibleAppend(callSiteDesc.kind());
    bytecodeOffsets_.infallibleAppend(callSiteDesc.bytecodeOffset());
    returnAddressOffsets_.infallibleAppend(returnAddressOffset);

    return true;
  }

  [[nodiscard]]
  bool appendAll(CallSites&& other, uint32_t baseCodeOffset,
                 InlinedCallerOffsetIndex baseInlinedCallerOffsetIndex) {
    // See comment on MAX_LENGTH for details.
    mozilla::CheckedUint32 newLength =
        mozilla::CheckedUint32(length()) + other.length();
    if (!newLength.isValid() || newLength.value() > MAX_LENGTH) {
      return false;
    }

    // Reserve space in all collections to avoid being in an inconsistent state
    // in case of failure.
    if (!kinds_.reserve(newLength.value()) ||
        !bytecodeOffsets_.reserve(newLength.value()) ||
        !returnAddressOffsets_.reserve(newLength.value())) {
      return false;
    }
    if (!inlinedCallerOffsetsMap_.reserve(
            inlinedCallerOffsetsMap_.count() +
            other.inlinedCallerOffsetsMap_.count())) {
      return false;
    }

    // Copy over the map of `other`s inlined caller offsets. The keys are call
    // site indices, and must be updated for the base index that `other` is
    // being inserted into. The values are inlined caller offsets and must be
    // updated for the base inlined caller offset that the associated inlining
    // context was added to. See ModuleGenerator::linkCompiledCode.
    uint32_t baseCallSiteIndex = length();
    for (auto iter = other.inlinedCallerOffsetsMap_.modIter(); !iter.done();
         iter.next()) {
      uint32_t newCallSiteIndex = iter.get().key() + baseCallSiteIndex;
      uint32_t newInlinedCallerOffsetIndex =
          iter.get().value().value() + baseInlinedCallerOffsetIndex.value();

      inlinedCallerOffsetsMap_.putNewInfallible(newCallSiteIndex,
                                                newInlinedCallerOffsetIndex);
    }

    // Add the baseCodeOffset to the pcOffsets that we are adding to ourselves.
    for (uint32_t& pcOffset : other.returnAddressOffsets_) {
      pcOffset += baseCodeOffset;
    }

    kinds_.infallibleAppend(other.kinds_.begin(), other.kinds_.end());
    bytecodeOffsets_.infallibleAppend(other.bytecodeOffsets_.begin(),
                                      other.bytecodeOffsets_.end());
    returnAddressOffsets_.infallibleAppend(other.returnAddressOffsets_.begin(),
                                           other.returnAddressOffsets_.end());
    return true;
  }

  void swap(CallSites& other) {
    kinds_.swap(other.kinds_);
    bytecodeOffsets_.swap(other.bytecodeOffsets_);
    returnAddressOffsets_.swap(other.returnAddressOffsets_);
    inlinedCallerOffsetsMap_.swap(other.inlinedCallerOffsetsMap_);
  }

  void clear() {
    kinds_.clear();
    bytecodeOffsets_.clear();
    returnAddressOffsets_.clear();
    inlinedCallerOffsetsMap_.clear();
  }

  [[nodiscard]]
  bool reserve(size_t length) {
    // See comment on MAX_LENGTH for details.
    if (length > MAX_LENGTH) {
      return false;
    }

    return kinds_.reserve(length) && bytecodeOffsets_.reserve(length) &&
           returnAddressOffsets_.reserve(length);
  }

  void shrinkStorageToFit() {
    kinds_.shrinkStorageToFit();
    bytecodeOffsets_.shrinkStorageToFit();
    returnAddressOffsets_.shrinkStorageToFit();
    inlinedCallerOffsetsMap_.compact();
  }

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const {
    return kinds_.sizeOfExcludingThis(mallocSizeOf) +
           bytecodeOffsets_.sizeOfExcludingThis(mallocSizeOf) +
           returnAddressOffsets_.sizeOfExcludingThis(mallocSizeOf) +
           inlinedCallerOffsetsMap_.shallowSizeOfExcludingThis(mallocSizeOf);
  }

  void checkInvariants() const {
#ifdef DEBUG
    MOZ_ASSERT(kinds_.length() == bytecodeOffsets_.length());
    MOZ_ASSERT(kinds_.length() == returnAddressOffsets_.length());
    uint32_t last = 0;
    for (uint32_t returnAddressOffset : returnAddressOffsets_) {
      MOZ_ASSERT(returnAddressOffset >= last);
      last = returnAddressOffset;
    }
    for (auto iter = inlinedCallerOffsetsMap_.iter(); !iter.done();
         iter.next()) {
      MOZ_ASSERT(iter.get().key() < length());
      MOZ_ASSERT(!iter.get().value().isNone());
    }
#endif
  }

  WASM_DECLARE_FRIEND_SERIALIZE(CallSites);
};

// A CallSiteTarget describes the callee of a CallSite, either a function or a
// trap exit. Although checked in debug builds, a CallSiteTarget doesn't
// officially know whether it targets a function or trap, relying on the Kind of
// the CallSite to discriminate.

class CallSiteTarget {
  uint32_t packed_;

  WASM_CHECK_CACHEABLE_POD(packed_);
#ifdef DEBUG
  enum Kind { None, FuncIndex, TrapExit } kind_;
  WASM_CHECK_CACHEABLE_POD(kind_);
#endif

 public:
  explicit CallSiteTarget()
      : packed_(UINT32_MAX)
#ifdef DEBUG
        ,
        kind_(None)
#endif
  {
  }

  explicit CallSiteTarget(uint32_t funcIndex)
      : packed_(funcIndex)
#ifdef DEBUG
        ,
        kind_(FuncIndex)
#endif
  {
  }

  explicit CallSiteTarget(Trap trap)
      : packed_(uint32_t(trap))
#ifdef DEBUG
        ,
        kind_(TrapExit)
#endif
  {
  }

  uint32_t funcIndex() const {
    MOZ_ASSERT(kind_ == FuncIndex);
    return packed_;
  }

  Trap trap() const {
    MOZ_ASSERT(kind_ == TrapExit);
    MOZ_ASSERT(packed_ < uint32_t(Trap::Limit));
    return Trap(packed_);
  }
};

WASM_DECLARE_CACHEABLE_POD(CallSiteTarget);

using CallSiteTargetVector = Vector<CallSiteTarget, 0, SystemAllocPolicy>;

// TryNotes are stored in a vector that acts as an exception table for
// wasm try-catch blocks. These represent the information needed to take
// exception handling actions after a throw is executed.
struct TryNote {
 private:
  // Sentinel value to detect a try note that has not been given a try body.
  static const uint32_t BEGIN_NONE = UINT32_MAX;

  // Sentinel value used in `entryPointOrIsDelegate_`.
  static const uint32_t IS_DELEGATE = UINT32_MAX;

  // Begin code offset of the try body.
  uint32_t begin_;
  // Exclusive end code offset of the try body.
  uint32_t end_;
  // Either a marker that this is a 'delegate' or else the code offset of the
  // landing pad to jump to.
  uint32_t entryPointOrIsDelegate_;
  // If this is a delegate, then this is the code offset to delegate to,
  // otherwise this is the offset from the frame pointer of the stack pointer
  // to use when jumping to the landing pad.
  uint32_t framePushedOrDelegateOffset_;

  WASM_CHECK_CACHEABLE_POD(begin_, end_, entryPointOrIsDelegate_,
                           framePushedOrDelegateOffset_);

 public:
  explicit TryNote()
      : begin_(BEGIN_NONE),
        end_(0),
        entryPointOrIsDelegate_(0),
        framePushedOrDelegateOffset_(0) {}

  // Returns whether a try note has been assigned a range for the try body.
  bool hasTryBody() const { return begin_ != BEGIN_NONE; }

  // The code offset of the beginning of the try body.
  uint32_t tryBodyBegin() const { return begin_; }

  // The code offset of the exclusive end of the try body.
  uint32_t tryBodyEnd() const { return end_; }

  // Returns whether an offset is within this try note's body.
  bool offsetWithinTryBody(uint32_t offset) const {
    return offset > begin_ && offset <= end_;
  }

  // Check if the unwinder should delegate the handling of this try note to the
  // try note given at the delegate offset.
  bool isDelegate() const { return entryPointOrIsDelegate_ == IS_DELEGATE; }

  // The code offset to delegate the handling of this try note to.
  uint32_t delegateOffset() const {
    MOZ_ASSERT(isDelegate());
    return framePushedOrDelegateOffset_;
  }

  // The code offset of the entry to the landing pad.
  uint32_t landingPadEntryPoint() const {
    MOZ_ASSERT(!isDelegate());
    return entryPointOrIsDelegate_;
  }

  // The stack frame pushed amount at the entry to the landing pad.
  uint32_t landingPadFramePushed() const {
    MOZ_ASSERT(!isDelegate());
    return framePushedOrDelegateOffset_;
  }

  // Set the beginning of the try body.
  void setTryBodyBegin(uint32_t begin) {
    // There must not be a begin to the try body yet
    MOZ_ASSERT(begin_ == BEGIN_NONE);
    begin_ = begin;
  }

  // Set the end of the try body.
  void setTryBodyEnd(uint32_t end) {
    // There must be a begin to the try body
    MOZ_ASSERT(begin_ != BEGIN_NONE);
    end_ = end;
    // We do not allow empty try bodies
    MOZ_ASSERT(end_ > begin_);
  }

  // Mark this try note as a delegate, requesting the unwinder to use the try
  // note found at the delegate offset.
  void setDelegate(uint32_t delegateOffset) {
    entryPointOrIsDelegate_ = IS_DELEGATE;
    framePushedOrDelegateOffset_ = delegateOffset;
  }

  // Set the entry point and frame pushed of the landing pad.
  void setLandingPad(uint32_t entryPoint, uint32_t framePushed) {
    MOZ_ASSERT(!isDelegate());
    entryPointOrIsDelegate_ = entryPoint;
    framePushedOrDelegateOffset_ = framePushed;
  }

  // Adjust all code offsets in this try note by a delta.
  void offsetBy(uint32_t offset) {
    begin_ += offset;
    end_ += offset;
    if (isDelegate()) {
      framePushedOrDelegateOffset_ += offset;
    } else {
      entryPointOrIsDelegate_ += offset;
    }
  }

  bool operator<(const TryNote& other) const {
    // Special case comparison with self. This avoids triggering the assertion
    // about non-intersection below. This case can arise in std::sort.
    if (this == &other) {
      return false;
    }
    // Try notes must be properly nested without touching at begin and end
    MOZ_ASSERT(end_ <= other.begin_ || begin_ >= other.end_ ||
               (begin_ > other.begin_ && end_ < other.end_) ||
               (other.begin_ > begin_ && other.end_ < end_));
    // A total order is therefore given solely by comparing end points. This
    // order will be such that the first try note to intersect a point is the
    // innermost try note for that point.
    return end_ < other.end_;
  }
};

WASM_DECLARE_CACHEABLE_POD(TryNote);
WASM_DECLARE_POD_VECTOR(TryNote, TryNoteVector)

class CodeRangeUnwindInfo {
 public:
  enum UnwindHow {
    Normal,
    RestoreFpRa,
    RestoreFp,
    UseFpLr,
    UseFp,
  };

 private:
  uint32_t offset_;
  UnwindHow unwindHow_;

  WASM_CHECK_CACHEABLE_POD(offset_, unwindHow_);

 public:
  CodeRangeUnwindInfo(uint32_t offset, UnwindHow unwindHow)
      : offset_(offset), unwindHow_(unwindHow) {}

  uint32_t offset() const { return offset_; }
  UnwindHow unwindHow() const { return unwindHow_; }

  // Adjust all code offsets in this info by a delta.
  void offsetBy(uint32_t offset) { offset_ += offset; }
};

WASM_DECLARE_CACHEABLE_POD(CodeRangeUnwindInfo);
WASM_DECLARE_POD_VECTOR(CodeRangeUnwindInfo, CodeRangeUnwindInfoVector)

enum class CallIndirectIdKind {
  // Use a machine code immediate for the signature check, only works on simple
  // function types, without super types, and without siblings in their
  // recursion group.
  Immediate,
  // Use the full type definition and subtyping machinery when performing the
  // signature check.
  Global,
  // Don't generate any signature check prologue, for functions that cannot be
  // stored in tables.
  None
};

// CallIndirectId describes how to compile a call_indirect and matching
// signature check in the function prologue for a given function type.

class CallIndirectId {
  CallIndirectIdKind kind_;
  union {
    size_t immediate_;
    struct {
      size_t instanceDataOffset_;
      bool hasSuperType_;
    } global_;
  };

  explicit CallIndirectId(CallIndirectIdKind kind) : kind_(kind) {}

 public:
  CallIndirectId() : kind_(CallIndirectIdKind::None) {}

  // Get the CallIndirectId for a function in a specific module.
  static CallIndirectId forFunc(const CodeMetadata& codeMeta,
                                uint32_t funcIndex);

  // Get the CallIndirectId for a function type in a specific module.
  static CallIndirectId forFuncType(const CodeMetadata& codeMeta,
                                    uint32_t funcTypeIndex);

  CallIndirectIdKind kind() const { return kind_; }
  bool isGlobal() const { return kind_ == CallIndirectIdKind::Global; }

  // The bit-packed representation of simple function types. See FuncType in
  // WasmTypeDef.h for more information.
  uint32_t immediate() const {
    MOZ_ASSERT(kind_ == CallIndirectIdKind::Immediate);
    return immediate_;
  }

  // The offset of the TypeDefInstanceData for the function type.
  uint32_t instanceDataOffset() const {
    MOZ_ASSERT(kind_ == CallIndirectIdKind::Global);
    return global_.instanceDataOffset_;
  }

  // Whether the TypeDef has any super types.
  bool hasSuperType() const {
    MOZ_ASSERT(kind_ == CallIndirectIdKind::Global);
    return global_.hasSuperType_;
  }
};

// CalleeDesc describes how to compile one of the variety of wasm calls.
// This is hoisted into WasmCodegenTypes.h for sharing between Ion and Baseline.

class CalleeDesc {
 public:
  enum Which {
    // Calls a function defined in the same module by its index.
    Func,

    // Calls the import identified by the offset of its FuncImportInstanceData
    // in
    // thread-local data.
    Import,

    // Calls a WebAssembly table (heterogeneous, index must be bounds
    // checked, callee instance depends on TableDesc).
    WasmTable,

    // Call a C++ function identified by SymbolicAddress.
    Builtin,

    // Like Builtin, but automatically passes Instance* as first argument.
    BuiltinInstanceMethod,

    // Calls a function reference.
    FuncRef,
  };

 private:
  // which_ shall be initialized in the static constructors
  MOZ_INIT_OUTSIDE_CTOR Which which_;
  union U {
    U() : funcIndex_(0) {}
    uint32_t funcIndex_;
    struct {
      uint32_t instanceDataOffset_;
    } import;
    struct {
      uint32_t instanceDataOffset_;
      uint64_t minLength_;
      mozilla::Maybe<uint64_t> maxLength_;
      CallIndirectId callIndirectId_;
    } table;
    SymbolicAddress builtin_;
  } u;

 public:
  CalleeDesc() = default;
  static CalleeDesc function(uint32_t funcIndex);
  static CalleeDesc import(uint32_t instanceDataOffset);
  static CalleeDesc wasmTable(const CodeMetadata& codeMeta,
                              const TableDesc& desc, uint32_t tableIndex,
                              CallIndirectId callIndirectId);
  static CalleeDesc builtin(SymbolicAddress callee);
  static CalleeDesc builtinInstanceMethod(SymbolicAddress callee);
  static CalleeDesc wasmFuncRef();
  Which which() const { return which_; }
  uint32_t funcIndex() const {
    MOZ_ASSERT(which_ == Func);
    return u.funcIndex_;
  }
  uint32_t importInstanceDataOffset() const {
    MOZ_ASSERT(which_ == Import);
    return u.import.instanceDataOffset_;
  }
  bool isTable() const { return which_ == WasmTable; }
  uint32_t tableLengthInstanceDataOffset() const {
    MOZ_ASSERT(isTable());
    return u.table.instanceDataOffset_ + offsetof(TableInstanceData, length);
  }
  uint32_t tableFunctionBaseInstanceDataOffset() const {
    MOZ_ASSERT(isTable());
    return u.table.instanceDataOffset_ + offsetof(TableInstanceData, elements);
  }
  CallIndirectId wasmTableSigId() const {
    MOZ_ASSERT(which_ == WasmTable);
    return u.table.callIndirectId_;
  }
  uint64_t wasmTableMinLength() const {
    MOZ_ASSERT(which_ == WasmTable);
    return u.table.minLength_;
  }
  mozilla::Maybe<uint64_t> wasmTableMaxLength() const {
    MOZ_ASSERT(which_ == WasmTable);
    return u.table.maxLength_;
  }
  SymbolicAddress builtin() const {
    MOZ_ASSERT(which_ == Builtin || which_ == BuiltinInstanceMethod);
    return u.builtin_;
  }
  bool isFuncRef() const { return which_ == FuncRef; }
};

struct FuncIonPerfSpewer {
  uint32_t funcIndex = 0;
  jit::IonPerfSpewer spewer;

  FuncIonPerfSpewer() = default;
  FuncIonPerfSpewer(uint32_t funcIndex, jit::IonPerfSpewer&& spewer)
      : funcIndex(funcIndex), spewer(std::move(spewer)) {}
  FuncIonPerfSpewer(FuncIonPerfSpewer&) = delete;
  FuncIonPerfSpewer(FuncIonPerfSpewer&&) = default;
  FuncIonPerfSpewer& operator=(FuncIonPerfSpewer&) = delete;
  FuncIonPerfSpewer& operator=(FuncIonPerfSpewer&&) = default;
};

using FuncIonPerfSpewerVector = Vector<FuncIonPerfSpewer, 8, SystemAllocPolicy>;
using FuncIonPerfSpewerSpan = mozilla::Span<FuncIonPerfSpewer>;

struct FuncBaselinePerfSpewer {
  uint32_t funcIndex = 0;
  jit::WasmBaselinePerfSpewer spewer;

  FuncBaselinePerfSpewer() = default;
  FuncBaselinePerfSpewer(uint32_t funcIndex,
                         jit::WasmBaselinePerfSpewer&& spewer)
      : funcIndex(funcIndex), spewer(std::move(spewer)) {}
  FuncBaselinePerfSpewer(FuncBaselinePerfSpewer&) = delete;
  FuncBaselinePerfSpewer(FuncBaselinePerfSpewer&&) = default;
  FuncBaselinePerfSpewer& operator=(FuncBaselinePerfSpewer&) = delete;
  FuncBaselinePerfSpewer& operator=(FuncBaselinePerfSpewer&&) = default;
};

using FuncBaselinePerfSpewerVector =
    Vector<FuncBaselinePerfSpewer, 8, SystemAllocPolicy>;
using FuncBaselinePerfSpewerSpan = mozilla::Span<FuncBaselinePerfSpewer>;

// This holds stats relating to compilation of some arbitrary set of functions.
// If you add fields, don't forget to update its `clear` and `empty` methods.
struct CompileStats {
  // number of functions in the set
  size_t numFuncs;
  // bytecode size of the functions
  size_t bytecodeSize;
  // number of direct-call / call-ref sites inlined
  size_t inlinedDirectCallCount;
  size_t inlinedCallRefCount;
  // total extra bytecode size from direct-call / call-ref inlining
  size_t inlinedDirectCallBytecodeSize;
  size_t inlinedCallRefBytecodeSize;
  // number of funcs for which inlining stopped due to budget overrun
  size_t numInliningBudgetOverruns;
  // number of funcs for which inlining was made less aggressive because the
  // function was already large
  size_t numLargeFunctionBackoffs = 0;

  void clear() {
    numFuncs = 0;
    bytecodeSize = 0;
    inlinedDirectCallCount = 0;
    inlinedCallRefCount = 0;
    inlinedDirectCallBytecodeSize = 0;
    inlinedCallRefBytecodeSize = 0;
    numInliningBudgetOverruns = 0;
    numLargeFunctionBackoffs = 0;
  }
  CompileStats() { clear(); }

  bool empty() const {
    return 0 == (numFuncs | bytecodeSize | inlinedDirectCallCount |
                 inlinedCallRefCount | inlinedDirectCallBytecodeSize |
                 inlinedCallRefBytecodeSize | numInliningBudgetOverruns |
                 numLargeFunctionBackoffs);
  }

  // Merge in the counts from `other`.  When using this, be careful to avoid
  // double-accounting bugs -- conceptually, `other` should be zeroed out as a
  // result of the merge.  Doing that as part of this routine would be nice but
  // unfortunately interferes with `const` qualification and thread-safety, so
  // that isn't done.
  void merge(const CompileStats& other);
};

// Same as CompileStats, but includes info about compiled-code size.
struct CompileAndLinkStats : public CompileStats {
  // total mapped addr space for generated code (a multiple of the page size)
  size_t codeBytesMapped;
  // total used space for generated code (will be less than the above)
  size_t codeBytesUsed;

  void clear() {
    CompileStats::clear();
    codeBytesMapped = 0;
    codeBytesUsed = 0;
  }
  CompileAndLinkStats() { clear(); }

  bool empty() const {
    return 0 == (codeBytesMapped | codeBytesUsed) && CompileStats::empty();
  }

  // Same comments as for CompileStats::merge apply.
  void merge(const CompileAndLinkStats& other);

  // Merge in just CompileStats from `other`.
  void mergeCompileStats(const CompileStats& other) {
    CompileStats::merge(other);
  }

  void print() const;
};

}  // namespace wasm
}  // namespace js

#endif  // wasm_codegen_types_h
