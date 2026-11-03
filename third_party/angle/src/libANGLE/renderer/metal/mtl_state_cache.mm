//
// Copyright 2019 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// mtl_state_cache.mm:
//    Implements StateCache, RenderPipelineCache and various
//    C struct versions of Metal sampler, depth stencil, render pass, render pipeline descriptors.
//

#include "libANGLE/renderer/metal/mtl_state_cache.h"

#include <sstream>

#include "common/debug.h"
#include "common/span.h"
#include "common/unsafe_buffers.h"
#include "libANGLE/renderer/metal/ContextMtl.h"
#include "libANGLE/renderer/metal/mtl_resources.h"
#include "libANGLE/renderer/metal/mtl_utils.h"
#include "platform/autogen/FeaturesMtl_autogen.h"

namespace rx
{
namespace mtl
{

namespace
{

inline angle::ObjCPtr<MTLStencilDescriptor> ToObjC(const StencilDesc &desc)
{
    auto objCDesc = angle::adoptObjCPtr([[MTLStencilDescriptor alloc] init]);
    objCDesc.get().stencilFailureOperation = desc.getStencilFailureOperation();
    objCDesc.get().depthFailureOperation   = desc.getDepthFailureOperation();
    objCDesc.get().depthStencilPassOperation = desc.getDepthStencilPassOperation();
    objCDesc.get().stencilCompareFunction    = desc.getStencilCompareFunction();
    objCDesc.get().readMask                  = desc.readMask;
    objCDesc.get().writeMask                 = desc.writeMask;
    return objCDesc;
}

inline angle::ObjCPtr<MTLDepthStencilDescriptor> ToObjC(const DepthStencilDesc &desc)
{
    auto objCDesc = angle::adoptObjCPtr([[MTLDepthStencilDescriptor alloc] init]);
    objCDesc.get().backFaceStencil  = ToObjC(desc.backFaceStencil);
    objCDesc.get().frontFaceStencil = ToObjC(desc.frontFaceStencil);
    objCDesc.get().depthCompareFunction = desc.getDepthCompareFunction();
    objCDesc.get().depthWriteEnabled    = desc.isDepthWriteEnabled();
    return objCDesc;
}

inline angle::ObjCPtr<MTLSamplerDescriptor> ToObjC(const SamplerDesc &desc)
{
    auto objCDesc = angle::adoptObjCPtr([[MTLSamplerDescriptor alloc] init]);
    objCDesc.get().rAddressMode    = desc.getRAddressMode();
    objCDesc.get().sAddressMode    = desc.getSAddressMode();
    objCDesc.get().tAddressMode    = desc.getTAddressMode();
    objCDesc.get().minFilter       = desc.getMinFilter();
    objCDesc.get().magFilter       = desc.getMagFilter();
    objCDesc.get().mipFilter       = desc.getMipFilter();
    objCDesc.get().maxAnisotropy   = desc.getMaxAnisotropy();
    objCDesc.get().compareFunction = desc.getCompareFunction();
    return objCDesc;
}

inline angle::ObjCPtr<MTLVertexAttributeDescriptor> ToObjC(const VertexAttributeDesc &desc)
{
    auto objCDesc = angle::adoptObjCPtr([[MTLVertexAttributeDescriptor alloc] init]);
    objCDesc.get().format      = desc.getFormat();
    objCDesc.get().offset      = desc.getOffset();
    objCDesc.get().bufferIndex = desc.getBufferIndex();
    ASSERT(desc.getBufferIndex() >= kVboBindingIndexStart);
    return objCDesc;
}

inline angle::ObjCPtr<MTLVertexBufferLayoutDescriptor> ToObjC(const VertexBufferLayoutDesc &desc)
{
    auto objCDesc = angle::adoptObjCPtr([[MTLVertexBufferLayoutDescriptor alloc] init]);
    objCDesc.get().stepFunction = desc.getStepFunction();
    objCDesc.get().stepRate     = desc.stepRate;
    objCDesc.get().stride       = desc.stride;
    return objCDesc;
}

inline angle::ObjCPtr<MTLVertexDescriptor> ToObjC(const VertexDesc &desc)
{
    auto objCDesc = angle::adoptObjCPtr([[MTLVertexDescriptor alloc] init]);
    [objCDesc reset];

    for (uint8_t i = 0; i < desc.numAttribs; ++i)
    {
        [objCDesc.get().attributes setObject:ToObjC(ANGLE_UNSAFE_TODO(desc.attributes[i]))
                          atIndexedSubscript:i];
    }

    for (uint8_t i = 0; i < desc.numBufferLayouts; ++i)
    {
        // Ignore if stepFunction is kVertexStepFunctionInvalid.
        // If we don't set this slot, it will apparently be disabled by metal runtime.
        if (ANGLE_UNSAFE_TODO(desc.layouts[i].getStepFunction()) != kVertexStepFunctionInvalid)
        {
            [objCDesc.get().layouts setObject:ToObjC(ANGLE_UNSAFE_TODO(desc.layouts[i]))
                           atIndexedSubscript:i];
        }
    }

    return objCDesc;
}

inline angle::ObjCPtr<MTLRenderPipelineColorAttachmentDescriptor> ToObjC(
    const RenderPipelineColorAttachmentDesc &desc)
{
    auto objCDesc = angle::adoptObjCPtr([[MTLRenderPipelineColorAttachmentDescriptor alloc] init]);
    objCDesc.get().pixelFormat                 = desc.getPixelFormat();
    objCDesc.get().writeMask                   = desc.getWriteMask();
    objCDesc.get().sourceRGBBlendFactor        = desc.getSourceRgbBlendFactor();
    objCDesc.get().sourceAlphaBlendFactor      = desc.getSourceAlphaBlendFactor();
    objCDesc.get().destinationRGBBlendFactor   = desc.getDestinationRgbBlendFactor();
    objCDesc.get().destinationAlphaBlendFactor = desc.getDestinationAlphaBlendFactor();
    objCDesc.get().rgbBlendOperation           = desc.getRgbBlendOperation();
    objCDesc.get().alphaBlendOperation         = desc.getAlphaBlendOperation();
    objCDesc.get().blendingEnabled             = desc.isBlendingEnabled();
    return objCDesc;
}

id<MTLTexture> ToObjC(const TextureRef &texture)
{
    auto textureRef = texture;
    return textureRef ? textureRef->get() : nil;
}

void BaseRenderPassAttachmentDescToObjC(const RenderPassAttachmentDesc &src,
                                        MTLRenderPassAttachmentDescriptor *dst)
{
    const TextureRef &implicitMsTexture = src.implicitMSTexture;

    if (implicitMsTexture)
    {
        dst.texture        = ToObjC(implicitMsTexture);
        dst.level          = 0;
        dst.slice          = 0;
        dst.depthPlane     = 0;
        dst.resolveTexture = ToObjC(src.texture);
        dst.resolveLevel   = src.level.get();
        if (dst.resolveTexture.textureType == MTLTextureType3D)
        {
            dst.resolveDepthPlane = src.sliceOrDepth;
            dst.resolveSlice      = 0;
        }
        else
        {
            dst.resolveSlice      = src.sliceOrDepth;
            dst.resolveDepthPlane = 0;
        }
    }
    else
    {
        dst.texture = ToObjC(src.texture);
        dst.level   = src.level.get();
        if (dst.texture.textureType == MTLTextureType3D)
        {
            dst.depthPlane = src.sliceOrDepth;
            dst.slice      = 0;
        }
        else
        {
            dst.slice      = src.sliceOrDepth;
            dst.depthPlane = 0;
        }
        dst.resolveTexture    = nil;
        dst.resolveLevel      = 0;
        dst.resolveSlice      = 0;
        dst.resolveDepthPlane = 0;
    }

    dst.loadAction         = src.loadAction;
    dst.storeAction        = src.storeAction;
    dst.storeActionOptions = src.storeActionOptions;
}

void ToObjC(const RenderPassColorAttachmentDesc &desc,
            MTLRenderPassColorAttachmentDescriptor *objCDesc)
{
    BaseRenderPassAttachmentDescToObjC(desc, objCDesc);
    objCDesc.clearColor = desc.clearColor;
}

void ToObjC(const RenderPassDepthAttachmentDesc &desc,
            MTLRenderPassDepthAttachmentDescriptor *objCDesc)
{
    BaseRenderPassAttachmentDescToObjC(desc, objCDesc);
    objCDesc.clearDepth = desc.clearDepth;
}

void ToObjC(const RenderPassStencilAttachmentDesc &desc,
            MTLRenderPassStencilAttachmentDescriptor *objCDesc)
{
    BaseRenderPassAttachmentDescToObjC(desc, objCDesc);
    objCDesc.clearStencil = desc.clearStencil;
}

}  // namespace

void DepthStencilDesc::updateDepthTestEnabled(const gl::DepthStencilState &dsState)
{
    if (!dsState.depthTest)
    {
        setDepthWriteDisabled();
    }
    else
    {
        updateDepthCompareFunc(dsState);
        updateDepthWriteEnabled(dsState);
    }
}

void DepthStencilDesc::updateDepthWriteEnabled(const gl::DepthStencilState &dsState)
{
    mDepthWriteEnabled = dsState.depthTest && dsState.depthMask;
}

void DepthStencilDesc::updateDepthCompareFunc(const gl::DepthStencilState &dsState)
{
    if (!dsState.depthTest)
    {
        return;
    }
    mDepthCompareFunction = static_cast<uint32_t>(GetCompareFunc(dsState.depthFunc));
}

void DepthStencilDesc::updateStencilTestEnabled(const gl::DepthStencilState &dsState)
{
    if (!dsState.stencilTest)
    {
        frontFaceStencil.setStencilCompareFunction(MTLCompareFunctionAlways);
        frontFaceStencil.setDepthFailureOperation(MTLStencilOperationKeep);
        frontFaceStencil.setDepthStencilPassOperation(MTLStencilOperationKeep);
        frontFaceStencil.writeMask = 0;

        backFaceStencil.setStencilCompareFunction(MTLCompareFunctionAlways);
        backFaceStencil.setDepthFailureOperation(MTLStencilOperationKeep);
        backFaceStencil.setDepthStencilPassOperation(MTLStencilOperationKeep);
        backFaceStencil.writeMask                 = 0;
    }
    else
    {
        updateStencilFrontFuncs(dsState);
        updateStencilFrontOps(dsState);
        updateStencilFrontWriteMask(dsState);
        updateStencilBackFuncs(dsState);
        updateStencilBackOps(dsState);
        updateStencilBackWriteMask(dsState);
    }
}

void DepthStencilDesc::updateStencilFrontOps(const gl::DepthStencilState &dsState)
{
    if (!dsState.stencilTest)
    {
        return;
    }
    frontFaceStencil.setStencilFailureOperation(GetStencilOp(dsState.stencilFail));
    frontFaceStencil.setDepthFailureOperation(GetStencilOp(dsState.stencilPassDepthFail));
    frontFaceStencil.setDepthStencilPassOperation(GetStencilOp(dsState.stencilPassDepthPass));
}

void DepthStencilDesc::updateStencilBackOps(const gl::DepthStencilState &dsState)
{
    if (!dsState.stencilTest)
    {
        return;
    }
    backFaceStencil.setStencilFailureOperation(GetStencilOp(dsState.stencilBackFail));
    backFaceStencil.setDepthFailureOperation(GetStencilOp(dsState.stencilBackPassDepthFail));
    backFaceStencil.setDepthStencilPassOperation(GetStencilOp(dsState.stencilBackPassDepthPass));
}

void DepthStencilDesc::updateStencilFrontFuncs(const gl::DepthStencilState &dsState)
{
    if (!dsState.stencilTest)
    {
        return;
    }
    frontFaceStencil.setStencilCompareFunction(GetCompareFunc(dsState.stencilFunc));
    frontFaceStencil.readMask = dsState.stencilMask & mtl::kStencilMaskAll;
}

void DepthStencilDesc::updateStencilBackFuncs(const gl::DepthStencilState &dsState)
{
    if (!dsState.stencilTest)
    {
        return;
    }
    backFaceStencil.setStencilCompareFunction(GetCompareFunc(dsState.stencilBackFunc));
    backFaceStencil.readMask = dsState.stencilBackMask & mtl::kStencilMaskAll;
}

void DepthStencilDesc::updateStencilFrontWriteMask(const gl::DepthStencilState &dsState)
{
    if (!dsState.stencilTest)
    {
        return;
    }
    frontFaceStencil.writeMask = dsState.stencilWritemask & mtl::kStencilMaskAll;
}

void DepthStencilDesc::updateStencilBackWriteMask(const gl::DepthStencilState &dsState)
{
    if (!dsState.stencilTest)
    {
        return;
    }
    backFaceStencil.writeMask = dsState.stencilBackWritemask & mtl::kStencilMaskAll;
}

SamplerDesc::SamplerDesc(const gl::SamplerState &glState)
    : mRAddressMode(static_cast<uint32_t>(GetSamplerAddressMode(glState.getWrapR()))),
      mSAddressMode(static_cast<uint32_t>(GetSamplerAddressMode(glState.getWrapS()))),
      mTAddressMode(static_cast<uint32_t>(GetSamplerAddressMode(glState.getWrapT()))),
      mMinFilter(static_cast<uint32_t>(GetFilter(glState.getMinFilter()))),
      mMagFilter(static_cast<uint32_t>(GetFilter(glState.getMagFilter()))),
      mMipFilter(static_cast<uint32_t>(GetMipmapFilter(glState.getMinFilter()))),
      mMaxAnisotropy(static_cast<uint32_t>(glState.getMaxAnisotropy())),
      mCompareFunction(static_cast<uint32_t>(GetCompareFunc(glState.getCompareFunc())))
{
}

void BlendDesc::updateWriteMask(uint8_t angleMask)
{
    ASSERT(angleMask == (angleMask & 0xF));

// ANGLE's packed color mask is abgr (matches Vulkan & D3D11), while Metal expects rgba.
#if defined(__aarch64__)
    // ARM64 can reverse bits in a single instruction
    mWriteMask = __builtin_bitreverse8(angleMask) >> 4;
#else
    /* On other architectures, Clang generates a polyfill that uses more
       instructions than the following expression optimized for a 4-bit value.

       (abgr * 0x41) & 0x14A:
        .......abgr +
        .abgr...... &
        00101001010 =
        ..b.r..a.g.

       (b.r..a.g.) * 0x111:
                b.r..a.g. +
            b.r..a.g..... +
        b.r..a.g......... =
        b.r.bargbarg.a.g.
              ^^^^
    */
    mWriteMask = ((((angleMask * 0x41) & 0x14A) * 0x111) >> 7) & 0xF;
#endif
}

void RenderPipelineColorAttachmentDesc::reset(MTLPixelFormat format, MTLColorWriteMask writeMask)
{
    // Reset the entire BlendDesc to defaults (blendingEnabled=false, default factors/operations),
    // then set the specific values.
    BlendDesc::operator=({});
    mWriteMask   = static_cast<uint32_t>(writeMask);
    mPixelFormat = static_cast<uint32_t>(format);
}

void RenderPipelineColorAttachmentDesc::reset(MTLPixelFormat format, const BlendDesc &blendDesc)
{
    BlendDesc::operator=(blendDesc);
    mPixelFormat = static_cast<uint32_t>(format);
}

bool RenderPipelineOutputDesc::operator==(const RenderPipelineOutputDesc &rhs) const
{
    if (mNumColorAttachments != rhs.mNumColorAttachments)
    {
        return false;
    }

    for (uint8_t i = 0; i < mNumColorAttachments; ++i)
    {
        if (colorAttachments[i] != rhs.colorAttachments[i])
        {
            return false;
        }
    }

    return mDepthAttachmentPixelFormat == rhs.mDepthAttachmentPixelFormat &&
           mStencilAttachmentPixelFormat == rhs.mStencilAttachmentPixelFormat &&
           mRasterSampleCount == rhs.mRasterSampleCount;
}

size_t RenderPipelineOutputDesc::hash() const
{
    size_t hash = 0;
    angle::HashCombine(hash, mNumColorAttachments);
    for (uint8_t i = 0; i < mNumColorAttachments; ++i)
    {
        angle::HashCombine(hash, colorAttachments[i]);
    }
    angle::HashCombine(hash, mDepthAttachmentPixelFormat);
    angle::HashCombine(hash, mStencilAttachmentPixelFormat);
    angle::HashCombine(hash, mRasterSampleCount);
    return hash;
}

void RenderPipelineOutputDesc::updateEnabledDrawBuffers(gl::DrawBufferMask enabledBuffers)
{
    for (uint32_t colorIndex = 0; colorIndex < mNumColorAttachments; ++colorIndex)
    {
        if (!enabledBuffers.test(colorIndex))
        {
            colorAttachments[colorIndex].setWriteMask(MTLColorWriteMaskNone);
        }
    }
}

bool RenderPipelineDesc::rasterizationEnabled() const
{
    return mRasterizationType != static_cast<uint32_t>(RenderPipelineRasterization::Disabled);
}

angle::ObjCPtr<MTLRenderPipelineDescriptor> RenderPipelineDesc::createMetalDesc(
    id<MTLFunction> vertexShader,
    id<MTLFunction> fragmentShader) const
{
    auto objCDesc = angle::adoptObjCPtr([[MTLRenderPipelineDescriptor alloc] init]);
    [objCDesc reset];

    objCDesc.get().vertexDescriptor = ToObjC(vertexDescriptor);

    for (uint8_t i = 0; i < outputDescriptor.getNumColorAttachments(); ++i)
    {
        [objCDesc.get().colorAttachments setObject:ToObjC(outputDescriptor.colorAttachments[i])
                                atIndexedSubscript:i];
    }
    objCDesc.get().depthAttachmentPixelFormat = outputDescriptor.getDepthAttachmentPixelFormat();
    objCDesc.get().stencilAttachmentPixelFormat =
        outputDescriptor.getStencilAttachmentPixelFormat();
    objCDesc.get().rasterSampleCount      = outputDescriptor.getRasterSampleCount();
    objCDesc.get().inputPrimitiveTopology = getInputPrimitiveTopology();
    objCDesc.get().alphaToCoverageEnabled = getAlphaToCoverageEnabled();

    // rasterizationEnabled will be true for both EmulatedDiscard & Enabled.
    objCDesc.get().rasterizationEnabled = rasterizationEnabled();

    objCDesc.get().vertexFunction   = vertexShader;
    objCDesc.get().fragmentFunction = objCDesc.get().rasterizationEnabled ? fragmentShader : nil;

    return objCDesc;
}

bool RenderPassAttachmentDesc::equalIgnoreLoadStoreOptions(
    const RenderPassAttachmentDesc &other) const
{
    return texture == other.texture && implicitMSTexture == other.implicitMSTexture &&
           level == other.level && sliceOrDepth == other.sliceOrDepth &&
           blendable == other.blendable;
}

bool RenderPassAttachmentDesc::operator==(const RenderPassAttachmentDesc &other) const
{
    if (!equalIgnoreLoadStoreOptions(other))
    {
        return false;
    }

    return loadAction == other.loadAction && storeAction == other.storeAction &&
           storeActionOptions == other.storeActionOptions;
}

void RenderPassDesc::populateRenderPipelineOutputDesc(RenderPipelineOutputDesc *outDesc) const
{
    WriteMaskArray writeMaskArray;
    writeMaskArray.fill(MTLColorWriteMaskAll);
    populateRenderPipelineOutputDesc(writeMaskArray, outDesc);
}

void RenderPassDesc::populateRenderPipelineOutputDesc(const WriteMaskArray &writeMaskArray,
                                                      RenderPipelineOutputDesc *outDesc) const
{
    // Default blend state with replaced color write masks.
    BlendDescArray blendDescArray;
    for (size_t i = 0; i < blendDescArray.size(); i++)
    {
        blendDescArray[i].reset(writeMaskArray[i]);
    }
    populateRenderPipelineOutputDesc(blendDescArray, outDesc);
}

void RenderPassDesc::populateRenderPipelineOutputDesc(const BlendDescArray &blendDescArray,
                                                      RenderPipelineOutputDesc *outDesc) const
{
    outDesc->setNumColorAttachments(numColorAttachments);
    outDesc->setRasterSampleCount(rasterSampleCount);
    for (uint32_t i = 0; i < this->numColorAttachments; ++i)
    {
        auto &renderPassColorAttachment = colorAttachments[i];
        auto texture                    = renderPassColorAttachment.texture;
        auto &outColorAttachment        = outDesc->colorAttachments[i];
        if (texture)
        {
            if (renderPassColorAttachment.blendable &&
                blendDescArray[i].getWriteMask() != MTLColorWriteMaskNone)
            {
                // Copy parameters from blend state
                outColorAttachment.reset(texture->pixelFormat(), blendDescArray[i]);
            }
            else
            {
                // Disable blending if the attachment's render target doesn't support blending
                // or if all its color channels are masked out. The latter is needed because:
                //
                // * When blending is enabled and *Source1* blend factors are used, Metal
                //   requires a fragment shader to bind both primary and secondary outputs
                //
                // * ANGLE frontend validation allows draw calls on draw buffers without
                //   bound fragment outputs if all their color channels are masked out
                //
                // * When all color channels are masked out, blending has no effect anyway
                //
                // Besides disabling blending, use default values for factors and
                // operations to reduce the number of unique pipeline states.
                outColorAttachment.reset(texture->pixelFormat(), blendDescArray[i].getWriteMask());
            }

            // Combine the masks. This is useful when the texture is not supposed to have alpha
            // channel such as GL_RGB8, however, Metal doesn't natively support 24 bit RGB, so
            // we need to use RGBA texture, and then disable alpha write to this texture
            outColorAttachment.setWriteMask(outColorAttachment.getWriteMask() &
                                            texture->getColorWritableMask());
        }
        else
        {

            outColorAttachment.setBlendingDisabled();
            outColorAttachment.setPixelFormat(MTLPixelFormatInvalid);
        }
    }

    // Reset the unused output slots to ensure consistent hash value
    for (uint32_t i = this->numColorAttachments; i < outDesc->colorAttachments.size(); ++i)
    {
        outDesc->colorAttachments[i] = {};
    }

    auto depthTexture = depthAttachment.texture;
    outDesc->setDepthAttachmentPixelFormat(depthTexture ? depthTexture->pixelFormat()
                                                        : MTLPixelFormatInvalid);

    auto stencilTexture = stencilAttachment.texture;
    outDesc->setStencilAttachmentPixelFormat(stencilTexture ? stencilTexture->pixelFormat()
                                                            : MTLPixelFormatInvalid);
}

bool RenderPassDesc::equalIgnoreLoadStoreOptions(const RenderPassDesc &other) const
{
    if (numColorAttachments != other.numColorAttachments)
    {
        return false;
    }

    for (uint32_t i = 0; i < numColorAttachments; ++i)
    {
        auto &renderPassColorAttachment = colorAttachments[i];
        auto &otherRPAttachment         = other.colorAttachments[i];
        if (!renderPassColorAttachment.equalIgnoreLoadStoreOptions(otherRPAttachment))
        {
            return false;
        }
    }

    if (defaultWidth != other.defaultWidth || defaultHeight != other.defaultHeight)
    {
        return false;
    }

    if (rasterSampleCount != other.rasterSampleCount)
    {
        return false;
    }

    return depthAttachment.equalIgnoreLoadStoreOptions(other.depthAttachment) &&
           stencilAttachment.equalIgnoreLoadStoreOptions(other.stencilAttachment);
}

bool RenderPassDesc::operator==(const RenderPassDesc &other) const
{
    if (numColorAttachments != other.numColorAttachments)
    {
        return false;
    }

    for (uint32_t i = 0; i < numColorAttachments; ++i)
    {
        if (colorAttachments[i] != other.colorAttachments[i])
        {
            return false;
        }
    }
    return depthAttachment == other.depthAttachment &&
           stencilAttachment == other.stencilAttachment && defaultWidth == other.defaultWidth &&
           defaultHeight == other.defaultHeight && rasterSampleCount == other.rasterSampleCount;
}

// Convert to Metal object
void RenderPassDesc::convertToMetalDesc(MTLRenderPassDescriptor *objCDesc,
                                        uint32_t deviceMaxRenderTargets) const
{
    ASSERT(deviceMaxRenderTargets <= kMaxRenderTargets);

    for (uint32_t i = 0; i < numColorAttachments; ++i)
    {
        ToObjC(colorAttachments[i], objCDesc.colorAttachments[i]);
    }
    for (uint32_t i = numColorAttachments; i < deviceMaxRenderTargets; ++i)
    {
        // Inactive render target
        ToObjC(RenderPassColorAttachmentDesc{}, objCDesc.colorAttachments[i]);
    }

    ToObjC(depthAttachment, objCDesc.depthAttachment);
    ToObjC(stencilAttachment, objCDesc.stencilAttachment);

    if ((defaultWidth | defaultHeight) != 0)
    {
        objCDesc.renderTargetWidth        = defaultWidth;
        objCDesc.renderTargetHeight       = defaultHeight;
        objCDesc.defaultRasterSampleCount = 1;
    }
    else
    {
        objCDesc.renderTargetWidth  = 0;
        objCDesc.renderTargetHeight = 0;
        // No need to reset defaultRasterSampleCount as it is only applied when no attachments are
        // provided.
    }
}

// StateCache implementation
StateCache::StateCache(const angle::FeaturesMtl &features) : mFeatures(features) {}

StateCache::~StateCache() {}

angle::ObjCPtr<id<MTLDepthStencilState>> StateCache::getNullDepthStencilState(
    const mtl::ContextDevice &device)
{
    if (!mNullDepthStencilState)
    {
        DepthStencilDesc desc(MTLCompareFunctionAlways, false);
        ASSERT(desc.frontFaceStencil.getStencilCompareFunction() == MTLCompareFunctionAlways);
        mNullDepthStencilState = getDepthStencilState(device, desc);
    }
    return mNullDepthStencilState;
}

angle::ObjCPtr<id<MTLDepthStencilState>> StateCache::getDepthStencilState(
    const mtl::ContextDevice &device,
    const DepthStencilDesc &desc)
{
    auto ite = mDepthStencilStates.find(desc);
    if (ite == mDepthStencilStates.end())
    {
        auto re = mDepthStencilStates.insert(
            std::make_pair(desc, device.newDepthStencilStateWithDescriptor(ToObjC(desc))));
        if (!re.second)
        {
            return nil;
        }

        ite = re.first;
    }

    return ite->second;
}

angle::ObjCPtr<id<MTLSamplerState>> StateCache::getSamplerState(const mtl::ContextDevice &device,
                                                                const SamplerDesc &desc)
{
    auto ite = mSamplerStates.find(desc);
    if (ite == mSamplerStates.end())
    {
        auto objCDesc = ToObjC(desc);
        if (!mFeatures.allowRuntimeSamplerCompareMode.enabled)
        {
            // Runtime sampler compare mode is not supported, fallback to never.
            objCDesc.get().compareFunction = MTLCompareFunctionNever;
        }
        auto re = mSamplerStates.insert(
            std::make_pair(desc, device.newSamplerStateWithDescriptor(objCDesc)));
        if (!re.second)
            return nil;

        ite = re.first;
    }

    return ite->second;
}

angle::ObjCPtr<id<MTLSamplerState>> StateCache::getNullSamplerState(ContextMtl *context)
{
    return getNullSamplerState(context->getMetalDevice());
}

angle::ObjCPtr<id<MTLSamplerState>> StateCache::getNullSamplerState(
    const mtl::ContextDevice &device)
{
    SamplerDesc desc;
    return getSamplerState(device, desc);
}

void StateCache::clear()
{
    mNullDepthStencilState = nil;
    mDepthStencilStates.clear();
    mSamplerStates.clear();
}

}  // namespace mtl
}  // namespace rx
