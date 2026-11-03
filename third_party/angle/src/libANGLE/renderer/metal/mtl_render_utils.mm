//
// Copyright 2019 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// mtl_render_utils.mm:
//    Implements the class methods for RenderUtils.
//

#include "libANGLE/renderer/metal/mtl_render_utils.h"

#include <utility>

#include "common/debug.h"
#include "common/unsafe_buffers.h"
#include "libANGLE/ErrorStrings.h"
#include "libANGLE/renderer/metal/BufferMtl.h"
#include "libANGLE/renderer/metal/ContextMtl.h"
#include "libANGLE/renderer/metal/DisplayMtl.h"
#include "libANGLE/renderer/metal/ProgramMtl.h"
#include "libANGLE/renderer/metal/mtl_common.h"
#include "libANGLE/renderer/metal/mtl_utils.h"

namespace rx
{
namespace mtl
{
namespace
{

#define NUM_COLOR_OUTPUTS_CONSTANT_NAME @"kNumColorOutputs"
#define SOURCE_BUFFER_ALIGNED_CONSTANT_NAME @"kSourceBufferAligned"
#define SOURCE_IDX_IS_U8_CONSTANT_NAME @"kSourceIndexIsU8"
#define SOURCE_IDX_IS_U16_CONSTANT_NAME @"kSourceIndexIsU16"
#define SOURCE_IDX_IS_U32_CONSTANT_NAME @"kSourceIndexIsU32"
#define PREMULTIPLY_ALPHA_CONSTANT_NAME @"kPremultiplyAlpha"
#define UNMULTIPLY_ALPHA_CONSTANT_NAME @"kUnmultiplyAlpha"
#define TRANSFORM_LINEAR_TO_SRGB_CONSTANT_NAME @"kTransformLinearToSrgb"
#define SOURCE_TEXTURE_TYPE_CONSTANT_NAME @"kSourceTextureType"
#define SOURCE_TEXTURE2_TYPE_CONSTANT_NAME @"kSourceTexture2Type"
#define COPY_FORMAT_TYPE_CONSTANT_NAME @"kCopyFormatType"
#define PIXEL_COPY_TEXTURE_TYPE_CONSTANT_NAME @"kCopyTextureType"
#define VISIBILITY_RESULT_KEEP_OLD_VAL_CONSTANT_NAME @"kCombineWithExistingResult"

// See libANGLE/renderer/metal/shaders/clear.metal
struct ClearParamsUniform
{
    float clearColor[4];
    float clearDepth;
    float padding[3];
};

// See libANGLE/renderer/metal/shaders/blit.metal
struct BlitParamsUniform
{
    // 0: lower left, 1: upper right
    float srcTexCoords[2][2];
    int srcLevel         = 0;
    int srcLayer         = 0;
    uint8_t dstLuminance = 0;  // dest texture is luminace
    uint8_t padding[7];
};

struct BlitStencilToBufferParamsUniform
{
    float srcStartTexCoords[2];
    float srcTexCoordSteps[2];
    uint32_t srcLevel;
    uint32_t srcLayer;

    uint32_t dstSize[2];
    uint32_t dstBufferRowPitch;
    uint8_t resolveMS;

    uint8_t padding[11];
};

// See libANGLE/renderer/metal/shaders/genIndices.metal
struct TriFanOrLineLoopArrayParams
{
    uint firstVertex;
    uint vertexCount;
    uint padding[2];
};

struct IndexConversionUniform
{
    uint32_t srcOffset;
    uint32_t indexCount;
    uint8_t primitiveRestartEnabled;
    uint8_t padding[7];
};

// See libANGLE/renderer/metal/shaders/visibility.metal
struct CombineVisibilityResultUniform
{
    uint32_t startOffset;
    uint32_t numOffsets;
    uint32_t padding[2];
};

// See libANGLE/renderer/metal/shaders/gen_mipmap.metal
struct Generate3DMipmapUniform
{
    uint32_t srcLevel;
    uint32_t numMipmapsToGenerate;
    uint8_t sRGB;
    uint8_t padding[7];
};

// See libANGLE/renderer/metal/shaders/copy_buffer.metal
struct CopyPixelFromBufferUniforms
{
    uint32_t bufferStartOffset;
    uint32_t pixelSize;
    uint32_t bufferRowPitch;
    uint32_t bufferDepthPitch;

    uint32_t textureOffset[2];
    uint8_t padding[8];
};
struct WritePixelToBufferUniforms
{
    uint32_t copySize[2];
    uint32_t textureOffset[2];

    uint32_t bufferStartOffset;
    uint32_t pixelSize;
    uint32_t bufferRowPitch;

    uint32_t textureLevel;
    uint32_t textureLayer;
    uint8_t reverseTextureRowOrder;

    uint8_t padding[11];
};

struct CopyVertexUniforms
{
    uint32_t srcBufferStartOffset;
    uint32_t srcStride;
    uint32_t srcComponentBytes;
    uint32_t srcComponents;
    uint32_t srcDefaultAlphaData;

    uint32_t dstBufferStartOffset;
    uint32_t dstStride;
    uint32_t dstComponents;

    uint32_t vertexCount;

    uint32_t padding[3];
};

constexpr angle::FormatID kSupportedPixelUnpackFormats[] = {
    // Float formats
    angle::FormatID::R5G6B5_UNORM,
    angle::FormatID::R8G8B8A8_UNORM,
    angle::FormatID::R8G8B8A8_UNORM_SRGB,
    angle::FormatID::R8G8B8A8_SNORM,
    angle::FormatID::B8G8R8A8_UNORM,
    angle::FormatID::B8G8R8A8_UNORM_SRGB,
    angle::FormatID::R8G8B8_UNORM,
    angle::FormatID::R8G8B8_UNORM_SRGB,
    angle::FormatID::R8G8B8_SNORM,
    angle::FormatID::L8_UNORM,
    angle::FormatID::L8A8_UNORM,
    angle::FormatID::R5G5B5A1_UNORM,
    angle::FormatID::R4G4B4A4_UNORM,
    angle::FormatID::R8_UNORM,
    angle::FormatID::R8_SNORM,
    angle::FormatID::R8G8_UNORM,
    angle::FormatID::R8G8_SNORM,
    angle::FormatID::R16_FLOAT,
    angle::FormatID::R16_SNORM,
    angle::FormatID::R16_UNORM,
    angle::FormatID::A16_FLOAT,
    angle::FormatID::L16_FLOAT,
    angle::FormatID::L16A16_FLOAT,
    angle::FormatID::R16G16_FLOAT,
    angle::FormatID::R16G16_SNORM,
    angle::FormatID::R16G16_UNORM,
    angle::FormatID::R16G16B16_FLOAT,
    angle::FormatID::R16G16B16_SNORM,
    angle::FormatID::R16G16B16_UNORM,
    angle::FormatID::R16G16B16A16_FLOAT,
    angle::FormatID::R16G16B16A16_SNORM,
    angle::FormatID::R16G16B16A16_UNORM,
    angle::FormatID::R32_FLOAT,
    angle::FormatID::A32_FLOAT,
    angle::FormatID::L32_FLOAT,
    angle::FormatID::L32A32_FLOAT,
    angle::FormatID::R32G32_FLOAT,
    angle::FormatID::R32G32B32_FLOAT,
    angle::FormatID::R32G32B32A32_FLOAT,

    // Int formats
    angle::FormatID::R8_SINT,
    angle::FormatID::R8G8_SINT,
    angle::FormatID::R8G8B8_SINT,
    angle::FormatID::R8G8B8A8_SINT,
    angle::FormatID::R16_SINT,
    angle::FormatID::R16G16_SINT,
    angle::FormatID::R16G16B16_SINT,
    angle::FormatID::R16G16B16A16_SINT,
    angle::FormatID::R32_SINT,
    angle::FormatID::R32G32_SINT,
    angle::FormatID::R32G32B32_SINT,
    angle::FormatID::R32G32B32A32_SINT,

    // Uint formats
    angle::FormatID::R8_UINT,
    angle::FormatID::R8G8_UINT,
    angle::FormatID::R8G8B8_UINT,
    angle::FormatID::R8G8B8A8_UINT,
    angle::FormatID::R16_UINT,
    angle::FormatID::R16G16_UINT,
    angle::FormatID::R16G16B16_UINT,
    angle::FormatID::R16G16B16A16_UINT,
    angle::FormatID::R32_UINT,
    angle::FormatID::R32G32_UINT,
    angle::FormatID::R32G32B32_UINT,
    angle::FormatID::R32G32B32A32_UINT,
};

// Class to automatically disable occlusion query upon entering block. The query will be resumed in
// the next ContextMtl's draw call.
struct ScopedDisableOcclusionQuery
{
    ScopedDisableOcclusionQuery(ContextMtl *contextMtl, RenderCommandEncoder *encoder)
        : mEncoder(encoder), mOcclusionQueryIsEnabled(contextMtl->hasActiveOcclusionQuery())
    {
        if (!mOcclusionQueryIsEnabled)
        {
            return;
        }
#ifndef NDEBUG
        encoder->pushDebugGroup(@"Disabled OcclusionQuery");
#endif
        contextMtl->disableActiveOcclusionQueryInRenderPass();
    }
    ~ScopedDisableOcclusionQuery()
    {
        if (!mOcclusionQueryIsEnabled)
        {
            return;
        }
#ifndef NDEBUG
        mEncoder->popDebugGroup();
#else
        ANGLE_UNUSED_VARIABLE(mEncoder);
#endif
    }

