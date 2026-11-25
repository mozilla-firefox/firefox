/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MemoryBlockCache.h"

#include "mozilla/Atomics.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Logging.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_media.h"
#include "nsWeakReference.h"
#include "prsystem.h"

namespace mozilla {

#undef LOG
LazyLogModule gMemoryBlockCacheLog("MemoryBlockCache");
#define LOG(x, ...)                                                           \
  MOZ_LOG_FMT(gMemoryBlockCacheLog, LogLevel::Debug, "{} " x, fmt::ptr(this), \
              ##__VA_ARGS__)

// Combined sizes of all MemoryBlockCache buffers.
// Initialized to 0 by non-local static initialization.
// Increases when a buffer grows (during initialization or unexpected OOB
// writes), decreases when a MemoryBlockCache (with its buffer) is destroyed.
static Atomic<size_t> gCombinedSizes;

static int32_t CalculateMaxBlocks(int64_t aContentLength) {
  int64_t maxSize = int64_t(StaticPrefs::media_memory_cache_max_size()) * 1024;
  MOZ_ASSERT(aContentLength <= maxSize);
  MOZ_ASSERT(maxSize % MediaBlockCacheBase::BLOCK_SIZE == 0);
  // Note: It doesn't matter if calculations overflow, Init() would later fail.
  // We want at least enough blocks to contain the original content length.
  const int32_t requiredBlocks = maxSize / MediaBlockCacheBase::BLOCK_SIZE;
  // Allow at least 1s of ultra HD (25Mbps).
  const int32_t workableBlocks =
      25 * 1024 * 1024 / 8 / MediaBlockCacheBase::BLOCK_SIZE;
  return std::max(requiredBlocks, workableBlocks);
}

MemoryBlockCache::MemoryBlockCache(int64_t aContentLength)
    // Buffer whole blocks.
    : mInitialContentLength((aContentLength >= 0) ? size_t(aContentLength) : 0),
      mMaxBlocks(CalculateMaxBlocks(aContentLength)),
      mMutex("MemoryBlockCache"),
      mHasGrown(false) {
  if (aContentLength <= 0) {
    LOG("MemoryBlockCache() MEMORYBLOCKCACHE_ERRORS='InitUnderuse'");
  }
}

MemoryBlockCache::~MemoryBlockCache() {
  MOZ_ASSERT(gCombinedSizes >= mBuffer.Length());
  size_t sizes = static_cast<size_t>(gCombinedSizes -= mBuffer.Length());
  LOG("~MemoryBlockCache() - destroying buffer of size {}; combined sizes now "
      "{}",
      mBuffer.Length(), sizes);
}

bool MemoryBlockCache::EnsureBufferCanContain(size_t aContentLength) {
  mMutex.AssertCurrentThreadOwns();
  if (aContentLength == 0) {
    return true;
  }
  const size_t initialLength = mBuffer.Length();
  const size_t desiredLength =
      ((aContentLength - 1) / BLOCK_SIZE + 1) * BLOCK_SIZE;
  if (initialLength >= desiredLength) {
    // Already large enough.
    return true;
  }
  // Need larger buffer. If we are allowed more memory, attempt to re-allocate.
  const size_t extra = desiredLength - initialLength;
  // Only check the very first allocation against the combined MemoryBlockCache
  // limit. Further growths will always be allowed, assuming MediaCache won't
  // go over GetMaxBlocks() by too much.
  if (initialLength == 0) {
    // Note: There is a small race between testing `atomic + extra > limit` and
    // committing to it with `atomic += extra` below; but this is acceptable, as
    // in the worst case it may allow a small number of buffers to go past the
    // limit.
    // The alternative would have been to reserve the space first with
    // `atomic += extra` and then undo it with `atomic -= extra` in case of
    // failure; but this would have meant potentially preventing other (small
    // but successful) allocations.
    static const size_t sysmem =
        std::max<size_t>(PR_GetPhysicalMemorySize(), 32 * 1024 * 1024);
    const size_t limit = std::min(
        size_t(StaticPrefs::media_memory_caches_combined_limit_kb()) * 1024,
        sysmem * StaticPrefs::media_memory_caches_combined_limit_pc_sysmem() /
            100);
    const size_t currentSizes = gCombinedSizes.load();
    if (currentSizes + extra > limit) {
      LOG("EnsureBufferCanContain({}) - buffer size {}, wanted + {} = {};"
          " combined sizes {} + {} > limit {}",
          aContentLength, initialLength, extra, desiredLength, currentSizes,
          extra, limit);
      return false;
    }
  }
  if (!mBuffer.SetLength(desiredLength, mozilla::fallible)) {
    LOG("EnsureBufferCanContain({}) - buffer size {}, wanted + {} = {}, "
        "allocation failed",
        aContentLength, initialLength, extra, desiredLength);
    return false;
  }
  MOZ_ASSERT(mBuffer.Length() == desiredLength);
  const size_t capacity = mBuffer.Capacity();
  const size_t extraCapacity = capacity - desiredLength;
  if (extraCapacity != 0) {
    // Our buffer was given a larger capacity than the requested length, we may
    // as well claim that extra capacity, both for our accounting, and to
    // possibly bypass some future growths that would fit in this new capacity.
    mBuffer.SetLength(capacity);
  }
  const size_t newSizes = gCombinedSizes += (extra + extraCapacity);
  LOG("EnsureBufferCanContain({}) - buffer size {} + requested {} + bonus "
      "{} = {}; combined sizes {}",
      aContentLength, initialLength, extra, extraCapacity, capacity, newSizes);
  mHasGrown = true;
  return true;
}

nsresult MemoryBlockCache::Init() {
  LOG("Init()");
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(mBuffer.IsEmpty());
  // Attempt to pre-allocate buffer for expected content length.
  if (!EnsureBufferCanContain(mInitialContentLength)) {
    LOG("Init() MEMORYBLOCKCACHE_ERRORS='InitAllocation'");
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

void MemoryBlockCache::Flush() {
  LOG("Flush()");
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(mBuffer.Length() >= mInitialContentLength);
  memset(mBuffer.Elements(), 0, mBuffer.Length());
  mHasGrown = false;
}

nsresult MemoryBlockCache::WriteBlock(uint32_t aBlockIndex,
                                      Span<const uint8_t> aData1,
                                      Span<const uint8_t> aData2) {
  MutexAutoLock lock(mMutex);

  size_t offset = BlockIndexToOffset(aBlockIndex);
  if (offset + aData1.Length() + aData2.Length() > mBuffer.Length() &&
      !mHasGrown) {
    LOG("WriteBlock() MEMORYBLOCKCACHE_ERRORS='WriteBlockOverflow'");
  }
  if (!EnsureBufferCanContain(offset + aData1.Length() + aData2.Length())) {
    LOG("WriteBlock() MEMORYBLOCKCACHE_ERRORS='WriteBlockCannotGrow'");
    return NS_ERROR_FAILURE;
  }

  memcpy(mBuffer.Elements() + offset, aData1.Elements(), aData1.Length());
  if (aData2.Length() > 0) {
    memcpy(mBuffer.Elements() + offset + aData1.Length(), aData2.Elements(),
           aData2.Length());
  }

  return NS_OK;
}

nsresult MemoryBlockCache::Read(int64_t aOffset, uint8_t* aData,
                                int32_t aLength) {
  MutexAutoLock lock(mMutex);

  MOZ_ASSERT(aOffset >= 0);
  if (aOffset + aLength > int64_t(mBuffer.Length())) {
    LOG("Read() MEMORYBLOCKCACHE_ERRORS='ReadOverrun'");
    return NS_ERROR_FAILURE;
  }

  memcpy(aData, mBuffer.Elements() + aOffset, aLength);
  return NS_OK;
}

nsresult MemoryBlockCache::MoveBlock(int32_t aSourceBlockIndex,
                                     int32_t aDestBlockIndex) {
  MutexAutoLock lock(mMutex);

  size_t sourceOffset = BlockIndexToOffset(aSourceBlockIndex);
  size_t destOffset = BlockIndexToOffset(aDestBlockIndex);
  if (sourceOffset + BLOCK_SIZE > mBuffer.Length()) {
    LOG("MoveBlock() MEMORYBLOCKCACHE_ERRORS='MoveBlockSourceOverrun'");
    return NS_ERROR_FAILURE;
  }
  if (destOffset + BLOCK_SIZE > mBuffer.Length() && !mHasGrown) {
    LOG("MoveBlock() MEMORYBLOCKCACHE_ERRORS='MoveBlockDestOverflow'");
  }
  if (!EnsureBufferCanContain(destOffset + BLOCK_SIZE)) {
    LOG("MoveBlock() MEMORYBLOCKCACHE_ERRORS='MoveBlockCannotGrow'");
    return NS_ERROR_FAILURE;
  }

  memcpy(mBuffer.Elements() + destOffset, mBuffer.Elements() + sourceOffset,
         BLOCK_SIZE);

  return NS_OK;
}

}  // End namespace mozilla.

// avoid redefined macro in unified build
#undef LOG
