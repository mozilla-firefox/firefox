//
// Copyright (c) 2020 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// mtl_occlusion_query_pool: A visibility pool for allocating visibility query within
// one render pass.
//

#include "libANGLE/renderer/metal/mtl_occlusion_query_pool.h"

#include <algorithm>

#include "libANGLE/renderer/metal/ContextMtl.h"
#include "libANGLE/renderer/metal/DisplayMtl.h"

namespace rx
{
namespace mtl
{

// OcclusionQueryPool implementation
OcclusionQueryPool::OcclusionQueryPool() {}
OcclusionQueryPool::~OcclusionQueryPool() {}

void OcclusionQueryPool::destroy(ContextMtl *contextMtl)
{
    mRenderPassResultsPool = nullptr;
    mAllocatedQueries.clear();
}

size_t OcclusionQueryPool::nextQueryOffset() const
{
    return mAllocatedQueries.empty()
               ? 0
               : (mAllocatedQueries.back().lastOffset() + kOcclusionQueryResultSize);
}

bool OcclusionQueryPool::canAllocateQueryOffset(ContextMtl *contextMtl) const
{
    return nextQueryOffset() + kOcclusionQueryResultSize <=
           contextMtl->getDisplay()->getMaxVisibilityQueryOffset();
}

angle::Result OcclusionQueryPool::ensurePoolCapacity(ContextMtl *contextMtl, size_t requiredEnd)
{
    if (mRenderPassResultsPool == nullptr || requiredEnd > mRenderPassResultsPool->size())
    {
        const size_t newSize = mRenderPassResultsPool == nullptr
                                   ? 16 * kOcclusionQueryResultSize
                                   : mRenderPassResultsPool->size() * 2;
        ANGLE_TRY(Buffer::MakeBufferWithStorageMode(contextMtl, MTLStorageModePrivate, newSize,
                                                    &mRenderPassResultsPool));
        mUsed                               = false;
        mRenderPassResultsPool->get().label = @"OcclusionQueryPool";
    }

    return angle::Result::Continue;
}

angle::Result OcclusionQueryPool::beginQuery(ContextMtl *contextMtl,
                                             const BufferRef &resultBuffer,
                                             size_t *outResultOffset)
{
    CHECK(canAllocateQueryOffset(contextMtl));

    size_t resultOffset = nextQueryOffset();

    ANGLE_TRY(ensurePoolCapacity(contextMtl, resultOffset + kOcclusionQueryResultSize));
    discardQuery(resultBuffer);

    mAllocatedQueries.emplace_back(resultBuffer, resultOffset, 1);
    if (resultOffset == 0)
    {
        mResetFirstQuery = true;
    }
    *outResultOffset = resultOffset;
    return angle::Result::Continue;
}

angle::Result OcclusionQueryPool::continueQuery(ContextMtl *contextMtl,
                                                const BufferRef &resultBuffer,
                                                size_t *outResultOffset)
{
    ASSERT(resultBuffer);
    CHECK(canAllocateQueryOffset(contextMtl));
    if (mAllocatedQueries.empty() &&
        !contextMtl->getDisplay()->getFeatures().allowBufferReadWrite.enabled)
    {
        // If we continue a query after a visibility results resolve, we need to load the
        // intermediate visibility result. Use an extra slot for the read unless buffer
        // read-write is possible.
        ANGLE_TRY(ensurePoolCapacity(contextMtl, 2 * kOcclusionQueryResultSize));
        mAllocatedQueries.emplace_back(resultBuffer, 0, 2);
        mResetFirstQuery = false;
        *outResultOffset = kOcclusionQueryResultSize;
        return angle::Result::Continue;
    }

    size_t resultOffset = nextQueryOffset();
    ANGLE_TRY(ensurePoolCapacity(contextMtl, resultOffset + kOcclusionQueryResultSize));

    if (mAllocatedQueries.empty())
    {
        mAllocatedQueries.emplace_back(resultBuffer, 0, 1);
        mResetFirstQuery = false;
    }
    else
    {
        // Non-initial continues always continue the previous result.
        ASSERT(mAllocatedQueries.back().resultBuffer() == resultBuffer);
        mAllocatedQueries.back().addOffset();
    }
    *outResultOffset = resultOffset;
    return angle::Result::Continue;
}

void OcclusionQueryPool::discardQuery(const BufferRef &resultBuffer)
{
    auto it = std::find_if(mAllocatedQueries.begin(), mAllocatedQueries.end(),
                           [&](const VisibilityBufferOffsetsMtl &entry) {
                               return entry.resultBuffer() == resultBuffer;
                           });
    if (it != mAllocatedQueries.end())
    {
        it->discard();
    }
}

void OcclusionQueryPool::resolveVisibilityResults(ContextMtl *contextMtl)
{
    if (mAllocatedQueries.empty())
    {
        return;
    }

    RenderUtils &utils              = contextMtl->getDisplay()->getUtils();
    BlitCommandEncoder *blitEncoder = nullptr;

    // Combine the values stored in the offsets allocated for first query
    {
        const auto &firstQuery        = mAllocatedQueries[0];
        const BufferRef &resultBuffer = firstQuery.resultBuffer();
        if (resultBuffer != nullptr)
        {
            bool keepOldValue = !mResetFirstQuery;
            if (keepOldValue &&
                !contextMtl->getDisplay()->getFeatures().allowBufferReadWrite.enabled)
            {
                blitEncoder = contextMtl->getBlitCommandEncoder();
                blitEncoder->copyBuffer(resultBuffer, 0, mRenderPassResultsPool,
                                        firstQuery.firstOffset(), kOcclusionQueryResultSize);
                keepOldValue = false;
            }
            utils.combineVisibilityResult(contextMtl, keepOldValue, firstQuery.firstOffset(),
                                          firstQuery.size(), mRenderPassResultsPool, resultBuffer);
        }
    }

    // Combine the values stored in the offsets allocated for each of the remaining queries
    for (size_t i = 1; i < mAllocatedQueries.size(); ++i)
    {
        const auto &query = mAllocatedQueries[i];
        if (query.resultBuffer() == nullptr)
        {
            continue;
        }

        utils.combineVisibilityResult(contextMtl, false, query.firstOffset(), query.size(),
                                      mRenderPassResultsPool, query.resultBuffer());
    }

    // Request synchronization and cleanup
    blitEncoder = contextMtl->getBlitCommandEncoder();
    for (auto &entry : mAllocatedQueries)
    {
        if (entry.resultBuffer() == nullptr)
        {
            continue;
        }
        entry.resultBuffer()->syncContent(contextMtl, blitEncoder);
    }

    mAllocatedQueries.clear();
}

void OcclusionQueryPool::prepareRenderPassVisibilityPoolBuffer(ContextMtl *contextMtl)
{
    if (mAllocatedQueries.empty())
    {
        return;
    }

    // If the current visibility pool buffer was not used before,
    // ensure that it will be cleared next time.
    if (!mUsed)
    {
        mUsed = true;
        return;
    }

    mUsed = false;

    // If the current visibility pool buffer was used before,
    // ensure that it does not contain previous results.
    auto blitEncoder = contextMtl->getBlitCommandEncoderWithoutEndingRenderEncoder();
    blitEncoder->fillBuffer(
        mRenderPassResultsPool,
        NSMakeRange(0, mAllocatedQueries.back().lastOffset() + kOcclusionQueryResultSize), 0);
    blitEncoder->endEncoding();
}

}  // namespace mtl
}  // namespace rx
