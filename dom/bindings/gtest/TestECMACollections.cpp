/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <iterator>

#include "gtest/gtest.h"
#include "mozilla/ReverseIterator.h"
#include "mozilla/dom/ECMACollections.h"
#include "nsHashKeys.h"
#include "nsISupportsImpl.h"
#include "nsString.h"

using mozilla::dom::ECMAMap;
using mozilla::dom::ECMASet;
using mozilla::dom::binding_detail::ECMAStorable;

static_assert(ECMAStorable<int>::value);
static_assert(ECMAStorable<nsString>::value);
static_assert(!ECMAStorable<JS::Value>::value);
static_assert(!ECMAStorable<JSObject*>::value);
static_assert(!ECMAStorable<JSString*>::value);
static_assert(!ECMAStorable<JS::Heap<JS::Value>>::value);
static_assert(!ECMAStorable<JS::Heap<JSObject*>>::value);

// The collections under test, with the length of their entry array (live
// entries plus tombstones) exposed so that tests can observe compaction.
template <typename T, typename... HashKey>
class TestSet : public ECMASet<T, HashKey...> {
 public:
  uint32_t PhysicalLength() const { return this->mEntries.Length(); }
};

template <typename K, typename V, typename... HashKey>
class TestMap : public ECMAMap<K, V, HashKey...> {
 public:
  uint32_t PhysicalLength() const { return this->mEntries.Length(); }
};

// ==========================================================================
// ECMASet -- basic operations
// ==========================================================================

TEST(ECMASet, EmptySet)
{
  TestSet<int> set;
  EXPECT_EQ(set.Size(), 0u);
  EXPECT_TRUE(set.IsEmpty());
  EXPECT_FALSE(set.Has(42));
}

TEST(ECMASet, AddAndHas)
{
  TestSet<int> set;
  EXPECT_TRUE(set.Add(1));
  EXPECT_TRUE(set.Add(2));
  EXPECT_TRUE(set.Add(3));
  EXPECT_EQ(set.Size(), 3u);
  EXPECT_TRUE(set.Has(1));
  EXPECT_TRUE(set.Has(2));
  EXPECT_TRUE(set.Has(3));
  EXPECT_FALSE(set.Has(4));
}

TEST(ECMASet, AddDuplicate)
{
  TestSet<int> set;
  EXPECT_TRUE(set.Add(1));
  EXPECT_FALSE(set.Add(1));
  EXPECT_EQ(set.Size(), 1u);
}

TEST(ECMASet, Delete)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);
  EXPECT_TRUE(set.Delete(2));
  EXPECT_EQ(set.Size(), 2u);
  EXPECT_TRUE(set.Has(1));
  EXPECT_FALSE(set.Has(2));
  EXPECT_TRUE(set.Has(3));
}

TEST(ECMASet, DeleteNonExistent)
{
  TestSet<int> set;
  set.Add(1);
  EXPECT_FALSE(set.Delete(99));
  EXPECT_EQ(set.Size(), 1u);
}

TEST(ECMASet, DeleteAll)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Delete(1);
  set.Delete(2);
  EXPECT_EQ(set.Size(), 0u);
  EXPECT_TRUE(set.IsEmpty());
  EXPECT_FALSE(set.Has(1));
  EXPECT_FALSE(set.Has(2));
}

TEST(ECMASet, DeleteTwice)
{
  TestSet<int> set;
  set.Add(1);
  EXPECT_TRUE(set.Delete(1));
  EXPECT_FALSE(set.Delete(1));
  EXPECT_EQ(set.Size(), 0u);
}

TEST(ECMASet, Clear)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);
  set.Clear();
  EXPECT_EQ(set.Size(), 0u);
  EXPECT_TRUE(set.IsEmpty());
  EXPECT_FALSE(set.Has(1));
}

TEST(ECMASet, DoubleClear)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Clear();
  EXPECT_EQ(set.Size(), 0u);
  set.Clear();
  EXPECT_EQ(set.Size(), 0u);
}

TEST(ECMASet, ReAddAfterDelete)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Delete(1);
  EXPECT_TRUE(set.Add(1));
  EXPECT_EQ(set.Size(), 2u);
  EXPECT_TRUE(set.Has(1));
}

TEST(ECMASet, ReAddAfterClear)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Clear();
  EXPECT_TRUE(set.Add(3));
  EXPECT_EQ(set.Size(), 1u);
  EXPECT_TRUE(set.Has(3));
  EXPECT_FALSE(set.Has(1));
}

// ==========================================================================
// ECMASet -- insertion-order iteration
// ==========================================================================

TEST(ECMASet, InsertionOrder)
{
  TestSet<int> set;
  set.Add(30);
  set.Add(10);
  set.Add(20);

  nsTArray<int> result;
  for (const auto& val : set) {
    result.AppendElement(val);
  }
  ASSERT_EQ(result.Length(), 3u);
  EXPECT_EQ(result[0], 30);
  EXPECT_EQ(result[1], 10);
  EXPECT_EQ(result[2], 20);
}

TEST(ECMASet, IterationSkipsTombstones)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);
  set.Add(4);
  set.Delete(2);
  set.Delete(3);

  nsTArray<int> result;
  for (const auto& val : set) {
    result.AppendElement(val);
  }
  ASSERT_EQ(result.Length(), 2u);
  EXPECT_EQ(result[0], 1);
  EXPECT_EQ(result[1], 4);
}

TEST(ECMASet, ReAddAppearsAtEnd)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);
  set.Delete(2);
  set.Add(2);

  nsTArray<int> result;
  for (const auto& val : set) {
    result.AppendElement(val);
  }
  ASSERT_EQ(result.Length(), 3u);
  EXPECT_EQ(result[0], 1);
  EXPECT_EQ(result[1], 3);
  EXPECT_EQ(result[2], 2);
}

// ==========================================================================
// ECMASet -- Cursor (for WebIDL codegen)
// ==========================================================================

TEST(ECMASet, CursorSkipsTombstones)
{
  TestSet<int> set;
  set.Add(10);
  set.Add(20);
  set.Add(30);
  set.Delete(20);

  auto cursor = set.CreateCursor();
  nsTArray<int> result;
  while (const auto* entry = cursor.Next()) {
    result.AppendElement(entry->mValue);
  }

  ASSERT_EQ(result.Length(), 2u);
  EXPECT_EQ(result[0], 10);
  EXPECT_EQ(result[1], 30);
}

// ==========================================================================
// ECMASet -- compaction
// ==========================================================================

TEST(ECMASet, CompactionHappensWhenTombstonesExceedLive)
{
  TestSet<int> set;
  for (int i = 0; i < 10; i++) {
    set.Add(i);
  }
  for (int i = 0; i < 8; i++) {
    set.Delete(i);
  }
  EXPECT_EQ(set.Size(), 2u);
  EXPECT_LT(set.PhysicalLength(), 10u);

  nsTArray<int> result;
  for (const auto& val : set) {
    result.AppendElement(val);
  }
  ASSERT_EQ(result.Length(), 2u);
  EXPECT_EQ(result[0], 8);
  EXPECT_EQ(result[1], 9);
}

TEST(ECMASet, ClearCompacts)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);
  set.Clear();
  EXPECT_EQ(set.PhysicalLength(), 0u);
  EXPECT_EQ(set.Size(), 0u);
}

// ==========================================================================
// ECMASet -- compaction under live iterators
//
// Compaction reclaims tombstoned memory while iterators are alive; the
// collection moves their cursors so they resume at the same logical position.
// ==========================================================================

TEST(ECMASet, CompactionProceedsUnderLiveIterator)
{
  TestSet<int> set;
  for (int i = 0; i < 10; i++) {
    set.Add(i);
  }

  auto it = set.begin();
  EXPECT_EQ(*it, 0);
  for (int i = 1; i < 10; i++) {
    set.Delete(i);
  }
  // The tombstones are reclaimed even though the iteration is unfinished.
  EXPECT_EQ(set.PhysicalLength(), 1u);
  EXPECT_EQ(set.Size(), 1u);

  // Not even the entry the iterator yielded last is pinned.
  set.Delete(0);
  EXPECT_EQ(set.PhysicalLength(), 0u);
  ++it;
  EXPECT_TRUE(it == set.end());
}

