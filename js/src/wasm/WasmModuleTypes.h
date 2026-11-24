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

#ifndef wasm_module_types_h
#define wasm_module_types_h

#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Span.h"
#include "mozilla/Variant.h"

#include "js/AllocPolicy.h"
#include "js/HashTable.h"
#include "js/RefCounted.h"
#include "js/Utility.h"
#include "js/Vector.h"
#include "wasm/WasmCompileArgs.h"
#include "wasm/WasmConstants.h"
#include "wasm/WasmExprType.h"
#include "wasm/WasmInitExpr.h"
#include "wasm/WasmMemory.h"
#include "wasm/WasmSerialize.h"
#include "wasm/WasmShareable.h"
#include "wasm/WasmTypeDecls.h"
#include "wasm/WasmTypeDef.h"
#include "wasm/WasmValType.h"
#include "wasm/WasmValue.h"

namespace js {
namespace wasm {

class FuncType;

// CacheableChars is used to cacheably store UniqueChars.

struct CacheableChars : UniqueChars {
  CacheableChars() = default;
  explicit CacheableChars(char* ptr) : UniqueChars(ptr) {}
  MOZ_IMPLICIT CacheableChars(UniqueChars&& rhs)
      : UniqueChars(std::move(rhs)) {}
  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
};

using CacheableCharsVector = Vector<CacheableChars, 0, SystemAllocPolicy>;

// CacheableName is used to cacheably store a UTF-8 string that may contain
// null terminators in sequence.

struct CacheableName {
 private:
  UTF8Bytes bytes_;

  const char* begin() const { return (const char*)bytes_.begin(); }
  size_t length() const { return bytes_.length(); }

 public:
  CacheableName() = default;
  MOZ_IMPLICIT CacheableName(UTF8Bytes&& rhs) : bytes_(std::move(rhs)) {}

  bool isEmpty() const { return bytes_.length() == 0; }

  mozilla::Span<char> utf8Bytes() { return mozilla::Span<char>(bytes_); }
  mozilla::Span<const char> utf8Bytes() const {
    return mozilla::Span<const char>(bytes_);
  }

  [[nodiscard]] bool clone(CacheableName* name) const {
    UTF8Bytes bytesCopy;
    if (!bytesCopy.appendAll(bytes_)) {
      return false;
    }
    *name = CacheableName(std::move(bytesCopy));
    return true;
  }

  bool operator==(const CacheableName& other) const {
    return utf8Bytes() == other.utf8Bytes();
  }

  [[nodiscard]] static bool fromUTF8Chars(const char* utf8Chars,
                                          CacheableName* name);
  [[nodiscard]] static bool fromUTF8Bytes(mozilla::Span<const char> utf8Bytes,
                                          CacheableName* name);
  [[nodiscard]] static bool fromLinearString(JSLinearString* str,
                                             CacheableName* name);

  [[nodiscard]] JSString* toJSString(JSContext* cx) const;
  [[nodiscard]] JSAtom* toAtom(JSContext* cx) const;
  [[nodiscard]] bool toPropertyKey(JSContext* cx,
                                   MutableHandleId propertyKey) const;
  [[nodiscard]] UniqueChars toQuotedString(JSContext* cx) const;

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
  WASM_DECLARE_FRIEND_SERIALIZE(CacheableName);
};

using CacheableNameVector = Vector<CacheableName, 0, SystemAllocPolicy>;

// A hash policy for names.
struct NameHasher {
  using Key = mozilla::Span<const char>;
  using Lookup = mozilla::Span<const char>;

  static HashNumber hash(const Lookup& aLookup) {
    return mozilla::HashString(aLookup.data(), aLookup.Length());
  }

  static bool match(const Key& aKey, const Lookup& aLookup) {
    return aKey == aLookup;
  }
};

// A variant of the same hash policy for tables/sets that own their keys.
struct CacheableNameHasher {
  using Key = CacheableName;
  using Lookup = mozilla::Span<const char>;

  static HashNumber hash(const Lookup& aLookup) {
    return mozilla::HashString(aLookup.data(), aLookup.Length());
  }

  static bool match(const Key& aKey, const Lookup& aLookup) {
    return aKey.utf8Bytes() == aLookup;
  }
};

// Import describes a single wasm import. An ImportVector describes all
// of a single module's imports.
//
// ImportVector is built incrementally by ModuleGenerator and then stored
// immutably by Module.

struct Import {
  CacheableName module;
  CacheableName field;
  DefinitionKind kind;

