/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CacheIndexIterator.h"

#include "CacheIndex.h"
#include "CacheLog.h"
#include "mozilla/DebugOnly.h"
#include "nsString.h"

namespace mozilla::net {

CacheIndexIterator::CacheIndexIterator(CacheIndex* aIndex, bool aAddNew)
    : mStatus(NS_OK), mIndex(aIndex), mAddNew(aAddNew) {
  LOG(("CacheIndexIterator::CacheIndexIterator() [this=%p]", this));
}

CacheIndexIterator::~CacheIndexIterator() {
  LOG(("CacheIndexIterator::~CacheIndexIterator() [this=%p]", this));
}

void CacheIndexIterator::Destroy() {
  {
    StaticMutexAutoLock lock(CacheIndex::sLock);
    ClearRecords(lock);
    CloseInternal(NS_ERROR_NOT_AVAILABLE);
  }
  delete this;
}

nsresult CacheIndexIterator::GetNextHash(SHA1Sum::Hash* aHash) {
  LOG(("CacheIndexIterator::GetNextHash() [this=%p]", this));

  StaticMutexAutoLock lock(CacheIndex::sLock);

  if (NS_FAILED(mStatus)) {
    return mStatus;
  }

  if (mRecords.IsEmpty()) {
    CloseInternal(NS_ERROR_NOT_AVAILABLE);
    return mStatus;
  }

  CacheIndexRecordWrapper* record = *mRecords.begin();
  memcpy(aHash, record->Get()->mHash, sizeof(SHA1Sum::Hash));
  mRecords.Remove(record);

  return NS_OK;
}

nsresult CacheIndexIterator::Close() {
  LOG(("CacheIndexIterator::Close() [this=%p]", this));

  StaticMutexAutoLock lock(CacheIndex::sLock);

  return CloseInternal(NS_ERROR_NOT_AVAILABLE);
}

nsresult CacheIndexIterator::CloseInternal(nsresult aStatus) {
  LOG(("CacheIndexIterator::CloseInternal() [this=%p, status=0x%08" PRIx32 "]",
       this, static_cast<uint32_t>(aStatus)));

  // Make sure status will be a failure
  MOZ_ASSERT(NS_FAILED(aStatus));
  if (NS_SUCCEEDED(aStatus)) {
    aStatus = NS_ERROR_UNEXPECTED;
  }

  if (NS_FAILED(mStatus)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  CacheIndex::sLock.AssertCurrentThreadOwns();
  DebugOnly<bool> removed = mIndex->mIterators.RemoveElement(this);
  MOZ_ASSERT(removed);
  mStatus = aStatus;

  return NS_OK;
}

void CacheIndexIterator::ClearRecords(const StaticMutexAutoLock& aProofOfLock) {
  mRecords.Clear();
}

void CacheIndexIterator::AddRecord(CacheIndexRecordWrapper* aRecord,
                                   const StaticMutexAutoLock& aProofOfLock) {
  LOG(("CacheIndexIterator::AddRecord() [this=%p, record=%p]", this, aRecord));

  mRecords.Insert(aRecord);
}

bool CacheIndexIterator::RemoveRecord(CacheIndexRecordWrapper* aRecord,
                                      const StaticMutexAutoLock& aProofOfLock) {
  LOG(("CacheIndexIterator::RemoveRecord() [this=%p, record=%p]", this,
       aRecord));

  return mRecords.EnsureRemoved(aRecord);
}

bool CacheIndexIterator::ReplaceRecord(
    CacheIndexRecordWrapper* aOldRecord, CacheIndexRecordWrapper* aNewRecord,
    const StaticMutexAutoLock& aProofOfLock) {
  LOG(
      ("CacheIndexIterator::ReplaceRecord() [this=%p, oldRecord=%p, "
       "newRecord=%p]",
       this, aOldRecord, aNewRecord));

  if (RemoveRecord(aOldRecord, aProofOfLock)) {
    AddRecord(aNewRecord, aProofOfLock);
    return true;
  }

  return false;
}

}  // namespace mozilla::net
