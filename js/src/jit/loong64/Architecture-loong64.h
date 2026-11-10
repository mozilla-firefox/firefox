/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_loong64_Architecture_loong64_h
#define jit_loong64_Architecture_loong64_h

#include "mozilla/EnumSet.h"

#include <algorithm>
#include <bit>

#include "jit/shared/Architecture-shared.h"
#include "js/Utility.h"

namespace js {
namespace jit {

// LoongArch64 has 32 64-bit integer registers, r0 though r31.
//  The program counter is not accessible as a register.
//
// SIMD and scalar floating-point registers share a register bank.
//  Floating-point registers are f0 through f31.
//  128 bit SIMD registers are vr0 through vr31.
//  e.g., f0 is the bottom 64 bits of vr0.

// LoongArch64 INT Register Convention:
//  Name         Alias           Usage
//  $r0          $zero           Constant zero
//  $r1          $ra             Return address
//  $r2          $tp             TLS
//  $r3          $sp             Stack pointer
//  $r4-$r11     $a0-$a7         Argument registers
//  $r12-$r20    $t0-$t8         Temporary registers
//  $r21         $x              Reserved
//  $r22         $fp             Frame pointer
//  $r23-$r31    $s0-$s8         Callee-saved registers

// LoongArch64 FP Register Convention:
//  Name         Alias           Usage
//  $f0-$f7      $fa0-$fa7       Argument registers
//  $f0-$f1      $fv0-$fv1       Return values
//  $f8-f23      $ft0-$ft15      Temporary registers
//  $f24-$f31    $fs0-$fs7       Callee-saved registers

class Registers {
 public:
  enum RegisterID {
    r0 = 0,
    r1,
    r2,
    r3,
    r4,
    r5,
    r6,
    r7,
    r8,
    r9,
    r10,
    r11,
    r12,
    r13,
    r14,
    r15,
    r16,
    r17,
    r18,
    r19,
    r20,
    r21,
    r22,
    r23,
    r24,
    r25,
    r26,
    r27,
    r28,
    r29,
    r30,
    r31,
    zero = r0,
    ra = r1,
    tp = r2,
    sp = r3,
    a0 = r4,
    a1 = r5,
    a2 = r6,
    a3 = r7,
    a4 = r8,
    a5 = r9,
    a6 = r10,
    a7 = r11,
    t0 = r12,
    t1 = r13,
    t2 = r14,
    t3 = r15,
    t4 = r16,
    t5 = r17,
    t6 = r18,
    t7 = r19,
    t8 = r20,
    rx = r21,
    fp = r22,
    s0 = r23,
    s1 = r24,
    s2 = r25,
    s3 = r26,
    s4 = r27,
    s5 = r28,
    s6 = r29,
    s7 = r30,
    s8 = r31,
    invalid_reg,
  };
  typedef uint8_t Code;
  typedef RegisterID Encoding;
  typedef uint32_t SetType;

  static const Encoding StackPointer = sp;
  static const Encoding Invalid = invalid_reg;

  // Content spilled during bailouts.
  union RegisterContent {
    uintptr_t r;
  };

  static uint32_t SetSize(SetType x) { return std::popcount(x); }
  static uint32_t FirstBit(SetType x) {
    MOZ_ASSERT(x);
    return std::countr_zero(x);
  }
  static uint32_t LastBit(SetType x) {
    MOZ_ASSERT(x);
    return std::bit_width(x) - 1;
  }

  static const char* GetName(uint32_t code) {
    static const char* const Names[] = {
        "$zero", "$ra", "$tp", "$sp", "$a0", "$a1", "$a2", "$a3",
        "$a4",   "$a5", "$a6", "$a7", "$t0", "$t1", "$t2", "$t3",
        "$t4",   "$t5", "$t6", "$t7", "$t8", "$rx", "$fp", "$s0",
        "$s1",   "$s2", "$s3", "$s4", "$s5", "$s6", "$s7", "$s8"};
    static_assert(Total == std::size(Names), "Table is the correct size");
    if (code >= Total) {
      return "invalid";
    }
    return Names[code];
  }

  static Code FromName(const char* name);

  static const uint32_t Total = 32;
  static const uint32_t TotalPhys = 32;
  static const uint32_t Allocatable =
      23;  // No named special-function registers.

  static const SetType AllMask = 0xFFFFFFFF;
  static const SetType NoneMask = 0x0;