  Import() = default;
  Import(CacheableName&& module, CacheableName&& field, DefinitionKind kind)
      : module(std::move(module)), field(std::move(field)), kind(kind) {}

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
};

using ImportVector = Vector<Import, 0, SystemAllocPolicy>;

// Export describes the export of a definition in a Module to a field in the
// export object. The Export stores the index of the exported item in the
// appropriate type-specific module data structure (function table, global
// table, table table, and - eventually - memory table).
//
// Note a single definition can be exported by multiple Exports in the
// ExportVector.
//
// ExportVector is built incrementally by ModuleGenerator and then stored
// immutably by Module.

class Export {
 public:
  struct CacheablePod {
    DefinitionKind kind_;
    uint32_t index_;

    WASM_CHECK_CACHEABLE_POD(kind_, index_);
  };

 private:
  CacheableName fieldName_;
  CacheablePod pod;

 public:
  Export() = default;
  explicit Export(CacheableName&& fieldName, uint32_t index,
                  DefinitionKind kind);

  const CacheableName& fieldName() const { return fieldName_; }

  DefinitionKind kind() const { return pod.kind_; }
  // The index of the exported item in the index space for its kind.
  uint32_t index() const { return pod.index_; }
  uint32_t funcIndex() const;
  uint32_t tagIndex() const;
  uint32_t memoryIndex() const;
  uint32_t globalIndex() const;
  uint32_t tableIndex() const;

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
  WASM_DECLARE_FRIEND_SERIALIZE(Export);
};

WASM_DECLARE_CACHEABLE_POD(Export::CacheablePod);

using ExportVector = Vector<Export, 0, SystemAllocPolicy>;

// FuncFlags provides metadata for a function definition.

enum class FuncFlags : uint8_t {
  None = 0x0,
  // The function maybe be accessible by JS and needs thunks generated for it.
  // See `[SMDOC] Exported wasm functions and the jit-entry stubs` in
  // WasmJS.cpp for more information.
  Exported = 0x1,
  // The function should have thunks generated upon instantiation, not upon
  // first call. May only be set if `Exported` is set.
  Eager = 0x2,
  // The function can be the target of a ref.func instruction in the code
  // section. May only be set if `Exported` is set.
  CanRefFunc = 0x4,
};

// A FuncDesc describes a single function definition.

struct FuncDesc {
  // Bit pack to keep this struct small on 32-bit systems
  uint32_t typeIndex : 24;
  FuncFlags flags : 8;

  WASM_CHECK_CACHEABLE_POD(typeIndex, flags);

  // Assert that the bit packing scheme is viable
  static_assert(MaxTypes <= (1 << 24) - 1);
  static_assert(sizeof(FuncFlags) == sizeof(uint8_t));

  FuncDesc() = default;
  explicit FuncDesc(uint32_t typeIndex)
      : typeIndex(typeIndex), flags(FuncFlags::None) {}

  void declareFuncExported(bool eager, bool canRefFunc) {
    // Set the `Exported` flag, if not set.
    flags = FuncFlags(uint8_t(flags) | uint8_t(FuncFlags::Exported));

    // Merge in the `Eager` and `CanRefFunc` flags, if they're set. Be sure
    // to not unset them if they've already been set.
    if (eager) {
      flags = FuncFlags(uint8_t(flags) | uint8_t(FuncFlags::Eager));
    }
    if (canRefFunc) {
      flags = FuncFlags(uint8_t(flags) | uint8_t(FuncFlags::CanRefFunc));
    }
  }

  bool isExported() const {
    return uint8_t(flags) & uint8_t(FuncFlags::Exported);
  }
  bool isEager() const { return uint8_t(flags) & uint8_t(FuncFlags::Eager); }
  bool canRefFunc() const {
    return uint8_t(flags) & uint8_t(FuncFlags::CanRefFunc);
  }
};

WASM_DECLARE_CACHEABLE_POD(FuncDesc);

using FuncDescVector = Vector<FuncDesc, 0, SystemAllocPolicy>;

struct CallRefMetricsRange {
  explicit CallRefMetricsRange() = default;
  explicit CallRefMetricsRange(uint32_t begin, uint32_t length)
      : begin(begin), length(length) {}

  uint32_t begin = 0;
  uint32_t length = 0;

  void offsetBy(uint32_t offset) { begin += offset; }

  WASM_CHECK_CACHEABLE_POD(begin, length);
};

struct AllocSitesRange {
  explicit AllocSitesRange() = default;
  explicit AllocSitesRange(uint32_t begin, uint32_t length)
      : begin(begin), length(length) {}

  uint32_t begin = 0;
  uint32_t length = 0;

  void offsetBy(uint32_t offset) { begin += offset; }

