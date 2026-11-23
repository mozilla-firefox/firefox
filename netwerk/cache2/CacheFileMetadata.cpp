/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CacheFileMetadata.h"

#include "CacheCrypto.h"
#include "CacheFileChunk.h"
#include "CacheFileIOManager.h"
#include "CacheFileUtils.h"
#include "CacheHashUtils.h"
#include "CacheLog.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "nsCRT.h"
#include "nsICacheEntry.h"
#include "nsICacheEntry.h"  // for nsICacheEntryMetaDataVisitor
#include "nsIFile.h"
#include "nsILoadContextInfo.h"
#include "prnetdb.h"

namespace mozilla::net {

// The metadata block ends with an unencrypted two-word trailer the reader
// consults before touching the rest:
//
//   [ formatVersion(uint32) ][ metaStartOffset | flag (uint32) ]
//
// The last word is the metadata start offset; its high bit (kEncryptedFlag) is
// set iff the entry is encrypted, and its low bits are the logical data size.
// The word before it is a clear-text copy of CacheFileMetadataHeader::mVersion,
// so the reader can identify the format up front -- a version it does not
// understand is rejected, and the physical metadata start of an encrypted entry
// is derived from the logical data size (each chunk is kBlockOverhead larger on
// disk).
//
// For an encrypted entry the rest of the metadata block (everything before the
// trailer) is a single AES-GCM block, encrypted and decrypted exactly like a
// data chunk via CacheCrypto::Encrypt/DecryptBlock with kMetadataBlockNumber.
// The read paths decrypt it before ParseMetadata runs, so ParseMetadata always
// works on plaintext.
//
// Entries from older formats (which this build still reads) end with just the
// bare offset word; they are recognized by the trailing version word not
// matching a known version.
static const uint32_t kEncryptedFlag = 0x80000000;

// Physical offset of the metadata block for an encrypted entry: the logical
// data size plus the per-chunk on-disk overhead for each chunk.
static uint32_t EncryptedMetaPhysicalOffset(uint32_t aLogicalDataSize) {
  uint32_t chunks = aLogicalDataSize / kChunkSize;
  if (aLogicalDataSize % kChunkSize) {
    chunks++;
  }
  return aLogicalDataSize + chunks * CacheCrypto::kBlockOverhead;
}

// Decoded form of the metadata trailer.
struct MetadataTrailer {
  bool mEncrypted;
  uint32_t mLogicalDataSize;
  // Physical offset of the metadata block within the entry file.
  uint32_t mPhysOffset;
  // Number of trailing offset words: 2 for the current [version][offset|flag]
  // trailer, 1 for old single-offset-word entries.
  uint32_t mTrailerWords;
};

// Decodes the two trailing words of an entry. aVersionWord is the
// second-to-last word (a clear-text copy of the format version in the current
// format; the caller passes 0 when it is not present), aOffsetWord the last
// word. The current format is recognized by the version word naming a version
// we understand, in which case the offset word's high bit flags encryption and
// its low bits are the logical data size; older entries end with just the bare
// offset word.
static MetadataTrailer DecodeMetadataTrailer(uint32_t aVersionWord,
                                             uint32_t aOffsetWord) {
  MetadataTrailer trailer;
  bool newFormat = aVersionWord >= 4 && aVersionWord <= kCacheEntryVersion;
  if (newFormat) {
    trailer.mEncrypted = aOffsetWord & kEncryptedFlag;
    trailer.mLogicalDataSize = aOffsetWord & ~kEncryptedFlag;
    trailer.mPhysOffset =
        trailer.mEncrypted
            ? EncryptedMetaPhysicalOffset(trailer.mLogicalDataSize)
            : trailer.mLogicalDataSize;
    trailer.mTrailerWords = 2;
  } else {
    // Old single-word-trailer entries predate encryption: the whole word is the
    // offset/data size and the entry is never encrypted.
    trailer.mEncrypted = false;
    trailer.mLogicalDataSize = aOffsetWord;
    trailer.mPhysOffset = aOffsetWord;
    trailer.mTrailerWords = 1;
  }
  return trailer;
}

#define kMinMetadataRead 1024  // TODO find optimal value from telemetry
#define kAlignSize 4096

// Most of the cache entries fit into one chunk due to current chunk size. Make
// sure to tweak this value if kChunkSize is going to change.
#define kInitialHashArraySize 1

// Initial elements buffer size.
#define kInitialBufSize 64

// Max size of elements in bytes.
#define kMaxElementsSize (64 * 1024)

#define NOW_SECONDS() (uint32_t(PR_Now() / PR_USEC_PER_SEC))

NS_IMPL_ISUPPORTS(CacheFileMetadata, CacheFileIOListener)

CacheFileMetadata::CacheFileMetadata(
    CacheFileHandle* aHandle, const nsACString& aKey,
    NotNull<CacheFileUtils::CacheFileLock*> aLock)
    : CacheMemoryConsumer(NORMAL),
      mHandle(aHandle),
      mOffset(-1),
      mIsDirty(false),
      mAnonymous(false),
      mAllocExactSize(false),
      mFirstRead(true),
      mLock(aLock) {
  LOG(("CacheFileMetadata::CacheFileMetadata() [this=%p, handle=%p, key=%s]",
       this, aHandle, PromiseFlatCString(aKey).get()));

  memset(&mMetaHdr, 0, sizeof(CacheFileMetadataHeader));
  mMetaHdr.mVersion = kCacheEntryVersion;
  mMetaHdr.mExpirationTime = nsICacheEntry::NO_EXPIRATION_TIME;
  mKey = aKey;

  DebugOnly<nsresult> rv{};
  rv = ParseKey(aKey);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

CacheFileMetadata::CacheFileMetadata(
    bool aMemoryOnly, bool aPinned, const nsACString& aKey,
    NotNull<CacheFileUtils::CacheFileLock*> aLock)
    : CacheMemoryConsumer(aMemoryOnly ? MEMORY_ONLY : NORMAL),
      mIsDirty(true),
      mAnonymous(false),
      mAllocExactSize(false),
      mFirstRead(true),
      mLock(aLock) {
  LOG(("CacheFileMetadata::CacheFileMetadata() [this=%p, key=%s]", this,
       PromiseFlatCString(aKey).get()));

  memset(&mMetaHdr, 0, sizeof(CacheFileMetadataHeader));
  mMetaHdr.mVersion = kCacheEntryVersion;
  if (aPinned) {
    AddFlags(kCacheEntryIsPinned);
  }
  mMetaHdr.mExpirationTime = nsICacheEntry::NO_EXPIRATION_TIME;
  mKey = aKey;
  mMetaHdr.mKeySize = mKey.Length();

  DebugOnly<nsresult> rv{};
  rv = ParseKey(aKey);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

CacheFileMetadata::CacheFileMetadata()
    : CacheMemoryConsumer(DONT_REPORT /* This is a helper class */),
      mIsDirty(false),
      mAnonymous(false),
      mAllocExactSize(false),
      mFirstRead(true),
      mLock(new CacheFileUtils::CacheFileLock()) {
  LOG(("CacheFileMetadata::CacheFileMetadata() [this=%p]", this));

  memset(&mMetaHdr, 0, sizeof(CacheFileMetadataHeader));
}

CacheFileMetadata::~CacheFileMetadata() {
  LOG(("CacheFileMetadata::~CacheFileMetadata() [this=%p]", this));

  MOZ_ASSERT(!mListener);

  if (mHashArray) {
    CacheFileUtils::FreeBuffer(mHashArray);
    mHashArray = nullptr;
    mHashArraySize = 0;
  }

  if (mBuf) {
    CacheFileUtils::FreeBuffer(mBuf);
    mBuf = nullptr;
    mBufSize = 0;
  }
}

void CacheFileMetadata::SetHandle(CacheFileHandle* aHandle) {
  LOG(("CacheFileMetadata::SetHandle() [this=%p, handle=%p]", this, aHandle));

  MOZ_ASSERT(!mHandle);

  mHandle = aHandle;
}

void CacheFileMetadata::ReadMetadata(CacheFileMetadataListener* aListener) {
  LOG(("CacheFileMetadata::ReadMetadata() [this=%p, listener=%p]", this,
       aListener));

  MOZ_ASSERT(!mListener);
  MOZ_ASSERT(!mHashArray);
  MOZ_ASSERT(!mBuf);
  MOZ_ASSERT(!mWriteBuf);

  nsresult rv;

  int64_t size = mHandle->FileSize();
  MOZ_ASSERT(size != -1);

  if (size == 0) {
    // this is a new entry
    LOG(
        ("CacheFileMetadata::ReadMetadata() - Filesize == 0, creating empty "
         "metadata. [this=%p]",
         this));

    InitEmptyMetadata();
    aListener->OnMetadataRead(NS_OK);
    return;
  }

  if (size < int64_t(sizeof(CacheFileMetadataHeader) + 2 * sizeof(uint32_t))) {
    // there must be at least checksum, header and offset
    // The current format additionally carries a clear-text version word (a
    // two-word trailer); ParseMetadata does the precise validation.
    LOG(
        ("CacheFileMetadata::ReadMetadata() - File is corrupted, creating "
         "empty metadata. [this=%p, filesize=%" PRId64 "]",
         this, size));

    InitEmptyMetadata();
    aListener->OnMetadataRead(NS_OK);
    return;
  }

  // Set offset so that we read at least kMinMetadataRead if the file is big
  // enough.
  int64_t offset;
  if (size < kMinMetadataRead) {
    offset = 0;
  } else {
    offset = size - kMinMetadataRead;
  }

  // round offset to kAlignSize blocks
  offset = (offset / kAlignSize) * kAlignSize;

  mBufSize = size - offset;
  mBuf = static_cast<char*>(moz_xmalloc(mBufSize));

  DoMemoryReport(MemoryUsage());

  LOG(
      ("CacheFileMetadata::ReadMetadata() - Reading metadata from disk, trying "
       "offset=%" PRId64 ", filesize=%" PRId64 " [this=%p]",
       offset, size, this));

  mReadStart = mozilla::TimeStamp::Now();
  mListener = aListener;
  rv = CacheFileIOManager::Read(mHandle, offset, mBuf, mBufSize, this);
  if (NS_FAILED(rv)) {
    LOG(
        ("CacheFileMetadata::ReadMetadata() - CacheFileIOManager::Read() failed"
         " synchronously, creating empty metadata. [this=%p, rv=0x%08" PRIx32
         "]",
         this, static_cast<uint32_t>(rv)));

    mListener = nullptr;
    InitEmptyMetadata();
    aListener->OnMetadataRead(NS_OK);
  }
}

uint32_t CacheFileMetadata::CalcMetadataSize(uint32_t aElementsSize,
                                             uint32_t aHashCount) {
  return sizeof(uint32_t) +                          // hash of the metadata
         aHashCount * sizeof(CacheHash::Hash16_t) +  // array of chunk hashes
         sizeof(CacheFileMetadataHeader) +           // metadata header
         mKey.Length() + 1 +                         // key with trailing null
         aElementsSize +                             // elements
         sizeof(uint32_t);                           // offset
}

nsresult CacheFileMetadata::WriteMetadata(
    uint32_t aOffset, CacheFileMetadataListener* aListener) {
  LOG(("CacheFileMetadata::WriteMetadata() [this=%p, offset=%d, listener=%p]",
       this, aOffset, aListener));

  MOZ_ASSERT(!mListener);
  MOZ_ASSERT(!mWriteBuf);

  nsresult rv;

  mIsDirty = false;

  const bool encrypted = IsEncrypted();
  RefPtr<CacheCrypto> crypto;
  if (encrypted) {
    crypto = CacheCrypto::GetInstanceOrNull();
    if (!crypto) {
      return NS_ERROR_NOT_AVAILABLE;
    }
  }

  // Plaintext content layout: [hash32][hashes][header][key+null][elements].
  // CalcMetadataSize counts one trailing offset word, which the content does
  // not include, so subtract it back out.
  uint32_t contentLen =
      CalcMetadataSize(mElementsSize, mHashCount) - sizeof(uint32_t);

  // On-disk buffer: the content (for an encrypted entry the whole content is
  // one AES-GCM block, growing by kBlockOverhead) followed by the unencrypted
  // two-word trailer [ formatVersion ][ metaStartOffset | flag ].
  uint32_t bufSize = contentLen +
                     (encrypted ? CacheCrypto::kBlockOverhead : 0) +
                     2 * sizeof(uint32_t);
  mWriteBuf = static_cast<char*>(malloc(bufSize));
  if (!mWriteBuf) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // Build the plaintext content. When encrypting it is assembled in a scratch
  // buffer and then encrypted into mWriteBuf; otherwise it is built in place.
  UniquePtr<char[]> scratch;
  char* content;
  if (encrypted) {
    scratch = std::make_unique<char[]>(contentLen);
    content = scratch.get();
  } else {
    content = mWriteBuf;
  }

  char* p = content + sizeof(uint32_t);  // leave room for the leading hash word
  if (mHashCount) {
    memcpy(p, mHashArray, mHashCount * sizeof(CacheHash::Hash16_t));
    p += mHashCount * sizeof(CacheHash::Hash16_t);
  }
  mMetaHdr.WriteToBuf(p);
  p += sizeof(CacheFileMetadataHeader);
  memcpy(p, mKey.get(), mKey.Length());
  p += mKey.Length();
  *p = 0;
  p++;
  if (mElementsSize) {
    memcpy(p, mBuf, mElementsSize);
    p += mElementsSize;
  }
  MOZ_ASSERT(uint32_t(p - content) == contentLen);

  LOG(("CacheFileMetadata::WriteMetadata() [this=%p, key=%s, mElementsSize=%d]",
       this, mKey.get(), mElementsSize));
  CacheHash::Hash32_t hash;
  hash = CacheHash::Hash(content + sizeof(uint32_t),
                         contentLen - sizeof(uint32_t));
  NetworkEndian::writeUint32(content, hash);

  // Write the unencrypted trailer: [ formatVersion ][ metaStartOffset | flag ].
  // The version is a clear-text copy of the header version; the high bit of the
  // offset word marks the entry encrypted. For an encrypted entry each chunk is
  // kBlockOverhead larger on disk, so the physical metadata start is past the
  // logical data size. The trailer is written before encrypting so it can be
  // bound as AAD below.
  char* trailer =
      mWriteBuf + contentLen + (encrypted ? CacheCrypto::kBlockOverhead : 0);
  MOZ_ASSERT((aOffset & kEncryptedFlag) == 0);
  NetworkEndian::writeUint32(trailer, mMetaHdr.mVersion);
  NetworkEndian::writeUint32(trailer + sizeof(uint32_t),
                             aOffset | (encrypted ? kEncryptedFlag : 0));

  // Encrypt the whole content into mWriteBuf, exactly like a data chunk. The
  // plaintext trailer (version + offset|flag) is bound as AAD so it cannot
  // be tampered with.
  if (encrypted) {
    rv = crypto->EncryptBlock(CacheCrypto::kMetadataBlockNumber,
                              reinterpret_cast<const uint8_t*>(content),
                              contentLen, reinterpret_cast<uint8_t*>(mWriteBuf),
                              reinterpret_cast<const uint8_t*>(trailer),
                              2 * sizeof(uint32_t));
    if (NS_FAILED(rv)) {
      CacheFileUtils::FreeBuffer(mWriteBuf);
      mWriteBuf = nullptr;
      return rv;
    }
  }

  int64_t writeOffset =
      encrypted ? EncryptedMetaPhysicalOffset(aOffset) : int64_t(aOffset);

  char* writeBuffer = mWriteBuf;
  if (aListener) {
    mListener = aListener;
    rv = CacheFileIOManager::Write(mHandle, writeOffset, writeBuffer, bufSize,
                                   true, true, this);
  } else {
    // We are not going to pass |this| as a callback so the buffer will be
    // released by CacheFileIOManager. Just null out mWriteBuf here.
    mWriteBuf = nullptr;
    rv = CacheFileIOManager::WriteWithoutCallback(
        mHandle, writeOffset, writeBuffer, bufSize, true, true);
  }

  if (NS_FAILED(rv)) {
    LOG(
        ("CacheFileMetadata::WriteMetadata() - CacheFileIOManager::Write() "
         "failed synchronously. [this=%p, rv=0x%08" PRIx32 "]",
         this, static_cast<uint32_t>(rv)));

    mListener = nullptr;
    if (mWriteBuf) {
      CacheFileUtils::FreeBuffer(mWriteBuf);
      mWriteBuf = nullptr;
    }
    NS_ENSURE_SUCCESS(rv, rv);
  }

  DoMemoryReport(MemoryUsage());

  return NS_OK;
}

nsresult CacheFileMetadata::SyncReadMetadata(nsIFile* aFile) {
  LOG(("CacheFileMetadata::SyncReadMetadata() [this=%p]", this));

  MOZ_ASSERT(!mListener);
  MOZ_ASSERT(!mHandle);
  MOZ_ASSERT(!mHashArray);
  MOZ_ASSERT(!mBuf);
  MOZ_ASSERT(!mWriteBuf);
  MOZ_ASSERT(mKey.IsEmpty());

  nsresult rv;

  int64_t fileSize;
  rv = aFile->GetFileSize(&fileSize);
  if (NS_FAILED(rv)) {
    // Don't bloat the console
    return rv;
  }

  PRFileDesc* fd;
  rv = aFile->OpenNSPRFileDesc(PR_RDONLY, 0600, &fd);
  NS_ENSURE_SUCCESS(rv, rv);
  auto closeFd = MakeScopeExit([&fd] { PR_Close(fd); });

  // Read the last two words. The current format's trailer is two words
  // ([version][offset|flag]); older formats have a single offset word. Reading
  // both covers either case (the version word disambiguates).
  if (fileSize < int64_t(2 * sizeof(uint32_t))) {
    return NS_ERROR_FAILURE;
  }
  int64_t offset = PR_Seek64(fd, fileSize - 2 * sizeof(uint32_t), PR_SEEK_SET);
  if (offset == -1) {
    return NS_ERROR_FAILURE;
  }

  uint32_t trailer[2];
  int32_t bytesRead = PR_Read(fd, trailer, 2 * sizeof(uint32_t));
  if (bytesRead != int32_t(2 * sizeof(uint32_t))) {
    return NS_ERROR_FAILURE;
  }

  MetadataTrailer meta =
      DecodeMetadataTrailer(NetworkEndian::readUint32(&trailer[0]),
                            NetworkEndian::readUint32(&trailer[1]));

  if (meta.mPhysOffset > fileSize) {
    return NS_ERROR_FAILURE;
  }

  mBuf = static_cast<char*>(malloc(fileSize - meta.mPhysOffset));
  if (!mBuf) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  mBufSize = fileSize - meta.mPhysOffset;

  DoMemoryReport(MemoryUsage());

  offset = PR_Seek64(fd, meta.mPhysOffset, PR_SEEK_SET);
  if (offset == -1) {
    return NS_ERROR_FAILURE;
  }

  bytesRead = PR_Read(fd, mBuf, mBufSize);
  if (bytesRead != static_cast<int32_t>(mBufSize)) {
    return NS_ERROR_FAILURE;
  }

  rv = NormalizeMetadataBuf(0, meta.mTrailerWords, meta.mEncrypted);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ParseMetadata(meta.mLogicalDataSize, false);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

void CacheFileMetadata::HandleCorruptMetaData() const {
  if (mHandle) {
    CacheFileIOManager::DoomFile(mHandle, nullptr);
  }
}

const char* CacheFileMetadata::GetElement(const char* aKey) {
  const char* data = mBuf;
  const char* limit = mBuf + mElementsSize;

  while (data != limit) {
    size_t maxLen = limit - data;
    size_t keyLen = strnlen(data, maxLen);

    if (keyLen == maxLen ||      // Key isn't null terminated!
        keyLen + 1 == maxLen) {  // There is no value for the key!
      HandleCorruptMetaData();
      return nullptr;
    }

    const char* value = data + keyLen + 1;
    maxLen = limit - value;
    size_t valueLen = strnlen(value, maxLen);
    if (valueLen == maxLen) {  // Value isn't null terminated
      HandleCorruptMetaData();
      return nullptr;
    }

    if (nsCRT::strcasecmp(data, aKey) == 0) {
      LOG(("CacheFileMetadata::GetElement() - Key found [this=%p, key=%s]",
           this, aKey));
      return value;
    }

    // point to next pair
    data += keyLen + valueLen + 2;
  }
  LOG(("CacheFileMetadata::GetElement() - Key not found [this=%p, key=%s]",
       this, aKey));
  return nullptr;
}

nsresult CacheFileMetadata::SetElement(const char* aKey, const char* aValue) {
  LOG(("CacheFileMetadata::SetElement() [this=%p, key=%s, value=%p]", this,
       aKey, aValue));

  mLock->Lock().AssertCurrentThreadOwns();

  nsresult rv;

  const uint32_t keySize = strlen(aKey) + 1;
  char* pos = const_cast<char*>(GetElement(aKey));

  // Storing the value an element already has (or removing one that isn't there)
  // changes nothing, so it must not dirty the metadata.
  if (!aValue ? !pos : (pos && strcmp(pos, aValue) == 0)) {
    return NS_OK;
  }

  MarkDirty();

  if (!aValue) {
    // No value means remove the key/value pair completely, if existing
    if (pos) {
      uint32_t oldValueSize = strlen(pos) + 1;
      uint32_t offset = pos - mBuf;
      uint32_t remainder = mElementsSize - (offset + oldValueSize);

      memmove(pos - keySize, pos + oldValueSize, remainder);
      mElementsSize -= keySize + oldValueSize;
    }
    return NS_OK;
  }

  const uint32_t valueSize = strlen(aValue) + 1;
  uint32_t newSize = mElementsSize + valueSize;
  if (pos) {
    const uint32_t oldValueSize = strlen(pos) + 1;
    const uint32_t offset = pos - mBuf;
    const uint32_t remainder = mElementsSize - (offset + oldValueSize);

    // Update the value in place
    newSize -= oldValueSize;
    rv = EnsureBuffer(newSize);
    if (NS_FAILED(rv)) {
      return rv;
    }

    // Move the remainder to the right place
    pos = mBuf + offset;
    memmove(pos + valueSize, pos + oldValueSize, remainder);
  } else {
    // allocate new meta data element
    newSize += keySize;
    rv = EnsureBuffer(newSize);
    if (NS_FAILED(rv)) {
      return rv;
    }

    // Add after last element
    pos = mBuf + mElementsSize;
    memcpy(pos, aKey, keySize);
    pos += keySize;
  }

  // Update value
  memcpy(pos, aValue, valueSize);
  mElementsSize = newSize;

  return NS_OK;
}

void CacheFileMetadata::Visit(nsICacheEntryMetaDataVisitor* aVisitor) {
  const char* data = mBuf;
  const char* limit = mBuf + mElementsSize;

  while (data < limit) {
    // Point to the value part
    const char* value = data + strlen(data) + 1;
    MOZ_ASSERT(value < limit, "Metadata elements corrupted");

    aVisitor->OnMetaDataElement(data, value);

    // Skip value part
    data = value + strlen(value) + 1;
  }

  MOZ_ASSERT(data == limit, "Metadata elements corrupted");
}

CacheHash::Hash16_t CacheFileMetadata::GetHash(uint32_t aIndex) {
  mLock->Lock().AssertCurrentThreadOwns();

  MOZ_ASSERT(aIndex < mHashCount);
  return NetworkEndian::readUint16(&mHashArray[aIndex]);
}

nsresult CacheFileMetadata::SetHash(uint32_t aIndex,
                                    CacheHash::Hash16_t aHash) {
  LOG(("CacheFileMetadata::SetHash() [this=%p, idx=%d, hash=%x]", this, aIndex,
       aHash));

  mLock->Lock().AssertCurrentThreadOwns();

  MarkDirty();

  MOZ_ASSERT(aIndex <= mHashCount);

  if (aIndex > mHashCount) {
    return NS_ERROR_INVALID_ARG;
  }
  if (aIndex == mHashCount) {
    if ((aIndex + 1) * sizeof(CacheHash::Hash16_t) > mHashArraySize) {
      // reallocate hash array buffer
      if (mHashArraySize == 0) {
        mHashArraySize = kInitialHashArraySize * sizeof(CacheHash::Hash16_t);
      } else {
        mHashArraySize *= 2;
      }
      mHashArray = static_cast<CacheHash::Hash16_t*>(
          moz_xrealloc(mHashArray, mHashArraySize));
    }

    mHashCount++;
  }

  NetworkEndian::writeUint16(&mHashArray[aIndex], aHash);

  DoMemoryReport(MemoryUsage());

  return NS_OK;
}

nsresult CacheFileMetadata::RemoveHash(uint32_t aIndex) {
  LOG(("CacheFileMetadata::RemoveHash() [this=%p, idx=%d]", this, aIndex));

  mLock->Lock().AssertCurrentThreadOwns();

  MarkDirty();

  MOZ_ASSERT((aIndex + 1) == mHashCount, "Can remove only last hash!");

  if (aIndex + 1 != mHashCount) {
    return NS_ERROR_INVALID_ARG;
  }

  mHashCount--;
  return NS_OK;
}

void CacheFileMetadata::AddFlags(uint32_t aFlags) {
  MarkDirty(false);
  mMetaHdr.mFlags |= aFlags;
}

void CacheFileMetadata::RemoveFlags(uint32_t aFlags) {
  MarkDirty(false);
  mMetaHdr.mFlags &= ~aFlags;
}

void CacheFileMetadata::SetExpirationTime(uint32_t aExpirationTime) {
  LOG(("CacheFileMetadata::SetExpirationTime() [this=%p, expirationTime=%d]",
       this, aExpirationTime));

  MarkDirty(false);
  mMetaHdr.mExpirationTime = aExpirationTime;
}

void CacheFileMetadata::SetFrecency(uint32_t aFrecency) {
  LOG(("CacheFileMetadata::SetFrecency() [this=%p, frecency=%f]", this,
       (double)aFrecency));

  // Deliberately do not mark the metadata dirty. Frecency is persisted through
  // the central index (the authoritative store for eviction); rewriting the
  // entry file on every read hit would generate needless IO and trigger
  // on-access anti-malware scans. The updated value is still written out for
  // free whenever the metadata is flushed for some other reason.
  mMetaHdr.mFrecency = aFrecency;
}

void CacheFileMetadata::OnFetched() {
  // Likewise stat-only: keep lastFetched/fetchCount current in memory without
  // dirtying the file. They may be approximate across a restart.
  mMetaHdr.mLastFetched = NOW_SECONDS();
  ++mMetaHdr.mFetchCount;
}

void CacheFileMetadata::MarkDirty(bool aUpdateLastModified) {
  mIsDirty = true;
  if (aUpdateLastModified) {
    mMetaHdr.mLastModified = NOW_SECONDS();
  }
}

nsresult CacheFileMetadata::OnFileOpened(CacheFileHandle* aHandle,
                                         nsresult aResult) {
  MOZ_CRASH("CacheFileMetadata::OnFileOpened should not be called!");
  return NS_ERROR_UNEXPECTED;
}

nsresult CacheFileMetadata::OnDataWritten(CacheFileHandle* aHandle,
                                          const char* aBuf, nsresult aResult) {
  LOG(
      ("CacheFileMetadata::OnDataWritten() [this=%p, handle=%p, "
       "result=0x%08" PRIx32 "]",
       this, aHandle, static_cast<uint32_t>(aResult)));

  nsCOMPtr<CacheFileMetadataListener> listener;
  {
    MutexAutoLock lock(mLock->Lock());

    MOZ_ASSERT(mListener);
    MOZ_ASSERT(mWriteBuf);

    CacheFileUtils::FreeBuffer(mWriteBuf);
    mWriteBuf = nullptr;

    mListener.swap(listener);
    DoMemoryReport(MemoryUsage());
  }

  listener->OnMetadataWritten(aResult);

  return NS_OK;
}

nsresult CacheFileMetadata::OnDataRead(CacheFileHandle* aHandle, char* aBuf,
                                       nsresult aResult) {
  LOG((
      "CacheFileMetadata::OnDataRead() [this=%p, handle=%p, result=0x%08" PRIx32
      "]",
      this, aHandle, static_cast<uint32_t>(aResult)));

  MOZ_ASSERT(mListener);

  nsresult rv;
  nsCOMPtr<CacheFileMetadataListener> listener;

  auto notifyListenerOutsideLock = mozilla::MakeScopeExit([&listener] {
    if (listener) {
      listener->OnMetadataRead(NS_OK);
    }
  });

  MutexAutoLock lock(mLock->Lock());

  if (NS_FAILED(aResult)) {
    LOG(
        ("CacheFileMetadata::OnDataRead() - CacheFileIOManager::Read() failed"
         ", creating empty metadata. [this=%p, rv=0x%08" PRIx32 "]",
         this, static_cast<uint32_t>(aResult)));

    InitEmptyMetadata();

    mListener.swap(listener);
    return NS_OK;
  }

#ifndef ANDROID
  mozilla::TimeStamp readEnd = mozilla::TimeStamp::Now();
  if (mFirstRead) {
    mozilla::glean::networking::cache_metadata_first_read_time
        .AccumulateRawDuration(readEnd - mReadStart);
  } else {
    mozilla::glean::networking::cache_metadata_second_read_time
        .AccumulateRawDuration(readEnd - mReadStart);
  }
#endif

  // Decode the trailer (always within this first tail read). The version word
  // is absent in old single-offset-word entries; pass 0 when the buffer is too
  // small to hold it.
  uint32_t versionWord =
      mBufSize >= 2 * sizeof(uint32_t)
          ? NetworkEndian::readUint32(mBuf + mBufSize - 2 * sizeof(uint32_t))
          : 0;
  MetadataTrailer meta = DecodeMetadataTrailer(
      versionWord,
      NetworkEndian::readUint32(mBuf + mBufSize - sizeof(uint32_t)));
  bool encrypted = meta.mEncrypted;
  uint32_t logicalDataSize = meta.mLogicalDataSize;
  uint32_t realOffset = meta.mPhysOffset;

  int64_t size = mHandle->FileSize();
  MOZ_ASSERT(size != -1);

  if (size < int64_t(mBufSize)) {
    LOG(
        ("CacheFileMetadata::OnDataRead() - File shrank during metadata read, "
         "creating empty metadata. [this=%p, bufSize=%u, size=%" PRId64 "]",
         this, mBufSize, size));

    InitEmptyMetadata();

    mListener.swap(listener);
    return NS_OK;
  }

  if (realOffset >= size) {
    LOG(
        ("CacheFileMetadata::OnDataRead() - Invalid realOffset, creating "
         "empty metadata. [this=%p, realOffset=%u, size=%" PRId64 "]",
         this, realOffset, size));

    InitEmptyMetadata();

    mListener.swap(listener);
    return NS_OK;
  }

  uint32_t maxHashCount = size / kChunkSize;
  uint32_t maxMetadataSize = CalcMetadataSize(kMaxElementsSize, maxHashCount);
  if (size - realOffset > maxMetadataSize) {
    LOG(
        ("CacheFileMetadata::OnDataRead() - Invalid realOffset, metadata would "
         "be too big, creating empty metadata. [this=%p, realOffset=%u, "
         "maxMetadataSize=%u, size=%" PRId64 "]",
         this, realOffset, maxMetadataSize, size));

    InitEmptyMetadata();

    mListener.swap(listener);
    return NS_OK;
  }

  uint32_t usedOffset = size - mBufSize;

  if (realOffset < usedOffset) {
    uint32_t missing = usedOffset - realOffset;
    // we need to read more data
    char* newBuf = static_cast<char*>(realloc(mBuf, mBufSize + missing));
    if (!newBuf) {
      LOG(
          ("CacheFileMetadata::OnDataRead() - Error allocating %d more bytes "
           "for the missing part of the metadata, creating empty metadata. "
           "[this=%p]",
           missing, this));

      InitEmptyMetadata();

      mListener.swap(listener);
      return NS_OK;
    }

    mBuf = newBuf;
    memmove(mBuf + missing, mBuf, mBufSize);
    mBufSize += missing;

    DoMemoryReport(MemoryUsage());

    LOG(
        ("CacheFileMetadata::OnDataRead() - We need to read %d more bytes to "
         "have full metadata. [this=%p]",
         missing, this));

    mFirstRead = false;
    mReadStart = mozilla::TimeStamp::Now();
    rv = CacheFileIOManager::Read(mHandle, realOffset, mBuf, missing, this);
    if (NS_FAILED(rv)) {
      LOG(
          ("CacheFileMetadata::OnDataRead() - CacheFileIOManager::Read() "
           "failed synchronously, creating empty metadata. [this=%p, "
           "rv=0x%08" PRIx32 "]",
           this, static_cast<uint32_t>(rv)));

      InitEmptyMetadata();

      mListener.swap(listener);
      return NS_OK;
    }

    return NS_OK;
  }

#ifndef ANDROID
  mozilla::glean::networking::cache_metadata_size.Accumulate(size - realOffset);
#endif

  // We have all data according to offset information at the end of the entry.
  // Strip the trailer (and decrypt, if needed) so mBuf holds plaintext content,
  // then parse it.
  rv = NormalizeMetadataBuf(realOffset - usedOffset, meta.mTrailerWords,
                            encrypted);
  if (NS_SUCCEEDED(rv)) {
    rv = ParseMetadata(logicalDataSize, true);
  }
  if (NS_FAILED(rv)) {
    LOG(
        ("CacheFileMetadata::OnDataRead() - Error parsing metadata, creating "
         "empty metadata. [this=%p]",
         this));
    InitEmptyMetadata();
  } else {
    // Shrink elements buffer.
    mBuf = static_cast<char*>(moz_xrealloc(mBuf, mElementsSize));
    mBufSize = mElementsSize;

    // There is usually no or just one call to SetMetadataElement() when the
    // metadata is parsed from disk. Avoid allocating power of two sized buffer
    // which we do in case of newly created metadata.
    mAllocExactSize = true;
  }

  mListener.swap(listener);

  return NS_OK;
}

nsresult CacheFileMetadata::OnFileDoomed(CacheFileHandle* aHandle,
                                         nsresult aResult) {
  MOZ_CRASH("CacheFileMetadata::OnFileDoomed should not be called!");
  return NS_ERROR_UNEXPECTED;
}

nsresult CacheFileMetadata::OnEOFSet(CacheFileHandle* aHandle,
                                     nsresult aResult) {
  MOZ_CRASH("CacheFileMetadata::OnEOFSet should not be called!");
  return NS_ERROR_UNEXPECTED;
}

nsresult CacheFileMetadata::OnFileRenamed(CacheFileHandle* aHandle,
                                          nsresult aResult) {
  MOZ_CRASH("CacheFileMetadata::OnFileRenamed should not be called!");
  return NS_ERROR_UNEXPECTED;
}

void CacheFileMetadata::InitEmptyMetadata() {
  if (mBuf) {
    CacheFileUtils::FreeBuffer(mBuf);
    mBuf = nullptr;
    mBufSize = 0;
  }
  mAllocExactSize = false;
  mOffset = 0;
  mMetaHdr.mVersion = kCacheEntryVersion;
  mMetaHdr.mFetchCount = 0;
  mMetaHdr.mExpirationTime = nsICacheEntry::NO_EXPIRATION_TIME;
  mMetaHdr.mKeySize = mKey.Length();

  // Deliberately not touching the "kCacheEntryIsPinned" flag.

  DoMemoryReport(MemoryUsage());

  // We're creating a new entry. If there is any old data truncate it.
  if (mHandle) {
    mHandle->SetPinned(Pinned());
    // We can pronounce the handle as invalid now, because it simply
    // doesn't have the correct metadata.  This will cause IO operations
    // be bypassed during shutdown (mainly dooming it, when a channel
    // is canceled by closing the window.)
    mHandle->SetInvalid();
    if (mHandle->FileExists() && mHandle->FileSize()) {
      CacheFileIOManager::TruncateSeekSetEOF(mHandle, 0, 0, nullptr);
    }
  }
}

nsresult CacheFileMetadata::NormalizeMetadataBuf(uint32_t aBlockOffset,
                                                 uint32_t aTrailerWords,
                                                 bool aEncrypted) {
  // Fail closed: when disk-cache encryption is active, every on-disk entry must
  // be encrypted.
  if (!aEncrypted && CacheCrypto::IsActive()) {
    LOG(
        ("CacheFileMetadata::NormalizeMetadataBuf() - plaintext entry while "
         "encryption is enabled, rejecting [this=%p]",
         this));
    return NS_ERROR_FILE_CORRUPTED;
  }

  uint32_t trailerLen = aTrailerWords * sizeof(uint32_t);
  if (mBufSize < aBlockOffset + trailerLen) {
    return NS_ERROR_FILE_CORRUPTED;
  }
  // The metadata block is everything between the leading bytes we over-read and
  // the trailing offset word(s).
  uint32_t blockLen = mBufSize - aBlockOffset - trailerLen;

  if (aEncrypted) {
    RefPtr<CacheCrypto> crypto = CacheCrypto::GetInstanceOrNull();
    if (!crypto) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    if (blockLen < CacheCrypto::kBlockOverhead) {
      return NS_ERROR_FILE_CORRUPTED;
    }
    // The block is [ciphertext][tag][nonce]; decrypt it into a fresh plaintext
    // buffer exactly like a data chunk. A failed AEAD check means corruption.
    uint32_t plaintextLen = blockLen - CacheCrypto::kBlockOverhead;
    char* plain = static_cast<char*>(malloc(plaintextLen ? plaintextLen : 1));
    if (!plain) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    // Authenticate the plaintext trailer (version + offset|flag) by passing it
    // as AAD: tampering with the format version, encryption flag or data size
    // fails the AEAD tag below.
    nsresult rv = crypto->DecryptBlock(
        CacheCrypto::kMetadataBlockNumber,
        reinterpret_cast<uint8_t*>(mBuf + aBlockOffset), plaintextLen,
        reinterpret_cast<uint8_t*>(plain),
        reinterpret_cast<uint8_t*>(mBuf + aBlockOffset + blockLen), trailerLen);
    if (NS_FAILED(rv)) {
      free(plain);
      return NS_ERROR_FILE_CORRUPTED;
    }
    CacheFileUtils::FreeBuffer(mBuf);
    mBuf = plain;
    mBufSize = plaintextLen;
  } else {
    if (aBlockOffset) {
      memmove(mBuf, mBuf + aBlockOffset, blockLen);
    }
    mBufSize = blockLen;
  }

  DoMemoryReport(MemoryUsage());
  return NS_OK;
}

nsresult CacheFileMetadata::ParseMetadata(uint32_t aLogicalDataSize,
                                          bool aHaveKey) {
  LOG(
      ("CacheFileMetadata::ParseMetadata() [this=%p, logicalDataSize=%u, "
       "haveKey=%u]",
       this, aLogicalDataSize, aHaveKey));

  nsresult rv;

  uint32_t hashesOffset = sizeof(uint32_t);
  uint32_t hashCount = aLogicalDataSize / kChunkSize;
  if (aLogicalDataSize % kChunkSize) {
    hashCount++;
  }
  uint32_t hashesLen = hashCount * sizeof(CacheHash::Hash16_t);
  uint32_t hdrOffset = hashesOffset + hashesLen;

  // mBuf holds exactly the plaintext content (no trailer); metaposOffset is its
  // end. Read the version (the leading header field, present in every version)
  // before computing where the key begins. Require the version field to be
  // within the buffer.
  uint32_t metaposOffset = mBufSize;
  if (hdrOffset + sizeof(uint32_t) > metaposOffset) {
    LOG((
        "CacheFileMetadata::ParseMetadata() - Buffer too small to hold header! "
        "[this=%p]",
        this));
    return NS_ERROR_FILE_CORRUPTED;
  }
  uint32_t version = NetworkEndian::readUint32(mBuf + hdrOffset);

  // The header's on-disk size depends on the version: v1 lacked mFlags.
  uint32_t onDiskHdrSize;
  switch (version) {
    case 1:
      onDiskHdrSize = sizeof(CacheFileMetadataHeader) - sizeof(mMetaHdr.mFlags);
      break;
    case 2:
      // Version 2 just lacks the ability to store alternative data.
      [[fallthrough]];
    case 3:
      [[fallthrough]];
    case kCacheEntryVersion:
      onDiskHdrSize = sizeof(CacheFileMetadataHeader);
      break;
    default:
      LOG(
          ("CacheFileMetadata::ParseMetadata() - Not a version we understand "
           "to. [version=0x%x, this=%p]",
           version, this));
      return NS_ERROR_UNEXPECTED;
  }

  uint32_t keyOffset = hdrOffset + onDiskHdrSize;

  LOG(
      ("CacheFileMetadata::ParseMetadata() [this=%p]\n  metaposOffset=%d\n  "
       "hashesOffset=%d\n  hashCount=%d\n  hashesLen=%d\n  hdfOffset=%d\n  "
       "keyOffset=%d\n",
       this, metaposOffset, hashesOffset, hashCount, hashesLen, hdrOffset,
       keyOffset));

  if (keyOffset > metaposOffset) {
    LOG(("CacheFileMetadata::ParseMetadata() - Wrong keyOffset! [this=%p]",
         this));
    return NS_ERROR_FILE_CORRUPTED;
  }

  mMetaHdr.ReadFromBuf(mBuf + hdrOffset);

  // Update the version stored in the header to make writes
  // store the header in the current version form.
  mMetaHdr.mVersion = kCacheEntryVersion;

  uint32_t elementsOffset = mMetaHdr.mKeySize + keyOffset + 1;

  if (elementsOffset > metaposOffset) {
    LOG(
        ("CacheFileMetadata::ParseMetadata() - Wrong elementsOffset %d "
         "[this=%p]",
         elementsOffset, this));
    return NS_ERROR_FILE_CORRUPTED;
  }

  // Verify the metadata hash over the content (the leading hash word excluded).
  CacheHash::Hash32_t hashComputed, hashExpected;
  hashComputed =
      CacheHash::Hash(mBuf + hashesOffset, metaposOffset - hashesOffset);
  hashExpected = NetworkEndian::readUint32(mBuf);

  if (hashComputed != hashExpected) {
    LOG(
        ("CacheFileMetadata::ParseMetadata() - Metadata hash mismatch! Hash of "
         "the metadata is %x, hash in file is %x [this=%p]",
         hashComputed, hashExpected, this));
    return NS_ERROR_FILE_CORRUPTED;
  }

  // check that key ends with \0
  if (mBuf[elementsOffset - 1] != 0) {
    LOG(
        ("CacheFileMetadata::ParseMetadata() - Elements not null terminated. "
         "[this=%p]",
         this));
    return NS_ERROR_FILE_CORRUPTED;
  }

  if (!aHaveKey) {
    // get the key form metadata
    mKey.Assign(mBuf + keyOffset, mMetaHdr.mKeySize);

    rv = ParseKey(mKey);
    if (NS_FAILED(rv)) return rv;
  } else {
    if (mMetaHdr.mKeySize != mKey.Length()) {
      LOG(
          ("CacheFileMetadata::ParseMetadata() - Key collision (1), key=%s "
           "[this=%p]",
           nsCString(mBuf + keyOffset, mMetaHdr.mKeySize).get(), this));
      return NS_ERROR_FILE_CORRUPTED;
    }

    if (memcmp(mKey.get(), mBuf + keyOffset, mKey.Length()) != 0) {
      LOG(
          ("CacheFileMetadata::ParseMetadata() - Key collision (2), key=%s "
           "[this=%p]",
           nsCString(mBuf + keyOffset, mMetaHdr.mKeySize).get(), this));
      return NS_ERROR_FILE_CORRUPTED;
    }
  }

  // check elements
  rv = CheckElements(mBuf + elementsOffset, metaposOffset - elementsOffset);
  if (NS_FAILED(rv)) return rv;

  if (mHandle) {
    if (!mHandle->SetPinned(Pinned())) {
      LOG(
          ("CacheFileMetadata::ParseMetadata() - handle was doomed for this "
           "pinning state, truncate the file [this=%p, pinned=%d]",
           this, Pinned()));
      return NS_ERROR_FILE_CORRUPTED;
    }
  }

  mHashArraySize = hashesLen;
  mHashCount = hashCount;
  if (mHashArraySize) {
    mHashArray = static_cast<CacheHash::Hash16_t*>(moz_xmalloc(mHashArraySize));
    memcpy(mHashArray, mBuf + hashesOffset, mHashArraySize);
  }

  // Parsing doesn't change the entry, only the header version upgraded above,
  // which needs writing out but isn't a content change (so no mLastModified).
  if (version != kCacheEntryVersion) {
    MarkDirty(false);
  }

  mElementsSize = metaposOffset - elementsOffset;
  memmove(mBuf, mBuf + elementsOffset, mElementsSize);
  // mOffset is the logical data size, not the physical metadata position.
  mOffset = aLogicalDataSize;

  DoMemoryReport(MemoryUsage());

  return NS_OK;
}

nsresult CacheFileMetadata::CheckElements(const char* aBuf, uint32_t aSize) {
  if (aSize) {
    // Check if the metadata ends with a zero byte.
    if (aBuf[aSize - 1] != 0) {
      NS_ERROR("Metadata elements are not null terminated");
      LOG(
          ("CacheFileMetadata::CheckElements() - Elements are not null "
           "terminated. [this=%p]",
           this));
      return NS_ERROR_FILE_CORRUPTED;
    }
    // Check that there are an even number of zero bytes
    // to match the pattern { key \0 value \0 }
    bool odd = false;
    for (uint32_t i = 0; i < aSize; i++) {
      if (aBuf[i] == 0) odd = !odd;
    }
    if (odd) {
      NS_ERROR("Metadata elements are malformed");
      LOG(
          ("CacheFileMetadata::CheckElements() - Elements are malformed. "
           "[this=%p]",
           this));
      return NS_ERROR_FILE_CORRUPTED;
    }
  }
  return NS_OK;
}

nsresult CacheFileMetadata::EnsureBuffer(uint32_t aSize) {
  if (aSize > kMaxElementsSize) {
    return NS_ERROR_FAILURE;
  }

  if (mBufSize < aSize) {
    if (mAllocExactSize) {
      // If this is not the only allocation, use power of two for following
      // allocations.
      mAllocExactSize = false;
    } else {
      // find smallest power of 2 greater than or equal to aSize
      --aSize;
      aSize |= aSize >> 1;
      aSize |= aSize >> 2;
      aSize |= aSize >> 4;
      aSize |= aSize >> 8;
      aSize |= aSize >> 16;
      ++aSize;
    }

    if (aSize < kInitialBufSize) {
      aSize = kInitialBufSize;
    }

    char* newBuf = static_cast<char*>(realloc(mBuf, aSize));
    if (!newBuf) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    mBufSize = aSize;
    mBuf = newBuf;

    DoMemoryReport(MemoryUsage());
  }

  return NS_OK;
}

nsresult CacheFileMetadata::ParseKey(const nsACString& aKey) {
  nsCOMPtr<nsILoadContextInfo> info = CacheFileUtils::ParseKey(aKey);
  NS_ENSURE_TRUE(info, NS_ERROR_FAILURE);

  mAnonymous = info->IsAnonymous();
  mOriginAttributes = *info->OriginAttributesPtr();

  return NS_OK;
}

// Memory reporting

size_t CacheFileMetadata::SizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  size_t n = 0;
  // mHandle reported via CacheFileIOManager.
  n += mKey.SizeOfExcludingThisIfUnshared(mallocSizeOf);
  n += mallocSizeOf(mHashArray);
  n += mallocSizeOf(mBuf);
  // Ignore mWriteBuf, it's not safe to access it when metadata is being
  // written and it's null otherwise.
  // mListener is usually the owning CacheFile.

  return n;
}

size_t CacheFileMetadata::SizeOfIncludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  return mallocSizeOf(this) + SizeOfExcludingThis(mallocSizeOf);
}

}  // namespace mozilla::net
