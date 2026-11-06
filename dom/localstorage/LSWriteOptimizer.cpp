/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LSWriteOptimizer.h"

#include <utility>

#include "nsBaseHashtable.h"
#include "nsTArray.h"

namespace mozilla::dom {

LSWriteOptimizerBase::LSWriteOptimizerBase()
    : mLastSerialNumber(0), mTotalDelta(0) {}

LSWriteOptimizerBase::LSWriteOptimizerBase(
    LSWriteOptimizerBase&& aWriteOptimizer)
    : mTruncateInfo(std::move(aWriteOptimizer.mTruncateInfo)),
      mLastSerialNumber(aWriteOptimizer.mLastSerialNumber) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(&aWriteOptimizer != this);

  mWriteInfos.SwapElements(aWriteOptimizer.mWriteInfos);
  mTotalDelta = aWriteOptimizer.mTotalDelta;
  aWriteOptimizer.mTotalDelta = 0;
}

class LSWriteOptimizerBase::WriteInfoComparator {
 public:
  bool Equals(const WriteInfo* a, const WriteInfo* b) const {
    MOZ_ASSERT(a && b);
    return a->SerialNumber() == b->SerialNumber();
  }

  bool LessThan(const WriteInfo* a, const WriteInfo* b) const {
    MOZ_ASSERT(a && b);
    return a->SerialNumber() < b->SerialNumber();
  }
};

void LSWriteOptimizerBase::DeleteItem(const nsAString& aKey, int64_t aDelta) {
  AssertIsOnOwningThread();

  mWriteInfos.WithEntryHandle(aKey, [&](auto&& entry) {
    if (entry && entry.Data()->GetType() == WriteInfo::InsertItem) {
      entry.Remove();
    } else {
      entry.InsertOrUpdate(
          std::make_unique<DeleteItemInfo>(NextSerialNumber(), aKey));
    }
  });

  mTotalDelta += aDelta;
}

void LSWriteOptimizerBase::Truncate(int64_t aDelta) {
  AssertIsOnOwningThread();

  mWriteInfos.Clear();

  if (!mTruncateInfo) {
    mTruncateInfo = std::make_unique<TruncateInfo>(NextSerialNumber());
  }

  mTotalDelta += aDelta;
}

void LSWriteOptimizerBase::Reset() {
  AssertIsOnOwningThread();

  mTruncateInfo = nullptr;
  mWriteInfos.Clear();
}

void LSWriteOptimizerBase::GetSortedWriteInfos(
    nsTArray<NotNull<WriteInfo*>>& aWriteInfos) {
  AssertIsOnOwningThread();

  // Appending and sorting once is O(n log n); inserting into the sorted
  // position one by one has to shift the tail on every insertion, which is
  // O(n^2) and dominates checkpointing a large number of writes.

  aWriteInfos.SetCapacity(mWriteInfos.Count() + (mTruncateInfo ? 1 : 0));

  if (mTruncateInfo) {
    aWriteInfos.AppendElement(WrapNotNullUnchecked(mTruncateInfo.get()));
  }

  for (const auto& entry : mWriteInfos) {
    aWriteInfos.AppendElement(WrapNotNull(entry.GetWeak()));
  }

  // Serial numbers are unique, so this is a total order and the result is
  // identical regardless of the sort's stability.
  aWriteInfos.Sort(WriteInfoComparator());

#ifdef DEBUG
  for (uint32_t index = 1; index < aWriteInfos.Length(); index++) {
    MOZ_ASSERT(aWriteInfos[index - 1]->SerialNumber() <
               aWriteInfos[index]->SerialNumber());
  }
#endif
}

}  // namespace mozilla::dom