  WASM_CHECK_CACHEABLE_POD(begin, length);
};

// A compact plain data summary of CallRefMetrics for use by our function
// compilers. See CallRefMetrics in WasmInstanceData.h for more information.
//
// We cannot allow the metrics collected by an instance to directly be read
// from our function compilers because they contain thread-local data and are
// written into without any synchronization.
//
// Instead, CodeMetadata contains an array of CallRefHint that every instance
// writes into when it has a function that requests a tier-up. This array is
// 1:1 with the non-threadsafe CallRefMetrics that is stored on the instance.
//
// This class must be thread safe, as it's read and written from different
// threads.  It is an array of up to 3 function indices, and the entire array
// can be read/written atomically.  Each function index is represented in 20
// bits, and 2 of the remaining 4 bits are used to indicate the array's current
// size.
//
// Although unstated and unenforced here, it is expected that -- in the case
// where more than one function index is stored -- the func index at `.get(0)`
// is the "most important" in terms of inlining, that at `.get(1)` is the
// second most important, etc.
//
// Note that the fact that this array has 3 elements is unrelated to the value
// of CallRefMetrics::NUM_TRACKED.  The target-collection mechanism will work
// properly even if CallRefMetrics::NUM_TRACKED is greater than 3, in which
// case at most only 3 targets (probably the hottest ones) will get baked into
// the CallRefHint.
class CallRefHint {
 public:
  using Repr = uint64_t;
  static constexpr size_t NUM_ENTRIES = 3;

 private:
  // Representation is:
  //
  // 63  61   42  41   22  21    2  1    0
  // |   |     |  |     |  |     |  |    |
  // 00  index#2  index#1  index#0  length
  static constexpr uint32_t ElemBits = 20;
  static constexpr uint32_t LengthBits = 2;
  static constexpr uint64_t Mask = (uint64_t(1) << ElemBits) - 1;
  static_assert(js::wasm::MaxFuncs <= Mask);
  static_assert(3 * ElemBits + LengthBits <= 8 * sizeof(Repr));

  Repr state_ = 0;

  bool valid() const {
    // Shift out the length field and all of the entries that the length field
    // implies are occupied.  What remains should be all zeroes.
    return (state_ >> (length() * ElemBits + LengthBits)) == 0;
  }

 public:
  // We omit the obvious single-argument constructor that takes a `Repr`,
  // because that is too easily confused with one that takes a function index,
  // and in any case it is not necessary.

  uint32_t length() const { return state_ & 3; }
  bool empty() const { return length() == 0; }
  bool full() const { return length() == 3; }

  uint32_t get(uint32_t index) const {
    MOZ_RELEASE_ASSERT(index < length());
    uint64_t res = (state_ >> (index * ElemBits + LengthBits)) & Mask;
    return uint32_t(res);
  }
  void set(uint32_t index, uint32_t funcIndex) {
    MOZ_RELEASE_ASSERT(index < length());
    MOZ_ASSERT(funcIndex <= Mask);
    uint32_t shift = index * ElemBits + LengthBits;
    uint64_t c = uint64_t(Mask) << shift;
    uint64_t s = uint64_t(funcIndex) << shift;
    state_ = (state_ & ~c) | s;
  }

  void append(uint32_t funcIndex) {
    MOZ_RELEASE_ASSERT(!full());
    // We know the lowest two bits of `state_` are not 0b11, so we can
    // increment the length field by incrementing `state_` as a whole.
    state_++;
    set(length() - 1, funcIndex);
  }

  static CallRefHint fromRepr(Repr repr) {
    CallRefHint res;
    res.state_ = repr;
    MOZ_ASSERT(res.valid());
    return res;
  }
  Repr toRepr() const { return state_; }
};

static_assert(sizeof(CallRefHint) == sizeof(CallRefHint::Repr));

using MutableCallRefHint = mozilla::Atomic<CallRefHint::Repr>;
using MutableCallRefHints =
    mozilla::UniquePtr<MutableCallRefHint[], JS::FreePolicy>;

WASM_DECLARE_CACHEABLE_POD(CallRefMetricsRange);

using CallRefMetricsRangeVector =
    Vector<CallRefMetricsRange, 0, SystemAllocPolicy>;

WASM_DECLARE_CACHEABLE_POD(AllocSitesRange);

using AllocSitesRangeVector = Vector<AllocSitesRange, 0, SystemAllocPolicy>;

enum class BranchHint : uint8_t { Unlikely = 0, Likely = 1, Invalid = 2 };

// Stores pairs of <BranchOffset, BranchHint>
struct BranchHintEntry {
  uint32_t branchOffset;
  BranchHint value;

  BranchHintEntry() = default;
  BranchHintEntry(uint32_t branchOffset, BranchHint value)
      : branchOffset(branchOffset), value(value) {}
};

// Branch hint sorted vector for a function,
// stores tuples of <BranchOffset, BranchHint>
using BranchHintVector = Vector<BranchHintEntry, 0, SystemAllocPolicy>;
using BranchHintFuncMap = HashMap<uint32_t, BranchHintVector,
                                  DefaultHasher<uint32_t>, SystemAllocPolicy>;

struct BranchHintCollection {
 private:
  // Used for lookups into the collection if a function
  // doesn't contain any hints.
  static BranchHintVector invalidVector_;

