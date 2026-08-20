/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CacheIndex_h_
#define CacheIndex_h_

#include "CacheFileIOManager.h"
#include "CacheHashUtils.h"
#include "CacheLog.h"
#include "gtest/MozGtestFriend.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/SHA1.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "nsICacheEntry.h"
#include "nsICacheStorageService.h"
#include "nsILoadContextInfo.h"
#include "nsIRunnable.h"
#include "nsIWeakReferenceUtils.h"
#include "nsTHashtable.h"
#include "nsThreadUtils.h"

class nsIFile;
class nsIDirectoryEnumerator;
class nsITimer;

#ifdef DEBUG
#  define DEBUG_STATS 1
#endif

namespace mozilla {
namespace net {

class CacheFileMetadata;
class FileOpenHelper;
class CacheIndexIterator;

using CacheIndexHeader = struct {
  // Version of the index. The index must be ignored and deleted when the file
  // on disk was written with a newer version.
  uint32_t mVersion;

  // Timestamp of time when the last successful write of the index started.
  // During update process we use this timestamp for a quick validation of entry
  // files. If last modified time of the file is lower than this timestamp, we
  // skip parsing of such file since the information in index should be up to
  // date.
  uint32_t mTimeStamp;

  // We set this flag as soon as possible after parsing index during startup
  // and clean it after we write journal to disk during shutdown. We ignore the
  // journal and start update process whenever this flag is set during index
  // parsing.
  uint32_t mIsDirty;

  // The amount of data written to the cache. When it reaches
  // kTelemetryReportBytesLimit a telemetry report is sent and the counter is
  // reset.
  uint32_t mKBWritten;

  // Whether the entries on disk are encrypted at rest (see
  // browser.cache.disk.encryption.enabled). Stored so that a change to the
  // encryption setting can be detected at startup: when it no longer matches
  // the pref the whole cache is purged, since it would otherwise hold a mix of
  // encrypted and plaintext entries.
  uint32_t mIsEncrypted;
};

static_assert(sizeof(CacheIndexHeader::mVersion) +
                      sizeof(CacheIndexHeader::mTimeStamp) +
                      sizeof(CacheIndexHeader::mIsDirty) +
                      sizeof(CacheIndexHeader::mKBWritten) +
                      sizeof(CacheIndexHeader::mIsEncrypted) ==
                  sizeof(CacheIndexHeader),
              "Unexpected sizeof(CacheIndexHeader)!");

#pragma pack(push, 1)
struct CacheIndexRecord {
  SHA1Sum::Hash mHash{};
  uint32_t mFrecency{0};
  OriginAttrsHash mOriginAttrsHash{0};
  uint32_t mLastFetched{0};
  uint32_t mFetchCount{0};
  uint8_t mContentType{nsICacheEntry::CONTENT_TYPE_UNKNOWN};

  /*
   *    1000 0000 0000 0000 0000 0000 0000 0000 : initialized
   *    0100 0000 0000 0000 0000 0000 0000 0000 : anonymous
   *    0010 0000 0000 0000 0000 0000 0000 0000 : removed
   *    0001 0000 0000 0000 0000 0000 0000 0000 : dirty
   *    0000 1000 0000 0000 0000 0000 0000 0000 : fresh
   *    0000 0100 0000 0000 0000 0000 0000 0000 : pinned
   *    0000 0010 0000 0000 0000 0000 0000 0000 : has cached alt data
   *    0000 0001 0000 0000 0000 0000 0000 0000 : is a dictionary
   *    0000 0000 1111 1111 1111 1111 1111 1111 : file size (in kB)
   *    Max file size is 16GiB
   */
  uint32_t mFlags{0};

  CacheIndexRecord() = default;
};
#pragma pack(pop)

// For Compression Dictionaries, we make special entries in the cache for
// each origin with a dictionary.  In the data or metadata for each of
// these entries, we store the hashes of the dictionary, the match value
// (required), the match-dest value (optional), the id (optional) and the
// type (optional).

// We mark an entry if it's a dictionary (use-as-dictionary); if it is,
// when the entry is removed, we remove it from the origin's dictionary
// entry.  If the origin's dictionary list is empty, we remove the origin.

static_assert(sizeof(CacheIndexRecord::mHash) +
                      sizeof(CacheIndexRecord::mFrecency) +
                      sizeof(CacheIndexRecord::mOriginAttrsHash) +
                      sizeof(CacheIndexRecord::mLastFetched) +
                      sizeof(CacheIndexRecord::mFetchCount) +
                      sizeof(CacheIndexRecord::mContentType) +
                      sizeof(CacheIndexRecord::mFlags) ==
                  sizeof(CacheIndexRecord),
              "Unexpected sizeof(CacheIndexRecord)!");

class CacheIndexRecordWrapper final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DESTROY(
      CacheIndexRecordWrapper, DispatchDeleteSelfToCurrentThread());

  CacheIndexRecordWrapper() : mRec(MakeUnique<CacheIndexRecord>()) {}
  CacheIndexRecord* Get() { return mRec.get(); }

 private:
  ~CacheIndexRecordWrapper();
  void DispatchDeleteSelfToCurrentThread();
  UniquePtr<CacheIndexRecord> mRec;
  friend class DeleteCacheIndexRecordWrapper;
};

class CacheIndexEntry : public PLDHashEntryHdr {
 public:
  using KeyType = const SHA1Sum::Hash&;
  using KeyTypePointer = const SHA1Sum::Hash*;

  explicit CacheIndexEntry(KeyTypePointer aKey) {
    MOZ_COUNT_CTOR(CacheIndexEntry);
    mRec = new CacheIndexRecordWrapper();
    LOG(("CacheIndexEntry::CacheIndexEntry() - Created record [rec=%p]",
         mRec->Get()));
    memcpy(&mRec->Get()->mHash, aKey, sizeof(SHA1Sum::Hash));
  }
  CacheIndexEntry(const CacheIndexEntry& aOther) {
    MOZ_ASSERT_UNREACHABLE("CacheIndexEntry copy constructor is forbidden!");
  }
  ~CacheIndexEntry() {
    MOZ_COUNT_DTOR(CacheIndexEntry);
    LOG(("CacheIndexEntry::~CacheIndexEntry() - Deleting record [rec=%p]",
         mRec->Get()));
  }

  // KeyEquals(): does this entry match this key?
  bool KeyEquals(KeyTypePointer aKey) const {
    return memcmp(&mRec->Get()->mHash, aKey, sizeof(SHA1Sum::Hash)) == 0;
  }

  // KeyToPointer(): Convert KeyType to KeyTypePointer
  static KeyTypePointer KeyToPointer(KeyType aKey) { return &aKey; }

  // HashKey(): calculate the hash number
  static PLDHashNumber HashKey(KeyTypePointer aKey) {
    return (reinterpret_cast<const uint32_t*>(aKey))[0];
  }

  // ALLOW_MEMMOVE can we move this class with memmove(), or do we have
  // to use the copy constructor?
  enum { ALLOW_MEMMOVE = true };

  bool operator==(const CacheIndexEntry& aOther) const {
    return KeyEquals(&aOther.mRec->Get()->mHash);
  }