  static const SetType ArgRegMask =
      (1U << Registers::a0) | (1U << Registers::a1) | (1U << Registers::a2) |
      (1U << Registers::a3) | (1U << Registers::a4) | (1U << Registers::a5) |
      (1U << Registers::a6) | (1U << Registers::a7);

  static const SetType VolatileMask =
      (1U << Registers::a0) | (1U << Registers::a1) | (1U << Registers::a2) |
      (1U << Registers::a3) | (1U << Registers::a4) | (1U << Registers::a5) |
      (1U << Registers::a6) | (1U << Registers::a7) | (1U << Registers::t0) |
      (1U << Registers::t1) | (1U << Registers::t2) | (1U << Registers::t3) |
      (1U << Registers::t4) | (1U << Registers::t5) | (1U << Registers::t6) |
      (1U << Registers::t7) | (1U << Registers::t8);

  // We use this constant to save registers when entering functions. This
  // is why $ra is added here even though it is not "Non Volatile".
  static const SetType NonVolatileMask =
      (1U << Registers::ra) | (1U << Registers::fp) | (1U << Registers::s0) |
      (1U << Registers::s1) | (1u << Registers::s2) | (1U << Registers::s3) |
      (1U << Registers::s4) | (1U << Registers::s5) | (1U << Registers::s6) |
      (1U << Registers::s7) | (1U << Registers::s8);

  static const SetType NonAllocatableMask =
      (1U << Registers::zero) |  // Always be zero.
      (1U << Registers::t6) |    // Scratch register.
      (1U << Registers::t7) |    // Scratch register.
      (1U << Registers::t8) |    // Scratch register.
      (1U << Registers::s8) |    // Saved scratch register.
      (1U << Registers::rx) |    // Reserved Register.
      (1U << Registers::ra) | (1U << Registers::tp) | (1U << Registers::sp) |
      (1U << Registers::fp);

  static const SetType WrapperMask = VolatileMask;

  // Registers returned from a JS -> JS call.
  static const SetType JSCallMask = (1 << Registers::a2);

  // Registers returned from a JS -> C call.
  static const SetType CallMask = (1 << Registers::a0);

  static const SetType AllocatableMask = AllMask & ~NonAllocatableMask;
};

// Smallest integer type that can hold a register bitmask.
typedef uint32_t PackedRegisterMask;

template <typename T>
class TypedRegisterSet;

// 128-bit bitset for FloatRegisters::SetType.
// From arm64's Bitset128.
class Bitset128 {
  // The order (hi, lo) looks best in the debugger.
  uint64_t hi, lo;

 public:
  MOZ_IMPLICIT constexpr Bitset128(uint64_t initial) : hi(0), lo(initial) {}
  MOZ_IMPLICIT constexpr Bitset128(const Bitset128& that) = default;

  constexpr Bitset128(uint64_t hi, uint64_t lo) : hi(hi), lo(lo) {}

  constexpr uint64_t high() const { return hi; }

  constexpr uint64_t low() const { return lo; }

  constexpr Bitset128 operator|(Bitset128 that) const {
    return Bitset128(hi | that.hi, lo | that.lo);
  }

  constexpr Bitset128 operator&(Bitset128 that) const {
    return Bitset128(hi & that.hi, lo & that.lo);
  }

  constexpr Bitset128 operator^(Bitset128 that) const {
    return Bitset128(hi ^ that.hi, lo ^ that.lo);
  }

  constexpr Bitset128 operator~() const { return Bitset128(~hi, ~lo); }

  // We must avoid shifting by the word width, which is complex.  Inlining plus
  // shift-by-constant will remove a lot of code in the normal case.

  constexpr Bitset128 operator<<(size_t shift) const {
    if (shift == 0) {
      return *this;
    }
    if (shift < 64) {
      return Bitset128((hi << shift) | (lo >> (64 - shift)), lo << shift);
    }
    if (shift == 64) {
      return Bitset128(lo, 0);
    }
    return Bitset128(lo << (shift - 64), 0);
  }

  constexpr Bitset128 operator>>(size_t shift) const {
    if (shift == 0) {
      return *this;
    }
    if (shift < 64) {
      return Bitset128(hi >> shift, (lo >> shift) | (hi << (64 - shift)));
    }
    if (shift == 64) {
      return Bitset128(0, hi);
    }
    return Bitset128(0, hi >> (shift - 64));
  }

  constexpr bool operator==(Bitset128 that) const {
    return lo == that.lo && hi == that.hi;
  }

