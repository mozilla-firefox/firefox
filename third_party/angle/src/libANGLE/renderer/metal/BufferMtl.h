//
// Copyright 2019 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// BufferMtl.h:
//    Defines the class interface for BufferMtl, implementing BufferImpl.
//

#ifndef LIBANGLE_RENDERER_METAL_BUFFERMTL_H_
#define LIBANGLE_RENDERER_METAL_BUFFERMTL_H_

#import <Metal/Metal.h>

#include <deque>
#include <memory>
#include <optional>
#include <utility>

#include "common/base/anglebase/containers/mru_cache.h"
#include "common/span.h"
#include "libANGLE/Buffer.h"
#include "libANGLE/Observer.h"
#include "libANGLE/angletypes.h"
#include "libANGLE/renderer/BufferImpl.h"
#include "libANGLE/renderer/Format.h"
#include "libANGLE/renderer/metal/mtl_buffer_pool.h"
#include "libANGLE/renderer/metal/mtl_resources.h"

namespace rx
{

struct DrawCommandRange
{
    DrawCommandRange(uint32_t count, size_t offset) : count(count), offset(offset) {}

    uint32_t count;
    size_t offset;
};

// Inclusive range of consecutive draw indexes.
// 1, 2, 3, 0xff, 1, 2, 3 produces DrawIndexRange(0, 2), DrawIndexRange(4, 6).
struct DrawIndexRange
{
    DrawIndexRange(size_t begin, size_t end) : begin(begin), end(end) {}

    size_t begin;
    size_t end;
};
// Conversion buffers hold translated index and vertex data.
struct ConversionBufferMtl
{
    ConversionBufferMtl(ContextMtl *context, size_t initialSize, size_t alignment);
    ~ConversionBufferMtl();

    // One state value determines if we need to re-stream vertex data.
    bool dirty{true};

    // The conversion is stored in a dynamic buffer.
    mtl::BufferPool bufferPool;
    // The buffer is to be filled by user of this buffer conversion.
    mtl::BufferSlice buffer;
};

struct VertexConversionBufferMtl : public ConversionBufferMtl
{
    VertexConversionBufferMtl(ContextMtl *context,
                              angle::FormatID formatIDIn,
                              GLuint strideIn,
                              size_t offsetIn);

    // The conversion is identified by the triple of {format, stride, offset}.
    angle::FormatID formatID;
    GLuint stride;
    size_t offset;
};

struct IndexConversionBufferMtl : public ConversionBufferMtl
{
    IndexConversionBufferMtl(ContextMtl *context,
                             gl::DrawElementsType elemType,
                             bool primitiveRestartEnabled,
                             size_t offsetIn);
    const gl::DrawElementsType elemType;
    const size_t offset;
    bool primitiveRestartEnabled;
};

struct UniformConversionBufferMtl : public ConversionBufferMtl
{
    UniformConversionBufferMtl(ContextMtl *context,
                               uint64_t programSerialIdIn,
                               std::pair<size_t, size_t> offsetIn,
                               size_t blockSize);

    size_t initialSrcOffset() { return offset.second; }

    const uint64_t programSerialId;
    const size_t uniformBufferBlockSize;
    const std::pair<size_t, size_t> offset;
};

// Identifies the draw whose indices a provoking vertex conversion rewrote.
struct ProvokingVertexConversionKey
{
    struct Hash
    {
        size_t operator()(const ProvokingVertexConversionKey &key) const;
    };

    bool operator==(const ProvokingVertexConversionKey &other) const;

    gl::PrimitiveMode mode;
    gl::DrawElementsType elemType;
    size_t firstIndex;
    GLsizei count;
    bool primitiveRestartEnabled;
};

class BufferHolderMtl
{
  public:
    virtual ~BufferHolderMtl() = default;