  // Map from function index to their collection of branch hints
  BranchHintFuncMap branchHintsMap_;
  // Whether the module had branch hints, but we failed to parse them. This
  // is not semantically visible to user code, but used for internal testing.
  bool failedParse_ = false;

 public:
  // Add all the branch hints for a function
  [[nodiscard]] bool addHintsForFunc(uint32_t functionIndex,
                                     BranchHintVector&& branchHints) {
    return branchHintsMap_.put(functionIndex, std::move(branchHints));
  }

  // Return the vector with branch hints for a funcIndex.
  // If this function doesn't contain any hints, return an empty vector.
  BranchHintVector& getHintVector(uint32_t funcIndex) const {
    if (auto hintsVector =
            branchHintsMap_.readonlyThreadsafeLookup(funcIndex)) {
      return hintsVector->value();
    }

    // If not found, return the empty invalid Vector
    return invalidVector_;
  }

  bool isEmpty() const { return branchHintsMap_.empty(); }

  void setFailedAndClear() {
    failedParse_ = true;
    branchHintsMap_.clearAndCompact();
  }
  bool failedParse() const { return failedParse_; }
};

enum class GlobalKind { Import, Constant, Variable };

struct GlobalType {
  ValType type;
  bool isMutable = false;

  GlobalType() = default;
  GlobalType(ValType type, bool isMutable) : type(type), isMutable(isMutable) {}

  // Checks whether the `src` global type is compatible with the `dst` global
  // type. This is a subtyping relationship on the value type + extra rules for
  // mutability.
  // https://webassembly.github.io/spec/core/valid/matching.html#global-types
  static bool matches(GlobalType src, GlobalType dst);
};

// A GlobalDesc describes a single global variable.
//
// wasm can import and export mutable and immutable globals.
class GlobalDesc {
  GlobalKind kind_ = GlobalKind::Constant;
  // Stores the value type of this global for all kinds, and the initializer
  // expression when `constant` or `variable`.
  InitExpr initial_;
  // Metadata for the global when `variable` or `import`.
  unsigned offset_ = 0;
  bool isMutable_ = false;
  bool isExport_ = false;
  // Metadata for the global when `import`.
  uint32_t importIndex_ = 0;

  // Private, as they have unusual semantics.

  bool isExport() const { return !isConstant() && isExport_; }

 public:
  GlobalDesc() = default;

  explicit GlobalDesc(InitExpr&& initial, bool isMutable)
      : kind_((!isMutable && initial.isLiteral()) ? GlobalKind::Constant
                                                  : GlobalKind::Variable) {
    initial_ = std::move(initial);
    if (isVariable()) {
      isMutable_ = isMutable;
      isExport_ = false;
      offset_ = UINT32_MAX;
    }
  }

  explicit GlobalDesc(const GlobalType& type, uint32_t importIndex)
      : kind_(GlobalKind::Import) {
    initial_ = InitExpr(LitVal(type.type));
    importIndex_ = importIndex;
    isMutable_ = type.isMutable;
    isExport_ = false;
    offset_ = UINT32_MAX;
  }

  void setOffset(unsigned offset) {
    MOZ_ASSERT(!isConstant());
    MOZ_ASSERT(offset_ == UINT32_MAX);
    offset_ = offset;
  }
  unsigned offset() const {
    MOZ_ASSERT(!isConstant());
    MOZ_ASSERT(offset_ != UINT32_MAX);
    return offset_;
  }

  void setIsExport() {
    if (!isConstant()) {
      isExport_ = true;
    }
  }

  GlobalKind kind() const { return kind_; }
  bool isVariable() const { return kind_ == GlobalKind::Variable; }
  bool isConstant() const { return kind_ == GlobalKind::Constant; }
  bool isImport() const { return kind_ == GlobalKind::Import; }

  bool isMutable() const { return !isConstant() && isMutable_; }
  const InitExpr& initExpr() const {
    MOZ_ASSERT(!isImport());
    return initial_;
  }
  uint32_t importIndex() const {
    MOZ_ASSERT(isImport());
    return importIndex_;
  }

  LitVal constantValue() const { return initial_.literal(); }

  // If isIndirect() is true then storage for the value is not in the
  // instance's global area, but in a WasmGlobalObject::Cell hanging off a
  // WasmGlobalObject; the global area contains a pointer to the Cell.
  //
  // We don't want to indirect unless we must, so only mutable, exposed
  // globals are indirected - in all other cases we copy values into and out
  // of their module.
  //
  // Note that isIndirect() isn't equivalent to getting a WasmGlobalObject:
  // an immutable exported global will still get an object, but will not be
  // indirect.
  bool isIndirect() const { return isMutable() && (isImport() || isExport()); }

