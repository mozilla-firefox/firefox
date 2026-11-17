/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <utility>

#include "mozilla/IntegerRange.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Vector.h"

using mozilla::IntegerRange;
using mozilla::UniquePtr;
using mozilla::Vector;
using mozilla::detail::VectorTesting;

struct mozilla::detail::VectorTesting {
  static void testReserved();
  static void testConstRange();
  static void testEmplaceBack();
  static void testReverse();
  static void testExtractRawBuffer();
  static void testExtractOrCopyRawBuffer();
  static void testReplaceRawBuffer();
  static void testInsert();
  static void testErase();
  static void testShrinkStorageToFit();
  static void testAppend();
};

void mozilla::detail::VectorTesting::testReserved() {
#ifdef DEBUG
  Vector<bool> bv;
  MOZ_RELEASE_ASSERT(bv.reserved() == 0);

  MOZ_RELEASE_ASSERT(bv.append(true));
  MOZ_RELEASE_ASSERT(bv.reserved() == 1);

  Vector<bool> otherbv;
  MOZ_RELEASE_ASSERT(otherbv.append(false));
  MOZ_RELEASE_ASSERT(otherbv.append(true));
  MOZ_RELEASE_ASSERT(bv.appendAll(otherbv));
  MOZ_RELEASE_ASSERT(bv.reserved() == 3);

  MOZ_RELEASE_ASSERT(bv.reserve(5));
  MOZ_RELEASE_ASSERT(bv.reserved() == 5);

  MOZ_RELEASE_ASSERT(bv.reserve(1));
  MOZ_RELEASE_ASSERT(bv.reserved() == 5);

  Vector<bool> bv2(std::move(bv));
  MOZ_RELEASE_ASSERT(bv.reserved() == 0);
  MOZ_RELEASE_ASSERT(bv2.reserved() == 5);

  bv2.clearAndFree();
  MOZ_RELEASE_ASSERT(bv2.reserved() == 0);

  Vector<int, 42> iv;
  MOZ_RELEASE_ASSERT(iv.reserved() == 0);

  MOZ_RELEASE_ASSERT(iv.append(17));
  MOZ_RELEASE_ASSERT(iv.reserved() == 1);

  Vector<int, 42> otheriv;
  MOZ_RELEASE_ASSERT(otheriv.append(42));
  MOZ_RELEASE_ASSERT(otheriv.append(37));
  MOZ_RELEASE_ASSERT(iv.appendAll(otheriv));
  MOZ_RELEASE_ASSERT(iv.reserved() == 3);

  MOZ_RELEASE_ASSERT(iv.reserve(5));
  MOZ_RELEASE_ASSERT(iv.reserved() == 5);

  MOZ_RELEASE_ASSERT(iv.reserve(1));
  MOZ_RELEASE_ASSERT(iv.reserved() == 5);

  MOZ_RELEASE_ASSERT(iv.reserve(55));
  MOZ_RELEASE_ASSERT(iv.reserved() == 55);

  Vector<int, 42> iv2(std::move(iv));
  MOZ_RELEASE_ASSERT(iv.reserved() == 0);
  MOZ_RELEASE_ASSERT(iv2.reserved() == 55);

  iv2.clearAndFree();
  MOZ_RELEASE_ASSERT(iv2.reserved() == 0);
#endif
}

void mozilla::detail::VectorTesting::testConstRange() {
#ifdef DEBUG
  Vector<int> vec;

  for (int i = 0; i < 10; i++) {
    MOZ_RELEASE_ASSERT(vec.append(i));
  }

  const auto& vecRef = vec;

  Vector<int>::ConstRange range = vecRef.all();
  for (int i = 0; i < 10; i++) {
    MOZ_RELEASE_ASSERT(!range.empty());
    MOZ_RELEASE_ASSERT(range.front() == i);
    range.popFront();
  }
#endif
}

namespace {

struct S {
  size_t j;
  UniquePtr<size_t> k;

  static size_t constructCount;
  static size_t moveCount;
  static size_t destructCount;

  static void resetCounts() {
    constructCount = 0;
    moveCount = 0;
    destructCount = 0;
  }

  S(size_t j, size_t k) : j(j), k(std::make_unique<size_t>(k)) {
    constructCount++;
  }

