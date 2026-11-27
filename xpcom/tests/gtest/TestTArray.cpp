/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/RefPtr.h"
#include "nsTArray.h"
#include "nsTHashMap.h"

using namespace mozilla;

namespace TestTArray {

struct Copyable {
  Copyable() : mDestructionCounter(nullptr) {}

  ~Copyable() {
    if (mDestructionCounter) {
      (*mDestructionCounter)++;
    }
  }

  Copyable(const Copyable&) = default;
  Copyable& operator=(const Copyable&) = default;

  uint32_t* mDestructionCounter;
};

struct Movable {
  Movable() : mDestructionCounter(nullptr) {}

  ~Movable() {
    if (mDestructionCounter) {
      (*mDestructionCounter)++;
    }
  }

  Movable(Movable&& aOther) : mDestructionCounter(aOther.mDestructionCounter) {
    aOther.mDestructionCounter = nullptr;
  }

  uint32_t* mDestructionCounter;
};

}  // namespace TestTArray

template <>
struct nsTArray_RelocationStrategy<TestTArray::Copyable> {
  using Type = nsTArray_RelocateUsingMoveConstructor<TestTArray::Copyable>;
};

template <>
struct nsTArray_RelocationStrategy<TestTArray::Movable> {
  using Type = nsTArray_RelocateUsingMoveConstructor<TestTArray::Movable>;
};