TEST(ECMASet, CompactionUnderLiveIteratorYieldsRemainingEntries)
{
  TestSet<int> set;
  for (int i = 0; i < 10; i++) {
    set.Add(i);
  }

  auto it = set.begin();
  EXPECT_EQ(*it, 0);
  ++it;
  EXPECT_EQ(*it, 1);

  for (int i = 2; i < 8; i++) {
    set.Delete(i);
  }
  EXPECT_EQ(set.PhysicalLength(), 4u);
  EXPECT_EQ(set.Size(), 4u);

  nsTArray<int> rest;
  for (++it; it != set.end(); ++it) {
    rest.AppendElement(*it);
  }
  ASSERT_EQ(rest.Length(), 2u);
  EXPECT_EQ(rest[0], 8);
  EXPECT_EQ(rest[1], 9);
}

TEST(ECMASet, TwoLiveIteratorsResumeAfterCompaction)
{
  TestSet<int> set;
  for (int i = 0; i < 10; i++) {
    set.Add(i);
  }

  auto first = set.begin();
  ++first;
  auto second = set.begin();
  ++second;
  ++second;
  ++second;
  EXPECT_EQ(*first, 1);
  EXPECT_EQ(*second, 3);

  for (int i = 4; i < 10; i++) {
    set.Delete(i);
  }
  EXPECT_EQ(set.PhysicalLength(), 4u);

  nsTArray<int> fromFirst;
  for (++first; first != set.end(); ++first) {
    fromFirst.AppendElement(*first);
  }
  ASSERT_EQ(fromFirst.Length(), 2u);
  EXPECT_EQ(fromFirst[0], 2);
  EXPECT_EQ(fromFirst[1], 3);

  ++second;
  EXPECT_TRUE(second == set.end());
}

TEST(ECMASet, TwoIteratorsAtSamePositionResumeAfterCompaction)
{
  TestSet<int> set;
  for (int i = 0; i < 10; i++) {
    set.Add(i);
  }

  auto first = set.begin();
  auto second = set.begin();
  EXPECT_EQ(*first, 0);
  EXPECT_EQ(*second, 0);

  for (int i = 4; i < 10; i++) {
    set.Delete(i);
  }
  EXPECT_EQ(set.PhysicalLength(), 4u);

  nsTArray<int> fromFirst;
  for (++first; first != set.end(); ++first) {
    fromFirst.AppendElement(*first);
  }
  nsTArray<int> fromSecond;
  for (++second; second != set.end(); ++second) {
    fromSecond.AppendElement(*second);
  }

  ASSERT_EQ(fromFirst.Length(), 3u);
  EXPECT_EQ(fromFirst[0], 1);
  EXPECT_EQ(fromFirst[2], 3);
  ASSERT_EQ(fromSecond.Length(), fromFirst.Length());
  for (size_t i = 0; i < fromFirst.Length(); i++) {
    EXPECT_EQ(fromSecond[i], fromFirst[i]);
  }
}

TEST(ECMASet, ReverseIteratorResumesAfterCompaction)
{
  TestSet<int> set;
  for (int i = 0; i < 10; i++) {
    set.Add(i);
  }

  auto it = set.rbegin();
  EXPECT_EQ(*it, 9);
  ++it;
  EXPECT_EQ(*it, 8);

  for (int i = 0; i < 6; i++) {
    set.Delete(i);
  }
  EXPECT_EQ(set.PhysicalLength(), 4u);

  nsTArray<int> rest;
  for (++it; it != set.rend(); ++it) {
    rest.AppendElement(*it);
  }
  ASSERT_EQ(rest.Length(), 2u);
  EXPECT_EQ(rest[0], 7);
  EXPECT_EQ(rest[1], 6);
}

TEST(ECMASet, ReverseCursorOnFirstTombstoneFinishes)
{
  TestSet<int> set;
  for (int i = 0; i < 10; i++) {
    set.Add(i);
  }

  auto it = set.rbegin();
  while (*it != 1) {
    ++it;
  }
  // The next entry the iterator would examine is the tombstone at index 0.
  set.Delete(0);
  for (int i = 2; i < 7; i++) {
    set.Delete(i);
  }
  EXPECT_EQ(set.PhysicalLength(), 4u);
  ++it;
  EXPECT_TRUE(it == set.rend());
}

TEST(ECMASet, CopiedIteratorResumesAfterCompaction)
{
  TestSet<int> set;
  for (int i = 0; i < 10; i++) {
    set.Add(i);
  }

  auto it = set.begin();
  ++it;
  auto copy = it;
  EXPECT_TRUE(copy == it);
  EXPECT_EQ(*copy, 1);

  for (int i = 2; i < 8; i++) {
    set.Delete(i);
  }
  EXPECT_EQ(set.PhysicalLength(), 4u);

  ++it;
  EXPECT_FALSE(copy == it);
  ++copy;
  EXPECT_TRUE(copy == it);
  EXPECT_EQ(*it, 8);
  EXPECT_EQ(*copy, 8);
}

TEST(ECMASet, DeleteCurrentEntryResumesAtNextEntry)
{
  TestSet<int> set;
  for (int i = 0; i < 10; i++) {
    set.Add(i);
  }

  auto it = set.begin();
  EXPECT_EQ(*it, 0);

  // The entry the iterator sits on is deleted, and the following deletes
  // compact the survivors underneath it.
  for (int i = 0; i < 6; i++) {
    set.Delete(i);
  }
  EXPECT_EQ(set.PhysicalLength(), 4u);

  nsTArray<int> rest;
  for (++it; it != set.end(); ++it) {
    rest.AppendElement(*it);
  }
  ASSERT_EQ(rest.Length(), 4u);
  EXPECT_EQ(rest[0], 6);
  EXPECT_EQ(rest[1], 7);
  EXPECT_EQ(rest[2], 8);
  EXPECT_EQ(rest[3], 9);
}

TEST(ECMASet, DeleteEveryEntryWhileIteratingYieldsAll)
{
  TestSet<int> set;
  for (int i = 0; i < 10; i++) {
    set.Add(i);
  }

  nsTArray<int> seen;
  for (auto it = set.begin(); it != set.end(); ++it) {
    const int value = *it;
    seen.AppendElement(value);
    ASSERT_LT(seen.Length(), 20u);
    set.Delete(value);
  }
  ASSERT_EQ(seen.Length(), 10u);
  for (size_t i = 0; i < seen.Length(); i++) {
    EXPECT_EQ(seen[i], static_cast<int>(i));
  }
  EXPECT_EQ(set.PhysicalLength(), 0u);
  EXPECT_TRUE(set.IsEmpty());
}

TEST(ECMASet, ReverseDeleteCurrentEntryResumesAtPreviousEntry)
{
  TestSet<int> set;
  for (int i = 0; i < 10; i++) {
    set.Add(i);
  }

  auto it = set.rbegin();
  EXPECT_EQ(*it, 9);

  set.Delete(9);
  for (int i = 0; i < 5; i++) {
    set.Delete(i);
  }
  EXPECT_EQ(set.PhysicalLength(), 4u);

  nsTArray<int> rest;
  for (++it; it != set.rend(); ++it) {
    rest.AppendElement(*it);
  }
  ASSERT_EQ(rest.Length(), 4u);
  EXPECT_EQ(rest[0], 8);
  EXPECT_EQ(rest[1], 7);
  EXPECT_EQ(rest[2], 6);
  EXPECT_EQ(rest[3], 5);
}

TEST(ECMASet, MultipleLiveIteratorsDoNotSuppressCompaction)
{
  TestSet<int> set;
  for (int i = 0; i < 8; i++) {
    set.Add(i);
  }

  auto first = set.begin();
  auto second = set.begin();
  ++second;
  auto third = set.begin();
  ++third;
  ++third;

  for (int i = 3; i < 8; i++) {
    set.Delete(i);
  }
  EXPECT_EQ(set.PhysicalLength(), 3u);
  EXPECT_EQ(set.Size(), 3u);
  ++first;
  ++second;
  ++third;
  EXPECT_EQ(*first, 1);
  EXPECT_EQ(*second, 2);
  EXPECT_TRUE(third == set.end());
}

