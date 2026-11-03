//
// Copyright 2019 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// BufferMtl.mm:
//    Implements the class methods for BufferMtl.
//

#include "libANGLE/renderer/metal/BufferMtl.h"

#include <tuple>

#include "common/debug.h"
#include "common/hash_utils.h"
#include "common/span.h"
#include "common/unsafe_buffers.h"
#include "common/utilities.h"
#include "libANGLE/ErrorStrings.h"
#include "libANGLE/renderer/metal/ContextMtl.h"
#include "libANGLE/renderer/metal/DisplayMtl.h"
#include "libANGLE/renderer/metal/mtl_buffer_manager.h"

namespace rx
{

namespace
{

// Start with a fairly small buffer size. We can increase this dynamically as we convert more data.
constexpr size_t kConvertedElementArrayBufferInitialSize = 1024 * 8;

template <typename IndexType>
angle::Result GetFirstLastIndices(const IndexType *indices,
                                  size_t count,
                                  std::pair<uint32_t, uint32_t> *outIndices)
{
    IndexType first, last;
    // Use memcpy to avoid unaligned memory access crash:
    ANGLE_UNSAFE_TODO({
        memcpy(&first, &indices[0], sizeof(first));
        memcpy(&last, &indices[count - 1], sizeof(last));
    })

    outIndices->first  = first;
    outIndices->second = last;

    return angle::Result::Continue;
}

bool isOffsetAndSizeMetalBlitCompatible(size_t offset, size_t size)
{
    // Metal requires offset and size to be multiples of 4
    return offset % 4 == 0 && size % 4 == 0;
}

}  // namespace

// ConversionBufferMtl implementation.
ConversionBufferMtl::ConversionBufferMtl(ContextMtl *contextMtl,
                                         size_t initialSize,
                                         size_t alignment)
{
    bufferPool.initialize(contextMtl, initialSize, alignment, 0);
}

ConversionBufferMtl::~ConversionBufferMtl() = default;

// IndexConversionBufferMtl implementation.
IndexConversionBufferMtl::IndexConversionBufferMtl(ContextMtl *context,
                                                   gl::DrawElementsType elemTypeIn,
                                                   bool primitiveRestartEnabledIn,
                                                   size_t offsetIn)
    : ConversionBufferMtl(context,
                          kConvertedElementArrayBufferInitialSize,
                          mtl::kIndexBufferOffsetAlignment),
      elemType(elemTypeIn),
      offset(offsetIn),
      primitiveRestartEnabled(primitiveRestartEnabledIn)
{}

// UniformConversionBufferMtl implementation
UniformConversionBufferMtl::UniformConversionBufferMtl(ContextMtl *context,
                                                       uint64_t programSerialIdIn,
                                                       std::pair<size_t, size_t> offsetIn,
                                                       size_t uniformBufferBlockSize)
    : ConversionBufferMtl(context, 0, mtl::kUniformBufferSettingOffsetMinAlignment),
      programSerialId(programSerialIdIn),
      uniformBufferBlockSize(uniformBufferBlockSize),
      offset(offsetIn)
{}

// VertexConversionBufferMtl implementation.
VertexConversionBufferMtl::VertexConversionBufferMtl(ContextMtl *context,
                                                     angle::FormatID formatIDIn,
                                                     GLuint strideIn,
                                                     size_t offsetIn)
    : ConversionBufferMtl(context, 0, mtl::kVertexAttribBufferStrideAlignment),
      formatID(formatIDIn),
      stride(strideIn),
      offset(offsetIn)
{}

// ProvokingVertexConversionKey implementation.
size_t ProvokingVertexConversionKey::Hash::operator()(const ProvokingVertexConversionKey &key) const
{
    return angle::HashMultiple(key.mode, key.elemType, key.firstIndex, key.count,
                               key.primitiveRestartEnabled);
}

bool ProvokingVertexConversionKey::operator==(const ProvokingVertexConversionKey &other) const
{
    return std::tie(mode, elemType, firstIndex, count, primitiveRestartEnabled) ==
           std::tie(other.mode, other.elemType, other.firstIndex, other.count,
                    other.primitiveRestartEnabled);
}

// BufferMtl implementation
BufferMtl::BufferMtl(const gl::BufferState &state) : BufferImpl(state) {}

BufferMtl::~BufferMtl() {}

void BufferMtl::destroy(const gl::Context *context)
{
    ContextMtl *contextMtl = mtl::GetImpl(context);
    mShadowCopy.clear();

    // if there's a buffer, give it back to the buffer manager
    if (mBuffer)
    {
        contextMtl->getBufferManager().returnBuffer(contextMtl, mBuffer);
        mBuffer = nullptr;
    }

    clearConversionBuffers();
}

angle::Result BufferMtl::setData(const gl::Context *context,
                                 gl::BufferBinding target,
                                 const void *data,
                                 size_t intendedSize,
                                 gl::BufferUsage usage,
                                 BufferFeedback *feedback,
                                 gl::ZeroFillRequired zeroFillRequired)
{
    const void *dataForImpl = data;
    if (zeroFillRequired == gl::ZeroFillRequired::Yes)
    {
        const angle::MemoryBuffer *scratchBuffer = nullptr;
        ANGLE_CHECK_GL_ALLOC(mtl::GetImpl(context),
                             context->getZeroFilledBuffer(intendedSize, &scratchBuffer));
        dataForImpl = scratchBuffer->data();
    }

    ANGLE_TRY(setDataImpl(context, target, intendedSize, usage, feedback));
    if (dataForImpl == nullptr || intendedSize == 0)
    {
        return angle::Result::Continue;
    }
    auto dataSpan = ANGLE_UNSAFE_TODO(
        angle::Span<const uint8_t>(static_cast<const uint8_t *>(dataForImpl), intendedSize));
    return setSubDataImpl(context, dataSpan, 0, feedback);
}

angle::Result BufferMtl::setSubData(const gl::Context *context,
                                    gl::BufferBinding target,
                                    const void *data,
                                    size_t size,
                                    size_t offset,
                                    BufferFeedback *feedback)
{
    ASSERT(data != nullptr);
    auto dataSpan =
        ANGLE_UNSAFE_TODO(angle::Span<const uint8_t>(static_cast<const uint8_t *>(data), size));
    return setSubDataImpl(context, dataSpan, offset, feedback);
}

angle::Result BufferMtl::copySubData(const gl::Context *context,
                                     BufferImpl *source,
                                     GLintptr sourceOffset,
                                     GLintptr destOffset,
                                     GLsizeiptr size,
                                     BufferFeedback *feedback)
{
    if (!source)
    {
        return angle::Result::Continue;
    }

    ContextMtl *contextMtl = mtl::GetImpl(context);
    auto srcMtl            = GetAs<BufferMtl>(source);

    markConversionBuffersDirty();

    if (mShadowCopy.size() > 0)
    {
        if (srcMtl->clientShadowCopyDataNeedSync(contextMtl) ||
            mBuffer->isBeingUsedByGPU(contextMtl))
        {
            // If shadow copy requires a synchronization then use blit command instead.
            // It might break a pending render pass, but still faster than synchronization with
            // GPU.
            mtl::BlitCommandEncoder *blitEncoder = contextMtl->getBlitCommandEncoder();
            blitEncoder->copyBuffer(srcMtl->getCurrentBuffer(), sourceOffset, mBuffer, destOffset,
                                    size);

            return angle::Result::Continue;
        }
        return setSubDataImpl(context,
                              srcMtl->getBufferDataReadOnly(contextMtl, sourceOffset).first(size),
                              destOffset, feedback);
    }

    mtl::BlitCommandEncoder *blitEncoder = contextMtl->getBlitCommandEncoder();
    blitEncoder->copyBuffer(srcMtl->getCurrentBuffer(), sourceOffset, mBuffer, destOffset, size);

    return angle::Result::Continue;
}

angle::Result BufferMtl::map(const gl::Context *context,
                             GLenum access,
                             void **mapPtr,
                             BufferFeedback *feedback)
{
    GLbitfield mapRangeAccess = 0;
    if ((access & GL_WRITE_ONLY_OES) != 0 || (access & GL_READ_WRITE) != 0)
    {
        mapRangeAccess |= GL_MAP_WRITE_BIT;
    }
    return mapRange(context, 0, size(), mapRangeAccess, mapPtr, feedback);
}

angle::Result BufferMtl::mapRange(const gl::Context *context,
                                  size_t offset,
                                  size_t length,
                                  GLbitfield access,
                                  void **mapPtr,
                                  BufferFeedback *feedback)
{
    if (access & GL_MAP_INVALIDATE_BUFFER_BIT)
    {
        ANGLE_TRY(setDataImpl(context, gl::BufferBinding::InvalidEnum, size(), mState.getUsage(),
                              feedback));
    }

    if (mapPtr)
    {
        ContextMtl *contextMtl = mtl::GetImpl(context);
        uint8_t *ptr           = nullptr;
        if (mShadowCopy.size() == 0)
        {
            ptr = mBuffer
                      ->mapWithOpt(contextMtl, (access & GL_MAP_WRITE_BIT) == 0,
                                   access & GL_MAP_UNSYNCHRONIZED_BIT, offset, length)
                      .data();
        }
        else
        {
            ptr = syncAndObtainShadowCopy(contextMtl, offset, length).data();
        }
        ANGLE_TRY(contextMtl->checkCommandBufferError());
        *mapPtr = ptr;
    }

    return angle::Result::Continue;
}

angle::Result BufferMtl::unmap(const gl::Context *context,
                               GLboolean *result,
                               BufferFeedback *feedback)
{
    ContextMtl *contextMtl = mtl::GetImpl(context);
    size_t offset          = static_cast<size_t>(mState.getMapOffset());
    size_t len             = static_cast<size_t>(mState.getMapLength());

    markConversionBuffersDirty();

    if (mShadowCopy.size() == 0)
    {
        ASSERT(mBuffer);
        if (mState.getAccessFlags() & GL_MAP_WRITE_BIT)
        {
            mBuffer->unmapAndFlushSubset(contextMtl, offset, len);
        }
        else
        {
            // Buffer is already mapped with readonly flag, so just unmap it, no flushing will
            // occur.
            mBuffer->unmap(contextMtl);
        }
    }
    else
    {
        if (mState.getAccessFlags() & GL_MAP_UNSYNCHRONIZED_BIT)
        {
            // Copy the mapped region without synchronization with GPU
            angle::Span<uint8_t> data             = mBuffer->mapNoSync(contextMtl, offset, len);
            angle::Span<const uint8_t> shadowData = mShadowCopy.subspan(offset, len);
            std::copy(shadowData.begin(), shadowData.end(), data.begin());
            mBuffer->unmapAndFlushSubset(contextMtl, offset, len);
        }
        else
        {
            // commit shadow copy data to GPU synchronously
            ANGLE_TRY(commitShadowCopy(contextMtl, feedback));
        }
    }

    if (result)
    {
        *result = true;
    }

    return angle::Result::Continue;
}

angle::Result BufferMtl::getIndexRange(const gl::Context *context,
                                       gl::DrawElementsType type,
                                       size_t offset,
                                       size_t count,
                                       bool primitiveRestartEnabled,
                                       gl::IndexRange *outRange)
{
    const uint8_t *indices = getBufferDataReadOnly(mtl::GetImpl(context), offset).data();

    *outRange = gl::ComputeIndexRange(type, indices, count, primitiveRestartEnabled);

    return angle::Result::Continue;
}

angle::Result BufferMtl::getFirstLastIndices(ContextMtl *contextMtl,
                                             gl::DrawElementsType type,
                                             size_t offset,
                                             size_t count,
                                             std::pair<uint32_t, uint32_t> *outIndices)
{
    const uint8_t *indices = getBufferDataReadOnly(contextMtl, offset).data();

    switch (type)
    {
        case gl::DrawElementsType::UnsignedByte:
            return GetFirstLastIndices(static_cast<const GLubyte *>(indices), count, outIndices);
        case gl::DrawElementsType::UnsignedShort:
            return GetFirstLastIndices(reinterpret_cast<const GLushort *>(indices), count,
                                       outIndices);
        case gl::DrawElementsType::UnsignedInt:
            return GetFirstLastIndices(reinterpret_cast<const GLuint *>(indices), count,
                                       outIndices);
        default:
            UNREACHABLE();
            return angle::Result::Stop;
    }
}

void BufferMtl::onDataChanged()
{
    markConversionBuffersDirty();
}

angle::Span<const uint8_t> BufferMtl::getBufferDataReadOnly(ContextMtl *contextMtl, size_t offset)
{
    if (mShadowCopy.size() == 0)
    {
        // Don't need shadow copy in this case, use the buffer directly
        return mBuffer->mapReadOnly(contextMtl, offset);
    }
    return syncAndObtainShadowCopy(contextMtl, offset);
}

bool BufferMtl::clientShadowCopyDataNeedSync(ContextMtl *contextMtl)
{
    return mBuffer->isCPUReadMemDirty();
}

void BufferMtl::ensureShadowCopySyncedFromGPU(ContextMtl *contextMtl)
{
    if (mBuffer->isCPUReadMemDirty())
    {
        angle::Span<uint8_t> shadowData = mShadowCopy.span();
        angle::Span<const uint8_t> bufferData =
            mBuffer->mapReadOnly(contextMtl, 0, shadowData.size());
        ASSERT(shadowData.size() == mBuffer->size());
        // Copy based on the shadow buffer's size, don't copy the extra padding bytes.
        std::copy(bufferData.begin(), bufferData.end(), shadowData.begin());
        mBuffer->unmap(contextMtl);

        mBuffer->resetCPUReadMemDirty();
    }
}
angle::Span<uint8_t> BufferMtl::syncAndObtainShadowCopy(ContextMtl *contextMtl, size_t offset)
{
    ASSERT(mShadowCopy.size());

    ensureShadowCopySyncedFromGPU(contextMtl);

    return mShadowCopy.subspan(offset);
}

ConversionBufferMtl *BufferMtl::getVertexConversionBuffer(ContextMtl *context,
                                                          angle::FormatID formatID,
                                                          GLuint stride,
                                                          size_t offset)
{
    for (VertexConversionBufferMtl &buffer : mVertexConversionBuffers)
    {
        if (buffer.formatID == formatID && buffer.stride == stride && buffer.offset <= offset &&
            buffer.offset % buffer.stride == offset % stride)
        {
            return &buffer;
        }
    }

    mVertexConversionBuffers.emplace_back(context, formatID, stride, offset);
    ConversionBufferMtl *conv        = &mVertexConversionBuffers.back();
    const angle::Format &angleFormat = angle::Format::Get(formatID);
    conv->bufferPool.updateAlignment(context, angleFormat.pixelBytes);

    return conv;
}

IndexConversionBufferMtl *BufferMtl::getIndexConversionBuffer(ContextMtl *context,
                                                              gl::DrawElementsType elemType,
                                                              bool primitiveRestartEnabled,
                                                              size_t offset)
{
    for (auto &buffer : mIndexConversionBuffers)
    {
        if (buffer.elemType == elemType && buffer.offset == offset &&
            buffer.primitiveRestartEnabled == primitiveRestartEnabled)
        {
            return &buffer;
        }
    }

    mIndexConversionBuffers.emplace_back(context, elemType, primitiveRestartEnabled, offset);
    return &mIndexConversionBuffers.back();
}

ConversionBufferMtl *BufferMtl::getUniformConversionBuffer(ContextMtl *context,
                                                           uint64_t programSerialId,
                                                           std::pair<size_t, size_t> offset,
                                                           size_t stdSize)
{
    for (UniformConversionBufferMtl &buffer : mUniformConversionBuffers)
    {
        if (buffer.programSerialId == programSerialId && buffer.offset.first == offset.first &&
            buffer.uniformBufferBlockSize == stdSize)
        {
            if (buffer.offset.second <= offset.second &&
                (offset.second - buffer.offset.second) % buffer.uniformBufferBlockSize == 0)
                return &buffer;
        }
    }

    constexpr size_t kMaxCacheSize = 32;
    if (mUniformConversionBuffers.size() >= kMaxCacheSize)
    {
        mUniformConversionBuffers.pop_front();
    }

    mUniformConversionBuffers.emplace_back(context, programSerialId, offset, stdSize);
    return &mUniformConversionBuffers.back();
}

ConversionBufferMtl *BufferMtl::getProvokingVertexConversionBuffer(ContextMtl *context,
                                                                   gl::PrimitiveMode mode,
                                                                   gl::DrawElementsType elemType,
                                                                   size_t firstIndex,
                                                                   GLsizei count,
                                                                   bool primitiveRestartEnabled)
{
    const ProvokingVertexConversionKey key          = {mode, elemType, firstIndex, count,
                                                       primitiveRestartEnabled};
    ProvokingVertexConversionCache::iterator cached = mProvokingVertexConversionBuffers.Get(key);
    if (cached != mProvokingVertexConversionBuffers.end())
    {
        return cached->second.get();
    }

    std::unique_ptr<ConversionBufferMtl> conversion;
    if (mProvokingVertexConversionBuffers.size() >= kMaxProvokingVertexConversionBuffers)
    {
        // Reuse the least recently used entry's pool to avoid allocating a new Metal buffer on
        // every draw when an application cycles through more ranges than fit in the cache.
        ProvokingVertexConversionCache::reverse_iterator oldest =
            mProvokingVertexConversionBuffers.rbegin();
        conversion = std::move(oldest->second);
        mProvokingVertexConversionBuffers.Erase(oldest);
        conversion->dirty  = true;
        conversion->buffer = {};
    }
    else
    {
        conversion =
            std::make_unique<ConversionBufferMtl>(context, 0, mtl::kIndexBufferOffsetAlignment);
    }
    return mProvokingVertexConversionBuffers.Put(key, std::move(conversion))->second.get();
}

void BufferMtl::markConversionBuffersDirty()
{
    for (VertexConversionBufferMtl &buffer : mVertexConversionBuffers)
    {
        buffer.dirty  = true;
        buffer.buffer = {};
    }

    for (IndexConversionBufferMtl &buffer : mIndexConversionBuffers)
    {
        buffer.dirty  = true;
        buffer.buffer = {};
    }

    for (UniformConversionBufferMtl &buffer : mUniformConversionBuffers)
    {
        buffer.dirty  = true;
        buffer.buffer = {};
    }
    for (ProvokingVertexConversionCache::value_type &entry : mProvokingVertexConversionBuffers)
    {
        entry.second->dirty  = true;
        entry.second->buffer = {};
    }
    mDrawIndexRangeCache.reset();
}

void BufferMtl::clearConversionBuffers()
{
    mVertexConversionBuffers.clear();
    mIndexConversionBuffers.clear();
    mUniformConversionBuffers.clear();
    mProvokingVertexConversionBuffers.Clear();
    mDrawIndexRangeCache.reset();
}

template <typename T>
static std::vector<DrawIndexRange> CalculateDrawIndexRanges(angle::Span<const uint8_t> data)
{
    std::vector<DrawIndexRange> result;
    angle::Span<const T> elements = angle::reinterpret_span<const T>(data);
    constexpr T restartMarker     = std::numeric_limits<T>::max();
    const auto begin              = elements.cbegin();
    const auto end                = elements.cend();
    auto it                       = begin;
    while (it != end)
    {
        if (*it == restartMarker)
        {
            ANGLE_UNSAFE_TODO(++it);
            continue;
        }
        auto rangeBegin = it;
        do
        {
            ANGLE_UNSAFE_TODO(++it);
        } while (it != end && *it != restartMarker);
        result.emplace_back(static_cast<size_t>(std::distance(begin, rangeBegin)),
                            static_cast<size_t>(std::distance(begin, it)) - 1);
    }
    return result;
}

const std::vector<DrawIndexRange> &BufferMtl::getDrawIndexRanges(ContextMtl *ctx,
                                                                 gl::DrawElementsType indexType)
{
    if (!mDrawIndexRangeCache || mDrawIndexRangeCache->indexType != indexType)
    {
        mDrawIndexRangeCache.reset();
        angle::Span<const uint8_t> data = getBufferDataReadOnly(ctx, 0);
        std::vector<DrawIndexRange> ranges;
        switch (indexType)
        {
            case gl::DrawElementsType::UnsignedByte:
                ranges = CalculateDrawIndexRanges<uint8_t>(data);
                break;
            case gl::DrawElementsType::UnsignedShort:
                ranges = CalculateDrawIndexRanges<uint16_t>(data);
                break;
            case gl::DrawElementsType::UnsignedInt:
                ranges = CalculateDrawIndexRanges<uint32_t>(data);
                break;
            default:
                ASSERT(false);
        }
        mDrawIndexRangeCache.emplace(std::move(ranges), indexType);
    }
    return mDrawIndexRangeCache->ranges;
}

const std::vector<DrawIndexRange> BufferMtl::GetDrawIndexRangesFromClientData(
    gl::DrawElementsType type,
    GLint count,
    const void *indices)
{
    auto data = ANGLE_UNSAFE_TODO(
        angle::Span<const uint8_t>(static_cast<const uint8_t *>(indices),
                                   static_cast<size_t>(count) * gl::GetDrawElementsTypeSize(type)));
    std::vector<DrawIndexRange> ranges;
    switch (type)
    {
        case gl::DrawElementsType::UnsignedByte:
            ranges = CalculateDrawIndexRanges<uint8_t>(data);
            break;
        case gl::DrawElementsType::UnsignedShort:
            ranges = CalculateDrawIndexRanges<uint16_t>(data);
            break;
        case gl::DrawElementsType::UnsignedInt:
            ranges = CalculateDrawIndexRanges<uint32_t>(data);
            break;
        default:
            ASSERT(false);
    }
    return ranges;
}

angle::Result BufferMtl::allocateNewMetalBuffer(ContextMtl *contextMtl,
                                                MTLStorageMode storageMode,
                                                size_t size,
                                                bool returnOldBufferImmediately,
                                                BufferFeedback *feedback)
{
    // Ensures no validation layer issues in std140 with data types like vec3 being 12 bytes vs 16
    // in MSL. Many buffer types can be bound as a uniform buffer, so align all buffer sizes.
    const size_t adjustedSize = roundUpPow2(std::max<size_t>(1, size), size_t(16));

    mtl::BufferManager &bufferManager = contextMtl->getBufferManager();
    if (returnOldBufferImmediately && mBuffer)
    {
        // Return the current buffer to the buffer manager
        // It will not be re-used until it's no longer in use.
        bufferManager.returnBuffer(contextMtl, mBuffer);
        mBuffer = nullptr;
    }
    ANGLE_TRY(bufferManager.getBuffer(contextMtl, storageMode, adjustedSize, mBuffer));

    feedback->internalMemoryAllocationChanged = true;

    return angle::Result::Continue;
}

angle::Result BufferMtl::setDataImpl(const gl::Context *context,
                                     gl::BufferBinding target,
                                     size_t intendedSize,
                                     gl::BufferUsage usage,
                                     BufferFeedback *feedback)
{
    ContextMtl *contextMtl             = mtl::GetImpl(context);
    const angle::FeaturesMtl &features = contextMtl->getDisplay()->getFeatures();

    // Invalidate conversion buffers
    if (mState.getSize() != static_cast<GLint64>(intendedSize))
    {
        clearConversionBuffers();
    }
    else
    {
        markConversionBuffersDirty();
    }

    mUsage  = usage;
    mGLSize = intendedSize;

    // Re-create the buffer
    auto storageMode = mtl::Buffer::getStorageModeForUsage(contextMtl, usage);
    ANGLE_TRY(allocateNewMetalBuffer(contextMtl, storageMode, intendedSize,
                                     /*returnOldBufferImmediately=*/true, feedback));

#ifndef NDEBUG
    ANGLE_MTL_OBJC_SCOPE
    {
        mBuffer->get().label = [NSString stringWithFormat:@"BufferMtl=%p", this];
    }
#endif

    // We may use shadow copy to maintain consistent data between buffers in pool
    size_t shadowSize = (!features.preferCpuForBuffersubdata.enabled &&
                         features.useShadowBuffersWhenAppropriate.enabled &&
                         mBuffer->size() <= mtl::kSharedMemBufferMaxBufSizeHint)
                            ? mBuffer->size()
                            : 0;
    ANGLE_CHECK_GL_ALLOC(contextMtl, mShadowCopy.resize(shadowSize));
    return angle::Result::Continue;
}

// states:
//  * The buffer is not use
//
//    safe = true
//
//  * The buffer has a pending blit
//
//    In this case, as long as we are only reading from it
//    via blit to a new buffer our blits will happen after existing
//    blits
//
//    safe = true
//
//  * The buffer has pending writes in a commited render encoder
//
//    In this case we're encoding commands that will happen after
//    that encoder
//
//    safe = true
//
//  * The buffer has pending writes in the current render encoder
//
//    in this case we have to split/end the render encoder
//    before we can use the buffer.
//
//    safe = false
bool BufferMtl::isSafeToReadFromBufferViaBlit(ContextMtl *contextMtl)
{
    uint64_t serial   = mBuffer->getLastWritingRenderEncoderSerial();
    bool isSameSerial = contextMtl->isCurrentRenderEncoderSerial(serial);
    return !isSameSerial;
}

angle::Result BufferMtl::updateExistingBufferViaBlitFromStagingBuffer(
    ContextMtl *contextMtl,
    angle::Span<const uint8_t> data,
    size_t offset)
{
    ASSERT(isOffsetAndSizeMetalBlitCompatible(offset, data.size()));

    mtl::BufferManager &bufferManager = contextMtl->getBufferManager();
    return bufferManager.queueBlitCopyDataToBuffer(contextMtl, data.data(), data.size(), offset,
                                                   mBuffer);
}

// * get a new or unused buffer
// * copy the new data to it
// * copy any old data not overwriten by the new data to the new buffer
// * start using the new buffer
angle::Result BufferMtl::putDataInNewBufferAndStartUsingNewBuffer(ContextMtl *contextMtl,
                                                                  angle::Span<const uint8_t> source,
                                                                  size_t offset,
                                                                  BufferFeedback *feedback)
{
    ASSERT(isOffsetAndSizeMetalBlitCompatible(offset, source.size()));

    mtl::BufferRef oldBuffer = mBuffer;
    auto storageMode         = mtl::Buffer::getStorageModeForUsage(contextMtl, mUsage);

    ANGLE_TRY(allocateNewMetalBuffer(contextMtl, storageMode, mGLSize,
                                     /*returnOldBufferImmediately=*/false, feedback));
    mBuffer->get().label = [NSString stringWithFormat:@"BufferMtl=%p(%lu)", this, ++mRevisionCount];

    angle::Span<uint8_t> data = mBuffer->mapNoSync(contextMtl, offset);
    std::copy(source.begin(), source.end(), data.begin());
    mBuffer->unmapAndFlushSubset(contextMtl, offset, source.size());

    if (offset > 0 || offset + source.size() < mGLSize)
    {
        mtl::BlitCommandEncoder *blitEncoder =
            contextMtl->getBlitCommandEncoderWithoutEndingRenderEncoder();
        if (offset > 0)
        {
            // copy old data before updated region
            blitEncoder->copyBuffer(oldBuffer, 0, mBuffer, 0, offset);
        }
        if (offset + source.size() < mGLSize)
        {
            // copy old data after updated region
            const size_t endOffset     = offset + source.size();
            const size_t endSizeToCopy = mGLSize - endOffset;
            blitEncoder->copyBuffer(oldBuffer, endOffset, mBuffer, endOffset, endSizeToCopy);
        }
    }

    mtl::BufferManager &bufferManager = contextMtl->getBufferManager();
    bufferManager.returnBuffer(contextMtl, oldBuffer);
    return angle::Result::Continue;
}

angle::Result BufferMtl::copyDataToExistingBufferViaCPU(ContextMtl *contextMtl,
                                                        angle::Span<const uint8_t> source,
                                                        size_t offset)
{
    angle::Span<uint8_t> data = mBuffer->map(contextMtl, offset, source.size());
    std::copy(source.begin(), source.end(), data.begin());
    mBuffer->unmapAndFlushSubset(contextMtl, offset, source.size());
    return angle::Result::Continue;
}

angle::Result BufferMtl::updateShadowCopyThenCopyShadowToNewBuffer(ContextMtl *contextMtl,
                                                                   angle::Span<const uint8_t> data,
                                                                   size_t offset,
                                                                   BufferFeedback *feedback)
{
    // 1. Before copying data from client, we need to synchronize modified data from GPU to
    // shadow copy first.
    ensureShadowCopySyncedFromGPU(contextMtl);

    // 2. Copy data from client to shadow copy.
    ANGLE_UNSAFE_TODO(std::copy(data.begin(), data.end(), mShadowCopy.data() + offset));

    // 3. Copy data from shadow copy to GPU.
    return commitShadowCopy(contextMtl, feedback);
}

angle::Result BufferMtl::setSubDataImpl(const gl::Context *context,
                                        angle::Span<const uint8_t> data,
                                        size_t offset,
                                        BufferFeedback *feedback)
{
    ASSERT(mBuffer);

    ContextMtl *contextMtl             = mtl::GetImpl(context);
    const angle::FeaturesMtl &features = contextMtl->getDisplay()->getFeatures();

    ANGLE_CHECK(contextMtl, offset <= mGLSize, gl::err::kInternalError, GL_INVALID_OPERATION);

    auto dataToCopy = data.first(std::min<size_t>(data.size(), mGLSize - offset));

    markConversionBuffersDirty();

    if (features.preferCpuForBuffersubdata.enabled)
    {
        return copyDataToExistingBufferViaCPU(contextMtl, dataToCopy, offset);
    }

    if (mShadowCopy.size() > 0)
    {
        return updateShadowCopyThenCopyShadowToNewBuffer(contextMtl, dataToCopy, offset, feedback);
    }
    else
    {
        bool alwaysUseStagedBufferUpdates = features.alwaysUseStagedBufferUpdates.enabled;

        if (isOffsetAndSizeMetalBlitCompatible(offset, dataToCopy.size()) &&
            (alwaysUseStagedBufferUpdates || mBuffer->isBeingUsedByGPU(contextMtl)))
        {
            if (alwaysUseStagedBufferUpdates || !isSafeToReadFromBufferViaBlit(contextMtl))
            {
                // We can't use the buffer now so copy the data
                // to a staging buffer and blit it in
                return updateExistingBufferViaBlitFromStagingBuffer(contextMtl, dataToCopy, offset);
            }
            else
            {
                return putDataInNewBufferAndStartUsingNewBuffer(contextMtl, dataToCopy, offset,
                                                                feedback);
            }
        }
        else
        {
            return copyDataToExistingBufferViaCPU(contextMtl, dataToCopy, offset);
        }
    }
}

angle::Result BufferMtl::commitShadowCopy(ContextMtl *contextMtl, BufferFeedback *feedback)
{
    return commitShadowCopy(contextMtl, mGLSize, feedback);
}

angle::Result BufferMtl::commitShadowCopy(ContextMtl *contextMtl,
                                          size_t size,
                                          BufferFeedback *feedback)
{
    auto storageMode = mtl::Buffer::getStorageModeForUsage(contextMtl, mUsage);

    size_t bufferSize = (mGLSize == 0 ? mShadowCopy.size() : mGLSize);
    ANGLE_TRY(allocateNewMetalBuffer(contextMtl, storageMode, bufferSize,
                                     /*returnOldBufferImmediately=*/true, feedback));

    if (size)
    {
        angle::Span<uint8_t> bufferData       = mBuffer->mapNoSync(contextMtl, 0, size);
        angle::Span<const uint8_t> shadowData = mShadowCopy.subspan(0, size);
        std::copy(shadowData.begin(), shadowData.end(), bufferData.begin());
        mBuffer->unmapAndFlushSubset(contextMtl, 0, size);
    }

    return angle::Result::Continue;
}

// SimpleWeakBufferHolderMtl implementation
SimpleWeakBufferHolderMtl::SimpleWeakBufferHolderMtl()
{
    mIsWeak = true;
}

}  // namespace rx