namespace TestTArray {

static_assert(
    std::is_same_v<nsTArray_RelocationStrategy<std::pair<int, int>>::Type,
                   nsTArray_RelocateUsingMemutils>);
static_assert(std::is_same_v<
              nsTArray_RelocationStrategy<std::pair<int, Movable>>::Type,
              nsTArray_RelocateUsingMoveConstructor<std::pair<int, Movable>>>);
static_assert(std::is_same_v<
              nsTArray_RelocationStrategy<std::pair<Movable, int>>::Type,
              nsTArray_RelocateUsingMoveConstructor<std::pair<Movable, int>>>);

constexpr int dummyArrayData[] = {4, 1, 2, 8};

static const nsTArray<int>& DummyArray() {
  static nsTArray<int> sArray;
  if (sArray.IsEmpty()) {
    sArray.AppendElements(dummyArrayData, std::size(dummyArrayData));
  }
  return sArray;
}

// This returns an invalid nsTArray with a huge length in order to test that
// fallible operations actually fail.
#ifdef DEBUG
static const nsTArray<int>& FakeHugeArray() {
  static nsTArray<int> sArray;
  if (sArray.IsEmpty()) {
    sArray.AppendElement();
    ((nsTArrayHeader*)sArray.DebugGetHeader())->mLength = UINT32_MAX;
  }
  return sArray;
}
#endif

TEST(TArray, int_AppendElements_PlainArray)
{
  nsTArray<int> array;

  int* ptr = array.AppendElements(dummyArrayData, std::size(dummyArrayData));
  ASSERT_EQ(&array[0], ptr);
  ASSERT_EQ(DummyArray(), array);

  ptr = array.AppendElements(dummyArrayData, std::size(dummyArrayData));
  ASSERT_EQ(&array[DummyArray().Length()], ptr);
  nsTArray<int> expected;
  expected.AppendElements(DummyArray());
  expected.AppendElements(DummyArray());
  ASSERT_EQ(expected, array);
}

TEST(TArray, int_AppendElements_PlainArray_Fallible)
{
  nsTArray<int> array;

  int* ptr =
      array.AppendElements(dummyArrayData, std::size(dummyArrayData), fallible);
  ASSERT_EQ(&array[0], ptr);
  ASSERT_EQ(DummyArray(), array);

  ptr =
      array.AppendElements(dummyArrayData, std::size(dummyArrayData), fallible);
  ASSERT_EQ(&array[DummyArray().Length()], ptr);
  nsTArray<int> expected;
  expected.AppendElements(DummyArray());
  expected.AppendElements(DummyArray());
  ASSERT_EQ(expected, array);
}

TEST(TArray, int_AppendElements_TArray_Copy)
{
  nsTArray<int> array;

  const nsTArray<int> temp(DummyArray().Clone());
  int* ptr = array.AppendElements(temp);
  ASSERT_EQ(&array[0], ptr);
  ASSERT_EQ(DummyArray(), array);
  ASSERT_FALSE(temp.IsEmpty());

  ptr = array.AppendElements(temp);
  ASSERT_EQ(&array[DummyArray().Length()], ptr);
  nsTArray<int> expected;
  expected.AppendElements(DummyArray());
  expected.AppendElements(DummyArray());
  ASSERT_EQ(expected, array);
  ASSERT_FALSE(temp.IsEmpty());
}

TEST(TArray, int_AppendElements_TArray_Copy_Fallible)
{
  nsTArray<int> array;

  const nsTArray<int> temp(DummyArray().Clone());
  int* ptr = array.AppendElements(temp, fallible);
  ASSERT_EQ(&array[0], ptr);
  ASSERT_EQ(DummyArray(), array);
  ASSERT_FALSE(temp.IsEmpty());

  ptr = array.AppendElements(temp, fallible);
  ASSERT_EQ(&array[DummyArray().Length()], ptr);
  nsTArray<int> expected;
  expected.AppendElements(DummyArray());
  expected.AppendElements(DummyArray());
  ASSERT_EQ(expected, array);
  ASSERT_FALSE(temp.IsEmpty());
}

TEST(TArray, int_AppendElements_TArray_Rvalue)
{
  nsTArray<int> array;

  nsTArray<int> temp(DummyArray().Clone());
  int* ptr = array.AppendElements(std::move(temp));
  ASSERT_EQ(&array[0], ptr);
  ASSERT_EQ(DummyArray(), array);
  ASSERT_TRUE(temp.IsEmpty());

  temp = DummyArray().Clone();
  ptr = array.AppendElements(std::move(temp));
  ASSERT_EQ(&array[DummyArray().Length()], ptr);
  nsTArray<int> expected;
  expected.AppendElements(DummyArray());
  expected.AppendElements(DummyArray());
  ASSERT_EQ(expected, array);
  ASSERT_TRUE(temp.IsEmpty());
}

TEST(TArray, int_AppendElements_TArray_Rvalue_Fallible)
{
  nsTArray<int> array;

  nsTArray<int> temp(DummyArray().Clone());
  int* ptr = array.AppendElements(std::move(temp), fallible);
  ASSERT_EQ(&array[0], ptr);
  ASSERT_EQ(DummyArray(), array);
  ASSERT_TRUE(temp.IsEmpty());

  temp = DummyArray().Clone();
  ptr = array.AppendElements(std::move(temp), fallible);
  ASSERT_EQ(&array[DummyArray().Length()], ptr);
  nsTArray<int> expected;
  expected.AppendElements(DummyArray());
  expected.AppendElements(DummyArray());
  ASSERT_EQ(expected, array);
  ASSERT_TRUE(temp.IsEmpty());
}

TEST(TArray, int_AppendElements_FallibleArray_Rvalue)
{
  nsTArray<int> array;

  FallibleTArray<int> temp;
  ASSERT_TRUE(temp.AppendElements(DummyArray(), fallible));
  int* ptr = array.AppendElements(std::move(temp));
  ASSERT_EQ(&array[0], ptr);
  ASSERT_EQ(DummyArray(), array);
  ASSERT_TRUE(temp.IsEmpty());

  ASSERT_TRUE(temp.AppendElements(DummyArray(), fallible));
  ptr = array.AppendElements(std::move(temp));
  ASSERT_EQ(&array[DummyArray().Length()], ptr);
  nsTArray<int> expected;
  expected.AppendElements(DummyArray());
  expected.AppendElements(DummyArray());
  ASSERT_EQ(expected, array);
  ASSERT_TRUE(temp.IsEmpty());
}

TEST(TArray, int_AppendElements_FallibleArray_Rvalue_Fallible)
{
  nsTArray<int> array;

  FallibleTArray<int> temp;
  ASSERT_TRUE(temp.AppendElements(DummyArray(), fallible));
  int* ptr = array.AppendElements(std::move(temp), fallible);
  ASSERT_EQ(&array[0], ptr);
  ASSERT_EQ(DummyArray(), array);
  ASSERT_TRUE(temp.IsEmpty());

  ASSERT_TRUE(temp.AppendElements(DummyArray(), fallible));
  ptr = array.AppendElements(std::move(temp), fallible);
  ASSERT_EQ(&array[DummyArray().Length()], ptr);
  nsTArray<int> expected;
  expected.AppendElements(DummyArray());
  expected.AppendElements(DummyArray());
  ASSERT_EQ(expected, array);
  ASSERT_TRUE(temp.IsEmpty());
}

TEST(TArray, AppendElementsSpan)
{
  nsTArray<int> array;

  nsTArray<int> temp(DummyArray().Clone());
  Span<int> span = temp;
  array.AppendElements(span);
  ASSERT_EQ(DummyArray(), array);

  Span<const int> constSpan = temp;
  array.AppendElements(constSpan);
  nsTArray<int> expected;
  expected.AppendElements(DummyArray());
  expected.AppendElements(DummyArray());
  ASSERT_EQ(expected, array);
}

TEST(TArray, int_AppendElement_NoElementArg)
{
  nsTArray<int> array;
  array.AppendElement();

  ASSERT_EQ(1u, array.Length());
}

TEST(TArray, int_AppendElement_NoElementArg_Fallible)
{
  nsTArray<int> array;
  ASSERT_NE(nullptr, array.AppendElement(fallible));

  ASSERT_EQ(1u, array.Length());
}

TEST(TArray, int_AppendElement_NoElementArg_Address)
{
  nsTArray<int> array;
  *array.AppendElement() = 42;

  ASSERT_EQ(1u, array.Length());
  ASSERT_EQ(42, array[0]);
}

TEST(TArray, int_AppendElement_NoElementArg_Fallible_Address)
{
  nsTArray<int> array;
  *array.AppendElement(fallible) = 42;

  ASSERT_EQ(1u, array.Length());
  ASSERT_EQ(42, array[0]);
}

TEST(TArray, int_AppendElement_ElementArg)
{
  nsTArray<int> array;
  array.AppendElement(42);

  ASSERT_EQ(1u, array.Length());
  ASSERT_EQ(42, array[0]);
}

TEST(TArray, int_AppendElement_ElementArg_Fallible)
{
  nsTArray<int> array;
  ASSERT_NE(nullptr, array.AppendElement(42, fallible));

  ASSERT_EQ(1u, array.Length());
  ASSERT_EQ(42, array[0]);
}

constexpr size_t dummyMovableArrayLength = 4;
uint32_t dummyMovableArrayDestructorCounter;

static nsTArray<Movable> DummyMovableArray() {
  nsTArray<Movable> res;
  res.SetLength(dummyMovableArrayLength);
  for (size_t i = 0; i < dummyMovableArrayLength; ++i) {
    res[i].mDestructionCounter = &dummyMovableArrayDestructorCounter;
  }
  return res;
}

TEST(TArray, Movable_AppendElements_TArray_Rvalue)
{
  dummyMovableArrayDestructorCounter = 0;
  {
    nsTArray<Movable> array;

    nsTArray<Movable> temp(DummyMovableArray());
    Movable* ptr = array.AppendElements(std::move(temp));
    ASSERT_EQ(&array[0], ptr);
    ASSERT_TRUE(temp.IsEmpty());

    temp = DummyMovableArray();
    ptr = array.AppendElements(std::move(temp));
    ASSERT_EQ(&array[dummyMovableArrayLength], ptr);
    ASSERT_TRUE(temp.IsEmpty());
  }
  ASSERT_EQ(2 * dummyMovableArrayLength, dummyMovableArrayDestructorCounter);
}

TEST(TArray, Movable_AppendElements_TArray_Rvalue_Fallible)
{
  dummyMovableArrayDestructorCounter = 0;
  {
    nsTArray<Movable> array;

    nsTArray<Movable> temp(DummyMovableArray());
    Movable* ptr = array.AppendElements(std::move(temp), fallible);
    ASSERT_EQ(&array[0], ptr);
    ASSERT_TRUE(temp.IsEmpty());

    temp = DummyMovableArray();
    ptr = array.AppendElements(std::move(temp), fallible);
    ASSERT_EQ(&array[dummyMovableArrayLength], ptr);
    ASSERT_TRUE(temp.IsEmpty());
  }
  ASSERT_EQ(2 * dummyMovableArrayLength, dummyMovableArrayDestructorCounter);
}

TEST(TArray, Movable_AppendElements_FallibleArray_Rvalue)
{
  dummyMovableArrayDestructorCounter = 0;
  {
    nsTArray<Movable> array;

    FallibleTArray<Movable> temp(DummyMovableArray());
    Movable* ptr = array.AppendElements(std::move(temp));
    ASSERT_EQ(&array[0], ptr);
    ASSERT_TRUE(temp.IsEmpty());

    temp = DummyMovableArray();
    ptr = array.AppendElements(std::move(temp));
    ASSERT_EQ(&array[dummyMovableArrayLength], ptr);
    ASSERT_TRUE(temp.IsEmpty());
  }
  ASSERT_EQ(2 * dummyMovableArrayLength, dummyMovableArrayDestructorCounter);
}

TEST(TArray, Movable_AppendElements_FallibleArray_Rvalue_Fallible)
{
  dummyMovableArrayDestructorCounter = 0;
  {
    nsTArray<Movable> array;

    FallibleTArray<Movable> temp(DummyMovableArray());
    Movable* ptr = array.AppendElements(std::move(temp), fallible);
    ASSERT_EQ(&array[0], ptr);
    ASSERT_TRUE(temp.IsEmpty());

    temp = DummyMovableArray();
    ptr = array.AppendElements(std::move(temp), fallible);
    ASSERT_EQ(&array[dummyMovableArrayLength], ptr);
    ASSERT_TRUE(temp.IsEmpty());
  }
  ASSERT_EQ(2 * dummyMovableArrayLength, dummyMovableArrayDestructorCounter);
}

TEST(TArray, Movable_AppendElement_NoElementArg)
{
  nsTArray<Movable> array;
  array.AppendElement();

  ASSERT_EQ(1u, array.Length());
}

TEST(TArray, Movable_AppendElement_NoElementArg_Fallible)
{
  nsTArray<Movable> array;
  ASSERT_NE(nullptr, array.AppendElement(fallible));

  ASSERT_EQ(1u, array.Length());
}

TEST(TArray, Movable_AppendElement_NoElementArg_Address)
{
  dummyMovableArrayDestructorCounter = 0;
  {
    nsTArray<Movable> array;
    array.AppendElement()->mDestructionCounter =
        &dummyMovableArrayDestructorCounter;

    ASSERT_EQ(1u, array.Length());
  }
  ASSERT_EQ(1u, dummyMovableArrayDestructorCounter);
}

TEST(TArray, Movable_AppendElement_NoElementArg_Fallible_Address)
{
  dummyMovableArrayDestructorCounter = 0;
  {
    nsTArray<Movable> array;
    array.AppendElement(fallible)->mDestructionCounter =
        &dummyMovableArrayDestructorCounter;

    ASSERT_EQ(1u, array.Length());
    ASSERT_EQ(&dummyMovableArrayDestructorCounter,
              array[0].mDestructionCounter);
  }
  ASSERT_EQ(1u, dummyMovableArrayDestructorCounter);
}

TEST(TArray, Movable_AppendElement_ElementArg)
{
  dummyMovableArrayDestructorCounter = 0;
  Movable movable;
  movable.mDestructionCounter = &dummyMovableArrayDestructorCounter;
  {
    nsTArray<Movable> array;
    array.AppendElement(std::move(movable));

    ASSERT_EQ(1u, array.Length());
    ASSERT_EQ(&dummyMovableArrayDestructorCounter,
              array[0].mDestructionCounter);
  }
  ASSERT_EQ(1u, dummyMovableArrayDestructorCounter);
}

TEST(TArray, Movable_AppendElement_ElementArg_Fallible)
{
  dummyMovableArrayDestructorCounter = 0;
  Movable movable;
  movable.mDestructionCounter = &dummyMovableArrayDestructorCounter;
  {
    nsTArray<Movable> array;
    ASSERT_NE(nullptr, array.AppendElement(std::move(movable), fallible));

    ASSERT_EQ(1u, array.Length());
    ASSERT_EQ(&dummyMovableArrayDestructorCounter,
              array[0].mDestructionCounter);
  }
  ASSERT_EQ(1u, dummyMovableArrayDestructorCounter);
}

TEST(TArray, int_Assign)
{
  nsTArray<int> array;
  array.Assign(DummyArray());
  ASSERT_EQ(DummyArray(), array);

  ASSERT_TRUE(array.Assign(DummyArray(), fallible));
  ASSERT_EQ(DummyArray(), array);

#ifdef DEBUG
  ASSERT_FALSE(array.Assign(FakeHugeArray(), fallible));
#endif

  nsTArray<int> array2;
  array2.Assign(std::move(array));
  ASSERT_TRUE(array.IsEmpty());
  ASSERT_EQ(DummyArray(), array2);
}

TEST(TArray, int_Assign_FromEmpty_ToNonEmpty)
{
  nsTArray<int> array;
  array.AppendElement(42);

  const nsTArray<int> empty;
  array.Assign(empty);

  ASSERT_TRUE(array.IsEmpty());
}

TEST(TArray, int_Assign_FromEmpty_ToNonEmpty_Fallible)
{
  nsTArray<int> array;
  array.AppendElement(42);

  const nsTArray<int> empty;
  ASSERT_TRUE(array.Assign(empty, fallible));

  ASSERT_TRUE(array.IsEmpty());
}

TEST(TArray, int_AssignmentOperatorSelfAssignment)
{
  CopyableTArray<int> array;
  array = DummyArray();

  array = *&array;
  ASSERT_EQ(DummyArray(), array);

#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 13)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wself-move"
#endif
  array = std::move(array);  // self-move
  ASSERT_EQ(DummyArray(), array);
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 13)
#  pragma GCC diagnostic pop
#endif
}

