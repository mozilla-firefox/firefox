//
// Copyright 2019 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// mtl_buffer_pool.h:
//    Defines class interface for BufferPool, managing a pool of mtl::Buffer
//

#ifndef LIBANGLE_RENDERER_METAL_MTL_BUFFER_POOL_H_
#define LIBANGLE_RENDERER_METAL_MTL_BUFFER_POOL_H_

#include "libANGLE/renderer/metal/mtl_resources.h"

#include <deque>

namespace rx
{

class ContextMtl;

namespace mtl
{

enum class TrackBufferInContext
{
    No,
    Yes,
};

// A buffer pool is conceptually an infinitely long buffer. Each time you write to the buffer,
// you will always write to a previously unused portion. After a series of writes, you must flush
// the buffer data to the device. Buffer lifetime currently assumes that each new allocation will
// last as long or longer than each prior allocation.
//
// Buffer pool is used to implement a variety of data streaming operations in Metal, such
// as for immediate vertex array and element array data, and other dynamic data.
//
// Internally buffer pool keeps a collection of mtl::Buffer. When we write past the end of a
// currently active mtl::Buffer we keep it until it is no longer in use. We then mark it available
// for future allocations in a free list.
class BufferPool
{
  public:
    BufferPool();
    // - alwaysAllocNewBuffer=true will always allocate new buffer or reuse free buffer on
    // allocate(), regardless of whether current buffer still has unused portion or not.
    BufferPool(bool alwaysAllocNewBuffer);
    ~BufferPool();

    // Init is called after the buffer creation so that the alignment can be specified later.
    void initialize(Context *context, size_t initialSize, size_t alignment, size_t maxBuffers);
    // If trackInContext is TrackBufferInContext::Yes, the pool must be tied to contextMtl's
    // lifetime and will be registered with it for trimming on glTrimMemoryANGLE().
    void initialize(ContextMtl *contextMtl,
                    size_t initialSize,
                    size_t alignment,
                    size_t maxBuffers,
                    TrackBufferInContext trackInContext);

    // This call will allocate a new region at the end of the buffer. It internally may trigger
    // a new buffer to be created (which is returned in the optional parameter
    // `newBufferAllocatedOut`).  The new region will be in the returned BufferSlice.

    angle::Result allocate(ContextMtl *contextMtl, size_t sizeInBytes, BufferSlice *outBuffer);

    // The buffer will be automatically mapped and the mapped region returned in outMappedData.
    angle::Result allocateAndMap(ContextMtl *contextMtl,
                                 size_t sizeInBytes,
                                 angle::Span<uint8_t> *outMappedData,
                                 BufferSlice *outBuffer);

    // After a sequence of CPU writes, call commit to ensure the data is visible to the GPU.
    // Note: the data will only be made visible to the GPU if the buffer's storage mode is not
    // shared AND a non-null pointer was passed to allocate(). Otherwise, this call only advances
    // the flush pointer.
    angle::Result commit(ContextMtl *contextMtl, bool flushEntireBuffer = false);

    // This releases all the buffers that have been allocated since this was last called.
    void releaseInFlightBuffers(ContextMtl *contextMtl);

    // Releases in-flight buffers and marks all idle free buffers as purgeable (volatile).
    void trim(ContextMtl *contextMtl);

    // This frees resources immediately.
    void destroy(ContextMtl *contextMtl);

    const BufferRef &getCurrentBuffer() { return mBuffer; }

    size_t getAlignment() { return mAlignment; }
    void updateAlignment(Context *context, size_t alignment);

    size_t getMaxBuffers() const { return mMaxBuffers; }

    // Set whether allocate() will always allocate new buffer or attempting to append to previous
    // buffer or not. Default is false.
    void setAlwaysAllocateNewBuffer(bool e) { mAlwaysAllocateNewBuffer = e; }

  private:
    enum class BufferListType
    {
        InFlight,
        NonVolatileFree,
        VolatileFree,
    };

    MTLStorageMode storageMode(ContextMtl *contextMtl) const;
    void reset();
    angle::Result allocateNewBuffer(ContextMtl *contextMtl);
    BufferRef popFreeBuffer(ContextMtl *contextMtl);
    void clearFreeLists();
    void destroyBufferList(std::deque<BufferRef> *buffers, BufferListType listType);
    angle::Result finalizePendingBuffer(ContextMtl *contextMtl);

    ContextMtl *mContext{nullptr};
    BufferRef mBuffer;
    std::deque<BufferRef> mInFlightBuffers;
    std::deque<BufferRef> mBufferFreeList;
    std::deque<BufferRef> mVolatileBufferFreeList;
    size_t mInitialSize{0};
    size_t mNextAllocationOffset{0};
    size_t mLastFlushOffset{0};
    size_t mSize{0};
    size_t mAlignment{1};
    size_t mBuffersAllocated{0};
    size_t mMaxBuffers{0};
    bool mAlwaysAllocateNewBuffer;
};

}  // namespace mtl
}  // namespace rx

#endif /* LIBANGLE_RENDERER_METAL_MTL_BUFFER_POOL_H_ */