  CacheIndexEntry& operator=(const CacheIndexEntry& aOther) {
    MOZ_ASSERT(memcmp(&mRec->Get()->mHash, &aOther.mRec->Get()->mHash,
                      sizeof(SHA1Sum::Hash)) == 0);
    mRec->Get()->mFrecency = aOther.mRec->Get()->mFrecency;
    mRec->Get()->mOriginAttrsHash = aOther.mRec->Get()->mOriginAttrsHash;
    mRec->Get()->mLastFetched = aOther.mRec->Get()->mLastFetched;
    mRec->Get()->mFetchCount = aOther.mRec->Get()->mFetchCount;
    mRec->Get()->mContentType = aOther.mRec->Get()->mContentType;
    mRec->Get()->mFlags = aOther.mRec->Get()->mFlags;
    return *this;
  }

  void InitNew() {
    mRec->Get()->mFrecency = 0;
    mRec->Get()->mOriginAttrsHash = 0;
    mRec->Get()->mLastFetched = 0;
    mRec->Get()->mFetchCount = 0;
    mRec->Get()->mContentType = nsICacheEntry::CONTENT_TYPE_UNKNOWN;
    mRec->Get()->mFlags = 0;
  }

  void Init(OriginAttrsHash aOriginAttrsHash, bool aAnonymous, bool aPinned) {
    MOZ_ASSERT(mRec->Get()->mFrecency == 0);
    MOZ_ASSERT(mRec->Get()->mOriginAttrsHash == 0);
    MOZ_ASSERT(mRec->Get()->mLastFetched == 0);
    MOZ_ASSERT(mRec->Get()->mFetchCount == 0);
    MOZ_ASSERT(mRec->Get()->mContentType ==
               nsICacheEntry::CONTENT_TYPE_UNKNOWN);
    // When we init the entry it must be fresh and may be dirty
    MOZ_ASSERT((mRec->Get()->mFlags & ~kDirtyMask) == kFreshMask);

    mRec->Get()->mOriginAttrsHash = aOriginAttrsHash;
    mRec->Get()->mFlags |= kInitializedMask;
    if (aAnonymous) {
      mRec->Get()->mFlags |= kAnonymousMask;
    }
    if (aPinned) {
      mRec->Get()->mFlags |= kPinnedMask;
    }
  }

  const SHA1Sum::Hash* Hash() const { return &mRec->Get()->mHash; }

  bool IsInitialized() const {
    return !!(mRec->Get()->mFlags & kInitializedMask);
  }

  mozilla::net::OriginAttrsHash OriginAttrsHash() const {
    return mRec->Get()->mOriginAttrsHash;
  }

  bool Anonymous() const { return !!(mRec->Get()->mFlags & kAnonymousMask); }

  bool IsRemoved() const { return !!(mRec->Get()->mFlags & kRemovedMask); }
  void MarkRemoved() { mRec->Get()->mFlags |= kRemovedMask; }

  bool IsDirty() const { return !!(mRec->Get()->mFlags & kDirtyMask); }
  void MarkDirty() { mRec->Get()->mFlags |= kDirtyMask; }
  void ClearDirty() { mRec->Get()->mFlags &= ~kDirtyMask; }

  bool IsFresh() const { return !!(mRec->Get()->mFlags & kFreshMask); }
  void MarkFresh() { mRec->Get()->mFlags |= kFreshMask; }

  bool IsPinned() const { return !!(mRec->Get()->mFlags & kPinnedMask); }

  void SetFrecency(uint32_t aFrecency) { mRec->Get()->mFrecency = aFrecency; }
  uint32_t GetFrecency() const { return mRec->Get()->mFrecency; }

  void SetHasAltData(bool aHasAltData) {
    aHasAltData ? mRec->Get()->mFlags |= kHasAltDataMask
                : mRec->Get()->mFlags &= ~kHasAltDataMask;
  }
  bool GetHasAltData() const {
    return !!(mRec->Get()->mFlags & kHasAltDataMask);
  }

  void SetHasNoVarySearch(bool aVal) {
    aVal ? mRec->Get()->mFlags |= kHasNoVarySearchMask
         : mRec->Get()->mFlags &= ~kHasNoVarySearchMask;
  }
  bool HasNoVarySearch() const {
    return !!(mRec->Get()->mFlags & kHasNoVarySearchMask);
  }

  void SetLastFetched(uint32_t aLastFetched) {
    mRec->Get()->mLastFetched = aLastFetched;
  }
  uint32_t GetLastFetched() const { return mRec->Get()->mLastFetched; }

  void SetFetchCount(uint32_t aFetchCount) {
    mRec->Get()->mFetchCount = aFetchCount;
  }
  uint32_t GetFetchCount() const { return mRec->Get()->mFetchCount; }

  void SetContentType(uint8_t aType) { mRec->Get()->mContentType = aType; }
  uint8_t GetContentType() const { return GetContentType(mRec->Get()); }
  static uint8_t GetContentType(CacheIndexRecord* aRec) {
    if (aRec->mContentType >= nsICacheEntry::CONTENT_TYPE_LAST) {
      LOG(
          ("CacheIndexEntry::GetContentType() - Found invalid content type "
           "[hash=%08x%08x%08x%08x%08x, contentType=%u]",
           LOGSHA1(aRec->mHash), aRec->mContentType));
      return nsICacheEntry::CONTENT_TYPE_UNKNOWN;
    }
    return aRec->mContentType;
  }

  // Sets filesize in kilobytes.
  void SetFileSize(uint32_t aFileSize) {
    if (aFileSize > kFileSizeMask) {
      LOG(
          ("CacheIndexEntry::SetFileSize() - FileSize is too large, "
           "truncating to %u",
           kFileSizeMask));
      aFileSize = kFileSizeMask;
    }
    mRec->Get()->mFlags &= ~kFileSizeMask;
    mRec->Get()->mFlags |= aFileSize;
  }
  // Returns filesize in kilobytes.
  uint32_t GetFileSize() const { return GetFileSize(*(mRec->Get())); }
  static uint32_t GetFileSize(const CacheIndexRecord& aRec) {
    return aRec.mFlags & kFileSizeMask;
  }
  static uint32_t IsPinned(CacheIndexRecord* aRec) {
    return aRec->mFlags & kPinnedMask;
  }
  bool IsFileEmpty() const { return GetFileSize() == 0; }

  void WriteToBuf(void* aBuf) {
    uint8_t* ptr = static_cast<uint8_t*>(aBuf);
    memcpy(ptr, mRec->Get()->mHash, sizeof(SHA1Sum::Hash));
    ptr += sizeof(SHA1Sum::Hash);
    NetworkEndian::writeUint32(ptr, mRec->Get()->mFrecency);
    ptr += sizeof(uint32_t);
    NetworkEndian::writeUint64(ptr, mRec->Get()->mOriginAttrsHash);
    ptr += sizeof(uint64_t);
    NetworkEndian::writeUint32(ptr, mRec->Get()->mLastFetched);
    ptr += sizeof(uint32_t);
    NetworkEndian::writeUint32(ptr, mRec->Get()->mFetchCount);
    ptr += sizeof(uint32_t);
    *ptr = mRec->Get()->mContentType;
    ptr += sizeof(uint8_t);
    // Dirty and fresh flags should never go to disk, since they make sense only
    // during current session.
    NetworkEndian::writeUint32(
        ptr, mRec->Get()->mFlags & ~(kDirtyMask | kFreshMask));
  }