TEST(TArray, Movable_CopyOverlappingForwards)
{
  const size_t rangeLength = 8;
  const size_t initialLength = 2 * rangeLength;
  uint32_t destructionCounters[initialLength];
  nsTArray<Movable> array;
  array.AppendElements(initialLength);

  for (uint32_t i = 0; i < initialLength; ++i) {
    destructionCounters[i] = 0;
  }
  for (uint32_t i = 0; i < initialLength; ++i) {
    array[i].mDestructionCounter = &destructionCounters[i];
  }

  const size_t removedLength = rangeLength / 2;
  array.RemoveElementsAt(0, removedLength);

  for (uint32_t i = 0; i < removedLength; ++i) {
    ASSERT_EQ(destructionCounters[i], 1u);
  }
  for (uint32_t i = removedLength; i < initialLength; ++i) {
    ASSERT_EQ(destructionCounters[i], 0u);
  }
}

// The code to copy overlapping regions had a bug in that it wouldn't correctly
// destroy all over the source elements being copied.
TEST(TArray, Copyable_CopyOverlappingBackwards)
{
  const size_t rangeLength = 8;
  const size_t initialLength = 2 * rangeLength;
  uint32_t destructionCounters[initialLength];
  nsTArray<Copyable> array;
  array.SetCapacity(3 * rangeLength);
  array.AppendElements(initialLength);
  // To tickle the bug, we need to copy a source region:
  //
  //   ..XXXXX..
  //
  // such that it overlaps the destination region:
  //
  //   ....XXXXX
  //
  // so we are forced to copy back-to-front to ensure correct behavior.
  // The easiest way to do that is to call InsertElementsAt, which will force
  // the desired kind of shift.
  for (uint32_t i = 0; i < initialLength; ++i) {
    destructionCounters[i] = 0;
  }
  for (uint32_t i = 0; i < initialLength; ++i) {
    array[i].mDestructionCounter = &destructionCounters[i];
  }

  array.InsertElementsAt(0, rangeLength);

  for (uint32_t i = 0; i < initialLength; ++i) {
    ASSERT_EQ(destructionCounters[i], 1u);
  }
}

