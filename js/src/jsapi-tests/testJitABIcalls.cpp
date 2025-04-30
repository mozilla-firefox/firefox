/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/FloatingPoint.h"
#include "mozilla/IntegerTypeTraits.h"

#include <iterator>

#include "jit/ABIFunctions.h"
#include "jit/IonAnalysis.h"
#include "jit/Linker.h"
#include "jit/MacroAssembler.h"
#include "jit/MIRGenerator.h"
#include "jit/MIRGraph.h"
#include "jit/ValueNumbering.h"
#include "jit/VMFunctions.h"
#include "js/Value.h"

#include "jsapi-tests/tests.h"
#include "jsapi-tests/testsJit.h"

#include "jit/ABIFunctionList-inl.h"
#include "jit/MacroAssembler-inl.h"
#include "jit/VMFunctionList-inl.h"

using namespace js;
using namespace js::jit;

// This test case relies on VMFUNCTION_LIST, ABIFUNCTION_LIST,
// ABIFUNCTION_AND_TYPE_LIST and ABIFUNCTIONSIG_LIST, to create a test case for
// each function registered, in order to check if the arguments are properly
// being interpreted after a call from the JIT.
//
// This test checks that the interpretation of the C++ compiler matches the
// interpretation of the JIT. It works by generating a call to a function which
// has the same signature as the tested function. The function being called
// re-interprets the arguments' content to ensure that it matches the content
// given as arguments by the JIT.
//
// These tests cases succeed if the content provided by the JIT matches the
// content read by the C++ code. Otherwise, a failure implies that either the
// MacroAssembler is not used properly, or that the code used by the JIT to
// generate the function call does not match the ABI of the targeted system.

// Convert the content of each macro list to a single and unique format which is
// (Name, Type).
#define ABIFUN_TO_ALLFUN(Fun) (#Fun, decltype(&Fun))
#define ABIFUN_AND_SIG_TO_ALLFUN(Fun, Sig) (#Fun " as " #Sig, Sig)
#define ABISIG_TO_ALLFUN(Sig) ("(none) as " #Sig, Sig)
#define VMFUN_TO_ALLFUN(Name, Fun, Pop...) (#Fun, decltype(&Fun))

#define APPLY(A, B) A B