    // Due to the complication of synchronizing accesses between CPU and GPU,
    // a mtl::Buffer might be under used by GPU but CPU wants to modify its content through
    // map() method, this could lead to GPU stalling. The more efficient method is maintain
    // a queue of mtl::Buffer and only let CPU modifies a free mtl::Buffer.
    // So, in order to let GPU use the most recent modified content, one must call this method
    // right before the draw call to retrieved the most up-to-date mtl::Buffer.
    mtl::BufferRef getCurrentBuffer() const { return mIsWeak ? mBufferWeakRef.lock() : mBuffer; }

  protected:
    mtl::BufferRef mBuffer;
    mtl::BufferWeakRef mBufferWeakRef;
    bool mIsWeak = false;
};

class BufferMtl : public BufferImpl, public BufferHolderMtl
{
  public:
    BufferMtl(const gl::BufferState &state);
    ~BufferMtl() override;
    void destroy(const gl::Context *context) override;

    angle::Result setData(const gl::Context *context,
                          gl::BufferBinding target,
                          const void *data,
                          size_t size,
                          gl::BufferUsage usage,
                          BufferFeedback *feedback,
                          gl::ZeroFillRequired zeroFillRequired) override;
    angle::Result setSubData(const gl::Context *context,
                             gl::BufferBinding target,
                             const void *data,
                             size_t size,
                             size_t offset,
                             BufferFeedback *feedback) override;
    angle::Result copySubData(const gl::Context *context,
                              BufferImpl *source,
                              GLintptr sourceOffset,
                              GLintptr destOffset,
                              GLsizeiptr size,
                              BufferFeedback *feedback) override;
    angle::Result map(const gl::Context *context,
                      GLenum access,
                      void **mapPtr,
                      BufferFeedback *feedback) override;
    angle::Result mapRange(const gl::Context *context,
                           size_t offset,
                           size_t length,
                           GLbitfield access,
                           void **mapPtr,
                           BufferFeedback *feedback) override;
    angle::Result unmap(const gl::Context *context,
                        GLboolean *result,
                        BufferFeedback *feedback) override;

    angle::Result getIndexRange(const gl::Context *context,
                                gl::DrawElementsType type,
                                size_t offset,
                                size_t count,
                                bool primitiveRestartEnabled,
                                gl::IndexRange *outRange) override;

    void onDataChanged() override;

    angle::Result getFirstLastIndices(ContextMtl *contextMtl,
                                      gl::DrawElementsType type,
                                      size_t offset,
                                      size_t count,
                                      std::pair<uint32_t, uint32_t> *outIndices);

    angle::Span<const uint8_t> getBufferDataReadOnly(ContextMtl *contextMtl, size_t offset);
    bool isSafeToReadFromBufferViaBlit(ContextMtl *contextMtl);

    ConversionBufferMtl *getVertexConversionBuffer(ContextMtl *context,
                                                   angle::FormatID formatID,
                                                   GLuint stride,
                                                   size_t offset);

    IndexConversionBufferMtl *getIndexConversionBuffer(ContextMtl *context,
                                                       gl::DrawElementsType elemType,
                                                       bool primitiveRestartEnabled,
                                                       size_t offset);

    ConversionBufferMtl *getUniformConversionBuffer(ContextMtl *context,
                                                    uint64_t programSerialId,
                                                    std::pair<size_t, size_t> offset,
                                                    size_t blockSize);

    size_t size() const { return static_cast<size_t>(mState.getSize()); }

    ConversionBufferMtl *getProvokingVertexConversionBuffer(ContextMtl *context,
                                                            gl::PrimitiveMode mode,
                                                            gl::DrawElementsType elemType,
                                                            size_t firstIndex,
                                                            GLsizei count,
                                                            bool primitiveRestartEnabled);

    const std::vector<DrawIndexRange> &getDrawIndexRanges(ContextMtl *ctx,
                                                          gl::DrawElementsType indexType);

    static const std::vector<DrawIndexRange>
    GetDrawIndexRangesFromClientData(gl::DrawElementsType type, GLint count, const void *indices);

  private:
    angle::Result allocateNewMetalBuffer(ContextMtl *contextMtl,
                                         MTLStorageMode storageMode,
                                         size_t size,
                                         bool returnOldBufferImmediately,
                                         BufferFeedback *feedback);

