/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <type_traits>
#include <vector>

#include "Intervals.h"
#include "TimeUnits.h"
#include "gtest/gtest.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/TimeRanges.h"

using namespace mozilla;

typedef media::Interval<uint8_t> ByteInterval;
typedef media::Interval<int> IntInterval;
typedef media::IntervalSet<int> IntIntervals;

ByteInterval CreateByteInterval(int32_t aStart, int32_t aEnd) {
  ByteInterval test(aStart, aEnd);
  return test;
}

media::IntervalSet<uint8_t> CreateByteIntervalSet(int32_t aStart,
                                                  int32_t aEnd) {
  media::IntervalSet<uint8_t> test;
  test += ByteInterval(aStart, aEnd);
  return test;
}

TEST(IntervalSet, Constructors)
{
  const int32_t start = 1;
  const int32_t end = 2;
  const int32_t fuzz = 0;

  // Compiler exercise.
  ByteInterval test1(start, end);
  ByteInterval test2(test1);
  ByteInterval test3(start, end, fuzz);
  ByteInterval test4(test3);
  ByteInterval test5 = CreateByteInterval(start, end);

  media::IntervalSet<uint8_t> blah1(test1);
  media::IntervalSet<uint8_t> blah2 = blah1;
  media::IntervalSet<uint8_t> blah3 = blah1 + test1;
  media::IntervalSet<uint8_t> blah4 = test1 + blah1;
  media::IntervalSet<uint8_t> blah5 = CreateByteIntervalSet(start, end);
  (void)test1;
  (void)test2;
  (void)test3;
  (void)test4;
  (void)test5;
  (void)blah1;
  (void)blah2;
  (void)blah3;
  (void)blah4;
  (void)blah5;
}

media::TimeInterval CreateTimeInterval(int32_t aStart, int32_t aEnd) {
  // Copy constructor test
  media::TimeUnit start = media::TimeUnit::FromMicroseconds(aStart);
  media::TimeUnit end;
  // operator=  test
  end = media::TimeUnit::FromMicroseconds(aEnd);
  media::TimeInterval ti(start, end);
  return ti;
}

media::TimeIntervals CreateTimeIntervals(int32_t aStart, int32_t aEnd) {
  media::TimeIntervals test;
  test += CreateTimeInterval(aStart, aEnd);
  return test;
}

TEST(IntervalSet, TimeIntervalsConstructors)
{
  const auto start = media::TimeUnit::FromMicroseconds(1);
  const auto end = media::TimeUnit::FromMicroseconds(2);
  const media::TimeUnit fuzz;

  // Compiler exercise.
  media::TimeInterval test1(start, end);
  media::TimeInterval test2(test1);
  media::TimeInterval test3(start, end, fuzz);
  media::TimeInterval test4(test3);
  media::TimeInterval test5 =
      CreateTimeInterval(start.ToMicroseconds(), end.ToMicroseconds());

  media::TimeIntervals blah1(test1);
  media::TimeIntervals blah2(blah1);
  media::TimeIntervals blah3 = blah1 + test1;
  media::TimeIntervals blah4 = test1 + blah1;
  media::TimeIntervals blah5 =
      CreateTimeIntervals(start.ToMicroseconds(), end.ToMicroseconds());
  (void)test1;
  (void)test2;
  (void)test3;
  (void)test4;
  (void)test5;
  (void)blah1;
  (void)blah2;
  (void)blah3;
  (void)blah4;
  (void)blah5;

  media::TimeIntervals i0{media::TimeInterval(media::TimeUnit::FromSeconds(0),
                                              media::TimeUnit::FromSeconds(0))};
  EXPECT_TRUE(i0.IsEmpty());  // Constructing with an empty time interval.
}

TEST(IntervalSet, Length)
{
  IntInterval i(15, 25);
  EXPECT_EQ(10, i.Length());
}