  S(S&& rhs) : j(rhs.j), k(std::move(rhs.k)) {
    rhs.j = 0;
    rhs.k.reset(nullptr);
    moveCount++;
  }

  ~S() { destructCount++; }

  S& operator=(S&& rhs) {
    j = rhs.j;
    rhs.j = 0;
    k = std::move(rhs.k);
    rhs.k.reset();
    moveCount++;
    return *this;
  }

  bool operator==(const S& rhs) const { return j == rhs.j && *k == *rhs.k; }

  S(const S&) = delete;
  S& operator=(const S&) = delete;
};

size_t S::constructCount = 0;
size_t S::moveCount = 0;
size_t S::destructCount = 0;

}  // namespace

void mozilla::detail::VectorTesting::testEmplaceBack() {
  S::resetCounts();

  Vector<S> vec;
  MOZ_RELEASE_ASSERT(vec.reserve(20));

  for (size_t i = 0; i < 10; i++) {
    S s(i, i * i);
    MOZ_RELEASE_ASSERT(vec.append(std::move(s)));
  }

  MOZ_RELEASE_ASSERT(vec.length() == 10);
  MOZ_RELEASE_ASSERT(S::constructCount == 10);
  MOZ_RELEASE_ASSERT(S::moveCount == 10);

  for (size_t i = 10; i < 20; i++) {
    MOZ_RELEASE_ASSERT(vec.emplaceBack(i, i * i));
  }

  MOZ_RELEASE_ASSERT(vec.length() == 20);
  MOZ_RELEASE_ASSERT(S::constructCount == 20);
  MOZ_RELEASE_ASSERT(S::moveCount == 10);

  for (size_t i = 0; i < 20; i++) {
    MOZ_RELEASE_ASSERT(vec[i].j == i);
    MOZ_RELEASE_ASSERT(*vec[i].k == i * i);
  }
}

void mozilla::detail::VectorTesting::testReverse() {
  // Use UniquePtr to make sure that reverse() can handler move-only types.
  Vector<UniquePtr<uint8_t>, 0> vec;

  // Reverse an odd number of elements.

  for (uint8_t i = 0; i < 5; i++) {
    auto p = std::make_unique<uint8_t>(i);
    MOZ_RELEASE_ASSERT(p);
    MOZ_RELEASE_ASSERT(vec.append(std::move(p)));
  }

  vec.reverse();

  MOZ_RELEASE_ASSERT(*vec[0] == 4);
  MOZ_RELEASE_ASSERT(*vec[1] == 3);
  MOZ_RELEASE_ASSERT(*vec[2] == 2);
  MOZ_RELEASE_ASSERT(*vec[3] == 1);
  MOZ_RELEASE_ASSERT(*vec[4] == 0);

  // Reverse an even number of elements.

  vec.popBack();
  vec.reverse();

  MOZ_RELEASE_ASSERT(*vec[0] == 1);
  MOZ_RELEASE_ASSERT(*vec[1] == 2);
  MOZ_RELEASE_ASSERT(*vec[2] == 3);
  MOZ_RELEASE_ASSERT(*vec[3] == 4);

  // Reverse an empty vector.

  vec.clear();
  MOZ_RELEASE_ASSERT(vec.length() == 0);
  vec.reverse();
  MOZ_RELEASE_ASSERT(vec.length() == 0);

  // Reverse a vector using only inline storage.

  Vector<UniquePtr<uint8_t>, 5> vec2;
  for (uint8_t i = 0; i < 5; i++) {
    auto p = std::make_unique<uint8_t>(i);
    MOZ_RELEASE_ASSERT(p);
    MOZ_RELEASE_ASSERT(vec2.append(std::move(p)));
  }

  vec2.reverse();

  MOZ_RELEASE_ASSERT(*vec2[0] == 4);
  MOZ_RELEASE_ASSERT(*vec2[1] == 3);
  MOZ_RELEASE_ASSERT(*vec2[2] == 2);
  MOZ_RELEASE_ASSERT(*vec2[3] == 1);
  MOZ_RELEASE_ASSERT(*vec2[4] == 0);
}

