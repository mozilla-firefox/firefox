//
// Copyright 2016 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// VertexArrayVk.h:
//    Defines the class interface for VertexArrayVk, implementing VertexArrayImpl.
//

#ifndef LIBANGLE_RENDERER_VULKAN_VERTEXARRAYVK_H_
#define LIBANGLE_RENDERER_VULKAN_VERTEXARRAYVK_H_

#include "libANGLE/renderer/VertexArrayImpl.h"
#include "libANGLE/renderer/vulkan/UtilsVk.h"
#include "libANGLE/renderer/vulkan/vk_cache_utils.h"
#include "libANGLE/renderer/vulkan/vk_helpers.h"

namespace rx
{
enum class BufferBindingDirty
{
    No,
    Yes,
};

class VertexArrayVk : public VertexArrayImpl
{
  public:
    VertexArrayVk(ContextVk *contextVk,
                  const gl::VertexArrayState &state,
                  const gl::VertexArrayBuffers &vertexArrayBuffers);
    ~VertexArrayVk() override;

    void destroy(const gl::Context *context) override;

    gl::VertexArray::DirtyBits checkBufferForDirtyBits(
        const gl::Context *context,
        const gl::VertexArrayBufferBindingMask bufferBindingMask) override;

    angle::Result syncState(const gl::Context *context,
                            const gl::VertexArray::DirtyBits &dirtyBits,
                            gl::VertexArray::DirtyAttribBitsArray *attribBits,
                            gl::VertexArray::DirtyBindingBitsArray *bindingBits) override;

    angle::Result updateDefaultAttribs(ContextVk *contextVk,
                                       const gl::AttributesMask &dirtyDefaultAttribsMask);

    angle::Result updateStreamedAttribs(const gl::Context *context,
                                        const gl::AttributesMask activeStreamingAttribsMask,
                                        GLint firstVertex,
                                        GLsizei vertexOrIndexCount,
                                        GLuint baseInstance,
                                        GLsizei instanceCount,
                                        gl::DrawElementsType indexTypeOrInvalid,
                                        const void *indices,
                                        gl::AttributesMask *strideDirtyAttribMaskOut);

    void updateCurrentActiveStreamingAttribsMask(const gl::Context *context,
                                                 vk::BufferHelper &emptyBuffer);

    angle::Result handleLineLoop(ContextVk *contextVk,
                                 GLint firstVertex,
                                 GLsizei vertexOrIndexCount,
                                 gl::DrawElementsType indexTypeOrInvalid,
                                 const void *indices,
                                 vk::BufferHelper **indexBufferOut,
                                 uint32_t *indexCountOut);

    angle::Result handleLineLoopIndexIndirect(ContextVk *contextVk,
                                              gl::DrawElementsType glIndexType,
                                              vk::BufferHelper *srcIndexBuffer,
                                              vk::BufferHelper *srcIndirectBuffer,
                                              VkDeviceSize indirectBufferOffset,
                                              vk::BufferHelper **indexBufferOut,
                                              vk::BufferHelper **indirectBufferOut);

    angle::Result handleLineLoopIndirectDraw(const gl::Context *context,
                                             vk::BufferHelper *indirectBufferVk,
                                             VkDeviceSize indirectBufferOffset,
                                             vk::BufferHelper **indexBufferOut,
                                             vk::BufferHelper **indirectBufferOut);

    const gl::AttribArray<VkBuffer> &getCurrentArrayBufferHandles() const
    {
        return mCurrentArrayBufferHandles;
    }

    const gl::AttribArray<VkDeviceSize> &getCurrentArrayBufferOffsets() const
    {
        return mCurrentArrayBufferOffsets;
    }

    const gl::AttribArray<VkDeviceSize> &getCurrentArrayBufferSizes() const
    {
        return mCurrentArrayBufferSizes;
    }