// Generate macro calls for all the lists which are used to allow, directly or
// indirectly, calls performed with callWithABI.
//
// This macro will delegate to a different macro call based on the type of the
// list the element is extracted from.
#define ALL_FUNCTIONS(PREFIX)                                  \
  ABIFUNCTION_LIST(PREFIX##_ABIFUN_TO_ALLFUN)                  \
  ABIFUNCTION_AND_TYPE_LIST(PREFIX##_ABIFUN_AND_SIG_TO_ALLFUN) \
  ABIFUNCTIONSIG_LIST(PREFIX##_ABISIG_TO_ALLFUN)               \
  VMFUNCTION_LIST(PREFIX##_VMFUN_TO_ALLFUN)

// sizeof(const T&) is not equal to sizeof(const T*), but references are passed
// as pointers.
//
// "When applied to a reference or a reference type, the result is the size of
// the referenced type." [expr.sizeof] (5.3.3.2)
//
// The following functions avoid this issue by wrapping the type in a structure
// which will share the same property, even if the wrapped type is a reference.
template <typename T>
constexpr size_t ActualSizeOf() {
  struct Wrapper {
    T _unused;
  };
  return sizeof(Wrapper);
}

template <typename T>
constexpr size_t ActualAlignOf() {
  struct Wrapper {
    T _unused;
  };
  return alignof(Wrapper);
}

// Given a type, return the integer type which has the same size.
template <typename T>
using IntTypeOf_t =
    typename mozilla::UnsignedStdintTypeForSize<ActualSizeOf<T>()>::Type;

// Concatenate 2 std::integer_sequence, and return an std::integer_sequence with
// the content of both parameters.
template <typename Before, typename After>
struct Concat;

template <typename Int, Int... Before, Int... After>
struct Concat<std::integer_sequence<Int, Before...>,
              std::integer_sequence<Int, After...>> {
  using type = std::integer_sequence<Int, Before..., After...>;
};

template <typename Before, typename After>
using Concat_t = typename Concat<Before, After>::type;

static_assert(std::is_same_v<Concat_t<std::integer_sequence<uint8_t, 1, 2>,
                                      std::integer_sequence<uint8_t, 3, 4>>,
                             std::integer_sequence<uint8_t, 1, 2, 3, 4>>);

// Generate an std::integer_sequence of `N` elements, where each element is an
// uint8_t integer with value `Value`.
template <size_t N, uint8_t Value>
constexpr auto CstSeq() {
  if constexpr (N == 0) {
    return std::integer_sequence<uint8_t>{};
  } else {
    return Concat_t<std::integer_sequence<uint8_t, Value>,
                    decltype(CstSeq<N - 1, Value>())>{};
  }
}

template <size_t N, uint8_t Value>
using CstSeq_t = decltype(CstSeq<N, Value>());

static_assert(
    std::is_same_v<CstSeq_t<4, 2>, std::integer_sequence<uint8_t, 2, 2, 2, 2>>);

// Computes the number of bytes to add before a type in order to align it in
// memory.
constexpr size_t PadBytes(size_t size, size_t align) {
  return (align - (size % align)) % align;
}

// Request a minimum alignment for the values added to a buffer in order to
// account for the read size used by the MoveOperand given as an argument of
// passWithABI. The MoveOperand does not take into consideration the size of
// the data being transfered, and might load a larger amount of data.
//
// This function ensures that the MoveOperand would read the 0x55 padding added
// after each value, when it reads too much.
constexpr size_t AtLeastSize() { return sizeof(uintptr_t); }

// Returns the size which needs to be added in addition to the memory consumed
// by the type, from which the size if given as argument.
template <typename Type>
constexpr size_t BackPadBytes() {
  return std::max(AtLeastSize(), ActualSizeOf<Type>()) - ActualSizeOf<Type>();
}

// Adds the padding and the reserved size for storing a value in a buffer which
// can be read by a MoveOperand.
template <typename Type>
constexpr size_t PadSize(size_t size) {
  return PadBytes(size, ActualAlignOf<Type>()) + ActualSizeOf<Type>() +
         BackPadBytes<Type>();
}

// Generate an std::integer_sequence of 0:uint8_t elements of the size of the
// padding needed to align a type in memory.
template <size_t Align, size_t CurrSize>
using PadSeq_t = decltype(CstSeq<PadBytes(CurrSize, Align), 0>());

static_assert(std::is_same_v<PadSeq_t<4, 0>, std::integer_sequence<uint8_t>>);
static_assert(
    std::is_same_v<PadSeq_t<4, 3>, std::integer_sequence<uint8_t, 0>>);
static_assert(
    std::is_same_v<PadSeq_t<4, 2>, std::integer_sequence<uint8_t, 0, 0>>);
static_assert(
    std::is_same_v<PadSeq_t<4, 1>, std::integer_sequence<uint8_t, 0, 0, 0>>);

// Spread an integer value `Value` into a new std::integer_sequence of `N`
// uint8_t elements, using Little Endian ordering of bytes.
template <size_t N, uint64_t Value, uint8_t... Rest>
constexpr auto FillLESeq() {
  if constexpr (N == 0) {
    return std::integer_sequence<uint8_t, Rest...>{};
  } else {
    return FillLESeq<N - 1, (Value >> 8), Rest..., uint8_t(Value & 0xff)>();
  }
}

template <size_t N, uint64_t Value>
using FillSeq_t = decltype(FillLESeq<N, Value>());

static_assert(std::is_same_v<FillSeq_t<4, 2>,
                             std::integer_sequence<uint8_t, 2, 0, 0, 0>>);

// Given a list of template parameters, generate an std::integer_sequence of
// size_t, where each element is 1 larger than the previous one. The generated
// sequence starts at 0.
template <typename... Args>
using ArgsIndexes_t =
    std::make_integer_sequence<uint64_t, uint64_t(sizeof...(Args))>;

static_assert(std::is_same_v<ArgsIndexes_t<uint8_t, uint64_t>,
                             std::integer_sequence<uint64_t, 0, 1>>);

// Extract a single bit for each element of an std::integer_sequence. This is
// used to work around some restrictions with providing boolean arguments,
// which might be truncated to a single bit.
template <size_t Bit, typename IntSeq>
struct ExtractBit;

template <size_t Bit, uint64_t... Values>
struct ExtractBit<Bit, std::integer_sequence<uint64_t, Values...>> {
  using type = std::integer_sequence<uint64_t, (Values >> Bit) & 1 ...>;
};

// Generate an std::integer_sequence of indexes which are filtered for a single
// bit, such that it can be used with boolean types.
template <size_t Bit, typename... Args>
using ArgsBitOfIndexes_t =
    typename ExtractBit<Bit, ArgsIndexes_t<Args...>>::type;

static_assert(std::is_same_v<ArgsBitOfIndexes_t<0, int, int, int, int>,
                             std::integer_sequence<uint64_t, 0, 1, 0, 1>>);
static_assert(std::is_same_v<ArgsBitOfIndexes_t<1, int, int, int, int>,
                             std::integer_sequence<uint64_t, 0, 0, 1, 1>>);

// Compute the offset of each argument in a buffer produced by GenArgsBuffer,
// this is used to fill the MoveOperand displacement field when loading value
// out of the buffer produced by GenArgsBuffer.
template <uint64_t Size, typename... Args>
struct ArgsOffsets;

template <uint64_t Size>
struct ArgsOffsets<Size> {
  using type = std::integer_sequence<uint64_t>;
};

template <uint64_t Size, typename Arg, typename... Args>
struct ArgsOffsets<Size, Arg, Args...> {
  using type =
      Concat_t<std::integer_sequence<
                   uint64_t, Size + PadBytes(Size, ActualAlignOf<Arg>())>,
               typename ArgsOffsets<Size + PadSize<Arg>(Size), Args...>::type>;
};

template <uint64_t Size, typename... Args>
using ArgsOffsets_t = typename ArgsOffsets<Size, Args...>::type;

// Not all 32bits architecture align uint64_t type on 8 bytes, so check the
// validity of the stored content based on the alignment of the architecture.
static_assert(ActualAlignOf<uint64_t>() != 8 ||
              std::is_same_v<ArgsOffsets_t<0, uint8_t, uint64_t, bool>,
                             std::integer_sequence<uint64_t, 0, 8, 16>>);

static_assert(ActualAlignOf<uint64_t>() != 4 ||
              std::is_same_v<ArgsOffsets_t<0, uint8_t, uint64_t, bool>,
                             std::integer_sequence<uint64_t, 0, 4, 12>>);

// Generate an std::integer_sequence containing the size of each argument in
// memory.
template <typename... Args>
using ArgsSizes_t = std::integer_sequence<uint64_t, ActualSizeOf<Args>()...>;

// Generate an std::integer_sequence containing values where all valid bits for
// each type are set to 1.
template <typename Type>
constexpr uint64_t FillBits() {
  constexpr uint64_t topBit = uint64_t(1) << ((8 * ActualSizeOf<Type>()) - 1);
  if constexpr (std::is_same_v<Type, bool>) {
    return uint64_t(1);
  } else if constexpr (std::is_same_v<Type, double> ||
                       std::is_same_v<Type, float>) {
    // A NaN has all the bits of its exponent set to 1. The CPU / C++ does not
    // garantee keeping the payload of NaN values, when interpreted as floating
    // point, which could cause some random failures. This removes one bit from
    // the exponent, such that the floating point value is not converted to a
    // canonicalized NaN by the time we compare it.
    constexpr uint64_t lowExpBit =
        uint64_t(1) << mozilla::FloatingPoint<Type>::kExponentShift;
    return uint64_t((topBit - 1) + topBit - lowExpBit);
  } else {
    // Note: Keep parentheses to avoid unwanted overflow.
    return uint64_t((topBit - 1) + topBit);
  }
}

template <typename... Args>
using ArgsFillBits_t = std::integer_sequence<uint64_t, FillBits<Args>()...>;

// Given a type, return the ABIType used by passABIArg to know how to
// interpret the value which are given as arguments.
template <typename Type>
constexpr ABIType TypeToABIType() {
  if constexpr (std::is_same_v<Type, float>) {
    return ABIType::Float32;
  } else if constexpr (std::is_same_v<Type, double>) {
    return ABIType::Float64;
  } else {
    return ABIType::General;
  }
}

// Generate a sequence which contains the associated ABIType of each argument.
// Note, a new class is defined because C++ header of clang are rejecting the
// option of having an enumerated type as argument of std::integer_sequence.
template <ABIType... Val>
class ABITypeSequence {};

template <typename... Args>
using ArgsABITypes_t = ABITypeSequence<TypeToABIType<Args>()...>;

// Generate an std::integer_sequence which corresponds to a buffer containing
// values which are spread at the location where each arguments type would be
// stored in a buffer.
template <typename Buffer, typename Values, typename... Args>
struct ArgsBuffer;

template <uint8_t... Buffer, typename Arg, typename... Args, uint64_t Val,
          uint64_t... Values>
struct ArgsBuffer<std::integer_sequence<uint8_t, Buffer...>,
                  std::integer_sequence<uint64_t, Val, Values...>, Arg,
                  Args...> {
  using type = typename ArgsBuffer<
      Concat_t<std::integer_sequence<uint8_t, Buffer...>,
               Concat_t<PadSeq_t<ActualAlignOf<Arg>(), sizeof...(Buffer)>,
                        Concat_t<FillSeq_t<ActualSizeOf<Arg>(), Val>,
                                 CstSeq_t<BackPadBytes<Arg>(), 0x55>>>>,
      std::integer_sequence<uint64_t, Values...>, Args...>::type;
};

template <typename Buffer>
struct ArgsBuffer<Buffer, std::integer_sequence<uint64_t>> {
  using type = Buffer;
};

template <typename Values, typename... Args>
using ArgsBuffer_t =
    typename ArgsBuffer<std::integer_sequence<uint8_t>, Values, Args...>::type;

// NOTE: The representation of the boolean might be surprising in this test
// case, see AtLeastSize function for an explanation.
#ifdef JS_64BIT
static_assert(sizeof(uintptr_t) == 8);
static_assert(
    std::is_same_v<
        ArgsBuffer_t<std::integer_sequence<uint64_t, 42, 51>, uint64_t, bool>,
        std::integer_sequence<uint8_t, 42, 0, 0, 0, 0, 0, 0, 0, 51, 0x55, 0x55,
                              0x55, 0x55, 0x55, 0x55, 0x55>>);
static_assert(
    std::is_same_v<
        ArgsBuffer_t<std::integer_sequence<uint64_t, 0xffffffff, 0xffffffff>,
                     uint8_t, uint16_t>,
        std::integer_sequence<uint8_t, 0xff, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
                              0x55, 0xff, 0xff, 0x55, 0x55, 0x55, 0x55, 0x55,
                              0x55>>);
#else
static_assert(sizeof(uintptr_t) == 4);
static_assert(
    std::is_same_v<
        ArgsBuffer_t<std::integer_sequence<uint64_t, 42, 51>, uint64_t, bool>,
        std::integer_sequence<uint8_t, 42, 0, 0, 0, 0, 0, 0, 0, 51, 0x55, 0x55,
                              0x55>>);
static_assert(
    std::is_same_v<
        ArgsBuffer_t<std::integer_sequence<uint64_t, 0xffffffff, 0xffffffff>,
                     uint8_t, uint16_t>,
        std::integer_sequence<uint8_t, 0xff, 0x55, 0x55, 0x55, 0xff, 0xff, 0x55,
                              0x55>>);
#endif

// Test used to check if any of the types given as template parameters are a
// `bool`, which is a corner case where a raw integer might be truncated by the
// C++ compiler.
template <typename... Args>
constexpr bool AnyBool_v = (std::is_same_v<Args, bool> || ...);

// Instantiate an std::integer_sequence as a buffer which is readable and
// addressable at runtime, for reading argument values from the generated code.
template <typename Seq>
struct InstanceSeq;

template <typename Int, Int... Values>
struct InstanceSeq<std::integer_sequence<Int, Values...>> {
  static constexpr Int table[sizeof...(Values)] = {Values...};
  static constexpr size_t size = sizeof...(Values);
};

// Instantiate a buffer for testing the position of arguments when calling a
// function.
template <typename... Args>
using TestArgsPositions =
    InstanceSeq<ArgsBuffer_t<ArgsIndexes_t<Args...>, Args...>>;

// Instantiate a buffer for testing the position of arguments, one bit at a
// time, when calling a function.
template <size_t Bit, typename... Args>
using TestArgsBitOfPositions =
    InstanceSeq<ArgsBuffer_t<ArgsBitOfIndexes_t<Bit, Args...>, Args...>>;

// Instantiate a buffer to check that the size of each argument is interpreted
// correctly when calling a function.
template <typename... Args>
using TestArgsSizes = InstanceSeq<ArgsBuffer_t<ArgsSizes_t<Args...>, Args...>>;

// Instantiate a buffer to check that all bits of each argument goes through.
template <typename... Args>
using TestArgsFillBits =
    InstanceSeq<ArgsBuffer_t<ArgsFillBits_t<Args...>, Args...>>;

// ConvertToInt returns the raw value of any argument as an integer value which
// can be compared with the expected values.
template <typename Type>
IntTypeOf_t<Type> ConvertToInt(Type v) {
  // Simplify working with types by casting the address of the value to the
  // equivalent `const void*`.
  auto address = reinterpret_cast<const void*>(&v);
  // Convert the `void*` to an integer pointer of the same size as the input
  // type, and return the raw value stored in the integer interpretation.
  static_assert(ActualSizeOf<Type>() == ActualSizeOf<IntTypeOf_t<Type>>());
  if constexpr (std::is_reference_v<Type>) {
    return reinterpret_cast<const IntTypeOf_t<Type>>(address);
  } else {
    return *reinterpret_cast<const IntTypeOf_t<Type>*>(address);
  }
}

// Attributes used to disable some parts of Undefined Behavior sanitizer. This
// is needed to keep the signature identical to what is used in production,
// instead of working around these limitations.
//
// * no_sanitize("enum"): Enumerated values given as arguments are checked to
//     see if the value given as argument matches any of the enumerated values.
//     The patterns used to check whether the values are correctly transmitted
//     from the JIT to C++ might go beyond the set of enumerated values, and
//     break this sanitizer check.
#if defined(__clang__) && defined(__has_attribute) && \
    __has_attribute(no_sanitize)
#  define NO_ARGS_CHECKS __attribute__((no_sanitize("enum")))
#else
#  define NO_ARGS_CHECKS
#endif

// Check if the raw values of arguments are equal to the numbers given in the
// std::integer_sequence given as the first argument.
template <typename... Args, typename Int, Int... Val>
NO_ARGS_CHECKS bool CheckArgsEqual(JSAPIRuntimeTest* instance, int lineno,
                                   std::integer_sequence<Int, Val...>,
                                   Args... args) {
  return (instance->checkEqual(ConvertToInt<Args>(args), IntTypeOf_t<Args>(Val),
                               "ConvertToInt<Args>(args)",
                               "IntTypeOf_t<Args>(Val)", __FILE__, lineno) &&
          ...);
}

// Generate code to register the value of each argument of the called function.
// Each argument's content is read from a buffer whose address is stored in the
// `base` register. The offsets of arguments are given as a third argument
// which is expected to be generated by `ArgsOffsets`. The ABIType types of
// arguments are given as the fourth argument and are expected to be generated
// by `ArgsABIType`.
template <uint64_t... Off, ABIType... Type>
static void passABIArgs(MacroAssembler& masm, Register base,
                        std::integer_sequence<uint64_t, Off...>,
                        ABITypeSequence<Type...>) {
  (masm.passABIArg(MoveOperand(base, size_t(Off)), Type), ...);
}

// For each function type given as a parameter, create a few functions with the
// given type, to be called by the JIT code produced by `generateCalls`. These
// functions report the result through the instance registered with the
// `set_instance` function, as we cannot add extra arguments to these functions.
template <typename Type>
struct DefineCheckArgs;

template <typename Res, typename... Args>
struct DefineCheckArgs<Res (*)(Args...)> {
  void set_instance(JSAPIRuntimeTest* instance, bool* reportTo) {
    MOZ_ASSERT((!instance_) != (!instance));
    instance_ = instance;
    MOZ_ASSERT((!reportTo_) != (!reportTo));
    reportTo_ = reportTo;
  }
  static void report(bool value) { *reportTo_ = *reportTo_ && value; }

  // Check that arguments are interpreted in the same order at compile time and
  // at runtime.
  static NO_ARGS_CHECKS Res CheckArgsPositions(Args... args) {
    AutoUnsafeCallWithABI unsafe;
    using Indexes = std::index_sequence_for<Args...>;
    report(CheckArgsEqual<Args...>(instance_, __LINE__, Indexes(),
                                   std::forward<Args>(args)...));
    return Res();
  }

  // This is the same test as above, but some compilers might clean the boolean
  // values using `& 1` operations, which corrupt the operand index, thus to
  // properly check for the position of boolean operands, we have to check the
  // position of the boolean operand using a single bit at a time.
  static NO_ARGS_CHECKS Res CheckArgsBitOfPositions0(Args... args) {
    AutoUnsafeCallWithABI unsafe;
    using Indexes = ArgsBitOfIndexes_t<0, Args...>;
    report(CheckArgsEqual<Args...>(instance_, __LINE__, Indexes(),
                                   std::forward<Args>(args)...));
    return Res();
  }

  static NO_ARGS_CHECKS Res CheckArgsBitOfPositions1(Args... args) {
    AutoUnsafeCallWithABI unsafe;
    using Indexes = ArgsBitOfIndexes_t<1, Args...>;
    report(CheckArgsEqual<Args...>(instance_, __LINE__, Indexes(),
                                   std::forward<Args>(args)...));
    return Res();
  }

  static NO_ARGS_CHECKS Res CheckArgsBitOfPositions2(Args... args) {
    AutoUnsafeCallWithABI unsafe;
    using Indexes = ArgsBitOfIndexes_t<2, Args...>;
    report(CheckArgsEqual<Args...>(instance_, __LINE__, Indexes(),
                                   std::forward<Args>(args)...));
    return Res();
  }

  static NO_ARGS_CHECKS Res CheckArgsBitOfPositions3(Args... args) {
    AutoUnsafeCallWithABI unsafe;
    using Indexes = ArgsBitOfIndexes_t<3, Args...>;
    report(CheckArgsEqual<Args...>(instance_, __LINE__, Indexes(),
                                   std::forward<Args>(args)...));
    return Res();
  }

  // Check that the compile time and run time sizes of each argument are the
  // same.
  static NO_ARGS_CHECKS Res CheckArgsSizes(Args... args) {
    AutoUnsafeCallWithABI unsafe;
    using Sizes = ArgsSizes_t<Args...>;
    report(CheckArgsEqual<Args...>(instance_, __LINE__, Sizes(),
                                   std::forward<Args>(args)...));
    return Res();
  }

  // Check that the compile time and run time all bits of each argument are
  // correctly passed through.
  static NO_ARGS_CHECKS Res CheckArgsFillBits(Args... args) {
    AutoUnsafeCallWithABI unsafe;
    using FillBits = ArgsFillBits_t<Args...>;
    report(CheckArgsEqual<Args...>(instance_, __LINE__, FillBits(),
                                   std::forward<Args>(args)...));
    return Res();
  }

  using FunType = Res (*)(Args...);
  struct Test {
    const uint8_t* buffer;
    const size_t size;
    const FunType fun;
  };

  // Generate JIT code for calling the above test functions where each argument
  // is given a different raw value that can be compared by each called
  // function.
  void generateCalls(MacroAssembler& masm, Register base, Register setup) {
    using ArgsPositions = TestArgsPositions<Args...>;
    using ArgsBitOfPositions0 = TestArgsBitOfPositions<0, Args...>;
    using ArgsBitOfPositions1 = TestArgsBitOfPositions<1, Args...>;
    using ArgsBitOfPositions2 = TestArgsBitOfPositions<2, Args...>;
    using ArgsBitOfPositions3 = TestArgsBitOfPositions<3, Args...>;
    using ArgsSizes = TestArgsSizes<Args...>;
    using ArgsFillBits = TestArgsFillBits<Args...>;
    static const Test testsWithoutBoolArgs[3] = {
        {ArgsPositions::table, ArgsPositions::size, CheckArgsPositions},
        {ArgsSizes::table, ArgsSizes::size, CheckArgsSizes},
        {ArgsFillBits::table, ArgsFillBits::size, CheckArgsFillBits},
    };
    static const Test testsWithBoolArgs[6] = {
        {ArgsBitOfPositions0::table, ArgsBitOfPositions0::size,
         CheckArgsBitOfPositions0},
        {ArgsBitOfPositions1::table, ArgsBitOfPositions1::size,
         CheckArgsBitOfPositions1},
        {ArgsBitOfPositions2::table, ArgsBitOfPositions2::size,
         CheckArgsBitOfPositions2},
        {ArgsBitOfPositions3::table, ArgsBitOfPositions3::size,
         CheckArgsBitOfPositions3},
        {ArgsSizes::table, ArgsSizes::size, CheckArgsSizes},
        {ArgsFillBits::table, ArgsFillBits::size, CheckArgsFillBits},
    };
    const Test* tests = testsWithoutBoolArgs;
    size_t numTests = std::size(testsWithoutBoolArgs);
    if (AnyBool_v<Args...>) {
      tests = testsWithBoolArgs;
      numTests = std::size(testsWithBoolArgs);
    }

    for (size_t i = 0; i < numTests; i++) {
      const Test& test = tests[i];
      masm.movePtr(ImmPtr(test.buffer), base);

      masm.setupUnalignedABICall(setup);
      using Offsets = ArgsOffsets_t<0, Args...>;
      using ABITypes = ArgsABITypes_t<Args...>;
      passABIArgs(masm, base, Offsets(), ABITypes());
      masm.callWithABI(DynFn{JS_FUNC_TO_DATA_PTR(void*, test.fun)},
                       TypeToABIType<Res>(),
                       CheckUnsafeCallWithABI::DontCheckOther);
    }
  }

 private:
  // As we are checking specific function signature, we cannot add extra
  // parameters, thus we rely on static variables to pass the value of the
  // instance that we are testing.
  static JSAPIRuntimeTest* instance_;
  static bool* reportTo_;
};

template <typename Res, typename... Args>
JSAPIRuntimeTest* DefineCheckArgs<Res (*)(Args...)>::instance_ = nullptr;

template <typename Res, typename... Args>
bool* DefineCheckArgs<Res (*)(Args...)>::reportTo_ = nullptr;

// This is a child class of JSAPIRuntimeTest, which is used behind the scenes to
// register test cases in jsapi-tests. Each instance of it creates a new test
// case. This class is specialized with the type of the function to check, and
// initialized with the name of the function with the given signature.
//
// When executed, it generates the JIT code to call functions with the same
// signature and checks that the JIT interpretation of arguments location
// matches the C++ interpretation. If it differs, the test case will fail.
template <typename Sig>
class JitABICall final : public JSAPIRuntimeTest, public DefineCheckArgs<Sig> {
 public:
  explicit JitABICall(const char* name) : name_(name) { reuseGlobal = true; }
  virtual const char* name() override { return name_; }
  virtual bool run(JS::HandleObject) override {
    bool result = true;
    this->set_instance(this, &result);

    TempAllocator temp(&cx->tempLifoAlloc());
    JitContext jcx(cx);
    StackMacroAssembler masm(cx, temp);
    AutoCreatedBy acb(masm, __func__);
    PrepareJit(masm);

    AllocatableGeneralRegisterSet regs(GeneralRegisterSet::All());
    // Initialize the base register the same way this is done while reading
    // arguments in generateVMWrapper, in order to avoid MOZ_RELEASE_ASSERT in
    // the MoveResolver.
#if defined(JS_CODEGEN_X86)
    Register base = regs.takeAny();
#elif defined(JS_CODEGEN_X64)
    Register base = r10;
    regs.take(base);
#elif defined(JS_CODEGEN_ARM)
    Register base = r5;
    regs.take(base);
#elif defined(JS_CODEGEN_ARM64)
    Register base = r8;
    regs.take(base);
#elif defined(JS_CODEGEN_MIPS64)
    Register base = t1;
    regs.take(base);
#elif defined(JS_CODEGEN_LOONG64)
    Register base = t0;
    regs.take(base);
#elif defined(JS_CODEGEN_RISCV64)
    Register base = t0;
    regs.take(base);
#else
#  error "Unknown architecture!"
#endif

    Register setup = regs.takeAny();

    this->generateCalls(masm, base, setup);

    if (!ExecuteJit(cx, masm)) {
      return false;
    }
    this->set_instance(nullptr, nullptr);
    return result;
  };

 private:
  const char* name_;
};

// GCC warns when the signature does not have matching attributes (for example
// [[nodiscard]]). Squelch this warning to avoid a GCC-only footgun.
#if MOZ_IS_GCC
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wignored-attributes"
#endif

// For each VMFunction and ABIFunction, create an instance of a JitABICall
// class to register a jsapi-tests test case.
#define TEST_INSTANCE(Name, Sig)                 \
  MOZ_RUNINIT static JitABICall<Sig> MOZ_CONCAT( \
      MOZ_CONCAT(cls_jitabicall, __COUNTER__),   \
      _instance)("JIT ABI for " Name);
#define TEST_INSTANCE_ABIFUN_TO_ALLFUN(...) \
  APPLY(TEST_INSTANCE, ABIFUN_TO_ALLFUN(__VA_ARGS__))
#define TEST_INSTANCE_ABIFUN_AND_SIG_TO_ALLFUN(...) \
  APPLY(TEST_INSTANCE, ABIFUN_AND_SIG_TO_ALLFUN(__VA_ARGS__))
#define TEST_INSTANCE_ABISIG_TO_ALLFUN(...) \
  APPLY(TEST_INSTANCE, ABISIG_TO_ALLFUN(__VA_ARGS__))
#define TEST_INSTANCE_VMFUN_TO_ALLFUN(...) \
  APPLY(TEST_INSTANCE, VMFUN_TO_ALLFUN(__VA_ARGS__))

ALL_FUNCTIONS(TEST_INSTANCE)

#if MOZ_IS_GCC
#  pragma GCC diagnostic pop
#endif