  void ReadFromBuf(void* aBuf) {
    const uint8_t* ptr = static_cast<const uint8_t*>(aBuf);
    MOZ_ASSERT(memcmp(&mRec->Get()->mHash, ptr, sizeof(SHA1Sum::Hash)) == 0);
    ptr += sizeof(SHA1Sum::Hash);
    mRec->Get()->mFrecency = NetworkEndian::readUint32(ptr);
    ptr += sizeof(uint32_t);
    mRec->Get()->mOriginAttrsHash = NetworkEndian::readUint64(ptr);
    ptr += sizeof(uint64_t);
    mRec->Get()->mLastFetched = NetworkEndian::readUint32(ptr);
    ptr += sizeof(uint32_t);
    mRec->Get()->mFetchCount = NetworkEndian::readUint32(ptr);
    ptr += sizeof(uint32_t);
    mRec->Get()->mContentType = *ptr;
    ptr += sizeof(uint8_t);
    mRec->Get()->mFlags = NetworkEndian::readUint32(ptr);
  }

  void Log() const {
    LOG(
        ("CacheIndexEntry::Log() [this=%p, hash=%08x%08x%08x%08x%08x, fresh=%u,"
         " initialized=%u, removed=%u, dirty=%u, anonymous=%u, "
         "originAttrsHash=%" PRIx64 ", frecency=%u, hasAltData=%u, "
         "lastFetched=%u, fetchCount=%u, contentType=%u, size=%u]",
         this, LOGSHA1(mRec->Get()->mHash), IsFresh(), IsInitialized(),
         IsRemoved(), IsDirty(), Anonymous(), OriginAttrsHash(), GetFrecency(),
         GetHasAltData(), GetLastFetched(), GetFetchCount(), GetContentType(),
         GetFileSize()));
  }

  static bool RecordMatchesLoadContextInfo(CacheIndexRecordWrapper* aRec,
                                           nsILoadContextInfo* aInfo) {
    MOZ_ASSERT(aInfo);

    return !aInfo->IsPrivate() &&
           GetOriginAttrsHash(*aInfo->OriginAttributesPtr()) ==
               aRec->Get()->mOriginAttrsHash &&
           aInfo->IsAnonymous() == !!(aRec->Get()->mFlags & kAnonymousMask);
  }

  // Memory reporting
  size_t SizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const {
    return mallocSizeOf(mRec->Get());
  }

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const {
    return mallocSizeOf(this) + SizeOfExcludingThis(mallocSizeOf);
  }

 private:
  friend class CacheIndexEntryUpdate;
  friend class CacheIndex;
  friend class CacheIndexEntryAutoManage;
  friend struct CacheIndexRecord;

  static const uint32_t kInitializedMask = 0x80000000;
  static const uint32_t kAnonymousMask = 0x40000000;

  // This flag is set when the entry was removed. We need to keep this
  // information in memory until we write the index file.
  static const uint32_t kRemovedMask = 0x20000000;

  // This flag is set when the information in memory is not in sync with the
  // information in index file on disk.
  static const uint32_t kDirtyMask = 0x10000000;

  // This flag is set when the information about the entry is fresh, i.e.
  // we've created or opened this entry during this session, or we've seen
  // this entry during update or build process.
  static const uint32_t kFreshMask = 0x08000000;

  // Indicates a pinned entry.
  static const uint32_t kPinnedMask = 0x04000000;

  // Indicates there is cached alternative data in the entry.
  static const uint32_t kHasAltDataMask = 0x02000000;

  // Indicates that this entry is a dictionary
  static const uint32_t kDictionaryMask = 0x01000000;

  // Indicates that this entry has a No-Vary-Search response header stored
  // in its metadata. Used to warm mNoVarySearchIndex on startup.
  static const uint32_t kHasNoVarySearchMask = 0x00800000;

  // FileSize in kilobytes (max 8GB)
  static const uint32_t kFileSizeMask = 0x007FFFFF;

  RefPtr<CacheIndexRecordWrapper> mRec;
};

class CacheIndexEntryUpdate : public CacheIndexEntry {
 public:
  explicit CacheIndexEntryUpdate(CacheIndexEntry::KeyTypePointer aKey)
      : CacheIndexEntry(aKey), mUpdateFlags(0) {
    MOZ_COUNT_CTOR(CacheIndexEntryUpdate);
    LOG(("CacheIndexEntryUpdate::CacheIndexEntryUpdate()"));
  }
  ~CacheIndexEntryUpdate() {
    MOZ_COUNT_DTOR(CacheIndexEntryUpdate);
    LOG(("CacheIndexEntryUpdate::~CacheIndexEntryUpdate()"));
  }

  CacheIndexEntryUpdate& operator=(const CacheIndexEntry& aOther) {
    MOZ_ASSERT(memcmp(&mRec->Get()->mHash, &aOther.mRec->Get()->mHash,
                      sizeof(SHA1Sum::Hash)) == 0);
    mUpdateFlags = 0;
    *(static_cast<CacheIndexEntry*>(this)) = aOther;
    return *this;
  }

  void InitNew() {
    mUpdateFlags = kFrecencyUpdatedMask | kHasAltDataUpdatedMask |
                   kLastFetchedUpdatedMask | kFetchCountUpdatedMask |
                   kContentTypeUpdatedMask | kFileSizeUpdatedMask;
    CacheIndexEntry::InitNew();
  }

  void SetFrecency(uint32_t aFrecency) {
    mUpdateFlags |= kFrecencyUpdatedMask;
    CacheIndexEntry::SetFrecency(aFrecency);
  }

  void SetHasAltData(bool aHasAltData) {
    mUpdateFlags |= kHasAltDataUpdatedMask;
    CacheIndexEntry::SetHasAltData(aHasAltData);
  }

  void SetLastFetched(uint32_t aLastFetched) {
    mUpdateFlags |= kLastFetchedUpdatedMask;
    CacheIndexEntry::SetLastFetched(aLastFetched);
  }

  void SetFetchCount(uint32_t aFetchCount) {
    mUpdateFlags |= kFetchCountUpdatedMask;
    CacheIndexEntry::SetFetchCount(aFetchCount);
  }

  void SetContentType(uint8_t aType) {
    mUpdateFlags |= kContentTypeUpdatedMask;
    CacheIndexEntry::SetContentType(aType);
  }

  void SetFileSize(uint32_t aFileSize) {
    mUpdateFlags |= kFileSizeUpdatedMask;
    CacheIndexEntry::SetFileSize(aFileSize);
  }