  constexpr bool operator!=(Bitset128 that) const {
    return lo != that.lo || hi != that.hi;
  }

  constexpr bool operator!() const { return (hi | lo) == 0; }

  Bitset128& operator|=(const Bitset128& that) {
    hi |= that.hi;
    lo |= that.lo;
    return *this;
  }

  Bitset128& operator&=(const Bitset128& that) {
    hi &= that.hi;
    lo &= that.lo;
    return *this;
  }

  uint32_t size() const { return std::popcount(hi) + std::popcount(lo); }

  uint32_t countTrailingZeroes() const {
    if (lo) {
      return std::countr_zero(lo);
    }
    return std::countr_zero(hi) + 64;
  }

  uint32_t countLeadingZeroes() const {
    if (hi) {
      return std::countl_zero(hi);
    }
    return std::countl_zero(lo) + 64;
  }

  uint32_t bitWidth() const { return 128 - countLeadingZeroes(); }
};

class FloatRegisters {
 public:
  enum FPRegisterID {
    f0 = 0,
    f1,
    f2,
    f3,
    f4,
    f5,
    f6,
    f7,
    f8,
    f9,
    f10,
    f11,
    f12,
    f13,
    f14,
    f15,
    f16,
    f17,
    f18,
    f19,
    f20,
    f21,
    f22,
    f23,  // Scratch register.
    f24,
    f25,
    f26,
    f27,
    f28,
    f29,
    f30,
    f31,
  };

  // Eight bits: (invalid << 7) | (kind << 5) | encoding
  typedef uint8_t Code;
  typedef FPRegisterID Encoding;
  typedef Bitset128 SetType;

#if defined(ENABLE_JIT_SIMD)
  enum Kind : uint8_t { Double, Single, Simd128, NumTypes };
#else
  enum Kind : uint8_t { Double, Single, NumTypes };
#endif

  static constexpr Code Invalid = 0x80;

  static const char* GetName(uint32_t code) {
    static const char* const FPUNames[] = {
        "$f0",  "$f1",  "$f2",  "$f3",  "$f4",  "$f5",  "$f6",  "$f7",
        "$f8",  "$f9",  "$f10", "$f11", "$f12", "$f13", "$f14", "$f15",
        "$f16", "$f17", "$f18", "$f19", "$f20", "$f21", "$f22", "$f23",
        "$f24", "$f25", "$f26", "$f27", "$f28", "$f29", "$f30", "$f31"};
    static_assert(TotalPhys == std::size(FPUNames),
                  "Table is the correct size");

#if defined(ENABLE_JIT_SIMD)
    static const char* const VectorNames[] = {
        "$vr0",  "$vr1",  "$vr2",  "$vr3",  "$vr4",  "$vr5",  "$vr6",  "$vr7",
        "$vr8",  "$vr9",  "$vr10", "$vr11", "$vr12", "$vr13", "$vr14", "$vr15",
        "$vr16", "$vr17", "$vr18", "$vr19", "$vr20", "$vr21", "$vr22", "$vr23",
        "$vr24", "$vr25", "$vr26", "$vr27", "$vr28", "$vr29", "$vr30", "$vr31"};
    static_assert(TotalPhys == std::size(VectorNames),
                  "Table is the correct size");
#endif

    if (code >= Total) {
      return "invalid";
    }
#if defined(ENABLE_JIT_SIMD)
    if (kind(Code(code)) == Simd128) {
      return VectorNames[encoding(Code(code))];
    }
#endif
    // Single and double fpu registers share same names on LoongArch.
    return FPUNames[encoding(Code(code))];
  }

  static Code FromName(const char* name);

  static const uint32_t TotalPhys = 32;
  static const uint32_t Total = TotalPhys * NumTypes;
  // Without $f22 and $f23, the scratch registers.
  static const uint32_t Allocatable = 30;

  static_assert(sizeof(SetType) * 8 >= Total,
                "SetType should be large enough to enumerate all registers.");

  static constexpr unsigned ShiftDouble = uint32_t(Double) * TotalPhys;
  static constexpr unsigned ShiftSingle = uint32_t(Single) * TotalPhys;
#if defined(ENABLE_JIT_SIMD)
  static constexpr unsigned ShiftSimd128 = uint32_t(Simd128) * TotalPhys;
#endif