void mozilla::detail::VectorTesting::testExtractRawBuffer() {
  S::resetCounts();

  Vector<S, 5> vec;
  MOZ_RELEASE_ASSERT(vec.reserve(5));
  for (size_t i = 0; i < 5; i++) {
    vec.infallibleEmplaceBack(i, i * i);
  }
  MOZ_RELEASE_ASSERT(vec.length() == 5);
  MOZ_ASSERT(vec.reserved() == 5);
  MOZ_RELEASE_ASSERT(S::constructCount == 5);
  MOZ_RELEASE_ASSERT(S::moveCount == 0);
  MOZ_RELEASE_ASSERT(S::destructCount == 0);

  S* buf = vec.extractRawBuffer();
  MOZ_RELEASE_ASSERT(!buf);
  MOZ_RELEASE_ASSERT(vec.length() == 5);
  MOZ_ASSERT(vec.reserved() == 5);
  MOZ_RELEASE_ASSERT(S::constructCount == 5);
  MOZ_RELEASE_ASSERT(S::moveCount == 0);
  MOZ_RELEASE_ASSERT(S::destructCount == 0);

  MOZ_RELEASE_ASSERT(vec.reserve(10));
  for (size_t i = 5; i < 10; i++) {
    vec.infallibleEmplaceBack(i, i * i);
  }
  MOZ_RELEASE_ASSERT(vec.length() == 10);
  MOZ_ASSERT(vec.reserved() == 10);
  MOZ_RELEASE_ASSERT(S::constructCount == 10);
  MOZ_RELEASE_ASSERT(S::moveCount == 5);
  MOZ_RELEASE_ASSERT(S::destructCount == 5);

  buf = vec.extractRawBuffer();
  MOZ_RELEASE_ASSERT(buf);
  MOZ_RELEASE_ASSERT(vec.length() == 0);
  MOZ_ASSERT(vec.reserved() == 0);
  MOZ_RELEASE_ASSERT(S::constructCount == 10);
  MOZ_RELEASE_ASSERT(S::moveCount == 5);
  MOZ_RELEASE_ASSERT(S::destructCount == 5);

  for (size_t i = 0; i < 10; i++) {
    MOZ_RELEASE_ASSERT(buf[i].j == i);
    MOZ_RELEASE_ASSERT(*buf[i].k == i * i);
  }

  free(buf);
}

void mozilla::detail::VectorTesting::testExtractOrCopyRawBuffer() {
  S::resetCounts();

  Vector<S, 5> vec;
  MOZ_RELEASE_ASSERT(vec.reserve(5));
  for (size_t i = 0; i < 5; i++) {
    vec.infallibleEmplaceBack(i, i * i);
  }
  MOZ_RELEASE_ASSERT(vec.length() == 5);
  MOZ_ASSERT(vec.reserved() == 5);
  MOZ_RELEASE_ASSERT(S::constructCount == 5);
  MOZ_RELEASE_ASSERT(S::moveCount == 0);
  MOZ_RELEASE_ASSERT(S::destructCount == 0);

  S* buf = vec.extractOrCopyRawBuffer();
  MOZ_RELEASE_ASSERT(buf);
  MOZ_RELEASE_ASSERT(vec.length() == 0);
  MOZ_ASSERT(vec.reserved() == 0);
  MOZ_RELEASE_ASSERT(S::constructCount == 5);
  MOZ_RELEASE_ASSERT(S::moveCount == 5);
  MOZ_RELEASE_ASSERT(S::destructCount == 5);

  for (size_t i = 0; i < 5; i++) {
    MOZ_RELEASE_ASSERT(buf[i].j == i);
    MOZ_RELEASE_ASSERT(*buf[i].k == i * i);
  }

  S::resetCounts();

  MOZ_RELEASE_ASSERT(vec.reserve(10));
  for (size_t i = 0; i < 10; i++) {
    vec.infallibleEmplaceBack(i, i * i);
  }
  MOZ_RELEASE_ASSERT(vec.length() == 10);
  MOZ_ASSERT(vec.reserved() == 10);
  MOZ_RELEASE_ASSERT(S::constructCount == 10);
  MOZ_RELEASE_ASSERT(S::moveCount == 0);
  MOZ_RELEASE_ASSERT(S::destructCount == 0);

  buf = vec.extractOrCopyRawBuffer();
  MOZ_RELEASE_ASSERT(buf);
  MOZ_RELEASE_ASSERT(vec.length() == 0);
  MOZ_ASSERT(vec.reserved() == 0);
  MOZ_RELEASE_ASSERT(S::constructCount == 10);
  MOZ_RELEASE_ASSERT(S::moveCount == 0);
  MOZ_RELEASE_ASSERT(S::destructCount == 0);

  for (size_t i = 0; i < 10; i++) {
    MOZ_RELEASE_ASSERT(buf[i].j == i);
    MOZ_RELEASE_ASSERT(*buf[i].k == i * i);
  }

  free(buf);
}