  ValType type() const { return initial_.type(); }
  GlobalType globalType() const { return GlobalType(type(), isMutable()); }

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
  WASM_DECLARE_FRIEND_SERIALIZE(GlobalDesc);
};

using GlobalDescVector = Vector<GlobalDesc, 0, SystemAllocPolicy>;

// A TagDesc represents fresh per-instance tags that are used for the
// exception handling proposal and potentially other future proposals.

// The TagOffsetVector represents the offsets in the layout of the
// data buffer stored in a Wasm exception.
using TagOffsetVector = Vector<uint32_t, 2, SystemAllocPolicy>;

class TagType : public AtomicRefCounted<TagType> {
  SharedTypeDef type_;
  TagOffsetVector exceptionArgOffsets_;
  uint32_t size_;

 public:
  TagType() : size_(0) {}

  [[nodiscard]] bool initialize(const SharedTypeDef& funcType);

  const TypeDef& type() const { return *type_; }
  const ValTypeVector& argTypes() const { return type_->funcType().args(); }
  const ValTypeVector& resultTypes() const {
    return type_->funcType().results();
  }

  // When this tag is used for WasmExceptionObject, what offset does each
  // argument reside in.
  const TagOffsetVector& exceptionArgOffsets() const {
    return exceptionArgOffsets_;
  }

  ResultType argResultType() const { return ResultType::Vector(argTypes()); }

  uint32_t tagSize() const { return size_; }

  static bool matches(const TagType& a, const TagType& b) {
    // Note that this does NOT use subtyping. This is deliberate per the spec.
    return a.type_ == b.type_;
  }

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
  WASM_DECLARE_FRIEND_SERIALIZE(TagType);
};

using MutableTagType = RefPtr<TagType>;
using SharedTagType = RefPtr<const TagType>;

struct TagDesc {
  TagKind kind = TagKind::Exception;
  SharedTagType type;
  bool isExport;

  TagDesc() : isExport(false) {}
  TagDesc(TagKind kind, const SharedTagType& type, bool isExport = false)
      : kind(kind), type(type), isExport(isExport) {}

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
};

using TagDescVector = Vector<TagDesc, 0, SystemAllocPolicy>;

#ifdef ENABLE_WASM_JSPI

class HandlerExpr {
  uint32_t tagIndex_;
  uint32_t labelDepth_;

  static constexpr uint32_t IsSwitch = UINT32_MAX;

 public:
  explicit HandlerExpr(uint32_t tagIndex)
      : tagIndex_(tagIndex), labelDepth_(IsSwitch) {
    MOZ_ASSERT(isSwitch());
  }
  HandlerExpr(uint32_t tagIndex, uint32_t labelDepth)
      : tagIndex_(tagIndex), labelDepth_(labelDepth) {
    MOZ_ASSERT(!isSwitch());
  }

  uint32_t tagIndex() const { return tagIndex_; }
  bool isSwitch() const { return labelDepth_ == IsSwitch; }
  uint32_t labelDepth() const {
    MOZ_ASSERT(!isSwitch());
    return labelDepth_;
  }
};

using HandlerExprVector = Vector<HandlerExpr, 2, SystemAllocPolicy>;

#endif  // ENABLE_WASM_JSPI

// This holds info about elem segments that is needed for instantiation.  It
// can be dropped when the associated wasm::Module is dropped.
struct ModuleElemSegment {
  enum class Kind {
    Active,
    Passive,
    Declared,
  };

  // The type of encoding used by this element segment. 0 is an invalid value to
  // make sure we notice if we fail to correctly initialize the element segment
  // - reading from the wrong representation could be a bad time.
  enum class Encoding {
    Indices = 1,
    Expressions,
  };

  struct Expressions {
    size_t count = 0;
    Bytes exprBytes;

    size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
  };

  Kind kind;
  uint32_t tableIndex;
  RefType elemType;
  mozilla::Maybe<InitExpr> offsetIfActive;

  // We store either an array of indices or the full bytecode of the element
  // expressions, depending on the encoding used for the element segment.
  Encoding encoding;
  Uint32Vector elemIndices;
  Expressions elemExpressions;

  bool active() const { return kind == Kind::Active; }

  const InitExpr& offset() const { return *offsetIfActive; }