namespace {

class E {
 public:
  E() : mA(-1), mB(-2) { constructCount++; }
  E(int a, int b) : mA(a), mB(b) { constructCount++; }
  E(E&& aRhs) : mA(aRhs.mA), mB(aRhs.mB) {
    aRhs.mA = 0;
    aRhs.mB = 0;
    moveCount++;
  }

  E& operator=(E&& aRhs) {
    mA = aRhs.mA;
    aRhs.mA = 0;
    mB = aRhs.mB;
    aRhs.mB = 0;
    moveCount++;
    return *this;
  }

  int a() const { return mA; }
  int b() const { return mB; }

  E(const E&) = delete;
  E& operator=(const E&) = delete;

  static size_t constructCount;
  static size_t moveCount;

 private:
  int mA;
  int mB;
};

size_t E::constructCount = 0;
size_t E::moveCount = 0;

}  // namespace

TEST(TArray, Emplace)
{
  nsTArray<E> array;
  array.SetCapacity(20);

  ASSERT_EQ(array.Length(), 0u);

  for (int i = 0; i < 10; i++) {
    E s(i, i * i);
    array.AppendElement(std::move(s));
  }

  ASSERT_EQ(array.Length(), 10u);
  ASSERT_EQ(E::constructCount, 10u);
  ASSERT_EQ(E::moveCount, 10u);

  for (int i = 10; i < 20; i++) {
    array.EmplaceBack(i, i * i);
  }

  ASSERT_EQ(array.Length(), 20u);
  ASSERT_EQ(E::constructCount, 20u);
  ASSERT_EQ(E::moveCount, 10u);

  for (int i = 0; i < 20; i++) {
    ASSERT_EQ(array[i].a(), i);
    ASSERT_EQ(array[i].b(), i * i);
  }

  array.EmplaceBack();

  ASSERT_EQ(array.Length(), 21u);
  ASSERT_EQ(E::constructCount, 21u);
  ASSERT_EQ(E::moveCount, 10u);

  ASSERT_EQ(array[20].a(), -1);
  ASSERT_EQ(array[20].b(), -2);
}