  static constexpr SetType NoneMask = SetType(0);
  static constexpr SetType AllPhysMask = ~(~SetType(0) << TotalPhys);
  static constexpr SetType AllDoubleMask = AllPhysMask << ShiftDouble;
  static constexpr SetType AllSingleMask = AllPhysMask << ShiftSingle;
#if defined(ENABLE_JIT_SIMD)
  static constexpr SetType AllSimd128Mask = AllPhysMask << ShiftSimd128;
#endif

#if defined(ENABLE_JIT_SIMD)
  static constexpr SetType AllMask =
      AllDoubleMask | AllSingleMask | AllSimd128Mask;
  static constexpr SetType AliasMask = (SetType(1) << ShiftDouble) |
                                       (SetType(1) << ShiftSingle) |
                                       (SetType(1) << ShiftSimd128);
#else
  static constexpr SetType AllMask = AllDoubleMask | AllSingleMask;
  static constexpr SetType AliasMask =
      (SetType(1) << ShiftDouble) | (SetType(1) << ShiftSingle);
#endif

  // The 64-bit views of $f24-$f31 are preserved across calls, but the 128-bit
  // views are not. E.g., GCC marks FP values wider than 8 bytes as partially
  // clobbered (loongarch_hard_regno_call_part_clobbered) and LLVM's preserved
  // sets contain no vector register.
  // TODO(loong64): Much less than ARM64 here.
  static constexpr SetType NonVolatileSingleMask =
      SetType((1U << FloatRegisters::f24) | (1U << FloatRegisters::f25) |
              (1U << FloatRegisters::f26) | (1U << FloatRegisters::f27) |
              (1U << FloatRegisters::f28) | (1U << FloatRegisters::f29) |
              (1U << FloatRegisters::f30) | (1U << FloatRegisters::f31));
  static constexpr SetType NonVolatileMask =
      (NonVolatileSingleMask << ShiftDouble) |
      (NonVolatileSingleMask << ShiftSingle);

  static constexpr SetType VolatileMask = AllMask & ~NonVolatileMask;

  static constexpr SetType WrapperMask = VolatileMask;

  // $f22 and $f23 are the scratch registers.
  static constexpr SetType NonAllocatableSingleMask =
      SetType((1U << FloatRegisters::f22) | (1U << FloatRegisters::f23));
#if defined(ENABLE_JIT_SIMD)
  static constexpr SetType NonAllocatableMask =
      (NonAllocatableSingleMask << ShiftDouble) |
      (NonAllocatableSingleMask << ShiftSingle) |
      (NonAllocatableSingleMask << ShiftSimd128);
#else
  static constexpr SetType NonAllocatableMask =
      (NonAllocatableSingleMask << ShiftDouble) |
      (NonAllocatableSingleMask << ShiftSingle);
#endif

  static constexpr SetType AllocatableMask = AllMask & ~NonAllocatableMask;

  // Content spilled during bailouts.
  union RegisterContent {
    float s;
    double d;
#if defined(ENABLE_JIT_SIMD)
    uint8_t v128[16];
#endif
  };

  static constexpr Encoding encoding(Code c) {
    // assert() not available in constexpr function.
    // assert(c < Total);
    return Encoding(c & 31);
  }

  static constexpr Kind kind(Code c) {
    // assert() not available in constexpr function.
    // assert(c < Total && ((c >> 5) & 3) < NumTypes);
    return Kind((c >> 5) & 3);
  }

  static constexpr Code fromParts(uint32_t encoding, uint32_t kind,
                                  uint32_t invalid) {
    return Code((invalid << 7) | (kind << 5) | encoding);
  }
};

static const uint32_t SpillSlotSize =
    std::max(sizeof(Registers::RegisterContent),
             sizeof(FloatRegisters::RegisterContent));

static constexpr uint32_t ShadowStackSpace = 0;
static const uint32_t SizeOfReturnAddressAfterCall = 0;

// This should be minRange(callWithPatch, farJumpWithPatch) == minRange(+/-128
// GiB, +/-2 GiB).
static const uint32_t JumpImmediateRange = INT32_MAX;

struct VRegister;

struct FloatRegister {
  typedef FloatRegisters Codes;
  typedef size_t Code;
  typedef Codes::Encoding Encoding;
  typedef Codes::SetType SetType;

  static uint32_t SetSize(SetType x) {
    x |= x >> FloatRegisters::TotalPhys;
#if defined(ENABLE_JIT_SIMD)
    // SIMD registers
    x |= x >> FloatRegisters::TotalPhys;
#endif
    x &= FloatRegisters::AllPhysMask;
    return x.size();
  }