TEST(IntervalSet, Intersects)
{
  EXPECT_TRUE(IntInterval(1, 5).Intersects(IntInterval(3, 4)));
  EXPECT_TRUE(IntInterval(1, 5).Intersects(IntInterval(3, 7)));
  EXPECT_TRUE(IntInterval(1, 5).Intersects(IntInterval(-1, 3)));
  EXPECT_TRUE(IntInterval(1, 5).Intersects(IntInterval(-1, 7)));
  EXPECT_FALSE(IntInterval(1, 5).Intersects(IntInterval(6, 7)));
  EXPECT_FALSE(IntInterval(1, 5).Intersects(IntInterval(-1, 0)));
  // End boundary is exclusive of the interval.
  EXPECT_FALSE(IntInterval(1, 5).Intersects(IntInterval(5, 7)));
  EXPECT_FALSE(IntInterval(1, 5).Intersects(IntInterval(0, 1)));
  // Empty identical interval do not intersect.
  EXPECT_FALSE(IntInterval(1, 1).Intersects(IntInterval(1, 1)));
  // Empty interval do not intersect.
  EXPECT_FALSE(IntInterval(1, 1).Intersects(IntInterval(2, 2)));
}

TEST(IntervalSet, Intersection)
{
  IntInterval i0(10, 20);
  IntInterval i1(15, 25);
  IntInterval i = i0.Intersection(i1);
  EXPECT_EQ(15, i.mStart);
  EXPECT_EQ(20, i.mEnd);
  IntInterval j0(10, 20);
  IntInterval j1(20, 25);
  IntInterval j = j0.Intersection(j1);
  EXPECT_TRUE(j.IsEmpty());
  IntInterval k0(2, 2);
  IntInterval k1(2, 2);
  IntInterval k = k0.Intersection(k1);
  EXPECT_TRUE(k.IsEmpty());
}

TEST(IntervalSet, Equals)
{
  IntInterval i0(10, 20);
  IntInterval i1(10, 20);
  EXPECT_EQ(i0, i1);

  IntInterval i2(5, 20);
  EXPECT_NE(i0, i2);

  IntInterval i3(10, 15);
  EXPECT_NE(i0, i2);
}

TEST(IntervalSet, IntersectionIntervalSet)
{
  IntIntervals i0;
  i0 += IntInterval(5, 10);
  i0 += IntInterval(20, 25);
  i0 += IntInterval(40, 60);

  IntIntervals i1;
  i1.Add(IntInterval(7, 15));
  i1.Add(IntInterval(16, 27));
  i1.Add(IntInterval(45, 50));
  i1.Add(IntInterval(53, 57));

  IntIntervals i = media::Intersection(i0, i1);

  EXPECT_EQ(4u, i.Length());

  EXPECT_EQ(7, i[0].mStart);
  EXPECT_EQ(10, i[0].mEnd);

  EXPECT_EQ(20, i[1].mStart);
  EXPECT_EQ(25, i[1].mEnd);

  EXPECT_EQ(45, i[2].mStart);
  EXPECT_EQ(50, i[2].mEnd);

  EXPECT_EQ(53, i[3].mStart);
  EXPECT_EQ(57, i[3].mEnd);
}

template <typename T>
static void Compare(const media::IntervalSet<T>& aI1,
                    const media::IntervalSet<T>& aI2) {
  EXPECT_EQ(aI1.Length(), aI2.Length());
  if (aI1.Length() != aI2.Length()) {
    return;
  }
  for (uint32_t i = 0; i < aI1.Length(); i++) {
    EXPECT_EQ(aI1[i].mStart, aI2[i].mStart);
    EXPECT_EQ(aI1[i].mEnd, aI2[i].mEnd);
  }
}

static void GeneratePermutations(const IntIntervals& aI1,
                                 const IntIntervals& aI2) {
  IntIntervals i_ref = media::Intersection(aI1, aI2);
  // Test all permutations possible
  std::vector<uint32_t> comb1;
  for (uint32_t i = 0; i < aI1.Length(); i++) {
    comb1.push_back(i);
  }
  std::vector<uint32_t> comb2;
  for (uint32_t i = 0; i < aI2.Length(); i++) {
    comb2.push_back(i);
  }

  do {
    do {
      // Create intervals according to new indexes.
      IntIntervals i_0;
      for (uint32_t i = 0; i < comb1.size(); i++) {
        i_0 += aI1[comb1[i]];
      }
      // Test that intervals are always normalized.
      Compare(aI1, i_0);
      IntIntervals i_1;
      for (uint32_t i = 0; i < comb2.size(); i++) {
        i_1 += aI2[comb2[i]];
      }
      Compare(aI2, i_1);
      // Check intersections yield the same result.
      Compare(i_0.Intersection(i_1), i_ref);
    } while (std::next_permutation(comb2.begin(), comb2.end()));
  } while (std::next_permutation(comb1.begin(), comb1.end()));
}