TEST(TArray, UnorderedRemoveElements)
{
  // When removing an element from the end of the array, it can be removed in
  // place, by destroying it and decrementing the length.
  //
  // [ 1, 2, 3 ] => [ 1, 2 ]
  //         ^
  {
    nsTArray<int> array{1, 2, 3};
    array.UnorderedRemoveElementAt(2);

    nsTArray<int> goal{1, 2};
    ASSERT_EQ(array, goal);
  }

  // When removing any other single element, it is removed by swapping it with
  // the last element, and then decrementing the length as before.
  //
  // [ 1, 2, 3, 4, 5, 6 ]  => [ 1, 6, 3, 4, 5 ]
  //      ^
  {
    nsTArray<int> array{1, 2, 3, 4, 5, 6};
    array.UnorderedRemoveElementAt(1);

    nsTArray<int> goal{1, 6, 3, 4, 5};
    ASSERT_EQ(array, goal);
  }

  // This method also supports efficiently removing a range of elements. If they
  // are at the end, then they can all be removed like in the one element case.
  //
  // [ 1, 2, 3, 4, 5, 6 ] => [ 1, 2 ]
  //         ^--------^
  {
    nsTArray<int> array{1, 2, 3, 4, 5, 6};
    array.UnorderedRemoveElementsAt(2, 4);

    nsTArray<int> goal{1, 2};
    ASSERT_EQ(array, goal);
  }

  // If more elements are removed than exist after the removed section, the
  // remaining elements will be shifted down like in a normal removal.
  //
  // [ 1, 2, 3, 4, 5, 6, 7, 8 ] => [ 1, 2, 7, 8 ]
  //         ^--------^
  {
    nsTArray<int> array{1, 2, 3, 4, 5, 6, 7, 8};
    array.UnorderedRemoveElementsAt(2, 4);

    nsTArray<int> goal{1, 2, 7, 8};
    ASSERT_EQ(array, goal);
  }

  // And if fewer elements are removed than exist after the removed section,
  // elements will be moved from the end of the array to fill the vacated space.
  //
  // [ 1, 2, 3, 4, 5, 6, 7, 8 ] => [ 1, 7, 8, 4, 5, 6 ]
  //      ^--^
  {
    nsTArray<int> array{1, 2, 3, 4, 5, 6, 7, 8};
    array.UnorderedRemoveElementsAt(1, 2);

    nsTArray<int> goal{1, 7, 8, 4, 5, 6};
    ASSERT_EQ(array, goal);
  }

  // We should do the right thing if we drain the entire array.
  {
    nsTArray<int> array{1, 2, 3, 4, 5};
    array.UnorderedRemoveElementsAt(0, 5);

    nsTArray<int> goal{};
    ASSERT_EQ(array, goal);
  }

  {
    nsTArray<int> array{1};
    array.UnorderedRemoveElementAt(0);

    nsTArray<int> goal{};
    ASSERT_EQ(array, goal);
  }

  // We should do the right thing if we remove the same number of elements that
  // we have remaining.
  {
    nsTArray<int> array{1, 2, 3, 4, 5, 6};
    array.UnorderedRemoveElementsAt(2, 2);

    nsTArray<int> goal{1, 2, 5, 6};
    ASSERT_EQ(array, goal);
  }

  {
    nsTArray<int> array{1, 2, 3};
    array.UnorderedRemoveElementAt(1);

    nsTArray<int> goal{1, 3};
    ASSERT_EQ(array, goal);
  }

  // We should be able to remove elements from the front without issue.
  {
    nsTArray<int> array{1, 2, 3, 4, 5, 6};
    array.UnorderedRemoveElementsAt(0, 2);

    nsTArray<int> goal{5, 6, 3, 4};
    ASSERT_EQ(array, goal);
  }

  {
    nsTArray<int> array{1, 2, 3, 4};
    array.UnorderedRemoveElementAt(0);

    nsTArray<int> goal{4, 2, 3};
    ASSERT_EQ(array, goal);
  }
}

