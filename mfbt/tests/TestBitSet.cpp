/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/BitSet.h"

using mozilla::Atomic;
using mozilla::BitSet;

static_assert([] {
  BitSet<65, uint32_t> bits;
  bits[0] = true;
  bits[32] = true;
  bits[64] = true;
  auto copy = bits;
  BitSet<65, uint32_t> assigned;
  assigned = copy;
  bits[32] = false;
  return assigned.test(0) && assigned.test(32) && assigned.test(64) &&
         !assigned.test(1) && !bits.test(32);
}());

template <typename Storage>
class BitSetSuite {
  template <size_t N>
  using TestBitSet = BitSet<N, Storage>;

  using Word = typename TestBitSet<1>::Word;

  static constexpr size_t kBitsPerWord = sizeof(Storage) * 8;

  static constexpr Word kAllBitsSet = ~Word{0};

 public:
  void testLength() {
    MOZ_RELEASE_ASSERT(TestBitSet<1>().Storage().LengthBytes() ==
                       sizeof(Storage));

    MOZ_RELEASE_ASSERT(TestBitSet<1>().Storage().Length() == 1);
    MOZ_RELEASE_ASSERT(TestBitSet<kBitsPerWord>().Storage().Length() == 1);
    MOZ_RELEASE_ASSERT(TestBitSet<kBitsPerWord + 1>().Storage().Length() == 2);
  }

  void testConstructAndAssign() {
    MOZ_RELEASE_ASSERT(TestBitSet<1>().Storage()[0] == 0);
    MOZ_RELEASE_ASSERT(TestBitSet<kBitsPerWord>().Storage()[0] == 0);
    MOZ_RELEASE_ASSERT(TestBitSet<kBitsPerWord + 1>().Storage()[0] == 0);
    MOZ_RELEASE_ASSERT(TestBitSet<kBitsPerWord + 1>().Storage()[1] == 0);

    TestBitSet<1> bitset1;
    bitset1.SetAll();
    TestBitSet<kBitsPerWord> bitsetW;
    bitsetW.SetAll();
    TestBitSet<kBitsPerWord + 1> bitsetW1;
    bitsetW1.SetAll();

    MOZ_RELEASE_ASSERT(bitset1.Storage()[0] == 1);
    MOZ_RELEASE_ASSERT(bitsetW.Storage()[0] == kAllBitsSet);
    MOZ_RELEASE_ASSERT(bitsetW1.Storage()[0] == kAllBitsSet);
    MOZ_RELEASE_ASSERT(bitsetW1.Storage()[1] == 1);

    MOZ_RELEASE_ASSERT(TestBitSet<1>(bitset1).Storage()[0] == 1);
    MOZ_RELEASE_ASSERT(TestBitSet<kBitsPerWord>(bitsetW).Storage()[0] ==
                       kAllBitsSet);
    MOZ_RELEASE_ASSERT(TestBitSet<kBitsPerWord + 1>(bitsetW1).Storage()[0] ==
                       kAllBitsSet);
    MOZ_RELEASE_ASSERT(TestBitSet<kBitsPerWord + 1>(bitsetW1).Storage()[1] ==
                       1);

    MOZ_RELEASE_ASSERT(TestBitSet<1>(bitset1.Storage()).Storage()[0] == 1);
    MOZ_RELEASE_ASSERT(
        TestBitSet<kBitsPerWord>(bitsetW.Storage()).Storage()[0] ==
        kAllBitsSet);
    MOZ_RELEASE_ASSERT(
        TestBitSet<kBitsPerWord + 1>(bitsetW1.Storage()).Storage()[0] ==
        kAllBitsSet);
    MOZ_RELEASE_ASSERT(
        TestBitSet<kBitsPerWord + 1>(bitsetW1.Storage()).Storage()[1] == 1);

    TestBitSet<1> bitset1Copy;
    bitset1Copy = bitset1;
    TestBitSet<kBitsPerWord> bitsetWCopy;
    bitsetWCopy = bitsetW;
    TestBitSet<kBitsPerWord + 1> bitsetW1Copy;
    bitsetW1Copy = bitsetW1;

    MOZ_RELEASE_ASSERT(bitset1Copy.Storage()[0] == 1);
    MOZ_RELEASE_ASSERT(bitsetWCopy.Storage()[0] == kAllBitsSet);
    MOZ_RELEASE_ASSERT(bitsetW1Copy.Storage()[0] == kAllBitsSet);
    MOZ_RELEASE_ASSERT(bitsetW1Copy.Storage()[1] == 1);
  }