TEST(IntervalSet, IntersectionNormalizedIntervalSet)
{
  IntIntervals i0;
  i0 += IntInterval(5, 10);
  i0 += IntInterval(20, 25);
  i0 += IntInterval(40, 60);

  IntIntervals i1;
  i1.Add(IntInterval(7, 15));
  i1.Add(IntInterval(16, 27));
  i1.Add(IntInterval(45, 50));
  i1.Add(IntInterval(53, 57));

  GeneratePermutations(i0, i1);
}

TEST(IntervalSet, IntersectionUnorderedNonNormalizedIntervalSet)
{
  IntIntervals i0;
  i0 += IntInterval(5, 10);
  i0 += IntInterval(8, 25);
  i0 += IntInterval(24, 60);

  IntIntervals i1;
  i1.Add(IntInterval(7, 15));
  i1.Add(IntInterval(10, 27));
  i1.Add(IntInterval(45, 50));
  i1.Add(IntInterval(53, 57));

  GeneratePermutations(i0, i1);
}

TEST(IntervalSet, IntersectionNonNormalizedInterval)
{
  IntIntervals i0;
  i0 += IntInterval(5, 10);
  i0 += IntInterval(8, 25);
  i0 += IntInterval(30, 60);

  media::Interval<int> i1(9, 15);
  i0.Intersection(i1);
  EXPECT_EQ(1u, i0.Length());
  EXPECT_EQ(i0[0].mStart, i1.mStart);
  EXPECT_EQ(i0[0].mEnd, i1.mEnd);
}

TEST(IntervalSet, IntersectionUnorderedNonNormalizedInterval)
{
  IntIntervals i0;
  i0 += IntInterval(1, 3);
  i0 += IntInterval(1, 10);
  i0 += IntInterval(9, 12);
  i0 += IntInterval(12, 15);
  i0 += IntInterval(8, 25);
  i0 += IntInterval(30, 60);
  i0 += IntInterval(5, 10);
  i0 += IntInterval(30, 60);

  media::Interval<int> i1(9, 15);
  i0.Intersection(i1);
  EXPECT_EQ(1u, i0.Length());
  EXPECT_EQ(i0[0].mStart, i1.mStart);
  EXPECT_EQ(i0[0].mEnd, i1.mEnd);
}

static IntIntervals Duplicate(const IntIntervals& aValue) {
  IntIntervals value(aValue);
  return value;
}

TEST(IntervalSet, Normalize)
{
  IntIntervals i;
  // Test IntervalSet<T> + Interval<T> operator.
  i = i + IntInterval(20, 30);
  // Test Internal<T> + IntervalSet<T> operator.
  i = IntInterval(2, 7) + i;
  // Test Interval<T> + IntervalSet<T> operator
  i = IntInterval(1, 8) + i;
  IntIntervals interval;
  interval += IntInterval(5, 10);
  // Test += with rval move.
  i += Duplicate(interval);
  // Test = with move and add with move.
  i = Duplicate(interval) + i;

  EXPECT_EQ(2u, i.Length());

  EXPECT_EQ(1, i[0].mStart);
  EXPECT_EQ(10, i[0].mEnd);

  EXPECT_EQ(20, i[1].mStart);
  EXPECT_EQ(30, i[1].mEnd);

  media::TimeIntervals ti;
  ti += media::TimeInterval(media::TimeUnit::FromSeconds(0.0),
                            media::TimeUnit::FromSeconds(3.203333));
  ti += media::TimeInterval(media::TimeUnit::FromSeconds(3.203366),
                            media::TimeUnit::FromSeconds(10.010065));
  EXPECT_EQ(2u, ti.Length());
  ti += media::TimeInterval(ti.Start(0), ti.End(0),
                            media::TimeUnit::FromMicroseconds(35000));
  EXPECT_EQ(1u, ti.Length());
}