    angle::Result setDataImpl(const gl::Context *context,
                              gl::BufferBinding target,
                              size_t size,
                              gl::BufferUsage usage,
                              BufferFeedback *feedback);
    angle::Result setSubDataImpl(const gl::Context *context,
                                 angle::Span<const uint8_t> data,
                                 size_t offset,
                                 BufferFeedback *feedback);

    angle::Result commitShadowCopy(ContextMtl *contextMtl, BufferFeedback *feedback);
    angle::Result commitShadowCopy(ContextMtl *contextMtl, size_t size, BufferFeedback *feedback);

    void markConversionBuffersDirty();
    void clearConversionBuffers();

    angle::Result putDataInNewBufferAndStartUsingNewBuffer(ContextMtl *contextMtl,
                                                           angle::Span<const uint8_t> data,
                                                           size_t offset,
                                                           BufferFeedback *feedback);
    angle::Result updateExistingBufferViaBlitFromStagingBuffer(ContextMtl *contextMtl,
                                                               angle::Span<const uint8_t> data,
                                                               size_t offset);
    angle::Result copyDataToExistingBufferViaCPU(ContextMtl *contextMtl,
                                                 angle::Span<const uint8_t> data,
                                                 size_t offset);
    angle::Result updateShadowCopyThenCopyShadowToNewBuffer(ContextMtl *contextMtl,
                                                            angle::Span<const uint8_t> data,
                                                            size_t offset,
                                                            BufferFeedback *feedback);

    bool clientShadowCopyDataNeedSync(ContextMtl *contextMtl);
    void ensureShadowCopySyncedFromGPU(ContextMtl *contextMtl);
    angle::Span<uint8_t> syncAndObtainShadowCopy(ContextMtl *contextMtl, size_t offset);
    angle::Span<uint8_t> syncAndObtainShadowCopy(ContextMtl *contextMtl,
                                                 size_t offset,
                                                 size_t length);

    // Optional client side shadow buffer
    angle::MemoryBuffer mShadowCopy;

    // A cache of converted vertex data.
    std::vector<VertexConversionBufferMtl> mVertexConversionBuffers;

    std::vector<IndexConversionBufferMtl> mIndexConversionBuffers;

    // TODO(crbug.com/500942658): Consider using LRU cache
    std::deque<UniformConversionBufferMtl> mUniformConversionBuffers;

    // Each entry owns its pool so that other conversions cannot overwrite indices in use.
    using ProvokingVertexConversionCache =
        angle::base::HashingMRUCache<ProvokingVertexConversionKey,
                                     std::unique_ptr<ConversionBufferMtl>,
                                     ProvokingVertexConversionKey::Hash>;
    static constexpr size_t kMaxProvokingVertexConversionBuffers = 4;
    ProvokingVertexConversionCache mProvokingVertexConversionBuffers{
        kMaxProvokingVertexConversionBuffers};

    struct DrawIndexRangeCache
    {
        DrawIndexRangeCache(std::vector<DrawIndexRange> &&ranges_, gl::DrawElementsType indexType_)
            : ranges(ranges_), indexType(indexType_)
        {}
        const std::vector<DrawIndexRange> ranges;
        const gl::DrawElementsType indexType;
    };
    std::optional<DrawIndexRangeCache> mDrawIndexRangeCache;
    size_t mGLSize        = 0;  // size GL asked for (vs size we actually allocated)
    size_t mRevisionCount = 0;  // for generating labels only
    gl::BufferUsage mUsage;
};

inline angle::Span<uint8_t> BufferMtl::syncAndObtainShadowCopy(ContextMtl *contextMtl,
                                                               size_t offset,
                                                               size_t length)
{
    return syncAndObtainShadowCopy(contextMtl, offset).first(length);
}

class SimpleWeakBufferHolderMtl : public BufferHolderMtl
{
  public:
    SimpleWeakBufferHolderMtl();

    void set(const mtl::BufferRef &buffer) { mBufferWeakRef = buffer; }
};

}  // namespace rx

#endif /* LIBANGLE_RENDERER_METAL_BUFFERMTL_H_ */