TEST(ECMASet, ClearUnderLiveIteratorReclaims)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);

  auto it = set.begin();
  EXPECT_EQ(*it, 1);

  set.Clear();
  EXPECT_EQ(set.PhysicalLength(), 0u);
  EXPECT_EQ(set.Size(), 0u);

  // Like an ES iterator, it goes on to the entries added after the clear.
  set.Add(4);
  EXPECT_EQ(set.PhysicalLength(), 1u);
  ++it;
  ASSERT_TRUE(it != set.end());
  EXPECT_EQ(*it, 4);
  ++it;
  EXPECT_TRUE(it == set.end());
}

TEST(ECMASet, UnlinkUnderLiveIterator)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);

  auto it = set.begin();
  EXPECT_EQ(*it, 1);

  set.Unlink();
  EXPECT_EQ(set.PhysicalLength(), 0u);
  EXPECT_EQ(set.Size(), 0u);

  // The detached cursor terminates instead of reading the discarded entries.
  ++it;
  EXPECT_TRUE(it == set.end());
}

TEST(ECMASet, LongLivedIteratorDoesNotPinTombstones)
{
  TestSet<int> set;
  for (int i = 0; i < 4; i++) {
    set.Add(i);
  }

  auto it = set.begin();
  for (int i = 0; i < 200; i++) {
    set.Add(100 + i);
    EXPECT_LE(set.PhysicalLength(), 2 * set.Size() + 2);
    set.Delete(100 + i);
    EXPECT_LE(set.PhysicalLength(), 2 * set.Size() + 2);
  }
  EXPECT_EQ(set.Size(), 4u);
  EXPECT_EQ(set.PhysicalLength(), 4u);

  nsTArray<int> rest;
  for (++it; it != set.end(); ++it) {
    rest.AppendElement(*it);
  }
  ASSERT_EQ(rest.Length(), 3u);
  EXPECT_EQ(rest[0], 1);
  EXPECT_EQ(rest[2], 3);
}

TEST(ECMASet, RangeForSurvivesCompaction)
{
  TestSet<int> set;
  for (int i = 0; i < 10; i++) {
    set.Add(i);
  }

  nsTArray<int> seen;
  for (const auto& val : set) {
    const int value = val;
    seen.AppendElement(value);
    if (value == 1) {
      for (int i = 2; i < 8; i++) {
        set.Delete(i);
      }
      EXPECT_EQ(set.PhysicalLength(), 4u);
    }
  }
  ASSERT_EQ(seen.Length(), 4u);
  EXPECT_EQ(seen[0], 0);
  EXPECT_EQ(seen[1], 1);
  EXPECT_EQ(seen[2], 8);
  EXPECT_EQ(seen[3], 9);
}

// ==========================================================================
// ECMASet -- string keys and move semantics
// ==========================================================================

TEST(ECMASet, StringKeys)
{
  TestSet<nsString> set;
  set.Add(u"hello"_ns);
  set.Add(u"world"_ns);
  EXPECT_TRUE(set.Has(u"hello"_ns));
  EXPECT_FALSE(set.Has(u"missing"_ns));
  EXPECT_EQ(set.Size(), 2u);
}

TEST(ECMASet, MoveAdd)
{
  TestSet<nsString> set;
  nsString str(u"hello"_ns);
  set.Add(std::move(str));
  EXPECT_TRUE(str.IsEmpty());
  EXPECT_TRUE(set.Has(u"hello"_ns));
  EXPECT_EQ(set.Size(), 1u);
}

TEST(ECMASet, MoveAddDuplicateDoesNotConsume)
{
  TestSet<nsString> set;
  set.Add(u"hello"_ns);
  nsString str(u"hello"_ns);
  EXPECT_FALSE(set.Add(std::move(str)));
  // Value not consumed on duplicate.
  EXPECT_FALSE(str.IsEmpty());
}

TEST(ECMASet, PointerKeys)
{
  int a = 0;
  int b = 0;
  int* const constA = &a;
  TestSet<int*> set;
  EXPECT_TRUE(set.Add(constA));
  EXPECT_TRUE(set.Add(&b));
  EXPECT_FALSE(set.Add(&a));
  EXPECT_EQ(set.Size(), 2u);
  EXPECT_TRUE(set.Has(&a));
  EXPECT_TRUE(set.Has(&b));
}

// ==========================================================================
// ECMAMap -- basic operations
// ==========================================================================

TEST(ECMAMap, EmptyMap)
{
  TestMap<int, int> map;
  EXPECT_EQ(map.Size(), 0u);
  EXPECT_TRUE(map.IsEmpty());
  EXPECT_FALSE(map.Has(1));
  EXPECT_EQ(map.Get(1), nullptr);
}

TEST(ECMAMap, SetAndGet)
{
  TestMap<int, int> map;
  EXPECT_TRUE(map.Set(1, 100));
  EXPECT_TRUE(map.Set(2, 200));
  EXPECT_EQ(map.Size(), 2u);
  ASSERT_NE(map.Get(1), nullptr);
  EXPECT_EQ(*map.Get(1), 100);
  ASSERT_NE(map.Get(2), nullptr);
  EXPECT_EQ(*map.Get(2), 200);
  EXPECT_EQ(map.Get(3), nullptr);
}

TEST(ECMAMap, ConstGet)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  const auto& constMap = map;
  const int* val = constMap.Get(1);
  ASSERT_NE(val, nullptr);
  EXPECT_EQ(*val, 100);
  EXPECT_EQ(constMap.Get(2), nullptr);
}

TEST(ECMAMap, SetUpdatesValueInPlace)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  EXPECT_FALSE(map.Set(1, 999));
  EXPECT_EQ(map.Size(), 2u);
  EXPECT_EQ(*map.Get(1), 999);
}

TEST(ECMAMap, SetUpdatePreservesInsertionOrder)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);
  map.Set(1, 999);

  nsTArray<int> keys;
  for (const auto& entry : map) {
    keys.AppendElement(entry.mKey);
  }
  ASSERT_EQ(keys.Length(), 3u);
  EXPECT_EQ(keys[0], 1);
  EXPECT_EQ(keys[1], 2);
  EXPECT_EQ(keys[2], 3);
}

TEST(ECMAMap, Delete)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);
  EXPECT_TRUE(map.Delete(2));
  EXPECT_EQ(map.Size(), 2u);
  EXPECT_FALSE(map.Has(2));
  EXPECT_EQ(map.Get(2), nullptr);
  EXPECT_TRUE(map.Has(1));
  EXPECT_TRUE(map.Has(3));
}

TEST(ECMAMap, DeleteNonExistent)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  EXPECT_FALSE(map.Delete(99));
  EXPECT_EQ(map.Size(), 1u);
}

TEST(ECMAMap, DeleteTwice)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  EXPECT_TRUE(map.Delete(1));
  EXPECT_FALSE(map.Delete(1));
  EXPECT_EQ(map.Size(), 0u);
  EXPECT_EQ(map.Get(1), nullptr);
}

TEST(ECMAMap, Clear)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Clear();
  EXPECT_EQ(map.Size(), 0u);
  EXPECT_FALSE(map.Has(1));
}

TEST(ECMAMap, DoubleClear)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Clear();
  EXPECT_EQ(map.Size(), 0u);
  map.Clear();
  EXPECT_EQ(map.Size(), 0u);
}

TEST(ECMAMap, GetReturnsNullAfterDelete)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Delete(1);
  EXPECT_EQ(map.Get(1), nullptr);
  ASSERT_NE(map.Get(2), nullptr);
  EXPECT_EQ(*map.Get(2), 200);
}

TEST(ECMAMap, ReAddAfterDelete)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Delete(1);
  EXPECT_TRUE(map.Set(1, 999));
  EXPECT_EQ(map.Size(), 2u);
  EXPECT_EQ(*map.Get(1), 999);
}

// ==========================================================================
// ECMAMap -- insertion-order iteration
// ==========================================================================