TEST(IntervalSet, ContainValue)
{
  IntIntervals i0;
  i0 += IntInterval(0, 10);
  i0 += IntInterval(15, 20);
  i0 += IntInterval(30, 50);
  EXPECT_TRUE(i0.Contains(0));  // start is inclusive.
  EXPECT_TRUE(i0.Contains(17));
  EXPECT_FALSE(i0.Contains(20));  // end boundary is exclusive.
  EXPECT_FALSE(i0.Contains(25));
}

TEST(IntervalSet, ContainValueWithFuzz)
{
  IntIntervals i0;
  i0 += IntInterval(0, 10);
  i0 += IntInterval(15, 20, 1);
  i0 += IntInterval(30, 50);
  EXPECT_TRUE(i0.Contains(0));  // start is inclusive.
  EXPECT_TRUE(i0.Contains(17));
  EXPECT_TRUE(
      i0.Contains(20));  // end boundary is exclusive but we have a fuzz of 1.
  EXPECT_FALSE(i0.Contains(25));
}

TEST(IntervalSet, ContainInterval)
{
  IntIntervals i0;
  i0 += IntInterval(0, 10);
  i0 += IntInterval(15, 20);
  i0 += IntInterval(30, 50);
  EXPECT_TRUE(i0.Contains(IntInterval(2, 8)));
  EXPECT_TRUE(i0.Contains(IntInterval(31, 50)));
  EXPECT_TRUE(i0.Contains(IntInterval(0, 10)));
  EXPECT_FALSE(i0.Contains(IntInterval(0, 11)));
  EXPECT_TRUE(i0.Contains(IntInterval(0, 5)));
  EXPECT_FALSE(i0.Contains(IntInterval(8, 15)));
  EXPECT_FALSE(i0.Contains(IntInterval(15, 30)));
  EXPECT_FALSE(i0.Contains(IntInterval(30, 55)));
}

TEST(IntervalSet, ContainIntervalWithFuzz)
{
  IntIntervals i0;
  i0 += IntInterval(0, 10);
  i0 += IntInterval(15, 20);
  i0 += IntInterval(30, 50);
  EXPECT_TRUE(i0.Contains(IntInterval(2, 8)));
  EXPECT_TRUE(i0.Contains(IntInterval(31, 50)));
  EXPECT_TRUE(i0.Contains(IntInterval(0, 11, 1)));
  EXPECT_TRUE(i0.Contains(IntInterval(0, 5)));
  EXPECT_FALSE(i0.Contains(IntInterval(8, 15)));
  EXPECT_FALSE(i0.Contains(IntInterval(15, 21)));
  EXPECT_FALSE(i0.Contains(IntInterval(15, 30)));
  EXPECT_FALSE(i0.Contains(IntInterval(30, 55)));

  IntIntervals i1;
  i1 += IntInterval(0, 10, 1);
  i1 += IntInterval(15, 20, 1);
  i1 += IntInterval(30, 50, 1);
  EXPECT_TRUE(i1.Contains(IntInterval(2, 8)));
  EXPECT_TRUE(i1.Contains(IntInterval(29, 51)));
  EXPECT_TRUE(i1.Contains(IntInterval(0, 11, 1)));
  EXPECT_TRUE(i1.Contains(IntInterval(15, 21)));
}

TEST(IntervalSet, Span)
{
  IntInterval i0(0, 10);
  IntInterval i1(20, 30);
  IntInterval i{i0.Span(i1)};

  EXPECT_EQ(i.mStart, 0);
  EXPECT_EQ(i.mEnd, 30);
}

TEST(IntervalSet, Union)
{
  IntIntervals i0;
  i0 += IntInterval(5, 10);
  i0 += IntInterval(20, 25);
  i0 += IntInterval(40, 60);

  IntIntervals i1;
  i1.Add(IntInterval(7, 15));
  i1.Add(IntInterval(16, 27));
  i1.Add(IntInterval(45, 50));
  i1.Add(IntInterval(53, 57));

  IntIntervals i = media::Union(i0, i1);

  EXPECT_EQ(3u, i.Length());

  EXPECT_EQ(5, i[0].mStart);
  EXPECT_EQ(15, i[0].mEnd);

  EXPECT_EQ(16, i[1].mStart);
  EXPECT_EQ(27, i[1].mEnd);

  EXPECT_EQ(40, i[2].mStart);
  EXPECT_EQ(60, i[2].mEnd);
}