    const gl::AttribArray<VkVertexInputBindingDescription2EXT> &getVertexInputBindingDescs() const
    {
        return mVertexInputBindingDescs;
    }

    const gl::AttribArray<VkVertexInputAttributeDescription2EXT> &getVertexInputAttribDescs() const
    {
        return mVertexInputAttribDescs;
    }

    GLuint getCurrentArrayBufferRelativeOffset(size_t attribIndex) const
    {
        return mVertexInputAttribDescs[attribIndex].offset;
    }

    VkFormat getCurrentArrayBufferVkFormat(size_t attribIndex) const
    {
        return mVertexInputAttribDescs[attribIndex].format;
    }

    angle::FormatID getCurrentArrayBufferFormatID(size_t attribIndex) const
    {
        return mCurrentEnabledAttribsMask.test(attribIndex)
                   ? mState.getVertexAttribute(attribIndex).format->id
                   : mDefaultAttribFormatIDs[attribIndex];
    }

    GLuint getCurrentArrayBufferStride(size_t attribIndex) const
    {
        return mVertexInputBindingDescs[attribIndex].stride;
    }

    GLuint getCurrentArrayBufferDivisor(size_t attribIndex) const
    {
        return mVertexInputBindingDescs[attribIndex].divisor;
    }

    // Update mCurrentElementArrayBuffer based on the vertex array state
    void updateCurrentElementArrayBuffer();

    vk::BufferHelper *getCurrentElementArrayBuffer() const { return mCurrentElementArrayBuffer; }

    const gl::AttribArray<vk::BufferHelper *> &getCurrentArrayBuffers() const
    {
        return mCurrentArrayBuffers;
    }

    void assertEmptyBufferConsistency(const vk::BufferHelper &emptyBuffer) const
    {
        for (size_t attribIndex = 0; attribIndex < mCurrentArrayBuffers.size(); ++attribIndex)
        {
            ASSERT(mCurrentEmptyBufferMask.test(attribIndex) ==
                   (mCurrentArrayBuffers[attribIndex] == &emptyBuffer));
        }
    }

    angle::Result convertIndexBufferGPU(ContextVk *contextVk,
                                        BufferVk *bufferVk,
                                        const void *indices);

    angle::Result convertIndexBufferIndirectGPU(ContextVk *contextVk,
                                                vk::BufferHelper *srcIndirectBuf,
                                                VkDeviceSize srcIndirectBufOffset,
                                                vk::BufferHelper **indirectBufferVkOut);

    angle::Result convertIndexBufferCPU(ContextVk *contextVk,
                                        gl::DrawElementsType indexType,
                                        size_t indexCount,
                                        const void *sourcePointer,
                                        BufferBindingDirty *bufferBindingDirty);

    gl::AttributesMask getStreamingVertexAttribsMask() const { return mStreamingVertexAttribsMask; }
    gl::AttributesMask getCurrentEnabledAttribsMask() const { return mCurrentEnabledAttribsMask; }
    gl::AttributesMask getCurrentDefaultAttribsMask() const { return mCurrentDefaultAttribsMask; }

    void syncDirtyDisabledAttribs(ContextVk *contextVk,
                                  const gl::AttributesMask &disabledAttributesMask);
    void resetInactiveStreamingAttribs(const gl::AttributesMask inactiveAttribMask,
                                       vk::BufferHelper &emptyBuffer);

  private:

    void setDefaultPackedInput(ContextVk *contextVk,
                               size_t attribIndex,
                               angle::FormatID *formatOut);

    angle::Result convertVertexBufferGPU(ContextVk *contextVk,
                                         BufferVk *srcBuffer,
                                         VertexConversionBuffer *conversion,
                                         const angle::Format &srcFormat,
                                         const angle::Format &dstFormat);
    angle::Result convertVertexBufferCPU(ContextVk *contextVk,
                                         BufferVk *srcBuffer,
                                         VertexConversionBuffer *conversion,
                                         const angle::Format &srcFormat,
                                         const angle::Format &dstFormat,
                                         const VertexCopyFunction vertexLoadFunction);

