/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GroupInfo.h"

#include "GroupInfoPair.h"
#include "OriginInfo.h"
#include "mozilla/dom/quota/AssertionsImpl.h"

namespace mozilla::dom::quota {

already_AddRefed<OriginInfo> GroupInfo::LockedGetOriginInfo(
    const nsACString& aOrigin) {
  AssertCurrentThreadOwnsQuotaMutex();

  for (const auto& originInfo : mOriginInfos) {
    if (originInfo->mOrigin == aOrigin) {
      RefPtr<OriginInfo> result = originInfo;
      return result.forget();
    }
  }

  return nullptr;
}

const nsCString& GroupInfo::GetGroup() const {
  MOZ_ASSERT(mGroupInfoPair);
  return mGroupInfoPair->Group();
}

void GroupInfo::LockedAddOriginInfo(NotNull<RefPtr<OriginInfo>>&& aOriginInfo) {
  AssertCurrentThreadOwnsQuotaMutex();

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  const int64_t usage = aOriginInfo->LockedUsage();
  const bool persisted = aOriginInfo->LockedPersisted();

  auto foundIndex = mOriginInfos.IndexOf(aOriginInfo);

  if (decltype(mOriginInfos)::NoIndex != foundIndex) {
    const auto& oldOriginInfo = mOriginInfos[foundIndex];
    const int64_t oldUsage = oldOriginInfo->LockedUsage();

    // The persisted flag may differ between old and new, so subtract and
    // re-add mUsage conditionally rather than using a simple delta.
    if (!oldOriginInfo->LockedPersisted()) {
      mUsage -= oldUsage;
      QM_ASSERT_NOT_NEGATIVE(mUsage);
    }

    quotaManager->mTemporaryStorageUsage -= oldUsage;
    QM_ASSERT_NOT_NEGATIVE(quotaManager->mTemporaryStorageUsage);
    quotaManager->LockedMaybeRecordTemporaryStorageUsageUnderflow(__func__);

    mOriginInfos[foundIndex] = std::move(aOriginInfo);
  } else {
    mOriginInfos.AppendElement(std::move(aOriginInfo));
  }

  if (!persisted) {
    AssertNoOverflow(mUsage, usage);
    mUsage += usage;
  }

  AssertNoOverflow(quotaManager->mTemporaryStorageUsage, usage);
  quotaManager->mTemporaryStorageUsage += usage;
}

void GroupInfo::LockedAdjustUsageForRemovedOriginInfo(
    const OriginInfo& aOriginInfo) {
  const int64_t usage = aOriginInfo.LockedUsage();

  if (!aOriginInfo.LockedPersisted()) {
    mUsage -= usage;
    QM_ASSERT_NOT_NEGATIVE(mUsage);
  }

  QuotaManager* const quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  quotaManager->mTemporaryStorageUsage -= usage;
  QM_ASSERT_NOT_NEGATIVE(quotaManager->mTemporaryStorageUsage);
  quotaManager->LockedMaybeRecordTemporaryStorageUsageUnderflow(__func__);
}

void GroupInfo::LockedRemoveOriginInfo(const nsACString& aOrigin) {
  AssertCurrentThreadOwnsQuotaMutex();

  const auto foundIt = std::find_if(mOriginInfos.cbegin(), mOriginInfos.cend(),
                                    [&aOrigin](const auto& originInfo) {
                                      return originInfo->mOrigin == aOrigin;
                                    });

  // XXX Or can we MOZ_ASSERT(foundIt != mOriginInfos.cend()) ?
  if (foundIt != mOriginInfos.cend()) {
    LockedAdjustUsageForRemovedOriginInfo(**foundIt);

    // The OriginInfo may still be referenced by mDirtyOriginInfos (which
    // holds RefPtr<OriginInfo>). Nulling mGroupInfo lets
    // FlushDirtyOriginInfos detect and skip it via its existing null check.
    // The dirty flag persists on disk, so the origin will be rescanned on
    // next initialization.
    foundIt->get()->mGroupInfo = nullptr;
    mOriginInfos.RemoveElementAt(foundIt);
  }
}

void GroupInfo::LockedRemoveOriginInfos() {
  AssertCurrentThreadOwnsQuotaMutex();

  for (const auto& originInfo : std::exchange(mOriginInfos, {})) {
    LockedAdjustUsageForRemovedOriginInfo(*originInfo);
    originInfo->mGroupInfo = nullptr;
  }
}

}  // namespace mozilla::dom::quota