TEST(IntervalSet, UnionNotOrdered)
{
  IntIntervals i0;
  i0 += IntInterval(20, 25);
  i0 += IntInterval(40, 60);
  i0 += IntInterval(5, 10);

  IntIntervals i1;
  i1.Add(IntInterval(16, 27));
  i1.Add(IntInterval(7, 15));
  i1.Add(IntInterval(53, 57));
  i1.Add(IntInterval(45, 50));

  IntIntervals i = media::Union(i0, i1);

  EXPECT_EQ(3u, i.Length());

  EXPECT_EQ(5, i[0].mStart);
  EXPECT_EQ(15, i[0].mEnd);

  EXPECT_EQ(16, i[1].mStart);
  EXPECT_EQ(27, i[1].mEnd);

  EXPECT_EQ(40, i[2].mStart);
  EXPECT_EQ(60, i[2].mEnd);
}

TEST(IntervalSet, NormalizeFuzz)
{
  IntIntervals i0;
  i0 += IntInterval(11, 25, 0);
  i0 += IntInterval(5, 10, 1);
  i0 += IntInterval(40, 60, 1);

  EXPECT_EQ(2u, i0.Length());

  EXPECT_EQ(5, i0[0].mStart);
  EXPECT_EQ(25, i0[0].mEnd);

  EXPECT_EQ(40, i0[1].mStart);
  EXPECT_EQ(60, i0[1].mEnd);
}

TEST(IntervalSet, UnionFuzz)
{
  IntIntervals i0;
  i0 += IntInterval(5, 10, 1);
  i0 += IntInterval(11, 25, 0);
  i0 += IntInterval(40, 60, 1);
  EXPECT_EQ(2u, i0.Length());
  EXPECT_EQ(5, i0[0].mStart);
  EXPECT_EQ(25, i0[0].mEnd);
  EXPECT_EQ(40, i0[1].mStart);
  EXPECT_EQ(60, i0[1].mEnd);

  IntIntervals i1;
  i1.Add(IntInterval(7, 15, 1));
  i1.Add(IntInterval(16, 27, 1));
  i1.Add(IntInterval(45, 50, 1));
  i1.Add(IntInterval(53, 57, 1));
  EXPECT_EQ(3u, i1.Length());
  EXPECT_EQ(7, i1[0].mStart);
  EXPECT_EQ(27, i1[0].mEnd);
  EXPECT_EQ(45, i1[1].mStart);
  EXPECT_EQ(50, i1[1].mEnd);
  EXPECT_EQ(53, i1[2].mStart);
  EXPECT_EQ(57, i1[2].mEnd);

  IntIntervals i = media::Union(i0, i1);

  EXPECT_EQ(2u, i.Length());

  EXPECT_EQ(5, i[0].mStart);
  EXPECT_EQ(27, i[0].mEnd);

  EXPECT_EQ(40, i[1].mStart);
  EXPECT_EQ(60, i[1].mEnd);
}

TEST(IntervalSet, Contiguous)
{
  EXPECT_FALSE(IntInterval(5, 10).Contiguous(IntInterval(11, 25)));
  EXPECT_TRUE(IntInterval(5, 10).Contiguous(IntInterval(10, 25)));
  EXPECT_TRUE(IntInterval(5, 10, 1).Contiguous(IntInterval(11, 25)));
  EXPECT_TRUE(IntInterval(5, 10).Contiguous(IntInterval(11, 25, 1)));
}

