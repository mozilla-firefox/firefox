//
// Copyright (c) 2020 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// mtl_occlusion_query_pool: A pool for allocating visibility query within
// one render pass.
//

#ifndef LIBANGLE_RENDERER_METAL_MTL_OCCLUSION_QUERY_POOL_H_
#define LIBANGLE_RENDERER_METAL_MTL_OCCLUSION_QUERY_POOL_H_

#include <vector>

#include "libANGLE/Context.h"
#include "libANGLE/renderer/metal/mtl_common.h"
#include "libANGLE/renderer/metal/mtl_resources.h"

namespace rx
{

class ContextMtl;

namespace mtl
{

// Tracks allocations of visibility buffer offsets for a query within the render pass.
// An occlusion query might have more than one offsets allocated, but all of them must be adjacent
// to each other. Multiple offsets typically allocated when the query is paused and resumed during
// viewport clear emulation with draw operations. In such case, Metal doesn't allow an offset to
// be reused in a render pass, hence multiple offsets will be allocated, and their values will
// be accumulated.
class VisibilityBufferOffsetsMtl
{
  public:
    VisibilityBufferOffsetsMtl(const BufferRef &buffer, size_t startOffset, size_t size)
        : mResultBuffer(buffer), mFirstOffset(startOffset), mSize(size)
    {
        ASSERT(mSize > 0);
    }
    size_t size() const { return mSize; }
    size_t lastOffset() const { return mFirstOffset + (mSize - 1) * kOcclusionQueryResultSize; }
    size_t firstOffset() const { return mFirstOffset; }
    void addOffset() { mSize++; }
    const BufferRef &resultBuffer() const { return mResultBuffer; }
    void discard() { mResultBuffer = nullptr; }

  private:
    BufferRef mResultBuffer;
    size_t mFirstOffset{0};
    size_t mSize{0};
};

class OcclusionQueryPool
{
  public:
    OcclusionQueryPool();
    ~OcclusionQueryPool();

    void destroy(ContextMtl *contextMtl);

    // Returns false if the render pass visibility offset limit of the device has been reached,
    // in which case the render pass must be split before beginning or continuing a query.
    bool canAllocateQueryOffset(ContextMtl *contextMtl) const;

    // Begin a new occlusion query in the current render pass.
    angle::Result beginQuery(ContextMtl *contextMtl,
                             const BufferRef &resultBuffer,
                             size_t *outResultOffset);

    // Continue an already-active query after a render pass restart.
    angle::Result continueQuery(ContextMtl *contextMtl,
                                const BufferRef &resultBuffer,
                                size_t *outResultOffset);

    void discardQuery(const BufferRef &resultBuffer);

    bool hasPendingVisibilityResults() const { return !mAllocatedQueries.empty(); }

    // Returns the buffer that contains the visibility result writes for the current render pass.
    const BufferRef &getRenderPassVisibilityPoolBuffer() const { return mRenderPassResultsPool; }

    // This function is called at the end of render pass
    void resolveVisibilityResults(ContextMtl *contextMtl);
    // Clear visibility pool buffer to drop previous results
    void prepareRenderPassVisibilityPoolBuffer(ContextMtl *contextMtl);

  private:
    angle::Result ensurePoolCapacity(ContextMtl *contextMtl, size_t requiredEnd);
    size_t nextQueryOffset() const;

    // Buffer to hold the visibility results for current render pass
    BufferRef mRenderPassResultsPool;

    // List of allocated queries per render pass.
    std::vector<VisibilityBufferOffsetsMtl> mAllocatedQueries;

    bool mResetFirstQuery = false;
    bool mUsed            = false;
};

}  // namespace mtl
}  // namespace rx

#endif /* LIBANGLE_RENDERER_METAL_MTL_OCCLUSION_QUERY_POOL_H_ */