  private:
    RenderCommandEncoder *mEncoder;
    const bool mOcclusionQueryIsEnabled;
};

void GetBlitTexCoords(const NormalizedCoords &normalizedCoords,
                      bool srcYFlipped,
                      bool unpackFlipX,
                      bool unpackFlipY,
                      float *u0,
                      float *v0,
                      float *u1,
                      float *v1)
{
    *u0 = normalizedCoords.v[0];
    *v0 = normalizedCoords.v[1];
    *u1 = normalizedCoords.v[2];
    *v1 = normalizedCoords.v[3];

    if (srcYFlipped)
    {
        *v0 = 1.0 - *v0;
        *v1 = 1.0 - *v1;
    }

    if (unpackFlipX)
    {
        std::swap(*u0, *u1);
    }

    if (unpackFlipY)
    {
        std::swap(*v0, *v1);
    }
}

template <typename T>
angle::Result GenTriFanFromClientElements(ContextMtl *contextMtl,
                                          GLsizei count,
                                          bool primitiveRestartEnabled,
                                          const T *indices,
                                          const BufferRef &dstBuffer,
                                          uint32_t dstOffset,
                                          uint32_t *indicesGenerated)
{
    ASSERT(count > 2);
    ASSERT(indicesGenerated != nullptr);
    constexpr T kSrcPrimitiveRestartIndex = std::numeric_limits<T>::max();
    GLsizei dstTriangle                   = 0;
    uint32_t *dstPtr = reinterpret_cast<uint32_t *>(dstBuffer->map(contextMtl, dstOffset).data());
    T triFirstIdx;
    ANGLE_UNSAFE_TODO(memcpy(&triFirstIdx, indices, sizeof(triFirstIdx)));

    if (primitiveRestartEnabled)
    {
        GLsizei triFirstIdxLoc = 0;
        while (triFirstIdx == kSrcPrimitiveRestartIndex && triFirstIdxLoc + 2 < count)
        {
            ++triFirstIdxLoc;
            ANGLE_UNSAFE_TODO(memcpy(&triFirstIdx, indices + triFirstIdxLoc, sizeof(triFirstIdx)));
        }

        T srcPrevIdx = 0;
        if (triFirstIdxLoc + 1 < count)
        {
            ANGLE_UNSAFE_TODO(
                memcpy(&srcPrevIdx, indices + triFirstIdxLoc + 1, sizeof(srcPrevIdx)));
        }

        for (GLsizei i = triFirstIdxLoc + 2; i < count; ++i)
        {
            uint32_t triIndices[3];
            T srcIdx;
            ANGLE_UNSAFE_TODO(memcpy(&srcIdx, indices + i, sizeof(srcIdx)));
            bool completeTriangle = true;
            if (srcPrevIdx == kSrcPrimitiveRestartIndex || srcIdx == kSrcPrimitiveRestartIndex)
            {
                // Incomplete triangle. Move to next triangle and set triFirstIndex
                triFirstIdx      = srcIdx;
                triFirstIdxLoc   = i;
                completeTriangle = false;
            }
            else if (i < triFirstIdxLoc + 2)
            {
                // Incomplete triangle, move to next triangle
                completeTriangle = false;
            }
            else
            {
                triIndices[0] = triFirstIdx;
                triIndices[1] = srcPrevIdx;
                triIndices[2] = srcIdx;
            }
            if (completeTriangle)
            {
                ANGLE_UNSAFE_TODO(memcpy(dstPtr + 3 * dstTriangle, triIndices, sizeof(triIndices)));
                ++dstTriangle;
            }
            srcPrevIdx = srcIdx;
        }
    }
    else
    {
        T srcPrevIdx;
        ANGLE_UNSAFE_TODO(memcpy(&srcPrevIdx, indices + 1, sizeof(srcPrevIdx)));

        for (GLsizei i = 2; i < count; ++i)
        {
            T srcIdx;
            ANGLE_UNSAFE_TODO(memcpy(&srcIdx, indices + i, sizeof(srcIdx)));

            uint32_t triIndices[3];
            triIndices[0] = triFirstIdx;
            triIndices[1] = srcPrevIdx;
            triIndices[2] = srcIdx;
            srcPrevIdx    = srcIdx;

            ANGLE_UNSAFE_TODO(memcpy(dstPtr + 3 * dstTriangle, triIndices, sizeof(triIndices)));
            ++dstTriangle;
        }
    }
    *indicesGenerated = dstTriangle * 3;
    dstBuffer->unmapAndFlushSubset(contextMtl, dstOffset, *(indicesGenerated) * sizeof(uint32_t));

    return angle::Result::Continue;
}

// Converts line loop vertices to line strip vertices.
// Returns number of vertices written.
template <typename In, typename Out>
size_t CopyLineLoopIndices(GLsizei indexCount,
                           const uint8_t *indices,
                           bool usePrimitiveRestart,
                           uint8_t *outIndices)
{
    if (usePrimitiveRestart)
    {
        return CopyLineLoopIndicesWithRestart<In, Out>(indexCount, indices, outIndices);
    }
    if (indexCount <= 0)
    {
        return 0;
    }
    In firstValue;
    ANGLE_UNSAFE_TODO(memcpy(&firstValue, indices, sizeof(In)));
    for (GLsizei i = 0; i < indexCount; ++i)
    {
        In value;
        ANGLE_UNSAFE_TODO({
            memcpy(&value, indices, sizeof(In));
            indices += sizeof(In);
        })
        Out outValue = value;
        ANGLE_UNSAFE_TODO({
            memcpy(outIndices, &outValue, sizeof(Out));
            outIndices += sizeof(Out);
        })
    }
    Out outFirstValue = firstValue;
    ANGLE_UNSAFE_TODO(memcpy(outIndices, &outFirstValue, sizeof(Out)));
    return indexCount + 1;
}

template <typename T>
void GetFirstLastIndicesFromClientElements(GLsizei count,
                                           const T *indices,
                                           uint32_t *firstOut,
                                           uint32_t *lastOut)
{
    *firstOut = 0;
    *lastOut  = 0;
    ANGLE_UNSAFE_TODO({
        memcpy(firstOut, indices, sizeof(indices[0]));
        memcpy(lastOut, indices + count - 1, sizeof(indices[0]));
    })
}

int GetShaderTextureType(const TextureRef &texture)
{
    if (!texture)
    {
        return -1;
    }
    switch (texture->textureType())
    {
        case MTLTextureType2D:
            return mtl_shader::kTextureType2D;
        case MTLTextureType2DArray:
            return mtl_shader::kTextureType2DArray;
        case MTLTextureType2DMultisample:
            return mtl_shader::kTextureType2DMultisample;
        case MTLTextureTypeCube:
            return mtl_shader::kTextureTypeCube;
        case MTLTextureType3D:
            return mtl_shader::kTextureType3D;
        default:
            UNREACHABLE();
    }

    return 0;
}

int GetPixelTypeIndex(const angle::Format &angleFormat)
{
    if (angleFormat.isSint())
    {
        return static_cast<int>(PixelType::Int);
    }
    else if (angleFormat.isUint())
    {
        return static_cast<int>(PixelType::UInt);
    }
    else
    {
        return static_cast<int>(PixelType::Float);
    }
}

angle::Result EnsureShaderInitialized(ContextMtl *context,
                                      NSString *functionName,
                                      angle::ObjCPtr<id<MTLFunction>> *shaderOut)
{
    angle::ObjCPtr<id<MTLFunction>> &shader = *shaderOut;
    if (shader)
    {
        return angle::Result::Continue;
    }

    ANGLE_MTL_OBJC_SCOPE
    {
        auto shaderLib = context->getDisplay()->getDefaultShadersLib();
        shader         = angle::adoptObjCPtr([shaderLib newFunctionWithName:functionName]);
        ANGLE_CHECK(context, shader, gl::err::kInternalError, GL_INVALID_OPERATION);
        return angle::Result::Continue;
    }
}

angle::Result EnsureSpecializedShaderInitialized(ContextMtl *context,
                                                 NSString *functionName,
                                                 MTLFunctionConstantValues *funcConstants,
                                                 angle::ObjCPtr<id<MTLFunction>> *shaderOut)
{
    if (!funcConstants)
    {
        // Non specialized constants provided, use default creation function.
        return EnsureShaderInitialized(context, functionName, shaderOut);
    }

    angle::ObjCPtr<id<MTLFunction>> &shader = *shaderOut;
    if (shader)
    {
        return angle::Result::Continue;
    }

    ANGLE_MTL_OBJC_SCOPE
    {
        auto shaderLib = context->getDisplay()->getDefaultShadersLib();
        NSError *err   = nil;
        shader         = angle::adoptObjCPtr([shaderLib newFunctionWithName:functionName
                                                     constantValues:funcConstants
                                                              error:&err]);
        ANGLE_MTL_CHECK(context, shader, err);
        return angle::Result::Continue;
    }
}

// Get pipeline descriptor for render pipeline that contains vertex shader acting as compute shader.
ANGLE_INLINE
RenderPipelineDesc GetComputingVertexShaderOnlyRenderPipelineDesc(RenderCommandEncoder *cmdEncoder)
{
    RenderPipelineDesc pipelineDesc;
    const RenderPassDesc &renderPassDesc = cmdEncoder->renderPassDesc();

    renderPassDesc.populateRenderPipelineOutputDesc(&pipelineDesc.outputDescriptor);
    pipelineDesc.setRasterizationType(RenderPipelineRasterization::Disabled);
    pipelineDesc.setInputPrimitiveTopology(MTLPrimitiveTopologyClassPoint);

    return pipelineDesc;
}

// Dispatch compute using 3D grid
void DispatchCompute(ContextMtl *contextMtl,
                     ComputeCommandEncoder *encoder,
                     bool allowNonUniform,
                     const MTLSize &numThreads,
                     const MTLSize &threadsPerThreadgroup)
{
    if (allowNonUniform && contextMtl->getDisplay()->getFeatures().hasNonUniformDispatch.enabled)
    {
        encoder->dispatchNonUniform(numThreads, threadsPerThreadgroup);
    }
    else
    {
        MTLSize groups = MTLSizeMake(
            (numThreads.width + threadsPerThreadgroup.width - 1) / threadsPerThreadgroup.width,
            (numThreads.height + threadsPerThreadgroup.height - 1) / threadsPerThreadgroup.height,
            (numThreads.depth + threadsPerThreadgroup.depth - 1) / threadsPerThreadgroup.depth);
        encoder->dispatch(groups, threadsPerThreadgroup);
    }
}

// Dispatch compute using 1D grid
void DispatchCompute(ContextMtl *contextMtl,
                     ComputeCommandEncoder *cmdEncoder,
                     id<MTLComputePipelineState> pipelineState,
                     size_t numThreads)
{
    NSUInteger w = std::min<NSUInteger>(pipelineState.threadExecutionWidth, numThreads);
    MTLSize threadsPerThreadgroup = MTLSizeMake(w, 1, 1);

    if (contextMtl->getDisplay()->getFeatures().hasNonUniformDispatch.enabled)
    {
        MTLSize threads = MTLSizeMake(numThreads, 1, 1);
        cmdEncoder->dispatchNonUniform(threads, threadsPerThreadgroup);
    }
    else
    {
        MTLSize groups = MTLSizeMake((numThreads + w - 1) / w, 1, 1);
        cmdEncoder->dispatch(groups, threadsPerThreadgroup);
    }
}

void SetupFullscreenQuadDrawCommonStates(RenderCommandEncoder *cmdEncoder)
{
    cmdEncoder->setCullMode(MTLCullModeNone);
    cmdEncoder->setTriangleFillMode(MTLTriangleFillModeFill);
    cmdEncoder->setDepthBias(0, 0, 0);
}

void SetupBlitWithDrawUniformData(RenderCommandEncoder *cmdEncoder,
                                  const BlitParams &params,
                                  bool isColorBlit)
{
    // To ensure consistent texture coordinate interpolation on Apple silicon, a two-triangle quad
    // with the common edge going from upper-left to lower-right must be used. Any other primitive,
    // e.g., a clipped triangle, would produce various texture sampling artifacts on that hardware.

    BlitParamsUniform uniformParams;
    uniformParams.srcLevel = params.srcLevel.get();
    uniformParams.srcLayer = params.srcLayer;
    if (isColorBlit)
    {
        const ColorBlitParams *colorParams = static_cast<const ColorBlitParams *>(&params);
        uniformParams.dstLuminance         = colorParams->dstLuminance ? 1 : 0;
    }

    float u0, v0, u1, v1;
    GetBlitTexCoords(params.srcNormalizedCoords, params.srcYFlipped, params.unpackFlipX,
                     params.unpackFlipY, &u0, &v0, &u1, &v1);

    if (params.dstFlipX)
    {
        std::swap(u0, u1);
    }

    // If viewport is not flipped, we have to flip Y in normalized device coordinates
    // since NDC has Y in the opposite direction of viewport coodrinates.
    // To keep the common edge properly oriented, swap the texture coordinates instead.
    if (!params.dstFlipY)
    {
        std::swap(v0, v1);
    }

    // lower left
    uniformParams.srcTexCoords[0][0] = u0;
    uniformParams.srcTexCoords[0][1] = v0;
    // upper right
    uniformParams.srcTexCoords[1][0] = u1;
    uniformParams.srcTexCoords[1][1] = v1;

    cmdEncoder->setVertexData(uniformParams, 0);
    cmdEncoder->setFragmentData(uniformParams, 0);
}

void SetupCommonBlitWithDrawStates(const gl::Context *context,
                                   RenderCommandEncoder *cmdEncoder,
                                   const BlitParams &params,
                                   bool isColorBlit)
{
    // Setup states
    SetupFullscreenQuadDrawCommonStates(cmdEncoder);

    // Viewport
    MTLViewport viewportMtl =
        GetViewport(params.dstRect, params.dstTextureSize.height, params.dstFlipY);
    MTLScissorRect scissorRectMtl =
        GetScissorRect(params.dstScissorRect, params.dstTextureSize.height, params.dstFlipY);
    cmdEncoder->setViewport(viewportMtl);
    cmdEncoder->setScissorRect(scissorRectMtl);

    if (params.src)
    {
        cmdEncoder->setFragmentTexture(params.src, 0);
    }

    // Uniform
    SetupBlitWithDrawUniformData(cmdEncoder, params, isColorBlit);
}

// Overloaded functions to be used with both compute and render command encoder.
ANGLE_INLINE void SetComputeOrVertexBuffer(RenderCommandEncoder *encoder,
                                           const BufferRef &buffer,
                                           size_t offset,
                                           uint32_t index)
{
    encoder->setBuffer(gl::ShaderType::Vertex, buffer, offset, index);
}
ANGLE_INLINE void SetComputeOrVertexBufferForWrite(RenderCommandEncoder *encoder,
                                                   const BufferRef &buffer,
                                                   size_t offset,
                                                   uint32_t index)
{
    encoder->setBufferForWrite(gl::ShaderType::Vertex, buffer, offset, index);
}
ANGLE_INLINE void SetComputeOrVertexBuffer(ComputeCommandEncoder *encoder,
                                           const BufferRef &buffer,
                                           size_t offset,
                                           uint32_t index)
{
    encoder->setBuffer(buffer, offset, index);
}
ANGLE_INLINE void SetComputeOrVertexBufferForWrite(ComputeCommandEncoder *encoder,
                                                   const BufferRef &buffer,
                                                   size_t offset,
                                                   uint32_t index)
{
    encoder->setBufferForWrite(buffer, offset, index);
}

template <typename T>
ANGLE_INLINE void SetComputeOrVertexData(RenderCommandEncoder *encoder,
                                         const T &data,
                                         uint32_t index)
{
    encoder->setData(gl::ShaderType::Vertex, data, index);
}
template <typename T>
ANGLE_INLINE void SetComputeOrVertexData(ComputeCommandEncoder *encoder,
                                         const T &data,
                                         uint32_t index)
{
    encoder->setData(data, index);
}

ANGLE_INLINE void SetPipelineState(RenderCommandEncoder *encoder,
                                   id<MTLRenderPipelineState> pipeline)
{
    encoder->setRenderPipelineState(pipeline);
}
ANGLE_INLINE void SetPipelineState(ComputeCommandEncoder *encoder,
                                   id<MTLComputePipelineState> pipeline)
{
    encoder->setComputePipelineState(pipeline);
}

}  // namespace

NormalizedCoords::NormalizedCoords() : v{0.0f, 0.0f, 1.0f, 1.0f} {}

NormalizedCoords::NormalizedCoords(float x,
                                   float y,
                                   float width,
                                   float height,
                                   const gl::Rectangle &rect)
    : v{
          x / rect.width,
          y / rect.height,
          (x + width) / rect.width,
          (y + height) / rect.height,
      }
{}

NormalizedCoords::NormalizedCoords(const gl::Rectangle &rect, const gl::Extents &extents)
    : v{
          static_cast<float>(rect.x0()) / extents.width,
          static_cast<float>(rect.y0()) / extents.height,
          static_cast<float>(rect.x1()) / extents.width,
          static_cast<float>(rect.y1()) / extents.height,
      }
{}

// StencilBlitViaBufferParams implementation
StencilBlitViaBufferParams::StencilBlitViaBufferParams() {}

StencilBlitViaBufferParams::StencilBlitViaBufferParams(const DepthStencilBlitParams &srcParams)
{
    dstTextureSize      = srcParams.dstTextureSize;
    dstRect             = srcParams.dstRect;
    dstScissorRect      = srcParams.dstScissorRect;
    dstFlipY            = srcParams.dstFlipY;
    dstFlipX            = srcParams.dstFlipX;
    srcNormalizedCoords = srcParams.srcNormalizedCoords;
    srcYFlipped         = srcParams.srcYFlipped;
    unpackFlipX         = srcParams.unpackFlipX;
    unpackFlipY         = srcParams.unpackFlipY;

    srcStencil = srcParams.srcStencil;
}

// RenderUtils implementation
RenderUtils::RenderUtils()
    : mClearUtils{ClearUtils("clearIntFS"), ClearUtils("clearUIntFS"), ClearUtils("clearFloatFS")},
      mColorBlitUtils{ColorBlitUtils("blitIntFS"), ColorBlitUtils("blitUIntFS"),
                      ColorBlitUtils("blitFloatFS")},
      mCopyTextureFloatToUIntUtils("copyTextureFloatToUIntFS"),
      mCopyPixelsUtils{
          CopyPixelsUtils("readFromBufferToIntTextureFS", "writeFromIntTextureToBufferCS"),
          CopyPixelsUtils("readFromBufferToUIntTextureFS", "writeFromUIntTextureToBufferCS"),
          CopyPixelsUtils("readFromBufferToFloatTextureFS", "writeFromFloatTextureToBufferCS")},
      mPixelUnpackSupportedFormats(std::begin(kSupportedPixelUnpackFormats),
                                   std::end(kSupportedPixelUnpackFormats))
{}

// Clear current framebuffer
angle::Result RenderUtils::clearWithDraw(const gl::Context *context,
                                         RenderCommandEncoder *cmdEncoder,
                                         const ClearRectParams &params)
{
    int index = 0;
    if (params.clearColor.valid())
    {
        index = static_cast<int>(params.clearColor.value().getType());
    }
    else if (params.colorFormat)
    {
        index = GetPixelTypeIndex(params.colorFormat->actualAngleFormat());
    }
    return mClearUtils[index].clearWithDraw(context, cmdEncoder, params);
}

// Blit texture data to current framebuffer
angle::Result RenderUtils::blitColorWithDraw(const gl::Context *context,
                                             RenderCommandEncoder *cmdEncoder,
                                             const angle::Format &srcAngleFormat,
                                             const ColorBlitParams &params)
{
    int index = GetPixelTypeIndex(srcAngleFormat);
    return mColorBlitUtils[index].blitColorWithDraw(context, cmdEncoder, params);
}

angle::Result RenderUtils::blitColorWithDraw(const gl::Context *context,
                                             RenderCommandEncoder *cmdEncoder,
                                             const angle::Format &srcAngleFormat,
                                             const TextureRef &srcTexture)
{
    if (!srcTexture)
    {
        return angle::Result::Continue;
    }
    ColorBlitParams params;
    params.enabledBuffers.set(0);
    params.src            = srcTexture;
    params.dstTextureSize = gl::Extents(static_cast<int>(srcTexture->widthAt0()),
                                        static_cast<int>(srcTexture->heightAt0()),
                                        static_cast<int>(srcTexture->depthAt0()));
    params.dstRect        = params.dstScissorRect =
        gl::Rectangle(0, 0, params.dstTextureSize.width, params.dstTextureSize.height);
    params.srcNormalizedCoords = NormalizedCoords();

    return blitColorWithDraw(context, cmdEncoder, srcAngleFormat, params);
}

angle::Result RenderUtils::copyTextureWithDraw(const gl::Context *context,
                                               RenderCommandEncoder *cmdEncoder,
                                               const angle::Format &srcAngleFormat,
                                               const angle::Format &dstAngleFormat,
                                               const ColorBlitParams &params)
{
    if (!srcAngleFormat.isInt() && dstAngleFormat.isUint())
    {
        return mCopyTextureFloatToUIntUtils.blitColorWithDraw(context, cmdEncoder, params);
    }
    ASSERT(srcAngleFormat.isSint() == dstAngleFormat.isSint() &&
           srcAngleFormat.isUint() == dstAngleFormat.isUint());
    return blitColorWithDraw(context, cmdEncoder, srcAngleFormat, params);
}

angle::Result RenderUtils::blitDepthStencilWithDraw(const gl::Context *context,
                                                    RenderCommandEncoder *cmdEncoder,
                                                    const DepthStencilBlitParams &params)
{
    return mDepthStencilBlitUtils.blitDepthStencilWithDraw(context, cmdEncoder, params);
}

angle::Result RenderUtils::blitStencilViaCopyBuffer(const gl::Context *context,
                                                    const StencilBlitViaBufferParams &params)
{
    return mDepthStencilBlitUtils.blitStencilViaCopyBuffer(context, params);
}

angle::Result RenderUtils::convertIndexBufferGPU(ContextMtl *contextMtl,
                                                 gl::DrawElementsType srcType,
                                                 uint32_t indexCount,
                                                 const BufferSlice &srcBuffer,
                                                 const BufferSlice &dstBuffer,
                                                 bool primitiveRestartEnabled)
{
    return mIndexUtils.convertIndexBufferGPU(contextMtl, srcType, indexCount, srcBuffer, dstBuffer,
                                             primitiveRestartEnabled);
}
angle::Result RenderUtils::generateTriFanBufferFromArrays(
    ContextMtl *contextMtl,
    const TriFanOrLineLoopFromArrayParams &params)
{
    return mIndexUtils.generateTriFanBufferFromArrays(contextMtl, params);
}
angle::Result RenderUtils::generateTriFanBufferFromElementsArray(
    ContextMtl *contextMtl,
    const IndexGenerationParams &params,
    uint32_t *indicesGenerated)
{
    return mIndexUtils.generateTriFanBufferFromElementsArray(contextMtl, params, indicesGenerated);
}

angle::Result RenderUtils::generateLineLoopBufferFromArrays(
    ContextMtl *contextMtl,
    const TriFanOrLineLoopFromArrayParams &params)
{
    return mIndexUtils.generateLineLoopBufferFromArrays(contextMtl, params);
}
angle::Result RenderUtils::generateLineLoopLastSegment(ContextMtl *contextMtl,
                                                       uint32_t firstVertex,
                                                       uint32_t lastVertex,
                                                       const BufferRef &dstBuffer,
                                                       uint32_t dstOffset)
{
    return mIndexUtils.generateLineLoopLastSegment(contextMtl, firstVertex, lastVertex, dstBuffer,
                                                   dstOffset);
}
angle::Result RenderUtils::generateLineLoopBufferFromElementsArray(
    ContextMtl *contextMtl,
    const IndexGenerationParams &params,
    uint32_t *indicesGenerated)
{
    return mIndexUtils.generateLineLoopBufferFromElementsArray(contextMtl, params,
                                                               indicesGenerated);
}
angle::Result RenderUtils::generateLineLoopLastSegmentFromElementsArray(
    ContextMtl *contextMtl,
    const IndexGenerationParams &params)
{
    return mIndexUtils.generateLineLoopLastSegmentFromElementsArray(contextMtl, params);
}

void RenderUtils::combineVisibilityResult(ContextMtl *contextMtl,
                                          bool keepOldValue,
                                          size_t startOffset,
                                          size_t numOffsets,
                                          const BufferRef &renderPassResultBuf,
                                          const BufferRef &finalResultBuf)
{
    // TODO(geofflang): Propagate this error. It spreads to adding angle::Result return values in
    // most of the metal backend's files.
    (void)mVisibilityResultUtils.combineVisibilityResult(
        contextMtl, keepOldValue, startOffset, numOffsets, renderPassResultBuf, finalResultBuf);
}

// Compute based mipmap generation
angle::Result RenderUtils::generateMipmapCS(ContextMtl *contextMtl,
                                            const TextureRef &srcTexture,
                                            bool sRGBMipmap,
                                            NativeTexLevelArray *mipmapOutputViews)
{
    return mMipmapUtils.generateMipmapCS(contextMtl, srcTexture, sRGBMipmap, mipmapOutputViews);
}

bool RenderUtils::isPixelsUnpackSupported(const angle::Format &format) const
{
    return mPixelUnpackSupportedFormats.count(format.id) != 0;
}

angle::Result RenderUtils::unpackPixelsWithDraw(const gl::Context *context,
                                                const angle::Format &srcAngleFormat,
                                                const CopyPixelsFromBufferParams &params)
{
    ASSERT(isPixelsUnpackSupported(srcAngleFormat));
    int index = GetPixelTypeIndex(srcAngleFormat);
    return mCopyPixelsUtils[index].unpackPixelsWithDraw(context, srcAngleFormat, params);
}
angle::Result RenderUtils::packPixelsCS(ContextMtl *contextMtl,
                                        const angle::Format &dstAngleFormat,
                                        const CopyPixelsToBufferParams &params)
{
    int index = GetPixelTypeIndex(dstAngleFormat);
    return mCopyPixelsUtils[index].packPixelsCS(contextMtl, dstAngleFormat, params);
}

angle::Result RenderUtils::convertVertexFormatToFloatCS(ContextMtl *contextMtl,
                                                        const angle::Format &srcAngleFormat,
                                                        const VertexFormatConvertParams &params)
{
    return mVertexFormatUtils.convertVertexFormatToFloatCS(contextMtl, srcAngleFormat, params);
}

angle::Result RenderUtils::convertVertexFormatToFloatVS(const gl::Context *context,
                                                        RenderCommandEncoder *encoder,
                                                        const angle::Format &srcAngleFormat,
                                                        const VertexFormatConvertParams &params)
{
    return mVertexFormatUtils.convertVertexFormatToFloatVS(context, encoder, srcAngleFormat,
                                                           params);
}

// Expand number of components per vertex's attribute
angle::Result RenderUtils::expandVertexFormatComponentsCS(ContextMtl *contextMtl,
                                                          const angle::Format &srcAngleFormat,
                                                          const VertexFormatConvertParams &params)
{
    return mVertexFormatUtils.expandVertexFormatComponentsCS(contextMtl, srcAngleFormat, params);
}

angle::Result RenderUtils::expandVertexFormatComponentsVS(const gl::Context *context,
                                                          RenderCommandEncoder *encoder,
                                                          const angle::Format &srcAngleFormat,
                                                          const VertexFormatConvertParams &params)
{
    return mVertexFormatUtils.expandVertexFormatComponentsVS(context, encoder, srcAngleFormat,
                                                             params);
}

angle::Result RenderUtils::linearizeBlocks(ContextMtl *contextMtl,
                                           const BlockLinearizationParams &params)
{
    return mBlockLinearizationUtils.linearizeBlocks(contextMtl, params);
}

angle::Result RenderUtils::saturateDepth(ContextMtl *contextMtl,
                                         const DepthSaturationParams &params)
{
    return mDepthSaturationUtils.saturateDepth(contextMtl, params);
}

// ClearUtils implementation
ClearUtils::ClearUtils(const std::string &fragmentShaderName)
    : mFragmentShaderName(fragmentShaderName)
{}

angle::Result ClearUtils::ensureShadersInitialized(ContextMtl *ctx, uint32_t numOutputs)
{
    ANGLE_MTL_OBJC_SCOPE
    {
        if (!mVertexShader)
        {
            id<MTLLibrary> shaderLib = ctx->getDisplay()->getDefaultShadersLib();
            mVertexShader = angle::adoptObjCPtr([shaderLib newFunctionWithName:@"clearVS"]);
            ANGLE_CHECK(ctx, mVertexShader, gl::err::kInternalError, GL_INVALID_OPERATION);
        }

        if (!mFragmentShaders[numOutputs])
        {
            NSError *err             = nil;
            id<MTLLibrary> shaderLib = ctx->getDisplay()->getDefaultShadersLib();
            angle::ObjCPtr<MTLFunctionConstantValues> funcConstants =
                angle::adoptObjCPtr([[MTLFunctionConstantValues alloc] init]);

            // Create clear shader for each number of color outputs.
            // So clear k color outputs will use mFragmentShaders[k] for example:
            [funcConstants setConstantValue:&numOutputs
                                       type:MTLDataTypeUInt
                                   withName:NUM_COLOR_OUTPUTS_CONSTANT_NAME];

            mFragmentShaders[numOutputs] = angle::adoptObjCPtr([shaderLib
                newFunctionWithName:[NSString stringWithUTF8String:mFragmentShaderName.c_str()]
                     constantValues:funcConstants
                              error:&err]);
            ANGLE_MTL_CHECK(ctx, mFragmentShaders[numOutputs], err);
        }
        return angle::Result::Continue;
    }
}

id<MTLDepthStencilState> ClearUtils::getClearDepthStencilState(const gl::Context *context,
                                                               const ClearRectParams &params)
{
    ContextMtl *contextMtl = GetImpl(context);

    if (!params.clearDepth.valid() && !params.clearStencil.valid())
    {
        // Doesn't clear depth nor stencil
        return contextMtl->getDisplay()->getStateCache().getNullDepthStencilState(
            contextMtl->getMetalDevice());
    }

    DepthStencilDesc desc(MTLCompareFunctionAlways, params.clearDepth.valid());
    if (params.clearStencil.valid())
    {
        // Clear stencil state
        desc.frontFaceStencil.setDepthStencilPassOperation(MTLStencilOperationReplace);
        desc.frontFaceStencil.writeMask                 = contextMtl->getStencilMask();
        desc.backFaceStencil.setDepthStencilPassOperation(MTLStencilOperationReplace);
        desc.backFaceStencil.writeMask                  = contextMtl->getStencilMask();
    }

    return contextMtl->getDisplay()->getStateCache().getDepthStencilState(
        contextMtl->getMetalDevice(), desc);
}

angle::Result ClearUtils::getClearRenderPipelineState(
    const gl::Context *context,
    RenderCommandEncoder *cmdEncoder,
    const ClearRectParams &params,
    angle::ObjCPtr<id<MTLRenderPipelineState>> *outPipelineState)
{
    ContextMtl *contextMtl = GetImpl(context);
    // The color mask to be applied to every color attachment:
    WriteMaskArray clearWriteMaskArray = params.clearWriteMaskArray;
    if (!params.clearColor.valid())
    {
        clearWriteMaskArray.fill(MTLColorWriteMaskNone);
    }
    else
    {
        // Adjust masks for disabled outputs before creating a pipeline.
        gl::DrawBufferMask disabledBuffers(params.enabledBuffers);
        for (size_t index : disabledBuffers.flip())
        {
            clearWriteMaskArray[index] = MTLColorWriteMaskNone;
        }
    }

    RenderPipelineDesc pipelineDesc;
    const RenderPassDesc &renderPassDesc = cmdEncoder->renderPassDesc();

    renderPassDesc.populateRenderPipelineOutputDesc(clearWriteMaskArray,
                                                    &pipelineDesc.outputDescriptor);

    pipelineDesc.setInputPrimitiveTopology(MTLPrimitiveTopologyClassTriangle);

    ANGLE_TRY(ensureShadersInitialized(contextMtl, renderPassDesc.numColorAttachments));

    return contextMtl->getPipelineCache().getRenderPipeline(
        contextMtl, mVertexShader, mFragmentShaders[renderPassDesc.numColorAttachments],
        pipelineDesc, outPipelineState);
}

angle::Result ClearUtils::setupClearWithDraw(const gl::Context *context,
                                             RenderCommandEncoder *cmdEncoder,
                                             const ClearRectParams &params)
{
    // Generate render pipeline state
    angle::ObjCPtr<id<MTLRenderPipelineState>> renderPipelineState;
    ANGLE_TRY(getClearRenderPipelineState(context, cmdEncoder, params, &renderPipelineState));

    // Setup states
    SetupFullscreenQuadDrawCommonStates(cmdEncoder);
    cmdEncoder->setRenderPipelineState(renderPipelineState);

    id<MTLDepthStencilState> dsState = getClearDepthStencilState(context, params);
    cmdEncoder->setDepthStencilState(dsState).setStencilRefVal(params.clearStencil.value());

    // Viewports
    MTLViewport viewport;
    MTLScissorRect scissorRect;

    viewport = GetViewport(params.clearArea, params.dstTextureSize.height, params.flipY);

    scissorRect = GetScissorRect(params.clearArea, params.dstTextureSize.height, params.flipY);

    cmdEncoder->setViewport(viewport);
    cmdEncoder->setScissorRect(scissorRect);

    // uniform
    ClearParamsUniform uniformParams;
    const ClearColorValue &clearValue = params.clearColor.value();
    // ClearColorValue is an int, uint, float union so it's safe to use only floats.
    // The Shader will do the bit cast based on appropriate format type.
    // See shaders/clear.metal (3 variants ClearFloatFS, ClearIntFS and ClearUIntFS each does the
    // appropriate bit cast)
    ASSERT(sizeof(uniformParams.clearColor) == clearValue.getValueBytes().size());
    ANGLE_UNSAFE_TODO(std::memcpy(uniformParams.clearColor, clearValue.getValueBytes().data(),
                                  clearValue.getValueBytes().size()));
    uniformParams.clearDepth = params.clearDepth.value();

    cmdEncoder->setVertexData(uniformParams, 0);
    cmdEncoder->setFragmentData(uniformParams, 0);
    return angle::Result::Continue;
}

angle::Result ClearUtils::clearWithDraw(const gl::Context *context,
                                        RenderCommandEncoder *cmdEncoder,
                                        const ClearRectParams &params)
{
    ClearRectParams overridedParams = params;
    // Make sure we don't clear attachment that doesn't exist
    const RenderPassDesc &renderPassDesc = cmdEncoder->renderPassDesc();
    if (renderPassDesc.numColorAttachments == 0)
    {
        overridedParams.clearColor.reset();
    }
    if (!renderPassDesc.depthAttachment.texture)
    {
        overridedParams.clearDepth.reset();
    }
    if (!renderPassDesc.stencilAttachment.texture)
    {
        overridedParams.clearStencil.reset();
    }

    if (!overridedParams.clearColor.valid() && !overridedParams.clearDepth.valid() &&
        !overridedParams.clearStencil.valid())
    {
        return angle::Result::Continue;
    }
    ContextMtl *contextMtl = GetImpl(context);
    ANGLE_TRY(setupClearWithDraw(context, cmdEncoder, overridedParams));

    {
        // Need to disable occlusion query, otherwise clearing will affect the occlusion counting
        ScopedDisableOcclusionQuery disableOcclusionQuery(contextMtl, cmdEncoder);
        // Draw the screen aligned triangle
        cmdEncoder->draw(MTLPrimitiveTypeTriangle, 0, 3);
    }

    // Invalidate current context's state
    contextMtl->invalidateState(context);

    return angle::Result::Continue;
}

// ColorBlitUtils implementation
ColorBlitUtils::ColorBlitUtils(const std::string &fragmentShaderName)
    : mFragmentShaderName(fragmentShaderName)
{}

angle::Result ColorBlitUtils::ensureShadersInitialized(
    ContextMtl *ctx,
    const ShaderKey &key,
    angle::ObjCPtr<id<MTLFunction>> *fragmentShaderOut)
{
    ANGLE_MTL_OBJC_SCOPE
    {
        if (!mVertexShader)
        {
            id<MTLLibrary> shaderLib = ctx->getDisplay()->getDefaultShadersLib();
            mVertexShader = angle::adoptObjCPtr([shaderLib newFunctionWithName:@"blitVS"]);
            ANGLE_CHECK(ctx, mVertexShader, gl::err::kInternalError, GL_INVALID_OPERATION);
        }

        if (!(*fragmentShaderOut))
        {
            NSError *err             = nil;
            id<MTLLibrary> shaderLib = ctx->getDisplay()->getDefaultShadersLib();
            angle::ObjCPtr<MTLFunctionConstantValues> funcConstants =
                angle::adoptObjCPtr([[MTLFunctionConstantValues alloc] init]);

            bool unmultiplyAlpha       = key.unmultiplyAlpha;
            bool premultiplyAlpha      = key.premultiplyAlpha;
            bool transformLinearToSrgb = key.transformLinearToSrgb;
            // Set alpha multiply flags
            [funcConstants setConstantValue:&unmultiplyAlpha
                                       type:MTLDataTypeBool
                                   withName:UNMULTIPLY_ALPHA_CONSTANT_NAME];
            [funcConstants setConstantValue:&premultiplyAlpha
                                       type:MTLDataTypeBool
                                   withName:PREMULTIPLY_ALPHA_CONSTANT_NAME];
            [funcConstants setConstantValue:&transformLinearToSrgb
                                       type:MTLDataTypeBool
                                   withName:TRANSFORM_LINEAR_TO_SRGB_CONSTANT_NAME];

            uint32_t numColorAttachments = key.numColorAttachments;
            // We create blit shader pipeline cache for each number of color outputs.
            // So blit k color outputs will use mBlitRenderPipelineCache[k-1] for example:
            [funcConstants setConstantValue:&numColorAttachments
                                       type:MTLDataTypeUInt
                                   withName:NUM_COLOR_OUTPUTS_CONSTANT_NAME];

            // Set texture type constant
            [funcConstants setConstantValue:&key.sourceTextureType
                                       type:MTLDataTypeInt
                                   withName:SOURCE_TEXTURE_TYPE_CONSTANT_NAME];

            *fragmentShaderOut = angle::adoptObjCPtr([shaderLib
                newFunctionWithName:[NSString stringWithUTF8String:mFragmentShaderName.c_str()]
                     constantValues:funcConstants
                              error:&err]);
            ANGLE_MTL_CHECK(ctx, *fragmentShaderOut, err);
        }
        return angle::Result::Continue;
    }
}

angle::Result ColorBlitUtils::getColorBlitRenderPipelineState(
    const gl::Context *context,
    RenderCommandEncoder *cmdEncoder,
    const ColorBlitParams &params,
    angle::ObjCPtr<id<MTLRenderPipelineState>> *outPipelineState)
{
    ContextMtl *contextMtl = GetImpl(context);
    RenderPipelineDesc pipelineDesc;
    const RenderPassDesc &renderPassDesc = cmdEncoder->renderPassDesc();

    renderPassDesc.populateRenderPipelineOutputDesc(&pipelineDesc.outputDescriptor);

    // Disable blit for some outputs that are not enabled
    pipelineDesc.outputDescriptor.updateEnabledDrawBuffers(params.enabledBuffers);

    pipelineDesc.setInputPrimitiveTopology(MTLPrimitiveTopologyClassTriangle);

    ShaderKey key(GetShaderTextureType(params.src), renderPassDesc.numColorAttachments,
                  params.unpackUnmultiplyAlpha, params.unpackPremultiplyAlpha,
                  params.transformLinearToSrgb);

    angle::ObjCPtr<id<MTLFunction>> *fragmentShader = &mBlitFragmentShaders[key];
    ANGLE_TRY(ensureShadersInitialized(contextMtl, key, fragmentShader));

    return contextMtl->getPipelineCache().getRenderPipeline(
        contextMtl, mVertexShader, *fragmentShader, pipelineDesc, outPipelineState);
}

angle::Result ColorBlitUtils::setupColorBlitWithDraw(const gl::Context *context,
                                                     RenderCommandEncoder *cmdEncoder,
                                                     const ColorBlitParams &params)
{
    ASSERT(cmdEncoder->renderPassDesc().numColorAttachments >= 1 && params.src);

    ContextMtl *contextMtl = mtl::GetImpl(context);

    // Generate render pipeline state
    angle::ObjCPtr<id<MTLRenderPipelineState>> renderPipelineState;
    ANGLE_TRY(getColorBlitRenderPipelineState(context, cmdEncoder, params, &renderPipelineState));

    // Setup states
    cmdEncoder->setRenderPipelineState(renderPipelineState);
    cmdEncoder->setDepthStencilState(
        contextMtl->getDisplay()->getStateCache().getNullDepthStencilState(
            contextMtl->getMetalDevice()));

    SetupCommonBlitWithDrawStates(context, cmdEncoder, params, true);

    // Set sampler state
    SamplerDesc samplerDesc;
    samplerDesc.setMinFilter(GetFilter(params.filter));
    samplerDesc.setMagFilter(GetFilter(params.filter));

    cmdEncoder->setFragmentSamplerState(contextMtl->getDisplay()->getStateCache().getSamplerState(
                                            contextMtl->getMetalDevice(), samplerDesc),
                                        0, FLT_MAX, 0);
    return angle::Result::Continue;
}

angle::Result ColorBlitUtils::blitColorWithDraw(const gl::Context *context,
                                                RenderCommandEncoder *cmdEncoder,
                                                const ColorBlitParams &params)
{
    if (!params.src)
    {
        return angle::Result::Continue;
    }
    ContextMtl *contextMtl = GetImpl(context);
    ANGLE_TRY(setupColorBlitWithDraw(context, cmdEncoder, params));

    {
        // Need to disable occlusion query, otherwise blitting will affect the occlusion counting
        ScopedDisableOcclusionQuery disableOcclusionQuery(contextMtl, cmdEncoder);
        // Draw the screen aligned quad
        cmdEncoder->draw(MTLPrimitiveTypeTriangleStrip, 0, 4);
    }

    // Invalidate current context's state
    contextMtl->invalidateState(context);

    return angle::Result::Continue;
}

angle::Result DepthStencilBlitUtils::ensureShadersInitialized(
    ContextMtl *ctx,
    int sourceDepthTextureType,
    int sourceStencilTextureType,
    angle::ObjCPtr<id<MTLFunction>> *fragmentShaderOut)
{

    ANGLE_MTL_OBJC_SCOPE
    {
        if (!mVertexShader)
        {
            id<MTLLibrary> shaderLib = ctx->getDisplay()->getDefaultShadersLib();
            mVertexShader = angle::adoptObjCPtr([shaderLib newFunctionWithName:@"blitVS"]);
            ANGLE_CHECK(ctx, mVertexShader, gl::err::kInternalError, GL_INVALID_OPERATION);
        }

        if (!(*fragmentShaderOut))
        {
            NSError *err             = nil;
            id<MTLLibrary> shaderLib = ctx->getDisplay()->getDefaultShadersLib();
            angle::ObjCPtr<MTLFunctionConstantValues> funcConstants =
                angle::adoptObjCPtr([[MTLFunctionConstantValues alloc] init]);
            NSString *shaderName;
            if (sourceDepthTextureType != -1 && sourceStencilTextureType != -1)
            {
                shaderName = @"blitDepthStencilFS";
            }
            else if (sourceDepthTextureType != -1)
            {
                shaderName = @"blitDepthFS";
            }
            else
            {
                shaderName = @"blitStencilFS";
            }

            if (sourceDepthTextureType != -1)
            {
                [funcConstants setConstantValue:&sourceDepthTextureType
                                           type:MTLDataTypeInt
                                       withName:SOURCE_TEXTURE_TYPE_CONSTANT_NAME];
            }
            if (sourceStencilTextureType != -1)
            {

                [funcConstants setConstantValue:&sourceStencilTextureType
                                           type:MTLDataTypeInt
                                       withName:SOURCE_TEXTURE2_TYPE_CONSTANT_NAME];
            }

            *fragmentShaderOut = angle::adoptObjCPtr([shaderLib newFunctionWithName:shaderName
                                                                     constantValues:funcConstants
                                                                              error:&err]);
            ANGLE_MTL_CHECK(ctx, *fragmentShaderOut, err);
        }

        return angle::Result::Continue;
    }
}

angle::Result DepthStencilBlitUtils::getStencilToBufferComputePipelineState(
    ContextMtl *contextMtl,
    const StencilBlitViaBufferParams &params,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outComputePipelineState)
{
    int sourceStencilTextureType = GetShaderTextureType(params.srcStencil);
    angle::ObjCPtr<id<MTLFunction>> &shader =
        mStencilBlitToBufferComputeShaders[sourceStencilTextureType];
    if (!shader)
    {
        ANGLE_MTL_OBJC_SCOPE
        {
            auto shaderLib = contextMtl->getDisplay()->getDefaultShadersLib();
            NSError *err   = nil;
            angle::ObjCPtr<MTLFunctionConstantValues> funcConstants =
                angle::adoptObjCPtr([[MTLFunctionConstantValues alloc] init]);

            [funcConstants setConstantValue:&sourceStencilTextureType
                                       type:MTLDataTypeInt
                                   withName:SOURCE_TEXTURE2_TYPE_CONSTANT_NAME];

            shader = angle::adoptObjCPtr([shaderLib newFunctionWithName:@"blitStencilToBufferCS"
                                                         constantValues:funcConstants
                                                                  error:&err]);
            ANGLE_MTL_CHECK(contextMtl, shader, err);
        }
    }

    return contextMtl->getPipelineCache().getComputePipeline(contextMtl, shader,
                                                             outComputePipelineState);
}

angle::Result DepthStencilBlitUtils::getDepthStencilBlitRenderPipelineState(
    const gl::Context *context,
    RenderCommandEncoder *cmdEncoder,
    const DepthStencilBlitParams &params,
    angle::ObjCPtr<id<MTLRenderPipelineState>> *outRenderPipelineState)
{
    ContextMtl *contextMtl = GetImpl(context);
    RenderPipelineDesc pipelineDesc;
    const RenderPassDesc &renderPassDesc = cmdEncoder->renderPassDesc();

    renderPassDesc.populateRenderPipelineOutputDesc(&pipelineDesc.outputDescriptor);

    // Disable all color outputs
    pipelineDesc.outputDescriptor.updateEnabledDrawBuffers(gl::DrawBufferMask());

    pipelineDesc.setInputPrimitiveTopology(MTLPrimitiveTopologyClassTriangle);

    angle::ObjCPtr<id<MTLFunction>> *fragmentShader = nullptr;
    int depthTextureType                            = GetShaderTextureType(params.src);
    int stencilTextureType                          = GetShaderTextureType(params.srcStencil);
    if (params.src && params.srcStencil)
    {
        fragmentShader = &mDepthStencilBlitFragmentShaders[depthTextureType][stencilTextureType];
    }
    else if (params.src)
    {
        // Only depth blit
        fragmentShader = &mDepthBlitFragmentShaders[depthTextureType];
    }
    else
    {
        // Only stencil blit
        fragmentShader = &mStencilBlitFragmentShaders[stencilTextureType];
    }

    ANGLE_TRY(
        ensureShadersInitialized(contextMtl, depthTextureType, stencilTextureType, fragmentShader));

    return contextMtl->getPipelineCache().getRenderPipeline(
        contextMtl, mVertexShader, *fragmentShader, pipelineDesc, outRenderPipelineState);
}

angle::Result DepthStencilBlitUtils::setupDepthStencilBlitWithDraw(
    const gl::Context *context,
    RenderCommandEncoder *cmdEncoder,
    const DepthStencilBlitParams &params)
{
    ContextMtl *contextMtl = mtl::GetImpl(context);

    ASSERT(params.src || params.srcStencil);

    SetupCommonBlitWithDrawStates(context, cmdEncoder, params, false);

    // Generate render pipeline state
    angle::ObjCPtr<id<MTLRenderPipelineState>> renderPipelineState;
    ANGLE_TRY(
        getDepthStencilBlitRenderPipelineState(context, cmdEncoder, params, &renderPipelineState));

    // Setup states
    cmdEncoder->setRenderPipelineState(renderPipelineState);

    // Depth stencil state
    mtl::DepthStencilDesc dsStateDesc(MTLCompareFunctionAlways,
                                      /*depthWriteEnabled*/ params.src != nullptr);

    if (params.srcStencil)
    {
        cmdEncoder->setFragmentTexture(params.srcStencil, 1);

        if (!contextMtl->getDisplay()->getFeatures().hasShaderStencilOutput.enabled)
        {
            // Hardware must support stencil writing directly in shader.
            UNREACHABLE();
        }
        // Enable stencil write to framebuffer
        dsStateDesc.frontFaceStencil.setStencilCompareFunction(MTLCompareFunctionAlways);
        dsStateDesc.backFaceStencil.setStencilCompareFunction(MTLCompareFunctionAlways);

        dsStateDesc.frontFaceStencil.setDepthStencilPassOperation(MTLStencilOperationReplace);
        dsStateDesc.backFaceStencil.setDepthStencilPassOperation(MTLStencilOperationReplace);

        dsStateDesc.frontFaceStencil.writeMask = kStencilMaskAll;
        dsStateDesc.backFaceStencil.writeMask  = kStencilMaskAll;
    }

    cmdEncoder->setDepthStencilState(contextMtl->getDisplay()->getStateCache().getDepthStencilState(
        contextMtl->getMetalDevice(), dsStateDesc));
    return angle::Result::Continue;
}

angle::Result DepthStencilBlitUtils::blitDepthStencilWithDraw(const gl::Context *context,
                                                              RenderCommandEncoder *cmdEncoder,
                                                              const DepthStencilBlitParams &params)
{
    if (!params.src && !params.srcStencil)
    {
        return angle::Result::Continue;
    }
    ContextMtl *contextMtl = GetImpl(context);

    ANGLE_TRY(setupDepthStencilBlitWithDraw(context, cmdEncoder, params));

    {
        // Need to disable occlusion query, otherwise blitting will affect the occlusion counting
        ScopedDisableOcclusionQuery disableOcclusionQuery(contextMtl, cmdEncoder);
        // Draw the screen aligned quad
        cmdEncoder->draw(MTLPrimitiveTypeTriangleStrip, 0, 4);
    }

    // Invalidate current context's state
    contextMtl->invalidateState(context);

    return angle::Result::Continue;
}

angle::Result DepthStencilBlitUtils::blitStencilViaCopyBuffer(
    const gl::Context *context,
    const StencilBlitViaBufferParams &params)
{
    // Depth texture must be omitted.
    ASSERT(!params.src);
    if (!params.srcStencil || !params.dstStencil)
    {
        return angle::Result::Continue;
    }
    ContextMtl *contextMtl = GetImpl(context);

    // Create intermediate buffer.
    uint32_t bufferRequiredRowPitch =
        static_cast<uint32_t>(params.dstRect.width) * params.dstStencil->samples();
    uint32_t bufferRequiredSize =
        bufferRequiredRowPitch * static_cast<uint32_t>(params.dstRect.height);
    if (!mStencilCopyBuffer || mStencilCopyBuffer->size() < bufferRequiredSize)
    {
        ANGLE_TRY(Buffer::MakeBuffer(contextMtl, bufferRequiredSize, &mStencilCopyBuffer));
    }

    // Copy stencil data to buffer via compute shader. We cannot use blit command since blit command
    // doesn't support multisample resolve and scaling.
    ComputeCommandEncoder *cmdEncoder = contextMtl->getComputeCommandEncoder();
    ASSERT(cmdEncoder);

    angle::ObjCPtr<id<MTLComputePipelineState>> pipeline;
    ANGLE_TRY(getStencilToBufferComputePipelineState(contextMtl, params, &pipeline));

    cmdEncoder->setComputePipelineState(pipeline);

    float u0, v0, u1, v1;
    bool unpackFlipX = params.unpackFlipX;
    bool unpackFlipY = params.unpackFlipY;
    if (params.dstFlipX)
    {
        unpackFlipX = !unpackFlipX;
    }
    if (params.dstFlipY)
    {
        unpackFlipY = !unpackFlipY;
    }
    GetBlitTexCoords(params.srcNormalizedCoords, params.srcYFlipped, unpackFlipX, unpackFlipY, &u0,
                     &v0, &u1, &v1);

    BlitStencilToBufferParamsUniform uniform;
    uniform.srcTexCoordSteps[0]  = (u1 - u0) / params.dstRect.width;
    uniform.srcTexCoordSteps[1]  = (v1 - v0) / params.dstRect.height;
    uniform.srcStartTexCoords[0] = u0 + uniform.srcTexCoordSteps[0] * 0.5f;
    uniform.srcStartTexCoords[1] = v0 + uniform.srcTexCoordSteps[1] * 0.5f;
    uniform.srcLevel             = params.srcLevel.get();
    uniform.srcLayer             = params.srcLayer;
    uniform.dstSize[0]           = params.dstRect.width;
    uniform.dstSize[1]           = params.dstRect.height;
    uniform.dstBufferRowPitch    = bufferRequiredRowPitch;
    uniform.resolveMS            = params.dstStencil->samples() == 1;

    cmdEncoder->setTexture(params.srcStencil, 1);

    cmdEncoder->setData(uniform, 0);
    cmdEncoder->setBufferForWrite(mStencilCopyBuffer, 0, 1);

    NSUInteger w                  = pipeline.get().threadExecutionWidth;
    MTLSize threadsPerThreadgroup = MTLSizeMake(w, 1, 1);
    DispatchCompute(contextMtl, cmdEncoder, /** allowNonUniform */ true,
                    MTLSizeMake(params.dstRect.width, params.dstRect.height, 1),
                    threadsPerThreadgroup);

    // Copy buffer to real destination texture
    ASSERT(params.dstStencil->textureType() != MTLTextureType3D);

    mtl::BlitCommandEncoder *blitEncoder = contextMtl->getBlitCommandEncoder();

    // Only copy the scissored area of the buffer.
    MTLScissorRect viewportRectMtl =
        GetScissorRect(params.dstRect, params.dstTextureSize.height, params.dstFlipY);
    MTLScissorRect scissorRectMtl =
        GetScissorRect(params.dstScissorRect, params.dstTextureSize.height, params.dstFlipY);

    uint32_t dx = static_cast<uint32_t>(scissorRectMtl.x - viewportRectMtl.x);
    uint32_t dy = static_cast<uint32_t>(scissorRectMtl.y - viewportRectMtl.y);

    uint32_t bufferStartReadableOffset = dx + bufferRequiredRowPitch * dy;
    blitEncoder->copyBufferToTexture(
        mStencilCopyBuffer, bufferStartReadableOffset, bufferRequiredRowPitch, 0,
        MTLSizeMake(scissorRectMtl.width, scissorRectMtl.height, 1), params.dstStencil,
        params.dstStencilLayer, params.dstStencilLevel,
        MTLOriginMake(scissorRectMtl.x, scissorRectMtl.y, 0),
        params.dstPackedDepthStencilFormat ? MTLBlitOptionStencilFromDepthStencil
                                           : MTLBlitOptionNone);

    return angle::Result::Continue;
}

angle::Result IndexGeneratorUtils::getIndexConversionPipeline(
    ContextMtl *contextMtl,
    gl::DrawElementsType srcType,
    uint32_t srcOffset,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outComputePipeline)
{
    size_t elementSize                      = gl::GetDrawElementsTypeSize(srcType);
    BOOL aligned                            = (srcOffset % elementSize) == 0;
    int srcTypeKey                          = static_cast<int>(srcType);
    angle::ObjCPtr<id<MTLFunction>> &shader = mIndexConversionShaders[srcTypeKey][aligned ? 1 : 0];

    if (!shader)
    {
        ANGLE_MTL_OBJC_SCOPE
        {
            angle::ObjCPtr<MTLFunctionConstantValues> funcConstants =
                angle::adoptObjCPtr([[MTLFunctionConstantValues alloc] init]);

            [funcConstants setConstantValue:&aligned
                                       type:MTLDataTypeBool
                                   withName:SOURCE_BUFFER_ALIGNED_CONSTANT_NAME];

            NSString *shaderName = nil;
            switch (srcType)
            {
                case gl::DrawElementsType::UnsignedByte:
                    // No need for specialized shader
                    funcConstants = nil;
                    shaderName    = @"convertIndexU8ToU16";
                    break;
                case gl::DrawElementsType::UnsignedShort:
                    shaderName = @"convertIndexU16";
                    break;
                case gl::DrawElementsType::UnsignedInt:
                    shaderName = @"convertIndexU32";
                    break;
                default:
                    UNREACHABLE();
            }

            ANGLE_TRY(
                EnsureSpecializedShaderInitialized(contextMtl, shaderName, funcConstants, &shader));
        }
    }

    return contextMtl->getPipelineCache().getComputePipeline(contextMtl, shader,
                                                             outComputePipeline);
}

angle::Result IndexGeneratorUtils::getIndicesFromElemArrayGeneratorPipeline(
    ContextMtl *contextMtl,
    gl::DrawElementsType srcType,
    uint32_t srcOffset,
    NSString *shaderName,
    IndexConversionShaderArray *shaderArray,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outComputePipeline)
{
    size_t elementSize = gl::GetDrawElementsTypeSize(srcType);
    BOOL aligned       = (srcOffset % elementSize) == 0;
    int srcTypeKey     = static_cast<int>(srcType);

    angle::ObjCPtr<id<MTLFunction>> &shader = (*shaderArray)[srcTypeKey][aligned ? 1 : 0];

    if (!shader)
    {
        ANGLE_MTL_OBJC_SCOPE
        {
            angle::ObjCPtr<MTLFunctionConstantValues> funcConstants =
                angle::adoptObjCPtr([[MTLFunctionConstantValues alloc] init]);

            bool isU8  = false;
            bool isU16 = false;
            bool isU32 = false;

            switch (srcType)
            {
                case gl::DrawElementsType::UnsignedByte:
                    isU8 = true;
                    break;
                case gl::DrawElementsType::UnsignedShort:
                    isU16 = true;
                    break;
                case gl::DrawElementsType::UnsignedInt:
                    isU32 = true;
                    break;
                default:
                    UNREACHABLE();
            }

            [funcConstants setConstantValue:&aligned
                                       type:MTLDataTypeBool
                                   withName:SOURCE_BUFFER_ALIGNED_CONSTANT_NAME];
            [funcConstants setConstantValue:&isU8
                                       type:MTLDataTypeBool
                                   withName:SOURCE_IDX_IS_U8_CONSTANT_NAME];
            [funcConstants setConstantValue:&isU16
                                       type:MTLDataTypeBool
                                   withName:SOURCE_IDX_IS_U16_CONSTANT_NAME];
            [funcConstants setConstantValue:&isU32
                                       type:MTLDataTypeBool
                                   withName:SOURCE_IDX_IS_U32_CONSTANT_NAME];

            ANGLE_TRY(
                EnsureSpecializedShaderInitialized(contextMtl, shaderName, funcConstants, &shader));
        }
    }

    return contextMtl->getPipelineCache().getComputePipeline(contextMtl, shader,
                                                             outComputePipeline);
}

angle::Result IndexGeneratorUtils::getTriFanFromArrayGeneratorPipeline(
    ContextMtl *contextMtl,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outComputePipeline)
{
    ANGLE_TRY(EnsureShaderInitialized(contextMtl, @"genTriFanIndicesFromArray",
                                      &mTriFanFromArraysGeneratorShader));
    return contextMtl->getPipelineCache().getComputePipeline(
        contextMtl, mTriFanFromArraysGeneratorShader, outComputePipeline);
}

angle::Result IndexGeneratorUtils::getLineLoopFromArrayGeneratorPipeline(
    ContextMtl *contextMtl,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outComputePipeline)
{
    ANGLE_TRY(EnsureShaderInitialized(contextMtl, @"genLineLoopIndicesFromArray",
                                      &mLineLoopFromArraysGeneratorShader));
    return contextMtl->getPipelineCache().getComputePipeline(
        contextMtl, mLineLoopFromArraysGeneratorShader, outComputePipeline);
}

angle::Result IndexGeneratorUtils::convertIndexBufferGPU(ContextMtl *contextMtl,
                                                         gl::DrawElementsType srcType,
                                                         uint32_t indexCount,
                                                         const BufferSlice &srcBuffer,
                                                         const BufferSlice &dstBuffer,
                                                         bool primitiveRestartEnabled)
{
    ComputeCommandEncoder *cmdEncoder = contextMtl->getIndexPreprocessingCommandEncoder();
    ASSERT(cmdEncoder);
    uint32_t conversionSourceOffset =
        static_cast<uint32_t>(srcBuffer.offset() % kConstantBufferOffsetAlignment);

    angle::ObjCPtr<id<MTLComputePipelineState>> pipelineState;
    ANGLE_TRY(
        getIndexConversionPipeline(contextMtl, srcType, conversionSourceOffset, &pipelineState));

    ASSERT(pipelineState);

    cmdEncoder->setComputePipelineState(pipelineState);

    ASSERT((dstBuffer.offset() % kIndexBufferOffsetAlignment) == 0);

    IndexConversionUniform uniform;
    uniform.srcOffset               = conversionSourceOffset;
    uniform.indexCount              = indexCount;
    uniform.primitiveRestartEnabled = primitiveRestartEnabled;

    cmdEncoder->setData(uniform, 0);
    cmdEncoder->setBuffer(srcBuffer.buffer(), srcBuffer.offset() - conversionSourceOffset, 1);
    cmdEncoder->setBufferForWrite(dstBuffer.buffer(), dstBuffer.offset(), 2);

    DispatchCompute(contextMtl, cmdEncoder, pipelineState, indexCount);

    return angle::Result::Continue;
}

angle::Result IndexGeneratorUtils::generateTriFanBufferFromArrays(
    ContextMtl *contextMtl,
    const TriFanOrLineLoopFromArrayParams &params)
{
    ComputeCommandEncoder *cmdEncoder = contextMtl->getComputeCommandEncoder();
    ASSERT(cmdEncoder);

    angle::ObjCPtr<id<MTLComputePipelineState>> pipeline;
    ANGLE_TRY(getTriFanFromArrayGeneratorPipeline(contextMtl, &pipeline));

    ASSERT(params.vertexCount > 2);

    cmdEncoder->setComputePipelineState(pipeline);

    ASSERT((params.dstOffset % kIndexBufferOffsetAlignment) == 0);

    TriFanOrLineLoopArrayParams uniform;

    uniform.firstVertex = params.firstVertex;
    uniform.vertexCount = params.vertexCount - 2;

    cmdEncoder->setData(uniform, 0);
    cmdEncoder->setBufferForWrite(params.dstBuffer, params.dstOffset, 2);

    DispatchCompute(contextMtl, cmdEncoder, pipeline, uniform.vertexCount);

    return angle::Result::Continue;
}

angle::Result IndexGeneratorUtils::generateTriFanBufferFromElementsArray(
    ContextMtl *contextMtl,
    const IndexGenerationParams &params,
    uint32_t *indicesGenerated)
{
    const gl::VertexArray *vertexArray = contextMtl->getState().getVertexArray();
    const gl::Buffer *elementBuffer    = vertexArray->getElementArrayBuffer();
    if (elementBuffer)
    {
        BufferMtl *elementBufferMtl = GetImpl(elementBuffer);
        size_t srcOffset            = reinterpret_cast<size_t>(params.indices);
        ANGLE_CHECK(contextMtl, srcOffset <= std::numeric_limits<uint32_t>::max(),
                    "Index offset is too large", GL_INVALID_VALUE);
        if (params.primitiveRestartEnabled ||
            (!contextMtl->getDisplay()->getFeatures().hasCheapRenderPass.enabled &&
             contextMtl->getRenderCommandEncoder()))
        {
            IndexGenerationParams cpuPathParams = params;
            cpuPathParams.indices =
                elementBufferMtl->getBufferDataReadOnly(contextMtl, srcOffset).data();
            return generateTriFanBufferFromElementsArrayCPU(contextMtl, cpuPathParams,
                                                            indicesGenerated);
        }
        else
        {
            return generateTriFanBufferFromElementsArrayGPU(
                contextMtl, params.srcType, params.indexCount, elementBufferMtl->getCurrentBuffer(),
                static_cast<uint32_t>(srcOffset), params.dstBuffer, params.dstOffset);
        }
    }
    else
    {
        return generateTriFanBufferFromElementsArrayCPU(contextMtl, params, indicesGenerated);
    }
}

angle::Result IndexGeneratorUtils::generateTriFanBufferFromElementsArrayGPU(
    ContextMtl *contextMtl,
    gl::DrawElementsType srcType,
    uint32_t indexCount,
    const BufferRef &srcBuffer,
    uint32_t srcOffset,
    const BufferRef &dstBuffer,
    // Must be multiples of kIndexBufferOffsetAlignment
    uint32_t dstOffset)
{
    ComputeCommandEncoder *cmdEncoder = contextMtl->getComputeCommandEncoder();
    ASSERT(cmdEncoder);

    angle::ObjCPtr<id<MTLComputePipelineState>> pipelineState;
    ANGLE_TRY(getIndicesFromElemArrayGeneratorPipeline(
        contextMtl, srcType, srcOffset, @"genTriFanIndicesFromElements",
        &mTriFanFromElemArrayGeneratorShaders, &pipelineState));

    ASSERT(pipelineState);

    cmdEncoder->setComputePipelineState(pipelineState);

    ASSERT((dstOffset % kIndexBufferOffsetAlignment) == 0);
    ASSERT(indexCount > 2);

    IndexConversionUniform uniform;
    uniform.srcOffset  = srcOffset;
    uniform.indexCount = indexCount - 2;  // Only start from the 3rd element.

    cmdEncoder->setData(uniform, 0);
    cmdEncoder->setBuffer(srcBuffer, 0, 1);
    cmdEncoder->setBufferForWrite(dstBuffer, dstOffset, 2);

    DispatchCompute(contextMtl, cmdEncoder, pipelineState, uniform.indexCount);

    return angle::Result::Continue;
}

angle::Result IndexGeneratorUtils::generateTriFanBufferFromElementsArrayCPU(
    ContextMtl *contextMtl,
    const IndexGenerationParams &params,
    uint32_t *genIndices)
{
    switch (params.srcType)
    {
        case gl::DrawElementsType::UnsignedByte:
            return GenTriFanFromClientElements(contextMtl, params.indexCount,
                                               params.primitiveRestartEnabled,
                                               static_cast<const uint8_t *>(params.indices),
                                               params.dstBuffer, params.dstOffset, genIndices);
        case gl::DrawElementsType::UnsignedShort:
            return GenTriFanFromClientElements(contextMtl, params.indexCount,
                                               params.primitiveRestartEnabled,
                                               static_cast<const uint16_t *>(params.indices),
                                               params.dstBuffer, params.dstOffset, genIndices);
        case gl::DrawElementsType::UnsignedInt:
            return GenTriFanFromClientElements(contextMtl, params.indexCount,
                                               params.primitiveRestartEnabled,
                                               static_cast<const uint32_t *>(params.indices),
                                               params.dstBuffer, params.dstOffset, genIndices);
        default:
            UNREACHABLE();
    }

    return angle::Result::Stop;
}

angle::Result IndexGeneratorUtils::generateLineLoopBufferFromArrays(
    ContextMtl *contextMtl,
    const TriFanOrLineLoopFromArrayParams &params)
{
    ComputeCommandEncoder *cmdEncoder = contextMtl->getComputeCommandEncoder();
    ASSERT(cmdEncoder);

    angle::ObjCPtr<id<MTLComputePipelineState>> pipeline;
    ANGLE_TRY(getLineLoopFromArrayGeneratorPipeline(contextMtl, &pipeline));

    cmdEncoder->setComputePipelineState(pipeline);

    ASSERT((params.dstOffset % kIndexBufferOffsetAlignment) == 0);

    TriFanOrLineLoopArrayParams uniform;

    uniform.firstVertex = params.firstVertex;
    uniform.vertexCount = params.vertexCount;

    cmdEncoder->setData(uniform, 0);
    cmdEncoder->setBufferForWrite(params.dstBuffer, params.dstOffset, 2);

    DispatchCompute(contextMtl, cmdEncoder, pipeline, uniform.vertexCount + 1);

    return angle::Result::Continue;
}

angle::Result IndexGeneratorUtils::generateLineLoopBufferFromElementsArray(
    ContextMtl *contextMtl,
    const IndexGenerationParams &params,
    uint32_t *indicesGenerated)
{
    const gl::VertexArray *vertexArray = contextMtl->getState().getVertexArray();
    const gl::Buffer *elementBuffer    = vertexArray->getElementArrayBuffer();
    if (elementBuffer)
    {
        BufferMtl *elementBufferMtl = GetImpl(elementBuffer);
        size_t srcOffset            = reinterpret_cast<size_t>(params.indices);
        ANGLE_CHECK(contextMtl, srcOffset <= std::numeric_limits<uint32_t>::max(),
                    "Index offset is too large", GL_INVALID_VALUE);
        if (params.primitiveRestartEnabled ||
            (!contextMtl->getDisplay()->getFeatures().hasCheapRenderPass.enabled &&
             contextMtl->getRenderCommandEncoder()))
        {
            IndexGenerationParams cpuPathParams = params;
            cpuPathParams.indices =
                elementBufferMtl->getBufferDataReadOnly(contextMtl, srcOffset).data();
            return generateLineLoopBufferFromElementsArrayCPU(contextMtl, cpuPathParams,
                                                              indicesGenerated);
        }
        else
        {
            *indicesGenerated = params.indexCount + 1;
            return generateLineLoopBufferFromElementsArrayGPU(
                contextMtl, params.srcType, params.indexCount, elementBufferMtl->getCurrentBuffer(),
                static_cast<uint32_t>(srcOffset), params.dstBuffer, params.dstOffset);
        }
    }
    else
    {
        return generateLineLoopBufferFromElementsArrayCPU(contextMtl, params, indicesGenerated);
    }
}

angle::Result IndexGeneratorUtils::generateLineLoopBufferFromElementsArrayGPU(
    ContextMtl *contextMtl,
    gl::DrawElementsType srcType,
    uint32_t indexCount,
    const BufferRef &srcBuffer,
    uint32_t srcOffset,
    const BufferRef &dstBuffer,
    // Must be multiples of kIndexBufferOffsetAlignment
    uint32_t dstOffset)
{
    ComputeCommandEncoder *cmdEncoder = contextMtl->getComputeCommandEncoder();
    ASSERT(cmdEncoder);

    angle::ObjCPtr<id<MTLComputePipelineState>> pipelineState;
    ANGLE_TRY(getIndicesFromElemArrayGeneratorPipeline(
        contextMtl, srcType, srcOffset, @"genLineLoopIndicesFromElements",
        &mLineLoopFromElemArrayGeneratorShaders, &pipelineState));

    cmdEncoder->setComputePipelineState(pipelineState);

    ASSERT((dstOffset % kIndexBufferOffsetAlignment) == 0);
    ASSERT(indexCount >= 2);

    IndexConversionUniform uniform;
    uniform.srcOffset  = srcOffset;
    uniform.indexCount = indexCount;

    cmdEncoder->setData(uniform, 0);
    cmdEncoder->setBuffer(srcBuffer, 0, 1);
    cmdEncoder->setBufferForWrite(dstBuffer, dstOffset, 2);

    DispatchCompute(contextMtl, cmdEncoder, pipelineState, uniform.indexCount + 1);

    return angle::Result::Continue;
}

angle::Result IndexGeneratorUtils::generateLineLoopBufferFromElementsArrayCPU(
    ContextMtl *contextMtl,
    const IndexGenerationParams &params,
    uint32_t *indicesGenerated)
{
    angle::Span<uint8_t> dstSpan = params.dstBuffer->map(contextMtl, params.dstOffset);
    uint8_t *dstIndices          = dstSpan.data();
    if (dstSpan.empty())
    {
        return angle::Result::Stop;
    }
    const uint8_t *indices = static_cast<const uint8_t *>(params.indices);
    size_t dstIndexCount   = 0;
    switch (params.srcType)
    {
        case gl::DrawElementsType::UnsignedByte:
            dstIndexCount = CopyLineLoopIndices<uint8_t, uint32_t>(
                params.indexCount, indices, params.primitiveRestartEnabled, dstIndices);
            break;
        case gl::DrawElementsType::UnsignedShort:
            dstIndexCount = CopyLineLoopIndices<uint16_t, uint32_t>(
                params.indexCount, indices, params.primitiveRestartEnabled, dstIndices);
            break;
        case gl::DrawElementsType::UnsignedInt:
            dstIndexCount = CopyLineLoopIndices<uint32_t, uint32_t>(
                params.indexCount, indices, params.primitiveRestartEnabled, dstIndices);
            break;
        default:
            UNREACHABLE();
    }
    params.dstBuffer->unmapAndFlushSubset(contextMtl, params.dstOffset,
                                          dstIndexCount * sizeof(uint32_t));
    *indicesGenerated = static_cast<uint32_t>(dstIndexCount);
    return angle::Result::Continue;
}

angle::Result IndexGeneratorUtils::generateLineLoopLastSegment(ContextMtl *contextMtl,
                                                               uint32_t firstVertex,
                                                               uint32_t lastVertex,
                                                               const BufferRef &dstBuffer,
                                                               uint32_t dstOffset)
{
    uint8_t *ptr = dstBuffer->map(contextMtl, dstOffset).data();

    uint32_t indices[2] = {lastVertex, firstVertex};
    ANGLE_UNSAFE_TODO(memcpy(ptr, indices, sizeof(indices)));

    dstBuffer->unmapAndFlushSubset(contextMtl, dstOffset, sizeof(indices));

    return angle::Result::Continue;
}

angle::Result IndexGeneratorUtils::generateLineLoopLastSegmentFromElementsArray(
    ContextMtl *contextMtl,
    const IndexGenerationParams &params)
{
    ASSERT(!params.primitiveRestartEnabled);
    const gl::VertexArray *vertexArray = contextMtl->getState().getVertexArray();
    const gl::Buffer *elementBuffer    = vertexArray->getElementArrayBuffer();
    if (elementBuffer)
    {
        size_t srcOffset = reinterpret_cast<size_t>(params.indices);
        ANGLE_CHECK(contextMtl, srcOffset <= std::numeric_limits<uint32_t>::max(),
                    "Index offset is too large", GL_INVALID_VALUE);

        BufferMtl *bufferMtl = GetImpl(elementBuffer);
        std::pair<uint32_t, uint32_t> firstLast;
        ANGLE_TRY(bufferMtl->getFirstLastIndices(contextMtl, params.srcType,
                                                 static_cast<uint32_t>(srcOffset),
                                                 params.indexCount, &firstLast));

        return generateLineLoopLastSegment(contextMtl, firstLast.first, firstLast.second,
                                           params.dstBuffer, params.dstOffset);
    }
    else
    {
        return generateLineLoopLastSegmentFromElementsArrayCPU(contextMtl, params);
    }
}

angle::Result IndexGeneratorUtils::generateLineLoopLastSegmentFromElementsArrayCPU(
    ContextMtl *contextMtl,
    const IndexGenerationParams &params)
{
    ASSERT(!params.primitiveRestartEnabled);

    uint32_t first, last;

    switch (params.srcType)
    {
        case gl::DrawElementsType::UnsignedByte:
            GetFirstLastIndicesFromClientElements(
                params.indexCount, static_cast<const uint8_t *>(params.indices), &first, &last);
            break;
        case gl::DrawElementsType::UnsignedShort:
            GetFirstLastIndicesFromClientElements(
                params.indexCount, static_cast<const uint16_t *>(params.indices), &first, &last);
            break;
        case gl::DrawElementsType::UnsignedInt:
            GetFirstLastIndicesFromClientElements(
                params.indexCount, static_cast<const uint32_t *>(params.indices), &first, &last);
            break;
        default:
            UNREACHABLE();
            return angle::Result::Stop;
    }

    return generateLineLoopLastSegment(contextMtl, first, last, params.dstBuffer, params.dstOffset);
}

angle::Result VisibilityResultUtils::getVisibilityResultCombinePipeline(
    ContextMtl *contextMtl,
    bool keepOldValue,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outComputePipeline)
{
    // There is no guarantee Objective-C's BOOL is equal to bool, so casting just in case.
    BOOL keepOldValueVal = keepOldValue;
    angle::ObjCPtr<id<MTLFunction>> &shader =
        mVisibilityResultCombineComputeShaders[keepOldValueVal];
    if (!shader)
    {
        ANGLE_MTL_OBJC_SCOPE
        {
            angle::ObjCPtr<MTLFunctionConstantValues> funcConstants =
                angle::adoptObjCPtr([[MTLFunctionConstantValues alloc] init]);

            [funcConstants setConstantValue:&keepOldValueVal
                                       type:MTLDataTypeBool
                                   withName:VISIBILITY_RESULT_KEEP_OLD_VAL_CONSTANT_NAME];

            ANGLE_TRY(EnsureSpecializedShaderInitialized(contextMtl, @"combineVisibilityResult",
                                                         funcConstants, &shader));
        }
    }

    return contextMtl->getPipelineCache().getComputePipeline(contextMtl, shader,
                                                             outComputePipeline);
}

angle::Result VisibilityResultUtils::combineVisibilityResult(ContextMtl *contextMtl,
                                                             bool keepOldValue,
                                                             size_t startOffset,
                                                             size_t numOffsets,
                                                             const BufferRef &renderPassResultBuf,
                                                             const BufferRef &finalResultBuf)
{
    if (numOffsets == 1 && !keepOldValue)
    {
        // Use blit command to copy directly
        BlitCommandEncoder *blitEncoder = contextMtl->getBlitCommandEncoder();

        blitEncoder->copyBuffer(renderPassResultBuf, startOffset, finalResultBuf, 0,
                                kOcclusionQueryResultSize);
        return angle::Result::Continue;
    }

    ComputeCommandEncoder *cmdEncoder = contextMtl->getComputeCommandEncoder();
    ASSERT(cmdEncoder);

    angle::ObjCPtr<id<MTLComputePipelineState>> pipeline;
    ANGLE_TRY(getVisibilityResultCombinePipeline(contextMtl, keepOldValue, &pipeline));
    cmdEncoder->setComputePipelineState(pipeline);

    CombineVisibilityResultUniform options;
    // Offset is viewed as 64 bit unit in compute shader.
    options.startOffset = static_cast<uint32_t>(startOffset / kOcclusionQueryResultSize);
    options.numOffsets  = static_cast<uint32_t>(numOffsets);

    cmdEncoder->setData(options, 0);
    cmdEncoder->setBuffer(renderPassResultBuf, 0, 1);
    cmdEncoder->setBufferForWrite(finalResultBuf, 0, 2);

    DispatchCompute(contextMtl, cmdEncoder, pipeline, 1);

    return angle::Result::Continue;
}

angle::Result MipmapUtils::get3DMipGeneratorPipeline(
    ContextMtl *contextMtl,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outComputePipeline)
{
    ANGLE_TRY(EnsureShaderInitialized(contextMtl, @"generate3DMipmaps", &m3DMipGeneratorShader));
    return contextMtl->getPipelineCache().getComputePipeline(contextMtl, m3DMipGeneratorShader,
                                                             outComputePipeline);
}

angle::Result MipmapUtils::get2DMipGeneratorPipeline(
    ContextMtl *contextMtl,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outComputePipeline)
{
    ANGLE_TRY(EnsureShaderInitialized(contextMtl, @"generate2DMipmaps", &m2DMipGeneratorShader));
    return contextMtl->getPipelineCache().getComputePipeline(contextMtl, m2DMipGeneratorShader,
                                                             outComputePipeline);
}

angle::Result MipmapUtils::get2DArrayMipGeneratorPipeline(
    ContextMtl *contextMtl,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outComputePipeline)
{
    ANGLE_TRY(EnsureShaderInitialized(contextMtl, @"generate2DArrayMipmaps",
                                      &m2DArrayMipGeneratorShader));
    return contextMtl->getPipelineCache().getComputePipeline(contextMtl, m2DArrayMipGeneratorShader,
                                                             outComputePipeline);
}

angle::Result MipmapUtils::getCubeMipGeneratorPipeline(
    ContextMtl *contextMtl,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outComputePipeline)
{
    ANGLE_TRY(
        EnsureShaderInitialized(contextMtl, @"generateCubeMipmaps", &mCubeMipGeneratorShader));
    return contextMtl->getPipelineCache().getComputePipeline(contextMtl, mCubeMipGeneratorShader,
                                                             outComputePipeline);
}

angle::Result MipmapUtils::generateMipmapCS(ContextMtl *contextMtl,
                                            const TextureRef &srcTexture,
                                            bool sRGBMipmap,
                                            NativeTexLevelArray *mipmapOutputViews)
{
    // Only support 3D texture for now.
    ASSERT(srcTexture->textureType() == MTLTextureType3D);

    MTLSize threadGroupSize;
    uint32_t slices = 1;
    angle::ObjCPtr<id<MTLComputePipelineState>> computePipeline;
    switch (srcTexture->textureType())
    {
        case MTLTextureType2D:
            ANGLE_TRY(get2DMipGeneratorPipeline(contextMtl, &computePipeline));
            threadGroupSize = MTLSizeMake(kGenerateMipThreadGroupSizePerDim,
                                          kGenerateMipThreadGroupSizePerDim, 1);
            break;
        case MTLTextureType2DArray:
            ANGLE_TRY(get2DArrayMipGeneratorPipeline(contextMtl, &computePipeline));
            slices          = srcTexture->arrayLength();
            threadGroupSize = MTLSizeMake(kGenerateMipThreadGroupSizePerDim,
                                          kGenerateMipThreadGroupSizePerDim, 1);
            break;
        case MTLTextureTypeCube:
            ANGLE_TRY(getCubeMipGeneratorPipeline(contextMtl, &computePipeline));
            slices          = 6;
            threadGroupSize = MTLSizeMake(kGenerateMipThreadGroupSizePerDim,
                                          kGenerateMipThreadGroupSizePerDim, 1);
            break;
        case MTLTextureType3D:
            ANGLE_TRY(get3DMipGeneratorPipeline(contextMtl, &computePipeline));
            threadGroupSize =
                MTLSizeMake(kGenerateMipThreadGroupSizePerDim, kGenerateMipThreadGroupSizePerDim,
                            kGenerateMipThreadGroupSizePerDim);
            break;
        default:
            UNREACHABLE();
    }

    // The compute shader supports up to 4 mipmaps generated per pass.
    // See shaders/gen_mipmap.metal
    uint32_t maxMipsPerBatch = 4;

    if (threadGroupSize.width * threadGroupSize.height * threadGroupSize.depth >
            computePipeline.get().maxTotalThreadsPerThreadgroup ||
        ANGLE_UNLIKELY(
            !contextMtl->getDisplay()->getFeatures().allowGenMultipleMipsPerPass.enabled))
    {
        // Multiple mipmaps generation is not supported due to hardware's thread group size limits.
        // Fallback to generate one mip per pass and reduce thread group size.
        if (ANGLE_UNLIKELY(threadGroupSize.width * threadGroupSize.height >
                           computePipeline.get().maxTotalThreadsPerThreadgroup))
        {
            // Even with reduced thread group size, we cannot proceed.
            // HACK: use blit command encoder to generate mipmaps if it is not possible
            // to use compute shader due to hardware limits.
            BlitCommandEncoder *blitEncoder = contextMtl->getBlitCommandEncoder();
            blitEncoder->generateMipmapsForTexture(srcTexture);
            return angle::Result::Continue;
        }

        threadGroupSize.depth = 1;
        maxMipsPerBatch       = 1;
    }

    ComputeCommandEncoder *cmdEncoder = contextMtl->getComputeCommandEncoder();
    ASSERT(cmdEncoder);
    cmdEncoder->setComputePipelineState(computePipeline);

    Generate3DMipmapUniform options;

    uint32_t remainMips             = srcTexture->mipmapLevels() - 1;
    MipmapNativeLevel batchSrcLevel = kZeroNativeMipLevel;
    options.srcLevel                = batchSrcLevel.get();
    options.sRGB                    = sRGBMipmap;

    cmdEncoder->setTexture(srcTexture, 0);
    cmdEncoder->markResourceBeingWrittenByGPU(srcTexture);
    while (remainMips)
    {
        const TextureRef &firstMipView =
            mipmapOutputViews->at(mtl::MipmapNativeLevel(batchSrcLevel + 1));
        gl::Extents size = firstMipView->sizeAt0();
        bool isPow2 = gl::isPow2(size.width) && gl::isPow2(size.height) && gl::isPow2(size.depth);

        // Currently multiple mipmaps generation is only supported for power of two base level.
        if (isPow2)
        {
            options.numMipmapsToGenerate = std::min(remainMips, maxMipsPerBatch);
        }
        else
        {
            options.numMipmapsToGenerate = 1;
        }

        cmdEncoder->setData(options, 0);

        for (uint32_t i = 1; i <= options.numMipmapsToGenerate; ++i)
        {
            cmdEncoder->setTexture(
                mipmapOutputViews->at(mtl::MipmapNativeLevel(options.srcLevel + i)), i);
        }

        uint32_t threadsPerZ = std::max(slices, firstMipView->depthAt0());

        DispatchCompute(
            contextMtl, cmdEncoder,
            /** allowNonUniform */ false,
            MTLSizeMake(firstMipView->widthAt0(), firstMipView->heightAt0(), threadsPerZ),
            threadGroupSize);

        remainMips -= options.numMipmapsToGenerate;
        batchSrcLevel    = batchSrcLevel + options.numMipmapsToGenerate;
        options.srcLevel = batchSrcLevel.get();
    }

    return angle::Result::Continue;
}

// CopyPixelsUtils implementation
CopyPixelsUtils::CopyPixelsUtils(const std::string &readShaderName,
                                 const std::string &writeShaderName)
    : mReadShaderName(readShaderName), mWriteShaderName(writeShaderName)
{}

angle::Result CopyPixelsUtils::getT2BComputePipeline(
    ContextMtl *contextMtl,
    const angle::Format &angleFormat,
    const TextureRef &texture,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outComputePipeline)
{
    int formatIDValue     = static_cast<int>(angleFormat.id);
    int shaderTextureType = GetShaderTextureType(texture);

    auto &shader = mT2BComputeShaders[formatIDValue][shaderTextureType];

    if (!shader)
    {
        // Shader not cached, create it now:
        ANGLE_MTL_OBJC_SCOPE
        {
            angle::ObjCPtr<MTLFunctionConstantValues> funcConstants =
                angle::adoptObjCPtr([[MTLFunctionConstantValues alloc] init]);

            [funcConstants setConstantValue:&formatIDValue
                                       type:MTLDataTypeInt
                                   withName:COPY_FORMAT_TYPE_CONSTANT_NAME];
            [funcConstants setConstantValue:&shaderTextureType
                                       type:MTLDataTypeInt
                                   withName:PIXEL_COPY_TEXTURE_TYPE_CONSTANT_NAME];

            NSString *shaderName = [NSString stringWithUTF8String:mWriteShaderName.c_str()];

            ANGLE_TRY(
                EnsureSpecializedShaderInitialized(contextMtl, shaderName, funcConstants, &shader));
        }
    }

    return contextMtl->getPipelineCache().getComputePipeline(contextMtl, shader,
                                                             outComputePipeline);
}

angle::Result CopyPixelsUtils::getB2TRenderPipeline(
    ContextMtl *contextMtl,
    RenderCommandEncoder *cmdEncoder,
    const angle::Format &angleFormat,
    angle::ObjCPtr<id<MTLRenderPipelineState>> *outRenderPipeline)
{

    // Vertex shader
    ANGLE_TRY(EnsureShaderInitialized(contextMtl, @"readFromBufferToTextureVS", &mB2TVertexShader));

    // Fragment shader:
    int formatIDValue    = static_cast<int>(angleFormat.id);
    auto &fragmentShader = mB2TFragmentShaders[formatIDValue];

    if (!fragmentShader)
    {
        // Shader not cached, create it now:
        ANGLE_MTL_OBJC_SCOPE
        {
            angle::ObjCPtr<MTLFunctionConstantValues> funcConstants =
                angle::adoptObjCPtr([[MTLFunctionConstantValues alloc] init]);

            [funcConstants setConstantValue:&formatIDValue
                                       type:MTLDataTypeInt
                                   withName:COPY_FORMAT_TYPE_CONSTANT_NAME];

            NSString *shaderName = [NSString stringWithUTF8String:mReadShaderName.c_str()];

            ANGLE_TRY(EnsureSpecializedShaderInitialized(contextMtl, shaderName, funcConstants,
                                                         &fragmentShader));
        }
    }

    RenderPipelineDesc pipelineDesc;
    const RenderPassDesc &renderPassDesc = cmdEncoder->renderPassDesc();
    renderPassDesc.populateRenderPipelineOutputDesc(&pipelineDesc.outputDescriptor);

    pipelineDesc.setInputPrimitiveTopology(MTLPrimitiveTopologyClassTriangle);

    return contextMtl->getPipelineCache().getRenderPipeline(
        contextMtl, mB2TVertexShader, fragmentShader, pipelineDesc, outRenderPipeline);
}

angle::Result CopyPixelsUtils::unpackPixelsWithDraw(const gl::Context *context,
                                                    const angle::Format &srcAngleFormat,
                                                    const CopyPixelsFromBufferParams &params)
{
    ContextMtl *contextMtl           = mtl::GetImpl(context);
    RenderCommandEncoder *cmdEncoder = contextMtl->getTextureRenderCommandEncoder(
        params.texture, kZeroNativeMipLevel, params.textureSliceOrDepth);
    ASSERT(cmdEncoder);

    angle::ObjCPtr<id<MTLRenderPipelineState>> pipeline;
    ANGLE_TRY(getB2TRenderPipeline(contextMtl, cmdEncoder, srcAngleFormat, &pipeline));

    // Setup states
    SetupFullscreenQuadDrawCommonStates(cmdEncoder);
    cmdEncoder->setRenderPipelineState(pipeline);
    cmdEncoder->setFragmentBuffer(params.buffer, 0, 1);

    id<MTLDepthStencilState> dsState =
        contextMtl->getDisplay()->getStateCache().getNullDepthStencilState(
            contextMtl->getMetalDevice());
    cmdEncoder->setDepthStencilState(dsState);

    // Viewports
    MTLViewport viewport;
    MTLScissorRect scissorRect;

    const gl::Rectangle rect(params.textureArea.x, params.textureArea.y, params.textureArea.width,
                             params.textureArea.height);
    viewport = GetViewport(rect);

    scissorRect = GetScissorRect(rect);

    cmdEncoder->setViewport(viewport);
    cmdEncoder->setScissorRect(scissorRect);

    // uniform
    CopyPixelFromBufferUniforms options;
    options.bufferStartOffset = params.bufferStartOffset;
    options.pixelSize         = srcAngleFormat.pixelBytes;
    options.bufferRowPitch    = params.bufferRowPitch;
    options.bufferDepthPitch  = params.bufferDepthPitch;
    options.textureOffset[0]  = params.textureArea.x;
    options.textureOffset[1]  = params.textureArea.y;
    cmdEncoder->setFragmentData(options, 0);

    {
        // Need to disable occlusion query, otherwise blitting will affect the occlusion counting
        ScopedDisableOcclusionQuery disableOcclusionQuery(contextMtl, cmdEncoder);
        // Draw the screen aligned quad
        cmdEncoder->draw(MTLPrimitiveTypeTriangleStrip, 0, 4);
    }

    // Invalidate current context's state
    contextMtl->invalidateState(context);

    return angle::Result::Continue;
}

angle::Result CopyPixelsUtils::packPixelsCS(ContextMtl *contextMtl,
                                            const angle::Format &dstAngleFormat,
                                            const CopyPixelsToBufferParams &params)
{
    ComputeCommandEncoder *cmdEncoder = contextMtl->getComputeCommandEncoder();
    ASSERT(cmdEncoder);

    angle::ObjCPtr<id<MTLComputePipelineState>> pipeline;
    ANGLE_TRY(getT2BComputePipeline(contextMtl, dstAngleFormat, params.texture, &pipeline));

    cmdEncoder->setComputePipelineState(pipeline);
    cmdEncoder->setTexture(params.texture, 0);
    cmdEncoder->setBufferForWrite(params.buffer, 0, 1);

    WritePixelToBufferUniforms options;
    options.copySize[0]            = params.textureArea.width;
    options.copySize[1]            = params.textureArea.height;
    options.bufferStartOffset      = params.bufferStartOffset;
    options.pixelSize              = dstAngleFormat.pixelBytes;
    options.bufferRowPitch         = params.bufferRowPitch;
    options.textureOffset[0]       = params.textureArea.x;
    options.textureOffset[1]       = params.textureArea.y;
    options.textureLevel           = params.textureLevel.get();
    options.textureLayer           = params.textureSliceOrDepth;
    options.reverseTextureRowOrder = params.reverseTextureRowOrder;
    cmdEncoder->setData(options, 0);

    NSUInteger w                  = pipeline.get().threadExecutionWidth;
    MTLSize threadsPerThreadgroup = MTLSizeMake(w, 1, 1);

    MTLSize threads = MTLSizeMake(params.textureArea.width, params.textureArea.height, 1);

    DispatchCompute(contextMtl, cmdEncoder,
                    /** allowNonUniform */ true, threads, threadsPerThreadgroup);

    return angle::Result::Continue;
}

angle::Result VertexFormatConversionUtils::convertVertexFormatToFloatCS(
    ContextMtl *contextMtl,
    const angle::Format &srcAngleFormat,
    const VertexFormatConvertParams &params)
{
    // Since vertex buffer doesn't depend on previous render commands we don't
    // need to end the current render encoder.
    ComputeCommandEncoder *cmdEncoder =
        contextMtl->getComputeCommandEncoderWithoutEndingRenderEncoder();
    ASSERT(cmdEncoder);

    angle::ObjCPtr<id<MTLComputePipelineState>> pipeline;
    ANGLE_TRY(getFloatConverstionComputePipeline(contextMtl, srcAngleFormat, &pipeline));

    ANGLE_TRY(setupCommonConvertVertexFormatToFloat(contextMtl, cmdEncoder, pipeline,
                                                    srcAngleFormat, params));

    DispatchCompute(contextMtl, cmdEncoder, pipeline, params.vertexCount);
    return angle::Result::Continue;
}

angle::Result VertexFormatConversionUtils::convertVertexFormatToFloatVS(
    const gl::Context *context,
    RenderCommandEncoder *cmdEncoder,
    const angle::Format &srcAngleFormat,
    const VertexFormatConvertParams &params)
{
    ContextMtl *contextMtl = GetImpl(context);
    ASSERT(cmdEncoder);
    ASSERT(contextMtl->getDisplay()->getFeatures().hasExplicitMemBarrier.enabled);

    angle::ObjCPtr<id<MTLRenderPipelineState>> pipeline;
    ANGLE_TRY(getFloatConverstionRenderPipeline(contextMtl, cmdEncoder, srcAngleFormat, &pipeline));

    ANGLE_TRY(setupCommonConvertVertexFormatToFloat(contextMtl, cmdEncoder, pipeline,
                                                    srcAngleFormat, params));

    cmdEncoder->draw(MTLPrimitiveTypePoint, 0, params.vertexCount);

    cmdEncoder->memoryBarrierWithResource(params.dstBuffer, MTLRenderStageVertex,
                                          MTLRenderStageVertex);

    // Invalidate current context's state.
    // NOTE(hqle): Consider invalidating only affected states.
    contextMtl->invalidateState(context);

    return angle::Result::Continue;
}

template <typename EncoderType, typename PipelineType>
angle::Result VertexFormatConversionUtils::setupCommonConvertVertexFormatToFloat(
    ContextMtl *contextMtl,
    EncoderType cmdEncoder,
    const PipelineType &pipeline,
    const angle::Format &srcAngleFormat,
    const VertexFormatConvertParams &params)
{
    if (pipeline == nullptr)
    {
        return angle::Result::Stop;
    }
    SetPipelineState(cmdEncoder, pipeline);
    SetComputeOrVertexBuffer(cmdEncoder, params.srcBuffer, 0, 1);
    SetComputeOrVertexBufferForWrite(cmdEncoder, params.dstBuffer, 0, 2);

    CopyVertexUniforms options;
    options.srcBufferStartOffset = params.srcBufferStartOffset;
    options.srcStride            = params.srcStride;

    options.dstBufferStartOffset = params.dstBufferStartOffset;
    options.dstStride            = params.dstStride;
    options.dstComponents        = params.dstComponents;

    options.vertexCount = params.vertexCount;
    SetComputeOrVertexData(cmdEncoder, options, 0);

    return angle::Result::Continue;
}

// Expand number of components per vertex's attribute
angle::Result VertexFormatConversionUtils::expandVertexFormatComponentsCS(
    ContextMtl *contextMtl,
    const angle::Format &srcAngleFormat,
    const VertexFormatConvertParams &params)
{
    // Since vertex buffer doesn't depend on previous render commands we don't
    // need to end the current render encoder.
    ComputeCommandEncoder *cmdEncoder =
        contextMtl->getComputeCommandEncoderWithoutEndingRenderEncoder();
    ASSERT(cmdEncoder);

    angle::ObjCPtr<id<MTLComputePipelineState>> pipeline;
    ANGLE_TRY(getComponentsExpandComputePipeline(contextMtl, &pipeline));

    ANGLE_TRY(setupCommonExpandVertexFormatComponents(contextMtl, cmdEncoder, pipeline,
                                                      srcAngleFormat, params));

    DispatchCompute(contextMtl, cmdEncoder, pipeline, params.vertexCount);
    return angle::Result::Continue;
}

angle::Result VertexFormatConversionUtils::expandVertexFormatComponentsVS(
    const gl::Context *context,
    RenderCommandEncoder *cmdEncoder,
    const angle::Format &srcAngleFormat,
    const VertexFormatConvertParams &params)
{
    ContextMtl *contextMtl = GetImpl(context);
    ASSERT(cmdEncoder);
    ASSERT(contextMtl->getDisplay()->getFeatures().hasExplicitMemBarrier.enabled);

    angle::ObjCPtr<id<MTLRenderPipelineState>> pipeline;
    ANGLE_TRY(getComponentsExpandRenderPipeline(contextMtl, cmdEncoder, &pipeline));

    ANGLE_TRY(setupCommonExpandVertexFormatComponents(contextMtl, cmdEncoder, pipeline,
                                                      srcAngleFormat, params));

    cmdEncoder->draw(MTLPrimitiveTypePoint, 0, params.vertexCount);

    cmdEncoder->memoryBarrierWithResource(params.dstBuffer, MTLRenderStageVertex,
                                          MTLRenderStageVertex);

    // Invalidate current context's state.
    // NOTE(hqle): Consider invalidating only affected states.
    contextMtl->invalidateState(context);

    return angle::Result::Continue;
}

template <typename EncoderType, typename PipelineType>
angle::Result VertexFormatConversionUtils::setupCommonExpandVertexFormatComponents(
    ContextMtl *contextMtl,
    EncoderType cmdEncoder,
    const PipelineType &pipeline,
    const angle::Format &srcAngleFormat,
    const VertexFormatConvertParams &params)
{
    if (pipeline == nullptr)
    {
        return angle::Result::Stop;
    }
    SetPipelineState(cmdEncoder, pipeline);
    SetComputeOrVertexBuffer(cmdEncoder, params.srcBuffer, 0, 1);
    SetComputeOrVertexBufferForWrite(cmdEncoder, params.dstBuffer, 0, 2);

    CopyVertexUniforms options;
    options.srcBufferStartOffset = params.srcBufferStartOffset;
    options.srcStride            = params.srcStride;
    options.srcComponentBytes    = srcAngleFormat.pixelBytes / srcAngleFormat.channelCount;
    options.srcComponents        = srcAngleFormat.channelCount;
    options.srcDefaultAlphaData  = params.srcDefaultAlphaData;

    options.dstBufferStartOffset = params.dstBufferStartOffset;
    options.dstStride            = params.dstStride;
    options.dstComponents        = params.dstComponents;

    options.vertexCount = params.vertexCount;
    SetComputeOrVertexData(cmdEncoder, options, 0);

    return angle::Result::Continue;
}

angle::Result VertexFormatConversionUtils::getComponentsExpandComputePipeline(
    ContextMtl *contextMtl,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outPipelineState)
{
    ANGLE_TRY(EnsureShaderInitialized(contextMtl, @"expandVertexFormatComponentsCS",
                                      &mComponentsExpandComputeShader));
    return contextMtl->getPipelineCache().getComputePipeline(
        contextMtl, mComponentsExpandComputeShader, outPipelineState);
}

angle::Result VertexFormatConversionUtils::getComponentsExpandRenderPipeline(
    ContextMtl *contextMtl,
    RenderCommandEncoder *cmdEncoder,
    angle::ObjCPtr<id<MTLRenderPipelineState>> *outPipelineState)
{
    ANGLE_MTL_OBJC_SCOPE
    {
        if (!mComponentsExpandVertexShader)
        {
            id<MTLLibrary> shaderLib      = contextMtl->getDisplay()->getDefaultShadersLib();
            mComponentsExpandVertexShader = angle::adoptObjCPtr(
                [shaderLib newFunctionWithName:@"expandVertexFormatComponentsVS"]);
            ANGLE_CHECK(contextMtl, mComponentsExpandVertexShader, gl::err::kInternalError,
                        GL_INVALID_OPERATION);
        }

        RenderPipelineDesc pipelineDesc =
            GetComputingVertexShaderOnlyRenderPipelineDesc(cmdEncoder);

        return contextMtl->getPipelineCache().getRenderPipeline(
            contextMtl, mComponentsExpandVertexShader, nullptr, pipelineDesc, outPipelineState);
    }
}

angle::Result VertexFormatConversionUtils::getFloatConverstionComputePipeline(
    ContextMtl *contextMtl,
    const angle::Format &srcAngleFormat,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outPipelineState)
{
    int formatIDValue = static_cast<int>(srcAngleFormat.id);

    auto &shader = mConvertToFloatCompPipelineCaches[formatIDValue];

    if (!shader)
    {
        // Pipeline not cached, create it now:
        ANGLE_MTL_OBJC_SCOPE
        {
            angle::ObjCPtr<MTLFunctionConstantValues> funcConstants =
                angle::adoptObjCPtr([[MTLFunctionConstantValues alloc] init]);

            [funcConstants setConstantValue:&formatIDValue
                                       type:MTLDataTypeInt
                                   withName:COPY_FORMAT_TYPE_CONSTANT_NAME];

            ANGLE_TRY(EnsureSpecializedShaderInitialized(
                contextMtl, @"convertToFloatVertexFormatCS", funcConstants, &shader));
        }
    }

    return contextMtl->getPipelineCache().getComputePipeline(contextMtl, shader, outPipelineState);
}

angle::Result VertexFormatConversionUtils::getFloatConverstionRenderPipeline(
    ContextMtl *contextMtl,
    RenderCommandEncoder *cmdEncoder,
    const angle::Format &srcAngleFormat,
    angle::ObjCPtr<id<MTLRenderPipelineState>> *outPipelineState)
{
    ANGLE_MTL_OBJC_SCOPE
    {
        int formatIDValue = static_cast<int>(srcAngleFormat.id);

        if (!mConvertToFloatVertexShaders[formatIDValue])
        {
            NSError *err             = nil;
            id<MTLLibrary> shaderLib = contextMtl->getDisplay()->getDefaultShadersLib();
            angle::ObjCPtr<MTLFunctionConstantValues> funcConstants =
                angle::adoptObjCPtr([[MTLFunctionConstantValues alloc] init]);

            [funcConstants setConstantValue:&formatIDValue
                                       type:MTLDataTypeInt
                                   withName:COPY_FORMAT_TYPE_CONSTANT_NAME];

            mConvertToFloatVertexShaders[formatIDValue] =
                angle::adoptObjCPtr([shaderLib newFunctionWithName:@"convertToFloatVertexFormatVS"
                                                    constantValues:funcConstants
                                                             error:&err]);
            ANGLE_MTL_CHECK(contextMtl, mConvertToFloatVertexShaders[formatIDValue], err);
        }

        RenderPipelineDesc pipelineDesc =
            GetComputingVertexShaderOnlyRenderPipelineDesc(cmdEncoder);

        return contextMtl->getPipelineCache().getRenderPipeline(
            contextMtl, mConvertToFloatVertexShaders[formatIDValue], nullptr, pipelineDesc,
            outPipelineState);
    }
}

angle::Result BlockLinearizationUtils::linearizeBlocks(ContextMtl *contextMtl,
                                                       const BlockLinearizationParams &params)
{
    ComputeCommandEncoder *cmdEncoder = contextMtl->getComputeCommandEncoder();
    ASSERT(cmdEncoder);

    angle::ObjCPtr<id<MTLComputePipelineState>> pipeline;
    ANGLE_TRY(getBlockLinearizationComputePipeline(contextMtl, &pipeline));
    cmdEncoder->setComputePipelineState(pipeline);

    // Block layout
    ASSERT(params.blocksWide >= 2 && params.blocksHigh >= 2);
    const uint32_t dimensions[2] = {params.blocksWide, params.blocksHigh};
    cmdEncoder->setData(dimensions, 0);

    // Buffer with original PVRTC1 blocks
    cmdEncoder->setBuffer(params.srcBuffer, params.srcBufferOffset, 1);

    // Buffer to hold linearized PVRTC1 blocks
    cmdEncoder->setBufferForWrite(params.dstBuffer, 0, 2);

    NSUInteger w                  = pipeline.get().threadExecutionWidth;
    NSUInteger h                  = pipeline.get().maxTotalThreadsPerThreadgroup / w;
    MTLSize threadsPerThreadgroup = MTLSizeMake(w, h, 1);
    MTLSize threads               = MTLSizeMake(params.blocksWide, params.blocksHigh, 1);
    DispatchCompute(contextMtl, cmdEncoder,
                    /** allowNonUniform */ true, threads, threadsPerThreadgroup);
    return angle::Result::Continue;
}

angle::Result BlockLinearizationUtils::getBlockLinearizationComputePipeline(
    ContextMtl *contextMtl,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outPipelineState)
{
    ANGLE_TRY(
        EnsureShaderInitialized(contextMtl, @"linearizeBlocks", &mLinearizeBlocksComputeShader));
    return contextMtl->getPipelineCache().getComputePipeline(
        contextMtl, mLinearizeBlocksComputeShader, outPipelineState);
}

angle::Result DepthSaturationUtils::saturateDepth(ContextMtl *contextMtl,
                                                  const DepthSaturationParams &params)
{
    ComputeCommandEncoder *cmdEncoder = contextMtl->getComputeCommandEncoder();
    ASSERT(cmdEncoder);

    angle::ObjCPtr<id<MTLComputePipelineState>> pipeline;
    ANGLE_TRY(getDepthSaturationComputePipeline(contextMtl, &pipeline));
    cmdEncoder->setComputePipelineState(pipeline);

    // Image layout
    ASSERT(params.dstWidth > 0 && params.dstHeight > 0);
    ASSERT(params.srcPitch >= params.dstWidth);
    const uint32_t dimensions[4] = {params.dstWidth, params.dstHeight, params.srcPitch, 0};
    cmdEncoder->setData(dimensions, 0);

    cmdEncoder->setBuffer(params.srcBuffer, params.srcBufferOffset, 1);
    cmdEncoder->setBuffer(params.dstBuffer, 0, 2);

    NSUInteger w                  = pipeline.get().threadExecutionWidth;
    NSUInteger h                  = pipeline.get().maxTotalThreadsPerThreadgroup / w;
    MTLSize threadsPerThreadgroup = MTLSizeMake(w, h, 1);
    MTLSize threads               = MTLSizeMake(params.dstWidth, params.dstHeight, 1);
    DispatchCompute(contextMtl, cmdEncoder,
                    /** allowNonUniform */ true, threads, threadsPerThreadgroup);
    return angle::Result::Continue;
}

angle::Result DepthSaturationUtils::getDepthSaturationComputePipeline(
    ContextMtl *contextMtl,
    angle::ObjCPtr<id<MTLComputePipelineState>> *outPipelineState)
{
    ANGLE_TRY(EnsureShaderInitialized(contextMtl, @"saturateDepth", &mSaturateDepthComputeShader));
    return contextMtl->getPipelineCache().getComputePipeline(
        contextMtl, mSaturateDepthComputeShader, outPipelineState);
}

}  // namespace mtl
}  // namespace rx
