/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_localstorage_LSWriteOptimizerImpl_h
#define mozilla_dom_localstorage_LSWriteOptimizerImpl_h

#include <memory>
#include <utility>

#include "LSWriteOptimizer.h"
#include "mozilla/UniquePtr.h"
#include "nsString.h"
#include "nsTArray.h"

namespace mozilla::dom {

template <typename T, typename U>
void LSWriteOptimizer<T, U>::InsertItem(const nsAString& aKey, const T& aValue,
                                        int64_t aDelta) {
  AssertIsOnOwningThread();

  mWriteInfos.WithEntryHandle(aKey, [&](auto&& entry) {
    if (entry && entry.Data()->GetType() == WriteInfo::DeleteItem) {
      // We could just simply replace the deletion with ordinary update, but
      // that would preserve item's original position/index. Imagine a case when
      // we have only one existing key k1. Now let's create a new optimizer and
      // remove k1, add k2 and add k1 back. The final order should be k2, k1
      // (ordinary update would produce k1, k2). So we need to differentiate
      // between normal update and "optimized" update which resulted from a
      // deletion followed by an insertion. We use the UpdateWithMove flag for
      // this.

      entry.Update(
          std::make_unique<UpdateItemInfo>(NextSerialNumber(), aKey, aValue,
                                           /* aUpdateWithMove */ true));
    } else {
      entry.InsertOrUpdate(
          std::make_unique<InsertItemInfo>(NextSerialNumber(), aKey, aValue));
    }
  });

  mTotalDelta += aDelta;
}

template <typename T, typename U>
void LSWriteOptimizer<T, U>::UpdateItem(const nsAString& aKey, const T& aValue,
                                        int64_t aDelta) {
  AssertIsOnOwningThread();

  mWriteInfos.WithEntryHandle(aKey, [&](auto&& entry) {
    if (entry && entry.Data()->GetType() == WriteInfo::InsertItem) {
      entry.Update(
          std::make_unique<InsertItemInfo>(NextSerialNumber(), aKey, aValue));
    } else {
      entry.InsertOrUpdate(
          std::make_unique<UpdateItemInfo>(NextSerialNumber(), aKey, aValue,
                                           /* aUpdateWithMove */ false));
    }
  });

  mTotalDelta += aDelta;
}

template <typename T, typename U>
void LSWriteOptimizer<T, U>::MergeFrom(LSWriteOptimizer&& aOlder) {
  AssertIsOnOwningThread();

  mTotalDelta += aOlder.mTotalDelta;

  if (mTruncateInfo) {
    return;
  }

  if (aOlder.mTruncateInfo) {
    mTruncateInfo = std::move(aOlder.mTruncateInfo);
  }

  for (auto& olderEntry : aOlder.mWriteInfos) {
    UniquePtr<WriteInfo>& olderWriteInfo = *olderEntry.GetModifiableData();
    const nsAString& key = olderEntry.GetKey();

    mWriteInfos.WithEntryHandle(key, [&](auto&& newerEntry) {
      if (!newerEntry) {
        newerEntry.Insert(std::move(olderWriteInfo));
        return;
      }

      UniquePtr<WriteInfo>& newerWriteInfo = *newerEntry;
      if (olderWriteInfo->GetType() == WriteInfo::DeleteItem &&
          newerWriteInfo->GetType() == WriteInfo::InsertItem) {
        // The older delete failed, so the key still exists in the database. The
        // newer insert must become an update, otherwise a later delete would be
        // optimized away (see LSWriteOptimizerBase::DeleteItem) and the key
        // would never be removed from the database.
        const InsertItemInfo* insertItemInfo =
            static_cast<const InsertItemInfo*>(newerWriteInfo.get());
        newerWriteInfo = std::make_unique<UpdateItemInfo>(
            newerWriteInfo->SerialNumber(), key, insertItemInfo->GetValue(),
            /* aUpdateWithMove */ true);
      }
    });
  }

  aOlder.mWriteInfos.Clear();
}

}  // namespace mozilla::dom

#endif  // mozilla_dom_localstorage_LSWriteOptimizerImpl_h