  void testSetBit() {
    TestBitSet<kBitsPerWord + 2> bitset;
    MOZ_RELEASE_ASSERT(!bitset.test(3));
    MOZ_RELEASE_ASSERT(!bitset[3]);
    MOZ_RELEASE_ASSERT(!bitset.test(kBitsPerWord + 1));
    MOZ_RELEASE_ASSERT(!bitset[kBitsPerWord + 1]);

    bitset[3] = true;
    MOZ_RELEASE_ASSERT(bitset.test(3));
    MOZ_RELEASE_ASSERT(bitset[3]);

    bitset[kBitsPerWord + 1] = true;
    MOZ_RELEASE_ASSERT(bitset.test(3));
    MOZ_RELEASE_ASSERT(bitset[3]);
    MOZ_RELEASE_ASSERT(bitset.test(kBitsPerWord + 1));
    MOZ_RELEASE_ASSERT(bitset[kBitsPerWord + 1]);

    bitset.ResetAll();
    for (size_t i = 0; i < decltype(bitset)::size(); i++) {
      MOZ_RELEASE_ASSERT(!bitset[i]);
    }

    bitset.SetAll();
    for (size_t i = 0; i < decltype(bitset)::size(); i++) {
      MOZ_RELEASE_ASSERT(bitset[i]);
    }

    // Test trailing unused bits are not set by SetAll().
    MOZ_RELEASE_ASSERT(bitset.Storage()[1] == 3);

    bitset.ResetAll();
    for (size_t i = 0; i < decltype(bitset)::size(); i++) {
      MOZ_RELEASE_ASSERT(!bitset[i]);
    }
  }

  void testFindBits() {
    TestBitSet<kBitsPerWord * 5 + 2> bitset;
    size_t size = bitset.size();
    MOZ_RELEASE_ASSERT(bitset.IsEmpty());
    MOZ_RELEASE_ASSERT(bitset.FindFirst() == SIZE_MAX);
    MOZ_RELEASE_ASSERT(bitset.FindLast() == SIZE_MAX);
    MOZ_RELEASE_ASSERT(bitset.FindNext(0) == SIZE_MAX);
    MOZ_RELEASE_ASSERT(bitset.FindNext(size - 1) == SIZE_MAX);
    MOZ_RELEASE_ASSERT(bitset.FindPrev(0) == SIZE_MAX);
    MOZ_RELEASE_ASSERT(bitset.FindPrev(size - 1) == SIZE_MAX);

    // Test with single bit set.
    for (size_t i = 0; i < size; i += 5) {
      bitset[i] = true;
      MOZ_RELEASE_ASSERT(bitset.FindFirst() == i);
      MOZ_RELEASE_ASSERT(bitset.FindLast() == i);
      MOZ_RELEASE_ASSERT(bitset.FindNext(i) == i);
      MOZ_RELEASE_ASSERT(bitset.FindPrev(i) == i);
      MOZ_RELEASE_ASSERT(bitset.FindNext(0) == i);
      MOZ_RELEASE_ASSERT(bitset.FindPrev(size - 1) == i);
      if (i != 0) {
        MOZ_RELEASE_ASSERT(bitset.FindNext(i - 1) == i);
        MOZ_RELEASE_ASSERT(bitset.FindPrev(i - 1) == SIZE_MAX);
      }
      if (i != size - 1) {
        MOZ_RELEASE_ASSERT(bitset.FindNext(i + 1) == SIZE_MAX);
        MOZ_RELEASE_ASSERT(bitset.FindPrev(i + 1) == i);
      }
      bitset[i] = false;
    }

    // Test with multiple bits set.
    //
    // This creates bits pattern with every |i|th bit set and checks the result
    // of calling FindNext/FindPrev at and around each set bit.
    for (size_t i = 3; i < size; i += 5) {
      bitset.ResetAll();
      for (size_t j = 0; j < size; j += i) {
        bitset[j] = true;
      }
      for (size_t j = 0; j < size; j += i) {
        // Test FindNext/FindPrev on the current bit.
        MOZ_RELEASE_ASSERT(bitset[j]);
        MOZ_RELEASE_ASSERT(bitset.FindNext(j) == j);
        MOZ_RELEASE_ASSERT(bitset.FindPrev(j) == j);
        // Test FindNext/FindPrev on the previous bit.
        if (j != 0) {
          MOZ_RELEASE_ASSERT(bitset[j - i]);
          MOZ_RELEASE_ASSERT(bitset.FindNext(j - 1) == j);
          MOZ_RELEASE_ASSERT(bitset.FindPrev(j - 1) == j - i);
        }
        // Test FindNext/FindPrev on the next bit.
        if (j + i < size) {
          MOZ_RELEASE_ASSERT(bitset[j + i]);
          MOZ_RELEASE_ASSERT(bitset.FindNext(j + 1) == j + i);
          MOZ_RELEASE_ASSERT(bitset.FindPrev(j + 1) == j);
        }
      }
    }
  }