TEST(ECMAMap, InsertionOrder)
{
  TestMap<int, int> map;
  map.Set(30, 300);
  map.Set(10, 100);
  map.Set(20, 200);

  nsTArray<int> keys;
  nsTArray<int> values;
  for (auto iter = map.begin(); iter != map.end(); ++iter) {
    keys.AppendElement(iter->mKey);
    values.AppendElement(iter->mValue);
  }
  ASSERT_EQ(keys.Length(), 3u);
  EXPECT_EQ(keys[0], 30);
  EXPECT_EQ(keys[1], 10);
  EXPECT_EQ(keys[2], 20);
  EXPECT_EQ(values[0], 300);
  EXPECT_EQ(values[1], 100);
  EXPECT_EQ(values[2], 200);
}

TEST(ECMAMap, IterationSkipsTombstones)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);
  map.Set(4, 400);
  map.Delete(2);
  map.Delete(3);

  nsTArray<int> keys;
  for (const auto& entry : map) {
    keys.AppendElement(entry.mKey);
  }
  ASSERT_EQ(keys.Length(), 2u);
  EXPECT_EQ(keys[0], 1);
  EXPECT_EQ(keys[1], 4);
}

TEST(ECMAMap, ReAddAppearsAtEnd)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);
  map.Delete(2);
  map.Set(2, 999);

  nsTArray<int> keys;
  for (const auto& entry : map) {
    keys.AppendElement(entry.mKey);
  }
  ASSERT_EQ(keys.Length(), 3u);
  EXPECT_EQ(keys[0], 1);
  EXPECT_EQ(keys[1], 3);
  EXPECT_EQ(keys[2], 2);
}

// ==========================================================================
// ECMAMap -- Cursor
// ==========================================================================

TEST(ECMAMap, CursorSkipsTombstones)
{
  TestMap<int, int> map;
  map.Set(10, 100);
  map.Set(20, 200);
  map.Set(30, 300);
  map.Delete(20);

  auto cursor = map.CreateCursor();
  nsTArray<int> keys;
  while (const auto* entry = cursor.Next()) {
    keys.AppendElement(entry->mKey);
  }

  ASSERT_EQ(keys.Length(), 2u);
  EXPECT_EQ(keys[0], 10);
  EXPECT_EQ(keys[1], 30);
}

// ==========================================================================
// ECMAMap -- compaction
// ==========================================================================

TEST(ECMAMap, CompactionHappensWhenTombstonesExceedLive)
{
  TestMap<int, int> map;
  for (int i = 0; i < 10; i++) {
    map.Set(i, i * 100);
  }
  for (int i = 0; i < 8; i++) {
    map.Delete(i);
  }
  EXPECT_EQ(map.Size(), 2u);
  EXPECT_LT(map.PhysicalLength(), 10u);
  EXPECT_EQ(*map.Get(8), 800);
  EXPECT_EQ(*map.Get(9), 900);
}

TEST(ECMAMap, ClearCompacts)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);
  map.Clear();
  EXPECT_EQ(map.PhysicalLength(), 0u);
  EXPECT_EQ(map.Size(), 0u);
}

// ==========================================================================
// ECMAMap -- compaction under live iterators
// ==========================================================================

TEST(ECMAMap, CompactionProceedsUnderLiveIterator)
{
  TestMap<int, int> map;
  for (int i = 0; i < 10; i++) {
    map.Set(i, i * 100);
  }

  auto it = map.begin();
  EXPECT_EQ(it->mKey, 0);
  for (int i = 1; i < 10; i++) {
    map.Delete(i);
  }
  EXPECT_EQ(map.PhysicalLength(), 1u);
  EXPECT_EQ(map.Size(), 1u);

  map.Delete(0);
  EXPECT_EQ(map.PhysicalLength(), 0u);
  ++it;
  EXPECT_TRUE(it == map.end());
}

TEST(ECMAMap, CompactionUnderLiveIteratorYieldsRemainingEntries)
{
  TestMap<int, int> map;
  for (int i = 0; i < 10; i++) {
    map.Set(i, i * 100);
  }

  auto it = map.begin();
  EXPECT_EQ(it->mKey, 0);
  ++it;
  EXPECT_EQ(it->mKey, 1);

  for (int i = 2; i < 8; i++) {
    map.Delete(i);
  }
  EXPECT_EQ(map.PhysicalLength(), 4u);
  EXPECT_EQ(map.Size(), 4u);

  nsTArray<int> keys;
  nsTArray<int> values;
  for (++it; it != map.end(); ++it) {
    keys.AppendElement(it->mKey);
    values.AppendElement(it->mValue);
  }
  ASSERT_EQ(keys.Length(), 2u);
  EXPECT_EQ(keys[0], 8);
  EXPECT_EQ(keys[1], 9);
  EXPECT_EQ(values[0], 800);
  EXPECT_EQ(values[1], 900);
}

TEST(ECMAMap, TwoLiveIteratorsResumeAfterCompaction)
{
  TestMap<int, int> map;
  for (int i = 0; i < 10; i++) {
    map.Set(i, i * 100);
  }

  auto first = map.begin();
  ++first;
  auto second = map.begin();
  ++second;
  ++second;
  ++second;
  EXPECT_EQ(first->mKey, 1);
  EXPECT_EQ(second->mKey, 3);

  for (int i = 4; i < 10; i++) {
    map.Delete(i);
  }
  EXPECT_EQ(map.PhysicalLength(), 4u);

  nsTArray<int> fromFirst;
  for (++first; first != map.end(); ++first) {
    fromFirst.AppendElement(first->mKey);
  }
  ASSERT_EQ(fromFirst.Length(), 2u);
  EXPECT_EQ(fromFirst[0], 2);
  EXPECT_EQ(fromFirst[1], 3);

  ++second;
  EXPECT_TRUE(second == map.end());
}

TEST(ECMAMap, TwoIteratorsAtSamePositionResumeAfterCompaction)
{
  TestMap<int, int> map;
  for (int i = 0; i < 10; i++) {
    map.Set(i, i * 100);
  }

  auto first = map.begin();
  auto second = map.begin();
  EXPECT_EQ(first->mKey, 0);
  EXPECT_EQ(second->mKey, 0);

  for (int i = 4; i < 10; i++) {
    map.Delete(i);
  }
  EXPECT_EQ(map.PhysicalLength(), 4u);

  nsTArray<int> fromFirst;
  for (++first; first != map.end(); ++first) {
    fromFirst.AppendElement(first->mKey);
  }
  nsTArray<int> fromSecond;
  for (++second; second != map.end(); ++second) {
    fromSecond.AppendElement(second->mKey);
  }

  ASSERT_EQ(fromFirst.Length(), 3u);
  EXPECT_EQ(fromFirst[0], 1);
  EXPECT_EQ(fromFirst[2], 3);
  ASSERT_EQ(fromSecond.Length(), fromFirst.Length());
  for (size_t i = 0; i < fromFirst.Length(); i++) {
    EXPECT_EQ(fromSecond[i], fromFirst[i]);
  }
}

TEST(ECMAMap, ReverseIteratorResumesAfterCompaction)
{
  TestMap<int, int> map;
  for (int i = 0; i < 10; i++) {
    map.Set(i, i * 100);
  }

  auto it = map.rbegin();
  EXPECT_EQ(it->mKey, 9);
  ++it;
  EXPECT_EQ(it->mKey, 8);

  for (int i = 0; i < 6; i++) {
    map.Delete(i);
  }
  EXPECT_EQ(map.PhysicalLength(), 4u);

  nsTArray<int> keys;
  for (++it; it != map.rend(); ++it) {
    keys.AppendElement(it->mKey);
  }
  ASSERT_EQ(keys.Length(), 2u);
  EXPECT_EQ(keys[0], 7);
  EXPECT_EQ(keys[1], 6);
}

TEST(ECMAMap, DeleteCurrentEntryResumesAtNextEntry)
{
  TestMap<int, int> map;
  for (int i = 0; i < 10; i++) {
    map.Set(i, i * 100);
  }

  auto it = map.begin();
  EXPECT_EQ(it->mKey, 0);

  for (int i = 0; i < 6; i++) {
    map.Delete(i);
  }
  EXPECT_EQ(map.PhysicalLength(), 4u);

  nsTArray<int> keys;
  nsTArray<int> values;
  for (++it; it != map.end(); ++it) {
    keys.AppendElement(it->mKey);
    values.AppendElement(it->mValue);
  }
  ASSERT_EQ(keys.Length(), 4u);
  EXPECT_EQ(keys[0], 6);
  EXPECT_EQ(keys[3], 9);
  EXPECT_EQ(values[0], 600);
  EXPECT_EQ(values[3], 900);
}