void mozilla::detail::VectorTesting::testReplaceRawBuffer() {
  S::resetCounts();

  S* s = nullptr;

  {
    Vector<S> v;
    MOZ_RELEASE_ASSERT(v.reserve(4));
    v.infallibleEmplaceBack(1, 2);
    v.infallibleEmplaceBack(3, 4);
    MOZ_RELEASE_ASSERT(S::constructCount == 2);
    s = v.extractRawBuffer();
  }

  MOZ_RELEASE_ASSERT(S::constructCount == 2);
  MOZ_RELEASE_ASSERT(S::moveCount == 0);
  MOZ_RELEASE_ASSERT(S::destructCount == 0);

  {
    Vector<S, 10> v;
    v.replaceRawBuffer(s, 2);
    MOZ_ASSERT(v.reserved() == 2);
    MOZ_RELEASE_ASSERT(v.length() == 2);
    MOZ_RELEASE_ASSERT(v.capacity() == 10);
    MOZ_RELEASE_ASSERT(v[0].j == 1);
    MOZ_RELEASE_ASSERT(v[1].j == 3);
    MOZ_RELEASE_ASSERT(S::destructCount == 2);
  }

  MOZ_RELEASE_ASSERT(S::constructCount == 2);
  MOZ_RELEASE_ASSERT(S::moveCount == 2);
  MOZ_RELEASE_ASSERT(S::destructCount == 4);

  S::resetCounts();

  {
    Vector<S, 2> v;
    MOZ_RELEASE_ASSERT(v.reserve(4));
    v.infallibleEmplaceBack(9, 10);
    MOZ_RELEASE_ASSERT(S::constructCount == 1);
    s = v.extractRawBuffer();
    MOZ_RELEASE_ASSERT(S::constructCount == 1);
    MOZ_RELEASE_ASSERT(S::moveCount == 0);
  }

  MOZ_RELEASE_ASSERT(S::destructCount == 0);

  {
    Vector<S> v;
    v.replaceRawBuffer(s, 1, 4);
    MOZ_ASSERT(v.reserved() == 4);
    MOZ_RELEASE_ASSERT(v.length() == 1);
    MOZ_RELEASE_ASSERT(v.capacity() == 4);
    MOZ_RELEASE_ASSERT(v[0].j == 9);
    for (size_t i = 0; i < 5; i++) MOZ_RELEASE_ASSERT(v.emplaceBack(i, i));
    MOZ_ASSERT(v.reserved() == 6);
    MOZ_RELEASE_ASSERT(v.length() == 6);
    MOZ_RELEASE_ASSERT(S::constructCount == 6);
    MOZ_RELEASE_ASSERT(S::moveCount == 4);
    MOZ_RELEASE_ASSERT(S::destructCount == 4);
  }

  MOZ_RELEASE_ASSERT(S::destructCount == 10);
}