  static uint32_t FirstBit(SetType x) {
    MOZ_ASSERT(x);
    return x.countTrailingZeroes();
  }
  static uint32_t LastBit(SetType x) {
    MOZ_ASSERT(x);
    return x.bitWidth() - 1;
  }

  static constexpr size_t SizeOfSimd128 = 16;

 private:
  // These fields only hold valid values: an invalid register is always
  // represented as a valid encoding and kind with the invalid_ bit set.
  uint8_t encoding_;  // 32 encodings
  uint8_t kind_;      // Double, Single, Simd128
  bool invalid_;

  typedef Codes::Kind Kind;

 public:
  constexpr FloatRegister(Encoding encoding, Kind kind)
      : encoding_(encoding), kind_(kind), invalid_(false) {
    // assert(uint32_t(encoding) < Codes::TotalPhys);
  }

  constexpr FloatRegister()
      : encoding_(0), kind_(FloatRegisters::Double), invalid_(true) {}

  static FloatRegister FromCode(uint32_t i) {
    MOZ_ASSERT(i < Codes::Total);
    return FloatRegister(FloatRegisters::encoding(i), FloatRegisters::kind(i));
  }

  bool isSingle() const {
    MOZ_ASSERT(!invalid_);
    return kind_ == FloatRegisters::Single;
  }
  bool isDouble() const {
    MOZ_ASSERT(!invalid_);
    return kind_ == FloatRegisters::Double;
  }
  bool isSimd128() const {
    MOZ_ASSERT(!invalid_);
#if defined(ENABLE_JIT_SIMD)
    return kind_ == FloatRegisters::Simd128;
#else
    return false;
#endif
  }
  bool isInvalid() const { return invalid_; }

  FloatRegister asSingle() const {
    MOZ_ASSERT(!invalid_);
    return FloatRegister(Encoding(encoding_), FloatRegisters::Single);
  }
  FloatRegister asDouble() const {
    MOZ_ASSERT(!invalid_);
    return FloatRegister(Encoding(encoding_), FloatRegisters::Double);
  }
  FloatRegister asSimd128() const {
#if defined(ENABLE_JIT_SIMD)
    MOZ_ASSERT(!invalid_);
    return FloatRegister(Encoding(encoding_), FloatRegisters::Simd128);
#else
    MOZ_CRASH("ENABLE_JIT_SIMD not set");
#endif
  }

  VRegister toV() const;

  constexpr uint32_t size() const {
    MOZ_ASSERT(!invalid_);
    if (kind_ == FloatRegisters::Double) {
      return sizeof(double);
    }
    if (kind_ == FloatRegisters::Single) {
      return sizeof(float);
    }
#if defined(ENABLE_JIT_SIMD)
    MOZ_ASSERT(kind_ == FloatRegisters::Simd128);
    return SizeOfSimd128;
#else
    MOZ_CRASH("ENABLE_JIT_SIMD not set");
#endif
  }

  constexpr Code code() const {
    // assert(!invalid_);
    return Codes::fromParts(encoding_, kind_, invalid_);
  }

  constexpr Encoding encoding() const {
    MOZ_ASSERT(!invalid_);
    return Encoding(encoding_);
  }

  const char* name() const { return FloatRegisters::GetName(code()); }
  bool volatile_() const {
    MOZ_ASSERT(!invalid_);
    return !!((SetType(1) << code()) & FloatRegisters::VolatileMask);
  }
  constexpr bool operator!=(FloatRegister other) const {
    return code() != other.code();
  }
  constexpr bool operator==(FloatRegister other) const {
    return code() == other.code();
  }

  bool aliases(FloatRegister other) const {
    return other.encoding_ == encoding_;
  }
  // Ensure that two floating point registers' types are equivalent.
  bool equiv(FloatRegister other) const {
    MOZ_ASSERT(!invalid_);
    return kind_ == other.kind_;
  }

  uint32_t numAliased() const { return Codes::NumTypes; }
  uint32_t numAlignedAliased() { return numAliased(); }

  FloatRegister aliased(uint32_t aliasIdx) {
    MOZ_ASSERT(!invalid_);
    MOZ_ASSERT(aliasIdx < numAliased());
    return FloatRegister(Encoding(encoding_),
                         Kind((aliasIdx + kind_) % numAliased()));
  }
  FloatRegister alignedAliased(uint32_t aliasIdx) {
    MOZ_ASSERT(aliasIdx < numAliased());
    return aliased(aliasIdx);
  }
  SetType alignedOrDominatedAliasedSet() const {
    return Codes::AliasMask << encoding_;
  }