  void testFormatting() {
    TestBitSet<30> bitset;

    auto&& check_fmt = [](const auto& bitset, const char* expected) {
      auto formatted = fmt::format("{}", bitset);
      MOZ_RELEASE_ASSERT(formatted == expected);
    };

    // No bits set.
    check_fmt(bitset, "{}");

    // Single bit set.
    bitset[0] = true;
    check_fmt(bitset, "{0}");

    bitset.ResetAll();
    bitset[7] = true;
    check_fmt(bitset, "{7}");

    bitset.ResetAll();
    bitset[23] = true;
    check_fmt(bitset, "{23}");

    // Multiple bits.
    bitset[0] = true;
    check_fmt(bitset, "{0,23}");

    bitset[1] = true;
    check_fmt(bitset, "{0,1,23}");

    bitset[2] = true;
    check_fmt(bitset, "{0-2,23}");

    bitset[22] = true;
    check_fmt(bitset, "{0-2,22,23}");

    bitset[24] = true;
    check_fmt(bitset, "{0-2,22-24}");

    bitset[1] = false;
    check_fmt(bitset, "{0,2,22-24}");

    // Bit ranges not anchored at the beginning.
    bitset.ResetAll();
    bitset[8] = true;
    check_fmt(bitset, "{8}");

    bitset[9] = true;
    check_fmt(bitset, "{8,9}");

    bitset[10] = true;
    check_fmt(bitset, "{8-10}");

    // Up against the end.
    bitset.ResetAll();
    bitset[29] = true;
    check_fmt(bitset, "{29}");

    bitset[28] = true;
    check_fmt(bitset, "{28,29}");

    bitset[27] = true;
    check_fmt(bitset, "{27-29}");

    // All bits on.
    for (size_t i = 0; i < 30; i++) {
      bitset[i] = true;
    }
    check_fmt(bitset, "{0-29}");

    // Allow formatting flags.
    auto formatted = fmt::format("{:#x}", bitset);
    MOZ_RELEASE_ASSERT(formatted == "{0x0-0x1d}");
  }

  void testCount() {
    testCountForSize<1>();
    testCountForSize<kBitsPerWord>();
    testCountForSize<kBitsPerWord + 1>();
  }

  template <size_t N>
  void testCountForSize() {
    TestBitSet<N> bits;
    MOZ_RELEASE_ASSERT(bits.Count() == 0);
    bits.SetAll();
    MOZ_RELEASE_ASSERT(bits.Count() == N);
    bits.ResetAll();
    bits[0] = true;
    MOZ_RELEASE_ASSERT(bits.Count() == 1);
    bits[0] = false;
    bits[N - 1] = true;
    MOZ_RELEASE_ASSERT(bits.Count() == 1);
  }

