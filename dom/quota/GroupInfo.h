/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_QUOTA_GROUPINFO_H_
#define DOM_QUOTA_GROUPINFO_H_

#include "OriginInfo.h"
#include "mozilla/NotNull.h"
#include "mozilla/dom/quota/Assertions.h"
#include "mozilla/dom/quota/PersistenceType.h"
#include "nsISupportsImpl.h"
#include "nsTArray.h"

namespace mozilla::dom::quota {

class GroupInfoPair;
class OriginInfo;
class DirtyTrackingAutoLock;

class GroupInfo final {
  friend class CanonicalQuotaObject;
  friend class DirtyTrackingAutoLock;
  friend class GroupInfoPair;
  friend class OriginInfo;
  friend class QuotaManager;

 public:
  GroupInfo(GroupInfoPair* aGroupInfoPair, PersistenceType aPersistenceType)
      : mGroupInfoPair(aGroupInfoPair),
        mPersistenceType(aPersistenceType),
        mUsage(0) {
    MOZ_ASSERT(aPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

    MOZ_COUNT_CTOR(GroupInfo);
  }

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GroupInfo)

  PersistenceType GetPersistenceType() const { return mPersistenceType; }

  const nsCString& GetGroup() const;

 private:
  // Private destructor, to discourage deletion outside of Release():
  MOZ_COUNTED_DTOR(GroupInfo)

  already_AddRefed<OriginInfo> LockedGetOriginInfo(const nsACString& aOrigin);

  void LockedAddOriginInfo(NotNull<RefPtr<OriginInfo>>&& aOriginInfo);

  void LockedAdjustUsageForRemovedOriginInfo(const OriginInfo& aOriginInfo);

  void LockedRemoveOriginInfo(const nsACString& aOrigin);

  void LockedRemoveOriginInfos();

  bool LockedHasOriginInfos() {
    AssertCurrentThreadOwnsQuotaMutex();

    return !mOriginInfos.IsEmpty();
  }

  nsTArray<NotNull<RefPtr<OriginInfo>>> mOriginInfos;

  GroupInfoPair* mGroupInfoPair;
  PersistenceType mPersistenceType;

  // Signed for the same reason as QuotaManager::mTemporaryStorageUsage: though
  // it shouldn't go negative, in case that happens (transient state or bug
  // elsewhere) don't let the value underflow, which would result in a huge
  // number, making all integers comparisons to become buggy. See bug 2066923
  // and bug 1585978 for details.
  int64_t mUsage;
};

}  // namespace mozilla::dom::quota

#endif  // DOM_QUOTA_GROUPINFO_H_