void mozilla::detail::VectorTesting::testInsert() {
  S::resetCounts();

  Vector<S, 8> vec;
  MOZ_RELEASE_ASSERT(vec.reserve(8));
  for (size_t i = 0; i < 7; i++) {
    vec.infallibleEmplaceBack(i, i * i);
  }

  MOZ_RELEASE_ASSERT(vec.length() == 7);
  MOZ_ASSERT(vec.reserved() == 8);
  MOZ_RELEASE_ASSERT(S::constructCount == 7);
  MOZ_RELEASE_ASSERT(S::moveCount == 0);
  MOZ_RELEASE_ASSERT(S::destructCount == 0);

  S s(42, 43);
  MOZ_RELEASE_ASSERT(vec.insert(vec.begin() + 4, std::move(s)));

  for (size_t i = 0; i < vec.length(); i++) {
    const S& s = vec[i];
    MOZ_RELEASE_ASSERT(s.k);
    if (i < 4) {
      MOZ_RELEASE_ASSERT(s.j == i && *s.k == i * i);
    } else if (i == 4) {
      MOZ_RELEASE_ASSERT(s.j == 42 && *s.k == 43);
    } else {
      MOZ_RELEASE_ASSERT(s.j == i - 1 && *s.k == (i - 1) * (i - 1));
    }
  }

  MOZ_RELEASE_ASSERT(vec.length() == 8);
  MOZ_ASSERT(vec.reserved() == 8);
  MOZ_RELEASE_ASSERT(S::constructCount == 8);
  MOZ_RELEASE_ASSERT(S::moveCount == 1 /* move in insert() call */ +
                                         1 /* move the back() element */ +
                                         3 /* elements to shift */);
  MOZ_RELEASE_ASSERT(S::destructCount == 1);
}

void mozilla::detail::VectorTesting::testErase() {
  S::resetCounts();

  Vector<S, 8> vec;
  MOZ_RELEASE_ASSERT(vec.reserve(8));
  for (size_t i = 0; i < 7; i++) {
    vec.infallibleEmplaceBack(i, i * i);
  }

  // vec: [0, 1, 2, 3, 4, 5, 6]
  MOZ_RELEASE_ASSERT(vec.length() == 7);
  MOZ_ASSERT(vec.reserved() == 8);
  MOZ_RELEASE_ASSERT(S::constructCount == 7);
  MOZ_RELEASE_ASSERT(S::moveCount == 0);
  MOZ_RELEASE_ASSERT(S::destructCount == 0);
  S::resetCounts();

  vec.erase(&vec[4]);
  // vec: [0, 1, 2, 3, 5, 6]
  MOZ_RELEASE_ASSERT(vec.length() == 6);
  MOZ_ASSERT(vec.reserved() == 8);
  MOZ_RELEASE_ASSERT(S::constructCount == 0);
  // 5 and 6 should have been moved into 4 and 5.
  MOZ_RELEASE_ASSERT(S::moveCount == 2);
  MOZ_RELEASE_ASSERT(S::destructCount == 1);
  MOZ_RELEASE_ASSERT(vec[4] == S(5, 5 * 5));
  MOZ_RELEASE_ASSERT(vec[5] == S(6, 6 * 6));
  S::resetCounts();

  vec.erase(&vec[3], &vec[5]);
  // vec: [0, 1, 2, 6]
  MOZ_RELEASE_ASSERT(vec.length() == 4);
  MOZ_ASSERT(vec.reserved() == 8);
  MOZ_RELEASE_ASSERT(S::constructCount == 0);
  // 6 should have been moved into 3.
  MOZ_RELEASE_ASSERT(S::moveCount == 1);
  MOZ_RELEASE_ASSERT(S::destructCount == 2);
  MOZ_RELEASE_ASSERT(vec[3] == S(6, 6 * 6));

  S s2(2, 2 * 2);
  S::resetCounts();

  vec.eraseIfEqual(s2);
  // vec: [0, 1, 6]
  MOZ_RELEASE_ASSERT(vec.length() == 3);
  MOZ_ASSERT(vec.reserved() == 8);
  MOZ_RELEASE_ASSERT(S::constructCount == 0);
  // 6 should have been moved into 2.
  MOZ_RELEASE_ASSERT(S::moveCount == 1);
  MOZ_RELEASE_ASSERT(S::destructCount == 1);
  MOZ_RELEASE_ASSERT(vec[2] == S(6, 6 * 6));
  S::resetCounts();

  // Predicate to find one element.
  vec.eraseIf([](const S& s) { return s.j == 1; });
  // vec: [0, 6]
  MOZ_RELEASE_ASSERT(vec.length() == 2);
  MOZ_ASSERT(vec.reserved() == 8);
  MOZ_RELEASE_ASSERT(S::constructCount == 0);
  // 6 should have been moved into 1.
  MOZ_RELEASE_ASSERT(S::moveCount == 1);
  MOZ_RELEASE_ASSERT(S::destructCount == 1);
  MOZ_RELEASE_ASSERT(vec[1] == S(6, 6 * 6));
  S::resetCounts();

  // Generic predicate that flags everything.
  vec.eraseIf([](auto&&) { return true; });
  // vec: []
  MOZ_RELEASE_ASSERT(vec.length() == 0);
  MOZ_ASSERT(vec.reserved() == 8);
  MOZ_RELEASE_ASSERT(S::constructCount == 0);
  MOZ_RELEASE_ASSERT(S::moveCount == 0);
  MOZ_RELEASE_ASSERT(S::destructCount == 2);

  for (size_t i = 0; i < 7; i++) {
    vec.infallibleEmplaceBack(i, i * i);
  }
  // vec: [0, 1, 2, 3, 4, 5, 6]
  MOZ_RELEASE_ASSERT(vec.length() == 7);
  S::resetCounts();

  // Predicate that flags all even numbers.
  vec.eraseIf([](const S& s) { return s.j % 2 == 0; });
  // vec: [1 (was 0), 3 (was 1), 5 (was 2)]
  MOZ_RELEASE_ASSERT(vec.length() == 3);
  MOZ_ASSERT(vec.reserved() == 8);
  MOZ_RELEASE_ASSERT(S::constructCount == 0);
  MOZ_RELEASE_ASSERT(S::moveCount == 3);
  MOZ_RELEASE_ASSERT(S::destructCount == 4);
}

