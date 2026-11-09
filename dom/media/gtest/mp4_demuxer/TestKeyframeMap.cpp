/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SampleIterator.h"
#include "gtest/gtest.h"

using mozilla::KeyframeMap;
using mozilla::Some;
using KeyframeInfo = KeyframeMap::KeyframeInfo;
using mozilla::media::TimeUnit;

constexpr int64_t BASE = 1000;

TimeUnit point(int64_t t) { return TimeUnit(t, BASE); }

KeyframeMap::Interval interval(int64_t start, int64_t end) {
  return KeyframeMap::Interval(TimeUnit(start, BASE), TimeUnit(end, BASE));
}

TEST(MP4KeyframeMap, Empty)
{
  KeyframeMap m;
  EXPECT_FALSE(m.Get(point(100)).isSome());
  EXPECT_FALSE(m.GetNearestUpTo(point(100)).isSome());
  EXPECT_FALSE(m.MinSampleTime().isSome());
  EXPECT_FALSE(m.MaxSampleTime().isSome());

  m.MarkNotKeyframe(interval(100, 120), KeyframeInfo::Container);
  EXPECT_EQ(m.MinSampleTime(), Some(point(100)));
  EXPECT_EQ(m.MaxSampleTime(), Some(point(120)));
  m.MarkNotKeyframe(interval(80, 90), KeyframeInfo::Container);
  m.MarkNotKeyframe(interval(200, 230), KeyframeInfo::Codec);
  m.MarkNotKeyframe(interval(400, 410), KeyframeInfo::Container);
  EXPECT_EQ(m.MinSampleTime(), Some(point(80)));
  EXPECT_EQ(m.MaxSampleTime(), Some(point(410)));

  EXPECT_TRUE(m.HaveChecked(point(200)));
  EXPECT_FALSE(m.HaveChecked(point(400)));

  m.Clear();
  EXPECT_FALSE(m.HaveChecked(point(200)));
  EXPECT_FALSE(m.MinSampleTime().isSome());
  EXPECT_FALSE(m.MaxSampleTime().isSome());
}

TEST(MP4KeyframeMap, UpdatePrecedence)
{
  KeyframeMap m;
  const auto i1 = interval(80, 100);
  KeyframeMap::Entry e1(i1, {0, 5}, KeyframeInfo::Container);

  m.MarkKeyframe(i1, {0, 5}, KeyframeInfo::Container);
  EXPECT_FALSE(m.HaveChecked(i1.mStart));
  EXPECT_EQ(m.Get(i1.mStart), Some(e1));
  EXPECT_EQ(m.GetNearestUpTo(point(10000)), Some(e1));
  EXPECT_EQ(m.GetNextAfter(point(10)), Some(e1));

  // Codec source should update previous Container info, including sample
  // position
  KeyframeMap::Entry e1Confirmed(i1, {1, 7}, KeyframeInfo::Codec);
  m.MarkKeyframe(i1, {1, 7}, KeyframeInfo::Codec);
  m.MarkKeyframe(i1, {2, 8}, KeyframeInfo::Container);
  // Container source can not erase a KF marked by Codec
  m.MarkNotKeyframe(i1, KeyframeInfo::Container);
  EXPECT_TRUE(m.HaveChecked(i1.mStart));
  EXPECT_TRUE(m.HaveChecked(i1.mStart + point(10)));
  EXPECT_TRUE(m.HaveChecked(i1.mEnd - point(1)));
  EXPECT_FALSE(m.HaveChecked(i1.mStart - point(1)));
  EXPECT_FALSE(m.HaveChecked(i1.mEnd));
  EXPECT_FALSE(m.HaveChecked(point(1)));

  const auto i2 = interval(200, 220);
  m.MarkKeyframe(i2, {1, 10}, KeyframeInfo::Container);
  KeyframeMap::Entry e2(i2, {1, 10}, KeyframeInfo::Container);
  EXPECT_FALSE(m.GetNearestUpTo(point(10)).isSome());
  EXPECT_EQ(m.GetNearestUpTo(i1.mStart), Some(e1Confirmed));
  EXPECT_EQ(m.GetNearestUpTo(i1.mStart + point(10)), Some(e1Confirmed));

  EXPECT_EQ(m.GetNextAfter(i1.mStart - point(1)), Some(e1Confirmed));
  EXPECT_EQ(m.GetNextAfter(i1.mStart - point(1), {0, 4}), Some(e1Confirmed));
  EXPECT_EQ(m.GetNextAfter(i1.mStart - point(1), {1, 8}), Some(e2));
  EXPECT_EQ(m.GetNextAfter(i1.mEnd), Some(e2));
  EXPECT_EQ(m.GetNextAfter(i1.mEnd, {0, 0}), Some(e2));
  EXPECT_FALSE(m.GetNextAfter(i1.mEnd, {100, 0}).isSome());

  EXPECT_EQ(m.GetNextAfter(point(120)), Some(e2));
  EXPECT_EQ(m.GetNearestUpTo(point(300)), Some(e2));
  EXPECT_EQ(m.GetNearestUpTo(point(220)), Some(e2));

  m.MarkNotKeyframe(i2, KeyframeInfo::Codec);
  // Container source can not override a non-KF from Codec
  m.MarkKeyframe(i2, {0, 1}, KeyframeInfo::Container);
  EXPECT_FALSE(m.GetNextAfter(point(120)).isSome());
  EXPECT_EQ(m.GetNearestUpTo(point(300)), Some(e1Confirmed));

  m.Clear();
  EXPECT_FALSE(m.GetNearestUpTo(point(300)).isSome());
  EXPECT_FALSE(m.GetNextAfter(point(120)).isSome());
  EXPECT_FALSE(m.HaveChecked(i1.mStart));
  EXPECT_FALSE(m.HaveChecked(i1.mStart + point(10)));
  EXPECT_FALSE(m.HaveChecked(i2.mStart));
}