TEST(IntervalSet, TimeRangesSeconds)
{
  media::TimeIntervals i0;
  i0 += media::TimeInterval(media::TimeUnit::FromSeconds(20),
                            media::TimeUnit::FromSeconds(25));
  i0 += media::TimeInterval(media::TimeUnit::FromSeconds(40),
                            media::TimeUnit::FromSeconds(60));
  i0 += media::TimeInterval(media::TimeUnit::FromSeconds(5),
                            media::TimeUnit::FromSeconds(10));

  media::TimeIntervals i1;
  i1.Add(media::TimeInterval(media::TimeUnit::FromSeconds(16),
                             media::TimeUnit::FromSeconds(27)));
  i1.Add(media::TimeInterval(media::TimeUnit::FromSeconds(7),
                             media::TimeUnit::FromSeconds(15)));
  i1.Add(media::TimeInterval(media::TimeUnit::FromSeconds(53),
                             media::TimeUnit::FromSeconds(57)));
  i1.Add(media::TimeInterval(media::TimeUnit::FromSeconds(45),
                             media::TimeUnit::FromSeconds(50)));

  media::TimeIntervals i(i0 + i1);
  RefPtr<dom::TimeRanges> tr = new dom::TimeRanges(i);
  EXPECT_EQ(tr->Length(), i.Length());
  for (dom::TimeRanges::index_type index = 0; index < tr->Length(); index++) {
    ErrorResult rv;
    EXPECT_EQ(tr->Start(index, rv), i[index].mStart.ToSeconds());
    EXPECT_EQ(tr->Start(index, rv), i.Start(index).ToSeconds());
    EXPECT_EQ(tr->End(index, rv), i[index].mEnd.ToSeconds());
    EXPECT_EQ(tr->End(index, rv), i.End(index).ToSeconds());
  }
}

static void CheckTimeRanges(dom::TimeRanges* aTr,
                            const media::TimeIntervals& aTi) {
  RefPtr<dom::TimeRanges> tr = new dom::TimeRanges;
  tr->Union(aTr, 0);  // This will normalize the time range.
  EXPECT_EQ(tr->Length(), aTi.Length());
  for (dom::TimeRanges::index_type i = 0; i < tr->Length(); i++) {
    ErrorResult rv;
    EXPECT_EQ(tr->Start(i, rv), aTi[i].mStart.ToSeconds());
    EXPECT_EQ(tr->Start(i, rv), aTi.Start(i).ToSeconds());
    EXPECT_EQ(tr->End(i, rv), aTi[i].mEnd.ToSeconds());
    EXPECT_EQ(tr->End(i, rv), aTi.End(i).ToSeconds());
  }
}

TEST(IntervalSet, TimeRangesConversion)
{
  RefPtr<dom::TimeRanges> tr = new dom::TimeRanges();
  tr->Add(20, 25);
  tr->Add(40, 60);
  tr->Add(5, 10);
  tr->Add(16, 27);
  tr->Add(53, 57);
  tr->Add(45, 50);

  // explicit copy constructor and ToTimeIntervals.
  media::TimeIntervals i1(tr->ToTimeIntervals());
  CheckTimeRanges(tr, i1);

  // ctor(const TimeIntervals&)
  RefPtr<dom::TimeRanges> tr2 = new dom::TimeRanges(tr->ToTimeIntervals());
  CheckTimeRanges(tr2, i1);
}

TEST(IntervalSet, TimeRangesMicroseconds)
{
  media::TimeIntervals i0;

  i0 += media::TimeInterval(media::TimeUnit::FromMicroseconds(20),
                            media::TimeUnit::FromMicroseconds(25));
  i0 += media::TimeInterval(media::TimeUnit::FromMicroseconds(40),
                            media::TimeUnit::FromMicroseconds(60));
  i0 += media::TimeInterval(media::TimeUnit::FromMicroseconds(5),
                            media::TimeUnit::FromMicroseconds(10));

  media::TimeIntervals i1;
  i1.Add(media::TimeInterval(media::TimeUnit::FromMicroseconds(16),
                             media::TimeUnit::FromMicroseconds(27)));
  i1.Add(media::TimeInterval(media::TimeUnit::FromMicroseconds(7),
                             media::TimeUnit::FromMicroseconds(15)));
  i1.Add(media::TimeInterval(media::TimeUnit::FromMicroseconds(53),
                             media::TimeUnit::FromMicroseconds(57)));
  i1.Add(media::TimeInterval(media::TimeUnit::FromMicroseconds(45),
                             media::TimeUnit::FromMicroseconds(50)));

  media::TimeIntervals i(i0 + i1);
  RefPtr<dom::TimeRanges> tr = new dom::TimeRanges(i);
  EXPECT_EQ(tr->Length(), i.Length());
  for (dom::TimeRanges::index_type index = 0; index < tr->Length(); index++) {
    ErrorResult rv;
    EXPECT_EQ(tr->Start(index, rv), i[index].mStart.ToSeconds());
    EXPECT_EQ(tr->Start(index, rv), i.Start(index).ToSeconds());
    EXPECT_EQ(tr->End(index, rv), i[index].mEnd.ToSeconds());
    EXPECT_EQ(tr->End(index, rv), i.End(index).ToSeconds());
  }

  tr->Normalize();
  EXPECT_EQ(tr->Length(), i.Length());
  for (dom::TimeRanges::index_type index = 0; index < tr->Length(); index++) {
    ErrorResult rv;
    EXPECT_EQ(tr->Start(index, rv), i[index].mStart.ToSeconds());
    EXPECT_EQ(tr->Start(index, rv), i.Start(index).ToSeconds());
    EXPECT_EQ(tr->End(index, rv), i[index].mEnd.ToSeconds());
    EXPECT_EQ(tr->End(index, rv), i.End(index).ToSeconds());
  }

  // Check infinity values aren't lost in the conversion.
  tr = new dom::TimeRanges();
  tr->Add(0, 30);
  tr->Add(50, std::numeric_limits<double>::infinity());
  media::TimeIntervals i_oo = tr->ToTimeIntervals();
  RefPtr<dom::TimeRanges> tr2 = new dom::TimeRanges(i_oo);
  EXPECT_EQ(tr->Length(), tr2->Length());
  for (dom::TimeRanges::index_type index = 0; index < tr->Length(); index++) {
    ErrorResult rv;
    EXPECT_EQ(tr->Start(index, rv), tr2->Start(index, rv));
    EXPECT_EQ(tr->End(index, rv), tr2->End(index, rv));
  }
}