TEST(ECMAMap, ReverseDeleteCurrentEntryResumesAtPreviousEntry)
{
  TestMap<int, int> map;
  for (int i = 0; i < 10; i++) {
    map.Set(i, i * 100);
  }

  auto it = map.rbegin();
  EXPECT_EQ(it->mKey, 9);

  map.Delete(9);
  for (int i = 0; i < 5; i++) {
    map.Delete(i);
  }
  EXPECT_EQ(map.PhysicalLength(), 4u);

  nsTArray<int> keys;
  for (++it; it != map.rend(); ++it) {
    keys.AppendElement(it->mKey);
  }
  ASSERT_EQ(keys.Length(), 4u);
  EXPECT_EQ(keys[0], 8);
  EXPECT_EQ(keys[1], 7);
  EXPECT_EQ(keys[2], 6);
  EXPECT_EQ(keys[3], 5);
}

TEST(ECMAMap, ClearUnderLiveIteratorReclaims)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);

  auto it = map.begin();
  EXPECT_EQ(it->mKey, 1);

  map.Clear();
  EXPECT_EQ(map.PhysicalLength(), 0u);
  EXPECT_EQ(map.Size(), 0u);

  // Like an ES iterator, it goes on to the entries added after the clear.
  map.Set(4, 400);
  EXPECT_EQ(map.PhysicalLength(), 1u);
  ++it;
  ASSERT_TRUE(it != map.end());
  EXPECT_EQ(it->mKey, 4);
  ++it;
  EXPECT_TRUE(it == map.end());
}

TEST(ECMAMap, LongLivedIteratorDoesNotPinTombstones)
{
  TestMap<int, int> map;
  for (int i = 0; i < 4; i++) {
    map.Set(i, i * 100);
  }

  auto it = map.begin();
  for (int i = 0; i < 200; i++) {
    map.Set(100 + i, i);
    EXPECT_LE(map.PhysicalLength(), 2 * map.Size() + 2);
    map.Delete(100 + i);
    EXPECT_LE(map.PhysicalLength(), 2 * map.Size() + 2);
  }
  EXPECT_EQ(map.Size(), 4u);
  EXPECT_EQ(map.PhysicalLength(), 4u);

  nsTArray<int> rest;
  for (++it; it != map.end(); ++it) {
    rest.AppendElement(it->mKey);
  }
  ASSERT_EQ(rest.Length(), 3u);
  EXPECT_EQ(rest[0], 1);
  EXPECT_EQ(rest[2], 3);
}

// ==========================================================================
// ECMAMap -- string keys and move semantics
// ==========================================================================

TEST(ECMAMap, StringKeys)
{
  TestMap<nsString, int, nsStringHashKey> map;
  map.Set(u"hello"_ns, 1);
  map.Set(u"world"_ns, 2);
  EXPECT_TRUE(map.Has(u"hello"_ns));
  EXPECT_FALSE(map.Has(u"missing"_ns));
  ASSERT_NE(map.Get(u"hello"_ns), nullptr);
  EXPECT_EQ(*map.Get(u"hello"_ns), 1);
}

TEST(ECMAMap, MoveSet)
{
  TestMap<nsString, nsString, nsStringHashKey> map;
  nsString key(u"key"_ns);
  nsString val(u"value"_ns);
  map.Set(std::move(key), std::move(val));
  EXPECT_TRUE(key.IsEmpty());
  EXPECT_TRUE(val.IsEmpty());
  EXPECT_TRUE(map.Has(u"key"_ns));
  EXPECT_EQ(*map.Get(u"key"_ns), u"value"_ns);
}

TEST(ECMAMap, MoveSetUpdateDoesNotConsumeKey)
{
  TestMap<nsString, nsString, nsStringHashKey> map;
  map.Set(u"key"_ns, u"old"_ns);
  nsString key(u"key"_ns);
  nsString val(u"new"_ns);
  EXPECT_FALSE(map.Set(std::move(key), std::move(val)));
  // Key not consumed on update (only value is moved).
  EXPECT_FALSE(key.IsEmpty());
  EXPECT_TRUE(val.IsEmpty());
  EXPECT_EQ(*map.Get(u"key"_ns), u"new"_ns);
}

TEST(ECMAMap, PointerKeys)
{
  int a = 0;
  int b = 0;
  int* const constA = &a;
  const int value = 1;
  TestMap<int*, int> map;
  EXPECT_TRUE(map.Set(constA, value));
  EXPECT_TRUE(map.Set(&b, 2));
  EXPECT_FALSE(map.Set(constA, 3));
  EXPECT_EQ(map.Size(), 2u);
  ASSERT_NE(map.Get(&a), nullptr);
  EXPECT_EQ(*map.Get(&a), 3);
  ASSERT_NE(map.Get(&b), nullptr);
  EXPECT_EQ(*map.Get(&b), 2);
}

// ==========================================================================
// ECMAMap -- RefPtr value unwrapping
// ==========================================================================

class TestRefCounted final {
 public:
  NS_INLINE_DECL_REFCOUNTING(TestRefCounted)
  explicit TestRefCounted(int aValue) : mValue(aValue) {}
  int mValue;

 private:
  ~TestRefCounted() = default;
};

// ==========================================================================
// ECMASet -- mutation during iteration (ECMA-262 iterator semantics)
// ==========================================================================

// ECMA-262 sec-set.prototype.delete: deleting an entry ahead of the cursor
// must cause the iterator to skip it.
TEST(ECMASet, DeleteAheadDuringIteration)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);
  set.Add(4);

  auto cursor = set.CreateCursor();
  nsTArray<int> seen;
  while (const auto* entry = cursor.Next()) {
    seen.AppendElement(entry->mValue);
    if (seen.LastElement() == 1) {
      // Delete the two entries the cursor has not reached yet.
      set.Delete(2);
      set.Delete(3);
    }
  }
  ASSERT_EQ(seen.Length(), 2u);
  EXPECT_EQ(seen[0], 1);
  EXPECT_EQ(seen[1], 4);
}

// ECMA-262 sec-set.prototype.add: adding an entry during iteration appends
// it to the end, and the iterator must visit it.
TEST(ECMASet, AddDuringIteration)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);

  auto cursor = set.CreateCursor();
  nsTArray<int> seen;
  while (const auto* entry = cursor.Next()) {
    seen.AppendElement(entry->mValue);
    if (seen.LastElement() == 2) {
      set.Add(3);
    }
  }
  ASSERT_EQ(seen.Length(), 3u);
  EXPECT_EQ(seen[0], 1);
  EXPECT_EQ(seen[1], 2);
  EXPECT_EQ(seen[2], 3);
}

// ECMA-262 sec-set.prototype.clear: "The existing [[SetData]] List is
// preserved because there may be existing Set Iterator objects that are
// suspended midway through iterating over that List."
// The iterator must see no more live entries and must terminate.
TEST(ECMASet, ClearDuringIteration)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);

  auto cursor = set.CreateCursor();
  nsTArray<int> seen;
  while (const auto* entry = cursor.Next()) {
    seen.AppendElement(entry->mValue);
    if (seen.LastElement() == 1) {
      set.Clear();
    }
  }
  ASSERT_EQ(seen.Length(), 1u);
  EXPECT_EQ(seen[0], 1);
  EXPECT_EQ(set.PhysicalLength(), 0u);
}

// Delete-then-re-add during iteration: the re-added entry appears at the end
// and is visited exactly once, at its new position.
TEST(ECMASet, DeleteAndReAddDuringIteration)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);

  auto cursor = set.CreateCursor();
  nsTArray<int> seen;
  while (const auto* entry = cursor.Next()) {
    seen.AppendElement(entry->mValue);
    if (seen.LastElement() == 1) {
      set.Delete(2);
      set.Add(2);
    }
  }
  ASSERT_EQ(seen.Length(), 3u);
  EXPECT_EQ(seen[0], 1);
  EXPECT_EQ(seen[1], 3);
  EXPECT_EQ(seen[2], 2);
}