  void ApplyUpdate(CacheIndexEntry* aDst) {
    MOZ_ASSERT(memcmp(&mRec->Get()->mHash, &aDst->mRec->Get()->mHash,
                      sizeof(SHA1Sum::Hash)) == 0);
    if (mUpdateFlags & kFrecencyUpdatedMask) {
      aDst->mRec->Get()->mFrecency = mRec->Get()->mFrecency;
    }
    aDst->mRec->Get()->mOriginAttrsHash = mRec->Get()->mOriginAttrsHash;
    if (mUpdateFlags & kLastFetchedUpdatedMask) {
      aDst->mRec->Get()->mLastFetched = mRec->Get()->mLastFetched;
    }
    if (mUpdateFlags & kFetchCountUpdatedMask) {
      aDst->mRec->Get()->mFetchCount = mRec->Get()->mFetchCount;
    }
    if (mUpdateFlags & kContentTypeUpdatedMask) {
      aDst->mRec->Get()->mContentType = mRec->Get()->mContentType;
    }
    if (mUpdateFlags & kHasAltDataUpdatedMask &&
        ((aDst->mRec->Get()->mFlags ^ mRec->Get()->mFlags) & kHasAltDataMask)) {
      // Toggle the bit if we need to.
      aDst->mRec->Get()->mFlags ^= kHasAltDataMask;
    }

    // mFlags packs the file size alongside the boolean flags, so this must be
    // an assignment and not a merge. Keep |HasAltData|, which the toggle above
    // has already set, and keep the file size unless the update carries a new
    // one; take every other bit from the update.
    uint32_t keepMask = kHasAltDataMask;
    if (!(mUpdateFlags & kFileSizeUpdatedMask)) {
      keepMask |= kFileSizeMask;
    }
    aDst->mRec->Get()->mFlags = (aDst->mRec->Get()->mFlags & keepMask) |
                                (mRec->Get()->mFlags & ~keepMask);
  }

 private:
  static const uint32_t kFrecencyUpdatedMask = 0x00000001;
  static const uint32_t kContentTypeUpdatedMask = 0x00000002;
  static const uint32_t kFileSizeUpdatedMask = 0x00000004;
  static const uint32_t kHasAltDataUpdatedMask = 0x00000008;
  static const uint32_t kLastFetchedUpdatedMask = 0x00000010;
  static const uint32_t kFetchCountUpdatedMask = 0x00000020;

  uint32_t mUpdateFlags;
};

class CacheIndexStats {
 public:
  CacheIndexStats() {
    for (uint32_t i = 0; i < nsICacheEntry::CONTENT_TYPE_LAST; ++i) {
      mCountByType[i] = 0;
      mSizeByType[i] = 0;
    }
  }

  bool operator==(const CacheIndexStats& aOther) const {
    for (uint32_t i = 0; i < nsICacheEntry::CONTENT_TYPE_LAST; ++i) {
      if (mCountByType[i] != aOther.mCountByType[i] ||
          mSizeByType[i] != aOther.mSizeByType[i]) {
        return false;
      }
    }

    return
#ifdef DEBUG
        aOther.mStateLogged == mStateLogged &&
#endif
        aOther.mCount == mCount && aOther.mNotInitialized == mNotInitialized &&
        aOther.mRemoved == mRemoved && aOther.mDirty == mDirty &&
        aOther.mFresh == mFresh && aOther.mEmpty == mEmpty &&
        aOther.mSize == mSize;
  }

#ifdef DEBUG
  void DisableLogging() { mDisableLogging = true; }
#endif

  void Log() {
    LOG(
        ("CacheIndexStats::Log() [count=%u, notInitialized=%u, removed=%u, "
         "dirty=%u, fresh=%u, empty=%u, size=%u]",
         mCount, mNotInitialized, mRemoved, mDirty, mFresh, mEmpty, mSize));
  }

  void Clear() {
    MOZ_ASSERT(!mStateLogged, "CacheIndexStats::Clear() - state logged!");

    mCount = 0;
    mNotInitialized = 0;
    mRemoved = 0;
    mDirty = 0;
    mFresh = 0;
    mEmpty = 0;
    mSize = 0;
    for (uint32_t i = 0; i < nsICacheEntry::CONTENT_TYPE_LAST; ++i) {
      mCountByType[i] = 0;
      mSizeByType[i] = 0;
    }
  }

#ifdef DEBUG
  bool StateLogged() { return mStateLogged; }
#endif

  uint32_t Count() {
    MOZ_ASSERT(!mStateLogged, "CacheIndexStats::Count() - state logged!");
    return mCount;
  }

  uint32_t CountByType(uint8_t aContentType) {
    MOZ_ASSERT(!mStateLogged, "CacheIndexStats::CountByType() - state logged!");
    MOZ_RELEASE_ASSERT(aContentType < nsICacheEntry::CONTENT_TYPE_LAST);
    return mCountByType[aContentType];
  }

  uint32_t Dirty() {
    MOZ_ASSERT(!mStateLogged, "CacheIndexStats::Dirty() - state logged!");
    return mDirty;
  }

  uint32_t Fresh() {
    MOZ_ASSERT(!mStateLogged, "CacheIndexStats::Fresh() - state logged!");
    return mFresh;
  }

  uint32_t ActiveEntriesCount() {
    MOZ_ASSERT(!mStateLogged,
               "CacheIndexStats::ActiveEntriesCount() - state "
               "logged!");
    return mCount - mRemoved - mNotInitialized - mEmpty;
  }

  uint32_t Size() {
    MOZ_ASSERT(!mStateLogged, "CacheIndexStats::Size() - state logged!");
    return mSize;
  }

  uint32_t SizeByType(uint8_t aContentType) {
    MOZ_ASSERT(!mStateLogged, "CacheIndexStats::SizeByType() - state logged!");
    MOZ_RELEASE_ASSERT(aContentType < nsICacheEntry::CONTENT_TYPE_LAST);
    return mSizeByType[aContentType];
  }

  void BeforeChange(const CacheIndexEntry* aEntry) {
#ifdef DEBUG_STATS
    if (!mDisableLogging) {
      LOG(("CacheIndexStats::BeforeChange()"));
      Log();
    }
#endif

    MOZ_ASSERT(!mStateLogged,
               "CacheIndexStats::BeforeChange() - state "
               "logged!");
#ifdef DEBUG
    mStateLogged = true;
#endif
    if (aEntry) {
      MOZ_ASSERT(mCount);
      uint8_t contentType = aEntry->GetContentType();
      mCount--;
      mCountByType[contentType]--;
      if (aEntry->IsDirty()) {
        MOZ_ASSERT(mDirty);
        mDirty--;
      }
      if (aEntry->IsFresh()) {
        MOZ_ASSERT(mFresh);
        mFresh--;
      }
      if (aEntry->IsRemoved()) {
        MOZ_ASSERT(mRemoved);
        mRemoved--;
      } else {
        if (!aEntry->IsInitialized()) {
          MOZ_ASSERT(mNotInitialized);
          mNotInitialized--;
        } else {
          if (aEntry->IsFileEmpty()) {
            MOZ_ASSERT(mEmpty);
            mEmpty--;
          } else {
            MOZ_ASSERT(mSize >= aEntry->GetFileSize());
            mSize -= aEntry->GetFileSize();
            mSizeByType[contentType] -= aEntry->GetFileSize();
          }
        }
      }
    }
  }