template <typename T>
class Foo {
 public:
  Foo() : mArg1(1), mArg2(2), mArg3(3) {}

  Foo(T a1, T a2, T a3) : mArg1(a1), mArg2(a2), mArg3(a3) {}

  Foo<T> operator+(const Foo<T>& aOther) const {
    Foo<T> blah;
    blah.mArg1 += aOther.mArg1;
    blah.mArg2 += aOther.mArg2;
    blah.mArg3 += aOther.mArg3;
    return blah;
  }
  Foo<T> operator-(const Foo<T>& aOther) const {
    Foo<T> blah;
    blah.mArg1 -= aOther.mArg1;
    blah.mArg2 -= aOther.mArg2;
    blah.mArg3 -= aOther.mArg3;
    return blah;
  }
  bool operator<(const Foo<T>& aOther) const { return mArg1 < aOther.mArg1; }
  bool operator==(const Foo<T>& aOther) const { return mArg1 == aOther.mArg1; }
  bool operator<=(const Foo<T>& aOther) const { return mArg1 <= aOther.mArg1; }

 private:
  int32_t mArg1;
  int32_t mArg2;
  int32_t mArg3;
};

TEST(IntervalSet, FooIntervalSet)
{
  media::Interval<Foo<int>> i(Foo<int>(), Foo<int>(4, 5, 6));
  media::IntervalSet<Foo<int>> is;
  is += i;
  is += i;
  is.Add(i);
  is = is + i;
  is = i + is;
  EXPECT_EQ(1u, is.Length());
  EXPECT_EQ(Foo<int>(), is[0].mStart);
  EXPECT_EQ(Foo<int>(4, 5, 6), is[0].mEnd);
}

TEST(IntervalSet, StaticAssert)
{
  media::Interval<int> i;

  static_assert(
      std::is_same_v<nsTArray_RelocationStrategy<IntIntervals>::Type,
                     nsTArray_RelocateUsingMoveConstructor<IntIntervals>>,
      "Must use copy constructor");
  static_assert(
      std::is_same_v<
          nsTArray_RelocationStrategy<media::TimeIntervals>::Type,
          nsTArray_RelocateUsingMoveConstructor<media::TimeIntervals>>,
      "Must use copy constructor");
}