void mozilla::detail::VectorTesting::testShrinkStorageToFit() {
  // Vectors not using inline storage realloc capacity to exact length.
  {
    Vector<int, 0> v1;
    MOZ_RELEASE_ASSERT(v1.reserve(10));
    v1.infallibleAppend(1);
    MOZ_ASSERT(v1.reserved() == 10);
    MOZ_RELEASE_ASSERT(v1.length() == 1);
    MOZ_RELEASE_ASSERT(v1.capacity() >= 10);
    v1.shrinkStorageToFit();
    MOZ_ASSERT(v1.reserved() == 1);
    MOZ_RELEASE_ASSERT(v1.length() == 1);
    MOZ_RELEASE_ASSERT(v1.capacity() == 1);
  }

  // Vectors using inline storage do nothing.
  {
    Vector<int, 2> v2;
    MOZ_RELEASE_ASSERT(v2.reserve(2));
    v2.infallibleAppend(1);
    MOZ_ASSERT(v2.reserved() == 2);
    MOZ_RELEASE_ASSERT(v2.length() == 1);
    MOZ_RELEASE_ASSERT(v2.capacity() == 2);
    v2.shrinkStorageToFit();
    MOZ_ASSERT(v2.reserved() == 2);
    MOZ_RELEASE_ASSERT(v2.length() == 1);
    MOZ_RELEASE_ASSERT(v2.capacity() == 2);
  }

  // shrinkStorageToFit uses inline storage if possible.
  {
    Vector<int, 2> v;
    MOZ_RELEASE_ASSERT(v.reserve(4));
    v.infallibleAppend(1);
    MOZ_ASSERT(v.reserved() == 4);
    MOZ_RELEASE_ASSERT(v.length() == 1);
    MOZ_RELEASE_ASSERT(v.capacity() >= 4);
    v.shrinkStorageToFit();
    MOZ_ASSERT(v.reserved() == 1);
    MOZ_RELEASE_ASSERT(v.length() == 1);
    MOZ_RELEASE_ASSERT(v.capacity() == 2);
  }

  // Non-pod shrinking to non-inline storage.
  {
    static size_t sConstructCounter = 0;
    static size_t sCopyCounter = 0;
    static size_t sMoveCounter = 0;
    static size_t sDestroyCounter = 0;
    struct NonPod {
      int mSomething = 10;

      NonPod() { sConstructCounter++; }

      NonPod(const NonPod& aOther) : mSomething(aOther.mSomething) {
        sCopyCounter++;
      }
      NonPod(NonPod&& aOther) : mSomething(aOther.mSomething) {
        sMoveCounter++;
      }
      ~NonPod() { sDestroyCounter++; }
    };

    Vector<NonPod, 5> v;
    MOZ_RELEASE_ASSERT(v.reserve(10));
    for (size_t i = 0; i < 8; ++i) {
      v.infallibleEmplaceBack();
    }
    MOZ_RELEASE_ASSERT(sConstructCounter == 8);
    MOZ_RELEASE_ASSERT(sCopyCounter == 0);
    MOZ_RELEASE_ASSERT(sMoveCounter == 0);
    MOZ_RELEASE_ASSERT(sDestroyCounter == 0);
    MOZ_RELEASE_ASSERT(v.length() == 8);
    MOZ_ASSERT(v.reserved() == 10);
    MOZ_RELEASE_ASSERT(v.capacity() >= 10);
    MOZ_RELEASE_ASSERT(v.shrinkStorageToFit());

    MOZ_RELEASE_ASSERT(sConstructCounter == 8);
    MOZ_RELEASE_ASSERT(sCopyCounter == 0);
    MOZ_RELEASE_ASSERT(sMoveCounter == 8);
    MOZ_RELEASE_ASSERT(sDestroyCounter == 8);
    MOZ_RELEASE_ASSERT(v.length() == 8);
    MOZ_ASSERT(v.reserved() == 8);
    MOZ_RELEASE_ASSERT(v.capacity() == 8);
  }

  // Non-POD shrinking to inline storage.
  {
    static size_t sConstructCounter = 0;
    static size_t sCopyCounter = 0;
    static size_t sMoveCounter = 0;
    static size_t sDestroyCounter = 0;
    struct NonPod {
      int mSomething = 10;

      NonPod() { sConstructCounter++; }

      NonPod(const NonPod& aOther) : mSomething(aOther.mSomething) {
        sCopyCounter++;
      }
      NonPod(NonPod&& aOther) : mSomething(aOther.mSomething) {
        sMoveCounter++;
      }
      ~NonPod() { sDestroyCounter++; }
    };

    Vector<NonPod, 5> v;
    MOZ_RELEASE_ASSERT(v.reserve(10));
    for (size_t i = 0; i < 3; ++i) {
      v.infallibleEmplaceBack();
    }
    MOZ_RELEASE_ASSERT(sConstructCounter == 3);
    MOZ_RELEASE_ASSERT(sCopyCounter == 0);
    MOZ_RELEASE_ASSERT(sMoveCounter == 0);
    MOZ_RELEASE_ASSERT(sDestroyCounter == 0);
    MOZ_RELEASE_ASSERT(v.length() == 3);
    MOZ_ASSERT(v.reserved() == 10);
    MOZ_RELEASE_ASSERT(v.capacity() >= 10);
    MOZ_RELEASE_ASSERT(v.shrinkStorageToFit());

    MOZ_RELEASE_ASSERT(sConstructCounter == 3);
    MOZ_RELEASE_ASSERT(sCopyCounter == 0);
    MOZ_RELEASE_ASSERT(sMoveCounter == 3);
    MOZ_RELEASE_ASSERT(sDestroyCounter == 3);
    MOZ_RELEASE_ASSERT(v.length() == 3);
    MOZ_ASSERT(v.reserved() == 3);
    MOZ_RELEASE_ASSERT(v.capacity() == 5);
  }
}

