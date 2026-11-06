/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LSWriteOptimizer.h"
#include "LSWriteOptimizerImpl.h"
#include "gtest/gtest.h"
#include "nsString.h"

namespace mozilla::dom {

// Minimal LSWriteOptimizer subclass to expose internals for testing.
class TestWriteOptimizer final : public LSWriteOptimizer<nsCString> {
 public:
  using LSWriteOptimizer::InsertItemInfo;
  using LSWriteOptimizerBase::mTotalDelta;
  using LSWriteOptimizerBase::mTruncateInfo;
  using LSWriteOptimizerBase::mWriteInfos;
  using LSWriteOptimizerBase::WriteInfo;
};

namespace {

const TestWriteOptimizer::InsertItemInfo* AsInsertItemInfo(
    const TestWriteOptimizer::WriteInfo* aWriteInfo) {
  return static_cast<const TestWriteOptimizer::InsertItemInfo*>(aWriteInfo);
}

}  // namespace

TEST(LSWriteOptimizer, MergeFromNonOverlappingKeys)
{
  TestWriteOptimizer newer;
  newer.InsertItem(u"a"_ns, "1"_ns, 2);

  TestWriteOptimizer older;
  older.InsertItem(u"b"_ns, "2"_ns, 3);

  newer.MergeFrom(std::move(older));

  EXPECT_TRUE(newer.mWriteInfos.Contains(u"a"_ns));
  EXPECT_TRUE(newer.mWriteInfos.Contains(u"b"_ns));
  EXPECT_EQ(newer.mTotalDelta, 5);
}

TEST(LSWriteOptimizer, MergeFromOverlappingKey)
{
  TestWriteOptimizer older;
  older.InsertItem(u"k"_ns, "OLD"_ns, 3);

  TestWriteOptimizer newer;
  newer.UpdateItem(u"k"_ns, "NEWVALUE"_ns, 5);

  newer.MergeFrom(std::move(older));

  const auto* writeInfo = newer.mWriteInfos.Get(u"k"_ns);
  ASSERT_TRUE(writeInfo);
  EXPECT_EQ(writeInfo->GetType(), TestWriteOptimizer::WriteInfo::UpdateItem);
  EXPECT_TRUE(
      AsInsertItemInfo(writeInfo)->GetValue().EqualsLiteral("NEWVALUE"));
  EXPECT_EQ(newer.mTotalDelta, 8);
}

TEST(LSWriteOptimizer, MergeFromNewerTruncate)
{
  // Newer truncate shall supersede older's write infos, but older's delta was
  // never applied to disk (its flush failed) and must still be counted.
  TestWriteOptimizer older;
  older.Truncate(-10);
  older.InsertItem(u"x"_ns, "Y"_ns, 3);

  TestWriteOptimizer newer;
  newer.Truncate(-5);

  newer.MergeFrom(std::move(older));

  // newer shall have replaced older's write infos
  EXPECT_TRUE(newer.mTruncateInfo);
  EXPECT_FALSE(newer.mWriteInfos.Contains(u"x"_ns));
  EXPECT_EQ(newer.mTotalDelta, -12);
}

TEST(LSWriteOptimizer, MergeFromOlderTruncate)
{
  TestWriteOptimizer older;
  older.Truncate(-10);
  older.InsertItem(u"x"_ns, "Y"_ns, 3);

  TestWriteOptimizer newer;
  newer.InsertItem(u"z"_ns, "W"_ns, 4);

  newer.MergeFrom(std::move(older));

  EXPECT_TRUE(newer.mTruncateInfo);
  EXPECT_TRUE(newer.mWriteInfos.Contains(u"x"_ns));
  EXPECT_TRUE(newer.mWriteInfos.Contains(u"z"_ns));
  EXPECT_EQ(newer.mTotalDelta, -3);
}

TEST(LSWriteOptimizer, MergeFromWithFlushFailure)
{
  // Mirrors the real scenario: an insert's flush fails (older), and before
  // that's discovered, the same key is updated again (newer). The merged
  // delta must reflect the net change from nothing straight to the final
  // value, not double-count the intermediate one.
  TestWriteOptimizer older;
  older.InsertItem(u"k"_ns, "A"_ns, 50);

  TestWriteOptimizer newer;
  newer.UpdateItem(u"k"_ns, "ABC"_ns, 2);

  newer.MergeFrom(std::move(older));

  const auto* writeInfo = newer.mWriteInfos.Get(u"k"_ns);
  ASSERT_TRUE(writeInfo);
  EXPECT_TRUE(AsInsertItemInfo(writeInfo)->GetValue().EqualsLiteral("ABC"));
  EXPECT_EQ(newer.mTotalDelta, 52);
}

TEST(LSWriteOptimizer, MergeFromOlderDeleteNewerInsert)
{
  // The older delete failed, so the key still exists in the database. The
  // newer insert must become an update, otherwise a later delete would be
  // optimized away and the key would never be removed from the database.
  TestWriteOptimizer older;
  older.DeleteItem(u"k"_ns, -5);

  TestWriteOptimizer newer;
  newer.InsertItem(u"k"_ns, "V"_ns, 2);

  newer.MergeFrom(std::move(older));

  const auto* writeInfo = newer.mWriteInfos.Get(u"k"_ns);
  ASSERT_TRUE(writeInfo);
  EXPECT_EQ(writeInfo->GetType(), TestWriteOptimizer::WriteInfo::UpdateItem);
  EXPECT_TRUE(AsInsertItemInfo(writeInfo)->GetValue().EqualsLiteral("V"));
  EXPECT_EQ(newer.mTotalDelta, -3);

  newer.DeleteItem(u"k"_ns, -2);

  writeInfo = newer.mWriteInfos.Get(u"k"_ns);
  ASSERT_TRUE(writeInfo);
  EXPECT_EQ(writeInfo->GetType(), TestWriteOptimizer::WriteInfo::DeleteItem);
  EXPECT_EQ(newer.mTotalDelta, -5);
}

}  // namespace mozilla::dom