  size_t numElements() const {
    switch (encoding) {
      case Encoding::Indices:
        return elemIndices.length();
      case Encoding::Expressions:
        return elemExpressions.count;
      default:
        MOZ_CRASH("unknown element segment encoding");
    }
  }

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
};

using ModuleElemSegmentVector = Vector<ModuleElemSegment, 0, SystemAllocPolicy>;

using InstanceElemSegment = GCVector<HeapPtr<AnyRef>, 0, SystemAllocPolicy>;
using InstanceElemSegmentVector =
    GCVector<InstanceElemSegment, 0, SystemAllocPolicy>;

// DataSegmentRange holds the initial results of decoding a data segment from
// the bytecode and is stored in the ModuleMetadata.  It contains the bytecode
// bounds of the data segment, and some auxiliary information, but not the
// segment contents itself.
//
// When compilation completes, each DataSegmentRange is transformed into a
// DataSegment, which are also stored in the ModuleMetadata.  DataSegment
// contains the same information as DataSegmentRange but additionally contains
// the segment contents itself.  This allows non-compilation uses of wasm
// validation to avoid expensive copies.
//
// A DataSegment that is "passive" is shared between a ModuleMetadata and its
// wasm::Instances.  To allow each segment to be released as soon as the last
// Instance mem.drops it and the Module (hence, also the ModuleMetadata) is
// destroyed, each DataSegment is individually atomically ref-counted.

constexpr uint32_t InvalidMemoryIndex = UINT32_MAX;
static_assert(InvalidMemoryIndex > MaxMemories, "Invariant");

struct DataSegmentRange {
  uint32_t memoryIndex;
  mozilla::Maybe<InitExpr> offsetIfActive;
  uint32_t bytecodeOffset;
  uint32_t length;
};

using DataSegmentRangeVector = Vector<DataSegmentRange, 0, SystemAllocPolicy>;

struct DataSegment : AtomicRefCounted<DataSegment> {
  uint32_t memoryIndex;
  mozilla::Maybe<InitExpr> offsetIfActive;
  Bytes bytes;

  DataSegment() = default;

  bool active() const { return !!offsetIfActive; }

  const InitExpr& offset() const { return *offsetIfActive; }

  [[nodiscard]] bool init(const BytecodeSource& bytecode,
                          const DataSegmentRange& src) {
    memoryIndex = src.memoryIndex;
    if (src.offsetIfActive) {
      offsetIfActive.emplace();
      if (!offsetIfActive->clone(*src.offsetIfActive)) {
        return false;
      }
    }
    MOZ_ASSERT(bytes.length() == 0);
    BytecodeSpan span =
        bytecode.getSpan(BytecodeRange(src.bytecodeOffset, src.length));
    return bytes.append(span.data(), span.size());
  }

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
};

using MutableDataSegment = RefPtr<DataSegment>;
using SharedDataSegment = RefPtr<const DataSegment>;
using DataSegmentVector = Vector<SharedDataSegment, 0, SystemAllocPolicy>;

// CustomSectionRange and CustomSection are related in the same way that
// DataSegmentRange and DataSegment are: the CustomSectionRanges are stored in
// the ModuleMetadata, and are transformed into CustomSections at the end of
// compilation and stored in wasm::Module.

struct CustomSectionRange {
  BytecodeRange name;
  BytecodeRange payload;

  WASM_CHECK_CACHEABLE_POD(name, payload);
};

WASM_DECLARE_CACHEABLE_POD(CustomSectionRange);

using CustomSectionRangeVector =
    Vector<CustomSectionRange, 0, SystemAllocPolicy>;

struct CustomSection {
  Bytes name;
  SharedBytes payload;

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
};

using CustomSectionVector = Vector<CustomSection, 0, SystemAllocPolicy>;

// A Name represents a string of utf8 chars embedded within the name custom
// section. The offset of a name is expressed relative to the beginning of the
// name section's payload so that Names can stored in wasm::Code, which only
// holds the name section's bytes, not the whole bytecode.
struct Name {
  // All fields are treated as cacheable POD:
  uint32_t offsetInNamePayload;
  uint32_t length;

  WASM_CHECK_CACHEABLE_POD(offsetInNamePayload, length);

  Name() : offsetInNamePayload(UINT32_MAX), length(0) {}

  bool valid() const { return offsetInNamePayload != UINT32_MAX; }
};

WASM_DECLARE_CACHEABLE_POD(Name);

struct IndirectNameKey {
  uint32_t outer;
  uint32_t inner;

  WASM_CHECK_CACHEABLE_POD(outer, inner);

  IndirectNameKey() : outer(UINT32_MAX), inner(UINT32_MAX) {}
  IndirectNameKey(uint32_t outer, uint32_t inner)
      : outer(outer), inner(inner) {}

  bool operator==(const IndirectNameKey& rhs) const = default;
};
WASM_DECLARE_CACHEABLE_POD(IndirectNameKey);

struct IndirectNameKeyHasher {
  using Lookup = IndirectNameKey;