// Adding and deleting on every step, which also drives compaction: every
// entry that is live when the cursor reaches it is yielded exactly once, and
// the iteration terminates.
TEST(ECMASet, EachEntryYieldedOnceUnderChurn)
{
  TestSet<int> set;
  for (int i = 0; i < 6; i++) {
    set.Add(i);
  }

  auto cursor = set.CreateCursor();
  nsTArray<int> seen;
  uint32_t mutations = 0;
  while (const auto* entry = cursor.Next()) {
    const int value = entry->mValue;
    seen.AppendElement(value);
    ASSERT_LT(seen.Length(), 20u);
    if (mutations++ < 3) {
      set.Delete(value + 1);
      set.Add(100 + value);
    }
  }
  ASSERT_EQ(seen.Length(), 6u);
  EXPECT_EQ(seen[0], 0);
  EXPECT_EQ(seen[1], 2);
  EXPECT_EQ(seen[2], 4);
  EXPECT_EQ(seen[3], 100);
  EXPECT_EQ(seen[4], 102);
  EXPECT_EQ(seen[5], 104);
}

// ==========================================================================
// ECMASet -- mutation during C++ range-for (ConstIterator)
// ==========================================================================

TEST(ECMASet, DeleteDuringRangeFor)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);
  set.Add(4);

  nsTArray<int> seen;
  for (const auto& val : set) {
    seen.AppendElement(val);
    if (val == 1) {
      // Delete ahead — 2 should be skipped, 3 and 4 still visited.
      set.Delete(2);
    }
  }
  ASSERT_EQ(seen.Length(), 3u);
  EXPECT_EQ(seen[0], 1);
  EXPECT_EQ(seen[1], 3);
  EXPECT_EQ(seen[2], 4);
}

TEST(ECMASet, AddDuringExplicitForLoop)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);

  nsTArray<int> seen;
  for (auto it = set.begin(); it != set.end(); ++it) {
    seen.AppendElement(*it);
    if (*it == 2) {
      set.Add(3);
    }
  }
  ASSERT_EQ(seen.Length(), 3u);
  EXPECT_EQ(seen[0], 1);
  EXPECT_EQ(seen[1], 2);
  EXPECT_EQ(seen[2], 3);
}

TEST(ECMASet, AddDuringRangeFor)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);

  nsTArray<int> seen;
  for (const auto& val : set) {
    seen.AppendElement(val);
    if (val == 2) {
      set.Add(3);
    }
  }
  ASSERT_EQ(seen.Length(), 3u);
  EXPECT_EQ(seen[0], 1);
  EXPECT_EQ(seen[1], 2);
  EXPECT_EQ(seen[2], 3);
}

// ==========================================================================
// ECMAMap -- mutation during iteration (ECMA-262 iterator semantics)
// ==========================================================================

TEST(ECMAMap, DeleteAheadDuringIteration)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);
  map.Set(4, 400);

  auto cursor = map.CreateCursor();
  nsTArray<int> seen;
  while (const auto* entry = cursor.Next()) {
    seen.AppendElement(entry->mKey);
    if (seen.LastElement() == 1) {
      map.Delete(2);
      map.Delete(3);
    }
  }
  ASSERT_EQ(seen.Length(), 2u);
  EXPECT_EQ(seen[0], 1);
  EXPECT_EQ(seen[1], 4);
}

TEST(ECMAMap, SetNewKeyDuringIteration)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);

  auto cursor = map.CreateCursor();
  nsTArray<int> keys;
  while (const auto* entry = cursor.Next()) {
    keys.AppendElement(entry->mKey);
    if (keys.LastElement() == 2) {
      map.Set(3, 300);
    }
  }
  ASSERT_EQ(keys.Length(), 3u);
  EXPECT_EQ(keys[0], 1);
  EXPECT_EQ(keys[1], 2);
  EXPECT_EQ(keys[2], 3);
}

TEST(ECMAMap, SetExistingKeyDuringIteration)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);

  auto cursor = map.CreateCursor();
  nsTArray<int> keys;
  nsTArray<int> values;
  while (const auto* entry = cursor.Next()) {
    keys.AppendElement(entry->mKey);
    values.AppendElement(entry->mValue);
    if (keys.LastElement() == 1) {
      // Updating an existing key must not change iteration order or count.
      map.Set(2, 999);
    }
  }
  ASSERT_EQ(keys.Length(), 3u);
  EXPECT_EQ(keys[0], 1);
  EXPECT_EQ(keys[1], 2);
  EXPECT_EQ(keys[2], 3);
  EXPECT_EQ(values[1], 999);
}

TEST(ECMAMap, ClearDuringIteration)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);

  auto cursor = map.CreateCursor();
  nsTArray<int> keys;
  while (const auto* entry = cursor.Next()) {
    keys.AppendElement(entry->mKey);
    if (keys.LastElement() == 1) {
      map.Clear();
    }
  }
  ASSERT_EQ(keys.Length(), 1u);
  EXPECT_EQ(keys[0], 1);
  EXPECT_EQ(map.PhysicalLength(), 0u);
}

TEST(ECMAMap, DeleteAndReAddDuringIteration)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);

  auto cursor = map.CreateCursor();
  nsTArray<int> keys;
  nsTArray<int> values;
  while (const auto* entry = cursor.Next()) {
    keys.AppendElement(entry->mKey);
    values.AppendElement(entry->mValue);
    if (keys.LastElement() == 1) {
      map.Delete(2);
      map.Set(2, 999);
    }
  }
  ASSERT_EQ(keys.Length(), 3u);
  EXPECT_EQ(keys[0], 1);
  EXPECT_EQ(keys[1], 3);
  EXPECT_EQ(keys[2], 2);
  EXPECT_EQ(values[2], 999);
}

TEST(ECMAMap, EachEntryYieldedOnceUnderChurn)
{
  TestMap<int, int> map;
  for (int i = 0; i < 6; i++) {
    map.Set(i, i * 100);
  }

  auto cursor = map.CreateCursor();
  nsTArray<int> keys;
  uint32_t mutations = 0;
  while (const auto* entry = cursor.Next()) {
    const int key = entry->mKey;
    keys.AppendElement(key);
    ASSERT_LT(keys.Length(), 20u);
    if (mutations++ < 3) {
      map.Delete(key + 1);
      map.Set(100 + key, key);
    }
  }
  ASSERT_EQ(keys.Length(), 6u);
  EXPECT_EQ(keys[0], 0);
  EXPECT_EQ(keys[1], 2);
  EXPECT_EQ(keys[2], 4);
  EXPECT_EQ(keys[3], 100);
  EXPECT_EQ(keys[4], 102);
  EXPECT_EQ(keys[5], 104);
}

// ==========================================================================
// ECMAMap -- mutation during C++ range-for (ConstIterator)
// ==========================================================================

TEST(ECMAMap, DeleteDuringRangeFor)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);
  map.Set(4, 400);

  nsTArray<int> seen;
  for (const auto& entry : map) {
    const int key = entry.mKey;
    seen.AppendElement(key);
    if (key == 1) {
      map.Delete(2);
    }
  }
  ASSERT_EQ(seen.Length(), 3u);
  EXPECT_EQ(seen[0], 1);
  EXPECT_EQ(seen[1], 3);
  EXPECT_EQ(seen[2], 4);
}

// Its destructor sets an entry in the map that held the last reference to it.
class ReentrantValue final {
 public:
  NS_INLINE_DECL_REFCOUNTING(ReentrantValue)
  explicit ReentrantValue(TestMap<int, RefPtr<ReentrantValue>>* aMap)
      : mMap(aMap) {}

 private:
  ~ReentrantValue() {
    if (mMap) {
      mMap->Set(-1, mozilla::MakeRefPtr<ReentrantValue>(nullptr));
    }
  }

  TestMap<int, RefPtr<ReentrantValue>>* mMap;
};

TEST(ECMAMap, DeleteReleasesValueAfterMutation)
{
  TestMap<int, RefPtr<ReentrantValue>> map;
  map.Set(1, mozilla::MakeRefPtr<ReentrantValue>(&map));
  EXPECT_TRUE(map.Delete(1));
  EXPECT_FALSE(map.Has(1));
  EXPECT_TRUE(map.Has(-1));
  EXPECT_EQ(map.Size(), 1u);
}