TEST(TArray, RemoveFromEnd)
{
  {
    nsTArray<int> array{1, 2, 3, 4};
    ASSERT_EQ(array.PopLastElement(), 4);
    array.RemoveLastElement();
    ASSERT_EQ(array.PopLastElement(), 2);
    array.RemoveLastElement();
    ASSERT_TRUE(array.IsEmpty());
  }
}

TEST(TArray, ConvertIteratorToConstIterator)
{
  nsTArray<int> array{1, 2, 3, 4};

  nsTArray<int>::const_iterator it = array.begin();
  ASSERT_EQ(array.cbegin(), it);
}

TEST(TArray, RemoveElementAt_ByIterator)
{
  nsTArray<int> array{1, 2, 3, 4};
  const auto it = std::find(array.begin(), array.end(), 3);
  const auto itAfter = array.RemoveElementAt(it);

  // Based on the implementation of the iterator, we could compare it and
  // itAfter, but we should not rely on such implementation details.

  ASSERT_EQ(2, std::distance(array.cbegin(), itAfter));
  const nsTArray<int> expected{1, 2, 4};
  ASSERT_EQ(expected, array);
}

TEST(TArray, RemoveElementsRange_ByIterator)
{
  nsTArray<int> array{1, 2, 3, 4};
  const auto it = std::find(array.begin(), array.end(), 3);
  const auto itAfter = array.RemoveElementsRange(it, array.end());

  // Based on the implementation of the iterator, we could compare it and
  // itAfter, but we should not rely on such implementation details.

  ASSERT_EQ(2, std::distance(array.cbegin(), itAfter));
  const nsTArray<int> expected{1, 2};
  ASSERT_EQ(expected, array);
}

