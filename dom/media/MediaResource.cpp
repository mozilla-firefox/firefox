/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaResource.h"

#include <bit>

#include "mozilla/ErrorNames.h"
#include "mozilla/Logging.h"
#include "mozilla/SchedulerGroup.h"

using mozilla::media::TimeUnit;

#undef ILOG

mozilla::LazyLogModule gMediaResourceIndexLog("MediaResourceIndex");
// Debug logging macro with object pointer and class name.
#define ILOG(msg, ...)                                                 \
  DDMOZ_LOG_FMT(gMediaResourceIndexLog, mozilla::LogLevel::Debug, msg, \
                ##__VA_ARGS__)

namespace mozilla {

static const uint32_t kMediaResourceIndexCacheSize = 8192;
static_assert(std::has_single_bit(kMediaResourceIndexCacheSize),
              "kMediaResourceIndexCacheSize cache size must be a power of 2");

MediaResourceIndex::MediaResourceIndex(MediaResource* aResource)
    : mResource(aResource),
      mOffset(0),
      mCacheBlockSize(
          aResource->ShouldCacheReads() ? kMediaResourceIndexCacheSize : 0),
      mCachedOffset(0),
      mCachedBytes(0),
      mCachedBlock(std::make_unique<char[]>(mCacheBlockSize)) {
  DDLINKCHILD("resource", aResource);
}

nsresult MediaResourceIndex::Read(char* aBuffer, uint32_t aCount,
                                  uint32_t* aBytes) {
  NS_ASSERTION(!NS_IsMainThread(), "Don't call on main thread");

  // We purposefuly don't check that we may attempt to read past
  // mResource->GetLength() as the resource's length may change over time.

  nsresult rv = ReadAt(mOffset, aBuffer, aCount, aBytes);
  if (NS_FAILED(rv)) {
    return rv;
  }
  mOffset += *aBytes;
  if (mOffset < 0) {
    // Very unlikely overflow; just return to position 0.
    mOffset = 0;
  }
  return NS_OK;
}

static nsCString ResultName(nsresult aResult) {
  nsCString name;
  GetErrorName(aResult, name);
  return name;
}

nsresult MediaResourceIndex::ReadAt(int64_t aOffset, char* aBuffer,
                                    uint32_t aCount, uint32_t* aBytes) {
  if (mCacheBlockSize == 0) {
    return UncachedReadAt(aOffset, aBuffer, aCount, aBytes);
  }

  *aBytes = 0;

  if (aCount == 0) {
    return NS_OK;
  }

  if (aOffset < 0 || aOffset > INT64_MAX - int64_t(aCount)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  const int64_t endOffset = aOffset + aCount;

  const int64_t lastBlockOffset = CacheOffsetContaining(endOffset - 1);

  if (mCachedBytes != 0 && mCachedOffset + mCachedBytes >= aOffset &&
      mCachedOffset < endOffset) {
    // There is data in the cache that is not completely before aOffset and not
    // completely after endOffset, so it could be usable (with potential
    // top-up).
    if (aOffset < mCachedOffset) {
      // We need to read before the cached data.
      const uint32_t toRead = uint32_t(mCachedOffset - aOffset);
      MOZ_ASSERT(toRead > 0);
      MOZ_ASSERT(toRead < aCount);
      uint32_t read = 0;
      nsresult rv = UncachedReadAt(aOffset, aBuffer, toRead, &read);
      if (NS_FAILED(rv)) {
        ILOG("ReadAt({}@{}) uncached read before cache -> {}, {}", aCount,
             aOffset, ResultName(rv).get(), *aBytes);
        return rv;
      }
      *aBytes = read;
      if (read < toRead) {
        // Could not read everything we wanted, we're done.
        ILOG("ReadAt({}@{}) uncached read before cache, incomplete -> OK, {}",
             aCount, aOffset, *aBytes);
        return NS_OK;
      }
      ILOG("ReadAt({}@{}) uncached read before cache: {}, remaining: {}@{}...",
           aCount, aOffset, read, aCount - read, aOffset + read);
      aOffset += read;
      aBuffer += read;
      aCount -= read;
      // We should have reached the cache.
      MOZ_ASSERT(aOffset == mCachedOffset);
    }
    MOZ_ASSERT(aOffset >= mCachedOffset);

    // We've reached our cache.
    const uint32_t toCopy =
        std::min(aCount, uint32_t(mCachedOffset + mCachedBytes - aOffset));
    // Note that we could in fact be just after the last byte of the cache, in
    // which case we can't actually read from it! (But we will top-up next.)
    if (toCopy != 0) {
      memcpy(aBuffer, &mCachedBlock[IndexInCache(aOffset)], toCopy);
      *aBytes += toCopy;
      aCount -= toCopy;
      if (aCount == 0) {
        // All done!
        ILOG(
            "ReadAt({}@{}) copied everything ({}) from cache({}@{}) :-D -> "
            "OK, {}",
            aCount, aOffset, toCopy, mCachedBytes, mCachedOffset, *aBytes);
        return NS_OK;
      }
      aOffset += toCopy;
      aBuffer += toCopy;
      ILOG("ReadAt({}@{}) copied {} from cache({}@{}) :-), remaining: {}@{}...",
           aCount + toCopy, aOffset - toCopy, toCopy, mCachedBytes,
           mCachedOffset, aCount, aOffset);
    }

    if (aOffset - 1 >= lastBlockOffset) {
      // We were already reading cached data from the last block, we need more
      // from it -> try to top-up, read what we can, and we'll be done.
      MOZ_ASSERT(aOffset == mCachedOffset + mCachedBytes);
      MOZ_ASSERT(endOffset <= lastBlockOffset + mCacheBlockSize);
      return CacheOrReadAt(aOffset, aBuffer, aCount, aBytes);
    }

    // We were not in the last block (but we may just have crossed the line now)
    MOZ_ASSERT(aOffset <= lastBlockOffset);
    // Continue below...
  } else if (aOffset >= lastBlockOffset) {
    // There was nothing we could get from the cache.
    // But we're already in the last block -> Cache or read what we can.
    // Make sure to invalidate the cache first.
    mCachedBytes = 0;
    return CacheOrReadAt(aOffset, aBuffer, aCount, aBytes);
  }

  // If we're here, either there was nothing usable in the cache, or we've just
  // read what was in the cache but there's still more to read.

  if (aOffset < lastBlockOffset) {
    // We need to read before the last block.
    // Start with an uncached read up to the last block.
    const uint32_t toRead = uint32_t(lastBlockOffset - aOffset);
    MOZ_ASSERT(toRead > 0);
    MOZ_ASSERT(toRead < aCount);
    uint32_t read = 0;
    nsresult rv = UncachedReadAt(aOffset, aBuffer, toRead, &read);
    if (NS_FAILED(rv)) {
      ILOG("ReadAt({}@{}) uncached read before last block failed -> {}, {}",
           aCount, aOffset, ResultName(rv).get(), *aBytes);
      return rv;
    }
    if (read == 0) {
      ILOG("ReadAt({}@{}) uncached read 0 before last block -> OK, {}", aCount,
           aOffset, *aBytes);
      return NS_OK;
    }
    *aBytes += read;
    if (read < toRead) {
      // Could not read everything we wanted, we're done.
      ILOG(
          "ReadAt({}@{}) uncached read before last block, incomplete -> OK, {}",
          aCount, aOffset, *aBytes);
      return NS_OK;
    }
    ILOG("ReadAt({}@{}) read {} before last block, remaining: {}@{}...", aCount,
         aOffset, read, aCount - read, aOffset + read);
    aOffset += read;
    aBuffer += read;
    aCount -= read;
  }

  // We should just have reached the start of the last block.
  MOZ_ASSERT(aOffset == lastBlockOffset);
  MOZ_ASSERT(aCount <= mCacheBlockSize);
  // Make sure to invalidate the cache first.
  mCachedBytes = 0;
  return CacheOrReadAt(aOffset, aBuffer, aCount, aBytes);
}

nsresult MediaResourceIndex::CacheOrReadAt(int64_t aOffset, char* aBuffer,
                                           uint32_t aCount, uint32_t* aBytes) {
  // We should be here because there is more data to read.
  MOZ_ASSERT(aCount > 0);
  // We should be in the last block, so we shouldn't try to read past it.
  MOZ_ASSERT(IndexInCache(aOffset) + aCount <= mCacheBlockSize);

  const int64_t length = GetLength();
  // If length is unknown (-1), look at resource-cached data.
  // If length is known and equal or greater than requested, also look at
  // resource-cached data.
  // Otherwise, if length is known but same, or less than(!?), requested, don't
  // attempt to access resource-cached data, as we're not expecting it to ever
  // be greater than the length.
  if (length < 0 || length >= aOffset + aCount) {
    // Is there cached data covering at least the requested range?
    const int64_t cachedDataEnd = mResource->GetCachedDataEnd(aOffset);
    if (cachedDataEnd >= aOffset + aCount) {
      // Try to read as much resource-cached data as can fill our local cache.
      // Assume we can read as much as is cached without blocking.
      const uint32_t cacheIndex = IndexInCache(aOffset);
      const uint32_t toRead = uint32_t(std::min(
          cachedDataEnd - aOffset, int64_t(mCacheBlockSize - cacheIndex)));
      MOZ_ASSERT(toRead >= aCount);
      uint32_t read = 0;
      // We would like `toRead` if possible, but ok with at least `aCount`.
      nsresult rv = UncachedRangedReadAt(aOffset, &mCachedBlock[cacheIndex],
                                         aCount, toRead - aCount, &read);
      if (NS_SUCCEEDED(rv)) {
        if (read == 0) {
          ILOG(
              "ReadAt({}@{}) - UncachedRangedReadAt({}..{}@{}) to top-up "
              "succeeded but read nothing -> OK anyway",
              aCount, aOffset, aCount, toRead, aOffset);
          // Couldn't actually read anything, but didn't error out, so count
          // that as success.
          return NS_OK;
        }
        if (mCachedOffset + mCachedBytes == aOffset) {
          // We were topping-up the cache, just update its size.
          ILOG(
              "ReadAt({}@{}) - UncachedRangedReadAt({}..{}@{}) to top-up "
              "succeeded to read {}...",
              aCount, aOffset, aCount, toRead, aOffset, read);
          mCachedBytes += read;
        } else {
          // We were filling the cache from scratch, save new cache information.
          ILOG(
              "ReadAt({}@{}) - UncachedRangedReadAt({}..{}@{}) to fill cache "
              "succeeded to read {}...",
              aCount, aOffset, aCount, toRead, aOffset, read);
          mCachedOffset = aOffset;
          mCachedBytes = read;
        }
        // Copy relevant part into output.
        uint32_t toCopy = std::min(aCount, read);
        memcpy(aBuffer, &mCachedBlock[cacheIndex], toCopy);
        *aBytes += toCopy;
        ILOG("ReadAt({}@{}) - copied {}@{} -> OK, {}", aCount, aOffset, toCopy,
             aOffset, *aBytes);
        // We may not have read all that was requested, but we got everything
        // we could get, so we're done.
        return NS_OK;
      }
      ILOG(
          "ReadAt({}@{}) - UncachedRangedReadAt({}..{}@{}) failed: {}, will "
          "fallback to blocking read...",
          aCount, aOffset, aCount, toRead, aOffset, ResultName(rv).get());
      // Failure during reading. Note that this may be due to the cache
      // changing between `GetCachedDataEnd` and `ReadAt`, so it's not
      // totally unexpected, just hopefully rare; but we do need to handle it.

      // Invalidate part of cache that may have been partially overridden.
      if (mCachedOffset + mCachedBytes == aOffset) {
        // We were topping-up the cache, just keep the old untouched data.
        // (i.e., nothing to do here.)
      } else {
        // We were filling the cache from scratch, invalidate cache.
        mCachedBytes = 0;
      }
    } else {
      ILOG("ReadAt({}@{}) - no cached data, will fallback to blocking read...",
           aCount, aOffset);
    }
  } else {
    ILOG(
        "ReadAt({}@{}) - length is {} ({}), will fallback to blocking read as "
        "the caller requested...",
        aCount, aOffset, length, length < 0 ? "unknown" : "too short!");
  }
  uint32_t read = 0;
  nsresult rv = UncachedReadAt(aOffset, aBuffer, aCount, &read);
  if (NS_SUCCEEDED(rv)) {
    *aBytes += read;
    ILOG("ReadAt({}@{}) - fallback uncached read got {} bytes -> {}, {}",
         aCount, aOffset, read, ResultName(rv).get(), *aBytes);
  } else {
    ILOG("ReadAt({}@{}) - fallback uncached read failed -> {}, {}", aCount,
         aOffset, ResultName(rv).get(), *aBytes);
  }
  return rv;
}

nsresult MediaResourceIndex::UncachedReadAt(int64_t aOffset, char* aBuffer,
                                            uint32_t aCount,
                                            uint32_t* aBytes) const {
  if (aOffset < 0) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  if (aCount == 0) {
    *aBytes = 0;
    return NS_OK;
  }
  return mResource->ReadAt(aOffset, aBuffer, aCount, aBytes);
}

nsresult MediaResourceIndex::UncachedRangedReadAt(int64_t aOffset,
                                                  char* aBuffer,
                                                  uint32_t aRequestedCount,
                                                  uint32_t aExtraCount,
                                                  uint32_t* aBytes) const {
  uint32_t count = aRequestedCount + aExtraCount;
  if (aOffset < 0 || count < aRequestedCount) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  if (count == 0) {
    *aBytes = 0;
    return NS_OK;
  }
  return mResource->ReadAt(aOffset, aBuffer, count, aBytes);
}

nsresult MediaResourceIndex::Seek(int32_t aWhence, int64_t aOffset) {
  switch (aWhence) {
    case SEEK_SET:
      break;
    case SEEK_CUR:
      aOffset += mOffset;
      break;
    case SEEK_END: {
      int64_t length = mResource->GetLength();
      if (length == -1 || length - aOffset < 0) {
        return NS_ERROR_FAILURE;
      }
      aOffset = mResource->GetLength() - aOffset;
    } break;
    default:
      return NS_ERROR_FAILURE;
  }

  if (aOffset < 0) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  mOffset = aOffset;

  return NS_OK;
}

already_AddRefed<MediaByteBuffer> MediaResourceIndex::MediaReadAt(
    int64_t aOffset, uint32_t aCount) const {
  NS_ENSURE_TRUE(aOffset >= 0, nullptr);
  RefPtr<MediaByteBuffer> bytes = new MediaByteBuffer();
  bool ok = bytes->SetLength(aCount, fallible);
  NS_ENSURE_TRUE(ok, nullptr);

  uint32_t bytesRead = 0;
  nsresult rv = mResource->ReadAt(
      aOffset, reinterpret_cast<char*>(bytes->Elements()), aCount, &bytesRead);
  NS_ENSURE_SUCCESS(rv, nullptr);

  bytes->SetLength(bytesRead);
  return bytes.forget();
}

already_AddRefed<MediaByteBuffer> MediaResourceIndex::CachedMediaReadAt(
    int64_t aOffset, uint32_t aCount) const {
  RefPtr<MediaByteBuffer> bytes = new MediaByteBuffer();
  bool ok = bytes->SetLength(aCount, fallible);
  NS_ENSURE_TRUE(ok, nullptr);
  char* curr = reinterpret_cast<char*>(bytes->Elements());
  nsresult rv = mResource->ReadFromCache(curr, aOffset, aCount);
  NS_ENSURE_SUCCESS(rv, nullptr);
  return bytes.forget();
}

// Get the length of the stream in bytes. Returns -1 if not known.
// This can change over time; after a seek operation, a misbehaving
// server may give us a resource of a different length to what it had
// reported previously --- or it may just lie in its Content-Length
// header and give us more or less data than it reported. We will adjust
// the result of GetLength to reflect the data that's actually arriving.
int64_t MediaResourceIndex::GetLength() const { return mResource->GetLength(); }

uint32_t MediaResourceIndex::IndexInCache(int64_t aOffsetInFile) const {
  const uint32_t index = uint32_t(aOffsetInFile) & (mCacheBlockSize - 1);
  MOZ_ASSERT(index == aOffsetInFile % mCacheBlockSize);
  return index;
}

int64_t MediaResourceIndex::CacheOffsetContaining(int64_t aOffsetInFile) const {
  const int64_t offset = aOffsetInFile & ~(int64_t(mCacheBlockSize) - 1);
  MOZ_ASSERT(offset == aOffsetInFile - IndexInCache(aOffsetInFile));
  return offset;
}

}  // namespace mozilla

// avoid redefined macro in unified build
#undef ILOG