void mozilla::detail::VectorTesting::testAppend() {
  // Test moving append/appendAll with a move-only type
  Vector<UniquePtr<int>> bv;
  for (const int val : IntegerRange<int>(0, 3)) {
    MOZ_RELEASE_ASSERT(bv.append(std::make_unique<int>(val)));
  }

  Vector<UniquePtr<int>> otherbv;
  for (const int val : IntegerRange<int>(3, 8)) {
    MOZ_RELEASE_ASSERT(otherbv.append(std::make_unique<int>(val)));
  }
  MOZ_RELEASE_ASSERT(bv.appendAll(std::move(otherbv)));

  MOZ_RELEASE_ASSERT(otherbv.length() == 0);
  MOZ_RELEASE_ASSERT(bv.length() == 8);
  for (const int val : IntegerRange<int>(0, 8)) {
    MOZ_RELEASE_ASSERT(*bv[val] == val);
  }
}

// Vector with no inline storage should occupy the absolute minimum space in
// non-debug builds.  (Debug adds a laundry list of other constraints, none
// directly relevant to shipping builds, that aren't worth precisely modeling.)
#ifndef DEBUG

template <typename T>
struct NoInlineStorageLayout {
  T* mBegin;
  size_t mLength;
  struct CRAndStorage {
    size_t mCapacity;
  } mTail;
};

