//
// Copyright 2016 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// VertexArrayVk.cpp:
//    Implements the class methods for VertexArrayVk.
//

#include "libANGLE/renderer/vulkan/VertexArrayVk.h"
#include "common/unsafe_buffers.h"

#include "common/debug.h"
#include "common/utilities.h"
#include "libANGLE/Context.h"
#include "libANGLE/renderer/vulkan/BufferVk.h"
#include "libANGLE/renderer/vulkan/ContextVk.h"
#include "libANGLE/renderer/vulkan/FramebufferVk.h"
#include "libANGLE/renderer/vulkan/vk_format_utils.h"
#include "libANGLE/renderer/vulkan/vk_renderer.h"
#include "libANGLE/renderer/vulkan/vk_resource.h"

namespace rx
{
namespace
{
constexpr int kStreamIndexBufferCachedIndexCount = 6;
constexpr int kMaxCachedStreamIndexBuffers       = 4;
constexpr size_t kDefaultValueSize               = sizeof(gl::VertexAttribCurrentValueData::Values);
constexpr gl::VertexArray::DirtyAttribBits kDirtyFormatAttribBits =
    gl::VertexArray::DirtyAttribBits({gl::VertexArray::DIRTY_ATTRIB_ENABLED,
                                      gl::VertexArray::DIRTY_ATTRIB_POINTER,
                                      gl::VertexArray::DIRTY_ATTRIB_FORMAT});

ANGLE_INLINE bool BindingIsAligned(const angle::Format &angleFormat,
                                   VkDeviceSize offset,
                                   GLuint stride)
{
    ASSERT(stride != 0);
    GLuint mask          = angleFormat.componentAlignmentMask;
    if (mask != std::numeric_limits<GLuint>::max())
    {
        return ((offset & mask) == 0 && (stride & mask) == 0);
    }
    else
    {
        // To perform the GPU conversion for formats with components that aren't byte-aligned
        // (for example, A2BGR10 or RGB10A2), one element has to be placed in 4 bytes to perform
        // the compute shader. So, binding offset and stride has to be aligned to formatSize.
        unsigned int formatSize = angleFormat.pixelBytes;
        return (offset % formatSize == 0) && (stride % formatSize == 0);
    }
}

ANGLE_INLINE bool ClientBindingAligned(const gl::VertexAttribute &attrib,
                                       GLuint stride,
                                       size_t alignment)
{
    return reinterpret_cast<uintptr_t>(attrib.pointer) % alignment == 0 && stride % alignment == 0;
}

bool ShouldCombineAttributes(vk::Renderer *renderer,
                             const gl::VertexAttribute &attrib,
                             const gl::VertexBinding &binding)
{
    ASSERT(renderer->getFeatures().enableMergeClientAttribBuffer.enabled);
    const vk::Format &vertexFormat = renderer->getFormat(attrib.format->id);
    return !vertexFormat.getVertexLoadRequiresConversion() && binding.getDivisor() == 0 &&
           ClientBindingAligned(attrib, binding.getStride(),
                                vertexFormat.getVertexInputAlignment());
}

struct AttributeRange
{
    // Stream vertex attribute start pointer address.
    uintptr_t startAddr;
    // Stream vertex attribute end pointer address.
    uintptr_t endAddr;
    // Stream vertex attribute first used pointer address.
    // ie. startAddr + startVertex * stride.
    uintptr_t copyStartAddr;