  void AfterChange(const CacheIndexEntry* aEntry) {
    MOZ_ASSERT(mStateLogged,
               "CacheIndexStats::AfterChange() - state not "
               "logged!");
#ifdef DEBUG
    mStateLogged = false;
#endif
    if (aEntry) {
      uint8_t contentType = aEntry->GetContentType();
      ++mCount;
      ++mCountByType[contentType];
      if (aEntry->IsDirty()) {
        mDirty++;
      }
      if (aEntry->IsFresh()) {
        mFresh++;
      }
      if (aEntry->IsRemoved()) {
        mRemoved++;
      } else {
        if (!aEntry->IsInitialized()) {
          mNotInitialized++;
        } else {
          if (aEntry->IsFileEmpty()) {
            mEmpty++;
          } else {
            mSize += aEntry->GetFileSize();
            mSizeByType[contentType] += aEntry->GetFileSize();
          }
        }
      }
    }

#ifdef DEBUG_STATS
    if (!mDisableLogging) {
      LOG(("CacheIndexStats::AfterChange()"));
      Log();
    }
#endif
  }

 private:
  uint32_t mCount{0};
  uint32_t mCountByType[nsICacheEntry::CONTENT_TYPE_LAST]{0};
  uint32_t mNotInitialized{0};
  uint32_t mRemoved{0};
  uint32_t mDirty{0};
  uint32_t mFresh{0};
  uint32_t mEmpty{0};
  uint32_t mSize{0};
  uint32_t mSizeByType[nsICacheEntry::CONTENT_TYPE_LAST]{0};
#ifdef DEBUG
  // We completely remove the data about an entry from the stats in
  // BeforeChange() and set this flag to true. The entry is then modified,
  // deleted or created and the data is again put into the stats and this flag
  // set to false. Statistics must not be read during this time since the
  // information is not correct.
  bool mStateLogged{false};

  // Disables logging in this instance of CacheIndexStats
  bool mDisableLogging{false};
#endif
};

class CacheIndex final : public CacheFileIOListener, public nsIRunnable {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIRUNNABLE

  CacheIndex();
  FRIEND_TEST(FrecencyStorageTest, AppendRemoveRecordTest);
  FRIEND_TEST(FrecencyStorageTest, ReplaceRecordTest);
  FRIEND_TEST(FrecencyStorageTest, ClearTest);
  FRIEND_TEST(FrecencyStorageTest, GetSortedSnapshotForEvictionTest);
  FRIEND_TEST(FrecencyStorageTest, PerformanceTest);

  static nsresult Init(nsIFile* aCacheDirectory);
  static nsresult PreShutdown();
  static nsresult Shutdown();

  // Force the in-memory index to be persisted to disk without shutting the
  // cache down, used when the app is backgrounded (e.g. on Android, where the
  // process is typically killed rather than cleanly shut down). The write is
  // dispatched to the IO thread; the dirty flag is intentionally left set so a
  // subsequent kill still triggers the normal recovery/update pass on restart.
  static void WriteIndexToDiskNow();

  // Following methods can be called only on IO thread.

  // Add entry to the index. The entry shouldn't be present in index. This
  // method is called whenever a new handle for a new entry file is created. The
  // newly created entry is not initialized and it must be either initialized
  // with InitEntry() or removed with RemoveEntry().
  static nsresult AddEntry(const SHA1Sum::Hash* aHash);

  // Inform index about an existing entry that should be present in index. This
  // method is called whenever a new handle for an existing entry file is
  // created. Like in case of AddEntry(), either InitEntry() or RemoveEntry()
  // must be called on the entry, since the entry is not initizlized if the
  // index is outdated.
  static nsresult EnsureEntryExists(const SHA1Sum::Hash* aHash);

  // Initialize the entry. It MUST be present in index. Call to AddEntry() or
  // EnsureEntryExists() must precede the call to this method.
  static nsresult InitEntry(const SHA1Sum::Hash* aHash,
                            OriginAttrsHash aOriginAttrsHash, bool aAnonymous,
                            bool aPinned);

  // Remove entry from index. The entry should be present in index.
  static nsresult RemoveEntry(const SHA1Sum::Hash* aHash,
                              const nsACString& aKey,
                              bool aClearDictionary = true);

  // Update some information in entry. The entry MUST be present in index and
  // MUST be initialized. Call to AddEntry() or EnsureEntryExists() and to
  // InitEntry() must precede the call to this method.
  // Pass nullptr if the value didn't change.
  static nsresult UpdateEntry(const SHA1Sum::Hash* aHash,
                              const uint32_t* aFrecency,
                              const bool* aHasAltData,
                              const uint32_t* aLastFetched,
                              const uint32_t* aFetchCount,
                              const uint8_t* aContentType,
                              const uint32_t* aSize);

  // Mark entries so we won't find them.  Used to implement synchronous
  // clearing for Clear-Site-Data: cache for Compression Dictionaries
  static void EvictByContext(const nsAString& aOrigin,
                             const nsAString& aBaseDomain);

  // Remove all entries from the index. Called when clearing the whole cache.
  static nsresult RemoveAll();

  enum EntryStatus { EXISTS = 0, DOES_NOT_EXIST = 1, DO_NOT_KNOW = 2 };

  // Used to store a snapshot of the frecency storage
  using EvictionSortedSnapshot = nsTArray<RefPtr<CacheIndexRecordWrapper>>;

  // Returns status of the entry in index for the given key. It can be called
  // on any thread.
  // If the optional aCB callback is given, the it will be called with a
  // CacheIndexEntry only if _retval is EXISTS when the method returns.
  static nsresult HasEntry(
      const nsACString& aKey, EntryStatus* _retval,
      const std::function<void(const CacheIndexEntry*)>& aCB = nullptr);
  static nsresult HasEntry(
      const SHA1Sum::Hash& hash, EntryStatus* _retval,
      const std::function<void(const CacheIndexEntry*)>& aCB = nullptr);

  // Returns a hash of the least important entry that should be evicted if the
  // cache size is over limit and also returns a total number of all entries in
  // the index minus the number of forced valid entries and unpinned entries
  // that we encounter when searching (see below)
  static nsresult GetEntryForEviction(EvictionSortedSnapshot& aSnapshot,
                                      bool aIgnoreEmptyEntries,
                                      SHA1Sum::Hash* aHash, uint32_t* aCnt);

  // Returns a sorted snapshot of the frecency storage.
  static EvictionSortedSnapshot GetSortedSnapshotForEviction();

  // Checks if a cache entry is currently forced valid. Used to prevent an entry
  // (that has been forced valid) from being evicted when the cache size reaches
  // its limit.
  static bool IsForcedValidEntry(const SHA1Sum::Hash* aHash);

  // Returns cache size in kB.
  static nsresult GetCacheSize(uint32_t* _retval);

  // Returns number of entry files in the cache
  static nsresult GetEntryFileCount(uint32_t* _retval);

  // Synchronously returns the disk occupation and number of entries
  // per-context. Callable on any thread. It will ignore loadContextInfo and get
  // stats for all entries if the aInfo is a nullptr.
  static nsresult GetCacheStats(nsILoadContextInfo* aInfo, uint32_t* aSize,
                                uint32_t* aCount);

  // Asynchronously gets the disk cache size, used for display in the UI.
  static nsresult AsyncGetDiskConsumption(
      nsICacheStorageConsumptionObserver* aObserver);