  static HashNumber hash(const Lookup& l) {
    return mozilla::HashGeneric(l.outer, l.inner);
  }
  static bool match(const IndirectNameKey& key, const Lookup& l) {
    return key == l;
  }
};

using NameMap =
    HashMap<uint32_t, Name, DefaultHasher<uint32_t>, SystemAllocPolicy>;
using IndirectNameMap =
    HashMap<IndirectNameKey, Name, IndirectNameKeyHasher, SystemAllocPolicy>;

struct NameSection {
  Name moduleName;
  NameMap funcNames;
  // We have no need for other name data at the moment, but more maps can be
  // added here.

  uint32_t customSectionIndex;

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
};

// The kind of limits to decode or convert from JS.

enum class LimitsKind {
  Memory,
  Table,
};

extern const char* ToString(LimitsKind kind);

// Represents the resizable limits of memories and tables.

struct Limits {
  // `addressType` may be I64 when memory64 is enabled.
  AddressType addressType;

  // The initial and maximum limit. The unit is pages for memories and elements
  // for tables.
  uint64_t initial;
  mozilla::Maybe<uint64_t> maximum;

  // `shared` is Shareable::False for tables but may be Shareable::True for
  // memories.
  Shareable shared;

  // `pageSize` is used only for memories. Defaults to the standard page size
  // but may be set to other values with the custom page size proposal.
  PageSize pageSize = PageSize::Standard;

  WASM_CHECK_CACHEABLE_POD(addressType, initial, maximum, shared, pageSize);

  Limits() = default;
  Limits(uint64_t initial, const mozilla::Maybe<uint64_t>& maximum,
         Shareable shared, PageSize pageSize)
      : addressType(AddressType::I32),
        initial(initial),
        maximum(maximum),
        shared(shared),
        pageSize(pageSize) {}

  // Checks whether the `src` limits are compatible with the `dst` limits; i.e.
  // that a memory or table with the `src` limits can be used in a context where
  // the `dst` limits are expected. This also checks the memory-only `shared`
  // and `pageSize` fields, which are always the same for tables:
  // https://webassembly.github.io/spec/core/valid/matching.html#match-limits
  // https://webassembly.github.io/spec/core/valid/matching.html#memory-types
  static bool matches(Limits src, Limits dst);
};

WASM_DECLARE_CACHEABLE_POD(Limits);

// MemoryDesc describes a memory.

struct MemoryDesc {
  // The limits of this memory
  Limits limits;
  // The index of the import if this memory is imported
  mozilla::Maybe<uint32_t> importIndex;

  WASM_CHECK_CACHEABLE_POD(limits, importIndex);

  bool isShared() const { return limits.shared == Shareable::True; }

  // Whether a backing store for this memory may move when grown.
  bool canMovingGrow() const { return limits.maximum.isNothing(); }

  // Whether the boundsCheckLimit will always fit within 32 bits. See the SMDOC
  // for "WASM Linear Memory structure".
  bool boundsCheckLimitIsAlways32Bits() const {
    return limits.maximum.isSome() &&
           limits.maximum.value() < (0x100000000 / PageSizeInBytes(pageSize()));
  }

  AddressType addressType() const { return limits.addressType; }

  PageSize pageSize() const { return limits.pageSize; }

  // The initial length of this memory in pages.
  Pages initialPages() const {
    return Pages::fromPageCount(limits.initial, pageSize());
  }

  // The maximum length of this memory in pages.
  mozilla::Maybe<Pages> maximumPages() const {
    return limits.maximum.map(
        [&](uint64_t x) { return Pages::fromPageCount(x, pageSize()); });
  }

  uint64_t initialLength() const {
    // See static_assert after MemoryDesc for why this is safe for memory32.
    MOZ_ASSERT_IF(addressType() == AddressType::I64,
                  limits.initial <= UINT64_MAX / PageSizeInBytes(pageSize()));
    return addressType() == AddressType::I64 ? initialPages().byteLength64()
                                             : initialPages().byteLength();
  }

  MemoryDesc() = default;
  explicit MemoryDesc(Limits limits)
      : limits(limits), importIndex(mozilla::Nothing()) {}
};

WASM_DECLARE_CACHEABLE_POD(MemoryDesc);

using MemoryDescVector = Vector<MemoryDesc, 1, SystemAllocPolicy>;

// We never need to worry about overflow with a Memory32 field when
// using a uint64_t.
static_assert(MaxMemory32StandardPagesValidation <=
              UINT64_MAX / StandardPageSizeBytes);
#ifdef ENABLE_WASM_CUSTOM_PAGE_SIZES
static_assert(MaxMemory32TinyPagesValidation <= UINT64_MAX);
#endif

struct TableType {
  Limits limits;
  RefType elemType;