    void syncDirtyEnabledNonStreamingAttrib(
        ContextVk *contextVk,
        const gl::VertexAttribute &attrib,
        const gl::VertexBinding &binding,
        size_t attribIndex,
        const gl::VertexArray::DirtyAttribBits &dirtyAttribBits);

    void syncDirtyEnabledStreamingAttrib(ContextVk *contextVk,
                                         const gl::VertexAttribute &attrib,
                                         const gl::VertexBinding &binding,
                                         size_t attribIndex,
                                         const gl::VertexArray::DirtyAttribBits &dirtyAttribBits);

    angle::Result syncNeedsConversionAttrib(ContextVk *contextVk,
                                            const gl::VertexAttribute &attrib,
                                            const gl::VertexBinding &binding,
                                            size_t attribIndex);

    void setVertexInputAttribDescFormat(vk::Renderer *renderer,
                                        size_t attribIndex,
                                        angle::FormatID formatID);

    void setVertexInputBindingDescDivisor(vk::Renderer *renderer,
                                          size_t attribIndex,
                                          GLuint divisor);

    gl::AttribArray<VkBuffer> mCurrentArrayBufferHandles;
    gl::AttribArray<VkDeviceSize> mCurrentArrayBufferOffsets;
    gl::AttribArray<VkDeviceSize> mCurrentArrayBufferSizes;
    gl::AttribArray<vk::BufferHelper *> mCurrentArrayBuffers;
    // Tracks BufferSerial of mCurrentArrayBuffers since they are always valid to access.
    gl::AttribArray<vk::BufferSerial> mCurrentArrayBufferSerial;
    // Tracks the default attribute format ID
    gl::AttribArray<angle::FormatID> mDefaultAttribFormatIDs;
    // The bit is set when mCurrentArrayBuffers is pointing to empty buffer.
    gl::AttributesMask mCurrentEmptyBufferMask;

    // These struct are defined by VK_EXT_vertex_input_dynamic_state, for convenience, we these to
    // store offset/divisor even when vertexInputDynamicState not supported.
    gl::AttribArray<VkVertexInputBindingDescription2EXT> mVertexInputBindingDescs;
    gl::AttribArray<VkVertexInputAttributeDescription2EXT> mVertexInputAttribDescs;

    vk::BufferHelper *mCurrentElementArrayBuffer;

    // Cached element array buffers for improving performance.
    vk::BufferHelperQueue mCachedStreamIndexBuffers;

    ConversionBuffer mStreamedIndexData;
    ConversionBuffer mTranslatedByteIndexData;
    ConversionBuffer mTranslatedByteIndirectData;

    LineLoopHelper mLineLoopHelper;
    Optional<GLint> mLineLoopBufferFirstIndex;
    Optional<size_t> mLineLoopBufferLastIndex;
    bool mDirtyLineLoopTranslation;

    gl::BufferBindingMask mDivisorExceedMaxSupportedValueBindingMask;

    // Equivalent to mState.getEnabledAttributesMask(), but used for detect from enabled to disabled
    // transition.
    gl::AttributesMask mCurrentEnabledAttribsMask;
    // The bit is set when the attribute is updated by VertexArrayVk::updateDefaultAttribs call
    gl::AttributesMask mCurrentDefaultAttribsMask;

    // Track client and/or emulated attribs that we have to stream their buffer contents
    gl::AttributesMask mStreamingVertexAttribsMask;
    gl::AttributesMask mNeedsConversionAttribsMask;

    // Divisor value if vertex inputRate is VK_VERTEX_INPUT_RATE_VERTEX. This maybe 0 or 1 depends
    // on feature bit.
    uint32_t mDivisorForVertexInputRateVertex;
};
}  // namespace rx

#endif  // LIBANGLE_RENDERER_VULKAN_VERTEXARRAYVK_H_