  void testComparison() {
    testComparisonForSize<1>();
    testComparisonForSize<kBitsPerWord>();
    testComparisonForSize<kBitsPerWord + 1>();
  }

  template <size_t N>
  void testComparisonForSize() {
    TestBitSet<N> a;
    TestBitSet<N> b;
    MOZ_RELEASE_ASSERT(a == b);
    MOZ_RELEASE_ASSERT(!(a != b));
    a[0] = true;
    MOZ_RELEASE_ASSERT(a != b);
    MOZ_RELEASE_ASSERT(!(a == b));
    b[0] = true;
    MOZ_RELEASE_ASSERT(a == b);
    MOZ_RELEASE_ASSERT(!(a != b));
    a.SetAll();
    b.SetAll();
    MOZ_RELEASE_ASSERT(a == b);
    MOZ_RELEASE_ASSERT(!(a != b));
    a[N - 1] = false;
    MOZ_RELEASE_ASSERT(a != b);
    MOZ_RELEASE_ASSERT(!(a == b));
    b[N - 1] = false;
    MOZ_RELEASE_ASSERT(a == b);
    MOZ_RELEASE_ASSERT(!(a != b));
  }

  void testLogical() {
    testLogicalForSize<2>();
    testLogicalForSize<kBitsPerWord>();
    testLogicalForSize<kBitsPerWord + 1>();
  }

  template <size_t N>
  void testLogicalForSize() {
    TestBitSet<N> none;
    TestBitSet<N> all;
    all.SetAll();
    TestBitSet<N> some;
    for (size_t i = 0; i < N; i += 2) {
      some[i] = true;
    }

    // operator& is implemented in terms of operator&= (and likewise for
    // operator|) so this tests both.

    MOZ_RELEASE_ASSERT(none.Count() == 0);
    MOZ_RELEASE_ASSERT(all.Count() == N);
    MOZ_RELEASE_ASSERT(some.Count() == (N + 1) / 2);

    MOZ_RELEASE_ASSERT((none & none) == none);
    MOZ_RELEASE_ASSERT((none & all) == none);
    MOZ_RELEASE_ASSERT((none & some) == none);

    MOZ_RELEASE_ASSERT((all & none) == none);
    MOZ_RELEASE_ASSERT((all & all) == all);
    MOZ_RELEASE_ASSERT((all & some) == some);

    MOZ_RELEASE_ASSERT((some & none) == none);
    MOZ_RELEASE_ASSERT((some & all) == some);
    MOZ_RELEASE_ASSERT((some & some) == some);

    MOZ_RELEASE_ASSERT((none | none) == none);
    MOZ_RELEASE_ASSERT((none | all) == all);
    MOZ_RELEASE_ASSERT((none | some) == some);

    MOZ_RELEASE_ASSERT((all | none) == all);
    MOZ_RELEASE_ASSERT((all | all) == all);
    MOZ_RELEASE_ASSERT((all | some) == all);

    MOZ_RELEASE_ASSERT((some | none) == some);
    MOZ_RELEASE_ASSERT((some | all) == all);
    MOZ_RELEASE_ASSERT((some | some) == some);

    MOZ_RELEASE_ASSERT(~none == all);
    MOZ_RELEASE_ASSERT(~all == none);
    MOZ_RELEASE_ASSERT(~some != some);
    MOZ_RELEASE_ASSERT(~some != all);
    MOZ_RELEASE_ASSERT(~some != none);
  }

  void runTests() {
    testLength();
    testConstructAndAssign();
    testSetBit();
    testFindBits();
    testCount();
    testComparison();
    testLogical();
    testFormatting();
  }
};

int main() {
  BitSetSuite<uint8_t>().runTests();
  BitSetSuite<uint32_t>().runTests();
  BitSetSuite<uint64_t>().runTests();
  BitSetSuite<Atomic<uint32_t>>().runTests();
  BitSetSuite<Atomic<uint64_t>>().runTests();

  return 0;
}