// Only one of these should be necessary, but test a few of them for good
// measure.
static_assert(sizeof(Vector<int, 0>) == sizeof(NoInlineStorageLayout<int>),
              "Vector of int without inline storage shouldn't occupy dead "
              "space for that absence of storage");

static_assert(sizeof(Vector<bool, 0>) == sizeof(NoInlineStorageLayout<bool>),
              "Vector of bool without inline storage shouldn't occupy dead "
              "space for that absence of storage");

static_assert(sizeof(Vector<S, 0>) == sizeof(NoInlineStorageLayout<S>),
              "Vector of S without inline storage shouldn't occupy dead "
              "space for that absence of storage");

#endif  // DEBUG

static void TestVectorBeginNonNull() {
  // Vector::begin() should never return nullptr, to accommodate callers that
  // (either for hygiene, or for semantic reasons) need a non-null pointer even
  // for zero elements.

  Vector<bool, 0> bvec0;
  MOZ_RELEASE_ASSERT(bvec0.length() == 0);
  MOZ_RELEASE_ASSERT(bvec0.begin() != nullptr);

  Vector<bool, 1> bvec1;
  MOZ_RELEASE_ASSERT(bvec1.length() == 0);
  MOZ_RELEASE_ASSERT(bvec1.begin() != nullptr);

  Vector<bool, 64> bvec64;
  MOZ_RELEASE_ASSERT(bvec64.length() == 0);
  MOZ_RELEASE_ASSERT(bvec64.begin() != nullptr);

  Vector<int, 0> ivec0;
  MOZ_RELEASE_ASSERT(ivec0.length() == 0);
  MOZ_RELEASE_ASSERT(ivec0.begin() != nullptr);

  Vector<int, 1> ivec1;
  MOZ_RELEASE_ASSERT(ivec1.length() == 0);
  MOZ_RELEASE_ASSERT(ivec1.begin() != nullptr);

  Vector<int, 64> ivec64;
  MOZ_RELEASE_ASSERT(ivec64.length() == 0);
  MOZ_RELEASE_ASSERT(ivec64.begin() != nullptr);

  Vector<long, 0> lvec0;
  MOZ_RELEASE_ASSERT(lvec0.length() == 0);
  MOZ_RELEASE_ASSERT(lvec0.begin() != nullptr);

  Vector<long, 1> lvec1;
  MOZ_RELEASE_ASSERT(lvec1.length() == 0);
  MOZ_RELEASE_ASSERT(lvec1.begin() != nullptr);

  Vector<long, 64> lvec64;
  MOZ_RELEASE_ASSERT(lvec64.length() == 0);
  MOZ_RELEASE_ASSERT(lvec64.begin() != nullptr);

  // Vector<T, N> doesn't guarantee N inline elements -- the actual count is
  // capped so that any Vector fits in a not-crazy amount of space -- so the
  // code below won't overflow stacks or anything crazy.
  struct VeryBig {
    int array[16 * 1024 * 1024];
  };

  Vector<VeryBig, 0> vbvec0;
  MOZ_RELEASE_ASSERT(vbvec0.length() == 0);
  MOZ_RELEASE_ASSERT(vbvec0.begin() != nullptr);

  Vector<VeryBig, 1> vbvec1;
  MOZ_RELEASE_ASSERT(vbvec1.length() == 0);
  MOZ_RELEASE_ASSERT(vbvec1.begin() != nullptr);

  Vector<VeryBig, 64> vbvec64;
  MOZ_RELEASE_ASSERT(vbvec64.length() == 0);
  MOZ_RELEASE_ASSERT(vbvec64.begin() != nullptr);
}

int main() {
  VectorTesting::testReserved();
  VectorTesting::testConstRange();
  VectorTesting::testEmplaceBack();
  VectorTesting::testReverse();
  VectorTesting::testExtractRawBuffer();
  VectorTesting::testExtractOrCopyRawBuffer();
  VectorTesting::testReplaceRawBuffer();
  VectorTesting::testInsert();
  VectorTesting::testErase();
  VectorTesting::testShrinkStorageToFit();
  VectorTesting::testAppend();
  TestVectorBeginNonNull();
}