TEST(IntervalSet, Substraction)
{
  IntIntervals i0;
  i0 += IntInterval(5, 10);
  i0 += IntInterval(20, 25);
  i0 += IntInterval(40, 60);

  IntInterval i1(8, 15);
  i0 -= i1;

  EXPECT_EQ(3u, i0.Length());
  EXPECT_EQ(5, i0[0].mStart);
  EXPECT_EQ(8, i0[0].mEnd);
  EXPECT_EQ(20, i0[1].mStart);
  EXPECT_EQ(25, i0[1].mEnd);
  EXPECT_EQ(40, i0[2].mStart);
  EXPECT_EQ(60, i0[2].mEnd);

  i0 = IntIntervals();
  i0 += IntInterval(5, 10);
  i0 += IntInterval(20, 25);
  i0 += IntInterval(40, 60);
  i1 = IntInterval(0, 60);
  i0 -= i1;
  EXPECT_TRUE(i0.IsEmpty());

  i0 = IntIntervals();
  i0 += IntInterval(5, 10);
  i0 += IntInterval(20, 25);
  i0 += IntInterval(40, 60);
  i1 = IntInterval(0, 45);
  i0 -= i1;
  EXPECT_EQ(1u, i0.Length());
  EXPECT_EQ(45, i0[0].mStart);
  EXPECT_EQ(60, i0[0].mEnd);

  i0 = IntIntervals();
  i0 += IntInterval(5, 10);
  i0 += IntInterval(20, 25);
  i0 += IntInterval(40, 60);
  i1 = IntInterval(8, 45);
  i0 -= i1;
  EXPECT_EQ(2u, i0.Length());
  EXPECT_EQ(5, i0[0].mStart);
  EXPECT_EQ(8, i0[0].mEnd);
  EXPECT_EQ(45, i0[1].mStart);
  EXPECT_EQ(60, i0[1].mEnd);

  i0 = IntIntervals();
  i0 += IntInterval(5, 10);
  i0 += IntInterval(20, 25);
  i0 += IntInterval(40, 60);
  i1 = IntInterval(8, 70);
  i0 -= i1;
  EXPECT_EQ(1u, i0.Length());
  EXPECT_EQ(5, i0[0].mStart);
  EXPECT_EQ(8, i0[0].mEnd);

  i0 = IntIntervals();
  i0 += IntInterval(0, 10);
  IntIntervals i2;
  i2 += IntInterval(4, 6);
  i0 -= i2;
  EXPECT_EQ(2u, i0.Length());
  EXPECT_EQ(0, i0[0].mStart);
  EXPECT_EQ(4, i0[0].mEnd);
  EXPECT_EQ(6, i0[1].mStart);
  EXPECT_EQ(10, i0[1].mEnd);

  i0 = IntIntervals();
  i0 += IntInterval(0, 1);
  i0 += IntInterval(3, 10);
  EXPECT_EQ(2u, i0.Length());
  // This fuzz should collapse i0 into [0,10).
  i0.SetFuzz(1);
  EXPECT_EQ(1u, i0.Length());
  EXPECT_EQ(1, i0[0].mFuzz);
  i2 = IntInterval(4, 6);
  i0 -= i2;
  EXPECT_EQ(2u, i0.Length());
  EXPECT_EQ(0, i0[0].mStart);
  EXPECT_EQ(4, i0[0].mEnd);
  EXPECT_EQ(6, i0[1].mStart);
  EXPECT_EQ(10, i0[1].mEnd);
  EXPECT_EQ(1, i0[0].mFuzz);
  EXPECT_EQ(1, i0[1].mFuzz);

  i0 = IntIntervals();
  i0 += IntInterval(0, 10);
  // [4,6) with fuzz 1 used to fail because the complementary interval set
  // [0,4)+[6,10) would collapse into [0,10).
  i2 = IntInterval(4, 6);
  i2.SetFuzz(1);
  i0 -= i2;
  EXPECT_EQ(2u, i0.Length());
  EXPECT_EQ(0, i0[0].mStart);
  EXPECT_EQ(4, i0[0].mEnd);
  EXPECT_EQ(6, i0[1].mStart);
  EXPECT_EQ(10, i0[1].mEnd);
}

TEST(IntervalSet, AddBeforeShortLastIntervalWithFuzz)
{
  IntIntervals i0;
  i0 += IntInterval(0, 5);
  i0 += IntInterval(387, 390, 1);
  i0 += IntInterval(385, 386, 5);

  EXPECT_EQ(2u, i0.Length());
  EXPECT_EQ(0, i0[0].mStart);
  EXPECT_EQ(5, i0[0].mEnd);
  EXPECT_EQ(385, i0[1].mStart);
  EXPECT_EQ(390, i0[1].mEnd);
  EXPECT_EQ(390, i0.GetEnd());
}