TEST(ECMAMap, ClearReleasesValuesAfterMutation)
{
  TestMap<int, RefPtr<ReentrantValue>> map;
  map.Set(1, mozilla::MakeRefPtr<ReentrantValue>(&map));
  map.Set(2, mozilla::MakeRefPtr<ReentrantValue>(nullptr));
  map.Clear();
  EXPECT_TRUE(map.Has(-1));
  EXPECT_EQ(map.Size(), 1u);
}

TEST(ECMAMap, SetReleasesReplacedValueAfterMutation)
{
  TestMap<int, RefPtr<ReentrantValue>> map;
  map.Set(1, mozilla::MakeRefPtr<ReentrantValue>(&map));
  EXPECT_FALSE(map.Set(1, mozilla::MakeRefPtr<ReentrantValue>(nullptr)));
  EXPECT_TRUE(map.Has(-1));
  EXPECT_EQ(map.Size(), 2u);
}

TEST(ECMAMap, SetNewKeyDuringExplicitForLoop)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);

  nsTArray<int> keys;
  for (auto it = map.begin(); it != map.end(); ++it) {
    const int key = it->mKey;
    keys.AppendElement(key);
    if (key == 2) {
      map.Set(3, 300);
    }
  }
  ASSERT_EQ(keys.Length(), 3u);
  EXPECT_EQ(keys[0], 1);
  EXPECT_EQ(keys[1], 2);
  EXPECT_EQ(keys[2], 3);
}

// ==========================================================================
// ECMAMap -- RefPtr value unwrapping
// ==========================================================================

TEST(ECMAMap, GetReturnsRawPointerForRefPtrValues)
{
  TestMap<int, RefPtr<TestRefCounted>> map;
  auto obj = mozilla::MakeRefPtr<TestRefCounted>(42);
  TestRefCounted* raw = obj.get();
  map.Set(1, obj);

  // Get() returns TestRefCounted*, not RefPtr<TestRefCounted>*.
  TestRefCounted* result = map.Get(1);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result, raw);
  EXPECT_EQ(result->mValue, 42);
  EXPECT_EQ(map.Get(2), nullptr);
}

TEST(ECMAMap, ConstGetReturnsRawPointerForRefPtrValues)
{
  TestMap<int, RefPtr<TestRefCounted>> map;
  map.Set(1, mozilla::MakeRefPtr<TestRefCounted>(99));
  const auto& constMap = map;

  TestRefCounted* result = constMap.Get(1);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->mValue, 99);
}

// ==========================================================================
// Lookup types: entries are looked up by the hash key's KeyType, so a stored
// nsString is found by any nsAString and a stored RefPtr<T> by a raw T*.
// ==========================================================================

TEST(ECMASet, StringKeysLookUpBySubstring)
{
  TestSet<nsString> set;
  set.Add(u"hello"_ns);
  set.Add(u"world"_ns);

  const nsAString& hello = Substring(u"say hello"_ns, 4);
  const nsAString& missing = nsDependentString(u"missing");
  EXPECT_TRUE(set.Has(hello));
  EXPECT_FALSE(set.Has(missing));
  EXPECT_TRUE(set.Delete(hello));
  EXPECT_FALSE(set.Has(hello));
  EXPECT_EQ(set.Size(), 1u);
}

TEST(ECMAMap, StringKeysLookUpBySubstring)
{
  TestMap<nsString, int> map;
  map.Set(u"hello"_ns, 1);

  const nsAString& hello = Substring(u"say hello"_ns, 4);
  EXPECT_TRUE(map.Has(hello));
  ASSERT_NE(map.Get(hello), nullptr);
  EXPECT_EQ(*map.Get(hello), 1);
  EXPECT_TRUE(map.Delete(hello));
  EXPECT_EQ(map.Get(hello), nullptr);
}

TEST(ECMASet, RefPtrKeysLookUpByRawPointer)
{
  TestSet<RefPtr<TestRefCounted>> set;
  auto first = mozilla::MakeRefPtr<TestRefCounted>(1);
  auto second = mozilla::MakeRefPtr<TestRefCounted>(2);
  set.Add(first);
  set.Add(second);

  TestRefCounted* raw = first.get();
  EXPECT_TRUE(set.Has(raw));
  EXPECT_TRUE(set.Has(second));
  EXPECT_TRUE(set.Delete(raw));
  EXPECT_FALSE(set.Has(raw));
  EXPECT_EQ(set.Size(), 1u);
}

TEST(ECMAMap, RefPtrKeysLookUpByRawPointer)
{
  TestMap<RefPtr<TestRefCounted>, int> map;
  auto key = mozilla::MakeRefPtr<TestRefCounted>(1);
  map.Set(key, 100);

  TestRefCounted* raw = key.get();
  EXPECT_TRUE(map.Has(raw));
  ASSERT_NE(map.Get(raw), nullptr);
  EXPECT_EQ(*map.Get(raw), 100);
  EXPECT_TRUE(map.Delete(raw));
  EXPECT_EQ(map.Get(raw), nullptr);
}

// ==========================================================================
// Binding argument types: the collections also take what the generated
// bindings pass, a U& for a stored RefPtr<U> and a substring for a stored
// nsString.
// ==========================================================================

TEST(ECMASet, RefPtrKeysTakeReferences)
{
  TestSet<RefPtr<TestRefCounted>> set;
  auto obj = mozilla::MakeRefPtr<TestRefCounted>(1);
  TestRefCounted& ref = *obj;
  EXPECT_TRUE(set.Add(ref));
  EXPECT_FALSE(set.Add(ref));
  EXPECT_TRUE(set.Has(ref));
  EXPECT_TRUE(set.Delete(ref));
  EXPECT_FALSE(set.Has(ref));
}

TEST(ECMASet, StringKeysAddSubstring)
{
  TestSet<nsString> set;
  const nsAString& hello = Substring(u"say hello"_ns, 4);
  EXPECT_TRUE(set.Add(hello));
  EXPECT_FALSE(set.Add(hello));
  EXPECT_TRUE(set.Has(u"hello"_ns));
  EXPECT_EQ(*set.begin(), u"hello"_ns);
}

TEST(ECMAMap, RefPtrKeysAndValuesTakeReferences)
{
  TestMap<RefPtr<TestRefCounted>, RefPtr<TestRefCounted>> map;
  auto key = mozilla::MakeRefPtr<TestRefCounted>(1);
  auto first = mozilla::MakeRefPtr<TestRefCounted>(2);
  auto second = mozilla::MakeRefPtr<TestRefCounted>(3);
  EXPECT_TRUE(map.Set(*key, *first));
  EXPECT_FALSE(map.Set(*key, *second));
  EXPECT_TRUE(map.Has(*key));
  EXPECT_EQ(map.Get(*key), second.get());
  EXPECT_TRUE(map.Delete(*key));
  EXPECT_EQ(map.Get(*key), nullptr);
}

TEST(ECMAMap, StringKeysSetSubstring)
{
  TestMap<nsString, RefPtr<TestRefCounted>> map;
  auto value = mozilla::MakeRefPtr<TestRefCounted>(1);
  const nsAString& hello = Substring(u"say hello"_ns, 4);
  EXPECT_TRUE(map.Set(hello, *value));
  EXPECT_EQ(map.Get(u"hello"_ns), value.get());
}

// ==========================================================================
// Cursors
// ==========================================================================

TEST(ECMASet, CopiedCursorContinuesIndependently)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);

  auto cursor = set.CreateCursor();
  EXPECT_EQ(cursor.Next()->mValue, 1);
  auto copy = cursor;
  EXPECT_EQ(cursor.Next()->mValue, 2);
  EXPECT_EQ(cursor.Next()->mValue, 3);
  EXPECT_EQ(cursor.Next(), nullptr);
  EXPECT_EQ(copy.Next()->mValue, 2);
}

TEST(ECMASet, ReverseCursor)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);
  set.Delete(2);

  auto cursor = set.CreateReverseCursor();
  EXPECT_EQ(cursor.Next()->mValue, 3);
  EXPECT_EQ(cursor.Next()->mValue, 1);
  EXPECT_EQ(cursor.Next(), nullptr);
}