    AttributeRange() : startAddr(0), endAddr(0), copyStartAddr(0) {}
};

// This function is a critical optimization for handling Client-Side Vertex Arrays (pointers to
// system memory passed to glVertexAttribPointer). If an application uses an interleaved vertex
// attribute layout, the memory looks like this: [Pos, Norm, UV, Pos, Norm, UV, ...]
//
// Without this function, ANGLE would trigger three separate copy operations:
//     Copy all Positions (strided).
//     Copy all Normals (strided).
//     Copy all UVs (strided).
//
// This function analyzes the memory addresses of the active attributes. It identifies attributes
// that are adjacent or overlapping in memory (interleaved). It calculates a single "bounding box"
// memory range that covers all those attributes. The result of this optimization is that it tells
// StreamVertexData to perform one large memcpy containing all the interleaved data. This
// significantly reduces CPU overhead and cache misses.
gl::AttributesMask MergeClientAttribsRange(
    vk::Renderer *renderer,
    const std::vector<gl::VertexAttribute> &attribs,
    const std::vector<gl::VertexBinding> &bindings,
    const gl::AttributesMask activeStreamedAttribs,
    size_t startVertex,
    size_t endVertex,
    std::array<AttributeRange, gl::MAX_VERTEX_ATTRIBS> &mergedRangesOut,
    std::array<size_t, gl::MAX_VERTEX_ATTRIBS> &mergedIndexesOut)
{
    ASSERT(renderer->getFeatures().enableMergeClientAttribBuffer.enabled);
    std::array<size_t, gl::MAX_VERTEX_ATTRIBS> sortedIndices;
    size_t sortedCount = 0;
    // Per-attribute alignment requirement, only meaningful for combinable attributes.
    std::array<size_t, gl::MAX_VERTEX_ATTRIBS> alignments = {};

    // 1. Analyze attributes and calculate individual range
    for (size_t attribIndex : activeStreamedAttribs)
    {
        const gl::VertexAttribute &attrib = attribs[attribIndex];
        ASSERT(attrib.enabled);
        const gl::VertexBinding &binding = bindings[attrib.bindingIndex];

        const vk::Format &vertexFormat = renderer->getFormat(attrib.format->id);

        bool canCombine = ShouldCombineAttributes(renderer, attrib, binding);
        if (canCombine)
        {
            sortedIndices[sortedCount++] = attribIndex;
            alignments[attribIndex]      = vertexFormat.getVertexInputAlignment();
        }

        const GLuint pixelBytes = vertexFormat.getActualBufferFormat().pixelBytes;
        // If combining, we copy the full stride (padding included). If not, we might pack tightly.
        const size_t destStride      = canCombine ? binding.getStride() : pixelBytes;
        const uintptr_t startAddress = reinterpret_cast<uintptr_t>(attrib.pointer);

        mergedRangesOut[attribIndex].startAddr = startAddress;
        mergedRangesOut[attribIndex].endAddr =
            startAddress + (endVertex - 1) * destStride + pixelBytes;
        mergedRangesOut[attribIndex].copyStartAddr =
            startAddress + startVertex * binding.getStride();

        // Always initialize the output index to point to itself initially
        mergedIndexesOut[attribIndex] = attribIndex;
    }

    // Fast exit if we don't have enough attributes to merge
    if (sortedCount <= 1)
    {
        return gl::AttributesMask(0);
    }

    // 2. Sort combinable attributes by memory address (Start Address Ascending)
    // We sort only the valid portion of the stack array.
    auto compareRanges = [&mergedRangesOut](size_t a, size_t b) -> bool {
        return mergedRangesOut[a].startAddr == mergedRangesOut[b].startAddr
                   ? mergedRangesOut[a].endAddr < mergedRangesOut[b].endAddr
                   : mergedRangesOut[a].startAddr < mergedRangesOut[b].startAddr;
    };
    std::sort(sortedIndices.begin(), sortedIndices.begin() + sortedCount, compareRanges);

    // 3. Merge Intervals (Linear Scan)
    // We keep track of a "Group Leader" (currentMergeIndex).
    // If the next attribute overlaps the Leader, we extend the Leader.
    gl::AttributesMask mergedAttribMask;
    size_t currentMergeIndex = sortedIndices[0];
    for (size_t i = 1; i < sortedCount; ++i)
    {
        size_t nextAttribIndex          = sortedIndices[i];
        AttributeRange &leaderRange     = mergedRangesOut[currentMergeIndex];
        const AttributeRange &nextRange = mergedRangesOut[nextAttribIndex];

        // Check for overlap or adjacency:
        // Leader: [Start ............ End]
        // Next:              [Start ............ End]
        if (leaderRange.endAddr >= nextRange.startAddr &&
            (leaderRange.startAddr % alignments[nextAttribIndex]) == 0)
        {
            // MERGE:
            // 1. Extend the leader's end address to cover the next attribute
            leaderRange.endAddr = std::max(leaderRange.endAddr, nextRange.endAddr);
            // 2. Extend the leader's copy start (min) to cover the next attribute
            leaderRange.copyStartAddr =
                std::min(leaderRange.copyStartAddr, nextRange.copyStartAddr);
            // 3. Point the next attribute to the leader index
            mergedIndexesOut[nextAttribIndex] = currentMergeIndex;

            // 4. Mark both as merged
            mergedAttribMask.set(currentMergeIndex);
            mergedAttribMask.set(nextAttribIndex);
        }
        else
        {
            // NO OVERLAP:
            // The next attribute becomes the new Leader for subsequent attributes.
            currentMergeIndex = nextAttribIndex;
        }
    }

    return mergedAttribMask;
}

void WarnOnVertexFormatConversion(ContextVk *contextVk, const vk::Format &vertexFormat)
{
    if (!vertexFormat.getVertexLoadRequiresConversion())
    {
        return;
    }

    ANGLE_VK_PERF_WARNING(
        contextVk, GL_DEBUG_SEVERITY_LOW,
        "The Vulkan driver does not support vertex attribute format 0x%04X, emulating with 0x%04X",
        vertexFormat.getIntendedFormat().glInternalFormat,
        vertexFormat.getActualBufferFormat().glInternalFormat);
}

angle::Result StreamVertexData(ContextVk *contextVk,
                               vk::BufferHelper *dstBufferHelper,
                               const uint8_t *srcData,
                               size_t bytesToCopy,
                               size_t dstOffset,
                               size_t vertexCount,
                               size_t srcStride,
                               VertexCopyFunction vertexLoadFunction)
{
    vk::Renderer *renderer = contextVk->getRenderer();

    // If the source pointer is null, it should not be accessed.
    if (srcData == nullptr)
    {
        return angle::Result::Continue;
    }

    uint8_t *dst = ANGLE_UNSAFE_TODO(dstBufferHelper->getMappedMemory() + dstOffset);

    if (vertexLoadFunction != nullptr)
    {
        vertexLoadFunction(srcData, srcStride, vertexCount, dst);
    }
    else
    {
        ANGLE_UNSAFE_TODO(memcpy(dst, srcData, bytesToCopy));
    }

    ANGLE_TRY(dstBufferHelper->flush(renderer));

    return angle::Result::Continue;
}

angle::Result StreamVertexDataWithDivisor(ContextVk *contextVk,
                                          vk::BufferHelper *dstBufferHelper,
                                          const uint8_t *srcData,
                                          size_t dstBufferSize,
                                          size_t srcStride,
                                          size_t dstStride,
                                          VertexCopyFunction vertexLoadFunction,
                                          uint32_t divisor,
                                          size_t skipVertices,
                                          size_t numSrcVertices)
{
    vk::Renderer *renderer = contextVk->getRenderer();

    uint8_t *dst = dstBufferHelper->getMappedMemory();

    // Each source vertex is used `divisor` times before advancing. Clamp to avoid OOB reads.
    ASSERT(dstBufferSize % dstStride == 0);
    const size_t dstBufferVertexCount =
        std::min(skipVertices + numSrcVertices * divisor, dstBufferSize / dstStride);

    // If base instance is non-zero, a number of vertices are skipped.  Since they are not used, set
    // them to zero.
    if (skipVertices > 0)
    {
        ANGLE_UNSAFE_TODO({
            memset(dst, 0, std::min(skipVertices * dstStride, dstBufferSize));
            dst += skipVertices * dstStride;
            srcData += skipVertices * srcStride;
        })
    }

    // Convert vertices from skipVertices to the end.
    ASSERT(divisor > 0);
    uint32_t srcVertexUseCount = 0;
    for (size_t vertexIndex = skipVertices; vertexIndex < dstBufferVertexCount; ++vertexIndex)
    {
        vertexLoadFunction(srcData, srcStride, 1, dst);
        srcVertexUseCount++;
        if (srcVertexUseCount == divisor)
        {
            ANGLE_UNSAFE_TODO(srcData += srcStride);
            srcVertexUseCount = 0;
        }
        ANGLE_UNSAFE_TODO(dst += dstStride);
    }

    // Satisfy robustness constraints (only if extension enabled)
    if (contextVk->getExtensions().robustnessAny() && dstBufferVertexCount < dstBufferSize)
    {
        ANGLE_UNSAFE_TODO(memset(dst, 0, dstBufferSize - dstBufferVertexCount * dstStride));
    }

    ANGLE_TRY(dstBufferHelper->flush(renderer));

    return angle::Result::Continue;
}

size_t GetVertexCountForRange(uint64_t srcBufferBytes,
                              uint32_t srcFormatSize,
                              uint32_t srcVertexStride)
{
    ASSERT(srcVertexStride != 0);
    ASSERT(srcFormatSize != 0);

    if (srcBufferBytes < srcFormatSize)
    {
        return 0;
    }

    // A vertex at stride-slot k occupies [k*stride, k*stride + formatSize). The maximum k such that
    // k*stride + formatSize <= srcBufferBytes is k_max = floor((srcBufferBytes - srcFormatSize) /
    // srcVertexStride). The number of vertices is k_max + 1.
    return static_cast<size_t>((srcBufferBytes - srcFormatSize) / srcVertexStride + 1);
}

size_t GetVertexCount(BufferVk *srcBuffer, const gl::VertexBinding &binding, uint32_t srcFormatSize)
{
    GLint64 size = srcBuffer->getSize();
    if (size < 0)
    {
        return 0;
    }

    uintptr_t unsignedSize = static_cast<uintptr_t>(size);
    uintptr_t offset       = binding.getOffset();
    if (unsignedSize < offset)
    {
        return 0;
    }
    // Bytes usable for vertex data.
    uint64_t bytes = unsignedSize - offset;
    GLuint stride = binding.getStride();
    if (stride == 0)
    {
        stride = srcFormatSize;
    }
    return GetVertexCountForRange(bytes, srcFormatSize, stride);
}

angle::Result CalculateMaxVertexCountForConversion(ContextVk *contextVk,
                                                   BufferVk *srcBuffer,
                                                   VertexConversionBuffer *conversion,
                                                   const angle::Format &srcFormat,
                                                   const angle::Format &dstFormat,
                                                   size_t *maxNumVerticesOut)
{
    // Initialize numVertices to 0
    *maxNumVerticesOut = 0;

    uint32_t srcFormatSize = srcFormat.pixelBytes;
    uint32_t dstFormatSize = dstFormat.pixelBytes;

    uint32_t srcStride = conversion->getCacheKey().stride;
    uint32_t dstStride = dstFormatSize;

    ASSERT(srcStride != 0);
    ASSERT(conversion->dirty());

    // Start the range from the beginning of the buffer to the end of the buffer. Then scissor it
    // with the dirtyRange.
    VkDeviceSize srcBufferSize = srcBuffer->getSize();
    size_t srcOffset  = conversion->getCacheKey().offset;
    if (srcBufferSize < srcOffset)
    {
        return angle::Result::Continue;
    }
    uint64_t srcLength = srcBufferSize - srcOffset;

    // The max number of vertices from binding to the end of the buffer
    size_t maxNumVertices = GetVertexCountForRange(srcLength, srcFormatSize, srcStride);
    if (maxNumVertices == 0)
    {
        return angle::Result::Continue;
    }

    // Allocate buffer for results
    vk::MemoryHostVisibility hostVisible = conversion->getCacheKey().hostVisible
                                               ? vk::MemoryHostVisibility::Visible
                                               : vk::MemoryHostVisibility::NonVisible;

    uint64_t dstBufferSize = static_cast<uint64_t>(maxNumVertices) * dstStride;
    ANGLE_VK_CHECK_MATH(contextVk, dstBufferSize <= std::numeric_limits<size_t>::max());

    ANGLE_TRY(contextVk->initBufferForVertexConversion(
        conversion, static_cast<size_t>(dstBufferSize), hostVisible));

    // Calculate numVertices to convert
    *maxNumVerticesOut = maxNumVertices;

    return angle::Result::Continue;
}

angle::Result CalculateOffsetAndVertexCountForDirtyRange(ContextVk *contextVk,
                                                         BufferVk *bufferVk,
                                                         VertexConversionBuffer *conversion,
                                                         const angle::Format &srcFormat,
                                                         const angle::Format &dstFormat,
                                                         const RangeDeviceSize &dirtyRange,
                                                         uint32_t *srcOffsetOut,
                                                         uint32_t *dstOffsetOut,
                                                         uint32_t *numVerticesOut)
{
    ASSERT(!dirtyRange.empty());
    unsigned srcFormatSize = srcFormat.pixelBytes;
    unsigned dstFormatSize = dstFormat.pixelBytes;

    uint32_t srcStride = conversion->getCacheKey().stride;
    uint32_t dstStride = dstFormatSize;

    ASSERT(srcStride != 0);
    ASSERT(conversion->dirty());

    // Start the range with the range from the the beginning of the buffer to the end of
    // buffer. Then scissor it with the dirtyRange.
    size_t srcOffset = conversion->getCacheKey().offset;
    size_t dstOffset = 0;

    VkDeviceSize srcBufferSize = bufferVk->getSize();
    uint64_t srcLength         = srcBufferSize - srcOffset;

    uint64_t currentSrcOffset = srcOffset;
    uint64_t currentDstOffset = dstOffset;
    uint64_t currentSrcLength = srcLength;

    // Adjust offset to the beginning of the dirty range
    if (dirtyRange.low() > srcOffset)
    {
        uint64_t vertexCountToSkip =
            (static_cast<uint64_t>(dirtyRange.low()) - srcOffset) / srcStride;
        uint64_t srcBytesToSkip = vertexCountToSkip * srcStride;
        uint64_t dstBytesToSkip = vertexCountToSkip * dstStride;

        currentSrcOffset += srcBytesToSkip;
        currentSrcLength -= srcBytesToSkip;
        currentDstOffset += dstBytesToSkip;
    }

    // Adjust dstOffset to align to 4 bytes. The GPU convert code path always write a uint32_t and
    // must aligned at 4 bytes. We could possibly make it able to store at unaligned uint32_t but
    // performance will be worse than just convert a few extra data.
    while ((currentDstOffset % 4) != 0)
    {
        ASSERT(currentDstOffset >= dstStride && currentSrcOffset >= srcStride);
        currentDstOffset -= dstStride;
        currentSrcOffset -= srcStride;
        currentSrcLength += srcStride;
    }

    // Adjust length
    if (dirtyRange.high() < srcBufferSize)
    {
        ASSERT(dirtyRange.high() >= currentSrcOffset);
        currentSrcLength = dirtyRange.high() - currentSrcOffset;
    }

    // Calculate numVertices to convert
    size_t numVertices = GetVertexCountForRange(currentSrcLength, srcFormatSize, srcStride);

    ANGLE_VK_CHECK_MATH(contextVk, numVertices <= std::numeric_limits<uint32_t>::max());
    ANGLE_VK_CHECK_MATH(contextVk, currentSrcOffset <= std::numeric_limits<uint32_t>::max());
    ANGLE_VK_CHECK_MATH(contextVk, currentDstOffset <= std::numeric_limits<uint32_t>::max());

    *numVerticesOut = static_cast<uint32_t>(numVertices);
    *srcOffsetOut   = static_cast<uint32_t>(currentSrcOffset);
    *dstOffsetOut   = static_cast<uint32_t>(currentDstOffset);

    return angle::Result::Continue;
}
}  // anonymous namespace

VertexArrayVk::VertexArrayVk(ContextVk *contextVk,
                             const gl::VertexArrayState &state,
                             const gl::VertexArrayBuffers &vertexArrayBuffers)
    : VertexArrayImpl(state, vertexArrayBuffers),
      mCurrentArrayBufferHandles{},
      mCurrentArrayBufferOffsets{},
      mCurrentArrayBufferSizes{},
      mCurrentArrayBuffers{},
      mDefaultAttribFormatIDs{},
      mCurrentEmptyBufferMask{},
      mVertexInputBindingDescs{},
      mVertexInputAttribDescs{},
      mCurrentElementArrayBuffer(nullptr),
      mLineLoopHelper(contextVk->getRenderer()),
      mDirtyLineLoopTranslation(true)
{
    vk::BufferHelper &emptyBuffer = contextVk->getEmptyBuffer();

    mCurrentArrayBufferHandles.fill(emptyBuffer.getBuffer().getHandle());
    mCurrentArrayBufferOffsets.fill(0);
    mCurrentArrayBufferSizes.fill(0);
    mCurrentArrayBuffers.fill(&emptyBuffer);
    mCurrentEmptyBufferMask.set();

    // When supportsVertexInputDynamicState is enabled,
    // VUID-VkVertexInputBindingDescription2EXT-divisor-06227 requires divisor to be 1 when
    // inputRate is VK_VERTEX_INPUT_RATE_VERTEX. When supportsVertexInputDynamicState is disabled,
    // the divisor goes through GraphicsPipelineDesc struct, which is a packed structure. It uses
    // "0" divisor value to indicate it is VK_VERTEX_INPUT_RATE_VERTEX.
    mDivisorForVertexInputRateVertex =
        contextVk->getFeatures().supportsVertexInputDynamicState.enabled ? 1 : 0;

    for (uint32_t attribIndex = 0; attribIndex < mVertexInputBindingDescs.size(); attribIndex++)
    {
        mVertexInputBindingDescs[attribIndex] = VkVertexInputBindingDescription2EXT{
            VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT,
            nullptr,
            attribIndex,
            0,
            static_cast<VkVertexInputRate>(VK_VERTEX_INPUT_RATE_VERTEX),
            mDivisorForVertexInputRateVertex};
    }

    for (uint32_t attribIndex = 0; attribIndex < mVertexInputAttribDescs.size(); attribIndex++)
    {
        mVertexInputAttribDescs[attribIndex] = VkVertexInputAttributeDescription2EXT{
            VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT,
            nullptr,
            attribIndex,
            attribIndex,
            VK_FORMAT_R8G8B8A8_UNORM,
            0};
    }
}

VertexArrayVk::~VertexArrayVk() {}

void VertexArrayVk::destroy(const gl::Context *context)
{
    ContextVk *contextVk = vk::GetImpl(context);

    for (std::unique_ptr<vk::BufferHelper> &buffer : mCachedStreamIndexBuffers)
    {
        buffer->release(contextVk);
    }

    mStreamedIndexData.release(contextVk);
    mTranslatedByteIndexData.release(contextVk);
    mTranslatedByteIndirectData.release(contextVk);
    mLineLoopHelper.release(contextVk);
}

angle::Result VertexArrayVk::convertIndexBufferGPU(ContextVk *contextVk,
                                                   BufferVk *bufferVk,
                                                   const void *indices)
{
    uintptr_t offsetIntoSrcData = reinterpret_cast<uintptr_t>(indices);
    size_t srcDataSize          = static_cast<size_t>(bufferVk->getSize()) - offsetIntoSrcData;

    // Allocate buffer for results
    ANGLE_TRY(contextVk->initBufferForVertexConversion(&mTranslatedByteIndexData,
                                                       sizeof(GLushort) * srcDataSize,
                                                       vk::MemoryHostVisibility::NonVisible));
    mCurrentElementArrayBuffer = mTranslatedByteIndexData.getBuffer();

    vk::BufferHelper *dst = mTranslatedByteIndexData.getBuffer();
    vk::BufferHelper *src = &bufferVk->getBuffer();

    // Copy relevant section of the source into destination at allocated offset.  Note that the
    // offset returned by allocate() above is in bytes. As is the indices offset pointer.
    UtilsVk::ConvertIndexParameters params = {};
    params.srcOffset                       = static_cast<uint32_t>(offsetIntoSrcData);
    params.dstOffset                       = 0;
    // Remaining space in buffer was already computed above.
    params.maxIndex = static_cast<uint32_t>(srcDataSize);

    ANGLE_TRY(contextVk->getUtils().convertIndexBuffer(contextVk, dst, src, params));
    mTranslatedByteIndexData.clearDirty();

    return angle::Result::Continue;
}

angle::Result VertexArrayVk::convertIndexBufferIndirectGPU(ContextVk *contextVk,
                                                           vk::BufferHelper *srcIndirectBuf,
                                                           VkDeviceSize srcIndirectBufOffset,
                                                           vk::BufferHelper **indirectBufferVkOut)
{
    size_t srcDataSize = static_cast<size_t>(mCurrentElementArrayBuffer->getSize());
    ASSERT(mCurrentElementArrayBuffer == &vk::GetImpl(getElementArrayBuffer())->getBuffer());

    vk::BufferHelper *srcIndexBuf = mCurrentElementArrayBuffer;

    // Allocate buffer for results
    ANGLE_TRY(contextVk->initBufferForVertexConversion(&mTranslatedByteIndexData,
                                                       sizeof(GLushort) * srcDataSize,
                                                       vk::MemoryHostVisibility::NonVisible));
    vk::BufferHelper *dstIndexBuf = mTranslatedByteIndexData.getBuffer();

    ANGLE_TRY(contextVk->initBufferForVertexConversion(&mTranslatedByteIndirectData,
                                                       sizeof(VkDrawIndexedIndirectCommand),
                                                       vk::MemoryHostVisibility::NonVisible));
    vk::BufferHelper *dstIndirectBuf = mTranslatedByteIndirectData.getBuffer();

    // Save new element array buffer
    mCurrentElementArrayBuffer = dstIndexBuf;
    // Tell caller what new indirect buffer is
    *indirectBufferVkOut = dstIndirectBuf;

    // Copy relevant section of the source into destination at allocated offset.  Note that the
    // offset returned by allocate() above is in bytes. As is the indices offset pointer.
    UtilsVk::ConvertIndexIndirectParameters params = {};
    params.srcIndirectBufOffset                    = static_cast<uint32_t>(srcIndirectBufOffset);
    params.srcIndexBufOffset                       = 0;
    params.dstIndexBufOffset                       = 0;
    params.maxIndex                                = static_cast<uint32_t>(srcDataSize);
    params.dstIndirectBufOffset                    = 0;

    ANGLE_TRY(contextVk->getUtils().convertIndexIndirectBuffer(
        contextVk, srcIndirectBuf, srcIndexBuf, dstIndirectBuf, dstIndexBuf, params));

    mTranslatedByteIndexData.clearDirty();
    mTranslatedByteIndirectData.clearDirty();

    return angle::Result::Continue;
}

angle::Result VertexArrayVk::handleLineLoopIndexIndirect(ContextVk *contextVk,
                                                         gl::DrawElementsType glIndexType,
                                                         vk::BufferHelper *srcIndexBuffer,
                                                         vk::BufferHelper *srcIndirectBuffer,
                                                         VkDeviceSize indirectBufferOffset,
                                                         vk::BufferHelper **indexBufferOut,
                                                         vk::BufferHelper **indirectBufferOut)
{
    return mLineLoopHelper.streamIndicesIndirect(contextVk, glIndexType, srcIndexBuffer,
                                                 srcIndirectBuffer, indirectBufferOffset,
                                                 indexBufferOut, indirectBufferOut);
}

angle::Result VertexArrayVk::handleLineLoopIndirectDraw(const gl::Context *context,
                                                        vk::BufferHelper *indirectBufferVk,
                                                        VkDeviceSize indirectBufferOffset,
                                                        vk::BufferHelper **indexBufferOut,
                                                        vk::BufferHelper **indirectBufferOut)
{
    size_t maxVertexCount = 0;
    ContextVk *contextVk  = vk::GetImpl(context);
    const gl::AttributesMask activeAttribs = context->getActiveBufferedAttribsMask();

    const auto &attribs  = mState.getVertexAttributes();
    const auto &bindings = mState.getVertexBindings();

    for (size_t attribIndex : activeAttribs)
    {
        const gl::VertexAttribute &attrib = attribs[attribIndex];
        ASSERT(attrib.enabled);
        VkDeviceSize bufSize             = getCurrentArrayBuffers()[attribIndex]->getSize();
        const gl::VertexBinding &binding = bindings[attrib.bindingIndex];
        size_t stride                    = binding.getStride();
        size_t vertexCount               = static_cast<size_t>(bufSize / stride);
        if (vertexCount > maxVertexCount)
        {
            maxVertexCount = vertexCount;
        }
    }
    ANGLE_TRY(mLineLoopHelper.streamArrayIndirect(contextVk, maxVertexCount + 1, indirectBufferVk,
                                                  indirectBufferOffset, indexBufferOut,
                                                  indirectBufferOut));

    return angle::Result::Continue;
}

angle::Result VertexArrayVk::convertIndexBufferCPU(ContextVk *contextVk,
                                                   gl::DrawElementsType indexType,
                                                   size_t indexCount,
                                                   const void *sourcePointer,
                                                   BufferBindingDirty *bindingDirty)
{
    ASSERT(!getElementArrayBuffer() || indexType == gl::DrawElementsType::UnsignedByte);
    vk::Renderer *renderer = contextVk->getRenderer();
    size_t elementSize     = contextVk->getVkIndexTypeSize(indexType);
    const size_t amount    = elementSize * indexCount;

    // Applications often time draw a quad with two triangles. This is try to catch all the
    // common used element array buffer with pre-created BufferHelper objects to improve
    // performance.
    if (indexCount == kStreamIndexBufferCachedIndexCount &&
        indexType == gl::DrawElementsType::UnsignedShort)
    {
        for (std::unique_ptr<vk::BufferHelper> &buffer : mCachedStreamIndexBuffers)
        {
            void *ptr = buffer->getMappedMemory();
            if (ANGLE_UNSAFE_TODO(memcmp(sourcePointer, ptr, amount)) == 0)
            {
                // Found a matching cached buffer, use the cached internal index buffer.
                *bindingDirty              = mCurrentElementArrayBuffer == buffer.get()
                                                 ? BufferBindingDirty::No
                                                 : BufferBindingDirty::Yes;
                mCurrentElementArrayBuffer = buffer.get();
                return angle::Result::Continue;
            }
        }

        // If we still have capacity, cache this index buffer for future use.
        if (mCachedStreamIndexBuffers.size() < kMaxCachedStreamIndexBuffers)
        {
            std::unique_ptr<vk::BufferHelper> buffer = std::make_unique<vk::BufferHelper>();
            ANGLE_TRY(contextVk->initBufferAllocation(
                buffer.get(),
                renderer->getVertexConversionBufferMemoryTypeIndex(
                    vk::MemoryHostVisibility::Visible),
                amount, renderer->getVertexConversionBufferAlignment(), BufferUsageType::Static));
            ANGLE_UNSAFE_TODO(memcpy(buffer->getMappedMemory(), sourcePointer, amount));
            ANGLE_TRY(buffer->flush(renderer));

            mCachedStreamIndexBuffers.push_back(std::move(buffer));

            *bindingDirty              = BufferBindingDirty::Yes;
            mCurrentElementArrayBuffer = mCachedStreamIndexBuffers.back().get();
            return angle::Result::Continue;
        }
    }

    ANGLE_TRY(contextVk->initBufferForVertexConversion(&mStreamedIndexData, amount,
                                                       vk::MemoryHostVisibility::Visible));
    mCurrentElementArrayBuffer = mStreamedIndexData.getBuffer();
    GLubyte *dst               = mCurrentElementArrayBuffer->getMappedMemory();
    *bindingDirty              = BufferBindingDirty::Yes;

    if (contextVk->shouldConvertUint8VkIndexType(indexType))
    {
        // Unsigned bytes don't have direct support in Vulkan so we have to expand the
        // memory to a GLushort.
        const GLubyte *in     = static_cast<const GLubyte *>(sourcePointer);
        GLushort *expandedDst = reinterpret_cast<GLushort *>(dst);
        bool primitiveRestart = contextVk->getState().isPrimitiveRestartEnabled();

        constexpr GLubyte kUnsignedByteRestartValue   = 0xFF;
        constexpr GLushort kUnsignedShortRestartValue = 0xFFFF;

        if (primitiveRestart)
        {
            for (size_t index = 0; index < indexCount; index++)
            {
                GLushort value = static_cast<GLushort>(ANGLE_UNSAFE_TODO(in[index]));
                if (ANGLE_UNSAFE_TODO(in[index]) == kUnsignedByteRestartValue)
                {
                    // Convert from 8-bit restart value to 16-bit restart value
                    value = kUnsignedShortRestartValue;
                }
                ANGLE_UNSAFE_TODO(expandedDst[index]) = value;
            }
        }
        else
        {
            // Fast path for common case.
            for (size_t index = 0; index < indexCount; index++)
            {
                ANGLE_UNSAFE_TODO(expandedDst[index]) =
                    static_cast<GLushort>(ANGLE_UNSAFE_TODO(in[index]));
            }
        }
    }
    else
    {
        // The primitive restart value is the same for OpenGL and Vulkan,
        // so there's no need to perform any conversion.
        ANGLE_UNSAFE_TODO(memcpy(dst, sourcePointer, amount));
    }

    mStreamedIndexData.clearDirty();

    return mCurrentElementArrayBuffer->flush(contextVk->getRenderer());
}

// We assume the buffer is completely full of the same kind of data and convert
// and/or align it as we copy it to a buffer. The assumption could be wrong
// but the alternative of copying it piecemeal on each draw would have a lot more
// overhead.
angle::Result VertexArrayVk::convertVertexBufferGPU(ContextVk *contextVk,
                                                    BufferVk *srcBuffer,
                                                    VertexConversionBuffer *conversion,
                                                    const angle::Format &srcFormat,
                                                    const angle::Format &dstFormat)
{
    uint32_t srcStride = conversion->getCacheKey().stride;
    ASSERT(srcStride % (srcFormat.pixelBytes / srcFormat.channelCount) == 0);

    size_t maxNumVertices;
    ANGLE_TRY(CalculateMaxVertexCountForConversion(contextVk, srcBuffer, conversion, srcFormat,
                                                   dstFormat, &maxNumVertices));
    if (maxNumVertices == 0)
    {
        return angle::Result::Continue;
    }

    vk::BufferHelper *srcBufferHelper = &srcBuffer->getBuffer();
    vk::BufferHelper *dstBuffer       = conversion->getBuffer();

    UtilsVk::OffsetAndVertexCounts additionalOffsetVertexCounts;

    UtilsVk::ConvertVertexParameters params = {};
    params.srcFormat   = &srcFormat;
    params.dstFormat   = &dstFormat;
    params.srcStride   = srcStride;
    params.vertexCount = 0;

    if (conversion->isEntireBufferDirty())
    {
        params.vertexCount = static_cast<uint32_t>(maxNumVertices);
        params.srcOffset   = static_cast<uint32_t>(conversion->getCacheKey().offset);
        params.dstOffset   = 0;
    }
    else
    {
        // dirtyRanges may overlap with each other. Try to do a quick merge to reduce the number of
        // dispatch calls as well as avoid redundant conversion in the overlapped area.
        conversion->consolidateDirtyRanges();

        const std::vector<RangeDeviceSize> &dirtyRanges = conversion->getDirtyBufferRanges();
        additionalOffsetVertexCounts.reserve(dirtyRanges.size());

        for (const RangeDeviceSize &dirtyRange : dirtyRanges)
        {
            if (dirtyRange.empty())
            {
                // consolidateDirtyRanges may end up with invalid range if it gets merged.
                continue;
            }

            uint32_t srcOffset, dstOffset, numVertices;
            ANGLE_TRY(CalculateOffsetAndVertexCountForDirtyRange(
                contextVk, srcBuffer, conversion, srcFormat, dstFormat, dirtyRange, &srcOffset,
                &dstOffset, &numVertices));
            if (params.vertexCount == 0)
            {
                params.vertexCount = numVertices;
                params.srcOffset   = srcOffset;
                params.dstOffset   = dstOffset;
            }
            else
            {
                additionalOffsetVertexCounts.emplace_back();
                additionalOffsetVertexCounts.back().srcOffset   = srcOffset;
                additionalOffsetVertexCounts.back().dstOffset   = dstOffset;
                additionalOffsetVertexCounts.back().vertexCount = numVertices;
            }
        }
    }
    ANGLE_TRY(contextVk->getUtils().convertVertexBuffer(contextVk, dstBuffer, srcBufferHelper,
                                                        params, additionalOffsetVertexCounts));
    conversion->clearDirty();

    return angle::Result::Continue;
}

angle::Result VertexArrayVk::convertVertexBufferCPU(ContextVk *contextVk,
                                                    BufferVk *srcBuffer,
                                                    VertexConversionBuffer *conversion,
                                                    const angle::Format &srcFormat,
                                                    const angle::Format &dstFormat,
                                                    const VertexCopyFunction vertexLoadFunction)
{
    ANGLE_TRACE_EVENT0("gpu.angle", "VertexArrayVk::convertVertexBufferCpu");

    size_t maxNumVertices;
    ANGLE_TRY(CalculateMaxVertexCountForConversion(contextVk, srcBuffer, conversion, srcFormat,
                                                   dstFormat, &maxNumVertices));
    if (maxNumVertices == 0)
    {
        return angle::Result::Continue;
    }

    uint8_t *src = nullptr;
    ANGLE_TRY(srcBuffer->mapForReadAccessOnly(contextVk, reinterpret_cast<void **>(&src)));
    uint32_t srcStride      = conversion->getCacheKey().stride;

    if (conversion->isEntireBufferDirty())
    {
        size_t srcOffset        = conversion->getCacheKey().offset;
        size_t dstOffset        = 0;
        const uint8_t *srcBytes = ANGLE_UNSAFE_TODO(src + srcOffset);
        size_t bytesToCopy      = maxNumVertices * dstFormat.pixelBytes;
        ANGLE_TRY(StreamVertexData(contextVk, conversion->getBuffer(), srcBytes, bytesToCopy,
                                   dstOffset, maxNumVertices, srcStride, vertexLoadFunction));
    }
    else
    {
        // dirtyRanges may overlap with each other. Try to do a quick merge to avoid redundant
        // conversion in the overlapped area.
        conversion->consolidateDirtyRanges();

        const std::vector<RangeDeviceSize> &dirtyRanges = conversion->getDirtyBufferRanges();
        for (const RangeDeviceSize &dirtyRange : dirtyRanges)
        {
            if (dirtyRange.empty())
            {
                // consolidateDirtyRanges may end up with invalid range if it gets merged.
                continue;
            }

            // Use numVertices instead of maxNumVertices to calculate bytesToCopy to avoid buffer
            // overrun.
            uint32_t srcOffset, dstOffset, numVertices;
            ANGLE_TRY(CalculateOffsetAndVertexCountForDirtyRange(
                contextVk, srcBuffer, conversion, srcFormat, dstFormat, dirtyRange, &srcOffset,
                &dstOffset, &numVertices));
            ASSERT(numVertices <= maxNumVertices);

            if (numVertices > 0)
            {
                const uint8_t *srcBytes = ANGLE_UNSAFE_TODO(src + srcOffset);

                size_t bytesToCopy = numVertices * dstFormat.pixelBytes;
                ANGLE_TRY(StreamVertexData(contextVk, conversion->getBuffer(), srcBytes,
                                           bytesToCopy, dstOffset, numVertices, srcStride,
                                           vertexLoadFunction));
            }
        }
    }

    conversion->clearDirty();
    ANGLE_TRY(srcBuffer->unmapReadAccessOnly(contextVk));

    return angle::Result::Continue;
}

void VertexArrayVk::updateCurrentElementArrayBuffer()
{
    ASSERT(getElementArrayBuffer() != nullptr);
    ASSERT(getElementArrayBuffer()->getSize() > 0);

    BufferVk *bufferVk         = vk::GetImpl(getElementArrayBuffer());
    mCurrentElementArrayBuffer = &bufferVk->getBuffer();
}

gl::VertexArray::DirtyBits VertexArrayVk::checkBufferForDirtyBits(
    const gl::Context *context,
    const gl::VertexArrayBufferBindingMask bufferBindingMask)
{
    gl::VertexArray::DirtyBits dirtyBits;

    const std::vector<gl::VertexAttribute> &attribs = mState.getVertexAttributes();
    const std::vector<gl::VertexBinding> &bindings  = mState.getVertexBindings();

    // Element buffer is not in bindings yet, has to handle separately.
    dirtyBits.set(gl::VertexArray::DIRTY_BIT_ELEMENT_ARRAY_BUFFER);

    gl::VertexArrayBufferBindingMask bindingMask = bufferBindingMask;
    bindingMask.reset(gl::kElementArrayBufferIndex);

    for (size_t bindingIndex : bindingMask)
    {
        const gl::Buffer *bufferGL    = getVertexArrayBuffer(bindingIndex);
        vk::BufferSerial bufferSerial = vk::GetImpl(bufferGL)->getBufferSerial();
        gl::AttributesMask enabledAttributeMask =
            bindings[bindingIndex].getBoundAttributesMask() & mState.getEnabledAttributesMask();
        for (size_t attribIndex : enabledAttributeMask)
        {
            ASSERT(attribs[attribIndex].enabled);
            if (!bufferSerial.valid() || bufferSerial != mCurrentArrayBufferSerial[attribIndex])
            {
                dirtyBits.set(gl::VertexArray::DIRTY_BIT_BINDING_0 + bindingIndex);
                break;
            }
        }
    }

    // buffer content may have changed when it became non-current. In that case we always assume
    // buffer data has changed.
    if (mContentsObserverBindingsMask.any())
    {
        uint64_t bits = mContentsObserverBindingsMask.bits();
        bits <<= gl::VertexArray::DIRTY_BIT_BUFFER_DATA_0;
        dirtyBits |= gl::VertexArray::DirtyBits(bits);
    }

    return dirtyBits;
}

angle::Result VertexArrayVk::syncState(const gl::Context *context,
                                       const gl::VertexArray::DirtyBits &dirtyBits,
                                       gl::VertexArray::DirtyAttribBitsArray *attribBits,
                                       gl::VertexArray::DirtyBindingBitsArray *bindingBits)
{
    ASSERT(dirtyBits.any());

    ContextVk *contextVk = vk::GetImpl(context);
    vk::Renderer *renderer = contextVk->getRenderer();
    contextVk->getPerfCounters().vertexArraySyncStateCalls++;

    gl::AttributesMask prevEnabledAttribsMask = mCurrentEnabledAttribsMask;
    mCurrentEnabledAttribsMask                = mState.getEnabledAttributesMask();

    const std::vector<gl::VertexAttribute> &attribs = mState.getVertexAttributes();
    const std::vector<gl::VertexBinding> &bindings  = mState.getVertexBindings();

    // Split out dirtyBits into bindingBits and attributeBits
    gl::BufferBindingMask bufferBindingDirtyBits(
        static_cast<uint32_t>(dirtyBits.bits() >> gl::VertexArray::DIRTY_BIT_BINDING_0));
    gl::BufferBindingMask bufferDataDirtyBits(
        static_cast<uint32_t>(dirtyBits.bits() >> gl::VertexArray::DIRTY_BIT_BUFFER_DATA_0));
    gl::AttributesMask attribDirtyBits(
        static_cast<uint32_t>(dirtyBits.bits() >> gl::VertexArray::DIRTY_BIT_ATTRIB_0));

    // Fold DIRTY_BIT_BINDING_n into DIRTY_BIT_ATTRIB_n
    if (bufferBindingDirtyBits.any())
    {
        for (size_t bindingIndex : bufferBindingDirtyBits)
        {
            attribDirtyBits |= bindings[bindingIndex].getBoundAttributesMask();
            mDivisorExceedMaxSupportedValueBindingMask.set(
                bindingIndex,
                bindings[bindingIndex].getDivisor() > renderer->getMaxVertexAttribDivisor());
        }
    }

    bool elementBufferDirty = dirtyBits[gl::VertexArray::DIRTY_BIT_ELEMENT_ARRAY_BUFFER] ||
                              dirtyBits[gl::VertexArray::DIRTY_BIT_ELEMENT_ARRAY_BUFFER_DATA];
    if (elementBufferDirty)
    {
        gl::Buffer *bufferGL = getElementArrayBuffer();
        if (bufferGL && bufferGL->getSize() > 0)
        {
            // Note that just updating buffer data may still result in a new
            // vk::BufferHelper allocation.
            updateCurrentElementArrayBuffer();
        }
        else
        {
            mCurrentElementArrayBuffer = nullptr;
        }

        mLineLoopBufferFirstIndex.reset();
        mLineLoopBufferLastIndex.reset();
        ANGLE_TRY(contextVk->onIndexBufferChange(mCurrentElementArrayBuffer));
        mDirtyLineLoopTranslation = true;
    }

    // Update mStreamingVertexAttribsMask
    mStreamingVertexAttribsMask = mState.getClientMemoryAttribsMask();
    if (ANGLE_UNLIKELY(mDivisorExceedMaxSupportedValueBindingMask.any()))
    {
        for (size_t bindingIndex : mDivisorExceedMaxSupportedValueBindingMask)
        {
            mStreamingVertexAttribsMask |= bindings[bindingIndex].getBoundAttributesMask();
        }
    }
    mStreamingVertexAttribsMask &= mState.getEnabledAttributesMask();

    // mStreamingVertexAttribsMask may have changed, update mCurrentActiveStreamingAttribsMask.
    contextVk->updateCurrentActiveStreamingAttribsMask(context);

    // All enabled attributes that are dirty
    gl::AttributesMask enabledAttribDirtyBits = attribDirtyBits & mState.getEnabledAttributesMask();

    // We are going to re-evaluate mNeedsConversionAttribsMask inside syncDirtyEnabledAttrib
    mNeedsConversionAttribsMask &= ~enabledAttribDirtyBits;

    // Sync all enabled non-streaming attributes that are dirty
    const gl::AttributesMask enabledNonStreamAttribDirtyBits =
        enabledAttribDirtyBits & ~mStreamingVertexAttribsMask;
    for (size_t attribIndex : enabledNonStreamAttribDirtyBits)
    {
        // This will also update mNeedsConversionAttribMask
        syncDirtyEnabledNonStreamingAttrib(contextVk, attribs[attribIndex],
                                           bindings[attribs[attribIndex].bindingIndex], attribIndex,
                                           (*attribBits)[attribIndex]);
    }

    // Sync all enabled and streaming attributes that are dirty
    const gl::AttributesMask enabledStreamAttribDirtyBits =
        enabledAttribDirtyBits & mStreamingVertexAttribsMask;
    for (size_t attribIndex : enabledStreamAttribDirtyBits)
    {
        // This will also update mNeedsConversionAttribMask
        syncDirtyEnabledStreamingAttrib(contextVk, attribs[attribIndex],
                                        bindings[attribs[attribIndex].bindingIndex], attribIndex,
                                        (*attribBits)[attribIndex]);
    }

    // Sync all enabled attributes that needs data conversion.
    if (ANGLE_UNLIKELY(mNeedsConversionAttribsMask.any()))
    {
        // Update mContentsObserverBindingsMask
        mContentsObserverBindingsMask.reset();
        mContentsObserverBindingsMask.set(gl::kElementArrayBufferIndex);
        for (size_t attribIndex : mNeedsConversionAttribsMask)
        {
            mContentsObserverBindingsMask.set(attribs[attribIndex].bindingIndex);
        }

        // As long as attribute has changed or data has changed, we need to reprocess it.
        gl::AttributesMask needsConversionAttribDirtyBits = attribDirtyBits;
        for (size_t bindingIndex : bufferDataDirtyBits)
        {
            needsConversionAttribDirtyBits |= bindings[bindingIndex].getBoundAttributesMask();
        }

        needsConversionAttribDirtyBits &= mNeedsConversionAttribsMask;
        needsConversionAttribDirtyBits &= mState.getEnabledAttributesMask();

        for (size_t attribIndex : needsConversionAttribDirtyBits)
        {
            ANGLE_TRY(syncNeedsConversionAttrib(contextVk, attribs[attribIndex],
                                                bindings[attribs[attribIndex].bindingIndex],
                                                attribIndex));
        }
    }

    // Sync all disabled attributes that are dirty. We only need to handle attributes that was
    // changed from enabled to disabled.
    const gl::AttributesMask prevEnabledNowDisabledAttribBits =
        prevEnabledAttribsMask & ~mState.getEnabledAttributesMask();
    ASSERT((prevEnabledNowDisabledAttribBits & attribDirtyBits) ==
           prevEnabledNowDisabledAttribBits);
    if (prevEnabledNowDisabledAttribBits.any())
    {
        // disabled attributes maybe active or inactive. We only need to set to empty buffer for
        // inactive ones.
        const gl::AttributesMask disabledInactiveAttribBits =
            prevEnabledNowDisabledAttribBits & ~context->getActiveDefaultAttribsMask();
        syncDirtyDisabledAttribs(contextVk, disabledInactiveAttribBits);

        // default attributes handler will update active attributes.
        const gl::AttributesMask defaultAttribBits =
            prevEnabledNowDisabledAttribBits & context->getActiveDefaultAttribsMask();
        contextVk->invalidateDefaultAttributes(defaultAttribBits);
    }

    if (ANGLE_UNLIKELY(mDivisorExceedMaxSupportedValueBindingMask.any()))
    {
        gl::AttributesMask divisorExceedMaxSupportedValueAttribMask;
        for (size_t bindingIndex : mDivisorExceedMaxSupportedValueBindingMask)
        {
            divisorExceedMaxSupportedValueAttribMask |=
                bindings[bindingIndex].getBoundAttributesMask();
        }
        // Set divisor to 1 for attribs with emulated divisor, since we always go down the stream
        // code path
        divisorExceedMaxSupportedValueAttribMask &= mCurrentEnabledAttribsMask;
        for (size_t attribIndex : divisorExceedMaxSupportedValueAttribMask)
        {
            ASSERT(mStreamingVertexAttribsMask.test(attribIndex));
            mVertexInputBindingDescs[attribIndex].divisor = 1;
        }
    }

    ANGLE_TRY(contextVk->onVertexArrayChange(enabledAttribDirtyBits));

    attribBits->fill(gl::VertexArray::DirtyAttribBits());
    bindingBits->fill(gl::VertexArray::DirtyBindingBits());

    return angle::Result::Continue;
}

ANGLE_INLINE void VertexArrayVk::setDefaultPackedInput(ContextVk *contextVk,
                                                       size_t attribIndex,
                                                       angle::FormatID *formatOut)
{
    const gl::State &glState = contextVk->getState();
    const gl::VertexAttribCurrentValueData &defaultValue =
        glState.getVertexAttribCurrentValues()[attribIndex];

    *formatOut = GetCurrentValueFormatID(defaultValue.Type);
}

ANGLE_INLINE void VertexArrayVk::syncDirtyEnabledNonStreamingAttrib(
    ContextVk *contextVk,
    const gl::VertexAttribute &attrib,
    const gl::VertexBinding &binding,
    size_t attribIndex,
    const gl::VertexArray::DirtyAttribBits &dirtyAttribBits)
{
    vk::Renderer *renderer = contextVk->getRenderer();
    ASSERT(attrib.enabled);
    ASSERT(!mStreamingVertexAttribsMask.test(attribIndex));
    ASSERT(!mNeedsConversionAttribsMask[attribIndex]);

    const vk::Format &vertexFormat = renderer->getFormat(attrib.format->id);

    // Init attribute offset to the front-end value
    mVertexInputAttribDescs[attribIndex].offset     = attrib.relativeOffset;
    gl::Buffer *bufferGL                            = getVertexArrayBuffer(attrib.bindingIndex);
    const angle::Format &srcFormat                  = vertexFormat.getIntendedFormat();
    const unsigned srcFormatSize                    = srcFormat.pixelBytes;
    const uint64_t bufferGLSize                     = bufferGL->getSize();
    bool hasAtLeastOneVertex =
        binding.getOffset() < bufferGLSize && (bufferGLSize - binding.getOffset()) >= srcFormatSize;
    // If buffer size is 0, hasAtLeastOneVertex must be false
    ASSERT(bufferGL->getSize() > 0 || !hasAtLeastOneVertex);

    if (ANGLE_LIKELY(hasAtLeastOneVertex))
    {
        BufferVk *bufferVk             = vk::GetImpl(bufferGL);
        uint32_t srcStride       = binding.getStride() == 0 ? srcFormatSize : binding.getStride();
        bool bindingIsAligned =
            BindingIsAligned(srcFormat, binding.getOffset() + attrib.relativeOffset, srcStride);
        bool needsConversion =
            (vertexFormat.getVertexLoadRequiresConversion() || !bindingIsAligned);

        if (ANGLE_UNLIKELY(needsConversion))
        {
            // Early out if needs conversion. We will handle these attributes last.
            mNeedsConversionAttribsMask.set(attribIndex);
            return;
        }

        vk::BufferHelper &bufferHelper         = bufferVk->getBuffer();
        mCurrentArrayBuffers[attribIndex]      = &bufferHelper;
        mCurrentArrayBufferSerial[attribIndex] = bufferHelper.getBufferSerial();
        mCurrentEmptyBufferMask.reset(attribIndex);
        VkDeviceSize bufferSize = renderer->padVertexAttribBufferSizeIfNeeded(bufferVk->getSize());

        VkDeviceSize bufferOffset;
        if (contextVk->getFeatures().supportsBindVertexBuffers2.enabled)
        {
            mCurrentArrayBufferHandles[attribIndex] = bufferHelper.getBuffer().getHandle();
            bufferOffset                            = bufferHelper.getOffset();
        }
        else
        {
            mCurrentArrayBufferHandles[attribIndex] =
                bufferHelper.getBufferForVertexArray(contextVk, bufferSize, &bufferOffset)
                    .getHandle();
        }

        // Vulkan requires the offset is within the buffer. We use robust access
        // behaviour to reset the offset if it starts outside the buffer.
        ASSERT(binding.getOffset() < bufferSize);
        mCurrentArrayBufferOffsets[attribIndex]      = bufferOffset + binding.getOffset();
        mCurrentArrayBufferSizes[attribIndex]        = bufferSize - binding.getOffset();
        mVertexInputBindingDescs[attribIndex].stride = binding.getStride();
    }
    else
    {
        vk::BufferHelper &emptyBuffer = contextVk->getEmptyBuffer();

        mCurrentArrayBuffers[attribIndex]            = &emptyBuffer;
        mCurrentArrayBufferSerial[attribIndex]       = emptyBuffer.getBufferSerial();
        mCurrentArrayBufferHandles[attribIndex]      = emptyBuffer.getBuffer().getHandle();
        mCurrentArrayBufferOffsets[attribIndex]      = emptyBuffer.getOffset();
        mCurrentArrayBufferSizes[attribIndex]        = emptyBuffer.getSize();
        mCurrentEmptyBufferMask.set(attribIndex);
        mVertexInputBindingDescs[attribIndex].stride = 0;
    }

    if ((dirtyAttribBits & kDirtyFormatAttribBits).any())
    {
        setVertexInputAttribDescFormat(renderer, attribIndex, attrib.format->id);
    }
    setVertexInputBindingDescDivisor(renderer, attribIndex, binding.getDivisor());

    mCurrentDefaultAttribsMask.reset(attribIndex);
}

ANGLE_INLINE void VertexArrayVk::syncDirtyEnabledStreamingAttrib(
    ContextVk *contextVk,
    const gl::VertexAttribute &attrib,
    const gl::VertexBinding &binding,
    size_t attribIndex,
    const gl::VertexArray::DirtyAttribBits &dirtyAttribBits)
{
    vk::Renderer *renderer = contextVk->getRenderer();
    ASSERT(attrib.enabled);
    ASSERT(mStreamingVertexAttribsMask.test(attribIndex));
    ASSERT(!mNeedsConversionAttribsMask[attribIndex]);

    const vk::Format &vertexFormat = renderer->getFormat(attrib.format->id);

    vk::BufferHelper &emptyBuffer           = contextVk->getEmptyBuffer();
    mCurrentArrayBuffers[attribIndex]       = &emptyBuffer;
    mCurrentArrayBufferSerial[attribIndex]  = emptyBuffer.getBufferSerial();
    mCurrentArrayBufferHandles[attribIndex] = emptyBuffer.getBuffer().getHandle();
    mCurrentArrayBufferOffsets[attribIndex] = emptyBuffer.getOffset();
    mCurrentArrayBufferSizes[attribIndex]   = emptyBuffer.getSize();
    mCurrentEmptyBufferMask.set(attribIndex);

    mVertexInputBindingDescs[attribIndex].stride = vertexFormat.getActualBufferFormat().pixelBytes;
    // Init attribute offset to the front-end value
    mVertexInputAttribDescs[attribIndex].offset = attrib.relativeOffset;

    if ((dirtyAttribBits & kDirtyFormatAttribBits).any())
    {
        setVertexInputAttribDescFormat(renderer, attribIndex, attrib.format->id);
    }
    setVertexInputBindingDescDivisor(renderer, attribIndex, binding.getDivisor());

    mCurrentDefaultAttribsMask.reset(attribIndex);
}

void VertexArrayVk::syncDirtyDisabledAttribs(ContextVk *contextVk,
                                             const gl::AttributesMask &disabledAttributesMask)
{
    ASSERT((mState.getEnabledAttributesMask() & disabledAttributesMask).none());
    vk::BufferHelper &emptyBuffer = contextVk->getEmptyBuffer();

    for (size_t attribIndex : disabledAttributesMask)
    {
        // These will be filled out by the ContextVk.
        mCurrentArrayBuffers[attribIndex]            = &emptyBuffer;
        mCurrentArrayBufferSerial[attribIndex]       = emptyBuffer.getBufferSerial();
        mCurrentArrayBufferHandles[attribIndex]      = emptyBuffer.getBuffer().getHandle();
        mCurrentArrayBufferOffsets[attribIndex]      = emptyBuffer.getOffset();
        mCurrentArrayBufferSizes[attribIndex]        = emptyBuffer.getSize();
        mVertexInputBindingDescs[attribIndex].stride = 0;
        setVertexInputBindingDescDivisor(contextVk->getRenderer(), attribIndex, 0);
        mVertexInputAttribDescs[attribIndex].offset = 0;
    }

    mCurrentDefaultAttribsMask &= ~disabledAttributesMask;
    mCurrentEmptyBufferMask |= disabledAttributesMask;
}

angle::Result VertexArrayVk::syncNeedsConversionAttrib(ContextVk *contextVk,
                                                       const gl::VertexAttribute &attrib,
                                                       const gl::VertexBinding &binding,
                                                       size_t attribIndex)
{
    vk::Renderer *renderer = contextVk->getRenderer();
    ASSERT(attrib.enabled);
    ASSERT(mNeedsConversionAttribsMask.test(attribIndex));
    ASSERT(!mStreamingVertexAttribsMask.test(attribIndex));
    ASSERT(mVertexInputAttribDescs[attribIndex].offset == attrib.relativeOffset);
    ASSERT(mContentsObserverBindingsMask.test(attrib.bindingIndex));

    const vk::Format &vertexFormat = renderer->getFormat(attrib.format->id);

    gl::Buffer *bufferGL = getVertexArrayBuffer(attrib.bindingIndex);
    ASSERT(bufferGL);
    ASSERT(bufferGL->getSize() > 0);
    BufferVk *bufferVk = vk::GetImpl(bufferGL);

    const angle::Format &srcFormat = vertexFormat.getIntendedFormat();
    unsigned srcFormatSize         = srcFormat.pixelBytes;
    uint32_t srcStride             = binding.getStride() == 0 ? srcFormatSize : binding.getStride();
    bool bindingIsAligned =
        BindingIsAligned(srcFormat, binding.getOffset() + attrib.relativeOffset, srcStride);

    const angle::Format &dstFormat = vertexFormat.getActualBufferFormat();
    // Converted buffer is tightly packed
    uint32_t dstStride = dstFormat.pixelBytes;

    ASSERT(vertexFormat.getVertexInputAlignment() <= vk::kVertexBufferAlignment);

    WarnOnVertexFormatConversion(contextVk, vertexFormat);

    const VertexConversionBuffer::CacheKey cacheKey{
        this,
        attribIndex,
        srcFormat.id,
        srcStride,
        static_cast<size_t>(binding.getOffset()) + attrib.relativeOffset,
        !bindingIsAligned,
        false};

    VertexConversionBuffer *conversion = bufferVk->getVertexConversionBuffer(renderer, cacheKey);

    // Converted attribs are packed in their own VK buffer so offset is relative to the
    // binding and coversion's offset. The conversion buffer try to reuse the existing
    // buffer as much as possible to reduce the amount of data that has to be converted.
    // When binding's offset changes, it will check if new offset and existing buffer's
    // offset are multiple of strides apart. It yes it will reuse. If new offset is
    // larger, all existing data are still valid. If the new offset is smaller it will
    // mark the newly exposed range dirty and then rely on
    // ContextVk::initBufferForVertexConversion to decide buffer's size is big enough or
    // not and reallocate (and mark entire buffer dirty) if needed.
    //
    // bufferVk:-----------------------------------------------------------------------
    //                 |                   |
    //                 |                bingding.offset + attrib.relativeOffset.
    //          conversion->getCacheKey().offset
    //
    // conversion.buffer: --------------------------------------------------------------
    //                                     |
    //                                   dstRelativeOffset
    size_t srcRelativeOffset =
        binding.getOffset() + attrib.relativeOffset - conversion->getCacheKey().offset;
    size_t numberOfVerticesToSkip = srcRelativeOffset / srcStride;
    size_t dstRelativeOffset      = numberOfVerticesToSkip * dstStride;

    if (!bufferVk->hasValidData() && conversion->dirty())
    {
        // In case of uninitialized buffer or redefined buffer with no input data, conversion should
        // not occur, even though a conversion buffer may still be allocated to be used for drawing.
        size_t maxNumVertices;
        ANGLE_TRY(CalculateMaxVertexCountForConversion(contextVk, bufferVk, conversion, srcFormat,
                                                       dstFormat, &maxNumVertices));
        conversion->clearDirty();
    }

    if (conversion->dirty())
    {
        if (bindingIsAligned)
        {
            ANGLE_TRY(
                convertVertexBufferGPU(contextVk, bufferVk, conversion, srcFormat, dstFormat));
        }
        else
        {
            ANGLE_VK_PERF_WARNING(contextVk, GL_DEBUG_SEVERITY_HIGH,
                                  "GPU stall due to vertex format conversion of unaligned data");

            ANGLE_TRY(convertVertexBufferCPU(contextVk, bufferVk, conversion, srcFormat, dstFormat,
                                             vertexFormat.getVertexLoadFunction()));
        }

        // If conversion happens, the destination buffer stride may be changed,
        // therefore an attribute change needs to be called. Note that it may trigger
        // unnecessary vulkan PSO update when the destination buffer stride does not
        // change, but for simplicity just make it conservative
    }

    vk::BufferHelper *bufferHelper         = conversion->getBuffer();
    mCurrentArrayBuffers[attribIndex]      = bufferHelper;
    mCurrentArrayBufferSerial[attribIndex] = bufferHelper->getBufferSerial();
    mCurrentEmptyBufferMask.reset(attribIndex);
    VkDeviceSize bufferSize                = bufferHelper->getSize();

    VkDeviceSize bufferOffset;
    if (contextVk->getFeatures().supportsBindVertexBuffers2.enabled)
    {
        mCurrentArrayBufferHandles[attribIndex] = bufferHelper->getBuffer().getHandle();
        bufferOffset                            = bufferHelper->getOffset();
    }
    else
    {
        mCurrentArrayBufferHandles[attribIndex] =
            bufferHelper->getBufferForVertexArray(contextVk, bufferSize, &bufferOffset).getHandle();
    }
    ASSERT(BindingIsAligned(dstFormat, bufferOffset + dstRelativeOffset, dstStride));
    mCurrentArrayBufferOffsets[attribIndex]         = bufferOffset + dstRelativeOffset;
    mCurrentArrayBufferSizes[attribIndex]           = bufferSize - dstRelativeOffset;
    mVertexInputAttribDescs[attribIndex].offset     = 0;
    mVertexInputBindingDescs[attribIndex].stride    = dstStride;
    setVertexInputAttribDescFormat(renderer, attribIndex, attrib.format->id);
    setVertexInputBindingDescDivisor(renderer, attribIndex, binding.getDivisor());

    mCurrentDefaultAttribsMask.reset(attribIndex);
    return angle::Result::Continue;
}

// Handle copying client attribs and/or expanding attrib buffer in case where attribute
// divisor value has to be emulated.
angle::Result VertexArrayVk::updateStreamedAttribs(
    const gl::Context *context,
    const gl::AttributesMask activeStreamingAttribsMask,
    GLint firstVertex,
    GLsizei vertexOrIndexCount,
    GLuint baseInstance,
    GLsizei instanceCount,
    gl::DrawElementsType indexTypeOrInvalid,
    const void *indices,
    gl::AttributesMask *strideDirtyAttribMaskOut)
{
    ContextVk *contextVk   = vk::GetImpl(context);
    vk::Renderer *renderer = contextVk->getRenderer();

    GLint startVertex;
    size_t vertexCount;
    ANGLE_TRY(GetVertexRangeInfo(context, firstVertex, vertexOrIndexCount, indexTypeOrInvalid,
                                 indices, 0, &startVertex, &vertexCount));

    ASSERT(vertexCount > 0);
    const auto &attribs  = mState.getVertexAttributes();
    const auto &bindings = mState.getVertexBindings();

    std::array<size_t, gl::MAX_VERTEX_ATTRIBS> mergedIndexes;
    std::array<AttributeRange, gl::MAX_VERTEX_ATTRIBS> mergedRanges;
    std::array<vk::BufferHelper *, gl::MAX_VERTEX_ATTRIBS> attribBufferHelper = {};
    gl::AttributesMask mergedAttribMask;
    if (renderer->getFeatures().enableMergeClientAttribBuffer.enabled)
    {
        mergedAttribMask = MergeClientAttribsRange(
            renderer, attribs, bindings, activeStreamingAttribsMask, startVertex,
            startVertex + vertexCount, mergedRanges, mergedIndexes);
    }

    for (size_t attribIndex : activeStreamingAttribsMask)
    {
        const gl::VertexAttribute &attrib = attribs[attribIndex];
        ASSERT(attrib.enabled);
        const gl::VertexBinding &binding = bindings[attrib.bindingIndex];

        const vk::Format &vertexFormat = renderer->getFormat(attrib.format->id);
        const angle::Format &dstFormat = vertexFormat.getActualBufferFormat();
        GLuint pixelBytes              = dstFormat.pixelBytes;

        ASSERT(vertexFormat.getVertexInputAlignment() <= vk::kVertexBufferAlignment);

        vk::BufferHelper *vertexDataBuffer = nullptr;
        const uint8_t *src                 = static_cast<const uint8_t *>(attrib.pointer);
        uint32_t divisor                   = binding.getDivisor();

        GLuint stride            = pixelBytes;
        VkDeviceSize startOffset = 0;
        if (divisor > 0)
        {
            // Instanced attrib
            if (divisor > renderer->getMaxVertexAttribDivisor())
            {
                // Divisor will be set to 1 & so update buffer to have 1 attrib per instance. Make
                // sure we do multiplication with uint64_t in case the result is more than 32 bit.
                // We will let buffer allocation code handle the failure if needed.
                size_t bytesToAllocate = static_cast<size_t>(baseInstance + instanceCount) * stride;

                // Allocate buffer for results
                ANGLE_TRY(contextVk->allocateStreamedVertexBuffer(attribIndex, bytesToAllocate,
                                                                  &vertexDataBuffer));

                gl::Buffer *bufferGL = getVertexArrayBuffer(attrib.bindingIndex);
                if (bufferGL != nullptr)
                {
                    // Only do the data copy if src buffer is valid.
                    if (bufferGL->getSize() > 0)
                    {
                        // Map buffer to expand attribs for divisor emulation
                        BufferVk *bufferVk = vk::GetImpl(bufferGL);
                        void *buffSrc      = nullptr;
                        ANGLE_TRY(bufferVk->mapForReadAccessOnly(contextVk, &buffSrc));
                        src = ANGLE_UNSAFE_TODO(reinterpret_cast<const uint8_t *>(buffSrc) +
                                                binding.getOffset());

                        uint32_t srcAttributeSize =
                            static_cast<uint32_t>(ComputeVertexAttributeTypeSize(attrib));

                        size_t numVertices = GetVertexCount(bufferVk, binding, srcAttributeSize);
                        numVertices = numVertices < baseInstance ? 0 : numVertices - baseInstance;

                        ANGLE_TRY(StreamVertexDataWithDivisor(
                            contextVk, vertexDataBuffer, src, bytesToAllocate, binding.getStride(),
                            stride, vertexFormat.getVertexLoadFunction(), divisor, baseInstance,
                            numVertices));

                        ANGLE_TRY(bufferVk->unmapReadAccessOnly(contextVk));
                    }
                    else if (contextVk->getExtensions().robustnessAny())
                    {
                        // Satisfy robustness constraints (only if extension enabled)
                        uint8_t *dst = vertexDataBuffer->getMappedMemory();
                        ANGLE_UNSAFE_TODO(memset(dst, 0, bytesToAllocate));
                    }
                }
                else
                {
                    size_t numVertices = instanceCount;
                    ANGLE_TRY(StreamVertexDataWithDivisor(
                        contextVk, vertexDataBuffer, src, bytesToAllocate, binding.getStride(),
                        stride, vertexFormat.getVertexLoadFunction(), divisor, baseInstance,
                        numVertices));
                }

                divisor = 1;
            }
            else
            {
                ASSERT(getVertexArrayBuffer(attrib.bindingIndex) == nullptr);
                size_t count           = UnsignedCeilDivide(instanceCount, divisor);
                size_t srcOffset       = baseInstance * stride;
                size_t dstOffset       = srcOffset;
                size_t bytesToAllocate = count * stride + dstOffset;

                // Allocate buffer for results
                ANGLE_TRY(contextVk->allocateStreamedVertexBuffer(attribIndex, bytesToAllocate,
                                                                  &vertexDataBuffer));

                ANGLE_UNSAFE_TODO(ANGLE_TRY(StreamVertexData(
                    contextVk, vertexDataBuffer, src + srcOffset, bytesToAllocate, dstOffset, count,
                    binding.getStride(), vertexFormat.getVertexLoadFunction())));
            }
        }
        else if (attrib.pointer == nullptr)
        {
            // Set them to the initial value.
            vk::BufferHelper &emptyBuffer            = contextVk->getEmptyBuffer();
            mCurrentArrayBuffers[attribIndex]        = &emptyBuffer;
            mCurrentArrayBufferHandles[attribIndex]  = emptyBuffer.getBuffer().getHandle();
            mCurrentArrayBufferOffsets[attribIndex]  = 0;
            mCurrentArrayBufferSizes[attribIndex]       = 0;
            mVertexInputBindingDescs[attribIndex].stride = 0;
            setVertexInputBindingDescDivisor(renderer, attribIndex, 0);
            mCurrentEmptyBufferMask.set(attribIndex);
            continue;
        }
        else if (mergedAttribMask.test(attribIndex))
        {
            ASSERT(getVertexArrayBuffer(attrib.bindingIndex) == nullptr);
            size_t mergedAttribIdx      = mergedIndexes[attribIndex];
            const AttributeRange &range = mergedRanges[mergedAttribIdx];
            stride                      = binding.getStride();
            if (attribBufferHelper[mergedAttribIdx] == nullptr)
            {
                size_t destOffset      = range.copyStartAddr - range.startAddr;
                size_t bytesToAllocate = range.endAddr - range.startAddr;
                ANGLE_TRY(contextVk->allocateStreamedVertexBuffer(
                    mergedAttribIdx, bytesToAllocate, &attribBufferHelper[mergedAttribIdx]));
                ANGLE_TRY(StreamVertexData(contextVk, attribBufferHelper[mergedAttribIdx],
                                           (const uint8_t *)range.copyStartAddr,
                                           bytesToAllocate - destOffset, destOffset, vertexCount,
                                           stride, nullptr));
            }
            vertexDataBuffer = attribBufferHelper[mergedAttribIdx];
            startOffset      = reinterpret_cast<uintptr_t>(attrib.pointer) - range.startAddr;
        }
        else
        {
            ASSERT(getVertexArrayBuffer(attrib.bindingIndex) == nullptr);
            // Allocate space for startVertex + vertexCount so indexing will work.  If we don't
            // start at zero all the indices will be off.
            // Only vertexCount vertices will be used by the upcoming draw so that is all we copy.
            ANGLE_UNSAFE_TODO(src += startVertex * binding.getStride());
            size_t destOffset      = startVertex * stride;
            size_t bytesToAllocate = (startVertex + vertexCount) * stride;

            // Allocate buffer for results
            ANGLE_TRY(contextVk->allocateStreamedVertexBuffer(attribIndex, bytesToAllocate,
                                                              &vertexDataBuffer));

            ANGLE_TRY(StreamVertexData(contextVk, vertexDataBuffer, src, bytesToAllocate,
                                       destOffset, vertexCount, binding.getStride(),
                                       vertexFormat.getVertexLoadFunction()));
            startOffset = 0;
        }
        ASSERT(vertexDataBuffer != nullptr);
        mCurrentArrayBuffers[attribIndex]      = vertexDataBuffer;
        mCurrentArrayBufferSerial[attribIndex] = vertexDataBuffer->getBufferSerial();
        mCurrentEmptyBufferMask.reset(attribIndex);
        VkDeviceSize bufferSize                = vertexDataBuffer->getSize();

        VkDeviceSize bufferOffset;
        if (contextVk->getFeatures().supportsBindVertexBuffers2.enabled)
        {
            mCurrentArrayBufferHandles[attribIndex] = vertexDataBuffer->getBuffer().getHandle();
            bufferOffset                            = vertexDataBuffer->getOffset();
        }
        else
        {
            mCurrentArrayBufferHandles[attribIndex] =
                vertexDataBuffer->getBufferForVertexArray(contextVk, bufferSize, &bufferOffset)
                    .getHandle();
        }
        mCurrentArrayBufferOffsets[attribIndex]  = bufferOffset + startOffset;
        mCurrentArrayBufferSizes[attribIndex]       = bufferSize - startOffset;
        if (mVertexInputBindingDescs[attribIndex].stride != stride)
        {
            mVertexInputBindingDescs[attribIndex].stride = stride;
            strideDirtyAttribMaskOut->set(attribIndex);
        }
        setVertexInputBindingDescDivisor(renderer, attribIndex, divisor);
        ASSERT(BindingIsAligned(dstFormat, mCurrentArrayBufferOffsets[attribIndex],
                                mVertexInputBindingDescs[attribIndex].stride));
    }

    return angle::Result::Continue;
}

void VertexArrayVk::resetInactiveStreamingAttribs(const gl::AttributesMask inactiveAttribMask,
                                                  vk::BufferHelper &emptyBuffer)
{
    const gl::AttributesMask inactiveAttribsToReset = inactiveAttribMask & ~mCurrentEmptyBufferMask;
    for (size_t inactiveAttribIndex : inactiveAttribsToReset)
    {
        mCurrentArrayBuffers[inactiveAttribIndex]       = &emptyBuffer;
        mCurrentArrayBufferSerial[inactiveAttribIndex]  = emptyBuffer.getBufferSerial();
        mCurrentArrayBufferHandles[inactiveAttribIndex] = emptyBuffer.getBuffer().getHandle();
        mCurrentArrayBufferOffsets[inactiveAttribIndex] = emptyBuffer.getOffset();
        mCurrentArrayBufferSizes[inactiveAttribIndex]   = emptyBuffer.getSize();
    }
    mCurrentEmptyBufferMask |= inactiveAttribMask;
}

angle::Result VertexArrayVk::handleLineLoop(ContextVk *contextVk,
                                            GLint firstVertex,
                                            GLsizei vertexOrIndexCount,
                                            gl::DrawElementsType indexTypeOrInvalid,
                                            const void *indices,
                                            vk::BufferHelper **indexBufferOut,
                                            uint32_t *indexCountOut)
{
    if (indexTypeOrInvalid != gl::DrawElementsType::InvalidEnum)
    {
        // Handle GL_LINE_LOOP drawElements.
        if (mDirtyLineLoopTranslation)
        {
            gl::Buffer *elementArrayBuffer = getElementArrayBuffer();

            if (!elementArrayBuffer)
            {
                ANGLE_TRY(mLineLoopHelper.streamIndices(
                    contextVk, indexTypeOrInvalid, vertexOrIndexCount,
                    reinterpret_cast<const uint8_t *>(indices), indexBufferOut, indexCountOut));
            }
            else
            {
                // When using an element array buffer, 'indices' is an offset to the first element.
                uintptr_t offset               = reinterpret_cast<uintptr_t>(indices);
                BufferVk *elementArrayBufferVk = vk::GetImpl(elementArrayBuffer);
                ANGLE_TRY(mLineLoopHelper.getIndexBufferForElementArrayBuffer(
                    contextVk, elementArrayBufferVk, indexTypeOrInvalid, vertexOrIndexCount, offset,
                    indexBufferOut, indexCountOut));
            }
        }

        // If we've had a drawArrays call with a line loop before, we want to make sure this is
        // invalidated the next time drawArrays is called since we use the same index buffer for
        // both calls.
        mLineLoopBufferFirstIndex.reset();
        mLineLoopBufferLastIndex.reset();
        return angle::Result::Continue;
    }

    // Note: Vertex indexes can be arbitrarily large.
    uint32_t clampedVertexCount = gl::clampCast<uint32_t>(vertexOrIndexCount);

    // Handle GL_LINE_LOOP drawArrays.
    size_t lastVertex = static_cast<size_t>(firstVertex + clampedVertexCount);
    if (!mLineLoopBufferFirstIndex.valid() || !mLineLoopBufferLastIndex.valid() ||
        mLineLoopBufferFirstIndex != firstVertex || mLineLoopBufferLastIndex != lastVertex)
    {
        ANGLE_TRY(mLineLoopHelper.getIndexBufferForDrawArrays(contextVk, clampedVertexCount,
                                                              firstVertex, indexBufferOut));

        mLineLoopBufferFirstIndex = firstVertex;
        mLineLoopBufferLastIndex  = lastVertex;
    }
    else
    {
        *indexBufferOut = mLineLoopHelper.getCurrentIndexBuffer();
    }
    *indexCountOut = vertexOrIndexCount + 1;

    return angle::Result::Continue;
}

angle::Result VertexArrayVk::updateDefaultAttribs(ContextVk *contextVk,
                                                  const gl::AttributesMask &dirtyDefaultAttribsMask)
{
    vk::Renderer *renderer = contextVk->getRenderer();
    // default attributes should never be enabled
    ASSERT((dirtyDefaultAttribsMask & mState.getEnabledAttributesMask()).none());

    for (size_t attribIndex : dirtyDefaultAttribsMask)
    {
        vk::BufferHelper *bufferHelper;
        ANGLE_TRY(
            contextVk->allocateStreamedVertexBuffer(attribIndex, kDefaultValueSize, &bufferHelper));

        const gl::VertexAttribCurrentValueData &defaultValue =
            contextVk->getState().getVertexAttribCurrentValues()[attribIndex];
        uint8_t *ptr = bufferHelper->getMappedMemory();
        ANGLE_UNSAFE_TODO(memcpy(ptr, &defaultValue.Values, kDefaultValueSize));
        ANGLE_TRY(bufferHelper->flush(contextVk->getRenderer()));

        VkDeviceSize bufferOffset;
        if (contextVk->getFeatures().supportsBindVertexBuffers2.enabled)
        {
            mCurrentArrayBufferHandles[attribIndex] = bufferHelper->getBuffer().getHandle();
            bufferOffset                            = bufferHelper->getOffset();
        }
        else
        {
            mCurrentArrayBufferHandles[attribIndex] =
                bufferHelper->getBufferForVertexArray(contextVk, kDefaultValueSize, &bufferOffset)
                    .getHandle();
        }
        mCurrentArrayBufferOffsets[attribIndex]      = bufferOffset;
        mCurrentArrayBufferSizes[attribIndex]        = kDefaultValueSize;
        mCurrentArrayBuffers[attribIndex]            = bufferHelper;
        mCurrentArrayBufferSerial[attribIndex]       = bufferHelper->getBufferSerial();
        mVertexInputBindingDescs[attribIndex].stride = 0;
        setVertexInputBindingDescDivisor(renderer, attribIndex, 0);

        setDefaultPackedInput(contextVk, attribIndex, &mDefaultAttribFormatIDs[attribIndex]);
        setVertexInputAttribDescFormat(renderer, attribIndex, mDefaultAttribFormatIDs[attribIndex]);
    }
    mCurrentDefaultAttribsMask |= dirtyDefaultAttribsMask;
    mCurrentEmptyBufferMask &= ~dirtyDefaultAttribsMask;

    return angle::Result::Continue;
}

ANGLE_INLINE void VertexArrayVk::setVertexInputAttribDescFormat(vk::Renderer *renderer,
                                                                size_t attribIndex,
                                                                angle::FormatID formatID)
{
    const vk::Format &format                   = renderer->getFormat(formatID);
    mVertexInputAttribDescs[attribIndex].format = format.getActualBufferVkFormat(renderer);
}

ANGLE_INLINE void VertexArrayVk::setVertexInputBindingDescDivisor(vk::Renderer *renderer,
                                                                  size_t attribIndex,
                                                                  GLuint divisor)
{
    if (divisor != 0)
    {
        mVertexInputBindingDescs[attribIndex].inputRate =
            static_cast<VkVertexInputRate>(VK_VERTEX_INPUT_RATE_INSTANCE);
        mVertexInputBindingDescs[attribIndex].divisor = divisor;
    }
    else
    {
        mVertexInputBindingDescs[attribIndex].inputRate =
            static_cast<VkVertexInputRate>(VK_VERTEX_INPUT_RATE_VERTEX);
        mVertexInputBindingDescs[attribIndex].divisor = mDivisorForVertexInputRateVertex;
    }
}
}  // namespace rx