TEST(TArray, RemoveLastElements_None)
{
  const nsTArray<int> original{1, 2, 3, 4};
  nsTArray<int> array = original.Clone();
  array.RemoveLastElements(0);

  ASSERT_EQ(original, array);
}

TEST(TArray, RemoveLastElements_Empty_None)
{
  nsTArray<int> array;
  array.RemoveLastElements(0);

  ASSERT_EQ(0u, array.Length());
}

TEST(TArray, RemoveLastElements_All)
{
  nsTArray<int> array{1, 2, 3, 4};
  array.RemoveLastElements(4);

  ASSERT_EQ(0u, array.Length());
}

TEST(TArray, RemoveLastElements_One)
{
  nsTArray<int> array{1, 2, 3, 4};
  array.RemoveLastElements(1);

  ASSERT_EQ((nsTArray<int>{1, 2, 3}), array);
}

static_assert(std::is_copy_assignable<decltype(MakeBackInserter(
                  std::declval<nsTArray<int>&>()))>::value,
              "output iteraror must be copy-assignable");
static_assert(std::is_copy_constructible<decltype(MakeBackInserter(
                  std::declval<nsTArray<int>&>()))>::value,
              "output iterator must be copy-constructible");

TEST(TArray, MakeBackInserter)
{
  const std::vector<int> src{1, 2, 3, 4};
  nsTArray<int> dst;

  std::copy(src.begin(), src.end(), MakeBackInserter(dst));

  const nsTArray<int> expected{1, 2, 3, 4};
  ASSERT_EQ(expected, dst);
}

