//
// Copyright 2021 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// ProvokingVertexHelper.h:
//    Manages re-writing index buffers when provoking vertex support is needed.
//    Handles points, lines, triangles, line strips, and triangle strips.
//    Line loops, and triangle fans should be pre-processed.
//
#ifndef LIBANGLE_RENDERER_METAL_PROVOKINGVERTEXHELPER_H
#define LIBANGLE_RENDERER_METAL_PROVOKINGVERTEXHELPER_H

#include "libANGLE/Context.h"
#include "libANGLE/renderer/ContextImpl.h"
#include "libANGLE/renderer/metal/BufferMtl.h"
#include "libANGLE/renderer/metal/DisplayMtl.h"
#include "libANGLE/renderer/metal/mtl_buffer_pool.h"
#include "libANGLE/renderer/metal/mtl_command_buffer.h"
#include "libANGLE/renderer/metal/mtl_context_device.h"
#include "libANGLE/renderer/metal/mtl_state_cache.h"
namespace rx
{
class ContextMtl;

class ProvokingVertexHelper : angle::NonCopyable
{
  public:
    ProvokingVertexHelper(ContextMtl *context);

    // Rewrites indices for the last-vertex convention into a buffer that starts at firstIndex. A
    // non-null conversion caches the result; pass nullptr for client-memory indices.
    angle::Result preconditionIndexBuffer(ContextMtl *context,
                                          GLsizei count,
                                          gl::PrimitiveMode mode,
                                          size_t firstIndex,
                                          bool primitiveRestartEnabled,
                                          const std::vector<DrawIndexRange> &drawIndexRanges,
                                          mtl::BufferSlice indexBuffer,
                                          gl::DrawElementsType indexBufferType,
                                          ConversionBufferMtl *conversion,
                                          mtl::BufferSlice *outNewIndexBuffer);

    angle::Result generateIndexBuffer(ContextMtl *context,
                                      size_t first,
                                      GLsizei indexCount,
                                      gl::PrimitiveMode primitiveMode,
                                      gl::DrawElementsType elementsType,
                                      uint32_t &outIndexCount,
                                      size_t &outIndexOffset,
                                      gl::PrimitiveMode &outPrimitiveMode,
                                      mtl::BufferRef &outNewBuffer);

    void releaseInFlightBuffers(ContextMtl *contextMtl);
    void ensureCommandBufferReady();
    void onDestroy(ContextMtl *context);
    mtl::ComputeCommandEncoder *getComputeCommandEncoder();

  private:
    angle::Result prepareCommandEncoderForFunction(ContextMtl *context,
                                                   mtl::ComputeCommandEncoder *encoder,
                                                   uint32_t indexBufferKey,
                                                   bool isForGenerateIndices);

    mtl::BufferPool mIndexBuffers;
    std::unordered_map<uint32_t, angle::ObjCPtr<id<MTLFunction>>> mFixIndexBufferFunctions;
    std::unordered_map<uint32_t, angle::ObjCPtr<id<MTLFunction>>> mGenIndexBufferFunctions;
};
}  // namespace rx
#endif /* LIBANGLE_RENDERER_METAL_PROVOKINGVERTEXHELPER_H */