  TableType() = default;
  TableType(Limits limits, RefType elemType)
      : limits(limits), elemType(elemType) {}

  // Checks if the `src` table type is compatible with the `dst` table type,
  // e.g. for imports:
  // https://webassembly.github.io/spec/core/valid/matching.html#table-types
  static bool matches(TableType src, TableType dst);
};

struct TableDesc {
  TableType type;

  bool isImported = false;
  bool isExported = false;
  mozilla::Maybe<InitExpr> initExpr;

  TableDesc() = default;
  TableDesc(const TableType& type, mozilla::Maybe<InitExpr>&& initExpr,
            bool isImported = false, bool isExported = false)
      : type(type),
        isImported(isImported),
        isExported(isExported),
        initExpr(std::move(initExpr)) {}

  AddressType addressType() const { return type.limits.addressType; }

  uint64_t initialLength() const { return type.limits.initial; }

  mozilla::Maybe<uint64_t> maximumLength() const { return type.limits.maximum; }

  RefType elemType() const { return type.elemType; }
};

using TableDescVector = Vector<TableDesc, 0, SystemAllocPolicy>;

// The type of an item that can be imported into or exported from a core
// module, independent of any particular module:
// https://webassembly.github.io/spec/core/syntax/types.html#external-types
//
// Value types within the extern type refer to TypeDefs by raw pointer, so the
// owner of this type must keep the relevant type context alive.
class CoreExternType {
  using Payload = mozilla::Variant<SharedTypeDef, TableType, Limits, GlobalType,
                                   SharedTagType>;
  Payload payload_;

  explicit CoreExternType(Payload&& payload) : payload_(std::move(payload)) {}

 public:
  static CoreExternType func(const TypeDef& funcType) {
    return CoreExternType(Payload(SharedTypeDef(&funcType)));
  }
  static CoreExternType table(TableType tableType) {
    return CoreExternType(Payload(tableType));
  }
  static CoreExternType memory(Limits limits) {
    return CoreExternType(Payload(limits));
  }
  static CoreExternType global(GlobalType globalType) {
    return CoreExternType(Payload(globalType));
  }
  static CoreExternType tag(const SharedTagType& tagType) {
    return CoreExternType(Payload(tagType));
  }

  DefinitionKind kind() const;

  const TypeDef& asFunc() const { return *payload_.as<SharedTypeDef>(); }
  TableType asTable() const { return payload_.as<TableType>(); }
  Limits asMemory() const { return payload_.as<Limits>(); }
  GlobalType asGlobal() const { return payload_.as<GlobalType>(); }
  const TagType& asTag() const { return *payload_.as<SharedTagType>(); }

  // Checks whether an item of the `src` type can be used where the `dst` type
  // is expected, e.g. for imports:
  // https://webassembly.github.io/spec/core/valid/matching.html#external-types
  static bool matches(const CoreExternType& src, const CoreExternType& dst);
};

// The type of a core module instance, which is just its named exports.
class CoreInstanceType : public AtomicRefCounted<CoreInstanceType> {
  using ExportMap = HashMap<CacheableName, CoreExternType, CacheableNameHasher,
                            SystemAllocPolicy>;
  ExportMap exports_;

 public:
  [[nodiscard]] bool addExport(CacheableName&& name, CoreExternType&& type);
  mozilla::Maybe<const CoreExternType&> getExport(
      const CacheableName& name) const;
};

using MutableCoreInstanceType = RefPtr<CoreInstanceType>;
using SharedCoreInstanceType = RefPtr<const CoreInstanceType>;

struct CoreImportType {
  CacheableName module;
  CacheableName field;
  CoreExternType type;
};

using CoreImportTypeVector = Vector<CoreImportType, 0, SystemAllocPolicy>;

// The type of a core module: its imports in order, and the type of the
// instances it produces.
class CoreModuleType : public AtomicRefCounted<CoreModuleType> {
  // Keeps alive all TypeDefs referenced by the imports and exports.
  SharedTypeContext types_;
  CoreImportTypeVector imports_;
  SharedCoreInstanceType instanceType_;

 public:
  CoreModuleType(SharedTypeContext types, CoreImportTypeVector&& imports,
                 SharedCoreInstanceType instanceType)
      : types_(std::move(types)),
        imports_(std::move(imports)),
        instanceType_(std::move(instanceType)) {}

  const CoreImportTypeVector& imports() const { return imports_; }
  const SharedCoreInstanceType& instanceType() const { return instanceType_; }
};

using SharedCoreModuleType = RefPtr<const CoreModuleType>;

}  // namespace wasm
}  // namespace js

#endif  // wasm_module_types_h
