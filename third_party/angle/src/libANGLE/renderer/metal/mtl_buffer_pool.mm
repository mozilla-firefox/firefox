//
// Copyright 2019 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// mtl_buffer_pool.mm:
//    Implements the class methods for BufferPool.
//

#include "libANGLE/renderer/metal/mtl_buffer_pool.h"

#include <atomic>
#include "libANGLE/renderer/metal/ContextMtl.h"
#include "libANGLE/renderer/metal/DisplayMtl.h"
#include "libANGLE/trace.h"

namespace rx
{

namespace mtl
{

namespace
{
void UpdateBufferPoolMetrics(int64_t totalMemoryDelta,
                             int64_t freeMemoryDelta,
                             int64_t volatileMemoryDelta,
                             int64_t totalBuffersDelta)
{
#if defined(ANGLE_USE_PERFETTO)
    static std::atomic<int64_t> s_BufferPoolTotalMemory(0);
    static std::atomic<int64_t> s_BufferPoolFreeMemory(0);
    static std::atomic<int64_t> s_BufferPoolVolatileMemory(0);
    static std::atomic<int64_t> s_BufferPoolTotalBuffers(0);

    if (totalMemoryDelta != 0)
    {
        int64_t newTotalMemory =
            s_BufferPoolTotalMemory.fetch_add(totalMemoryDelta, std::memory_order_relaxed) +
            totalMemoryDelta;
        ANGLE_TRACE_COUNTER("gpu.angle", "ANGLEMetalBufferPoolTotalMemoryKb",
                            newTotalMemory / 1024);
    }

    if (freeMemoryDelta != 0)
    {
        int64_t newFreeMemory =
            s_BufferPoolFreeMemory.fetch_add(freeMemoryDelta, std::memory_order_relaxed) +
            freeMemoryDelta;
        ANGLE_TRACE_COUNTER("gpu.angle", "ANGLEMetalBufferPoolFreeMemoryKb", newFreeMemory / 1024);
    }

    if (volatileMemoryDelta != 0)
    {
        int64_t newVolatileMemory =
            s_BufferPoolVolatileMemory.fetch_add(volatileMemoryDelta, std::memory_order_relaxed) +
            volatileMemoryDelta;
        ANGLE_TRACE_COUNTER("gpu.angle", "ANGLEMetalBufferPoolVolatileMemoryKb",
                            newVolatileMemory / 1024);
    }

    if (totalBuffersDelta != 0)
    {
        int64_t newTotalBuffers =
            s_BufferPoolTotalBuffers.fetch_add(totalBuffersDelta, std::memory_order_relaxed) +
            totalBuffersDelta;
        ANGLE_TRACE_COUNTER("gpu.angle", "ANGLEMetalBufferPoolTotalBuffers", newTotalBuffers);
    }
#endif
}
}  // namespace

// BufferPool implementation.
BufferPool::BufferPool() : BufferPool(false) {}

BufferPool::BufferPool(bool alwaysAllocNewBuffer)
    : mBuffer(nullptr), mAlwaysAllocateNewBuffer(alwaysAllocNewBuffer)
{}

void BufferPool::initialize(Context *context,
                            size_t initialSize,
                            size_t alignment,
                            size_t maxBuffers)
{
    if (mBuffersAllocated)
    {
        // Invalid call, must call destroy() first.
        UNREACHABLE();
    }

    mInitialSize = initialSize;

    mMaxBuffers = maxBuffers;

    updateAlignment(context, alignment);
}

void BufferPool::initialize(ContextMtl *contextMtl,
                            size_t initialSize,
                            size_t alignment,
                            size_t maxBuffers,
                            TrackBufferInContext trackInContext)
{
    initialize(contextMtl, initialSize, alignment, maxBuffers);

    // Some instances are not tied to a single ContextMtl, but rather to a
    // ShareGroup, in which case they should not be registered in the Context
    // list of BufferPool instances.
    if (trackInContext == TrackBufferInContext::Yes &&
        contextMtl->getDisplay()->getFeatures().purgeableBufferPool.enabled)
    {
        ASSERT(mContext == nullptr);
        mContext = contextMtl;
        mContext->registerBufferPool(this);
    }
}

BufferPool::~BufferPool()
{
    destroy(nullptr);
}

MTLStorageMode BufferPool::storageMode(ContextMtl *contextMtl) const
{
    if (mContext != nullptr)
    {
        // Making a buffer purgeable requires Shared storage mode. Make sure
        // that we only set this when it is enabled (even though this is already
        // the default on target platforms).
        //
        // This is a smaller change than it seems: on
        // Intel,alwaysUseSharedStorageModeForBuffers is enabled, and on Apple
        // Silicon, managed is treated as shared internally. However, care must
        // be taken on discrete graphics platforms.
        ASSERT(mContext->getDisplay()->getFeatures().purgeableBufferPool.enabled);
        return MTLStorageModeShared;
    }
#if TARGET_OS_OSX || TARGET_OS_MACCATALYST
    if (mSize > kSharedMemBufferMaxBufSizeHint)
    {
        return MTLStorageModeManaged;
    }
#endif
    return Buffer::getStorageModeForSharedBuffer(contextMtl);
}

BufferRef BufferPool::popFreeBuffer(ContextMtl *contextMtl)
{
    // Prefer reusing a NonVolatile buffer at the front of mBufferFreeList if one is ready.
    if (!mBufferFreeList.empty() && !mBufferFreeList.front()->isBeingUsedByGPU(contextMtl))
    {
        BufferRef buffer = std::move(mBufferFreeList.front());
        mBufferFreeList.pop_front();
        UpdateBufferPoolMetrics(0, -static_cast<int64_t>(buffer->size()), 0, 0);
        return buffer;
    }

    // Otherwise, reclaim the most-recently trimmed Volatile buffer from the back of
    // mVolatileBufferFreeList (LIFO) so excess buffers at the front remain cold.
    while (!mVolatileBufferFreeList.empty())
    {
        BufferRef buffer = std::move(mVolatileBufferFreeList.back());
        mVolatileBufferFreeList.pop_back();
        int64_t size = static_cast<int64_t>(buffer->size());
        UpdateBufferPoolMetrics(0, -size, -size, 0);
        if ([buffer->get() setPurgeableState:MTLPurgeableStateNonVolatile] ==
            MTLPurgeableStateEmpty)
        {
            UpdateBufferPoolMetrics(-size, 0, 0, -1);
            mBuffersAllocated--;
            continue;
        }
        return buffer;
    }

    return nullptr;
}

angle::Result BufferPool::allocateNewBuffer(ContextMtl *contextMtl)
{
    if (mMaxBuffers > 0 && mBuffersAllocated >= mMaxBuffers)
    {
        // We reach the max number of buffers allowed.
        // Try to deallocate old and smaller size inflight buffers.
        releaseInFlightBuffers(contextMtl);

        if (mBuffersAllocated >= mMaxBuffers)
        {
            // If we reach this point, it means there was no buffer deallocated inside
            // releaseInFlightBuffers() thus, the number of buffers allocated still exceeds number
            // allowed. Also, popFreeBuffer() has already drained any volatile buffers.
            ASSERT(!mBufferFreeList.empty() && mVolatileBufferFreeList.empty());

            // Reuse the buffer in free list:
            if (mBufferFreeList.front()->isBeingUsedByGPU(contextMtl))
            {
                contextMtl->flushCommandBuffer(mtl::NoWait);
                // Force the GPU to finish its rendering and make the old buffer available.
                contextMtl->cmdQueue().ensureResourceReadyForCPU(mBufferFreeList.front());
            }
        }

        mBuffer = popFreeBuffer(contextMtl);
        if (mBuffer)
        {
            return angle::Result::Continue;
        }
    }

    ANGLE_TRY(
        Buffer::MakeBufferWithStorageMode(contextMtl, storageMode(contextMtl), mSize, &mBuffer));

    ASSERT(mBuffer);

    UpdateBufferPoolMetrics(mBuffer->size(), 0, 0, 1);

    mBuffersAllocated++;

    return angle::Result::Continue;
}

angle::Result BufferPool::allocateAndMap(ContextMtl *contextMtl,
                                         size_t sizeInBytes,
                                         angle::Span<uint8_t> *mappedOut,
                                         BufferSlice *outBuffer)
{
    ANGLE_TRY(allocate(contextMtl, sizeInBytes, outBuffer));
    // We don't need to synchronize with GPU access, since allocation should return a
    // non-overlapped region each time.
    *mappedOut = mBuffer->mapNoSync(contextMtl, outBuffer->offset(), sizeInBytes);
    return angle::Result::Continue;
}

angle::Result BufferPool::allocate(ContextMtl *contextMtl,
                                   size_t sizeInBytes,
                                   BufferSlice *outBuffer)
{
    size_t sizeToAllocate = roundUp(sizeInBytes, mAlignment);

    angle::base::CheckedNumeric<size_t> checkedNextWriteOffset = mNextAllocationOffset;
    checkedNextWriteOffset += sizeToAllocate;

    if (!mBuffer || !checkedNextWriteOffset.IsValid() ||
        checkedNextWriteOffset.ValueOrDie() >= mSize ||
        // If the current buffer has been modified by GPU, do not reuse it:
        mBuffer->isCPUReadMemNeedSync() || mAlwaysAllocateNewBuffer)
    {
        if (mBuffer)
        {
            ANGLE_TRY(finalizePendingBuffer(contextMtl));
        }

        const size_t oldSize = mSize;
        if (sizeToAllocate > mSize)
        {
            mSize = std::max(mInitialSize, sizeToAllocate);

            // Clear the free lists since the free buffers are now too small.
            clearFreeLists();
        }

        if ((mBuffer = popFreeBuffer(contextMtl)) == nullptr)
        {
            // If allocating the new buffer fails (e.g. sizeToAllocate exceeds maxBufferLength),
            // revert mSize so subsequent smaller allocations on this pool do not keep attempting
            // to allocate the oversized mSize.
            if (allocateNewBuffer(contextMtl) != angle::Result::Continue)
            {
                mSize = mBuffersAllocated > 0 ? oldSize : mInitialSize;
                return angle::Result::Stop;
            }
        }

        ASSERT(mBuffer->size() == mSize);

        mNextAllocationOffset = 0;
        mLastFlushOffset      = 0;
    }

    ASSERT(mBuffer != nullptr);

    if (outBuffer != nullptr)
    {
        *outBuffer = BufferSlice(mBuffer).subslice(mNextAllocationOffset, sizeInBytes);
    }
    mNextAllocationOffset += sizeToAllocate;
    return angle::Result::Continue;
}

angle::Result BufferPool::commit(ContextMtl *contextMtl, bool flushEntireBuffer)
{
    if (mBuffer && mNextAllocationOffset > mLastFlushOffset)
    {
        if (flushEntireBuffer)
        {
            mBuffer->flush(contextMtl, 0, mLastFlushOffset);
        }
        else
        {
            mBuffer->flush(contextMtl, mLastFlushOffset, mNextAllocationOffset - mLastFlushOffset);
        }
        mLastFlushOffset = mNextAllocationOffset;
    }
    return angle::Result::Continue;
}

angle::Result BufferPool::finalizePendingBuffer(ContextMtl *contextMtl)
{
    if (mBuffer)
    {
        ANGLE_TRY(commit(contextMtl));
        // commit() already flushes so no need to flush here.
        mBuffer->unmapNoFlush(contextMtl);

        mInFlightBuffers.push_back(mBuffer);
        mBuffer = nullptr;
    }

    mNextAllocationOffset = 0;
    mLastFlushOffset      = 0;

    return angle::Result::Continue;
}

void BufferPool::releaseInFlightBuffers(ContextMtl *contextMtl)
{
    for (auto &toRelease : mInFlightBuffers)
    {
        // If the dynamic buffer was resized we cannot reuse the retained buffer.
        if (toRelease->size() < mSize
#if TARGET_OS_OSX || TARGET_OS_MACCATALYST
            // Also release buffer if it was allocated in different policy
            || toRelease->storageMode() != storageMode(contextMtl)
#endif
        )
        {
            UpdateBufferPoolMetrics(-static_cast<int64_t>(toRelease->size()), 0, 0, -1);
            toRelease = nullptr;
            mBuffersAllocated--;
        }

        // Need to maintain the requirement of the free list that buffers in use
        // by the GPU are stored in FIFO order and that after the first in-use
        // buffer, the rest of the free list is in-use as well. To achieve this
        // in-use buffers are appended to the end of the free list and free buffers
        // are prepended to the beginning of the free list to maintain the following:
        //
        //  +------+------+-------+-------+-------+
        //  | Free | Free | Inuse |  ...  | Inuse |
        //  +------+------+-------+-------+-------+
        //  ^             ^               ^-------- Youngest, in-use buffer
        //  |             +------------------------ Oldest, in-use buffer
        //  +-------------------------------------- First, free buffer
        else if (toRelease->isBeingUsedByGPU(contextMtl))
        {
            mBufferFreeList.push_back(toRelease);
            UpdateBufferPoolMetrics(0, toRelease->size(), 0, 0);
        }
        else
        {
            mBufferFreeList.push_front(toRelease);
            UpdateBufferPoolMetrics(0, toRelease->size(), 0, 0);
        }
    }

    mInFlightBuffers.clear();
}

void BufferPool::trim(ContextMtl *contextMtl)
{
    ASSERT(contextMtl->getDisplay()->getFeatures().purgeableBufferPool.enabled);
    if (mBuffer && !mBuffer->isBeingUsedByGPU(contextMtl))
    {
        (void)finalizePendingBuffer(contextMtl);
    }
    releaseInFlightBuffers(contextMtl);

    size_t freeCount = 0;
    while (freeCount < mBufferFreeList.size() &&
           !mBufferFreeList[freeCount]->isBeingUsedByGPU(contextMtl))
    {
        freeCount++;
    }

    // Move free NonVolatile buffers to mVolatileBufferFreeList in oldest-to-newest order so the
    // most-recently used buffer is at back() and reclaimed first by popFreeBuffer().
    for (size_t i = freeCount; i > 0; --i)
    {
        BufferRef buffer = std::move(mBufferFreeList[i - 1]);
        [buffer->get() setPurgeableState:MTLPurgeableStateVolatile];
        UpdateBufferPoolMetrics(0, 0, static_cast<int64_t>(buffer->size()), 0);
        mVolatileBufferFreeList.push_back(std::move(buffer));
    }
    mBufferFreeList.erase(mBufferFreeList.begin(), mBufferFreeList.begin() + freeCount);
}

void BufferPool::clearFreeLists()
{
    destroyBufferList(&mBufferFreeList, BufferListType::NonVolatileFree);
    destroyBufferList(&mVolatileBufferFreeList, BufferListType::VolatileFree);
}

void BufferPool::destroyBufferList(std::deque<BufferRef> *buffers, BufferListType listType)
{
    ASSERT(mBuffersAllocated >= buffers->size());
    mBuffersAllocated -= buffers->size();
    const bool isFreeList = listType != BufferListType::InFlight;
    const bool isVolatile = listType == BufferListType::VolatileFree;
    for (const BufferRef &buffer : *buffers)
    {
        if (buffer)
        {
            int64_t size = static_cast<int64_t>(buffer->size());
            UpdateBufferPoolMetrics(-size, isFreeList ? -size : 0, isVolatile ? -size : 0, -1);
        }
    }
    buffers->clear();
}

void BufferPool::destroy(ContextMtl *contextMtl)
{
    destroyBufferList(&mInFlightBuffers, BufferListType::InFlight);
    clearFreeLists();

    if (mBuffer)
    {
        ASSERT(mBuffersAllocated == 1);
        mBuffersAllocated--;
        UpdateBufferPoolMetrics(-static_cast<int64_t>(mBuffer->size()), 0, 0, -1);
        mBuffer->unmap(contextMtl);

        mBuffer = nullptr;
    }
    ASSERT(mBuffersAllocated == 0);

    reset();
    if (mContext)
    {
        mContext->unregisterBufferPool(this);
        mContext = nullptr;
    }
}

void BufferPool::updateAlignment(Context *context, size_t alignment)
{
    ASSERT(alignment > 0);

    // NOTE(hqle): May check additional platform limits.

    // If alignment has changed, make sure the next allocation is done at an aligned offset.
    if (alignment != mAlignment)
    {
        mNextAllocationOffset = roundUp(mNextAllocationOffset, alignment);
        mAlignment            = alignment;
    }
}

void BufferPool::reset()
{
    mSize                    = 0;
    mNextAllocationOffset    = 0;
    mLastFlushOffset         = 0;
    mMaxBuffers              = 0;
    mAlwaysAllocateNewBuffer = false;
    mBuffersAllocated        = 0;
}
}  // namespace mtl
}  // namespace rx