TEST(TArray, MakeBackInserter_Move)
{
  uint32_t destructionCounter = 0;

  {
    std::vector<Movable> src(1);
    src[0].mDestructionCounter = &destructionCounter;

    nsTArray<Movable> dst;

    std::copy(std::make_move_iterator(src.begin()),
              std::make_move_iterator(src.end()), MakeBackInserter(dst));

    ASSERT_EQ(1u, dst.Length());
    ASSERT_EQ(0u, destructionCounter);
  }

  ASSERT_EQ(1u, destructionCounter);
}

TEST(TArray, ConvertToSpan)
{
  nsTArray<int> arr = {1, 2, 3, 4, 5};

  // from const
  {
    const auto& constArrRef = arr;

    auto span = Span{constArrRef};
    static_assert(std::is_same_v<decltype(span), Span<const int>>);
  }

  // from non-const
  {
    auto span = Span{arr};
    static_assert(std::is_same_v<decltype(span), Span<int>>);
  }
}

// This should compile:
struct RefCounted;

class Foo {
  ~Foo();  // Intentionally out of line

  nsTArray<RefPtr<RefCounted>> mArray;

  const RefCounted* GetFirst() const { return mArray.SafeElementAt(0); }
};

TEST(TArray, StableSort)
{
  const nsTArray<std::pair<int, int>> expected = {
      std::pair(1, 9), std::pair(1, 8), std::pair(1, 7), std::pair(2, 0),
      std::pair(3, 0)};
  nsTArray<std::pair<int, int>> array = {std::pair(1, 9), std::pair(2, 0),
                                         std::pair(1, 8), std::pair(3, 0),
                                         std::pair(1, 7)};

  array.StableSort([](std::pair<int, int> left, std::pair<int, int> right) {
    return left.first - right.first;
  });

  EXPECT_EQ(expected, array);
}

TEST(TArray, ToArray)
{
  const auto src = std::array{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

  nsTArray<int> keys = ToArray(src);
  keys.Sort();

  EXPECT_EQ((nsTArray<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}), keys);
}

// Test this to make sure this properly uses ADL.
TEST(TArray, ToArray_HashMap)
{
  nsTHashMap<uint32_t, uint64_t> src;

  for (uint32_t i = 0; i < 10; ++i) {
    src.InsertOrUpdate(i, i);
  }

  nsTArray<uint32_t> keys = ToArray(src.Keys());
  keys.Sort();

  EXPECT_EQ((nsTArray<uint32_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}), keys);
}

TEST(TArray, ToTArray)
{
  const auto src = std::array{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

  auto keys = ToTArray<AutoTArray<uint64_t, 10>>(src);
  keys.Sort();

  static_assert(std::is_same_v<decltype(keys), AutoTArray<uint64_t, 10>>);

  EXPECT_EQ((nsTArray<uint64_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}), keys);
}

TEST(TArray, RemoveElementsBy)
{
  // Removing elements returns the correct number of removed elements.
  {
    nsTArray<int> array{8, 1, 1, 3, 3, 5, 2, 3};
    auto removed = array.RemoveElementsBy([](int i) { return i == 3; });
    EXPECT_EQ(removed, 3u);

    nsTArray<int> goal{8, 1, 1, 5, 2};
    EXPECT_EQ(array, goal);
  }

  // The check is called in order.
  {
    int index = 0;
    nsTArray<int> array{0, 1, 2, 3, 4, 5};
    auto removed = array.RemoveElementsBy([&](int i) {
      EXPECT_EQ(index, i);
      index++;
      return i == 3;
    });
    EXPECT_EQ(removed, 1u);

    nsTArray<int> goal{0, 1, 2, 4, 5};
    EXPECT_EQ(array, goal);
  }

  // Removing nothing works
  {
    nsTArray<int> array{0, 1, 2, 3, 4};
    auto removed = array.RemoveElementsBy([](int) { return false; });
    EXPECT_EQ(removed, 0u);

    nsTArray<int> goal{0, 1, 2, 3, 4};
    EXPECT_EQ(array, goal);
  }

  // Removing everything works
  {
    nsTArray<int> array{0, 1, 2, 3, 4};
    auto removed = array.RemoveElementsBy([](int) { return true; });
    EXPECT_EQ(removed, 5u);

    nsTArray<int> goal{};
    EXPECT_EQ(array, goal);
  }
}

}  // namespace TestTArray
