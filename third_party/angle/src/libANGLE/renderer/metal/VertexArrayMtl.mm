//
// Copyright 2019 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// VertexArrayMtl.mm:
//    Implements the class methods for VertexArrayMtl.
//

#include "libANGLE/renderer/metal/VertexArrayMtl.h"

#include <TargetConditionals.h>

#include "libANGLE/ErrorStrings.h"
#include "libANGLE/renderer/metal/BufferMtl.h"
#include "libANGLE/renderer/metal/ContextMtl.h"
#include "libANGLE/renderer/metal/DisplayMtl.h"
#include "libANGLE/renderer/metal/mtl_format_utils.h"

#include "common/debug.h"
#include "common/span_util.h"
#include "common/unsafe_buffers.h"
#include "common/utilities.h"

namespace rx
{
namespace
{
constexpr size_t kDynamicIndexDataSize = 1024 * 8;

angle::Result StreamVertexData(ContextMtl *contextMtl,
                               mtl::BufferPool *dynamicBuffer,
                               const uint8_t *sourceData,
                               size_t bytesToAllocate,
                               size_t destOffset,
                               size_t vertexCount,
                               size_t stride,
                               VertexCopyFunction vertexLoadFunction,
                               mtl::BufferSlice *outBuffer)
{
    ANGLE_CHECK(contextMtl, vertexLoadFunction, gl::err::kInternalError, GL_INVALID_OPERATION);
    angle::Span<uint8_t> newBufferData;
    mtl::BufferSlice newBuffer;
    ANGLE_TRY(
        dynamicBuffer->allocateAndMap(contextMtl, bytesToAllocate, &newBufferData, &newBuffer));
    vertexLoadFunction(sourceData, stride, vertexCount, newBufferData.subspan(destOffset).data());

    ANGLE_TRY(dynamicBuffer->commit(contextMtl));
    *outBuffer = std::move(newBuffer);
    return angle::Result::Continue;
}

template <typename SizeT>
const mtl::VertexFormat &GetVertexConversionFormat(ContextMtl *contextMtl,
                                                   angle::FormatID originalFormat,
                                                   SizeT *strideOut)
{
    // Convert to tightly packed format
    const mtl::VertexFormat &packedFormat = contextMtl->getVertexFormat(originalFormat, true);
    *strideOut                            = packedFormat.actualAngleFormat().pixelBytes;
    return packedFormat;
}

void StreamIndexData(ContextMtl *contextMtl,
                     gl::DrawElementsType indexType,
                     size_t indexCount,
                     angle::Span<const uint8_t> source,
                     angle::Span<uint8_t> destination,
                     bool primitiveRestartEnabled)
{
    RELEASE_ASSERT(source.size() <= destination.size());
    if (indexType == gl::DrawElementsType::UnsignedByte)
    {
        // Unsigned bytes don't have direct support in Metal so we have to expand the
        // memory to a GLushort.
        angle::Span<uint16_t> expanded = reinterpret_span<uint16_t>(destination);
        auto s                         = source.begin();
        auto sourceEnd                 = source.end();
        auto d                         = expanded.begin();
        if (primitiveRestartEnabled)
        {
            for (; s != sourceEnd; ANGLE_UNSAFE_TODO(s++), ANGLE_UNSAFE_TODO(d++))
            {
                uint8_t value = *s;
                *d            = value == 0xFF ? 0xFFFF : static_cast<uint16_t>(value);
            }
        }
        else
        {
            for (; s != sourceEnd; ANGLE_UNSAFE_TODO(s++), ANGLE_UNSAFE_TODO(d++))
            {
                *d = static_cast<uint16_t>(*s);
            }
        }  // if (primitiveRestartEnabled)
    }
    else
    {
        SpanMemcpy(destination, source);
    }
}

size_t GetVertexCount(BufferMtl *srcBuffer,
                      const gl::VertexBinding &binding,
                      uint32_t srcFormatSize)
{
    // Bytes usable for vertex data.
    GLint64 bytes = srcBuffer->size() - binding.getOffset();
    if (bytes < srcFormatSize)
        return 0;

    // Count the last vertex.  It may occupy less than a full stride.
    size_t numVertices = 1;
    bytes -= srcFormatSize;

    // Count how many strides fit remaining space.
    if (bytes > 0)
        numVertices += static_cast<size_t>(bytes) / binding.getStride();

    return numVertices;
}

size_t GetVertexCountWithConversion(BufferMtl *srcBuffer,
                                    VertexConversionBufferMtl *conversionBuffer,
                                    const gl::VertexBinding &binding,
                                    uint32_t srcFormatSize)
{
    // Bytes usable for vertex data.
    GLint64 bytes = srcBuffer->size() -
                    MIN(static_cast<uintptr_t>(conversionBuffer->offset), binding.getOffset());
    if (bytes < srcFormatSize)
        return 0;

    // Count the last vertex.  It may occupy less than a full stride.
    size_t numVertices = 1;
    bytes -= srcFormatSize;

    // Count how many strides fit remaining space.
    if (bytes > 0)
        numVertices += static_cast<size_t>(bytes) / binding.getStride();

    return numVertices;
}

inline MTLVertexFormat GetCurrentAttribFormat(GLenum type)
{
    switch (type)
    {
        case GL_INT:
        case GL_INT_VEC2:
        case GL_INT_VEC3:
        case GL_INT_VEC4:
            return MTLVertexFormatInt4;
        case GL_UNSIGNED_INT:
        case GL_UNSIGNED_INT_VEC2:
        case GL_UNSIGNED_INT_VEC3:
        case GL_UNSIGNED_INT_VEC4:
            return MTLVertexFormatUInt4;
        default:
            return MTLVertexFormatFloat4;
    }
}

}  // namespace

// VertexArrayMtl implementation
VertexArrayMtl::VertexArrayMtl(const gl::VertexArrayState &state,
                               const gl::VertexArrayBuffers &vertexArrayBuffers,
                               ContextMtl *context)
    : VertexArrayImpl(state, vertexArrayBuffers),
      mDefaultFloatVertexFormat(
          context->getVertexFormat(angle::FormatID::R32G32B32A32_FLOAT, false))
{
    reset(context);

    mDynamicVertexData.initialize(context, 0, mtl::kVertexAttribBufferStrideAlignment,
                                  /** maxBuffers */ 10 * mtl::kMaxVertexAttribs,
                                  mtl::TrackBufferInContext::Yes);

    mDynamicIndexData.initialize(context, kDynamicIndexDataSize, mtl::kIndexBufferOffsetAlignment,
                                 0, mtl::TrackBufferInContext::Yes);
}
VertexArrayMtl::~VertexArrayMtl() {}

void VertexArrayMtl::destroy(const gl::Context *context)
{
    ContextMtl *contextMtl = mtl::GetImpl(context);

    reset(contextMtl);

    mDynamicVertexData.destroy(contextMtl);
    mDynamicIndexData.destroy(contextMtl);
}

void VertexArrayMtl::reset(ContextMtl *context)
{
    for (BufferHolderMtl *&buffer : mCurrentArrayBuffers)
    {
        buffer = nullptr;
    }
    for (size_t &offset : mCurrentArrayBufferOffsets)
    {
        offset = 0;
    }
    for (GLuint &stride : mCurrentArrayBufferStrides)
    {
        stride = 0;
    }
    for (const mtl::VertexFormat *&format : mCurrentArrayBufferFormats)
    {
        format = &mDefaultFloatVertexFormat;
    }

    for (size_t &inlineDataSize : mCurrentArrayInlineDataSizes)
    {
        inlineDataSize = 0;
    }

    for (angle::MemoryBuffer &convertedClientArray : mConvertedClientSmallArrays)
    {
        convertedClientArray.clear();
    }

    for (const uint8_t *&clientPointer : mCurrentArrayInlineDataPointers)
    {
        clientPointer = nullptr;
    }

    if (context->getDisplay()->getFeatures().allowInlineConstVertexData.enabled)
    {
        mInlineDataMaxSize = mtl::kInlineConstDataMaxSize;
    }
    else
    {
        mInlineDataMaxSize = 0;
    }

    mVertexArrayDirty = true;
}

angle::Result VertexArrayMtl::syncState(const gl::Context *context,
                                        const gl::VertexArray::DirtyBits &dirtyBits,
                                        gl::VertexArray::DirtyAttribBitsArray *attribBits,
                                        gl::VertexArray::DirtyBindingBitsArray *bindingBits)
{
    const std::vector<gl::VertexAttribute> &attribs = mState.getVertexAttributes();
    const std::vector<gl::VertexBinding> &bindings  = mState.getVertexBindings();

    for (size_t dirtyBit : dirtyBits)
    {
        switch (dirtyBit)
        {
            case gl::VertexArray::DIRTY_BIT_ELEMENT_ARRAY_BUFFER:
            case gl::VertexArray::DIRTY_BIT_ELEMENT_ARRAY_BUFFER_DATA:
            {
                mVertexDataDirty = true;
                break;
            }

#define ANGLE_VERTEX_DIRTY_ATTRIB_FUNC(INDEX)                                                     \
    case gl::VertexArray::DIRTY_BIT_ATTRIB_0 + INDEX:                                             \
        ANGLE_TRY(syncDirtyAttrib(context, attribs[INDEX], bindings[attribs[INDEX].bindingIndex], \
                                  INDEX));                                                        \
        mVertexArrayDirty = true;                                                                 \
        (*attribBits)[INDEX].reset();                                                             \
        break;

                ANGLE_VERTEX_INDEX_CASES(ANGLE_VERTEX_DIRTY_ATTRIB_FUNC)

#define ANGLE_VERTEX_DIRTY_BINDING_FUNC(INDEX)                                                    \
    case gl::VertexArray::DIRTY_BIT_BINDING_0 + INDEX:                                            \
        ANGLE_TRY(syncDirtyAttrib(context, attribs[INDEX], bindings[attribs[INDEX].bindingIndex], \
                                  INDEX));                                                        \
        mVertexArrayDirty = true;                                                                 \
        (*bindingBits)[INDEX].reset();                                                            \
        break;

                ANGLE_VERTEX_INDEX_CASES(ANGLE_VERTEX_DIRTY_BINDING_FUNC)

#define ANGLE_VERTEX_DIRTY_BUFFER_DATA_FUNC(INDEX)                                                \
    case gl::VertexArray::DIRTY_BIT_BUFFER_DATA_0 + INDEX:                                        \
        ANGLE_TRY(syncDirtyAttrib(context, attribs[INDEX], bindings[attribs[INDEX].bindingIndex], \
                                  INDEX));                                                        \
        mVertexDataDirty = true;                                                                  \
        break;

                ANGLE_VERTEX_INDEX_CASES(ANGLE_VERTEX_DIRTY_BUFFER_DATA_FUNC)

            default:
                UNREACHABLE();
                break;
        }
    }

    return angle::Result::Continue;
}

// vertexDescChanged is both input and output, the input value if is true, will force new
// mtl::VertexDesc to be returned via vertexDescOut. This typically happens when active shader
// program is changed.
// Otherwise, it is only returned when the vertex array is dirty.
angle::Result VertexArrayMtl::setupDraw(const gl::Context *glContext,
                                        mtl::RenderCommandEncoder *cmdEncoder,
                                        bool *vertexDescChanged,
                                        mtl::VertexDesc *vertexDescOut)
{
    // NOTE(hqle): consider only updating dirty attributes
    bool dirty = mVertexArrayDirty || *vertexDescChanged;

    if (dirty)
    {

        mVertexArrayDirty = false;
        mEmulatedInstanceAttribs.clear();

        const gl::ProgramExecutable *executable = glContext->getState().getProgramExecutable();
        const gl::AttributesMask &programActiveAttribsMask =
            executable->getActiveAttribLocationsMask();

        const std::vector<gl::VertexAttribute> &attribs = mState.getVertexAttributes();
        const std::vector<gl::VertexBinding> &bindings  = mState.getVertexBindings();

        mtl::VertexDesc &desc = *vertexDescOut;

        desc.numAttribs       = mtl::kMaxVertexAttribs;
        desc.numBufferLayouts = mtl::kMaxVertexAttribs;

        // Initialize the buffer layouts with constant step rate
        for (mtl::VertexBufferLayoutDesc &layout : desc.layouts)
        {
            layout = {0, 0, mtl::kVertexStepFunctionInvalid};
        }

        // Cache vertex shader input types
        std::array<uint8_t, mtl::kMaxVertexAttribs> currentAttribFormats{};
        for (auto &input : executable->getProgramInputs())
        {
            if (input.isBuiltIn())
            {
                continue;
            }

            ASSERT(input.getLocation() != -1);
            ASSERT(input.getLocation() < static_cast<int>(mtl::kMaxVertexAttribs));
            currentAttribFormats[input.getLocation()] = GetCurrentAttribFormat(input.getType());
        }
        MTLVertexFormat currentAttribFormat = MTLVertexFormatInvalid;

        for (uint32_t v = 0; v < mtl::kMaxVertexAttribs; ++v)
        {
            if (!programActiveAttribsMask.test(v))
            {
                ANGLE_UNSAFE_TODO(desc.attributes[v]) = {MTLVertexFormatInvalid, 0, 0};
                continue;
            }

            const auto &attrib               = attribs[v];
            const gl::VertexBinding &binding = bindings[attrib.bindingIndex];

            bool attribEnabled = attrib.enabled;
            if (attribEnabled &&
                !(mCurrentArrayBuffers[v] && mCurrentArrayBuffers[v]->getCurrentBuffer()) &&
                !mCurrentArrayInlineDataPointers[v])
            {
                // Disable it to avoid crash.
                attribEnabled = false;
            }

            if (currentAttribFormats[v] != MTLVertexFormatInvalid)
            {
                currentAttribFormat = static_cast<MTLVertexFormat>(currentAttribFormats[v]);
            }
            else
            {
                // This is a non-first matrix column
                ASSERT(currentAttribFormat != MTLVertexFormatInvalid);
            }

            if (!attribEnabled)
            {
                // Use default attribute
                ANGLE_UNSAFE_TODO(desc.attributes[v]) = {
                    currentAttribFormat,
                    /*offset*/ v * mtl::kDefaultAttributeSize,
                    /*bufferIndex*/ mtl::kDefaultAttribsBindingIndex};
            }
            else
            {
                uint32_t bufferIdx    = mtl::kVboBindingIndexStart + v;
                ASSERT(bufferIdx < mtl::kMaxVertexAttribs);
                uint32_t bufferOffset = static_cast<uint32_t>(mCurrentArrayBufferOffsets[v]);
                ASSERT((bufferOffset % mtl::kVertexAttribBufferStrideAlignment) == 0);
                ANGLE_UNSAFE_TODO(desc.attributes[v]) = {mCurrentArrayBufferFormats[v]->metalFormat,
                                                         /*offset*/ 0, bufferIdx};
                MTLVertexStepFunction stepFunction = MTLVertexStepFunctionPerVertex;
                uint32_t stepRate                  = 1;
                if (binding.getDivisor() != 0)
                {
                    stepFunction = MTLVertexStepFunctionPerInstance;
                    stepRate     = binding.getDivisor();
                }

                // Metal does not allow the sum of the buffer binding
                // offset and the vertex layout stride to be greater
                // than the buffer length.
                // In OpenGL, this is valid only when a draw call accesses just
                // one vertex, so just replace the stride with the format size.
                uint32_t stride = mCurrentArrayBufferStrides[v];
                if (mCurrentArrayBuffers[v])
                {
                    const size_t length = mCurrentArrayBuffers[v]->getCurrentBuffer()->size();
                    const size_t offset = mCurrentArrayBufferOffsets[v];
                    ASSERT(offset < length);
                    if (length - offset < stride)
                    {
                        stride = mCurrentArrayBufferFormats[v]->actualAngleFormat().pixelBytes;
                        ASSERT(stride % mtl::kVertexAttribBufferStrideAlignment == 0);
                    }
                }
                ANGLE_UNSAFE_TODO(desc.layouts[bufferIdx]) = {stepRate, stride, stepFunction};
            }
        }  // for (v)
    }

    if (dirty || mVertexDataDirty)
    {
        mVertexDataDirty                        = false;
        const gl::ProgramExecutable *executable = glContext->getState().getProgramExecutable();
        const gl::AttributesMask &programActiveAttribsMask =
            executable->getActiveAttribLocationsMask();

        for (uint32_t v = 0; v < mtl::kMaxVertexAttribs; ++v)
        {
            if (!programActiveAttribsMask.test(v))
            {
                continue;
            }
            uint32_t bufferIdx    = mtl::kVboBindingIndexStart + v;
            if (mCurrentArrayBuffers[v])
            {
                size_t bufferOffset = mCurrentArrayBufferOffsets[v];
                cmdEncoder->setVertexBuffer(mCurrentArrayBuffers[v]->getCurrentBuffer(),
                                            bufferOffset, bufferIdx);
            }
            else if (mCurrentArrayInlineDataPointers[v])
            {
                // No buffer specified, use the client memory directly as inline constant data
                ASSERT(mCurrentArrayInlineDataSizes[v] <= mInlineDataMaxSize);
                cmdEncoder->setVertexBytes(mCurrentArrayInlineDataPointers[v],
                                           mCurrentArrayInlineDataSizes[v], bufferIdx);
            }
        }
    }

    *vertexDescChanged = dirty;

    return angle::Result::Continue;
}

angle::Result VertexArrayMtl::updateClientAttribs(const gl::Context *context,
                                                  GLint firstVertex,
                                                  GLsizei vertexOrIndexCount,
                                                  GLsizei instanceCount,
                                                  gl::DrawElementsType indexTypeOrInvalid,
                                                  const void *indices)
{
    ContextMtl *contextMtl                  = mtl::GetImpl(context);
    const gl::AttributesMask &clientAttribs = context->getActiveClientAttribsMask();

    ASSERT(clientAttribs.any());

    GLint startVertex;
    size_t vertexCount;
    ANGLE_TRY(GetVertexRangeInfo(context, firstVertex, vertexOrIndexCount, indexTypeOrInvalid,
                                 indices, 0, &startVertex, &vertexCount));

    mDynamicVertexData.releaseInFlightBuffers(contextMtl);

    const std::vector<gl::VertexAttribute> &attribs = mState.getVertexAttributes();
    const std::vector<gl::VertexBinding> &bindings  = mState.getVertexBindings();

    for (size_t attribIndex : clientAttribs)
    {
        const gl::VertexAttribute &attrib = attribs[attribIndex];
        const gl::VertexBinding &binding  = bindings[attrib.bindingIndex];
        ASSERT(attrib.enabled && getVertexArrayBuffer(attrib.bindingIndex) == nullptr);

        // Source client memory pointer
        const uint8_t *src = static_cast<const uint8_t *>(attrib.pointer);
        ASSERT(src);

        GLint startElement;
        size_t elementCount;
        if (binding.getDivisor() == 0)
        {
            // Per vertex attribute
            startElement = startVertex;
            elementCount = vertexCount;
        }
        else
        {
            // Per instance attribute
            startElement = 0;
            elementCount = UnsignedCeilDivide(instanceCount, binding.getDivisor());
        }
        size_t bytesIntendedToUse = (startElement + elementCount) * binding.getStride();

        const mtl::VertexFormat &format = contextMtl->getVertexFormat(attrib.format->id, false);
        bool needStreaming              = format.actualFormatId != format.intendedFormatId ||
                             (binding.getStride() % mtl::kVertexAttribBufferStrideAlignment) != 0 ||
                             (binding.getStride() < format.actualAngleFormat().pixelBytes) ||
                             bytesIntendedToUse > mInlineDataMaxSize;

        if (!needStreaming)
        {
            // Data will be uploaded directly as inline constant data
            mCurrentArrayBuffers[attribIndex]            = nullptr;
            mCurrentArrayInlineDataPointers[attribIndex] = src;
            mCurrentArrayInlineDataSizes[attribIndex]    = bytesIntendedToUse;
            mCurrentArrayBufferOffsets[attribIndex]      = 0;
            mCurrentArrayBufferFormats[attribIndex]      = &format;
            mCurrentArrayBufferStrides[attribIndex]      = binding.getStride();
        }
        else
        {
            GLuint convertedStride;
            // Need to stream the client vertex data to a buffer.
            const mtl::VertexFormat &streamFormat =
                GetVertexConversionFormat(contextMtl, attrib.format->id, &convertedStride);

            // Allocate space for startElement + elementCount so indexing will work.  If we don't
            // start at zero all the indices will be off.
            // Only elementCount vertices will be used by the upcoming draw so that is all we copy.
            size_t bytesToAllocate = (startElement + elementCount) * convertedStride;
            ANGLE_UNSAFE_TODO(src += startElement * binding.getStride());
            size_t destOffset = startElement * convertedStride;

            mCurrentArrayBufferFormats[attribIndex] = &streamFormat;
            mCurrentArrayBufferStrides[attribIndex] = convertedStride;

            if (bytesToAllocate <= mInlineDataMaxSize)
            {
                // If the data is small enough, use host memory instead of creating GPU buffer. To
                // avoid synchronizing access to GPU buffer that is still in use.
                angle::MemoryBuffer &convertedClientArray =
                    mConvertedClientSmallArrays[attribIndex];
                if (bytesToAllocate > convertedClientArray.size())
                {
                    ANGLE_CHECK_GL_ALLOC(contextMtl, convertedClientArray.resize(bytesToAllocate));
                }

                ASSERT(streamFormat.vertexLoadFunction);
                streamFormat.vertexLoadFunction(
                    src, binding.getStride(), elementCount,
                    ANGLE_UNSAFE_TODO(convertedClientArray.data() + destOffset));

                mCurrentArrayBuffers[attribIndex]            = nullptr;
                mCurrentArrayInlineDataPointers[attribIndex] = convertedClientArray.data();
                mCurrentArrayInlineDataSizes[attribIndex]    = bytesToAllocate;
                mCurrentArrayBufferOffsets[attribIndex]      = 0;
            }
            else
            {
                // Stream the client data to a GPU buffer. Synchronization might happen if buffer is
                // in use.
                mDynamicVertexData.updateAlignment(contextMtl,
                                                   streamFormat.actualAngleFormat().pixelBytes);
                mtl::BufferSlice streamedBuffer;
                ANGLE_TRY(StreamVertexData(contextMtl, &mDynamicVertexData, src, bytesToAllocate,
                                           destOffset, elementCount, binding.getStride(),
                                           streamFormat.vertexLoadFunction, &streamedBuffer));
                if (contextMtl->getDisplay()->getFeatures().flushAfterStreamVertexData.enabled)
                {
                    // WaitUntilScheduled is needed for this workaround. NoWait does not have the
                    // needed effect.
                    contextMtl->flushCommandBuffer(mtl::WaitUntilScheduled);
                }

                mConvertedArrayBufferHolders[attribIndex].set(streamedBuffer.buffer());
                mCurrentArrayBufferOffsets[attribIndex] = streamedBuffer.offset();
                mCurrentArrayBuffers[attribIndex] = &mConvertedArrayBufferHolders[attribIndex];
            }
        }  // if (needStreaming)
    }

    mVertexArrayDirty = true;

    return angle::Result::Continue;
}

angle::Result VertexArrayMtl::syncDirtyAttrib(const gl::Context *glContext,
                                              const gl::VertexAttribute &attrib,
                                              const gl::VertexBinding &binding,
                                              size_t attribIndex)
{
    ContextMtl *contextMtl = mtl::GetImpl(glContext);
    ASSERT(mtl::kMaxVertexAttribs > attribIndex);
    mContentsObserverBindingsMask.reset(attrib.bindingIndex);

    if (attrib.enabled)
    {
        gl::Buffer *bufferGL            = getVertexArrayBuffer(attrib.bindingIndex);
        const mtl::VertexFormat &format = contextMtl->getVertexFormat(attrib.format->id, false);

        if (bufferGL)
        {
            BufferMtl *bufferMtl = mtl::GetImpl(bufferGL);
            // https://bugs.webkit.org/show_bug.cgi?id=236733
            // even non-converted buffers need to be observed for potential
            // data rebinds.
            mContentsObserverBindingsMask.set(attrib.bindingIndex);
            bool needConversion =
                format.actualFormatId != format.intendedFormatId ||
                (binding.getOffset() % mtl::kVertexAttribBufferStrideAlignment) != 0 ||
                (binding.getStride() < format.actualAngleFormat().pixelBytes) ||
                (binding.getStride() % mtl::kVertexAttribBufferStrideAlignment) != 0;

            if (needConversion)
            {
                ANGLE_TRY(convertVertexBuffer(glContext, bufferMtl, binding, attribIndex, format));
            }
            else
            {
                mCurrentArrayBuffers[attribIndex]       = bufferMtl;
                mCurrentArrayBufferOffsets[attribIndex] = binding.getOffset();
                mCurrentArrayBufferStrides[attribIndex] = binding.getStride();

                mCurrentArrayBufferFormats[attribIndex] = &format;
            }
        }
        else
        {
            // ContextMtl must feed the client data using updateClientAttribs()
        }
    }
    else
    {
        // Use default attribute value. Handled in setupDraw().
        mCurrentArrayBuffers[attribIndex]       = nullptr;
        mCurrentArrayBufferOffsets[attribIndex] = 0;
        mCurrentArrayBufferStrides[attribIndex] = 0;
        mCurrentArrayBufferFormats[attribIndex] =
            &contextMtl->getVertexFormat(angle::FormatID::NONE, false);
    }

    return angle::Result::Continue;
}

template <size_t indexRewind>
static void AppendDrawCommandsTemplate(std::vector<DrawCommandRange> &drawCommands,
                                       size_t count,
                                       size_t firstIndex,
                                       size_t indexSize)
{
    // Break the draw into hunks of 100'663'290 to avoid overflowing index count uint32_t.
    // Preserves primitive boundaries as the limit is divisible by all the possible per primitive
    // counts.
    constexpr size_t perCommandIndexCount = 0xffffff * 2 * 3;
    while (count > 0)
    {
        size_t offset       = firstIndex * indexSize;
        size_t commandCount = std::min(count, perCommandIndexCount);
        drawCommands.emplace_back(static_cast<uint32_t>(commandCount), offset);
        count -= commandCount;
        firstIndex += commandCount - indexRewind;  // Underflow ok, loop will terminate.
    }
}

static void AppendDrawCommands(std::vector<DrawCommandRange> &drawCommands,
                               gl::PrimitiveMode mode,
                               size_t count,
                               size_t firstIndex,
                               gl::PrimitiveMode drawMode,
                               gl::DrawElementsType type)
{
    size_t elementSize = gl::GetDrawElementsTypeSize(type);
    uint32_t perPrimitiveIndexCount;
    switch (drawMode)
    {
        case gl::PrimitiveMode::Points:
            perPrimitiveIndexCount = 1;
            break;
        case gl::PrimitiveMode::Lines:
        case gl::PrimitiveMode::LineStrip:
            perPrimitiveIndexCount = 2;
            break;
        case gl::PrimitiveMode::Triangles:
        case gl::PrimitiveMode::TriangleStrip:
            perPrimitiveIndexCount = 3;
            break;
        default:
            UNREACHABLE();
            return;
    }
    if (count < perPrimitiveIndexCount)
    {
        return;
    }
    if (mode != drawMode)
    {
        firstIndex *= perPrimitiveIndexCount;
        count = (count - perPrimitiveIndexCount + 1) * perPrimitiveIndexCount;
    }
    switch (drawMode)
    {
        case gl::PrimitiveMode::Points:
            AppendDrawCommandsTemplate<0>(drawCommands, count, firstIndex, elementSize);
            break;
        case gl::PrimitiveMode::Lines:
        case gl::PrimitiveMode::Triangles:
            // Rewritten index buffers only contain complete primitives.
            AppendDrawCommandsTemplate<0>(drawCommands, count - count % perPrimitiveIndexCount,
                                          firstIndex, elementSize);
            break;
        case gl::PrimitiveMode::LineStrip:
            AppendDrawCommandsTemplate<1>(drawCommands, count, firstIndex, elementSize);
            break;
        case gl::PrimitiveMode::TriangleStrip:
            AppendDrawCommandsTemplate<2>(drawCommands, count, firstIndex, elementSize);
            break;
        default:
            UNREACHABLE();
            return;
    }
}

// Computes draw command ranges from draw index ranges for primitive restart.
// The draw index ranges are in source buffer element space. The output draw commands
// are in draw buffer space, accounting for any expansion from mode conversion
// (e.g., TriangleStrip to Triangles via provoking vertex).
//
// drawIndexRanges: pre-computed ranges of consecutive non-restart indices in the source buffer
// firstIndex: first element index in the source buffer to draw from
// count: number of source elements in the draw window
// mode: source primitive mode, may be strip.
// drawMode: output primitive mode. Always a simple type: Points, Lines, Triangles.
// indexBufferType: type of the output buffer elements.
void AppendSimpleDrawCommandRanges(std::vector<DrawCommandRange> &drawCommands,
                                   gl::PrimitiveMode mode,
                                   uint32_t count,
                                   size_t firstIndex,
                                   const std::vector<DrawIndexRange> &drawIndexRanges,
                                   gl::PrimitiveMode drawMode,
                                   gl::DrawElementsType indexBufferType)
{
    uint32_t perPrimitiveIndexCount;
    switch (drawMode)
    {
        case gl::PrimitiveMode::Points:
            perPrimitiveIndexCount = 1;
            break;
        case gl::PrimitiveMode::Lines:
            perPrimitiveIndexCount = 2;
            break;
        case gl::PrimitiveMode::Triangles:
            perPrimitiveIndexCount = 3;
            break;
        default:
            UNREACHABLE();
            return;
    }
    if (count < perPrimitiveIndexCount)
    {
        return;
    }
    const size_t drawIndexSize = gl::GetDrawElementsTypeSize(indexBufferType);
    const size_t lastIndex     = firstIndex + count - 1;

    for (const auto &range : drawIndexRanges)
    {
        if (range.end < firstIndex)
        {
            continue;
        }
        if (range.begin > lastIndex)
        {
            break;
        }
        DrawIndexRange clippedRange{std::max(range.begin, firstIndex),
                                    std::min(range.end, lastIndex)};
        size_t indexCount = clippedRange.end - clippedRange.begin + 1;

        if (indexCount < perPrimitiveIndexCount)
        {
            continue;
        }

        size_t drawIndexCount;
        size_t drawBeginIndex;
        if (mode == drawMode)
        {
            drawIndexCount = indexCount - (indexCount % perPrimitiveIndexCount);
            drawBeginIndex = clippedRange.begin;
        }
        else
        {
            // Expanded modes: `N` source indices produce `(N - perPrimitiveIndexCount + 1)`
            // primitives that produce `perPrimitiveIndexCount` indices.
            drawIndexCount = (indexCount - perPrimitiveIndexCount + 1) * perPrimitiveIndexCount;
            drawBeginIndex = clippedRange.begin * perPrimitiveIndexCount;
        }
        AppendDrawCommandsTemplate<0>(drawCommands, drawIndexCount, drawBeginIndex, drawIndexSize);
    }
}

angle::Result VertexArrayMtl::resolveDrawElementsDraw(
    const gl::Context *glContext,
    gl::PrimitiveMode mode,
    gl::DrawElementsType type,
    GLsizei count,
    const void *indices,
    bool rewriteProvokingVertex,
    bool isPrimitiveRestartEnabled,
    gl::PrimitiveMode *outNewMode,
    std::vector<DrawCommandRange> *outDrawCommands,
    mtl::BufferSlice *outIndexBuffer,
    gl::DrawElementsType *outIndexBufferType)
{
    ContextMtl *contextMtl = mtl::GetImpl(glContext);

    // Resulting draw is either `uint16_t` or `uint32_t`.
    gl::DrawElementsType indexBufferType =
        type == gl::DrawElementsType::UnsignedByte ? gl::DrawElementsType::UnsignedShort : type;

    // Resulting draw is made with `newMode`.
    gl::PrimitiveMode newMode = mode;
    if (rewriteProvokingVertex)
    {
        // Provoking vertex will convert strips to their simple equivalents.
        switch (mode)
        {
            case gl::PrimitiveMode::Triangles:
            case gl::PrimitiveMode::TriangleStrip:
                newMode = gl::PrimitiveMode::Triangles;
                break;
            case gl::PrimitiveMode::Lines:
            case gl::PrimitiveMode::LineStrip:
                newMode = gl::PrimitiveMode::Lines;
                break;
            default:
                UNREACHABLE();
                return angle::Result::Stop;
        }
    }

    size_t firstIndex;
    mtl::BufferSlice indexBuffer;

    // Step 1: Get the index buffer of supported type and resolve the first index to
    // process.
    const gl::Buffer *glElementArrayBuffer = getElementArrayBuffer();
    if (glElementArrayBuffer == nullptr)
    {
        firstIndex = 0;
        ANGLE_TRY(streamIndexBufferFromClient(glContext, type, count, indices, &indexBuffer));
    }
    else
    {
        firstIndex = static_cast<size_t>(reinterpret_cast<uintptr_t>(indices)) /
                     gl::GetDrawElementsTypeSize(type);
        if (type != indexBufferType)
        {
            ANGLE_TRY(convertIndexBuffer(glContext, type, 0, &indexBuffer));
        }
        else
        {
            BufferMtl *bufferMtl = mtl::GetImpl(glElementArrayBuffer);
            indexBuffer          = mtl::BufferSlice(bufferMtl->getCurrentBuffer());
        }
    }

    // Step 2: GL draw range is firstIndex, count. In case Metal primitive topology for
    // `newMode` does not support primitive restart, compute the list of draw ranges
    // containing the primitives.
    const std::vector<DrawIndexRange> *indexRanges;
    std::vector<DrawIndexRange> indexRangesStorage;

    bool isSimpleType = newMode == gl::PrimitiveMode::Points ||
                        newMode == gl::PrimitiveMode::Lines ||
                        newMode == gl::PrimitiveMode::Triangles;

    if (isPrimitiveRestartEnabled && isSimpleType)
    {
        if (glElementArrayBuffer != nullptr)
        {
            BufferMtl *idxBuffer = mtl::GetImpl(glElementArrayBuffer);
            indexRanges          = &idxBuffer->getDrawIndexRanges(contextMtl, type);
        }
        else
        {
            indexRangesStorage = BufferMtl::GetDrawIndexRangesFromClientData(type, count, indices);
            indexRanges        = &indexRangesStorage;
        }
    }
    else
    {
        // No primitive restart splitting. Inject a single full range.
        indexRangesStorage.emplace_back(firstIndex, firstIndex + count - 1);
        indexRanges = &indexRangesStorage;
    }

    // Step 3: Conditionally rewrite the index buffer for provoking vertex.
    // preconditionIndexBuffer dispatches per-range, so with primitive restart it only
    // processes the non-restart runs (not the full buffer).
    if (rewriteProvokingVertex)
    {
        ConversionBufferMtl *conversion =
            glElementArrayBuffer
                ? mtl::GetImpl(glElementArrayBuffer)
                      ->getProvokingVertexConversionBuffer(contextMtl, mode, type, firstIndex,
                                                           count, isPrimitiveRestartEnabled)
                : nullptr;
        ANGLE_TRY(contextMtl->getProvokingVertexHelper().preconditionIndexBuffer(
            contextMtl, count, mode, firstIndex, isPrimitiveRestartEnabled, *indexRanges,
            std::move(indexBuffer), indexBufferType, conversion, &indexBuffer));
    }

    // Step 4: Compute draw command ranges (handles primitive restart splitting and large draw
    // chunking).  Reuses the same index ranges computed in Step 2.
    std::vector<DrawCommandRange> drawCommands;

    if (isPrimitiveRestartEnabled && isSimpleType)
    {
        AppendSimpleDrawCommandRanges(drawCommands, mode, static_cast<uint32_t>(count), firstIndex,
                                      *indexRanges, newMode, indexBufferType);
    }
    else
    {
        AppendDrawCommands(drawCommands, mode, static_cast<uint32_t>(count), firstIndex, newMode,
                           indexBufferType);
    }

    if (rewriteProvokingVertex)
    {
        // The rewritten index buffer starts at firstIndex, so rebase the draw commands onto it.
        size_t rewrittenOffset = firstIndex * gl::GetDrawElementsTypeSize(indexBufferType);
        if (mode != newMode)
        {
            // Strips expand to one primitive per source index.
            rewrittenOffset *= newMode == gl::PrimitiveMode::Triangles ? 3 : 2;
        }
        for (DrawCommandRange &command : drawCommands)
        {
            command.offset -= rewrittenOffset;
        }
    }

    *outNewMode         = newMode;
    *outDrawCommands    = std::move(drawCommands);
    *outIndexBuffer     = std::move(indexBuffer);
    *outIndexBufferType = indexBufferType;

    return angle::Result::Continue;
}

angle::Result VertexArrayMtl::convertIndexBuffer(const gl::Context *glContext,
                                                 gl::DrawElementsType indexType,
                                                 size_t offset,
                                                 mtl::BufferSlice *outIdxBuffer)
{
    size_t offsetModulo = offset % mtl::kIndexBufferOffsetAlignment;
    ASSERT(offsetModulo != 0 || indexType == gl::DrawElementsType::UnsignedByte);

    size_t alignedOffset = offset - offsetModulo;
    if (indexType == gl::DrawElementsType::UnsignedByte)
    {
        // Unsigned byte index will be promoted to unsigned short, thus double its offset.
        alignedOffset = alignedOffset << 1;
    }

    ContextMtl *contextMtl   = mtl::GetImpl(glContext);
    const gl::State &glState = glContext->getState();
    BufferMtl *idxBuffer     = mtl::GetImpl(getElementArrayBuffer());

    IndexConversionBufferMtl *conversion = idxBuffer->getIndexConversionBuffer(
        contextMtl, indexType, glState.isPrimitiveRestartEnabled(), offsetModulo);

    // Has the content of the buffer has changed since last conversion?
    if (!conversion->dirty)
    {
        // reuse the converted buffer
        *outIdxBuffer = conversion->buffer.subslice(alignedOffset);
        return angle::Result::Continue;
    }

    DisplayMtl *display = contextMtl->getDisplay();

    const size_t elementSize = gl::GetDrawElementsTypeSize(indexType);
    const size_t indexCount  = (idxBuffer->size() - offsetModulo) / elementSize;
    const size_t indexSize   = indexCount * elementSize;
    const size_t convertedIndexSize =
        indexType == gl::DrawElementsType::UnsignedByte ? indexSize * 2 : indexSize;
    conversion->bufferPool.releaseInFlightBuffers(contextMtl);
    mtl::BufferSlice converted;

    if ((!display->getFeatures().hasCheapRenderPass.enabled &&
         contextMtl->getRenderCommandEncoder()))
    {
        angle::Span<uint8_t> destination;
        ANGLE_TRY(conversion->bufferPool.allocateAndMap(contextMtl, convertedIndexSize,
                                                        &destination, &converted));

        // We shouldn't use GPU to convert when we are in a middle of a render pass.
        angle::Span<const uint8_t> source =
            idxBuffer->getBufferDataReadOnly(contextMtl, offsetModulo).first(indexSize);
        StreamIndexData(contextMtl, indexType, indexCount, source, destination,
                        glState.isPrimitiveRestartEnabled());
    }
    else
    {
        mtl::BufferSlice source =
            mtl::BufferSlice(idxBuffer->getCurrentBuffer()).subslice(offsetModulo);
        ANGLE_TRY(conversion->bufferPool.allocate(contextMtl, convertedIndexSize, &converted));
        ANGLE_TRY(display->getUtils().convertIndexBufferGPU(
            contextMtl, indexType, static_cast<uint32_t>(indexCount), source, converted,
            glState.isPrimitiveRestartEnabled()));
    }
    ANGLE_TRY(conversion->bufferPool.commit(contextMtl));
    conversion->dirty  = false;
    conversion->buffer = std::move(converted);
    // Calculate ranges for prim restart simple types.
    *outIdxBuffer = conversion->buffer.subslice(alignedOffset);

    return angle::Result::Continue;
}

angle::Result VertexArrayMtl::streamIndexBufferFromClient(const gl::Context *context,
                                                          gl::DrawElementsType indexType,
                                                          size_t indexCount,
                                                          const void *sourcePointer,
                                                          mtl::BufferSlice *outIdxBuffer)
{
    ASSERT(getElementArrayBuffer() == nullptr);
    ContextMtl *contextMtl = mtl::GetImpl(context);

    const size_t elementSize = gl::GetDrawElementsTypeSize(indexType);
    const size_t indexSize   = indexCount * elementSize;
    auto source              = ANGLE_UNSAFE_TODO(
        angle::Span<const uint8_t>(static_cast<const uint8_t *>(sourcePointer), indexSize));
    const size_t convertedIndexSize =
        indexType == gl::DrawElementsType::UnsignedByte ? indexSize * 2 : indexSize;
    mDynamicIndexData.releaseInFlightBuffers(contextMtl);
    mtl::BufferSlice converted;
    angle::Span<uint8_t> dstData;
    ANGLE_TRY(
        mDynamicIndexData.allocateAndMap(contextMtl, convertedIndexSize, &dstData, &converted));
    StreamIndexData(contextMtl, indexType, indexCount, source, dstData,
                    context->getState().isPrimitiveRestartEnabled());
    ANGLE_TRY(mDynamicIndexData.commit(contextMtl));
    *outIdxBuffer = std::move(converted);
    return angle::Result::Continue;
}

angle::Result VertexArrayMtl::convertVertexBuffer(const gl::Context *glContext,
                                                  BufferMtl *srcBuffer,
                                                  const gl::VertexBinding &binding,
                                                  size_t attribIndex,
                                                  const mtl::VertexFormat &srcVertexFormat)
{
    unsigned srcFormatSize = srcVertexFormat.intendedAngleFormat().pixelBytes;

    size_t numVertices = GetVertexCount(srcBuffer, binding, srcFormatSize);
    if (numVertices == 0)
    {
        // Out of bound buffer access, can return any values.
        // See KHR_robust_buffer_access_behavior
        mCurrentArrayBuffers[attribIndex]       = srcBuffer;
        mCurrentArrayBufferFormats[attribIndex] = &srcVertexFormat;
        mCurrentArrayBufferOffsets[attribIndex] = 0;
        mCurrentArrayBufferStrides[attribIndex] = 16;
        return angle::Result::Continue;
    }

    ContextMtl *contextMtl = mtl::GetImpl(glContext);

    // Convert to tightly packed format
    GLuint stride;
    const mtl::VertexFormat &convertedFormat =
        GetVertexConversionFormat(contextMtl, srcVertexFormat.intendedFormatId, &stride);

    ConversionBufferMtl *conversion = srcBuffer->getVertexConversionBuffer(
        contextMtl, srcVertexFormat.intendedFormatId, binding.getStride(), binding.getOffset());

    // Has the content of the buffer has changed since last conversion?
    if (!conversion->dirty)
    {
        VertexConversionBufferMtl *vertexConversionMtl =
            static_cast<VertexConversionBufferMtl *>(conversion);
        ASSERT((binding.getOffset() - vertexConversionMtl->offset) % binding.getStride() == 0);
        mConvertedArrayBufferHolders[attribIndex].set(conversion->buffer.buffer());
        mCurrentArrayBufferOffsets[attribIndex] =
            conversion->buffer.offset() +
            stride * ((binding.getOffset() - vertexConversionMtl->offset) / binding.getStride());

        mCurrentArrayBuffers[attribIndex]       = &mConvertedArrayBufferHolders[attribIndex];
        mCurrentArrayBufferFormats[attribIndex] = &convertedFormat;
        mCurrentArrayBufferStrides[attribIndex] = stride;
        return angle::Result::Continue;
    }
    numVertices = GetVertexCountWithConversion(
        srcBuffer, static_cast<VertexConversionBufferMtl *>(conversion), binding, srcFormatSize);

    const angle::Format &convertedAngleFormat = convertedFormat.actualAngleFormat();
    bool canConvertToFloatOnGPU =
        convertedAngleFormat.isFloat() && !convertedAngleFormat.isVertexTypeHalfFloat();

    bool canExpandComponentsOnGPU = convertedFormat.actualSameGLType;

    conversion->bufferPool.releaseInFlightBuffers(contextMtl);
    conversion->bufferPool.updateAlignment(contextMtl, convertedAngleFormat.pixelBytes);

    if (canConvertToFloatOnGPU || canExpandComponentsOnGPU)
    {
        ANGLE_TRY(convertVertexBufferGPU(glContext, srcBuffer, binding, attribIndex,
                                         convertedFormat, stride, numVertices,
                                         canExpandComponentsOnGPU, conversion));
    }
    else
    {
        ANGLE_TRY(convertVertexBufferCPU(contextMtl, srcBuffer, binding, attribIndex,
                                         convertedFormat, stride, numVertices, conversion));
    }

    mConvertedArrayBufferHolders[attribIndex].set(conversion->buffer.buffer());
    mCurrentArrayBufferOffsets[attribIndex] =
        conversion->buffer.offset() +
        stride *
            ((binding.getOffset() - static_cast<VertexConversionBufferMtl *>(conversion)->offset) /
             binding.getStride());
    mCurrentArrayBuffers[attribIndex]       = &mConvertedArrayBufferHolders[attribIndex];
    mCurrentArrayBufferFormats[attribIndex] = &convertedFormat;
    mCurrentArrayBufferStrides[attribIndex] = stride;

    ASSERT(conversion->dirty);
    conversion->dirty = false;

#ifndef NDEBUG
    ANGLE_MTL_OBJC_SCOPE
    {
        mConvertedArrayBufferHolders[attribIndex].getCurrentBuffer()->get().label =
            [NSString stringWithFormat:@"Converted from %p offset=%zu stride=%u", srcBuffer,
                                       binding.getOffset(), binding.getStride()];
    }
#endif

    return angle::Result::Continue;
}

angle::Result VertexArrayMtl::convertVertexBufferCPU(ContextMtl *contextMtl,
                                                     BufferMtl *srcBuffer,
                                                     const gl::VertexBinding &binding,
                                                     size_t attribIndex,
                                                     const mtl::VertexFormat &convertedFormat,
                                                     GLuint targetStride,
                                                     size_t numVertices,
                                                     ConversionBufferMtl *conversion)
{
    VertexConversionBufferMtl *vertexConverison =
        static_cast<VertexConversionBufferMtl *>(conversion);
    size_t srcOffset = MIN(binding.getOffset(), static_cast<uintptr_t>(vertexConverison->offset));
    angle::Span<const uint8_t> srcBytes = srcBuffer->getBufferDataReadOnly(contextMtl, srcOffset);
    ANGLE_CHECK_GL_ALLOC(contextMtl, !srcBytes.empty());
    mtl::BufferSlice convertedBuffer;
    ANGLE_TRY(StreamVertexData(contextMtl, &conversion->bufferPool, srcBytes.data(),
                               numVertices * targetStride, 0, numVertices, binding.getStride(),
                               convertedFormat.vertexLoadFunction, &convertedBuffer));
    conversion->buffer = std::move(convertedBuffer);
    return angle::Result::Continue;
}

angle::Result VertexArrayMtl::convertVertexBufferGPU(const gl::Context *glContext,
                                                     BufferMtl *srcBuffer,
                                                     const gl::VertexBinding &binding,
                                                     size_t attribIndex,
                                                     const mtl::VertexFormat &convertedFormat,
                                                     GLuint targetStride,
                                                     size_t numVertices,
                                                     bool isExpandingComponents,
                                                     ConversionBufferMtl *conversion)
{
    ContextMtl *contextMtl = mtl::GetImpl(glContext);

    mtl::BufferSlice newBuffer;
    ANGLE_TRY(conversion->bufferPool.allocate(contextMtl, numVertices * targetStride, &newBuffer));

    ANGLE_CHECK_GL_MATH(contextMtl, binding.getOffset() <= std::numeric_limits<uint32_t>::max());
    ANGLE_CHECK_GL_MATH(contextMtl, newBuffer.offset() <= std::numeric_limits<uint32_t>::max());
    ANGLE_CHECK_GL_MATH(contextMtl, numVertices <= std::numeric_limits<uint32_t>::max());

    mtl::VertexFormatConvertParams params;
    VertexConversionBufferMtl *vertexConversion =
        static_cast<VertexConversionBufferMtl *>(conversion);
    params.srcBuffer            = srcBuffer->getCurrentBuffer();
    params.srcBufferStartOffset = static_cast<uint32_t>(
        MIN(static_cast<uintptr_t>(vertexConversion->offset), binding.getOffset()));
    params.srcStride           = binding.getStride();
    params.srcDefaultAlphaData = convertedFormat.defaultAlpha;

    params.dstBuffer            = newBuffer.buffer();
    params.dstBufferStartOffset = static_cast<uint32_t>(newBuffer.offset());
    params.dstStride            = targetStride;
    params.dstComponents        = convertedFormat.actualAngleFormat().channelCount;

    params.vertexCount = static_cast<uint32_t>(numVertices);

    mtl::RenderUtils &utils = contextMtl->getDisplay()->getUtils();

    // Compute based buffer conversion.
    if (!isExpandingComponents)
    {
        ANGLE_TRY(utils.convertVertexFormatToFloatCS(
            contextMtl, convertedFormat.intendedAngleFormat(), params));
    }
    else
    {
        ANGLE_TRY(utils.expandVertexFormatComponentsCS(
            contextMtl, convertedFormat.intendedAngleFormat(), params));
    }

    ANGLE_TRY(conversion->bufferPool.commit(contextMtl));

    conversion->buffer = std::move(newBuffer);

    return angle::Result::Continue;
}
}  // namespace rx