TEST(ECMAMap, CursorEndsOnUnlink)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);

  auto cursor = map.CreateCursor();
  EXPECT_EQ(cursor.Next()->mKey, 1);
  map.Unlink();
  EXPECT_EQ(cursor.Next(), nullptr);
}

// ==========================================================================
// Standard container behaviour
// ==========================================================================

TEST(ECMASet, WorksWithStandardAlgorithms)
{
  TestSet<int> set;
  for (int i = 0; i < 5; i++) {
    set.Add(i * 10);
  }
  set.Delete(20);

  auto found = std::find_if(set.begin(), set.end(),
                            [](int aValue) { return aValue > 15; });
  ASSERT_TRUE(found != set.end());
  EXPECT_EQ(*found, 30);
  EXPECT_EQ(std::distance(set.begin(), set.end()), 4);
  EXPECT_EQ(*std::max_element(set.begin(), set.end()), 40);
  EXPECT_EQ(std::count_if(set.begin(), set.end(),
                          [](int aValue) { return aValue % 20 == 0; }),
            2);
}

TEST(ECMAMap, WorksWithStandardAlgorithms)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);

  auto found = std::find_if(map.begin(), map.end(), [](const auto& aEntry) {
    return aEntry.mValue == 200;
  });
  ASSERT_TRUE(found != map.end());
  EXPECT_EQ(found->mKey, 2);
  EXPECT_EQ(std::distance(map.rbegin(), map.rend()), 3);
}

TEST(ECMASet, IteratorCopyAssignmentAndPostIncrement)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);

  auto it = set.begin();
  auto old = it++;
  EXPECT_EQ(*old, 1);
  EXPECT_EQ(*it, 2);

  old = it;
  EXPECT_TRUE(old == it);
  ++it;
  EXPECT_FALSE(old == it);
  EXPECT_EQ(*old, 2);
  EXPECT_EQ(*it, 3);

  it = set.end();
  EXPECT_TRUE(it == set.end());
  EXPECT_EQ(*old, 2);
}

TEST(ECMASet, IteratorEquality)
{
  TestSet<int> set;
  EXPECT_TRUE(set.begin() == set.end());
  EXPECT_TRUE(set.rbegin() == set.rend());

  set.Add(1);
  set.Add(2);
  EXPECT_TRUE(set.begin() == set.begin());
  EXPECT_TRUE(set.end() == set.end());
  EXPECT_TRUE(set.begin() != set.end());
  EXPECT_TRUE(++set.begin() != set.begin());
}

// ==========================================================================
// ECMASet -- reverse iteration
// ==========================================================================

TEST(ECMASet, ReverseInsertionOrder)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);

  nsTArray<int> result;
  for (auto it = set.rbegin(); it != set.rend(); ++it) {
    result.AppendElement(*it);
  }
  ASSERT_EQ(result.Length(), 3u);
  EXPECT_EQ(result[0], 3);
  EXPECT_EQ(result[1], 2);
  EXPECT_EQ(result[2], 1);
}

TEST(ECMASet, ReverseEmptySet)
{
  TestSet<int> set;
  EXPECT_EQ(set.rbegin(), set.rend());
  // Iteration over empty set visits nothing.
  int count = 0;
  for (auto it = set.rbegin(); it != set.rend(); ++it) {
    count++;
  }
  EXPECT_EQ(count, 0);
}

TEST(ECMASet, ReverseSingleElement)
{
  TestSet<int> set;
  set.Add(42);

  nsTArray<int> result;
  for (auto it = set.rbegin(); it != set.rend(); ++it) {
    result.AppendElement(*it);
  }
  ASSERT_EQ(result.Length(), 1u);
  EXPECT_EQ(result[0], 42);
}

TEST(ECMASet, ReverseSkipsTombstones)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);
  set.Add(4);
  set.Delete(1);
  set.Delete(3);

  nsTArray<int> result;
  for (auto it = set.rbegin(); it != set.rend(); ++it) {
    result.AppendElement(*it);
  }
  ASSERT_EQ(result.Length(), 2u);
  EXPECT_EQ(result[0], 4);
  EXPECT_EQ(result[1], 2);
}

TEST(ECMASet, ReverseAllTombstoned)
{
  TestSet<int> set;
  set.Add(1);
  set.Add(2);
  set.Add(3);

  set.Delete(1);
  set.Delete(2);
  set.Delete(3);
  EXPECT_EQ(set.PhysicalLength(), 0u);

  int count = 0;
  for (auto it = set.rbegin(); it != set.rend(); ++it) {
    count++;
  }
  EXPECT_EQ(count, 0);
}

TEST(ECMASet, ForwardAndReverseYieldSameElements)
{
  TestSet<int> set;
  for (int i = 0; i < 10; i++) {
    set.Add(i);
  }
  set.Delete(3);
  set.Delete(7);

  nsTArray<int> fwd;
  for (const auto& val : set) {
    fwd.AppendElement(val);
  }

  nsTArray<int> rev;
  for (auto it = set.rbegin(); it != set.rend(); ++it) {
    rev.AppendElement(*it);
  }

  ASSERT_EQ(fwd.Length(), rev.Length());
  for (size_t i = 0; i < fwd.Length(); i++) {
    EXPECT_EQ(fwd[i], rev[rev.Length() - 1 - i]);
  }
}

// ==========================================================================
// ECMAMap -- reverse iteration
// ==========================================================================

TEST(ECMAMap, ReverseInsertionOrder)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);

  nsTArray<int> keys;
  nsTArray<int> values;
  for (auto it = map.rbegin(); it != map.rend(); ++it) {
    keys.AppendElement(it->mKey);
    values.AppendElement(it->mValue);
  }
  ASSERT_EQ(keys.Length(), 3u);
  EXPECT_EQ(keys[0], 3);
  EXPECT_EQ(keys[1], 2);
  EXPECT_EQ(keys[2], 1);
  EXPECT_EQ(values[0], 300);
  EXPECT_EQ(values[1], 200);
  EXPECT_EQ(values[2], 100);
}

// mozilla::Reversed copies the iterators it is given.
TEST(ECMAMap, ReversedRange)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);
  map.Delete(2);

  nsTArray<int> keys;
  for (const auto& [key, value] : mozilla::Reversed(map)) {
    EXPECT_EQ(value, key * 100);
    keys.AppendElement(key);
  }
  ASSERT_EQ(keys.Length(), 2u);
  EXPECT_EQ(keys[0], 3);
  EXPECT_EQ(keys[1], 1);
}

TEST(ECMAMap, ReverseEmptyMap)
{
  TestMap<int, int> map;
  EXPECT_EQ(map.rbegin(), map.rend());
  int count = 0;
  for (auto it = map.rbegin(); it != map.rend(); ++it) {
    count++;
  }
  EXPECT_EQ(count, 0);
}

TEST(ECMAMap, ReverseSkipsTombstones)
{
  TestMap<int, int> map;
  map.Set(1, 100);
  map.Set(2, 200);
  map.Set(3, 300);
  map.Set(4, 400);
  map.Delete(1);
  map.Delete(3);

  nsTArray<int> keys;
  for (auto it = map.rbegin(); it != map.rend(); ++it) {
    keys.AppendElement(it->mKey);
  }
  ASSERT_EQ(keys.Length(), 2u);
  EXPECT_EQ(keys[0], 4);
  EXPECT_EQ(keys[1], 2);
}

TEST(ECMAMap, ForwardAndReverseYieldSameElements)
{
  TestMap<int, int> map;
  for (int i = 0; i < 10; i++) {
    map.Set(i, i * 10);
  }
  map.Delete(3);
  map.Delete(7);

  nsTArray<int> fwd;
  for (const auto& entry : map) {
    fwd.AppendElement(entry.mKey);
  }

  nsTArray<int> rev;
  for (auto it = map.rbegin(); it != map.rend(); ++it) {
    rev.AppendElement(it->mKey);
  }

  ASSERT_EQ(fwd.Length(), rev.Length());
  for (size_t i = 0; i < fwd.Length(); i++) {
    EXPECT_EQ(fwd[i], rev[rev.Length() - 1 - i]);
  }
}