  static constexpr RegTypeName DefaultType = RegTypeName::Float64;

  template <RegTypeName Name = DefaultType>
  static SetType LiveAsIndexableSet(SetType s) {
    return SetType(0);
  }

  template <RegTypeName Name = DefaultType>
  static SetType AllocatableAsIndexableSet(SetType s) {
    static_assert(Name != RegTypeName::Any, "Allocatable set are not iterable");
    return LiveAsIndexableSet<Name>(s);
  }

  static TypedRegisterSet<FloatRegister> ReduceSetForPush(
      const TypedRegisterSet<FloatRegister>& s);
  static uint32_t GetPushSizeInBytes(const TypedRegisterSet<FloatRegister>& s);
  uint32_t getRegisterDumpOffsetInBytes();
};

template <>
inline FloatRegister::SetType
FloatRegister::LiveAsIndexableSet<RegTypeName::Float32>(SetType set) {
  return set & FloatRegisters::AllSingleMask;
}

template <>
inline FloatRegister::SetType
FloatRegister::LiveAsIndexableSet<RegTypeName::Float64>(SetType set) {
  return set & FloatRegisters::AllDoubleMask;
}

#if defined(ENABLE_JIT_SIMD)
template <>
inline FloatRegister::SetType
FloatRegister::LiveAsIndexableSet<RegTypeName::Vector128>(SetType set) {
  return set & FloatRegisters::AllSimd128Mask;
}
#endif

template <>
inline FloatRegister::SetType
FloatRegister::LiveAsIndexableSet<RegTypeName::Any>(SetType set) {
  return set;
}

// LoongArch doesn't have double registers that alias multiple floats.
inline bool hasMultiAlias() { return false; }

// Get the canonical QNaN mandated by the LoongArch ISA.
template <typename T>
inline constexpr T FPUDefaultQNaN() {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);

  // <https://loongson.github.io/LoongArch-Documentation/LoongArch-Vol1-EN.html#_non_numerical_result_of_instructions>
  if constexpr (std::is_same_v<T, float>) {
    // > The default single-precision QNaN value is 0x7FC00000, [...]
    return mozilla::BitwiseCast<float>(UINT32_C(0x7fc00000));
  } else {
    // > [...] and the default double-precision QNaN value is
    // > 0x7FF8000000000000.
    return mozilla::BitwiseCast<double>(UINT64_C(0x7ff8000000000000));
  }
}

enum class LOONG64Extension : uint32_t {
  // Flag when the extensions are initialized, so they can be atomically set.
  Initialized,

  // Atomic operations AM{SWAP,ADD}{,_DB}.[BH].
  LamBh,

  // Atomic operations AMCAS{,_DB}.[BHWD].
  Lamcas,

  // LoongArch SIMD Extension (128-bit SIMD).
  Lsx,
};

using LOONG64Extensions = mozilla::EnumSet<LOONG64Extension>;

class LOONG64Flags final {
  // The override flags selected by the LOONG64_ISA environment variable or
  // the --loong64-isa JS shell argument. They are stable after startup: there
  // is no programmatic way of setting these from JS.
  static inline LOONG64Extensions extensions{};

 public:
  LOONG64Flags() = delete;

  // LOONG64Flags::Init is called from the JitContext constructor to read the
  // hardware flags. This method must only be called once.
  static void Init();

  static bool IsInitialized() {
    return extensions.contains(LOONG64Extension::Initialized);
  }

  static uint32_t GetFlags() {
    MOZ_ASSERT(IsInitialized());
    return extensions.serialize();
  }

  static bool HasLamBhExtension() {
    return extensions.contains(LOONG64Extension::LamBh);
  }

  static bool HasLamcasExtension() {
    return extensions.contains(LOONG64Extension::Lamcas);
  }

  static bool HasLsxExtension() {
    return extensions.contains(LOONG64Extension::Lsx);
  }
};

// Register a LoongArch ISA target. During engine initialization, this target
// is used instead of enabling every detected hardware feature. This must be
// called before JS_Init and the passed string's buffer must outlive JS_Init.
void SetLOONG64ISAString(const char* isa);

// Retrieve the Loong64 extensions as a bitmask. They must have been set.
inline uint32_t GetLOONG64Flags() { return LOONG64Flags::GetFlags(); }

}  // namespace jit
}  // namespace js

#endif /* jit_loong64_Architecture_loong64_h */