  // Returns an iterator that returns entries matching a given context that were
  // present in the index at the time this method was called. If aAddNew is true
  // then the iterator will also return entries created after this call.
  // NOTE: When some entry is removed from index it is removed also from the
  // iterator regardless what aAddNew was passed.
  static nsresult GetIterator(nsILoadContextInfo* aInfo, bool aAddNew,
                              CacheIndexIterator** _retval);

  // Returns true if we _think_ that the index is up to date. I.e. the state is
  // READY or WRITING and mIndexNeedsUpdate as well as mShuttingDown is false.
  static nsresult IsUpToDate(bool* _retval);

  // Called from CacheStorageService::Clear() and
  // CacheFileContextEvictor::EvictEntries(), sets a flag that blocks
  // notification to AsyncGetDiskConsumption.
  static void OnAsyncEviction(bool aEvicting);

  // We keep track of total bytes written to the cache to be able to do
  // a telemetry report after writting certain amount of data to the cache.
  static void UpdateTotalBytesWritten(uint32_t aBytesWritten);

  // Memory reporting
  static size_t SizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf);
  static size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf);

 private:
  friend class CacheIndexEntryAutoManage;
  friend class FileOpenHelper;
  friend class CacheIndexIterator;
  friend class CacheIndexRecordWrapper;
  friend class DeleteCacheIndexRecordWrapper;

  virtual ~CacheIndex();

  NS_IMETHOD OnFileOpened(CacheFileHandle* aHandle, nsresult aResult) override;
  void OnFileOpenedInternal(FileOpenHelper* aOpener, CacheFileHandle* aHandle,
                            nsresult aResult,
                            const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);
  NS_IMETHOD OnDataWritten(CacheFileHandle* aHandle, const char* aBuf,
                           nsresult aResult) override;
  NS_IMETHOD OnDataRead(CacheFileHandle* aHandle, char* aBuf,
                        nsresult aResult) override;
  NS_IMETHOD OnFileDoomed(CacheFileHandle* aHandle, nsresult aResult) override;
  NS_IMETHOD OnEOFSet(CacheFileHandle* aHandle, nsresult aResult) override;
  NS_IMETHOD OnFileRenamed(CacheFileHandle* aHandle, nsresult aResult) override;

  nsresult InitInternal(nsIFile* aCacheDirectory,
                        const StaticMutexAutoLock& aProofOfLock);
  void PreShutdownInternal();

  // This method returns false when index is not initialized or is shut down.
  bool IsIndexUsable() MOZ_REQUIRES(sLock);

  // This method checks whether the entry has the same values of
  // originAttributes and isAnonymous. We don't expect to find a collision
  // since these values are part of the key that we hash and we use a strong
  // hash function.
  static bool IsCollision(CacheIndexEntry* aEntry,
                          OriginAttrsHash aOriginAttrsHash, bool aAnonymous);

  // Checks whether any of the information about the entry has changed.
  static bool HasEntryChanged(CacheIndexEntry* aEntry,
                              const uint32_t* aFrecency,
                              const bool* aHasAltData,
                              const uint32_t* aLastFetched,
                              const uint32_t* aFetchCount,
                              const uint8_t* aContentType,
                              const uint32_t* aSize);

  // Merge all pending operations from mPendingUpdates into mIndex.
  void ProcessPendingOperations(const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);

  // Following methods perform writing of the index file.
  //
  // The index is written periodically, but not earlier than once in
  // browser.cache.disk.index.min_dump_interval_ms and there must be at least
  // browser.cache.disk.index.min_unwritten_changes differences between index on
  // disk and in memory. Index is always first written to a temporary file and
  // the old index file is replaced when the writing process succeeds.
  //
  // Starts writing of index when both limits (minimal delay between writes and
  // minimum number of changes in index) were exceeded.
  bool WriteIndexToDiskIfNeeded(const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);
  // IO-thread half of WriteIndexToDiskNow(): force a write if the index is
  // READY and has any dirty entry.
  void WriteIndexToDiskNowInternal();
  // Starts writing of index file.
  void WriteIndexToDisk(const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);
  // Serializes part of mIndex hashtable to the write buffer a writes the buffer
  // to the file.
  void WriteRecords(const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);
  // Finalizes writing process.
  void FinishWrite(bool aSucceeded, const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);

  // Following methods perform writing of the journal during shutdown. All these
  // methods must be called only during shutdown since they write/delete files
  // directly on the main thread instead of using CacheFileIOManager that does
  // it asynchronously on IO thread. Journal contains only entries that are
  // dirty, i.e. changes that are not present in the index file on the disk.
  // When the log is written successfully, the dirty flag in index file is
  // cleared.
  nsresult GetFile(const nsACString& aName, nsIFile** _retval);
  void RemoveFile(const nsACString& aName) MOZ_REQUIRES(sLock);
  void RemoveAllIndexFiles() MOZ_REQUIRES(sLock);
  void RemoveJournalAndTempFile() MOZ_REQUIRES(sLock);
  // Writes journal to the disk and clears dirty flag in index header.
  nsresult WriteLogToDisk() MOZ_REQUIRES(sLock);

  // Following methods perform reading of the index from the disk.
  //
  // Index is read at startup just after initializing the CacheIndex. There are
  // 3 files used when manipulating with index: index file, journal file and
  // a temporary file. All files contain the hash of the data, so we can check
  // whether the content is valid and complete. Index file contains also a dirty
  // flag in the index header which is unset on a clean shutdown. During opening
  // and reading of the files we determine the status of the whole index from
  // the states of the separate files. Following table shows all possible
  // combinations:
  //
  // index, journal, tmpfile
  // M      *        *       - index is missing    -> BUILD
  // I      *        *       - index is invalid    -> BUILD
  // D      *        *       - index is dirty      -> UPDATE
  // C      M        *       - index is dirty      -> UPDATE
  // C      I        *       - unexpected state    -> UPDATE
  // C      V        E       - unexpected state    -> UPDATE
  // C      V        M       - index is up to date -> READY
  //
  // where the letters mean:
  //   * - any state
  //   E - file exists
  //   M - file is missing
  //   I - data is invalid (parsing failed or hash didn't match)
  //   D - dirty (data in index file is correct, but dirty flag is set)
  //   C - clean (index file is clean)
  //   V - valid (data in journal file is correct)
  //
  // Note: We accept the data from journal only when the index is up to date as
  // a whole (i.e. C,V,M state).
  //
  // We rename the journal file to the temporary file as soon as possible after
  // initial test to ensure that we start update process on the next startup if
  // FF crashes during parsing of the index.
  //
  // Initiates reading index from disk.
  void ReadIndexFromDisk(const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);
  // Starts reading data from index file.
  void StartReadingIndex(const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);
  // Parses data read from index file.
  void ParseRecords(const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);
  // Starts reading data from journal file.
  void StartReadingJournal(const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);
  // Parses data read from journal file.
  void ParseJournal(const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);
  // Merges entries from journal into mIndex.
  void MergeJournal(const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);
  // In debug build this method checks that we have no fresh entry in mIndex
  // after we finish reading index and before we process pending operations.
  void EnsureNoFreshEntry() MOZ_REQUIRES(sLock);
  // In debug build this method is called after processing pending operations
  // to make sure mIndexStats contains correct information.
  void EnsureCorrectStats() MOZ_REQUIRES(sLock);

  // Finalizes reading process.
  void FinishRead(bool aSucceeded, const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);

  // Following methods perform updating and building of the index.
  // Timer callback that starts update or build process.
  static void DelayedUpdate(nsITimer* aTimer, void* aClosure);
  void DelayedUpdateLocked(const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);
  // Posts timer event that start update or build process.
  nsresult ScheduleUpdateTimer(uint32_t aDelay) MOZ_REQUIRES(sLock);
  nsresult SetupDirectoryEnumerator() MOZ_REQUIRES(sLock);
  nsresult InitEntryFromDiskData(CacheIndexEntry* aEntry,
                                 CacheFileMetadata* aMetaData,
                                 int64_t aFileSize);
  // Returns true when either a timer is scheduled or event is posted.
  bool IsUpdatePending() MOZ_REQUIRES(sLock);
  // Iterates through all files in entries directory that we didn't create/open
  // during this session, parses them and adds the entries to the index.
  void BuildIndex(const StaticMutexAutoLock& aProofOfLock) MOZ_REQUIRES(sLock);

  bool StartUpdatingIndexIfNeeded(const StaticMutexAutoLock& aProofOfLock,
                                  bool aSwitchingToReadyState = false);
  // Starts update or build process or fires a timer when it is too early after
  // startup.
  void StartUpdatingIndex(bool aRebuild,
                          const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);
  // Iterates through all files in entries directory that we didn't create/open
  // during this session and theirs last modified time is newer than timestamp
  // in the index header. Parses the files and adds the entries to the index.
  void UpdateIndex(const StaticMutexAutoLock& aProofOfLock) MOZ_REQUIRES(sLock);
  // Finalizes update or build process.
  void FinishUpdate(bool aSucceeded, const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);

  void RemoveNonFreshEntries(const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);

  enum EState {
    // Initial state in which the index is not usable
    // Possible transitions:
    //  -> READING
    INITIAL = 0,

    // Index is being read from the disk.
    // Possible transitions:
    //  -> INITIAL  - We failed to dispatch a read event.
    //  -> BUILDING - No or corrupted index file was found.
    //  -> UPDATING - No or corrupted journal file was found.
    //              - Dirty flag was set in index header.
    //  -> READY    - Index was read successfully or was interrupted by
    //                pre-shutdown.
    //  -> SHUTDOWN - This could happen only in case of pre-shutdown failure.
    READING = 1,

    // Index is being written to the disk.
    // Possible transitions:
    //  -> READY    - Writing of index finished or was interrupted by
    //                pre-shutdown..
    //  -> UPDATING - Writing of index finished, but index was found outdated
    //                during writing.
    //  -> SHUTDOWN - This could happen only in case of pre-shutdown failure.
    WRITING = 2,

    // Index is being build.
    // Possible transitions:
    //  -> READY    - Building of index finished or was interrupted by
    //                pre-shutdown.
    //  -> SHUTDOWN - This could happen only in case of pre-shutdown failure.
    BUILDING = 3,

    // Index is being updated.
    // Possible transitions:
    //  -> READY    - Updating of index finished or was interrupted by
    //                pre-shutdown.
    //  -> SHUTDOWN - This could happen only in case of pre-shutdown failure.
    UPDATING = 4,

    // Index is ready.
    // Possible transitions:
    //  -> UPDATING - Index was found outdated.
    //  -> SHUTDOWN - Index is shutting down.
    READY = 5,

    // Index is shutting down.
    SHUTDOWN = 6
  };

  static char const* StateString(EState aState);
  void ChangeState(EState aNewState, const StaticMutexAutoLock& aProofOfLock);
  void NotifyAsyncGetDiskConsumptionCallbacks() MOZ_REQUIRES(sLock);

  // Allocates and releases buffer used for reading and writing index.
  void AllocBuffer() MOZ_REQUIRES(sLock);
  void ReleaseBuffer() MOZ_REQUIRES(sLock);

  // Methods used by CacheIndexEntryAutoManage to keep the iterators up to date.
  void AddRecordToIterators(CacheIndexRecordWrapper* aRecord,
                            const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);
  void RemoveRecordFromIterators(CacheIndexRecordWrapper* aRecord,
                                 const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);
  void ReplaceRecordInIterators(CacheIndexRecordWrapper* aOldRecord,
                                CacheIndexRecordWrapper* aNewRecord,
                                const StaticMutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(sLock);

  // Memory reporting (private part)
  size_t SizeOfExcludingThisInternal(mozilla::MallocSizeOf mallocSizeOf) const
      MOZ_REQUIRES(sLock);

  // Reports telemetry about cache, i.e. size, entry count and content type
  // stats.
  void DoTelemetryReport() MOZ_REQUIRES(sLock);

  static mozilla::StaticRefPtr<CacheIndex> gInstance MOZ_GUARDED_BY(sLock);

  // sLock guards almost everything here...
  // Also guards FileOpenHelper::mCanceled
  static StaticMutex sLock;

  nsCOMPtr<nsIFile> mCacheDirectory;

  EState mState MOZ_GUARDED_BY(sLock){INITIAL};
  // Timestamp of time when the index was initialized. We use it to delay
  // initial update or build of index.
  TimeStamp mStartTime MOZ_GUARDED_BY(sLock);
  // Set to true in PreShutdown(), it is checked on variaous places to prevent
  // starting any process (write, update, etc.) during shutdown.
  bool mShuttingDown MOZ_GUARDED_BY(sLock){false};
  // When set to true, update process should start as soon as possible. This
  // flag is set whenever we find some inconsistency which would be fixed by
  // update process. The flag is checked always when switching to READY state.
  // To make sure we start the update process as soon as possible, methods that
  // set this flag should also call StartUpdatingIndexIfNeeded() to cover the
  // case when we are currently in READY state.
  bool mIndexNeedsUpdate MOZ_GUARDED_BY(sLock){false};
  // Set at the beginning of RemoveAll() which clears the whole index. When
  // removing all entries we must stop any pending reading, writing, updating or
  // building operation. This flag is checked at various places and it prevents
  // we won't start another operation (e.g. canceling reading of the index would
  // normally start update or build process)
  bool mRemovingAll MOZ_GUARDED_BY(sLock){false};
  // Whether the index file on disk exists and is valid.
  bool mIndexOnDiskIsValid MOZ_GUARDED_BY(sLock){false};
  // When something goes wrong during updating or building process, we don't
  // mark index clean (and also don't write journal) to ensure that update or
  // build will be initiated on the next start.
  bool mDontMarkIndexClean MOZ_GUARDED_BY(sLock){false};
  // Timestamp value from index file. It is used during update process to skip
  // entries that were last modified before this timestamp.
  uint32_t mIndexTimeStamp MOZ_GUARDED_BY(sLock){0};
  // Timestamp of last time the index was dumped to disk.
  // NOTE: The index might not be necessarily dumped at this time. The value
  // is used to schedule next dump of the index.
  TimeStamp mLastDumpTime MOZ_GUARDED_BY(sLock);

  // Timer of delayed update/build.
  nsCOMPtr<nsITimer> mUpdateTimer MOZ_GUARDED_BY(sLock);
  // True when build or update event is posted
  bool mUpdateEventPending MOZ_GUARDED_BY(sLock){false};

  // Helper members used when reading/writing index from/to disk.
  // Contains number of entries that should be skipped:
  //  - in hashtable when writing index because they were already written
  //  - in index file when reading index because they were already read
  uint32_t mSkipEntries MOZ_GUARDED_BY(sLock){0};
  // Number of entries that should be written to disk. This is number of entries
  // in hashtable that are initialized and are not marked as removed when
  // writing begins.
  uint32_t mProcessEntries MOZ_GUARDED_BY(sLock){0};
  char* mRWBuf MOZ_GUARDED_BY(sLock){nullptr};
  uint32_t mRWBufSize MOZ_GUARDED_BY(sLock){0};
  uint32_t mRWBufPos MOZ_GUARDED_BY(sLock){0};
  RefPtr<CacheHash> mRWHash MOZ_GUARDED_BY(sLock);

  // True if read or write operation is pending. It is used to ensure that
  // mRWBuf is not freed until OnDataRead or OnDataWritten is called.
  bool mRWPending MOZ_GUARDED_BY(sLock){false};

  // Reading of journal succeeded if true.
  bool mJournalReadSuccessfully MOZ_GUARDED_BY(sLock){false};

  // Handle used for writing and reading index file.
  RefPtr<CacheFileHandle> mIndexHandle MOZ_GUARDED_BY(sLock);
  // Handle used for reading journal file.
  RefPtr<CacheFileHandle> mJournalHandle MOZ_GUARDED_BY(sLock);
  // Used to check the existence of the file during reading process.
  RefPtr<CacheFileHandle> mTmpHandle MOZ_GUARDED_BY(sLock);

  RefPtr<FileOpenHelper> mIndexFileOpener MOZ_GUARDED_BY(sLock);
  RefPtr<FileOpenHelper> mJournalFileOpener MOZ_GUARDED_BY(sLock);
  RefPtr<FileOpenHelper> mTmpFileOpener MOZ_GUARDED_BY(sLock);

  // Directory enumerator used when building and updating index.
  nsCOMPtr<nsIDirectoryEnumerator> mDirEnumerator MOZ_GUARDED_BY(sLock);

  // Main index hashtable.
  nsTHashtable<CacheIndexEntry> mIndex MOZ_GUARDED_BY(sLock);

  // We cannot add, remove or change any entry in mIndex in states READING and
  // WRITING. We track all changes in mPendingUpdates during these states.
  nsTHashtable<CacheIndexEntryUpdate> mPendingUpdates MOZ_GUARDED_BY(sLock);

  // Contains information statistics for mIndex + mPendingUpdates.
  CacheIndexStats mIndexStats MOZ_GUARDED_BY(sLock);

  // When reading journal, we must first parse the whole file and apply the
  // changes iff the journal was read successfully. mTmpJournal is used to store
  // entries from the journal file. We throw away all these entries if parsing
  // of the journal fails or the hash does not match.
  nsTHashtable<CacheIndexEntry> mTmpJournal MOZ_GUARDED_BY(sLock);

  // FrecencyStorage maintains records for eviction. Due to the massive amount
  // of calls at startup to the "Contains" method,
  // we keep the records in a hashtable for faster lookup. Sorting is always
  // done during eviction. This allows us to keep the hashtable fast and
  // efficient, while still being able to evict entries based on frecency.
  class FrecencyStorage final {
    FRIEND_TEST(FrecencyStorageTest, AppendRemoveRecordTest);
    FRIEND_TEST(FrecencyStorageTest, ReplaceRecordTest);
    FRIEND_TEST(FrecencyStorageTest, ClearTest);
    FRIEND_TEST(FrecencyStorageTest, GetSortedSnapshotForEvictionTest);
    FRIEND_TEST(FrecencyStorageTest, PerformanceTest);

   public:
    FrecencyStorage() = default;

    // Methods used by CacheIndexEntryAutoManage to keep the storage up to date.
    void AppendRecord(CacheIndexRecordWrapper* aRecord,
                      const StaticMutexAutoLock& aProofOfLock);

    void RemoveRecord(CacheIndexRecordWrapper* aRecord,
                      const StaticMutexAutoLock& aProofOfLock);

    void ReplaceRecord(CacheIndexRecordWrapper* aOldRecord,
                       CacheIndexRecordWrapper* aNewRecord,
                       const StaticMutexAutoLock& aProofOfLock);

    bool RecordExistedUnlocked(CacheIndexRecordWrapper* aRecord);

    // EvictionSortedSnapshot is used to transform FrecencyStorage into a sorted
    // array which is then used for eviction.
    EvictionSortedSnapshot GetSortedSnapshotForEviction();

    size_t Length() const { return mRecs.Count(); }

    void Clear(const StaticMutexAutoLock& aProofOfLock) { mRecs.Clear(); }

   private:
    friend class CacheIndex;
    nsTHashtable<nsRefPtrHashKey<CacheIndexRecordWrapper>> mRecs;
  };

  FrecencyStorage mFrecencyStorage MOZ_GUARDED_BY(sLock);

  nsTArray<CacheIndexIterator*> mIterators MOZ_GUARDED_BY(sLock);

  // This flag is true iff we are between CacheStorageService:Clear() and
  // processing all contexts to be evicted.  It will make UI to show
  // "calculating" instead of any intermediate cache size.
  bool mAsyncGetDiskConsumptionBlocked MOZ_GUARDED_BY(sLock){false};

  class DiskConsumptionObserver : public Runnable {
   public:
    static DiskConsumptionObserver* Init(
        nsICacheStorageConsumptionObserver* aObserver) {
      nsWeakPtr observer = do_GetWeakReference(aObserver);
      if (!observer) return nullptr;

      return new DiskConsumptionObserver(observer);
    }

    void OnDiskConsumption(int64_t aSize) {
      mSize = aSize;
      NS_DispatchToMainThread(this);
    }

   private:
    explicit DiskConsumptionObserver(nsWeakPtr const& aWeakObserver)
        : Runnable("net::CacheIndex::DiskConsumptionObserver"),
          mObserver(aWeakObserver),
          mSize(0) {}
    virtual ~DiskConsumptionObserver() {
      if (mObserver && !NS_IsMainThread()) {
        NS_ReleaseOnMainThread("DiskConsumptionObserver::mObserver",
                               mObserver.forget());
      }
    }

    NS_IMETHOD Run() override {
      MOZ_ASSERT(NS_IsMainThread());

      nsCOMPtr<nsICacheStorageConsumptionObserver> observer =
          do_QueryReferent(mObserver);

      mObserver = nullptr;

      if (observer) {
        observer->OnNetworkCacheDiskConsumption(mSize);
      }

      return NS_OK;
    }

    nsWeakPtr mObserver;
    int64_t mSize;
  };

  // List of async observers that want to get disk consumption information
  nsTArray<RefPtr<DiskConsumptionObserver>> mDiskConsumptionObservers
      MOZ_GUARDED_BY(sLock);

  // Number of bytes written to the cache since the last telemetry report
  uint64_t mTotalBytesWritten MOZ_GUARDED_BY(sLock){0};
};

}  // namespace net
}  // namespace mozilla

#endif
