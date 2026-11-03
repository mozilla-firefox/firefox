//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// TextureGL.cpp: Implements the class methods for TextureGL.

#include "libANGLE/renderer/gl/TextureGL.h"
#include "common/mathutil.h"
#include "common/unsafe_buffers.h"

#include "common/bitset_utils.h"
#include "common/debug.h"
#include "common/mathutil.h"
#include "common/utilities.h"
#include "libANGLE/Context.h"
#include "libANGLE/Display.h"
#include "libANGLE/MemoryObject.h"
#include "libANGLE/State.h"
#include "libANGLE/Surface.h"
#include "libANGLE/angletypes.h"
#include "libANGLE/formatutils.h"
#include "libANGLE/queryconversions.h"
#include "libANGLE/renderer/gl/BlitGL.h"
#include "libANGLE/renderer/gl/BufferGL.h"
#include "libANGLE/renderer/gl/ContextGL.h"
#include "libANGLE/renderer/gl/DisplayGL.h"
#include "libANGLE/renderer/gl/FramebufferGL.h"
#include "libANGLE/renderer/gl/FunctionsGL.h"
#include "libANGLE/renderer/gl/ImageGL.h"
#include "libANGLE/renderer/gl/MemoryObjectGL.h"
#include "libANGLE/renderer/gl/StateManagerGL.h"
#include "libANGLE/renderer/gl/SurfaceGL.h"
#include "libANGLE/renderer/gl/formatutilsgl.h"
#include "libANGLE/renderer/gl/renderergl_utils.h"
#include "platform/autogen/FeaturesGL_autogen.h"

using angle::CheckedNumeric;

namespace rx
{

namespace
{
GLuint GetMaxMipmapLevel(const gl::Caps &caps, gl::TextureType target)
{
    switch (target)
    {
        case gl::TextureType::_2D:
        case gl::TextureType::_2DArray:
            return static_cast<GLuint>(gl::log2(caps.max2DTextureSize));
        case gl::TextureType::_3D:
            return static_cast<GLuint>(gl::log2(caps.max3DTextureSize));
        case gl::TextureType::CubeMap:
        case gl::TextureType::CubeMapArray:
            return static_cast<GLuint>(gl::log2(caps.maxCubeMapTextureSize));
        default:
            return 0u;
    }
}

// For use with the uploadTextureDataInChunks feature.  See http://crbug.com/1181068
constexpr const size_t kUploadTextureDataInChunksUploadSize = (120 * 1024) - 1;

size_t GetLevelInfoIndex(gl::TextureTarget target, size_t level)
{
    return gl::IsCubeMapFaceTarget(target)
               ? ((level * gl::kCubeFaceCount) + gl::CubeMapTextureTargetToFaceIndex(target))
               : level;
}

bool IsLUMAFormat(GLenum format)
{
    return format == GL_LUMINANCE || format == GL_ALPHA || format == GL_LUMINANCE_ALPHA;
}

LUMAWorkaroundGL GetLUMAWorkaroundInfo(GLenum originalFormat, GLenum destinationFormat)
{
    if (IsLUMAFormat(originalFormat))
    {
        return LUMAWorkaroundGL(!IsLUMAFormat(destinationFormat), destinationFormat);
    }
    else
    {
        return LUMAWorkaroundGL(false, GL_NONE);
    }
}

bool GetDepthStencilWorkaround(GLenum format)
{
    return format == GL_DEPTH_COMPONENT || format == GL_DEPTH_STENCIL;
}

bool GetEmulatedAlphaChannel(const angle::FeaturesGL &features,
                             const gl::InternalFormat &originalInternalFormat)
{
    return (features.RGBDXT1TexturesSampleZeroAlpha.enabled &&
            (originalInternalFormat.sizedInternalFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ||
             originalInternalFormat.sizedInternalFormat == GL_COMPRESSED_SRGB_S3TC_DXT1_EXT)) ||
           (features.emulateRGB10.enabled && originalInternalFormat.format == GL_RGB &&
            originalInternalFormat.type == GL_UNSIGNED_INT_2_10_10_10_REV_EXT);
}

LevelInfoGL GetLevelInfo(const angle::FeaturesGL &features,
                         const gl::InternalFormat &originalInternalFormat,
                         GLenum destinationInternalFormat)
{
    GLenum destinationFormat = gl::GetUnsizedFormat(destinationInternalFormat);
    return LevelInfoGL(originalInternalFormat.format, destinationInternalFormat,
                       GetDepthStencilWorkaround(originalInternalFormat.format),
                       GetLUMAWorkaroundInfo(originalInternalFormat.format, destinationFormat),
                       GetEmulatedAlphaChannel(features, originalInternalFormat));
}

gl::Texture::DirtyBits GetLevelWorkaroundDirtyBits()
{
    gl::Texture::DirtyBits bits;
    bits.set(gl::Texture::DIRTY_BIT_MIN_FILTER);
    bits.set(gl::Texture::DIRTY_BIT_MAG_FILTER);
    bits.set(gl::Texture::DIRTY_BIT_SWIZZLE_RED);
    bits.set(gl::Texture::DIRTY_BIT_SWIZZLE_GREEN);
    bits.set(gl::Texture::DIRTY_BIT_SWIZZLE_BLUE);
    bits.set(gl::Texture::DIRTY_BIT_SWIZZLE_ALPHA);
    return bits;
}

size_t GetMaxLevelInfoCountForTextureType(gl::TextureType type)
{
    switch (type)
    {
        case gl::TextureType::CubeMap:
            return (gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS + 1) * gl::kCubeFaceCount;

        case gl::TextureType::External:
            return 1;

        default:
            return gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS + 1;
    }
}

bool FormatHasBorderColorWorkarounds(GLenum format)
{
    switch (format)
    {
        case GL_ALPHA:
        case GL_LUMINANCE_ALPHA:
            return true;
        default:
            return false;
    }
}

angle::Result GetDepthOneFilledBuffer(const gl::Context *context,
                                      size_t imageSize,
                                      GLenum type,
                                      const angle::MemoryBuffer **bufferOut)
{
    ContextGL *contextGL               = GetImplAs<ContextGL>(context);
    angle::MemoryBuffer *scratchBuffer = nullptr;
    ANGLE_CHECK_GL_ALLOC(contextGL, context->getScratchBuffer(imageSize, &scratchBuffer));

    FillDepthOneMemory(type, scratchBuffer->first(imageSize));
    *bufferOut = scratchBuffer;
    return angle::Result::Continue;
}

}  // anonymous namespace

LUMAWorkaroundGL::LUMAWorkaroundGL() : LUMAWorkaroundGL(false, GL_NONE) {}

LUMAWorkaroundGL::LUMAWorkaroundGL(bool enabled_, GLenum workaroundFormat_)
    : enabled(enabled_), workaroundFormat(workaroundFormat_)
{}

LevelInfoGL::LevelInfoGL() : LevelInfoGL(GL_NONE, GL_NONE, false, LUMAWorkaroundGL(), false) {}

LevelInfoGL::LevelInfoGL(GLenum sourceFormat_,
                         GLenum nativeInternalFormat_,
                         bool depthStencilWorkaround_,
                         const LUMAWorkaroundGL &lumaWorkaround_,
                         bool emulatedAlphaChannel_)
    : sourceFormat(sourceFormat_),
      nativeInternalFormat(nativeInternalFormat_),
      depthStencilWorkaround(depthStencilWorkaround_),
      lumaWorkaround(lumaWorkaround_),
      emulatedAlphaChannel(emulatedAlphaChannel_)
{}

TextureGL::TextureGL(const gl::TextureState &state, GLuint id)
    : TextureImpl(state),
      mAppliedSwizzle(state.getSwizzleState()),
      mAppliedSampler(state.getSamplerState()),
      mAppliedBaseLevel(state.getEffectiveBaseLevel()),
      mAppliedMaxLevel(state.getEffectiveMaxLevel()),
      mAppliedDepthStencilTextureMode(state.getDepthStencilTextureMode()),
      mTextureID(id)
{
    mLevelInfo.resize(GetMaxLevelInfoCountForTextureType(getType()));
}

TextureGL::~TextureGL()
{
    ASSERT(mTextureID == 0);
}

void TextureGL::onDestroy(const gl::Context *context)
{
    GetImplAs<ContextGL>(context)->flushIfNecessaryBeforeDeleteTextures();
    clearVirtualLevels(context);
    StateManagerGL *stateManager = GetStateManagerGL(context);
    stateManager->deleteTexture(mTextureID);
    mTextureID = 0;
}

bool TextureGL::isVirtualMipchainType() const
{
    switch (getType())
    {
        case gl::TextureType::_2D:
        case gl::TextureType::CubeMap:
        case gl::TextureType::_2DArray:
        case gl::TextureType::_3D:
        case gl::TextureType::CubeMapArray:
            return true;
        default:
            return false;
    }
}

TextureGL::NativeLevelTarget TextureGL::getNativeLevelTarget(gl::TextureTarget target,
                                                             size_t level,
                                                             bool invalidateCpuData) const
{
    if (!mVirtualMipChain.has_value())
    {
        return {mTextureID, target, static_cast<GLint>(level)};
    }
    size_t idx = GetLevelInfoIndex(target, level);
    ASSERT(idx < mVirtualMipChain->virtualLevels.size());
    const VirtualLevelInfo &vLevel = mVirtualMipChain->virtualLevels[idx];
    if (invalidateCpuData)
    {
        vLevel.tailMip3DCpuData.clearCpuData();
    }
    // If this virtual level currently resides in a standalone 1-level companion texture (because
    // it was defined or redefined outside the contiguous mipchain in mTextureID), return that
    // companion texture at native level 0.
    if (vLevel.texID != 0)
    {
        gl::TextureTarget levelTarget =
            (getType() == gl::TextureType::CubeMap) ? gl::TextureTarget::_2D : target;
        return {vLevel.texID, levelTarget, 0};
    }
    // Otherwise, if this virtual level is part of the contiguous mipchain [baseLevel, baseLevel +
    // levels) currently allocated in mTextureID, return mTextureID at the shifted native level
    // (level - baseLevel).
    if (mVirtualMipChain->levels > 0 && level >= mVirtualMipChain->baseLevel &&
        level < mVirtualMipChain->baseLevel + mVirtualMipChain->levels)
    {
        return {mTextureID, target, static_cast<GLint>(level - mVirtualMipChain->baseLevel)};
    }
    // If the requested virtual level has not been defined (or was defined with empty 0x0 size),
    // fall back to native level 0 of mTextureID. We must return 0 rather than
    // mVirtualMipChain->levels because mTextureID is allocated with glTexStorage* (making it an
    // immutable-format texture in the native driver), and in OpenGL ES 3.1+ calling native
    // glFramebufferTexture2D with level >= GL_TEXTURE_IMMUTABLE_LEVELS generates GL_INVALID_VALUE.
    // Frontend framebuffer completeness checking (Framebuffer::checkStatusWithGLFrontEnd) inspects
    // gl::TextureState and marks the FBO incomplete while the virtual level is undefined,
    // preventing any rendering to native level 0. If the application later defines this virtual
    // level before rendering, ObjectReallocated / SubjectChanged notifications mark any attached
    // FBOs dirty so FramebufferGL::syncState will re-query getNativeLevelTarget and bind the newly
    // allocated level.
    return {mTextureID, target, 0};
}

void TextureGL::clearVirtualLevels(const gl::Context *context)
{
    if (!mVirtualMipChain.has_value())
    {
        return;
    }
    StateManagerGL *stateManager = GetStateManagerGL(context);
    for (VirtualLevelInfo &level : mVirtualMipChain->virtualLevels)
    {
        stateManager->deleteTexture(level.texID);
        level = VirtualLevelInfo();
    }
}

angle::Result TextureGL::allocateTexStorage(const gl::Context *context,
                                            GLuint texID,
                                            gl::TextureType type,
                                            size_t levels,
                                            GLenum internalFormat,
                                            const gl::Extents &size)
{
    ContextGL *contextGL              = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    stateManager->bindTexture(type, texID);
    if (nativegl::UseTexImage2D(type))
    {
        ASSERT(size.depth == 1);
        ANGLE_GL_TRY_ALWAYS_CHECK(
            context, functions->texStorage2D(ToGLenum(type), static_cast<GLsizei>(levels),
                                             internalFormat, size.width, size.height));
    }
    else
    {
        ASSERT(nativegl::UseTexImage3D(type));
        const gl::InternalFormat &internalFormatInfo =
            gl::GetSizedInternalFormatInfo(internalFormat);
        const bool bypassTexStorage3D = type == gl::TextureType::_3D &&
                                        internalFormatInfo.compressed &&
                                        features.emulateImmutableCompressedTexture3D.enabled;
        if (functions->texStorage3D && !bypassTexStorage3D)
        {
            ANGLE_GL_TRY_ALWAYS_CHECK(
                context,
                functions->texStorage3D(ToGLenum(type), static_cast<GLsizei>(levels),
                                        internalFormat, size.width, size.height, size.depth));
        }
        else
        {
            GLuint prevUnpackBuffer = stateManager->getBufferID(gl::BufferBinding::PixelUnpack);
            stateManager->bindBuffer(gl::BufferBinding::PixelUnpack, 0,
                                     context->isHardenedContext());
            for (GLsizei i = 0; i < static_cast<GLsizei>(levels); i++)
            {
                gl::Extents levelSize(
                    std::max(size.width >> i, 1), std::max(size.height >> i, 1),
                    type == gl::TextureType::_3D ? std::max(size.depth >> i, 1) : size.depth);
                if (internalFormatInfo.compressed)
                {
                    GLuint dataSize = 0;
                    ANGLE_CHECK_GL_MATH(contextGL, internalFormatInfo.computeCompressedImageSize(
                                                       levelSize, &dataSize));
                    ANGLE_GL_TRY_ALWAYS_CHECK(
                        context,
                        functions->compressedTexImage3D(
                            ToGLenum(type), i, internalFormat, levelSize.width, levelSize.height,
                            levelSize.depth, 0, static_cast<GLsizei>(dataSize), nullptr));
                }
                else
                {
                    ANGLE_GL_TRY_ALWAYS_CHECK(
                        context, functions->texImage3D(
                                     ToGLenum(type), i, internalFormat, levelSize.width,
                                     levelSize.height, levelSize.depth, 0,
                                     internalFormatInfo.format, internalFormatInfo.type, nullptr));
                }
            }
            stateManager->bindBuffer(gl::BufferBinding::PixelUnpack, prevUnpackBuffer,
                                     context->isHardenedContext());
        }
    }
    return angle::Result::Continue;
}

angle::Result TextureGL::copyBetweenNativeLevels(const gl::Context *context,
                                                 GLuint srcTexID,
                                                 gl::TextureTarget srcTarget,
                                                 GLint srcLevel,
                                                 GLuint dstTexID,
                                                 gl::TextureTarget dstTarget,
                                                 GLint dstLevel,
                                                 const VirtualLevelInfo &dstVLevel)
{
    const FunctionsGL *functions      = GetFunctionsGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);
    ASSERT(functions->copyImageSubData != nullptr);

    const gl::Extents &size = dstVLevel.size;
    if (size.empty())
    {
        return angle::Result::Continue;
    }

    // PowerVR's glCopyImageSubDataEXT validates the source/destination mipmap level count of
    // GL_TEXTURE_3D textures using only 2D (width, height) dimensions (floor(log2(max(w, h))) + 1)
    // instead of 3D (width, height, depth) dimensions. When a 3D texture has depth > max(w, h)
    // (such as 63x57x67 in dEQP-GLES3.functional.texture.filtering.3d.combinations.*), any tail
    // mip level where width and height have already reached 1x1 (e.g., level 6 of 63x57x67) is
    // rejected by glCopyImageSubDataEXT with GL_INVALID_VALUE. Avoid glCopyImageSubDataEXT for
    // non-zero 1x1xD 3D levels by either re-uploading the cached CPU texel data via glTexSubImage3D
    // or copying slice-by-slice via a temporary read FBO and glCopyTexSubImage3D.
    if (features.copyImageSubData3DFailsOnTailMipLevels.enabled &&
        (srcTarget == gl::TextureTarget::_3D || dstTarget == gl::TextureTarget::_3D) &&
        (srcLevel > 0 || dstLevel > 0) && size.width == 1 && size.height == 1)
    {
        StateManagerGL *stateManager    = GetStateManagerGL(context);
        const TailMip3DCpuData &cpuInfo = dstVLevel.tailMip3DCpuData;
        if (!cpuInfo.data.empty() &&
            cpuInfo.contiguousSlicesWritten == static_cast<size_t>(size.depth))
        {
            gl::PixelUnpackState unpackState;
            unpackState.alignment = 1;
            ANGLE_TRY(stateManager->setPixelUnpackState(context, unpackState));
            GLuint prevUnpackBuffer = stateManager->getBufferID(gl::BufferBinding::PixelUnpack);
            stateManager->bindBuffer(gl::BufferBinding::PixelUnpack, 0,
                                     context->isHardenedContext());
            stateManager->bindTexture(gl::TextureType::_3D, dstTexID);
            if (cpuInfo.compressed)
            {
                ANGLE_GL_TRY(context,
                             functions->compressedTexSubImage3D(
                                 ToGLenum(dstTarget), dstLevel, 0, 0, 0, size.width, size.height,
                                 size.depth, cpuInfo.format,
                                 static_cast<GLsizei>(cpuInfo.data.size()), cpuInfo.data.data()));
            }
            else
            {
                ANGLE_GL_TRY(context, functions->texSubImage3D(ToGLenum(dstTarget), dstLevel, 0, 0,
                                                               0, size.width, size.height,
                                                               size.depth, cpuInfo.format,
                                                               cpuInfo.type, cpuInfo.data.data()));
            }
            ANGLE_TRY(
                stateManager->setPixelUnpackState(context, context->getState().getUnpackState()));
            stateManager->bindBuffer(gl::BufferBinding::PixelUnpack, prevUnpackBuffer,
                                     context->isHardenedContext());
            return angle::Result::Continue;
        }

        const GLenum fboTarget   = stateManager->getHasSeparateFramebufferBindings()
                                       ? GL_READ_FRAMEBUFFER
                                       : GL_FRAMEBUFFER;
        const GLuint prevReadFBO = stateManager->getFramebufferID(angle::FramebufferBindingRead);
        GLuint tempFBO           = 0;
        ANGLE_GL_TRY(context, functions->genFramebuffers(1, &tempFBO));
        stateManager->bindFramebuffer(fboTarget, tempFBO);
        stateManager->bindTexture(gl::TextureType::_3D, dstTexID);
        for (GLint z = 0; z < size.depth; ++z)
        {
            ANGLE_GL_TRY(context, functions->framebufferTextureLayer(
                                      fboTarget, GL_COLOR_ATTACHMENT0, srcTexID, srcLevel, z));
            ANGLE_GL_TRY(context, functions->copyTexSubImage3D(ToGLenum(dstTarget), dstLevel, 0, 0,
                                                               z, 0, 0, size.width, size.height));
        }
        stateManager->bindFramebuffer(fboTarget, prevReadFBO);
        stateManager->deleteFramebuffer(tempFBO);
        return angle::Result::Continue;
    }

    GLenum srcCopyTarget = ToGLenum(srcTarget);
    GLint srcZ           = 0;
    if (gl::IsCubeMapFaceTarget(srcTarget))
    {
        srcCopyTarget = GL_TEXTURE_CUBE_MAP;
        srcZ          = static_cast<GLint>(gl::CubeMapTextureTargetToFaceIndex(srcTarget));
    }

    GLenum dstCopyTarget = ToGLenum(dstTarget);
    GLint dstZ           = 0;
    if (gl::IsCubeMapFaceTarget(dstTarget))
    {
        dstCopyTarget = GL_TEXTURE_CUBE_MAP;
        dstZ          = static_cast<GLint>(gl::CubeMapTextureTargetToFaceIndex(dstTarget));
    }

    ANGLE_GL_TRY(context, functions->copyImageSubData(srcTexID, srcCopyTarget, srcLevel, 0, 0, srcZ,
                                                      dstTexID, dstCopyTarget, dstLevel, 0, 0, dstZ,
                                                      size.width, size.height, size.depth));

    return angle::Result::Continue;
}

angle::Result TextureGL::updateVirtualLevel3D1x1CpuData(const gl::Context *context,
                                                        size_t level,
                                                        const gl::Box &area,
                                                        GLenum format,
                                                        GLenum type,
                                                        GLenum nativeFormat,
                                                        GLenum nativeType,
                                                        const gl::PixelUnpackState &unpack,
                                                        bool hasUnpackBuffer,
                                                        const uint8_t *pixels)
{
    const angle::FeaturesGL &features = GetFeaturesGL(context);
    if (!features.copyImageSubData3DFailsOnTailMipLevels.enabled || !mVirtualMipChain.has_value() ||
        getType() != gl::TextureType::_3D)
    {
        return angle::Result::Continue;
    }

    size_t idx                = GetLevelInfoIndex(gl::TextureTarget::_3D, level);
    VirtualLevelInfo &vLevel  = mVirtualMipChain->virtualLevels[idx];
    TailMip3DCpuData &cpuInfo = vLevel.tailMip3DCpuData;
    if (vLevel.size.width != 1 || vLevel.size.height != 1)
    {
        cpuInfo.clearCpuData();
        return angle::Result::Continue;
    }

    ASSERT(area.depth > 0);
    if (hasUnpackBuffer || pixels == nullptr || area.x != 0 || area.y != 0 || area.width != 1 ||
        area.height != 1 || area.z < 0 || area.z + area.depth > vLevel.size.depth ||
        (vLevel.texID == 0 && (mState.hasBeenBoundAsAttachment() || mState.hasBeenBoundAsImage())))
    {
        cpuInfo.clearCpuData();
        return angle::Result::Continue;
    }

    ContextGL *contextGL               = GetImplAs<ContextGL>(context);
    const gl::InternalFormat &glFormat = gl::GetInternalFormatInfo(format, type);
    const GLuint pixelBytes            = glFormat.computePixelBytes(type);
    if (pixelBytes == 0)
    {
        cpuInfo.clearCpuData();
        return angle::Result::Continue;
    }

    GLuint rowBytes   = 0;
    GLuint imageBytes = 0;
    GLuint skipBytes  = 0;
    ANGLE_CHECK_GL_MATH(contextGL, glFormat.computeRowDepthSkipBytes(
                                       type, area.width, area.height, unpack, /*is3D=*/true,
                                       &rowBytes, &imageBytes, &skipBytes));

    GLuint endByte = 0;
    ANGLE_CHECK_GL_MATH(contextGL, glFormat.computePackUnpackEndByte(
                                       type, gl::Extents(area.width, area.height, area.depth),
                                       unpack, /*is3D=*/true, &endByte));

    const size_t totalBytes = static_cast<size_t>(pixelBytes) * vLevel.size.depth;
    if (area.z == 0 && area.depth == vLevel.size.depth)
    {
        // Full-level upload covering all Z slices [0, vLevel.size.depth): allocate (or resize) the
        // CPU buffer and mark all slices as contiguously populated.
        cpuInfo.data.resize(totalBytes);
        cpuInfo.format                  = nativeFormat;
        cpuInfo.type                    = nativeType;
        cpuInfo.compressed              = false;
        cpuInfo.contiguousSlicesWritten = static_cast<size_t>(vLevel.size.depth);
    }
    else if (static_cast<size_t>(area.z) <= cpuInfo.contiguousSlicesWritten)
    {
        // Incremental upload starting within or immediately adjacent to the already-populated
        // prefix [0, contiguousSlicesWritten) (for example, uploading slices 0, 1, ..., D - 1 one
        // by one): initialize the CPU buffer if starting at slice 0, verify format/type match if
        // appending/overwriting, and extend the contiguous prefix to cover the new slice range.
        if (cpuInfo.data.empty())
        {
            ASSERT(area.z == 0);
            cpuInfo.data.resize(totalBytes, 0);
            cpuInfo.format     = nativeFormat;
            cpuInfo.type       = nativeType;
            cpuInfo.compressed = false;
        }
        else if (cpuInfo.compressed || cpuInfo.format != nativeFormat ||
                 cpuInfo.type != nativeType || cpuInfo.data.size() != totalBytes)
        {
            cpuInfo.clearCpuData();
            return angle::Result::Continue;
        }
        cpuInfo.contiguousSlicesWritten =
            std::max<size_t>(cpuInfo.contiguousSlicesWritten, area.z + area.depth);
    }
    else
    {
        // Out-of-order slice upload that leaves a gap of unwritten slices between
        // contiguousSlicesWritten and area.z: invalidate the CPU cache so copyBetweenNativeLevels
        // falls back to the FBO slice-by-slice copy path.
        cpuInfo.clearCpuData();
        return angle::Result::Continue;
    }

    // SAFETY: `pixels` points to client memory validated by the GL entry point (or `scratchBuffer`
    // in `setSubImage3DFromUnpackBufferViaCpu`) to hold at least `endByte` bytes for the 3D unpack
    // parameters.
    angle::Span<const uint8_t> srcSpan =
        ANGLE_UNSAFE_BUFFERS(angle::Span<const uint8_t>(pixels, endByte)).subspan(skipBytes);
    angle::Span<uint8_t> dstSpan(cpuInfo.data);
    for (int z = 0; z < area.depth; ++z)
    {
        angle::Span<const uint8_t> srcSlice =
            srcSpan.subspan(static_cast<size_t>(z) * imageBytes, pixelBytes);
        angle::Span<uint8_t> dstSlice =
            dstSpan.subspan(static_cast<size_t>(area.z + z) * pixelBytes, pixelBytes);
        std::copy(srcSlice.begin(), srcSlice.end(), dstSlice.begin());
    }

    return angle::Result::Continue;
}

bool TextureGL::shouldReadback3DPboDuringUpload(const gl::Context *context,
                                                const gl::InternalFormat &internalFormatInfo,
                                                GLenum format,
                                                GLenum type,
                                                const gl::PixelUnpackState &unpack) const
{
    const angle::FeaturesGL &features = GetFeaturesGL(context);
    if (!features.readback3DDepthStencilPbosDuringUpload.enabled)
    {
        return false;
    }

    // PowerVR has a race condition and offset calculation bug when glTexSubImage3D unpacks from a
    // PBO into an immutable 3D / 2D array texture. Since we do not have the driver source code to
    // know the exact internal predicate, we conservatively read back the PBO on the CPU for:
    // 1. Depth or stencil formats.
    // 2. Uploads with non-zero unpack sub-rectangle parameters (skipImages, imageHeight, skipRows,
    //    skipPixels, or rowLength).
    // 3. Uploads where the source format or type differs from the internal format's canonical
    //    format/type (e.g., uploading GL_FLOAT data into GL_R11F_G11F_B10F).
    // 4. 3-channel RGB / RGB_INTEGER formats (which GPU hardware expands to 4 channels during
    //    upload), LUMA formats, and 16-bit packed color formats (GL_RGBA4, GL_RGB5_A1, GL_RGB565).
    const bool isDepthOrStencil         = internalFormatInfo.isDepthOrStencil();
    const bool hasUnpackSubrect         = unpack.skipImages != 0 || unpack.imageHeight != 0 ||
                                          unpack.skipRows != 0 || unpack.skipPixels != 0 ||
                                          unpack.rowLength != 0;
    const bool isThreeChannelOrLuma     = internalFormatInfo.format == GL_RGB ||
                                          internalFormatInfo.format == GL_RGB_INTEGER ||
                                          internalFormatInfo.isLUMA();
    const bool isPacked16BitColor       = internalFormatInfo.sizedInternalFormat == GL_RGBA4 ||
                                          internalFormatInfo.sizedInternalFormat == GL_RGB5_A1 ||
                                          internalFormatInfo.sizedInternalFormat == GL_RGB565;
    const bool requiresFormatConversion = format != internalFormatInfo.format ||
                                          type != internalFormatInfo.type || isThreeChannelOrLuma ||
                                          isPacked16BitColor;

    return isDepthOrStencil || hasUnpackSubrect || requiresFormatConversion;
}

angle::Result TextureGL::setSubImage3DFromUnpackBufferViaCpu(const gl::Context *context,
                                                             const NativeLevelTarget &nlt,
                                                             size_t level,
                                                             const gl::Box &area,
                                                             GLenum format,
                                                             GLenum type,
                                                             GLenum nativeFormat,
                                                             GLenum nativeType,
                                                             const gl::PixelUnpackState &unpack,
                                                             const uint8_t *pixels)
{
    if (!area.valid())
    {
        return angle::Result::Continue;
    }

    ContextGL *contextGL               = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions       = GetFunctionsGL(context);
    StateManagerGL *stateManager       = GetStateManagerGL(context);
    const angle::FeaturesGL &features  = GetFeaturesGL(context);
    const gl::InternalFormat &glFormat = gl::GetInternalFormatInfo(format, type);

    GLuint rowBytes   = 0;
    GLuint imageBytes = 0;
    GLuint skipBytes  = 0;
    ANGLE_CHECK_GL_MATH(contextGL, glFormat.computeRowDepthSkipBytes(
                                       type, area.width, area.height, unpack, /*is3D=*/true,
                                       &rowBytes, &imageBytes, &skipBytes));

    GLuint endByte = 0;
    ANGLE_CHECK_GL_MATH(contextGL, glFormat.computePackUnpackEndByte(
                                       type, gl::Extents(area.width, area.height, area.depth),
                                       unpack, /*is3D=*/true, &endByte));
    ASSERT(endByte >= skipBytes);
    const size_t copyBytes   = static_cast<size_t>(endByte - skipBytes);
    const size_t startOffset = reinterpret_cast<uintptr_t>(pixels) + static_cast<size_t>(skipBytes);
    const size_t allocBytes =
        std::max<size_t>(copyBytes, static_cast<size_t>(area.depth) * imageBytes);

    angle::MemoryBuffer *scratchBuffer = nullptr;
    ANGLE_CHECK_GL_ALLOC(contextGL, context->getScratchBuffer(allocBytes, &scratchBuffer));

    const gl::Buffer *unpackBuffer =
        context->getState().getTargetBuffer(gl::BufferBinding::PixelUnpack);
    ASSERT(unpackBuffer != nullptr);
    ANGLE_TRY(stateManager->setPixelUnpackBuffer(context, unpackBuffer));

    if (copyBytes > 0)
    {
        const void *mappedPtr = nullptr;
        ANGLE_GL_TRY(context, mappedPtr = functions->mapBufferRange(
                                  GL_PIXEL_UNPACK_BUFFER, static_cast<GLintptr>(startOffset),
                                  static_cast<GLsizeiptr>(copyBytes), GL_MAP_READ_BIT));
        if (mappedPtr == nullptr)
        {
            return angle::Result::Stop;
        }
        // SAFETY: `scratchBuffer` is allocated with `allocBytes >= copyBytes`, and `mappedPtr`
        // points to `copyBytes` bytes mapped from `GL_PIXEL_UNPACK_BUFFER` by `glMapBufferRange`.
        ANGLE_UNSAFE_BUFFERS(memcpy(scratchBuffer->data(), mappedPtr, copyBytes));
        ANGLE_GL_TRY(context, functions->unmapBuffer(GL_PIXEL_UNPACK_BUFFER));
    }

    gl::PixelUnpackState directUnpack = unpack;
    directUnpack.skipRows             = 0;
    directUnpack.skipPixels           = 0;
    directUnpack.skipImages           = 0;
    ANGLE_TRY(stateManager->setPixelUnpackState(context, directUnpack));
    ANGLE_TRY(stateManager->setPixelUnpackBuffer(context, nullptr));

    gl::TextureType bindType = gl::TextureTargetToType(nlt.target);
    stateManager->bindTexture(bindType, nlt.textureID);
    ANGLE_GL_TRY(context,
                 functions->texSubImage3D(ToGLenum(nlt.target), nlt.level, area.x, area.y, area.z,
                                          area.width, area.height, area.depth, nativeFormat,
                                          nativeType, scratchBuffer->data()));

    if (features.copyImageSubData3DFailsOnTailMipLevels.enabled &&
        getType() == gl::TextureType::_3D)
    {
        ANGLE_TRY(updateVirtualLevel3D1x1CpuData(context, level, area, format, type, nativeFormat,
                                                 nativeType, directUnpack,
                                                 /*hasUnpackBuffer=*/false, scratchBuffer->data()));
    }

    ANGLE_TRY(stateManager->setPixelUnpackState(context, unpack));
    ANGLE_TRY(stateManager->setPixelUnpackBuffer(context, unpackBuffer));

    return angle::Result::Continue;
}

angle::Result TextureGL::evacuateMipchainToLevelTextures(const gl::Context *context)
{
    if (!mVirtualMipChain.has_value() || mVirtualMipChain->levels == 0)
    {
        return angle::Result::Continue;
    }

    const FunctionsGL *functions = GetFunctionsGL(context);
    gl::TextureType levelType =
        (getType() == gl::TextureType::CubeMap) ? gl::TextureType::_2D : getType();
    gl::TextureTarget dstTarget = NonCubeTextureTypeToTarget(levelType);

    for (size_t offset = 0; offset < mVirtualMipChain->levels; ++offset)
    {
        size_t level       = mVirtualMipChain->baseLevel + offset;
        auto evacuateLevel = [&](gl::TextureTarget target) -> angle::Result {
            size_t idx               = GetLevelInfoIndex(target, level);
            VirtualLevelInfo &vLevel = mVirtualMipChain->virtualLevels[idx];
            // When vLevel.texID != 0, this level was already redefined into its own standalone
            // 1-level companion texture, which holds the authoritative texel data. Only levels
            // with vLevel.texID == 0 still have their authoritative data in mTextureID and need
            // to be copied out before mTextureID is replaced.
            if (vLevel.texID == 0 && !vLevel.size.empty())
            {
                ANGLE_GL_TRY(context, functions->genTextures(1, &vLevel.texID));
                ANGLE_TRY(allocateTexStorage(context, vLevel.texID, levelType, 1,
                                             vLevel.internalFormat, vLevel.size));
                ANGLE_TRY(copyBetweenNativeLevels(context, mTextureID, target,
                                                  static_cast<GLint>(offset), vLevel.texID,
                                                  dstTarget, 0, vLevel));
            }
            return angle::Result::Continue;
        };

        if (getType() == gl::TextureType::CubeMap)
        {
            for (gl::TextureTarget face : gl::AllCubeFaceTextureTargets())
            {
                ANGLE_TRY(evacuateLevel(face));
            }
        }
        else
        {
            ANGLE_TRY(evacuateLevel(NonCubeTextureTypeToTarget(getType())));
        }
    }

    return angle::Result::Continue;
}

angle::Result TextureGL::allocateOrUpdateVirtualLevel(const gl::Context *context,
                                                      gl::TextureTarget target,
                                                      size_t level,
                                                      GLenum internalFormat,
                                                      const gl::Extents &size)
{
    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    if (!mVirtualMipChain.has_value())
    {
        mVirtualMipChain.emplace(mLevelInfo.size());
    }
    size_t idx               = GetLevelInfoIndex(target, level);
    VirtualLevelInfo &vLevel = mVirtualMipChain->virtualLevels[idx];
    if (features.copyImageSubData3DFailsOnTailMipLevels.enabled)
    {
        vLevel.tailMip3DCpuData = TailMip3DCpuData();
    }

    const bool hasNonEmptySize = !size.empty();
    const bool hasEGLImage =
        mState.hasBeenBoundAsSourceOfEglImage() || mVirtualMipChain->isEGLImageTarget;
    if (hasEGLImage)
    {
        mVirtualMipChain->forceReallocation = true;
    }

    // If this level is currently resident in mTextureID and its size and native internal format
    // are unchanged, we can reuse its existing storage in mTextureID in-place.
    if (hasNonEmptySize && !mVirtualMipChain->forceReallocation && vLevel.texID == 0 &&
        mVirtualMipChain->levels > 0 && level >= mVirtualMipChain->baseLevel &&
        level < mVirtualMipChain->baseLevel + mVirtualMipChain->levels && vLevel.size == size &&
        vLevel.internalFormat == internalFormat)
    {
        return angle::Result::Continue;
    }

    // If this level already has a dedicated 1-level texture with the exact same size and format,
    // reuse it in-place.
    if (hasNonEmptySize && vLevel.texID != 0 && vLevel.size == size &&
        vLevel.internalFormat == internalFormat)
    {
        return angle::Result::Continue;
    }

    stateManager->deleteTexture(vLevel.texID);
    vLevel.texID = 0;

    vLevel.size           = size;
    vLevel.internalFormat = hasNonEmptySize ? internalFormat : GL_NONE;

    if (hasNonEmptySize)
    {
        gl::TextureType levelType =
            (getType() == gl::TextureType::CubeMap) ? gl::TextureType::_2D : getType();
        ANGLE_GL_TRY(context, functions->genTextures(1, &vLevel.texID));
        ANGLE_TRY(
            allocateTexStorage(context, vLevel.texID, levelType, 1, internalFormat, vLevel.size));
    }

    mLocalDirtyBits.set(gl::Texture::DIRTY_BIT_IMPLEMENTATION);
    onStateChange(angle::SubjectMessage::DirtyBitsFlagged);
    onStateChange(angle::SubjectMessage::ObjectReallocated);

    return angle::Result::Continue;
}

angle::Result TextureGL::syncVirtualMipchain(const gl::Context *context)
{
    if (!mVirtualMipChain.has_value())
    {
        return angle::Result::Continue;
    }

    const FunctionsGL *functions                 = GetFunctionsGL(context);
    StateManagerGL *stateManager                 = GetStateManagerGL(context);
    const angle::FeaturesGL &features            = GetFeaturesGL(context);
    std::vector<VirtualLevelInfo> &virtualLevels = mVirtualMipChain->virtualLevels;

    const size_t maxLevelLimit = GetMaxMipmapLevel(context->getCaps(), getType());
    const size_t baseLevel     = std::min<size_t>(mAppliedBaseLevel, maxLevelLimit);

    // Determine whether baseLevel is defined and valid for all required targets.
    gl::Extents baseSize;
    GLenum baseFormat = GL_NONE;
    bool baseValid    = false;

    if (getType() == gl::TextureType::CubeMap)
    {
        const VirtualLevelInfo &posX =
            virtualLevels[GetLevelInfoIndex(gl::TextureTarget::CubeMapPositiveX, baseLevel)];
        if (posX.size.width > 0 && posX.size.width == posX.size.height && posX.size.depth == 1 &&
            posX.internalFormat != GL_NONE)
        {
            baseSize   = posX.size;
            baseFormat = posX.internalFormat;
            baseValid  = true;
            for (gl::TextureTarget face : gl::AllCubeFaceTextureTargets())
            {
                const VirtualLevelInfo &faceInfo =
                    virtualLevels[GetLevelInfoIndex(face, baseLevel)];
                if (faceInfo.size != baseSize || faceInfo.internalFormat != baseFormat)
                {
                    baseValid = false;
                    break;
                }
            }
        }
    }
    else
    {
        gl::TextureTarget target         = NonCubeTextureTypeToTarget(getType());
        const VirtualLevelInfo &baseInfo = virtualLevels[GetLevelInfoIndex(target, baseLevel)];
        if (baseInfo.size.width > 0 && baseInfo.size.height > 0 && baseInfo.size.depth > 0 &&
            baseInfo.internalFormat != GL_NONE)
        {
            baseSize   = baseInfo.size;
            baseFormat = baseInfo.internalFormat;
            baseValid  = true;
        }
    }

    if (!baseValid)
    {
        return angle::Result::Continue;
    }

    // Count how many consecutive levels starting at baseLevel have compatible mipmap dimensions and
    // identical internalFormat.
    const int maxDim = std::max(
        {baseSize.width, baseSize.height, getType() == gl::TextureType::_3D ? baseSize.depth : 1});
    const size_t maxPossibleLevels =
        std::min<size_t>(static_cast<size_t>(gl::log2(maxDim)) + 1, maxLevelLimit - baseLevel + 1);

    size_t validLevels = 1;
    for (size_t k = 1; k < maxPossibleLevels; ++k)
    {
        size_t level = baseLevel + k;
        gl::Extents expectedSize(
            std::max(1, baseSize.width >> k), std::max(1, baseSize.height >> k),
            getType() == gl::TextureType::_3D ? std::max(1, baseSize.depth >> k) : baseSize.depth);

        bool levelMatches = true;
        if (getType() == gl::TextureType::CubeMap)
        {
            for (gl::TextureTarget face : gl::AllCubeFaceTextureTargets())
            {
                const VirtualLevelInfo &faceInfo = virtualLevels[GetLevelInfoIndex(face, level)];
                if (faceInfo.size != expectedSize || faceInfo.internalFormat != baseFormat)
                {
                    levelMatches = false;
                    break;
                }
            }
        }
        else
        {
            gl::TextureTarget target     = NonCubeTextureTypeToTarget(getType());
            const VirtualLevelInfo &info = virtualLevels[GetLevelInfoIndex(target, level)];
            if (info.size != expectedSize || info.internalFormat != baseFormat)
            {
                levelMatches = false;
            }
        }

        if (!levelMatches)
        {
            break;
        }
        validLevels = k + 1;
    }

    const bool mipchainShapeMatches =
        !mVirtualMipChain->forceReallocation && mVirtualMipChain->levels > 0 &&
        mVirtualMipChain->baseLevel == baseLevel && mVirtualMipChain->levels == validLevels &&
        mVirtualMipChain->baseSize == baseSize && mVirtualMipChain->internalFormat == baseFormat;

    bool anyLevelMoved = false;

    if (mipchainShapeMatches)
    {
        // mTextureID already has the exact storage needed. Just merge any per-level textures in
        // [baseLevel, baseLevel + validLevels) back into mTextureID.
        for (size_t k = 0; k < validLevels; ++k)
        {
            size_t level    = baseLevel + k;
            GLint dstLevel  = static_cast<GLint>(k);
            auto mergeLevel = [&](gl::TextureTarget target) -> angle::Result {
                size_t idx               = GetLevelInfoIndex(target, level);
                VirtualLevelInfo &vLevel = virtualLevels[idx];
                if (vLevel.texID != 0)
                {
                    gl::TextureTarget srcTarget =
                        gl::IsCubeMapFaceTarget(target) ? gl::TextureTarget::_2D : target;
                    ANGLE_TRY(copyBetweenNativeLevels(context, vLevel.texID, srcTarget, 0,
                                                      mTextureID, target, dstLevel, vLevel));
                    stateManager->deleteTexture(vLevel.texID);
                    vLevel.texID = 0;
                    if (!gl::IsCubeMapFaceTarget(target) &&
                        features.copyImageSubData3DFailsOnTailMipLevels.enabled &&
                        (mState.hasBeenBoundAsAttachment() || mState.hasBeenBoundAsImage()))
                    {
                        vLevel.tailMip3DCpuData.clearCpuData();
                    }
                    anyLevelMoved = true;
                }
                return angle::Result::Continue;
            };

            if (getType() == gl::TextureType::CubeMap)
            {
                for (gl::TextureTarget face : gl::AllCubeFaceTextureTargets())
                {
                    ANGLE_TRY(mergeLevel(face));
                }
            }
            else
            {
                ANGLE_TRY(mergeLevel(NonCubeTextureTypeToTarget(getType())));
            }
        }
    }
    else
    {
        const GLuint oldTextureID         = mTextureID;
        const size_t oldMipchainBaseLevel = mVirtualMipChain->baseLevel;
        const size_t oldMipchainLevels    = mVirtualMipChain->levels;

        // 1. Evacuate any level in oldTextureID that will NOT be part of the new mipchain into its
        // per-level texture.
        if (oldMipchainLevels > 0)
        {
            gl::TextureType levelType =
                (getType() == gl::TextureType::CubeMap) ? gl::TextureType::_2D : getType();
            gl::TextureTarget dstTarget = NonCubeTextureTypeToTarget(levelType);
            for (size_t offset = 0; offset < oldMipchainLevels; ++offset)
            {
                size_t oldLevel = oldMipchainBaseLevel + offset;
                if (oldLevel >= baseLevel && oldLevel < baseLevel + validLevels)
                {
                    continue;
                }
                auto evacuateOldLevel = [&](gl::TextureTarget target) -> angle::Result {
                    size_t idx               = GetLevelInfoIndex(target, oldLevel);
                    VirtualLevelInfo &vLevel = virtualLevels[idx];
                    if (vLevel.texID == 0 && !vLevel.size.empty())
                    {
                        ANGLE_GL_TRY(context, functions->genTextures(1, &vLevel.texID));
                        ANGLE_TRY(allocateTexStorage(context, vLevel.texID, levelType, 1,
                                                     vLevel.internalFormat, vLevel.size));
                        ANGLE_TRY(copyBetweenNativeLevels(context, oldTextureID, target,
                                                          static_cast<GLint>(offset), vLevel.texID,
                                                          dstTarget, 0, vLevel));
                    }
                    return angle::Result::Continue;
                };
                if (getType() == gl::TextureType::CubeMap)
                {
                    for (gl::TextureTarget face : gl::AllCubeFaceTextureTargets())
                    {
                        ANGLE_TRY(evacuateOldLevel(face));
                    }
                }
                else
                {
                    ANGLE_TRY(evacuateOldLevel(NonCubeTextureTypeToTarget(getType())));
                }
            }
        }

        // 2. Allocate newTexID with immutable glTexStorage* for [baseLevel, baseLevel +
        // validLevels).
        GLuint newTexID = mTextureID;
        if (oldMipchainLevels > 0)
        {
            ANGLE_GL_TRY(context, functions->genTextures(1, &newTexID));
        }
        ANGLE_TRY(
            allocateTexStorage(context, newTexID, getType(), validLevels, baseFormat, baseSize));

        // 3. Populate newTexID from either virtualLevels[idx].texID or oldTextureID.
        for (size_t k = 0; k < validLevels; ++k)
        {
            size_t level       = baseLevel + k;
            GLint dstLevel     = static_cast<GLint>(k);
            auto populateLevel = [&](gl::TextureTarget target) -> angle::Result {
                size_t idx               = GetLevelInfoIndex(target, level);
                VirtualLevelInfo &vLevel = virtualLevels[idx];
                if (vLevel.texID != 0)
                {
                    gl::TextureTarget srcTarget =
                        gl::IsCubeMapFaceTarget(target) ? gl::TextureTarget::_2D : target;
                    ANGLE_TRY(copyBetweenNativeLevels(context, vLevel.texID, srcTarget, 0, newTexID,
                                                      target, dstLevel, vLevel));
                    stateManager->deleteTexture(vLevel.texID);
                    vLevel.texID = 0;
                    if (!gl::IsCubeMapFaceTarget(target) &&
                        features.copyImageSubData3DFailsOnTailMipLevels.enabled &&
                        (mState.hasBeenBoundAsAttachment() || mState.hasBeenBoundAsImage()))
                    {
                        vLevel.tailMip3DCpuData.clearCpuData();
                    }
                }
                else if (oldMipchainLevels > 0 && level >= oldMipchainBaseLevel &&
                         level < oldMipchainBaseLevel + oldMipchainLevels)
                {
                    GLint srcLevel = static_cast<GLint>(level - oldMipchainBaseLevel);
                    ANGLE_TRY(copyBetweenNativeLevels(context, oldTextureID, target, srcLevel,
                                                      newTexID, target, dstLevel, vLevel));
                }
                return angle::Result::Continue;
            };

            if (getType() == gl::TextureType::CubeMap)
            {
                for (gl::TextureTarget face : gl::AllCubeFaceTextureTargets())
                {
                    ANGLE_TRY(populateLevel(face));
                }
            }
            else
            {
                ANGLE_TRY(populateLevel(NonCubeTextureTypeToTarget(getType())));
            }
        }

        if (oldMipchainLevels > 0)
        {
            stateManager->deleteTexture(oldTextureID);
            mTextureID = newTexID;

            mAppliedSwizzle                 = gl::SwizzleState();
            mAppliedSampler                 = gl::SamplerState::CreateDefaultForTarget(getType());
            mAppliedMaxLevel                = gl::kInitialMaxLevel;
            mAppliedDepthStencilTextureMode = GL_DEPTH_COMPONENT;
            mLocalDirtyBits |= mAllModifiedDirtyBits;
            if (mLocalDirtyBits.any())
            {
                onStateChange(angle::SubjectMessage::DirtyBitsFlagged);
            }
        }

        mVirtualMipChain->baseLevel         = baseLevel;
        mVirtualMipChain->levels            = validLevels;
        mVirtualMipChain->baseSize          = baseSize;
        mVirtualMipChain->internalFormat    = baseFormat;
        mVirtualMipChain->forceReallocation = false;
        mVirtualMipChain->isEGLImageTarget  = false;
        anyLevelMoved                       = true;
    }

    if (anyLevelMoved)
    {
        onStateChange(angle::SubjectMessage::ObjectReallocated);
        stateManager->onProgramTextureAndImageBindingsChanged();

        // ObjectReallocated marks all FBOs with this texture attached dirty; unbound FBOs will
        // sync when bound, while the currently bound FBOs must be re-synced here because
        // State::syncDirtyObjects syncs framebuffers before textures.
        gl::Framebuffer *readFBO = context->getState().getReadFramebuffer();
        if (readFBO && readFBO->hasAnyDirtyBit())
        {
            ANGLE_TRY(readFBO->syncState(context, GL_READ_FRAMEBUFFER, gl::Command::Other));
        }
        gl::Framebuffer *drawFBO = context->getState().getDrawFramebuffer();
        if (drawFBO && drawFBO->hasAnyDirtyBit())
        {
            ANGLE_TRY(drawFBO->syncState(context, GL_DRAW_FRAMEBUFFER, gl::Command::Other));
        }
    }

    return angle::Result::Continue;
}

angle::Result TextureGL::setImage(const gl::Context *context,
                                  const gl::ImageIndex &index,
                                  GLenum internalFormat,
                                  const gl::Extents &size,
                                  GLenum format,
                                  GLenum type,
                                  const gl::PixelUnpackState &unpack,
                                  gl::Buffer *unpackBuffer,
                                  const uint8_t *pixels)
{
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    gl::TextureTarget target = index.getTarget();
    size_t level             = static_cast<size_t>(index.getLevelIndex());

    if (features.unpackOverlappingRowsSeparatelyUnpackBuffer.enabled && unpackBuffer &&
        unpack.rowLength != 0 && unpack.rowLength < size.width)
    {
        // The rows overlap in unpack memory. Upload the texture row by row to work around
        // driver bug.
        ANGLE_TRY(
            reserveTexImageToBeFilled(context, target, level, internalFormat, size, format, type));

        if (size.width == 0 || size.height == 0 || size.depth == 0)
        {
            return angle::Result::Continue;
        }

        gl::Box area(0, 0, 0, size.width, size.height, size.depth);
        return setSubImageRowByRowWorkaround(context, target, level, area, format, type, unpack,
                                             unpackBuffer, 0, pixels);
    }

    if (features.unpackLastRowSeparatelyForPaddingInclusion.enabled)
    {
        bool apply = false;
        ANGLE_TRY(ShouldApplyLastRowPaddingWorkaround(
            GetImplAs<ContextGL>(context), size, unpack, unpackBuffer, format, type,
            nativegl::UseTexImage3D(getType()), pixels, &apply));

        // The driver will think the pixel buffer doesn't have enough data, work around this bug
        // by uploading the last row (and last level if 3D) separately.
        if (apply)
        {
            ANGLE_TRY(reserveTexImageToBeFilled(context, target, level, internalFormat, size,
                                                format, type));

            if (size.width == 0 || size.height == 0 || size.depth == 0)
            {
                return angle::Result::Continue;
            }

            gl::Box area(0, 0, 0, size.width, size.height, size.depth);
            ANGLE_TRY(setSubImagePaddingWorkaround(context, target, level, area, format, type,
                                                   unpack, unpackBuffer, pixels));
            if (features.copyImageSubData3DFailsOnTailMipLevels.enabled &&
                getType() == gl::TextureType::_3D && size.width == 1 && size.height == 1)
            {
                const FunctionsGL *functions = GetFunctionsGL(context);
                nativegl::TexSubImageFormat texSubImageFormat =
                    nativegl::GetTexSubImageFormat(functions, features, format, type);
                ANGLE_TRY(updateVirtualLevel3D1x1CpuData(
                    context, level, area, format, type, texSubImageFormat.format,
                    texSubImageFormat.type, unpack, unpackBuffer != nullptr, pixels));
            }
            return angle::Result::Continue;
        }
    }

    ANGLE_TRY(setImageHelper(context, target, level, internalFormat, size, format, type, pixels));

    return angle::Result::Continue;
}

angle::Result TextureGL::setImageHelper(const gl::Context *context,
                                        gl::TextureTarget target,
                                        size_t level,
                                        GLenum internalFormat,
                                        const gl::Extents &size,
                                        GLenum format,
                                        GLenum type,
                                        const uint8_t *pixels)
{
    ASSERT(TextureTargetToType(target) == getType());

    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    if (features.reattachFboDepthStencilOnReallocation.enabled)
    {
        onStateChange(angle::SubjectMessage::ObjectReallocated);
    }

    if (features.reattachTextureToFboAfterLayerIncrease.enabled &&
        gl::IsLayeredTextureType(getType()))
    {
        const gl::ImageDesc &desc = mState.getImageDesc(target, level);
        if (size.depth > desc.size.depth)
        {
            onStateChange(angle::SubjectMessage::TextureLayerCountIncreased);
        }
    }

    const gl::InternalFormat &originalInternalFormatInfo =
        gl::GetInternalFormatInfo(internalFormat, type);
    nativegl::TexImageFormat texImageFormat =
        nativegl::GetTexImageFormat(functions, features, internalFormat, format, type);

    if (features.virtualizeBaseLevelAndMipchain.enabled && isVirtualMipchainType())
    {
        nativegl::TexStorageFormat texStorageFormat = nativegl::GetTexStorageFormat(
            functions, features, originalInternalFormatInfo.sizedInternalFormat);
        ANGLE_TRY(allocateOrUpdateVirtualLevel(context, target, level,
                                               texStorageFormat.internalFormat, size));

        if (size.width > 0 && size.height > 0 && size.depth > 0 &&
            (pixels != nullptr || stateManager->getBufferID(gl::BufferBinding::PixelUnpack) != 0))
        {
            const bool hasUnpackBuffer =
                stateManager->getBufferID(gl::BufferBinding::PixelUnpack) != 0;
            NativeLevelTarget nlt =
                getNativeLevelTarget(target, level, /*invalidateCpuData=*/false);
            gl::TextureType bindType = gl::TextureTargetToType(nlt.target);
            stateManager->bindTexture(bindType, nlt.textureID);
            if (nativegl::UseTexImage2D(bindType))
            {
                ASSERT(size.depth == 1);
                if (features.splitLevel0PboFullSubImage2D.enabled && nlt.level == 0 &&
                    hasUnpackBuffer && (size.width > 1 || size.height > 1))
                {
                    ContextGL *contextGL               = GetImplAs<ContextGL>(context);
                    const gl::PixelUnpackState &unpack = context->getState().getUnpackState();
                    const gl::Buffer *unpackBuffer =
                        context->getState().getTargetBuffer(gl::BufferBinding::PixelUnpack);
                    const gl::InternalFormat &glFormat = gl::GetInternalFormatInfo(format, type);
                    GLuint rowBytes                    = 0;
                    GLuint imageBytes                  = 0;
                    GLuint skipBytes                   = 0;
                    ANGLE_CHECK_GL_MATH(contextGL,
                                        glFormat.computeRowDepthSkipBytes(
                                            type, size.width, size.height, unpack, /*is3D=*/false,
                                            &rowBytes, &imageBytes, &skipBytes));
                    size_t maxBytesUploadedPerChunk = 0;
                    if (size.height > 1)
                    {
                        maxBytesUploadedPerChunk = static_cast<size_t>(size.height - 1) * rowBytes;
                    }
                    else
                    {
                        ASSERT(size.height == 1 && size.width > 1);
                        GLuint pixelBytes        = glFormat.computePixelBytes(type);
                        maxBytesUploadedPerChunk = static_cast<size_t>(size.width - 1) * pixelBytes;
                    }
                    gl::Box area(0, 0, 0, size.width, size.height, size.depth);
                    ANGLE_TRY(setSubImageRowByRowWorkaround(context, target, level, area, format,
                                                            type, unpack, unpackBuffer,
                                                            maxBytesUploadedPerChunk, pixels));
                }
                else
                {
                    ANGLE_GL_TRY(context, functions->texSubImage2D(ToGLenum(nlt.target), nlt.level,
                                                                   0, 0, size.width, size.height,
                                                                   texImageFormat.format,
                                                                   texImageFormat.type, pixels));
                }
            }
            else
            {
                ASSERT(nativegl::UseTexImage3D(bindType));
                const gl::PixelUnpackState &unpack = context->getState().getUnpackState();
                if (features.readback3DDepthStencilPbosDuringUpload.enabled && hasUnpackBuffer &&
                    functions->mapBufferRange != nullptr &&
                    shouldReadback3DPboDuringUpload(context, originalInternalFormatInfo, format,
                                                    type, unpack))
                {
                    ANGLE_TRY(setSubImage3DFromUnpackBufferViaCpu(
                        context, nlt, level, gl::Box(0, 0, 0, size.width, size.height, size.depth),
                        format, type, texImageFormat.format, texImageFormat.type, unpack, pixels));
                }
                else
                {
                    ANGLE_GL_TRY(context, functions->texSubImage3D(
                                              ToGLenum(nlt.target), nlt.level, 0, 0, 0, size.width,
                                              size.height, size.depth, texImageFormat.format,
                                              texImageFormat.type, pixels));
                    if (features.copyImageSubData3DFailsOnTailMipLevels.enabled &&
                        getType() == gl::TextureType::_3D && size.width == 1 && size.height == 1)
                    {
                        ANGLE_TRY(updateVirtualLevel3D1x1CpuData(
                            context, level, gl::Box(0, 0, 0, size.width, size.height, size.depth),
                            format, type, texImageFormat.format, texImageFormat.type, unpack,
                            hasUnpackBuffer, pixels));
                    }
                }
            }
        }

        LevelInfoGL levelInfo =
            GetLevelInfo(features, originalInternalFormatInfo, texStorageFormat.internalFormat);
        setLevelInfo(context, target, level, 1, levelInfo);
        return angle::Result::Continue;
    }

    stateManager->bindTexture(getType(), mTextureID);

    if (features.recreateTextureOnTexImage3dDepthIncrease.enabled &&
        getType() == gl::TextureType::_3D && !mState.getImmutableFormat() &&
        functions->copyImageSubData)
    {
        ANGLE_TRY(recreateTextureOnTexImage3DDepthIncreaseWorkaround(context, target, level, size));
    }

    if (features.resetTexImage2DBaseLevel.enabled)
    {
        // setBaseLevel doesn't ever generate errors.
        (void)setBaseLevel(context, 0);
    }

    if (nativegl::UseTexImage2D(getType()))
    {
        ASSERT(size.depth == 1);
        if (features.useTexSubImageForClientDataNpotUploads.enabled && pixels != nullptr &&
            (!gl::isPow2(size.width) || !gl::isPow2(size.height)))
        {
            ANGLE_GL_TRY_ALWAYS_CHECK(
                context, functions->texImage2D(
                             nativegl::GetTextureBindingTarget(target), static_cast<GLint>(level),
                             texImageFormat.internalFormat, size.width, size.height, 0,
                             texImageFormat.format, texImageFormat.type, nullptr));
            ANGLE_GL_TRY(context, functions->texSubImage2D(
                                      nativegl::GetTextureBindingTarget(target),
                                      static_cast<GLint>(level), 0, 0, size.width, size.height,
                                      texImageFormat.format, texImageFormat.type, pixels));
        }
        else
        {
            ANGLE_GL_TRY_ALWAYS_CHECK(
                context, functions->texImage2D(
                             nativegl::GetTextureBindingTarget(target), static_cast<GLint>(level),
                             texImageFormat.internalFormat, size.width, size.height, 0,
                             texImageFormat.format, texImageFormat.type, pixels));
        }
    }
    else
    {
        ASSERT(nativegl::UseTexImage3D(getType()));
        ANGLE_GL_TRY_ALWAYS_CHECK(
            context, functions->texImage3D(ToGLenum(target), static_cast<GLint>(level),
                                           texImageFormat.internalFormat, size.width, size.height,
                                           size.depth, 0, texImageFormat.format,
                                           texImageFormat.type, pixels));
    }

    LevelInfoGL levelInfo =
        GetLevelInfo(features, originalInternalFormatInfo, texImageFormat.internalFormat);
    setLevelInfo(context, target, level, 1, levelInfo);

    if (features.setZeroLevelBeforeGenerateMipmap.enabled && getType() == gl::TextureType::_2D &&
        level != 0 && mLevelInfo[0].nativeInternalFormat == GL_NONE)
    {
        // Only fill level zero if it's possible that mipmaps can be generated with this format
        const gl::InternalFormat &internalFormatInfo =
            gl::GetInternalFormatInfo(internalFormat, type);
        if (!internalFormatInfo.sized ||
            (internalFormatInfo.filterSupport(context->getClientVersion(),
                                              context->getExtensions()) &&
             internalFormatInfo.textureAttachmentSupport(context->getClientVersion(),
                                                         context->getExtensions())))
        {
            ANGLE_GL_TRY_ALWAYS_CHECK(
                context,
                functions->texImage2D(nativegl::GetTextureBindingTarget(target), 0,
                                      texImageFormat.internalFormat, 1, 1, 0, texImageFormat.format,
                                      texImageFormat.type, nullptr));
            setLevelInfo(context, target, 0, 1, levelInfo);
        }
    }

    return angle::Result::Continue;
}

angle::Result TextureGL::reserveTexImageToBeFilled(const gl::Context *context,
                                                   gl::TextureTarget target,
                                                   size_t level,
                                                   GLenum internalFormat,
                                                   const gl::Extents &size,
                                                   GLenum format,
                                                   GLenum type)
{
    StateManagerGL *stateManager = GetStateManagerGL(context);
    ANGLE_TRY(stateManager->setPixelUnpackBuffer(context, nullptr));
    ANGLE_TRY(setImageHelper(context, target, level, internalFormat, size, format, type, nullptr));
    return angle::Result::Continue;
}

angle::Result TextureGL::setSubImage(const gl::Context *context,
                                     const gl::ImageIndex &index,
                                     const gl::Box &area,
                                     GLenum format,
                                     GLenum type,
                                     const gl::PixelUnpackState &unpack,
                                     gl::Buffer *unpackBuffer,
                                     const uint8_t *pixels)
{
    ASSERT(TextureTargetToType(index.getTarget()) == getType());

    ContextGL *contextGL              = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    const gl::InternalFormat &originalInternalFormatInfo = *mState.getImageDesc(index).format.info;
    nativegl::TexSubImageFormat texSubImageFormat =
        nativegl::GetTexSubImageFormat(functions, features, format, type);

    gl::TextureTarget target = index.getTarget();
    size_t level             = static_cast<size_t>(index.getLevelIndex());

    ASSERT(getLevelInfo(target, level).lumaWorkaround.enabled ==
           GetLevelInfo(features, originalInternalFormatInfo, texSubImageFormat.format)
               .lumaWorkaround.enabled);

    NativeLevelTarget nlt    = getNativeLevelTarget(target, level, /*invalidateCpuData=*/false);
    gl::TextureType bindType = gl::TextureTargetToType(nlt.target);
    if (features.virtualizeBaseLevelAndMipchain.enabled && isVirtualMipchainType() &&
        features.readback3DDepthStencilPbosDuringUpload.enabled &&
        nativegl::UseTexImage3D(bindType) && unpackBuffer != nullptr &&
        functions->mapBufferRange != nullptr &&
        shouldReadback3DPboDuringUpload(context, originalInternalFormatInfo, format, type, unpack))
    {
        ANGLE_TRY(setSubImage3DFromUnpackBufferViaCpu(context, nlt, level, area, format, type,
                                                      texSubImageFormat.format,
                                                      texSubImageFormat.type, unpack, pixels));
        contextGL->markWorkSubmitted();
        return angle::Result::Continue;
    }

    if (features.copyImageSubData3DFailsOnTailMipLevels.enabled &&
        getType() == gl::TextureType::_3D)
    {
        ANGLE_TRY(updateVirtualLevel3D1x1CpuData(context, level, area, format, type,
                                                 texSubImageFormat.format, texSubImageFormat.type,
                                                 unpack, unpackBuffer != nullptr, pixels));
    }
    stateManager->bindTexture(bindType, nlt.textureID);
    const gl::ImageDesc &desc = mState.getImageDesc(index);
    const bool isFullImage    = area.x == 0 && area.y == 0 && area.width == desc.size.width &&
                                area.height == desc.size.height;

    if (features.splitLevel0PboFullSubImage2D.enabled && nlt.level == 0 && unpackBuffer &&
        nativegl::UseTexImage2D(bindType) && isFullImage && (area.width > 1 || area.height > 1))
    {
        GLuint rowBytes   = 0;
        GLuint imageBytes = 0;
        GLuint skipBytes  = 0;
        ANGLE_CHECK_GL_MATH(contextGL, originalInternalFormatInfo.computeRowDepthSkipBytes(
                                           type, area.width, area.height, unpack, /*is3D=*/false,
                                           &rowBytes, &imageBytes, &skipBytes));

        size_t maxBytesUploadedPerChunk = 0;
        if (area.height > 1)
        {
            maxBytesUploadedPerChunk = static_cast<size_t>(area.height - 1) * rowBytes;
        }
        else
        {
            ASSERT(area.height == 1 && area.width > 1);
            GLuint pixelBytes        = originalInternalFormatInfo.computePixelBytes(type);
            maxBytesUploadedPerChunk = static_cast<size_t>(area.width - 1) * pixelBytes;
        }

        ANGLE_TRY(setSubImageRowByRowWorkaround(context, target, level, area, format, type, unpack,
                                                unpackBuffer, maxBytesUploadedPerChunk, pixels));
        contextGL->markWorkSubmitted();
        return angle::Result::Continue;
    }

    if (features.unpackOverlappingRowsSeparatelyUnpackBuffer.enabled && unpackBuffer &&
        unpack.rowLength != 0 && unpack.rowLength < area.width)
    {
        ANGLE_TRY(setSubImageRowByRowWorkaround(context, target, level, area, format, type, unpack,
                                                unpackBuffer, 0, pixels));
        contextGL->markWorkSubmitted();
        return angle::Result::Continue;
    }

    if (features.unpackLastRowSeparatelyForPaddingInclusion.enabled)
    {
        gl::Extents size(area.width, area.height, area.depth);

        bool apply = false;
        ANGLE_TRY(ShouldApplyLastRowPaddingWorkaround(
            GetImplAs<ContextGL>(context), size, unpack, unpackBuffer, format, type,
            nativegl::UseTexImage3D(bindType), pixels, &apply));

        // The driver will think the pixel buffer doesn't have enough data, work around this bug
        // by uploading the last row (and last level if 3D) separately.
        if (apply)
        {
            ANGLE_TRY(setSubImagePaddingWorkaround(context, target, level, area, format, type,
                                                   unpack, unpackBuffer, pixels));
            contextGL->markWorkSubmitted();
            return angle::Result::Continue;
        }
    }

    if (features.uploadTextureDataInChunks.enabled)
    {
        ANGLE_TRY(setSubImageRowByRowWorkaround(context, target, level, area, format, type, unpack,
                                                unpackBuffer, kUploadTextureDataInChunksUploadSize,
                                                pixels));
        contextGL->markWorkSubmitted();
        return angle::Result::Continue;
    }

    if (nativegl::UseTexImage2D(bindType))
    {
        ASSERT(area.z == 0 && area.depth == 1);
        ANGLE_GL_TRY(context, functions->texSubImage2D(
                                  nativegl::GetTextureBindingTarget(nlt.target), nlt.level, area.x,
                                  area.y, area.width, area.height, texSubImageFormat.format,
                                  texSubImageFormat.type, pixels));
    }
    else
    {
        ASSERT(nativegl::UseTexImage3D(bindType));
        ANGLE_GL_TRY(context, functions->texSubImage3D(ToGLenum(nlt.target), nlt.level, area.x,
                                                       area.y, area.z, area.width, area.height,
                                                       area.depth, texSubImageFormat.format,
                                                       texSubImageFormat.type, pixels));
    }

    contextGL->markWorkSubmitted();
    return angle::Result::Continue;
}

angle::Result TextureGL::setSubImageRowByRowWorkaround(const gl::Context *context,
                                                       gl::TextureTarget target,
                                                       size_t level,
                                                       const gl::Box &area,
                                                       GLenum format,
                                                       GLenum type,
                                                       const gl::PixelUnpackState &unpack,
                                                       const gl::Buffer *unpackBuffer,
                                                       size_t maxBytesUploadedPerChunk,
                                                       const uint8_t *pixels)
{
    ContextGL *contextGL              = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    NativeLevelTarget nlt    = getNativeLevelTarget(target, level, /*invalidateCpuData=*/false);
    gl::TextureType bindType = gl::TextureTargetToType(nlt.target);
    stateManager->bindTexture(bindType, nlt.textureID);

    gl::PixelUnpackState directUnpack = unpack;
    directUnpack.skipRows             = 0;
    directUnpack.skipPixels           = 0;
    directUnpack.skipImages           = 0;
    ANGLE_TRY(stateManager->setPixelUnpackState(context, directUnpack));
    ANGLE_TRY(stateManager->setPixelUnpackBuffer(context, unpackBuffer));

    const gl::InternalFormat &glFormat = gl::GetInternalFormatInfo(format, type);

    GLuint rowBytes    = 0;
    GLuint imageBytes  = 0;
    GLuint skipBytes   = 0;
    bool useTexImage3D = nativegl::UseTexImage3D(bindType);

    ANGLE_CHECK_GL_MATH(contextGL, glFormat.computeRowDepthSkipBytes(
                                       type, area.width, area.height, unpack, useTexImage3D,
                                       &rowBytes, &imageBytes, &skipBytes));

    GLint rowsPerChunk =
        std::min(std::max(static_cast<GLint>(maxBytesUploadedPerChunk / rowBytes), 1), area.height);
    if (maxBytesUploadedPerChunk > 0 && rowsPerChunk < area.height)
    {
        ANGLE_PERF_WARNING(contextGL->getDebug(), GL_DEBUG_SEVERITY_LOW,
                           "Chunking upload of texture data to work around driver hangs.");
    }

    nativegl::TexSubImageFormat texSubImageFormat =
        nativegl::GetTexSubImageFormat(functions, features, format, type);

    const uint8_t *pixelsWithSkip = ANGLE_UNSAFE_TODO(pixels + skipBytes);
    if (useTexImage3D)
    {
        for (GLint image = 0; image < area.depth; ++image)
        {
            GLint imageByteOffset = image * imageBytes;
            for (GLint row = 0; row < area.height; row += rowsPerChunk)
            {
                GLint height             = std::min(rowsPerChunk, area.height - row);
                GLint byteOffset         = imageByteOffset + row * rowBytes;
                const GLubyte *rowPixels = ANGLE_UNSAFE_TODO(pixelsWithSkip + byteOffset);
                ANGLE_GL_TRY(context,
                             functions->texSubImage3D(ToGLenum(nlt.target), nlt.level, area.x,
                                                      row + area.y, image + area.z, area.width,
                                                      height, 1, texSubImageFormat.format,
                                                      texSubImageFormat.type, rowPixels));
            }
        }
    }
    else
    {
        ASSERT(nativegl::UseTexImage2D(bindType));
        if (area.height == 1 && maxBytesUploadedPerChunk > 0 && maxBytesUploadedPerChunk < rowBytes)
        {
            GLuint pixelBytes    = glFormat.computePixelBytes(type);
            GLint pixelsPerChunk = std::min(
                std::max(static_cast<GLint>(maxBytesUploadedPerChunk / pixelBytes), 1), area.width);
            for (GLint col = 0; col < area.width; col += pixelsPerChunk)
            {
                GLint width              = std::min(pixelsPerChunk, area.width - col);
                GLint byteOffset         = col * pixelBytes;
                const GLubyte *colPixels = ANGLE_UNSAFE_TODO(pixelsWithSkip + byteOffset);
                ANGLE_GL_TRY(context,
                             functions->texSubImage2D(ToGLenum(nlt.target), nlt.level, area.x + col,
                                                      area.y, width, 1, texSubImageFormat.format,
                                                      texSubImageFormat.type, colPixels));
            }
        }
        else
        {
            for (GLint row = 0; row < area.height; row += rowsPerChunk)
            {
                GLint height             = std::min(rowsPerChunk, area.height - row);
                GLint byteOffset         = row * rowBytes;
                const GLubyte *rowPixels = ANGLE_UNSAFE_TODO(pixelsWithSkip + byteOffset);
                ANGLE_GL_TRY(context, functions->texSubImage2D(ToGLenum(nlt.target), nlt.level,
                                                               area.x, row + area.y, area.width,
                                                               height, texSubImageFormat.format,
                                                               texSubImageFormat.type, rowPixels));
            }
        }
    }
    return angle::Result::Continue;
}

angle::Result TextureGL::setSubImagePaddingWorkaround(const gl::Context *context,
                                                      gl::TextureTarget target,
                                                      size_t level,
                                                      const gl::Box &area,
                                                      GLenum format,
                                                      GLenum type,
                                                      const gl::PixelUnpackState &unpack,
                                                      const gl::Buffer *unpackBuffer,
                                                      const uint8_t *pixels)
{
    ContextGL *contextGL         = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions = GetFunctionsGL(context);
    StateManagerGL *stateManager = GetStateManagerGL(context);

    NativeLevelTarget nlt    = getNativeLevelTarget(target, level, /*invalidateCpuData=*/false);
    gl::TextureType bindType = gl::TextureTargetToType(nlt.target);
    stateManager->bindTexture(bindType, nlt.textureID);

    const gl::InternalFormat &glFormat = gl::GetInternalFormatInfo(format, type);
    GLuint rowBytes                    = 0;
    GLuint imageBytes                  = 0;
    bool useTexImage3D                 = nativegl::UseTexImage3D(bindType);
    GLuint skipBytes                   = 0;

    ANGLE_CHECK_GL_MATH(contextGL, glFormat.computeRowDepthSkipBytes(
                                       type, area.width, area.height, unpack, useTexImage3D,
                                       &rowBytes, &imageBytes, &skipBytes));

    ANGLE_TRY(stateManager->setPixelUnpackState(context, unpack));
    ANGLE_TRY(stateManager->setPixelUnpackBuffer(context, unpackBuffer));

    gl::PixelUnpackState directUnpack;
    directUnpack.alignment = 1;

    if (useTexImage3D)
    {
        // Upload all but the last slice
        if (area.depth > 1)
        {
            ANGLE_GL_TRY(context, functions->texSubImage3D(ToGLenum(nlt.target), nlt.level, area.x,
                                                           area.y, area.z, area.width, area.height,
                                                           area.depth - 1, format, type, pixels));
        }

        // Upload the last slice but its last row
        if (area.height > 1)
        {
            // Do not include skipBytes in the last image pixel start offset as it will be done by
            // the driver
            size_t lastImageOffset         = (area.depth - 1) * imageBytes;
            const GLubyte *lastImagePixels = ANGLE_UNSAFE_TODO(pixels + lastImageOffset);
            ANGLE_GL_TRY(context, functions->texSubImage3D(ToGLenum(nlt.target), nlt.level, area.x,
                                                           area.y, area.z + area.depth - 1,
                                                           area.width, area.height - 1, 1, format,
                                                           type, lastImagePixels));
        }

        // Upload the last row of the last slice "manually"
        ANGLE_TRY(stateManager->setPixelUnpackState(context, directUnpack));

        size_t lastRowOffset =
            skipBytes + (area.depth - 1) * imageBytes + (area.height - 1) * rowBytes;
        const GLubyte *lastRowPixels = ANGLE_UNSAFE_TODO(pixels + lastRowOffset);
        ANGLE_GL_TRY(context,
                     functions->texSubImage3D(ToGLenum(nlt.target), nlt.level, area.x,
                                              area.y + area.height - 1, area.z + area.depth - 1,
                                              area.width, 1, 1, format, type, lastRowPixels));
    }
    else
    {
        ASSERT(nativegl::UseTexImage2D(bindType));

        // Upload all but the last row
        if (area.height > 1)
        {
            ANGLE_GL_TRY(context, functions->texSubImage2D(ToGLenum(nlt.target), nlt.level, area.x,
                                                           area.y, area.width, area.height - 1,
                                                           format, type, pixels));
        }

        // Upload the last row "manually"
        ANGLE_TRY(stateManager->setPixelUnpackState(context, directUnpack));

        size_t lastRowOffset         = skipBytes + (area.height - 1) * rowBytes;
        const GLubyte *lastRowPixels = ANGLE_UNSAFE_TODO(pixels + lastRowOffset);
        ANGLE_GL_TRY(context, functions->texSubImage2D(ToGLenum(nlt.target), nlt.level, area.x,
                                                       area.y + area.height - 1, area.width, 1,
                                                       format, type, lastRowPixels));
    }

    return angle::Result::Continue;
}

angle::Result TextureGL::setCompressedImage(const gl::Context *context,
                                            const gl::ImageIndex &index,
                                            GLenum internalFormat,
                                            const gl::Extents &size,
                                            const gl::PixelUnpackState &unpack,
                                            size_t imageSize,
                                            const uint8_t *pixels)
{
    ContextGL *contextGL              = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    gl::TextureTarget target = index.getTarget();
    size_t level             = static_cast<size_t>(index.getLevelIndex());
    ASSERT(TextureTargetToType(target) == getType());

    const gl::InternalFormat &originalInternalFormatInfo =
        gl::GetSizedInternalFormatInfo(internalFormat);

    nativegl::CompressedTexImageFormat compressedTexImageFormat =
        nativegl::GetCompressedTexImageFormat(functions, features, internalFormat);

    if (features.virtualizeBaseLevelAndMipchain.enabled && isVirtualMipchainType())
    {
        nativegl::TexStorageFormat texStorageFormat =
            nativegl::GetTexStorageFormat(functions, features, internalFormat);
        ANGLE_TRY(allocateOrUpdateVirtualLevel(context, target, level,
                                               texStorageFormat.internalFormat, size));

        if (size.width > 0 && size.height > 0 && size.depth > 0 &&
            (pixels != nullptr || stateManager->getBufferID(gl::BufferBinding::PixelUnpack) != 0))
        {
            const bool hasUnpackBuffer =
                stateManager->getBufferID(gl::BufferBinding::PixelUnpack) != 0;
            NativeLevelTarget nlt =
                getNativeLevelTarget(target, level, /*invalidateCpuData=*/false);
            gl::TextureType bindType = gl::TextureTargetToType(nlt.target);
            stateManager->bindTexture(bindType, nlt.textureID);
            ANGLE_TRY(stateManager->setPixelUnpackState(context, unpack));

            nativegl::CompressedTexSubImageFormat compressedTexSubImageFormat =
                nativegl::GetCompressedSubTexImageFormat(functions, features, internalFormat);

            if (nativegl::UseTexImage2D(bindType))
            {
                ASSERT(size.depth == 1);
                ANGLE_GL_TRY(context, functions->compressedTexSubImage2D(
                                          ToGLenum(nlt.target), nlt.level, 0, 0, size.width,
                                          size.height, compressedTexSubImageFormat.format,
                                          static_cast<GLsizei>(imageSize), pixels));
            }
            else
            {
                ASSERT(nativegl::UseTexImage3D(bindType));
                ANGLE_GL_TRY(context,
                             functions->compressedTexSubImage3D(
                                 ToGLenum(nlt.target), nlt.level, 0, 0, 0, size.width, size.height,
                                 size.depth, compressedTexSubImageFormat.format,
                                 static_cast<GLsizei>(imageSize), pixels));
                if (features.copyImageSubData3DFailsOnTailMipLevels.enabled &&
                    getType() == gl::TextureType::_3D && size.width == 1 && size.height == 1 &&
                    !hasUnpackBuffer && pixels != nullptr && unpack.skipImages == 0 &&
                    unpack.imageHeight == 0 && unpack.skipRows == 0 && unpack.skipPixels == 0 &&
                    unpack.rowLength == 0)
                {
                    VirtualLevelInfo &vLevel =
                        mVirtualMipChain->virtualLevels[GetLevelInfoIndex(target, level)];
                    TailMip3DCpuData &cpuInfo = vLevel.tailMip3DCpuData;
                    // SAFETY: `pixels` points to client memory validated by the GL entry point to
                    // contain `imageSize` bytes of compressed texture data.
                    angle::Span<const uint8_t> srcSpan =
                        ANGLE_UNSAFE_BUFFERS(angle::Span<const uint8_t>(pixels, imageSize));
                    cpuInfo.data.assign(srcSpan.begin(), srcSpan.end());
                    cpuInfo.format                  = compressedTexSubImageFormat.format;
                    cpuInfo.type                    = GL_NONE;
                    cpuInfo.compressed              = true;
                    cpuInfo.contiguousSlicesWritten = static_cast<size_t>(size.depth);
                }
            }
        }

        LevelInfoGL levelInfo =
            GetLevelInfo(features, originalInternalFormatInfo, texStorageFormat.internalFormat);
        ASSERT(!levelInfo.lumaWorkaround.enabled);
        setLevelInfo(context, target, level, 1, levelInfo);

        contextGL->markWorkSubmitted();
        return angle::Result::Continue;
    }

    stateManager->bindTexture(getType(), mTextureID);
    ANGLE_TRY(stateManager->setPixelUnpackState(context, unpack));

    if (nativegl::UseTexImage2D(getType()))
    {
        ASSERT(size.depth == 1);
        ANGLE_GL_TRY_ALWAYS_CHECK(
            context, functions->compressedTexImage2D(ToGLenum(target), static_cast<GLint>(level),
                                                     compressedTexImageFormat.internalFormat,
                                                     size.width, size.height, 0,
                                                     static_cast<GLsizei>(imageSize), pixels));
    }
    else
    {
        ASSERT(nativegl::UseTexImage3D(getType()));

        ANGLE_GL_TRY_ALWAYS_CHECK(
            context, functions->compressedTexImage3D(ToGLenum(target), static_cast<GLint>(level),
                                                     compressedTexImageFormat.internalFormat,
                                                     size.width, size.height, size.depth, 0,
                                                     static_cast<GLsizei>(imageSize), pixels));
    }

    LevelInfoGL levelInfo =
        GetLevelInfo(features, originalInternalFormatInfo, compressedTexImageFormat.internalFormat);
    ASSERT(!levelInfo.lumaWorkaround.enabled);
    setLevelInfo(context, target, level, 1, levelInfo);

    contextGL->markWorkSubmitted();
    return angle::Result::Continue;
}

angle::Result TextureGL::setCompressedSubImage(const gl::Context *context,
                                               const gl::ImageIndex &index,
                                               const gl::Box &area,
                                               GLenum format,
                                               const gl::PixelUnpackState &unpack,
                                               size_t imageSize,
                                               const uint8_t *pixels)
{
    ContextGL *contextGL              = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    gl::TextureTarget target = index.getTarget();
    size_t level             = static_cast<size_t>(index.getLevelIndex());
    ASSERT(TextureTargetToType(target) == getType());

    const gl::InternalFormat &originalInternalFormatInfo = gl::GetSizedInternalFormatInfo(format);
    nativegl::CompressedTexSubImageFormat compressedTexSubImageFormat =
        nativegl::GetCompressedSubTexImageFormat(functions, features, format);

    NativeLevelTarget nlt    = getNativeLevelTarget(target, level);
    gl::TextureType bindType = gl::TextureTargetToType(nlt.target);
    stateManager->bindTexture(bindType, nlt.textureID);
    ANGLE_TRY(stateManager->setPixelUnpackState(context, unpack));

    if (nativegl::UseTexImage2D(bindType))
    {
        ASSERT(area.z == 0 && area.depth == 1);
        ANGLE_GL_TRY(context, functions->compressedTexSubImage2D(
                                  ToGLenum(nlt.target), nlt.level, area.x, area.y, area.width,
                                  area.height, compressedTexSubImageFormat.format,
                                  static_cast<GLsizei>(imageSize), pixels));
    }
    else
    {
        ASSERT(nativegl::UseTexImage3D(bindType));
        ANGLE_GL_TRY(context,
                     functions->compressedTexSubImage3D(
                         ToGLenum(nlt.target), nlt.level, area.x, area.y, area.z, area.width,
                         area.height, area.depth, compressedTexSubImageFormat.format,
                         static_cast<GLsizei>(imageSize), pixels));
        if (features.copyImageSubData3DFailsOnTailMipLevels.enabled &&
            mVirtualMipChain.has_value() && getType() == gl::TextureType::_3D)
        {
            VirtualLevelInfo &vLevel =
                mVirtualMipChain->virtualLevels[GetLevelInfoIndex(target, level)];
            TailMip3DCpuData &cpuInfo = vLevel.tailMip3DCpuData;
            const bool hasUnpackBuffer =
                stateManager->getBufferID(gl::BufferBinding::PixelUnpack) != 0;
            if (vLevel.size.width == 1 && vLevel.size.height == 1 && area.x == 0 && area.y == 0 &&
                area.z == 0 && area.width == 1 && area.height == 1 &&
                area.depth == vLevel.size.depth && !hasUnpackBuffer && pixels != nullptr &&
                unpack.skipImages == 0 && unpack.imageHeight == 0 && unpack.skipRows == 0 &&
                unpack.skipPixels == 0 && unpack.rowLength == 0 &&
                (vLevel.texID != 0 ||
                 (!mState.hasBeenBoundAsAttachment() && !mState.hasBeenBoundAsImage())))
            {
                // SAFETY: `pixels` points to client memory validated by the GL entry point to
                // contain `imageSize` bytes of compressed texture data.
                angle::Span<const uint8_t> srcSpan =
                    ANGLE_UNSAFE_BUFFERS(angle::Span<const uint8_t>(pixels, imageSize));
                cpuInfo.data.assign(srcSpan.begin(), srcSpan.end());
                cpuInfo.format                  = compressedTexSubImageFormat.format;
                cpuInfo.type                    = GL_NONE;
                cpuInfo.compressed              = true;
                cpuInfo.contiguousSlicesWritten = static_cast<size_t>(vLevel.size.depth);
            }
        }
    }

    ASSERT(!getLevelInfo(target, level).lumaWorkaround.enabled &&
           !GetLevelInfo(features, originalInternalFormatInfo, compressedTexSubImageFormat.format)
                .lumaWorkaround.enabled);

    contextGL->markWorkSubmitted();
    return angle::Result::Continue;
}

angle::Result TextureGL::handleCopyImageSelfCopyRedefine(const gl::Context *context,
                                                         GLenum internalFormat,
                                                         GLenum initTexFormat,
                                                         GLenum initTexType,
                                                         const gl::Rectangle &sourceArea,
                                                         bool outside,
                                                         const gl::ImageIndex &destIndex,
                                                         gl::Framebuffer *source)
{
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const FunctionsGL *functions      = GetFunctionsGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);
    gl::TextureTarget target          = destIndex.getTarget();
    size_t level                      = static_cast<size_t>(destIndex.getLevelIndex());

    gl::Extents fbSize = source->getReadColorAttachment()->getSize();

    bool requiresInitialization =
        outside && (context->isRobustResourceInitEnabled() || context->isWebGL());
    const angle::MemoryBuffer *zero = nullptr;
    if (requiresInitialization)
    {
        ContextGL *contextGL = GetImplAs<ContextGL>(context);
        const gl::InternalFormat &initFormatInfo =
            gl::GetInternalFormatInfo(initTexFormat, initTexType);
        const size_t bufferSize =
            static_cast<size_t>(sourceArea.width) * sourceArea.height * initFormatInfo.pixelBytes;
        ANGLE_CHECK_GL_ALLOC(contextGL, context->getZeroFilledBuffer(bufferSize, &zero));

        gl::PixelUnpackState unpack;
        unpack.alignment = 1;
        ANGLE_TRY(stateManager->setPixelUnpackState(context, unpack));
        ANGLE_TRY(stateManager->setPixelUnpackBuffer(context, nullptr));
    }

    gl::Rectangle clippedArea;
    if (!ClipRectangle(sourceArea, gl::Rectangle(0, 0, fbSize.width, fbSize.height), &clippedArea))
    {
        // We won't be copying, but redefine the destination texture in case sourceArea is larger
        if (features.virtualizeBaseLevelAndMipchain.enabled && isVirtualMipchainType())
        {
            nativegl::TexStorageFormat texStorageFormat = nativegl::GetTexStorageFormat(
                functions, features,
                gl::GetInternalFormatInfo(internalFormat, GL_UNSIGNED_BYTE).sizedInternalFormat);
            ANGLE_TRY(allocateOrUpdateVirtualLevel(
                context, target, level, texStorageFormat.internalFormat,
                gl::Extents(sourceArea.width, sourceArea.height, 1)));
            if (zero && sourceArea.width > 0 && sourceArea.height > 0)
            {
                NativeLevelTarget nlt = getNativeLevelTarget(target, level);
                stateManager->bindTexture(gl::TextureTargetToType(nlt.target), nlt.textureID);
                ANGLE_GL_TRY(context,
                             functions->texSubImage2D(ToGLenum(nlt.target), nlt.level, 0, 0,
                                                      sourceArea.width, sourceArea.height,
                                                      initTexFormat, initTexType, zero->data()));
            }
            return angle::Result::Continue;
        }
        stateManager->bindTexture(getType(), mTextureID);
        ANGLE_GL_TRY_ALWAYS_CHECK(
            context,
            functions->texImage2D(ToGLenum(target), static_cast<GLint>(level), internalFormat,
                                  sourceArea.width, sourceArea.height, 0, initTexFormat,
                                  initTexType, zero ? zero->data() : nullptr));
        return angle::Result::Continue;
    }

    gl::Offset destOffset(clippedArea.x - sourceArea.x, clippedArea.y - sourceArea.y, 0);

    // Avoid redefining the texture before the copy, as that would invalidate the
    // source attachment. Copy through a temporary texture first.

    GLuint tempTex = 0;
    ANGLE_GL_TRY(context, functions->genTextures(1, &tempTex));

    // Always use a 2D temp texture to keep the attachment complete (especially for
    // cube maps in ES, which require all faces to be defined).
    stateManager->bindTexture(gl::TextureType::_2D, tempTex);
    ANGLE_GL_TRY(context, functions->copyTexImage2D(GL_TEXTURE_2D, 0, internalFormat, clippedArea.x,
                                                    clippedArea.y, clippedArea.width,
                                                    clippedArea.height, 0));

    GLuint tempFBO = 0;
    ANGLE_GL_TRY(context, functions->genFramebuffers(1, &tempFBO));
    stateManager->bindFramebuffer(GL_FRAMEBUFFER, tempFBO);
    ANGLE_GL_TRY(context, functions->framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                          GL_TEXTURE_2D, tempTex, 0));

    // Redefine the destination texture after the temp framebuffer is set up.
    if (features.virtualizeBaseLevelAndMipchain.enabled && isVirtualMipchainType())
    {
        nativegl::TexStorageFormat texStorageFormat = nativegl::GetTexStorageFormat(
            functions, features,
            gl::GetInternalFormatInfo(internalFormat, GL_UNSIGNED_BYTE).sizedInternalFormat);
        ANGLE_TRY(
            allocateOrUpdateVirtualLevel(context, target, level, texStorageFormat.internalFormat,
                                         gl::Extents(sourceArea.width, sourceArea.height, 1)));
        NativeLevelTarget nlt = getNativeLevelTarget(target, level);
        stateManager->bindTexture(gl::TextureTargetToType(nlt.target), nlt.textureID);
        if (zero && sourceArea.width > 0 && sourceArea.height > 0)
        {
            ANGLE_GL_TRY(context, functions->texSubImage2D(
                                      ToGLenum(nlt.target), nlt.level, 0, 0, sourceArea.width,
                                      sourceArea.height, initTexFormat, initTexType, zero->data()));
        }
        ANGLE_GL_TRY(context, functions->copyTexSubImage2D(ToGLenum(nlt.target), nlt.level,
                                                           destOffset.x, destOffset.y, 0, 0,
                                                           clippedArea.width, clippedArea.height));
    }
    else
    {
        stateManager->bindTexture(getType(), mTextureID);
        ANGLE_GL_TRY_ALWAYS_CHECK(
            context,
            functions->texImage2D(ToGLenum(target), static_cast<GLint>(level), internalFormat,
                                  sourceArea.width, sourceArea.height, 0, initTexFormat,
                                  initTexType, zero ? zero->data() : nullptr));

        ANGLE_GL_TRY(context, functions->copyTexSubImage2D(
                                  ToGLenum(target), static_cast<GLint>(level), destOffset.x,
                                  destOffset.y, 0, 0, clippedArea.width, clippedArea.height));
    }

    stateManager->deleteFramebuffer(tempFBO);
    stateManager->deleteTexture(tempTex);

    return angle::Result::Continue;
}

angle::Result TextureGL::copyImage(const gl::Context *context,
                                   const gl::ImageIndex &index,
                                   const gl::Rectangle &sourceArea,
                                   GLenum internalFormat,
                                   gl::Framebuffer *source)
{
    ContextGL *contextGL              = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    gl::TextureTarget target = index.getTarget();
    size_t level             = static_cast<size_t>(index.getLevelIndex());
    GLenum type              = source->getImplementationColorReadType(context);
    const gl::InternalFormat &originalInternalFormatInfo =
        gl::GetInternalFormatInfo(internalFormat, type);
    const bool useVirtualMipchain =
        features.virtualizeBaseLevelAndMipchain.enabled && isVirtualMipchainType();
    nativegl::CopyTexImageImageFormat copyTexImageFormat =
        nativegl::GetCopyTexImageImageFormat(functions, features, internalFormat);
    const GLenum destInternalFormat =
        useVirtualMipchain
            ? nativegl::GetTexStorageFormat(
                  functions, features,
                  gl::GetInternalFormatInfo(internalFormat, GL_UNSIGNED_BYTE).sizedInternalFormat)
                  .internalFormat
            : copyTexImageFormat.internalFormat;
    const gl::InternalFormat &copyInternalFormatInfo =
        gl::GetInternalFormatInfo(destInternalFormat, GL_UNSIGNED_BYTE);
    nativegl::TexImageFormat initTexImageFormat =
        nativegl::GetTexImageFormat(functions, features, internalFormat,
                                    copyInternalFormatInfo.format, copyInternalFormatInfo.type);

    if (features.flushBeforeDeleteTextureIfCopiedTo.enabled)
    {
        contextGL->setNeedsFlushBeforeDeleteTextures();
    }

    const FramebufferGL *sourceFramebufferGL = GetImplAs<FramebufferGL>(source);
    gl::Extents fbSize = sourceFramebufferGL->getState().getReadAttachment()->getSize();

    // Did the read area go outside the framebuffer?
    bool outside = sourceArea.x < 0 || sourceArea.y < 0 ||
                   sourceArea.x + sourceArea.width > fbSize.width ||
                   sourceArea.y + sourceArea.height > fbSize.height;

    // If fbo's read buffer and the target texture are the same texture but different levels,
    // and if the read buffer is a non-base texture level, then implementations glTexImage2D
    // may change the target texture and make the original texture mipmap incomplete, which in
    // turn makes the fbo incomplete.
    // To avoid that, we clamp BASE_LEVEL and MAX_LEVEL to the same texture level as the fbo's
    // read buffer attachment. See http://crbug.com/797235
    bool isSourceTextureSame                    = false;
    bool isSelfCopy                             = false;
    const gl::FramebufferAttachment *readBuffer = source->getReadColorAttachment();
    if (readBuffer && readBuffer->type() == GL_TEXTURE)
    {
        TextureGL *sourceTexture = GetImplAs<TextureGL>(readBuffer->getTexture());
        if (sourceTexture && sourceTexture->mTextureID == mTextureID && !useVirtualMipchain)
        {
            if (features.finishBeforeSameTextureCopy.enabled)
            {
                // Force a finish before copying from a framebuffer that has the destination texture
                // attached. Avoids an IMG driver bug if the source still has pending rendering.
                ANGLE_GL_TRY(context, functions->finish());
            }
            GLuint attachedTextureLevel = readBuffer->mipLevel();
            if (attachedTextureLevel != mState.getEffectiveBaseLevel())
            {
                ANGLE_TRY(setBaseLevel(context, attachedTextureLevel));
                ANGLE_TRY(setMaxLevel(context, attachedTextureLevel));
            }
        }
        const bool isSameCubeFace = readBuffer->cubeMapFace() == gl::TextureTarget::InvalidEnum ||
                                    readBuffer->cubeMapFace() == target;
        isSourceTextureSame = sourceTexture && sourceTexture->mTextureID == mTextureID;
        isSelfCopy          = isSourceTextureSame && isSameCubeFace &&
                     readBuffer->mipLevel() == static_cast<GLint>(level);
    }

    LevelInfoGL levelInfo = GetLevelInfo(features, originalInternalFormatInfo, destInternalFormat);
    if (features.forceLumaWorkaroundForSameTextureCopyTexImage2D.enabled &&
        !levelInfo.lumaWorkaround.enabled && originalInternalFormatInfo.isLUMA() &&
        isSourceTextureSame)
    {
        ASSERT(functions->isAtLeastGLES(gl::Version(3, 0)));
        const GLenum workaroundFormat =
            (originalInternalFormatInfo.format == GL_LUMINANCE_ALPHA) ? GL_RG : GL_RED;
        levelInfo.lumaWorkaround = LUMAWorkaroundGL(true, workaroundFormat);
    }

    if (isSelfCopy)
    {
        ANGLE_TRY(handleCopyImageSelfCopyRedefine(
            context, copyTexImageFormat.internalFormat, initTexImageFormat.format,
            initTexImageFormat.type, sourceArea, outside, index, source));
        setLevelInfo(context, target, level, 1, levelInfo);

        contextGL->markWorkSubmitted();
        return angle::Result::Continue;
    }

    if (useVirtualMipchain)
    {
        ANGLE_TRY(
            allocateOrUpdateVirtualLevel(context, target, level, destInternalFormat,
                                         gl::Extents(sourceArea.width, sourceArea.height, 1)));
        setLevelInfo(context, target, level, 1, levelInfo);
    }

    NativeLevelTarget nlt    = getNativeLevelTarget(target, level);
    gl::TextureType bindType = gl::TextureTargetToType(nlt.target);
    stateManager->bindTexture(bindType, nlt.textureID);

    // TODO: Find a way to initialize the texture entirely in the gl level with ensureInitialized.
    // Right now there is no easy way to pre-fill the texture when it is being redefined with
    // partially uninitialized data.
    bool requiresInitialization =
        outside && (context->isRobustResourceInitEnabled() || context->isWebGL());

    // When robust resource initialization is enabled, the area outside the framebuffer must be
    // zeroed. We just zero the whole thing before copying into the area that overlaps the
    // framebuffer.
    if (requiresInitialization)
    {
        const gl::InternalFormat &initFormatInfo =
            gl::GetInternalFormatInfo(initTexImageFormat.format, initTexImageFormat.type);
        GLuint pixelBytes = initFormatInfo.pixelBytes;
        // TODO(b/495363705): Validate if this CheckedNumeric is required, remove either this TODO
        // or the CheckedNumeric based on the result.
        angle::CheckedNumeric<size_t> checkedBufferSize = angle::base::CheckMul(
            angle::base::CheckMul(sourceArea.width, sourceArea.height), pixelBytes);
        ANGLE_CHECK_GL_MATH(contextGL, checkedBufferSize.IsValid());
        const angle::MemoryBuffer *zero;
        ANGLE_CHECK_GL_ALLOC(contextGL,
                             context->getZeroFilledBuffer(checkedBufferSize.ValueOrDie(), &zero));

        gl::PixelUnpackState unpack;
        unpack.alignment = 1;
        ANGLE_TRY(stateManager->setPixelUnpackState(context, unpack));
        ANGLE_TRY(stateManager->setPixelUnpackBuffer(context, nullptr));

        if (useVirtualMipchain)
        {
            if (sourceArea.width > 0 && sourceArea.height > 0)
            {
                ANGLE_GL_TRY(context, functions->texSubImage2D(
                                          ToGLenum(nlt.target), nlt.level, 0, 0, sourceArea.width,
                                          sourceArea.height, initTexImageFormat.format,
                                          initTexImageFormat.type, zero->data()));
            }
        }
        else
        {
            ANGLE_GL_TRY_ALWAYS_CHECK(
                context, functions->texImage2D(ToGLenum(target), static_cast<GLint>(level),
                                               copyTexImageFormat.internalFormat, sourceArea.width,
                                               sourceArea.height, 0, initTexImageFormat.format,
                                               initTexImageFormat.type, zero->data()));
        }
    }

    // Clip source area to framebuffer and copy if remaining area is not empty.
    gl::Rectangle clippedArea;
    if (ClipRectangle(sourceArea, gl::Rectangle(0, 0, fbSize.width, fbSize.height), &clippedArea))
    {
        gl::Offset destOffset(clippedArea.x - sourceArea.x, clippedArea.y - sourceArea.y, 0);

        if (levelInfo.lumaWorkaround.enabled)
        {
            BlitGL *blitter = GetBlitGL(context);

            if (requiresInitialization || useVirtualMipchain)
            {
                ANGLE_TRY(blitter->copySubImageToLUMAWorkaroundTexture(
                    context, nlt.textureID, bindType, nlt.target, levelInfo.sourceFormat, nlt.level,
                    destOffset, clippedArea, source));
            }
            else
            {
                ANGLE_TRY(blitter->copyImageToLUMAWorkaroundTexture(
                    context, nlt.textureID, bindType, nlt.target, levelInfo.sourceFormat, nlt.level,
                    clippedArea, copyTexImageFormat.internalFormat, source));
            }
        }
        else
        {
            ASSERT(nativegl::UseTexImage2D(bindType));
            stateManager->bindFramebuffer(GL_READ_FRAMEBUFFER,
                                          sourceFramebufferGL->getFramebufferID());
            if (features.emulateCopyTexImage2DFromRenderbuffers.enabled && readBuffer &&
                readBuffer->type() == GL_RENDERBUFFER)
            {
                BlitGL *blitter = GetBlitGL(context);
                ANGLE_TRY(blitter->blitColorBufferWithShader(
                    context, source, nlt.textureID, nlt.target, nlt.level, clippedArea,
                    gl::Rectangle(destOffset.x, destOffset.y, clippedArea.width,
                                  clippedArea.height),
                    GL_NEAREST, true));
            }
            else if (requiresInitialization || useVirtualMipchain)
            {
                ANGLE_GL_TRY(context, functions->copyTexSubImage2D(
                                          ToGLenum(nlt.target), nlt.level, destOffset.x,
                                          destOffset.y, clippedArea.x, clippedArea.y,
                                          clippedArea.width, clippedArea.height));
            }
            else
            {
                if (features.emulateCopyTexImage2D.enabled)
                {
                    ANGLE_GL_TRY_ALWAYS_CHECK(
                        context,
                        functions->texImage2D(ToGLenum(target), static_cast<GLint>(level),
                                              copyTexImageFormat.internalFormat, sourceArea.width,
                                              sourceArea.height, 0, initTexImageFormat.format,
                                              initTexImageFormat.type, nullptr));
                    ANGLE_GL_TRY_ALWAYS_CHECK(
                        context,
                        functions->copyTexSubImage2D(ToGLenum(target), static_cast<GLint>(level), 0,
                                                     0, sourceArea.x, sourceArea.y,
                                                     sourceArea.width, sourceArea.height));
                }
                else
                {
                    ANGLE_GL_TRY_ALWAYS_CHECK(
                        context, functions->copyTexImage2D(
                                     ToGLenum(target), static_cast<GLint>(level),
                                     copyTexImageFormat.internalFormat, sourceArea.x, sourceArea.y,
                                     sourceArea.width, sourceArea.height, 0));
                }
            }
        }
        setLevelInfo(context, target, level, 1, levelInfo);
    }

    contextGL->markWorkSubmitted();
    return angle::Result::Continue;
}

angle::Result TextureGL::copySubImage(const gl::Context *context,
                                      const gl::ImageIndex &index,
                                      const gl::Offset &destOffset,
                                      const gl::Rectangle &sourceArea,
                                      gl::Framebuffer *source)
{
    ContextGL *contextGL              = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    gl::TextureTarget target                 = index.getTarget();
    size_t level                             = static_cast<size_t>(index.getLevelIndex());
    const FramebufferGL *sourceFramebufferGL = GetImplAs<FramebufferGL>(source);

    // Clip source area to framebuffer.
    const gl::Extents fbSize = sourceFramebufferGL->getState().getReadAttachment()->getSize();
    gl::Rectangle clippedArea;
    if (!ClipRectangle(sourceArea, gl::Rectangle(0, 0, fbSize.width, fbSize.height), &clippedArea))
    {
        // nothing to do
        return angle::Result::Continue;
    }
    gl::Offset clippedOffset(destOffset.x + clippedArea.x - sourceArea.x,
                             destOffset.y + clippedArea.y - sourceArea.y, destOffset.z);

    if (features.finishBeforeSameTextureCopy.enabled)
    {
        const gl::FramebufferAttachment *readBuffer = source->getReadColorAttachment();
        if (readBuffer && readBuffer->type() == GL_TEXTURE)
        {
            const TextureGL *sourceTexture = GetImplAs<TextureGL>(readBuffer->getTexture());
            if (sourceTexture && sourceTexture->mTextureID == mTextureID)
            {
                // Force a finish before copying from a framebuffer that has the destination texture
                // attached. Avoids an IMG driver bug if the source still has pending rendering.
                ANGLE_GL_TRY(context, functions->finish());
            }
        }
    }

    NativeLevelTarget nlt    = getNativeLevelTarget(target, level);
    gl::TextureType bindType = gl::TextureTargetToType(nlt.target);
    stateManager->bindTexture(bindType, nlt.textureID);
    GLenum framebufferTarget =
        stateManager->getHasSeparateFramebufferBindings() ? GL_READ_FRAMEBUFFER : GL_FRAMEBUFFER;
    stateManager->bindFramebuffer(framebufferTarget, sourceFramebufferGL->getFramebufferID());

    const LevelInfoGL &levelInfo = getLevelInfo(target, level);
    if (levelInfo.lumaWorkaround.enabled)
    {
        BlitGL *blitter = GetBlitGL(context);
        ANGLE_TRY(blitter->copySubImageToLUMAWorkaroundTexture(
            context, nlt.textureID, bindType, nlt.target, levelInfo.sourceFormat, nlt.level,
            clippedOffset, clippedArea, source));
    }
    else
    {
        if (nativegl::UseTexImage2D(bindType))
        {
            ASSERT(clippedOffset.z == 0);
            if (features.emulateCopyTexImage2DFromRenderbuffers.enabled &&
                source->getReadColorAttachment() &&
                source->getReadColorAttachment()->type() == GL_RENDERBUFFER)
            {
                BlitGL *blitter = GetBlitGL(context);
                ANGLE_TRY(blitter->blitColorBufferWithShader(
                    context, source, nlt.textureID, nlt.target, nlt.level, clippedArea,
                    gl::Rectangle(clippedOffset.x, clippedOffset.y, clippedArea.width,
                                  clippedArea.height),
                    GL_NEAREST, true));
            }
            else
            {
                ANGLE_GL_TRY(context, functions->copyTexSubImage2D(
                                          ToGLenum(nlt.target), nlt.level, clippedOffset.x,
                                          clippedOffset.y, clippedArea.x, clippedArea.y,
                                          clippedArea.width, clippedArea.height));
            }
        }
        else
        {
            ASSERT(nativegl::UseTexImage3D(bindType));
            ANGLE_GL_TRY(context, functions->copyTexSubImage3D(
                                      ToGLenum(nlt.target), nlt.level, clippedOffset.x,
                                      clippedOffset.y, clippedOffset.z, clippedArea.x,
                                      clippedArea.y, clippedArea.width, clippedArea.height));
        }
    }

    if (features.flushBeforeDeleteTextureIfCopiedTo.enabled)
    {
        contextGL->setNeedsFlushBeforeDeleteTextures();
    }

    contextGL->markWorkSubmitted();
    return angle::Result::Continue;
}

angle::Result TextureGL::copyTexture(const gl::Context *context,
                                     const gl::ImageIndex &index,
                                     GLenum internalFormat,
                                     GLenum type,
                                     gl::LevelIndex sourceLevel,
                                     bool unpackFlipY,
                                     bool unpackPremultiplyAlpha,
                                     bool unpackUnmultiplyAlpha,
                                     const gl::Texture *source)
{
    gl::TextureTarget target  = index.getTarget();
    size_t level              = static_cast<size_t>(index.getLevelIndex());
    const TextureGL *sourceGL = GetImplAs<TextureGL>(source);
    const gl::ImageDesc &sourceImageDesc = sourceGL->mState.getImageDesc(
        NonCubeTextureTypeToTarget(source->getType()), sourceLevel.get());
    gl::Rectangle sourceArea(0, 0, sourceImageDesc.size.width, sourceImageDesc.size.height);

    ANGLE_TRY(reserveTexImageToBeFilled(context, target, level, internalFormat,
                                        sourceImageDesc.size, gl::GetUnsizedFormat(internalFormat),
                                        type));

    const gl::InternalFormat &destFormatInfo = gl::GetInternalFormatInfo(internalFormat, type);
    return copySubTextureHelper(context, target, level, gl::Offset(0, 0, 0), sourceLevel.get(),
                                sourceArea, destFormatInfo, unpackFlipY, unpackPremultiplyAlpha,
                                unpackUnmultiplyAlpha, source);
}

angle::Result TextureGL::copySubTexture(const gl::Context *context,
                                        const gl::ImageIndex &index,
                                        const gl::Offset &destOffset,
                                        gl::LevelIndex sourceLevel,
                                        const gl::Box &sourceBox,
                                        bool unpackFlipY,
                                        bool unpackPremultiplyAlpha,
                                        bool unpackUnmultiplyAlpha,
                                        const gl::Texture *source)
{
    gl::TextureTarget target                 = index.getTarget();
    size_t level                             = static_cast<size_t>(index.getLevelIndex());
    const gl::InternalFormat &destFormatInfo = *mState.getImageDesc(target, level).format.info;
    return copySubTextureHelper(context, target, level, destOffset, sourceLevel.get(),
                                sourceBox.toRect(), destFormatInfo, unpackFlipY,
                                unpackPremultiplyAlpha, unpackUnmultiplyAlpha, source);
}

angle::Result TextureGL::copySubTextureHelper(const gl::Context *context,
                                              gl::TextureTarget target,
                                              size_t level,
                                              const gl::Offset &destOffset,
                                              GLint sourceLevel,
                                              const gl::Rectangle &sourceArea,
                                              const gl::InternalFormat &destFormat,
                                              bool unpackFlipY,
                                              bool unpackPremultiplyAlpha,
                                              bool unpackUnmultiplyAlpha,
                                              const gl::Texture *source)
{
    ContextGL *contextGL              = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions      = GetFunctionsGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);
    BlitGL *blitter                   = GetBlitGL(context);

    TextureGL *sourceGL = GetImplAs<TextureGL>(source);
    const gl::ImageDesc &sourceImageDesc =
        sourceGL->mState.getImageDesc(NonCubeTextureTypeToTarget(source->getType()), sourceLevel);

    if (features.flushBeforeDeleteTextureIfCopiedTo.enabled)
    {
        // Conservatively indicate that this workaround is necessary. Not clear
        // if it is on this code path, but added for symmetry.
        contextGL->setNeedsFlushBeforeDeleteTextures();
    }

    // Check is this is a simple copySubTexture that can be done with a copyTexSubImage
    ASSERT(sourceGL->getType() == gl::TextureType::_2D ||
           source->getType() == gl::TextureType::External ||
           source->getType() == gl::TextureType::Rectangle);
    const LevelInfoGL &sourceLevelInfo =
        sourceGL->getLevelInfo(NonCubeTextureTypeToTarget(source->getType()), sourceLevel);
    bool needsLumaWorkaround = sourceLevelInfo.lumaWorkaround.enabled;

    const gl::InternalFormat &sourceFormatInfo = *sourceImageDesc.format.info;
    GLenum sourceFormat                        = sourceFormatInfo.format;
    bool sourceFormatContainSupersetOfDestFormat =
        (sourceFormat == destFormat.format && sourceFormat != GL_BGRA_EXT) ||
        (sourceFormat == GL_RGBA && destFormat.format == GL_RGB);
    bool sourceSRGB = sourceFormatInfo.colorEncoding == GL_SRGB;

    GLenum sourceComponentType = sourceFormatInfo.componentType;
    GLenum destComponentType   = destFormat.componentType;
    bool destSRGB              = destFormat.colorEncoding == GL_SRGB;
    if (!unpackFlipY && unpackPremultiplyAlpha == unpackUnmultiplyAlpha && !needsLumaWorkaround &&
        sourceFormatContainSupersetOfDestFormat && sourceComponentType == destComponentType &&
        !destSRGB && !sourceSRGB && sourceGL->getType() == gl::TextureType::_2D)
    {
        bool copySucceeded = false;
        ANGLE_TRY(blitter->copyTexSubImage(context, sourceGL, sourceLevel, this, target, level,
                                           sourceArea, destOffset, &copySucceeded));
        if (copySucceeded)
        {
            contextGL->markWorkSubmitted();
            return angle::Result::Continue;
        }
    }

    // Check if the destination is renderable and copy on the GPU
    const LevelInfoGL &destLevelInfo = getLevelInfo(target, level);
    // todo(jonahr): http://crbug.com/773861
    // Behavior for now is to fallback to CPU readback implementation if the destination texture
    // is a luminance format. The correct solution is to handle both source and destination in the
    // luma workaround.
    if (!destSRGB && !destLevelInfo.lumaWorkaround.enabled &&
        nativegl::SupportsNativeRendering(functions, getType(), destLevelInfo.nativeInternalFormat))
    {
        ANGLE_TRY(sourceGL->setBaseLevel(context, static_cast<GLuint>(sourceLevel)));
        bool copySucceeded    = false;
        NativeLevelTarget nlt = getNativeLevelTarget(target, level);
        ANGLE_TRY(blitter->copySubTexture(
            context, sourceGL, sourceLevel, sourceComponentType, nlt.textureID, nlt.target,
            nlt.level, destComponentType, sourceImageDesc.size, sourceArea, destOffset,
            needsLumaWorkaround, sourceLevelInfo.sourceFormat, unpackFlipY, unpackPremultiplyAlpha,
            unpackUnmultiplyAlpha, sourceSRGB, &copySucceeded));
        if (copySucceeded)
        {
            contextGL->markWorkSubmitted();
            return angle::Result::Continue;
        }
    }

    // Fall back to CPU-readback
    ANGLE_TRY(blitter->copySubTextureCPUReadback(
        context, sourceGL, sourceLevel, sourceFormatInfo.sizedInternalFormat, this, target, level,
        destFormat.format, destFormat.type, sourceImageDesc.size, sourceArea, destOffset,
        needsLumaWorkaround, sourceLevelInfo.sourceFormat, unpackFlipY, unpackPremultiplyAlpha,
        unpackUnmultiplyAlpha));
    contextGL->markWorkSubmitted();
    return angle::Result::Continue;
}

angle::Result TextureGL::setStorage(const gl::Context *context,
                                    gl::TextureType type,
                                    size_t levels,
                                    GLenum internalFormat,
                                    const gl::Extents &size)
{
    ContextGL *contextGL              = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    if (features.reattachFboDepthStencilOnReallocation.enabled)
    {
        onStateChange(angle::SubjectMessage::ObjectReallocated);
    }

    if (features.reattachTextureToFboAfterLayerIncrease.enabled &&
        gl::IsLayeredTextureType(getType()))
    {
        for (size_t level = 0; level < levels; level++)
        {
            const gl::ImageDesc &desc = mState.getImageDesc(gl::TextureTarget::_2DArray, level);
            if (size.depth > desc.size.depth)
            {
                onStateChange(angle::SubjectMessage::TextureLayerCountIncreased);
                break;
            }
        }
    }

    const gl::InternalFormat &originalInternalFormatInfo =
        gl::GetSizedInternalFormatInfo(internalFormat);
    nativegl::TexStorageFormat texStorageFormat =
        nativegl::GetTexStorageFormat(functions, features, internalFormat);

    if (features.virtualizeBaseLevelAndMipchain.enabled && isVirtualMipchainType())
    {
        clearVirtualLevels(context);
        if (mVirtualMipChain.has_value() && mVirtualMipChain->levels > 0)
        {
            ANGLE_TRY(recreateTexture(context));
        }

        VirtualMipChain &mipChain = mVirtualMipChain.emplace(mLevelInfo.size());
        mipChain.baseLevel        = 0;
        mipChain.levels           = levels;
        mipChain.baseSize         = size;
        mipChain.internalFormat   = texStorageFormat.internalFormat;

        ANGLE_TRY(allocateTexStorage(context, mTextureID, type, levels,
                                     texStorageFormat.internalFormat, size));

        for (size_t level = 0; level < levels; ++level)
        {
            gl::Extents levelSize(
                std::max(1, size.width >> level), std::max(1, size.height >> level),
                type == gl::TextureType::_3D ? std::max(1, size.depth >> level) : size.depth);
            if (type == gl::TextureType::CubeMap)
            {
                for (gl::TextureTarget face : gl::AllCubeFaceTextureTargets())
                {
                    size_t idx                                 = GetLevelInfoIndex(face, level);
                    mipChain.virtualLevels[idx].texID          = 0;
                    mipChain.virtualLevels[idx].size           = levelSize;
                    mipChain.virtualLevels[idx].internalFormat = texStorageFormat.internalFormat;
                }
            }
            else
            {
                gl::TextureTarget target                   = NonCubeTextureTypeToTarget(type);
                size_t idx                                 = GetLevelInfoIndex(target, level);
                mipChain.virtualLevels[idx].texID          = 0;
                mipChain.virtualLevels[idx].size           = levelSize;
                mipChain.virtualLevels[idx].internalFormat = texStorageFormat.internalFormat;
            }
        }

        if (mState.getEffectiveBaseLevel() != 0)
        {
            mLocalDirtyBits.set(gl::Texture::DIRTY_BIT_IMPLEMENTATION);
            onStateChange(angle::SubjectMessage::DirtyBitsFlagged);
        }

        setLevelInfo(
            context, type, 0, levels,
            GetLevelInfo(features, originalInternalFormatInfo, texStorageFormat.internalFormat));
        return angle::Result::Continue;
    }

    stateManager->bindTexture(getType(), mTextureID);
    if (nativegl::UseTexImage2D(getType()))
    {
        ASSERT(size.depth == 1);
        if (functions->texStorage2D)
        {
            ANGLE_GL_TRY_ALWAYS_CHECK(
                context,
                functions->texStorage2D(ToGLenum(type), static_cast<GLsizei>(levels),
                                        texStorageFormat.internalFormat, size.width, size.height));
        }
        else
        {
            // Make sure no pixel unpack buffer is bound
            stateManager->bindBuffer(gl::BufferBinding::PixelUnpack, 0,
                                     context->isHardenedContext());

            const gl::InternalFormat &internalFormatInfo =
                gl::GetSizedInternalFormatInfo(internalFormat);

            // Internal format must be sized
            ASSERT(internalFormatInfo.sized);

            for (size_t level = 0; level < levels; level++)
            {
                gl::Extents levelSize(std::max(size.width >> level, 1),
                                      std::max(size.height >> level, 1), 1);

                if (getType() == gl::TextureType::_2D || getType() == gl::TextureType::Rectangle)
                {
                    if (internalFormatInfo.compressed)
                    {
                        nativegl::CompressedTexSubImageFormat compressedTexImageFormat =
                            nativegl::GetCompressedSubTexImageFormat(functions, features,
                                                                     internalFormat);

                        GLuint dataSize = 0;
                        ANGLE_CHECK_GL_MATH(
                            contextGL,
                            internalFormatInfo.computeCompressedImageSize(levelSize, &dataSize));
                        ANGLE_GL_TRY_ALWAYS_CHECK(
                            context,
                            functions->compressedTexImage2D(
                                ToGLenum(type), static_cast<GLint>(level),
                                compressedTexImageFormat.format, levelSize.width, levelSize.height,
                                0, static_cast<GLsizei>(dataSize), nullptr));
                    }
                    else
                    {
                        nativegl::TexImageFormat texImageFormat = nativegl::GetTexImageFormat(
                            functions, features, internalFormat, internalFormatInfo.format,
                            internalFormatInfo.type);

                        ANGLE_GL_TRY_ALWAYS_CHECK(
                            context,
                            functions->texImage2D(ToGLenum(type), static_cast<GLint>(level),
                                                  texImageFormat.internalFormat, levelSize.width,
                                                  levelSize.height, 0, texImageFormat.format,
                                                  texImageFormat.type, nullptr));
                    }
                }
                else
                {
                    ASSERT(getType() == gl::TextureType::CubeMap);
                    for (gl::TextureTarget face : gl::AllCubeFaceTextureTargets())
                    {
                        if (internalFormatInfo.compressed)
                        {
                            nativegl::CompressedTexSubImageFormat compressedTexImageFormat =
                                nativegl::GetCompressedSubTexImageFormat(functions, features,
                                                                         internalFormat);

                            GLuint dataSize = 0;
                            ANGLE_CHECK_GL_MATH(contextGL,
                                                internalFormatInfo.computeCompressedImageSize(
                                                    levelSize, &dataSize));
                            ANGLE_GL_TRY_ALWAYS_CHECK(
                                context,
                                functions->compressedTexImage2D(
                                    ToGLenum(face), static_cast<GLint>(level),
                                    compressedTexImageFormat.format, levelSize.width,
                                    levelSize.height, 0, static_cast<GLsizei>(dataSize), nullptr));
                        }
                        else
                        {
                            nativegl::TexImageFormat texImageFormat = nativegl::GetTexImageFormat(
                                functions, features, internalFormat, internalFormatInfo.format,
                                internalFormatInfo.type);

                            ANGLE_GL_TRY_ALWAYS_CHECK(
                                context, functions->texImage2D(
                                             ToGLenum(face), static_cast<GLint>(level),
                                             texImageFormat.internalFormat, levelSize.width,
                                             levelSize.height, 0, texImageFormat.format,
                                             texImageFormat.type, nullptr));
                        }
                    }
                }
            }
        }
    }
    else
    {
        ASSERT(nativegl::UseTexImage3D(getType()));
        const gl::InternalFormat &internalFormatInfo =
            gl::GetSizedInternalFormatInfo(internalFormat);
        const bool bypassTexStorage3D = type == gl::TextureType::_3D &&
                                        internalFormatInfo.compressed &&
                                        features.emulateImmutableCompressedTexture3D.enabled;
        if (functions->texStorage3D && !bypassTexStorage3D)
        {
            ANGLE_GL_TRY_ALWAYS_CHECK(
                context, functions->texStorage3D(ToGLenum(type), static_cast<GLsizei>(levels),
                                                 texStorageFormat.internalFormat, size.width,
                                                 size.height, size.depth));
        }
        else
        {
            // Make sure no pixel unpack buffer is bound
            stateManager->bindBuffer(gl::BufferBinding::PixelUnpack, 0,
                                     context->isHardenedContext());

            // Internal format must be sized
            ASSERT(internalFormatInfo.sized);

            for (GLsizei i = 0; i < static_cast<GLsizei>(levels); i++)
            {
                gl::Extents levelSize(
                    std::max(size.width >> i, 1), std::max(size.height >> i, 1),
                    getType() == gl::TextureType::_3D ? std::max(size.depth >> i, 1) : size.depth);

                if (internalFormatInfo.compressed)
                {
                    nativegl::CompressedTexSubImageFormat compressedTexImageFormat =
                        nativegl::GetCompressedSubTexImageFormat(functions, features,
                                                                 internalFormat);

                    GLuint dataSize = 0;
                    ANGLE_CHECK_GL_MATH(contextGL, internalFormatInfo.computeCompressedImageSize(
                                                       levelSize, &dataSize));
                    ANGLE_GL_TRY_ALWAYS_CHECK(
                        context, functions->compressedTexImage3D(
                                     ToGLenum(type), i, compressedTexImageFormat.format,
                                     levelSize.width, levelSize.height, levelSize.depth, 0,
                                     static_cast<GLsizei>(dataSize), nullptr));
                }
                else
                {
                    nativegl::TexImageFormat texImageFormat = nativegl::GetTexImageFormat(
                        functions, features, internalFormat, internalFormatInfo.format,
                        internalFormatInfo.type);

                    ANGLE_GL_TRY_ALWAYS_CHECK(
                        context,
                        functions->texImage3D(ToGLenum(type), i, texImageFormat.internalFormat,
                                              levelSize.width, levelSize.height, levelSize.depth, 0,
                                              texImageFormat.format, texImageFormat.type, nullptr));
                }
            }
        }
    }

    setLevelInfo(
        context, type, 0, levels,
        GetLevelInfo(features, originalInternalFormatInfo, texStorageFormat.internalFormat));

    return angle::Result::Continue;
}

angle::Result TextureGL::setStorageMultisample(const gl::Context *context,
                                               gl::TextureType type,
                                               GLsizei samples,
                                               GLint internalformat,
                                               const gl::Extents &size,
                                               bool fixedSampleLocations)
{
    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    if (features.reattachFboDepthStencilOnReallocation.enabled)
    {
        onStateChange(angle::SubjectMessage::ObjectReallocated);
    }

    const gl::InternalFormat &originalInternalFormatInfo =
        gl::GetSizedInternalFormatInfo(internalformat);
    nativegl::TexStorageFormat texStorageFormat =
        nativegl::GetTexStorageFormat(functions, features, internalformat);

    stateManager->bindTexture(getType(), mTextureID);

    if (nativegl::UseTexImage2D(getType()))
    {
        ASSERT(size.depth == 1);
        if (functions->texStorage2DMultisample)
        {
            ANGLE_GL_TRY_ALWAYS_CHECK(
                context, functions->texStorage2DMultisample(
                             ToGLenum(type), samples, texStorageFormat.internalFormat, size.width,
                             size.height, gl::ConvertToGLBoolean(fixedSampleLocations)));
        }
        else
        {
            // texImage2DMultisample is similar to texStorage2DMultisample of es 3.1 core feature,
            // On macos and some old drivers which doesn't support OpenGL ES 3.1, the function can
            // be supported by ARB_texture_multisample or OpenGL 3.2 core feature.
            ANGLE_GL_TRY_ALWAYS_CHECK(
                context, functions->texImage2DMultisample(
                             ToGLenum(type), samples, texStorageFormat.internalFormat, size.width,
                             size.height, gl::ConvertToGLBoolean(fixedSampleLocations)));
        }
    }
    else
    {
        ASSERT(nativegl::UseTexImage3D(getType()));
        ANGLE_GL_TRY_ALWAYS_CHECK(
            context, functions->texStorage3DMultisample(
                         ToGLenum(type), samples, texStorageFormat.internalFormat, size.width,
                         size.height, size.depth, gl::ConvertToGLBoolean(fixedSampleLocations)));
    }

    setLevelInfo(
        context, type, 0, 1,
        GetLevelInfo(features, originalInternalFormatInfo, texStorageFormat.internalFormat));

    return angle::Result::Continue;
}

angle::Result TextureGL::setStorageExternalMemory(const gl::Context *context,
                                                  gl::TextureType type,
                                                  size_t levels,
                                                  GLenum internalFormat,
                                                  const gl::Extents &size,
                                                  gl::MemoryObject *memoryObject,
                                                  GLuint64 offset,
                                                  GLbitfield createFlags,
                                                  GLbitfield usageFlags,
                                                  const void *imageCreateInfoPNext)
{
    // GL_ANGLE_external_objects_flags not supported.
    ASSERT(createFlags == std::numeric_limits<uint32_t>::max());
    ASSERT(usageFlags == std::numeric_limits<uint32_t>::max());
    ASSERT(imageCreateInfoPNext == nullptr);

    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    MemoryObjectGL *memoryObjectGL = GetImplAs<MemoryObjectGL>(memoryObject);

    const gl::InternalFormat &originalInternalFormatInfo =
        gl::GetSizedInternalFormatInfo(internalFormat);
    nativegl::TexStorageFormat texStorageFormat =
        nativegl::GetTexStorageFormat(functions, features, internalFormat);

    stateManager->bindTexture(getType(), mTextureID);
    if (nativegl::UseTexImage2D(getType()))
    {
        ANGLE_GL_TRY_ALWAYS_CHECK(
            context,
            functions->texStorageMem2DEXT(ToGLenum(type), static_cast<GLsizei>(levels),
                                          texStorageFormat.internalFormat, size.width, size.height,
                                          memoryObjectGL->getMemoryObjectID(), offset));
    }
    else
    {
        ASSERT(nativegl::UseTexImage3D(getType()));
        ANGLE_GL_TRY_ALWAYS_CHECK(
            context,
            functions->texStorageMem3DEXT(ToGLenum(type), static_cast<GLsizei>(levels),
                                          texStorageFormat.internalFormat, size.width, size.height,
                                          size.depth, memoryObjectGL->getMemoryObjectID(), offset));
    }

    setLevelInfo(
        context, type, 0, levels,
        GetLevelInfo(features, originalInternalFormatInfo, texStorageFormat.internalFormat));

    return angle::Result::Continue;
}

angle::Result TextureGL::setImageExternal(const gl::Context *context,
                                          gl::TextureType type,
                                          egl::Stream *stream,
                                          const egl::Stream::GLTextureDescription &desc)
{
    ANGLE_GL_UNREACHABLE(GetImplAs<ContextGL>(context));
    return angle::Result::Stop;
}

angle::Result TextureGL::generateMipmap(const gl::Context *context)
{
    ContextGL *contextGL              = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    if (features.flushBeforeGenerateMipmap.enabled)
    {
        // Force a flush before generating the mipmap, which avoids bad states in the IMG driver.
        ANGLE_GL_TRY(context, stateManager->forcefullyFlush());
    }

    const GLuint effectiveBaseLevel = mState.getEffectiveBaseLevel();
    const GLuint maxLevel           = mState.getMipmapMaxLevel();

    const gl::ImageDesc &baseLevelDesc                = mState.getBaseLevelDesc();
    const gl::InternalFormat &baseLevelInternalFormat = *baseLevelDesc.format.info;

    const LevelInfoGL &baseLevelInfo = getBaseLevelInfo();

    if (mVirtualMipChain.has_value())
    {
        const GLuint levelCount = maxLevel - effectiveBaseLevel + 1;
        const bool is3D         = getType() == gl::TextureType::_3D;

        // Levels in (effectiveBaseLevel, maxLevel] will be overwritten by glGenerateMipmap.
        // Delete any per-level textures for those levels and set their virtual level metadata
        // to the expected mipmap dimensions and internal format before syncing the mipchain.
        for (GLuint levelIdx = 1; levelIdx < levelCount; ++levelIdx)
        {
            size_t level = effectiveBaseLevel + levelIdx;
            gl::Extents levelSize(std::max(baseLevelDesc.size.width >> levelIdx, 1),
                                  std::max(baseLevelDesc.size.height >> levelIdx, 1),
                                  is3D ? std::max(baseLevelDesc.size.depth >> levelIdx, 1)
                                       : baseLevelDesc.size.depth);
            if (getType() == gl::TextureType::CubeMap)
            {
                for (gl::TextureTarget face : gl::AllCubeFaceTextureTargets())
                {
                    size_t idx               = GetLevelInfoIndex(face, level);
                    VirtualLevelInfo &vLevel = mVirtualMipChain->virtualLevels[idx];
                    stateManager->deleteTexture(vLevel.texID);
                    vLevel.texID          = 0;
                    vLevel.size           = levelSize;
                    vLevel.internalFormat = baseLevelInfo.nativeInternalFormat;
                }
            }
            else
            {
                gl::TextureTarget target = NonCubeTextureTypeToTarget(getType());
                size_t idx               = GetLevelInfoIndex(target, level);
                VirtualLevelInfo &vLevel = mVirtualMipChain->virtualLevels[idx];
                stateManager->deleteTexture(vLevel.texID);
                vLevel.texID          = 0;
                vLevel.size           = levelSize;
                vLevel.internalFormat = baseLevelInfo.nativeInternalFormat;
                if (features.copyImageSubData3DFailsOnTailMipLevels.enabled)
                {
                    vLevel.tailMip3DCpuData.clearCpuData();
                }
            }
        }

        mAppliedBaseLevel = effectiveBaseLevel;
        ANGLE_TRY(syncVirtualMipchain(context));

        stateManager->bindTexture(getType(), mTextureID);
        const GLuint maxLevelLimit   = GetMaxMipmapLevel(context->getCaps(), getType());
        const GLuint clampedMaxLevel = std::min(mState.getEffectiveMaxLevel(), maxLevelLimit);
        const GLuint nativeMaxLevel =
            (mState.getEffectiveMaxLevel() == gl::kInitialMaxLevel)
                ? gl::kInitialMaxLevel
                : ((clampedMaxLevel >= mVirtualMipChain->baseLevel)
                       ? static_cast<GLuint>(clampedMaxLevel - mVirtualMipChain->baseLevel)
                       : 0);
        if (mAppliedMaxLevel != nativeMaxLevel)
        {
            mAppliedMaxLevel = nativeMaxLevel;
            ANGLE_GL_TRY(context,
                         functions->texParameteri(nativegl::GetTextureBindingTarget(getType()),
                                                  GL_TEXTURE_MAX_LEVEL, mAppliedMaxLevel));
        }

        if (getType() == gl::TextureType::_2D &&
            ((baseLevelInternalFormat.colorEncoding == GL_SRGB &&
              features.decodeEncodeSRGBForGenerateMipmap.enabled) ||
             (features.useIntermediateTextureForGenerateMipmap.enabled &&
              nativegl::SupportsNativeRendering(functions, mState.getType(),
                                                baseLevelInfo.nativeInternalFormat))))
        {
            const nativegl::TexImageFormat texImageFormat = nativegl::GetTexImageFormat(
                functions, features, baseLevelInternalFormat.internalFormat,
                baseLevelInternalFormat.format, baseLevelInternalFormat.type);

            BlitGL *blitter = GetBlitGL(context);
            if (baseLevelInternalFormat.colorEncoding == GL_SRGB)
            {
                ANGLE_TRY(blitter->generateSRGBMipmap(context, this, effectiveBaseLevel, levelCount,
                                                      baseLevelDesc.size));
            }
            else
            {
                ANGLE_TRY(blitter->generateMipmap(context, this, effectiveBaseLevel, levelCount,
                                                  baseLevelDesc.size, texImageFormat));
            }
        }
        else
        {
            ANGLE_GL_TRY_ALWAYS_CHECK(context, functions->generateMipmap(ToGLenum(getType())));
        }

        setLevelInfo(context, getType(), effectiveBaseLevel, maxLevel - effectiveBaseLevel,
                     baseLevelInfo);
        onStateChange(angle::SubjectMessage::ObjectReallocated);

        contextGL->markWorkSubmitted();
        return angle::Result::Continue;
    }

    stateManager->bindTexture(getType(), mTextureID);

    bool recreateMipmapLevelsBeforeGenerate =
        features.recreateMipmapLevelsBeforeGenerate.enabled && !mState.getImmutableFormat();
    if (recreateMipmapLevelsBeforeGenerate)
    {
        ANGLE_TRY(allocateMipmapLevelsForGeneration(context));
    }

    if (getType() == gl::TextureType::_2D &&
        ((baseLevelInternalFormat.colorEncoding == GL_SRGB &&
          features.decodeEncodeSRGBForGenerateMipmap.enabled) ||
         (features.useIntermediateTextureForGenerateMipmap.enabled &&
          nativegl::SupportsNativeRendering(functions, mState.getType(),
                                            baseLevelInfo.nativeInternalFormat))))
    {
        // Manually allocate the mip levels of this texture if they don't exist
        // This might already be done above if recreateMipmapLevelsBeforeGenerate is in effect.
        if (!recreateMipmapLevelsBeforeGenerate)
        {
            ANGLE_TRY(allocateMipmapLevelsForGeneration(context));
        }

        // Use the blitter to generate the mips
        const nativegl::TexImageFormat texImageFormat = nativegl::GetTexImageFormat(
            functions, features, baseLevelInternalFormat.internalFormat,
            baseLevelInternalFormat.format, baseLevelInternalFormat.type);
        const GLuint levelCount = maxLevel - effectiveBaseLevel + 1;

        BlitGL *blitter = GetBlitGL(context);
        if (baseLevelInternalFormat.colorEncoding == GL_SRGB)
        {
            ANGLE_TRY(blitter->generateSRGBMipmap(context, this, effectiveBaseLevel, levelCount,
                                                  baseLevelDesc.size));
        }
        else
        {
            ANGLE_TRY(blitter->generateMipmap(context, this, effectiveBaseLevel, levelCount,
                                              baseLevelDesc.size, texImageFormat));
        }
    }
    else
    {
        ANGLE_GL_TRY_ALWAYS_CHECK(context, functions->generateMipmap(ToGLenum(getType())));
    }

    setLevelInfo(context, getType(), effectiveBaseLevel, maxLevel - effectiveBaseLevel,
                 getBaseLevelInfo());

    contextGL->markWorkSubmitted();
    return angle::Result::Continue;
}

angle::Result TextureGL::allocateMipmapLevelsForGeneration(const gl::Context *context)
{
    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    const GLuint effectiveBaseLevel = mState.getEffectiveBaseLevel();
    const GLuint maxLevel           = mState.getMipmapMaxLevel();

    const gl::ImageDesc &baseLevelDesc                = mState.getBaseLevelDesc();
    const gl::InternalFormat &baseLevelInternalFormat = *baseLevelDesc.format.info;

    nativegl::TexImageFormat texImageFormat =
        nativegl::GetTexImageFormat(functions, features, baseLevelInternalFormat.internalFormat,
                                    baseLevelInternalFormat.format, baseLevelInternalFormat.type);

    const bool is3D                = getType() == gl::TextureType::_3D;
    const gl::TextureTarget target = getType() == gl::TextureType::CubeMap
                                         ? gl::TextureTarget::CubeMapPositiveX
                                         : NonCubeTextureTypeToTarget(getType());

    // Manually allocate the mip levels of this texture if they don't exist
    GLuint levelCount = maxLevel - effectiveBaseLevel + 1;
    for (GLuint levelIdx = 1; levelIdx < levelCount; levelIdx++)
    {
        gl::Extents levelSize(
            std::max(baseLevelDesc.size.width >> levelIdx, 1),
            std::max(baseLevelDesc.size.height >> levelIdx, 1),
            is3D ? std::max(baseLevelDesc.size.depth >> levelIdx, 1) : baseLevelDesc.size.depth);

        const gl::ImageDesc &levelDesc = mState.getImageDesc(target, effectiveBaseLevel + levelIdx);

        if (levelDesc.size != levelSize || *levelDesc.format.info != baseLevelInternalFormat)
        {
            // Make sure no pixel unpack buffer is bound
            stateManager->bindBuffer(gl::BufferBinding::PixelUnpack, 0,
                                     context->isHardenedContext());

            switch (getType())
            {
                case gl::TextureType::_2D:
                    ANGLE_GL_TRY_ALWAYS_CHECK(
                        context,
                        functions->texImage2D(ToGLenum(getType()), effectiveBaseLevel + levelIdx,
                                              texImageFormat.internalFormat, levelSize.width,
                                              levelSize.height, 0, texImageFormat.format,
                                              texImageFormat.type, nullptr));
                    break;
                case gl::TextureType::_3D:
                case gl::TextureType::_2DArray:
                case gl::TextureType::CubeMapArray:
                    ANGLE_GL_TRY_ALWAYS_CHECK(
                        context,
                        functions->texImage3D(ToGLenum(getType()), effectiveBaseLevel + levelIdx,
                                              texImageFormat.internalFormat, levelSize.width,
                                              levelSize.height, levelSize.depth, 0,
                                              texImageFormat.format, texImageFormat.type, nullptr));
                    break;
                case gl::TextureType::CubeMap:
                    for (gl::TextureTarget face : gl::AllCubeFaceTextureTargets())
                    {
                        ANGLE_GL_TRY_ALWAYS_CHECK(
                            context,
                            functions->texImage2D(ToGLenum(face), effectiveBaseLevel + levelIdx,
                                                  texImageFormat.internalFormat, levelSize.width,
                                                  levelSize.height, 0, texImageFormat.format,
                                                  texImageFormat.type, nullptr));
                    }
                    break;
                default:
                    // Cannot call glGenerateMipmap with any other texture type
                    UNREACHABLE();
                    break;
            }
        }
    }
    return angle::Result::Continue;
}

angle::Result TextureGL::clearImage(const gl::Context *context,
                                    gl::LevelIndex level,
                                    GLenum format,
                                    GLenum type,
                                    const uint8_t *data)
{
    ContextGL *contextGL              = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions      = GetFunctionsGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    nativegl::TexSubImageFormat texSubImageFormat =
        nativegl::GetTexSubImageFormat(functions, features, format, type);

    // Some drivers may use color mask state when clearing textures.
    contextGL->getStateManager()->setColorMask(true, true, true, true);

    if (mVirtualMipChain.has_value() && getType() == gl::TextureType::CubeMap)
    {
        for (gl::TextureTarget face : gl::AllCubeFaceTextureTargets())
        {
            NativeLevelTarget nlt = getNativeLevelTarget(face, level.get());
            if (nlt.textureID != mTextureID)
            {
                ANGLE_GL_TRY(context, functions->clearTexImage(nlt.textureID, nlt.level,
                                                               texSubImageFormat.format,
                                                               texSubImageFormat.type, data));
            }
            else
            {
                ANGLE_GL_TRY(context, functions->clearTexImage(mTextureID, nlt.level,
                                                               texSubImageFormat.format,
                                                               texSubImageFormat.type, data));
                break;
            }
        }
    }
    else
    {
        gl::TextureTarget target = (getType() == gl::TextureType::CubeMap)
                                       ? gl::TextureTarget::CubeMapPositiveX
                                       : NonCubeTextureTypeToTarget(getType());
        NativeLevelTarget nlt    = getNativeLevelTarget(target, level.get());
        ANGLE_GL_TRY(context,
                     functions->clearTexImage(nlt.textureID, nlt.level, texSubImageFormat.format,
                                              texSubImageFormat.type, data));
    }

    contextGL->markWorkSubmitted();
    return angle::Result::Continue;
}

angle::Result TextureGL::clearSubImage(const gl::Context *context,
                                       gl::LevelIndex level,
                                       const gl::Box &area,
                                       GLenum format,
                                       GLenum type,
                                       const uint8_t *data)
{
    ContextGL *contextGL              = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions      = GetFunctionsGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    // Some drivers may use color mask state when clearing textures.
    contextGL->getStateManager()->setColorMask(true, true, true, true);

    nativegl::TexSubImageFormat texSubImageFormat =
        nativegl::GetTexSubImageFormat(functions, features, format, type);
    if (mVirtualMipChain.has_value() && getType() == gl::TextureType::CubeMap)
    {
        for (int z = 0; z < area.depth; ++z)
        {
            gl::TextureTarget face = gl::CubeFaceIndexToTextureTarget(area.z + z);
            NativeLevelTarget nlt  = getNativeLevelTarget(face, level.get());
            if (nlt.textureID != mTextureID)
            {
                ANGLE_GL_TRY(context, functions->clearTexSubImage(
                                          nlt.textureID, nlt.level, area.x, area.y, 0, area.width,
                                          area.height, 1, texSubImageFormat.format,
                                          texSubImageFormat.type, data));
            }
            else
            {
                ANGLE_GL_TRY(context, functions->clearTexSubImage(
                                          mTextureID, nlt.level, area.x, area.y, area.z, area.width,
                                          area.height, area.depth, texSubImageFormat.format,
                                          texSubImageFormat.type, data));
                break;
            }
        }
    }
    else
    {
        gl::TextureTarget target = (getType() == gl::TextureType::CubeMap)
                                       ? gl::TextureTarget::CubeMapPositiveX
                                       : NonCubeTextureTypeToTarget(getType());
        NativeLevelTarget nlt    = getNativeLevelTarget(target, level.get());
        ANGLE_GL_TRY(context, functions->clearTexSubImage(nlt.textureID, nlt.level, area.x, area.y,
                                                          area.z, area.width, area.height,
                                                          area.depth, texSubImageFormat.format,
                                                          texSubImageFormat.type, data));
    }

    contextGL->markWorkSubmitted();
    return angle::Result::Continue;
}

angle::Result TextureGL::bindTexImage(const gl::Context *context, egl::Surface *surface)
{
    ASSERT(getType() == gl::TextureType::_2D || getType() == gl::TextureType::Rectangle);

    const angle::FeaturesGL &features = GetFeaturesGL(context);

    if (mVirtualMipChain.has_value())
    {
        ANGLE_TRY(recreateTexture(context));
    }

    StateManagerGL *stateManager = GetStateManagerGL(context);

    // Make sure this texture is bound
    stateManager->bindTexture(getType(), mTextureID);

    SurfaceGL *surfaceGL = GetImplAs<SurfaceGL>(surface);

    const gl::Format &surfaceFormat = surface->getBindTexImageFormat();
    setLevelInfo(context, getType(), 0, 1,
                 LevelInfoGL(surfaceFormat.info->format, surfaceFormat.info->internalFormat, false,
                             LUMAWorkaroundGL(), surfaceGL->hasEmulatedAlphaChannel()));

    if (features.virtualizeBaseLevelAndMipchain.enabled && isVirtualMipchainType())
    {
        VirtualMipChain &mipChain = mVirtualMipChain.emplace(mLevelInfo.size());
        mipChain.baseLevel        = 0;
        mipChain.levels           = 1;
        mipChain.baseSize         = surface->getSize();
        mipChain.internalFormat   = surfaceFormat.info->sizedInternalFormat;
        size_t idx                = GetLevelInfoIndex(NonCubeTextureTypeToTarget(getType()), 0);
        mipChain.virtualLevels[idx].texID          = 0;
        mipChain.virtualLevels[idx].size           = mipChain.baseSize;
        mipChain.virtualLevels[idx].internalFormat = mipChain.internalFormat;
    }

    return angle::Result::Continue;
}

angle::Result TextureGL::releaseTexImage(const gl::Context *context)
{
    ANGLE_TRY(recreateTexture(context));
    return angle::Result::Continue;
}

angle::Result TextureGL::setEGLImageTarget(const gl::Context *context,
                                           gl::TextureType type,
                                           egl::Image *image)
{
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    if (mVirtualMipChain.has_value())
    {
        ANGLE_TRY(recreateTexture(context));
    }

    ImageGL *imageGL = GetImplAs<ImageGL>(image);

    GLenum imageNativeInternalFormat = GL_NONE;
    ANGLE_TRY(imageGL->setTexture2D(context, type, this, &imageNativeInternalFormat));

    const gl::InternalFormat &originalInternalFormatInfo = *image->getFormat().info;

    setLevelInfo(context, type, 0, 1,
                 GetLevelInfo(features, originalInternalFormatInfo, imageNativeInternalFormat));

    if (features.virtualizeBaseLevelAndMipchain.enabled && isVirtualMipchainType())
    {
        ASSERT(imageNativeInternalFormat != GL_NONE);
        VirtualMipChain &mipChain         = mVirtualMipChain.emplace(mLevelInfo.size());
        mipChain.isEGLImageTarget         = true;
        mipChain.baseLevel                = 0;
        mipChain.levels                   = 1;
        mipChain.baseSize                 = image->getExtents();
        mipChain.internalFormat           = imageNativeInternalFormat;
        size_t idx                        = GetLevelInfoIndex(NonCubeTextureTypeToTarget(type), 0);
        mipChain.virtualLevels[idx].texID = 0;
        mipChain.virtualLevels[idx].size  = mipChain.baseSize;
        mipChain.virtualLevels[idx].internalFormat = imageNativeInternalFormat;
    }

    return angle::Result::Continue;
}

angle::Result TextureGL::syncState(const gl::Context *context,
                                   const gl::Texture::DirtyBits &dirtyBits,
                                   gl::Command source)
{
    if (dirtyBits.none() && mLocalDirtyBits.none())
    {
        return angle::Result::Continue;
    }

    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    gl::Texture::DirtyBits syncDirtyBits = dirtyBits | mLocalDirtyBits;
    if (dirtyBits[gl::Texture::DIRTY_BIT_BASE_LEVEL] || dirtyBits[gl::Texture::DIRTY_BIT_MAX_LEVEL])
    {
        // Don't know if the previous base level was using any workarounds, always re-sync the
        // workaround dirty bits
        syncDirtyBits |= GetLevelWorkaroundDirtyBits();

        // If the base level format has changed, depth stencil texture mode may need to be updated
        if (!mState.getImmutableFormat() && (context->getClientVersion() >= gl::ES_3_1 ||
                                             context->getExtensions().stencilTexturingANGLE))
        {
            syncDirtyBits.set(gl::Texture::DIRTY_BIT_DEPTH_STENCIL_TEXTURE_MODE);
        }

        // If the base level format has changed, border color may need to be updated
        if (!mState.getImmutableFormat() && (context->getClientVersion() >= gl::ES_3_2 ||
                                             context->getExtensions().textureBorderClampAny()))
        {
            syncDirtyBits.set(gl::Texture::DIRTY_BIT_BORDER_COLOR);
        }
    }

    if (mVirtualMipChain.has_value())
    {
        const GLuint maxLevelLimit    = GetMaxMipmapLevel(context->getCaps(), getType());
        const GLuint clampedBaseLevel = std::min(mState.getEffectiveBaseLevel(), maxLevelLimit);
        const bool baseLevelChanged   = (mAppliedBaseLevel != clampedBaseLevel);
        mAppliedBaseLevel             = clampedBaseLevel;

        if (baseLevelChanged || syncDirtyBits[gl::Texture::DIRTY_BIT_BASE_LEVEL] ||
            syncDirtyBits[gl::Texture::DIRTY_BIT_IMPLEMENTATION])
        {
            ANGLE_TRY(syncVirtualMipchain(context));
            syncDirtyBits |= mLocalDirtyBits;
            syncDirtyBits.set(gl::Texture::DIRTY_BIT_MAX_LEVEL);
        }
    }

    if (syncDirtyBits[gl::Texture::DIRTY_BIT_COMPARE_MODE] &&
        getBaseLevelInfo().depthStencilWorkaround)
    {
        syncDirtyBits.set(gl::Texture::DIRTY_BIT_MIN_FILTER);
        syncDirtyBits.set(gl::Texture::DIRTY_BIT_MAG_FILTER);
    }

    const bool forceNearestFilterForUnsizedDepth =
        functions->standard == STANDARD_GL_ES && getBaseLevelInfo().depthStencilWorkaround &&
        mState.getSamplerState().getCompareMode() == GL_NONE &&
        (context->getClientVersion() < gl::ES_3_0 ||
         !mState.getBaseLevelDesc().format.info->sized) &&
        gl::GetSizedInternalFormatInfo(getBaseLevelInfo().nativeInternalFormat).sized;

    stateManager->bindTexture(getType(), mTextureID);

    for (auto dirtyBit : syncDirtyBits)
    {

        switch (dirtyBit)
        {
            case gl::Texture::DIRTY_BIT_MIN_FILTER:
            {
                GLenum minFilter = mState.getSamplerState().getMinFilter();
                if (forceNearestFilterForUnsizedDepth)
                {
                    minFilter = gl::ConvertToNearestFilterMode(minFilter);
                }
                if (mAppliedSampler.setMinFilter(minFilter))
                {
                    ANGLE_GL_TRY(context,
                                 functions->texParameteri(
                                     nativegl::GetTextureBindingTarget(getType()),
                                     GL_TEXTURE_MIN_FILTER, mAppliedSampler.getMinFilter()));
                }
                break;
            }
            case gl::Texture::DIRTY_BIT_MAG_FILTER:
            {
                GLenum magFilter = mState.getSamplerState().getMagFilter();
                if (forceNearestFilterForUnsizedDepth)
                {
                    magFilter = gl::ConvertToNearestFilterMode(magFilter);
                }
                if (mAppliedSampler.setMagFilter(magFilter))
                {
                    ANGLE_GL_TRY(context,
                                 functions->texParameteri(
                                     nativegl::GetTextureBindingTarget(getType()),
                                     GL_TEXTURE_MAG_FILTER, mAppliedSampler.getMagFilter()));
                }
                break;
            }
            case gl::Texture::DIRTY_BIT_WRAP_S:
                if (mAppliedSampler.setWrapS(mState.getSamplerState().getWrapS()))
                {
                    ANGLE_GL_TRY(context, functions->texParameteri(
                                              nativegl::GetTextureBindingTarget(getType()),
                                              GL_TEXTURE_WRAP_S, mAppliedSampler.getWrapS()));
                }
                break;
            case gl::Texture::DIRTY_BIT_WRAP_T:
                if (mAppliedSampler.setWrapT(mState.getSamplerState().getWrapT()))
                {
                    ANGLE_GL_TRY(context, functions->texParameteri(
                                              nativegl::GetTextureBindingTarget(getType()),
                                              GL_TEXTURE_WRAP_T, mAppliedSampler.getWrapT()));
                }
                break;
            case gl::Texture::DIRTY_BIT_WRAP_R:
                if (mAppliedSampler.setWrapR(mState.getSamplerState().getWrapR()))
                {
                    ANGLE_GL_TRY(context, functions->texParameteri(
                                              nativegl::GetTextureBindingTarget(getType()),
                                              GL_TEXTURE_WRAP_R, mAppliedSampler.getWrapR()));
                }
                break;
            case gl::Texture::DIRTY_BIT_MAX_ANISOTROPY:
                if (mAppliedSampler.setMaxAnisotropy(mState.getSamplerState().getMaxAnisotropy()))
                {
                    ANGLE_GL_TRY(context, functions->texParameterf(
                                              nativegl::GetTextureBindingTarget(getType()),
                                              GL_TEXTURE_MAX_ANISOTROPY_EXT,
                                              mAppliedSampler.getMaxAnisotropy()));
                }
                break;
            case gl::Texture::DIRTY_BIT_MIN_LOD:
                if (mAppliedSampler.setMinLod(mState.getSamplerState().getMinLod()))
                {
                    ANGLE_GL_TRY(context, functions->texParameterf(
                                              nativegl::GetTextureBindingTarget(getType()),
                                              GL_TEXTURE_MIN_LOD, mAppliedSampler.getMinLod()));
                }
                break;
            case gl::Texture::DIRTY_BIT_MAX_LOD:
                if (mAppliedSampler.setMaxLod(mState.getSamplerState().getMaxLod()))
                {
                    ANGLE_GL_TRY(context, functions->texParameterf(
                                              nativegl::GetTextureBindingTarget(getType()),
                                              GL_TEXTURE_MAX_LOD, mAppliedSampler.getMaxLod()));
                }
                break;
            case gl::Texture::DIRTY_BIT_COMPARE_MODE:
                if (mAppliedSampler.setCompareMode(mState.getSamplerState().getCompareMode()))
                {
                    ANGLE_GL_TRY(context,
                                 functions->texParameteri(
                                     nativegl::GetTextureBindingTarget(getType()),
                                     GL_TEXTURE_COMPARE_MODE, mAppliedSampler.getCompareMode()));
                }
                break;
            case gl::Texture::DIRTY_BIT_COMPARE_FUNC:
                if (mAppliedSampler.setCompareFunc(mState.getSamplerState().getCompareFunc()))
                {
                    ANGLE_GL_TRY(context,
                                 functions->texParameteri(
                                     nativegl::GetTextureBindingTarget(getType()),
                                     GL_TEXTURE_COMPARE_FUNC, mAppliedSampler.getCompareFunc()));
                }
                break;
            case gl::Texture::DIRTY_BIT_SRGB_DECODE:
                if (mAppliedSampler.setSRGBDecode(mState.getSamplerState().getSRGBDecode()))
                {
                    ANGLE_GL_TRY(context,
                                 functions->texParameteri(
                                     nativegl::GetTextureBindingTarget(getType()),
                                     GL_TEXTURE_SRGB_DECODE_EXT, mAppliedSampler.getSRGBDecode()));
                }
                break;
            case gl::Texture::DIRTY_BIT_BORDER_COLOR:
            {
                const LevelInfoGL &levelInfo    = getBaseLevelInfo();
                angle::ColorGeneric borderColor = mState.getSamplerState().getBorderColor();
                // Formats that have workarounds must be present in FormatHasBorderColorWorkarounds.
                if (levelInfo.sourceFormat == GL_ALPHA)
                {
                    if (levelInfo.lumaWorkaround.enabled)
                    {
                        ASSERT(levelInfo.lumaWorkaround.workaroundFormat == GL_RED);
                        borderColor.colorF.red = borderColor.colorF.alpha;
                    }
                    else
                    {
                        // Some ES drivers treat ALPHA as swizzled RG, triplicating
                        // border's red to RGB and sampling border's green as alpha.
                        borderColor.colorF.red   = 0.0f;
                        borderColor.colorF.green = borderColor.colorF.alpha;
                    }
                }
                else if (levelInfo.sourceFormat == GL_LUMINANCE_ALPHA)
                {
                    if (levelInfo.lumaWorkaround.enabled)
                    {
                        ASSERT(levelInfo.lumaWorkaround.workaroundFormat == GL_RG);
                    }
                    // When using desktop GL, this format is emulated as swizzled RG.
                    // Some ES drivers do the same without adjusting the border color.
                    borderColor.colorF.green = borderColor.colorF.alpha;
                }

                if (mAppliedSampler.setBorderColor(borderColor))
                {
                    switch (borderColor.type)
                    {
                        case angle::ColorGeneric::Type::Float:
                            ANGLE_GL_TRY(context,
                                         functions->texParameterfv(
                                             nativegl::GetTextureBindingTarget(getType()),
                                             GL_TEXTURE_BORDER_COLOR, &borderColor.colorF.red));
                            break;
                        case angle::ColorGeneric::Type::Int:
                            ANGLE_GL_TRY(context,
                                         functions->texParameterIiv(
                                             nativegl::GetTextureBindingTarget(getType()),
                                             GL_TEXTURE_BORDER_COLOR, &borderColor.colorI.red));
                            break;
                        case angle::ColorGeneric::Type::UInt:
                            ANGLE_GL_TRY(context,
                                         functions->texParameterIuiv(
                                             nativegl::GetTextureBindingTarget(getType()),
                                             GL_TEXTURE_BORDER_COLOR, &borderColor.colorUI.red));
                            break;
                        default:
                            UNREACHABLE();
                            break;
                    }
                }
                break;
            }

            // Texture state
            case gl::Texture::DIRTY_BIT_SWIZZLE_RED:
                ANGLE_TRY(syncTextureStateSwizzle(context, functions, GL_TEXTURE_SWIZZLE_R,
                                                  mState.getSwizzleState().swizzleRed,
                                                  &mAppliedSwizzle.swizzleRed));
                break;
            case gl::Texture::DIRTY_BIT_SWIZZLE_GREEN:
                ANGLE_TRY(syncTextureStateSwizzle(context, functions, GL_TEXTURE_SWIZZLE_G,
                                                  mState.getSwizzleState().swizzleGreen,
                                                  &mAppliedSwizzle.swizzleGreen));
                break;
            case gl::Texture::DIRTY_BIT_SWIZZLE_BLUE:
                ANGLE_TRY(syncTextureStateSwizzle(context, functions, GL_TEXTURE_SWIZZLE_B,
                                                  mState.getSwizzleState().swizzleBlue,
                                                  &mAppliedSwizzle.swizzleBlue));
                break;
            case gl::Texture::DIRTY_BIT_SWIZZLE_ALPHA:
                ANGLE_TRY(syncTextureStateSwizzle(context, functions, GL_TEXTURE_SWIZZLE_A,
                                                  mState.getSwizzleState().swizzleAlpha,
                                                  &mAppliedSwizzle.swizzleAlpha));
                break;
            case gl::Texture::DIRTY_BIT_BASE_LEVEL:
            {
                if (features.virtualizeBaseLevelAndMipchain.enabled && isVirtualMipchainType())
                {
                    break;
                }
                const GLuint maxLevelLimit = GetMaxMipmapLevel(context->getCaps(), getType());
                const GLuint clampedBaseLevel =
                    std::min(mState.getEffectiveBaseLevel(), maxLevelLimit);
                if (mAppliedBaseLevel != clampedBaseLevel)
                {
                    mAppliedBaseLevel = clampedBaseLevel;
                    ANGLE_GL_TRY(context, functions->texParameteri(
                                              nativegl::GetTextureBindingTarget(getType()),
                                              GL_TEXTURE_BASE_LEVEL, mAppliedBaseLevel));
                }
                break;
            }
            case gl::Texture::DIRTY_BIT_MAX_LEVEL:
            {
                const GLuint maxLevelLimit = GetMaxMipmapLevel(context->getCaps(), getType());
                const GLuint clampedMaxLevel =
                    std::min(mState.getEffectiveMaxLevel(), maxLevelLimit);
                const GLuint targetMaxLevel =
                    mVirtualMipChain.has_value()
                        ? ((mState.getEffectiveMaxLevel() == gl::kInitialMaxLevel)
                               ? gl::kInitialMaxLevel
                               : ((clampedMaxLevel >= mVirtualMipChain->baseLevel)
                                      ? static_cast<GLuint>(clampedMaxLevel -
                                                            mVirtualMipChain->baseLevel)
                                      : 0))
                        : clampedMaxLevel;
                if (mAppliedMaxLevel != targetMaxLevel)
                {
                    mAppliedMaxLevel = targetMaxLevel;
                    ANGLE_GL_TRY(context, functions->texParameteri(
                                              nativegl::GetTextureBindingTarget(getType()),
                                              GL_TEXTURE_MAX_LEVEL, mAppliedMaxLevel));
                }
                break;
            }
            case gl::Texture::DIRTY_BIT_DEPTH_STENCIL_TEXTURE_MODE:
            {
                ASSERT(context->getClientVersion() >= gl::ES_3_1 ||
                       context->getExtensions().stencilTexturingANGLE);

                // The DEPTH_STENCIL_TEXTURE_MODE state must affect only
                // DEPTH_STENCIL textures (OpenGL ES 3.2, Section 8.16).
                // Some drivers do not follow this rule and exhibit various side effects
                // when this mode is set to STENCIL_INDEX for textures of other formats.
                const GLenum mode = (getBaseLevelInfo().sourceFormat != GL_DEPTH_STENCIL)
                                        ? GL_DEPTH_COMPONENT
                                        : mState.getDepthStencilTextureMode();

                if (mAppliedDepthStencilTextureMode != mode)
                {
                    mAppliedDepthStencilTextureMode = mode;
                    ANGLE_GL_TRY(context, functions->texParameteri(
                                              nativegl::GetTextureBindingTarget(getType()),
                                              GL_DEPTH_STENCIL_TEXTURE_MODE, mode));
                }
                break;
            }
            case gl::Texture::DIRTY_BIT_USAGE:
            case gl::Texture::DIRTY_BIT_RENDERABILITY_VALIDATION_ANGLE:
                break;

            case gl::Texture::DIRTY_BIT_IMPLEMENTATION:
                // This special dirty bit is used to signal the front-end that the implementation
                // has local dirty bits. The real dirty bits are in mLocalDirty bits.
                break;
            case gl::Texture::DIRTY_BIT_BOUND_AS_IMAGE:
            case gl::Texture::DIRTY_BIT_BOUND_AS_ATTACHMENT:
                // Only used for Vulkan.
                break;

            default:
                UNREACHABLE();
        }
    }

    mAllModifiedDirtyBits |= syncDirtyBits;
    mLocalDirtyBits.reset();
    return angle::Result::Continue;
}

bool TextureGL::hasAnyDirtyBit() const
{
    return mLocalDirtyBits.any();
}

angle::Result TextureGL::setBaseLevel(const gl::Context *context, GLuint baseLevel)
{
    const GLuint maxLevelLimit        = GetMaxMipmapLevel(context->getCaps(), getType());
    const GLuint clampedBaseLevel     = std::min(baseLevel, maxLevelLimit);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    if (features.virtualizeBaseLevelAndMipchain.enabled && isVirtualMipchainType())
    {
        if (clampedBaseLevel != mAppliedBaseLevel)
        {
            mAppliedBaseLevel = clampedBaseLevel;
            mLocalDirtyBits.set(gl::Texture::DIRTY_BIT_BASE_LEVEL);

            // Signal to the GL layer that the Impl has dirty bits.
            onStateChange(angle::SubjectMessage::DirtyBitsFlagged);
        }
        if (mVirtualMipChain.has_value())
        {
            ANGLE_TRY(syncVirtualMipchain(context));
        }
        return angle::Result::Continue;
    }

    if (clampedBaseLevel != mAppliedBaseLevel)
    {
        const FunctionsGL *functions = GetFunctionsGL(context);
        StateManagerGL *stateManager = GetStateManagerGL(context);

        mAppliedBaseLevel = clampedBaseLevel;
        mLocalDirtyBits.set(gl::Texture::DIRTY_BIT_BASE_LEVEL);

        // Signal to the GL layer that the Impl has dirty bits.
        onStateChange(angle::SubjectMessage::DirtyBitsFlagged);

        stateManager->bindTexture(getType(), mTextureID);
        ANGLE_GL_TRY(context, functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_BASE_LEVEL,
                                                       clampedBaseLevel));
    }
    return angle::Result::Continue;
}

angle::Result TextureGL::setMaxLevel(const gl::Context *context, GLuint maxLevel)
{
    const GLuint maxLevelLimit   = GetMaxMipmapLevel(context->getCaps(), getType());
    const GLuint clampedMaxLevel = std::min(maxLevel, maxLevelLimit);
    const GLuint targetMaxLevel =
        mVirtualMipChain.has_value()
            ? ((clampedMaxLevel >= mVirtualMipChain->baseLevel)
                   ? static_cast<GLuint>(clampedMaxLevel - mVirtualMipChain->baseLevel)
                   : 0)
            : clampedMaxLevel;
    if (targetMaxLevel != mAppliedMaxLevel)
    {
        const FunctionsGL *functions = GetFunctionsGL(context);
        StateManagerGL *stateManager = GetStateManagerGL(context);

        mAppliedMaxLevel = targetMaxLevel;
        mLocalDirtyBits.set(gl::Texture::DIRTY_BIT_MAX_LEVEL);

        // Signal to the GL layer that the Impl has dirty bits.
        onStateChange(angle::SubjectMessage::DirtyBitsFlagged);

        stateManager->bindTexture(getType(), mTextureID);
        ANGLE_GL_TRY(context, functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_MAX_LEVEL,
                                                       targetMaxLevel));
    }
    return angle::Result::Continue;
}

angle::Result TextureGL::setMinFilter(const gl::Context *context, GLenum filter)
{
    if (mAppliedSampler.setMinFilter(filter))
    {
        const FunctionsGL *functions = GetFunctionsGL(context);
        StateManagerGL *stateManager = GetStateManagerGL(context);

        mLocalDirtyBits.set(gl::Texture::DIRTY_BIT_MIN_FILTER);

        // Signal to the GL layer that the Impl has dirty bits.
        onStateChange(angle::SubjectMessage::DirtyBitsFlagged);

        stateManager->bindTexture(getType(), mTextureID);
        ANGLE_GL_TRY(context,
                     functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_MIN_FILTER, filter));
    }
    return angle::Result::Continue;
}
angle::Result TextureGL::setMagFilter(const gl::Context *context, GLenum filter)
{
    if (mAppliedSampler.setMagFilter(filter))
    {
        const FunctionsGL *functions = GetFunctionsGL(context);
        StateManagerGL *stateManager = GetStateManagerGL(context);

        mLocalDirtyBits.set(gl::Texture::DIRTY_BIT_MAG_FILTER);

        // Signal to the GL layer that the Impl has dirty bits.
        onStateChange(angle::SubjectMessage::DirtyBitsFlagged);

        stateManager->bindTexture(getType(), mTextureID);
        ANGLE_GL_TRY(context,
                     functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_MAG_FILTER, filter));
    }
    return angle::Result::Continue;
}

angle::Result TextureGL::setSwizzle(const gl::Context *context, GLint swizzle[4])
{
    gl::SwizzleState resultingSwizzle =
        ANGLE_UNSAFE_TODO(gl::SwizzleState(swizzle[0], swizzle[1], swizzle[2], swizzle[3]));

    if (resultingSwizzle != mAppliedSwizzle)
    {
        const FunctionsGL *functions = GetFunctionsGL(context);
        StateManagerGL *stateManager = GetStateManagerGL(context);

        mAppliedSwizzle = resultingSwizzle;
        mLocalDirtyBits.set(gl::Texture::DIRTY_BIT_SWIZZLE_RED);
        mLocalDirtyBits.set(gl::Texture::DIRTY_BIT_SWIZZLE_GREEN);
        mLocalDirtyBits.set(gl::Texture::DIRTY_BIT_SWIZZLE_BLUE);
        mLocalDirtyBits.set(gl::Texture::DIRTY_BIT_SWIZZLE_ALPHA);

        // Signal to the GL layer that the Impl has dirty bits.
        onStateChange(angle::SubjectMessage::DirtyBitsFlagged);

        stateManager->bindTexture(getType(), mTextureID);
        if (functions->standard == STANDARD_GL_ES)
        {
            ANGLE_GL_TRY(context, functions->texParameteri(ToGLenum(getType()),
                                                           GL_TEXTURE_SWIZZLE_R, swizzle[0]));
            ANGLE_UNSAFE_TODO(ANGLE_GL_TRY(
                context,
                functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_SWIZZLE_G, swizzle[1])));
            ANGLE_UNSAFE_TODO(ANGLE_GL_TRY(
                context,
                functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_SWIZZLE_B, swizzle[2])));
            ANGLE_UNSAFE_TODO(ANGLE_GL_TRY(
                context,
                functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_SWIZZLE_A, swizzle[3])));
        }
        else
        {
            ANGLE_GL_TRY(context, functions->texParameteriv(ToGLenum(getType()),
                                                            GL_TEXTURE_SWIZZLE_RGBA, swizzle));
        }
    }
    return angle::Result::Continue;
}

angle::Result TextureGL::setBuffer(const gl::Context *context, GLenum internalFormat)
{
    const FunctionsGL *functions                              = GetFunctionsGL(context);
    const gl::OffsetBindingPointer<gl::Buffer> &bufferBinding = mState.getBuffer();
    const gl::Buffer *buffer                                  = bufferBinding.get();
    const GLintptr offset                                     = bufferBinding.getOffset();
    const GLsizeiptr size                                     = bufferBinding.getSize();
    const GLuint bufferID = buffer ? GetImplAs<BufferGL>(buffer)->getBufferID() : 0;

    // If buffer is not bound, use texBuffer to unbind it.  If size is 0, texBuffer was used to
    // create this binding, so use the same function.  This will allow the implementation to take
    // the current size of the buffer on every draw/dispatch call even if the buffer size changes.
    if (buffer == nullptr || size == 0)
    {
        ANGLE_GL_TRY(context, functions->texBuffer(GL_TEXTURE_BUFFER, internalFormat, bufferID));
    }
    else
    {
        ANGLE_GL_TRY(context,
                     functions->texBufferRange(GL_TEXTURE_BUFFER, internalFormat, bufferID, offset,
                                               GetBoundBufferAvailableSize(bufferBinding)));
    }

    return angle::Result::Continue;
}

GLenum TextureGL::getNativeInternalFormat(const gl::ImageIndex &index) const
{
    return getLevelInfo(index.getTarget(), index.getLevelIndex()).nativeInternalFormat;
}

bool TextureGL::hasEmulatedAlphaChannel(const gl::ImageIndex &index) const
{
    return getLevelInfo(index.getTargetOrFirstCubeFace(), index.getLevelIndex())
        .emulatedAlphaChannel;
}

angle::Result TextureGL::recreateTexture(const gl::Context *context)
{
    const FunctionsGL *functions = GetFunctionsGL(context);
    StateManagerGL *stateManager = GetStateManagerGL(context);

    clearVirtualLevels(context);
    mVirtualMipChain.reset();

    stateManager->bindTexture(getType(), mTextureID);
    stateManager->deleteTexture(mTextureID);

    functions->genTextures(1, &mTextureID);
    stateManager->bindTexture(getType(), mTextureID);

    mLevelInfo.clear();
    mLevelInfo.resize(GetMaxLevelInfoCountForTextureType(getType()));

    mAppliedSwizzle = gl::SwizzleState();
    mAppliedSampler = gl::SamplerState::CreateDefaultForTarget(getType());

    mAppliedBaseLevel = 0;
    mAppliedMaxLevel  = gl::kInitialMaxLevel;

    mLocalDirtyBits = mAllModifiedDirtyBits;

    onStateChange(angle::SubjectMessage::SubjectChanged);
    onStateChange(angle::SubjectMessage::ObjectReallocated);

    return angle::Result::Continue;
}

angle::Result TextureGL::syncTextureStateSwizzle(const gl::Context *context,
                                                 const FunctionsGL *functions,
                                                 GLenum name,
                                                 GLenum value,
                                                 GLenum *currentlyAppliedValue)
{
    const LevelInfoGL &levelInfo = getBaseLevelInfo();
    GLenum resultSwizzle         = value;
    if (levelInfo.lumaWorkaround.enabled)
    {
        switch (value)
        {
            case GL_RED:
            case GL_GREEN:
            case GL_BLUE:
                if (levelInfo.sourceFormat == GL_LUMINANCE ||
                    levelInfo.sourceFormat == GL_LUMINANCE_ALPHA)
                {
                    // Texture is backed by a RED or RG texture, point all color channels at the
                    // red channel.
                    ASSERT(levelInfo.lumaWorkaround.workaroundFormat == GL_RED ||
                           levelInfo.lumaWorkaround.workaroundFormat == GL_RG);
                    resultSwizzle = GL_RED;
                }
                else
                {
                    ASSERT(levelInfo.sourceFormat == GL_ALPHA);
                    // Color channels are not supposed to exist, make them always sample 0.
                    resultSwizzle = GL_ZERO;
                }
                break;

            case GL_ALPHA:
                if (levelInfo.sourceFormat == GL_LUMINANCE)
                {
                    // Alpha channel is not supposed to exist, make it always sample 1.
                    resultSwizzle = GL_ONE;
                }
                else if (levelInfo.sourceFormat == GL_ALPHA)
                {
                    // Texture is backed by a RED texture, point the alpha channel at the red
                    // channel.
                    ASSERT(levelInfo.lumaWorkaround.workaroundFormat == GL_RED);
                    resultSwizzle = GL_RED;
                }
                else
                {
                    ASSERT(levelInfo.sourceFormat == GL_LUMINANCE_ALPHA);
                    // Texture is backed by an RG texture, point the alpha channel at the green
                    // channel.
                    ASSERT(levelInfo.lumaWorkaround.workaroundFormat == GL_RG);
                    resultSwizzle = GL_GREEN;
                }
                break;

            case GL_ZERO:
            case GL_ONE:
                // Don't modify the swizzle state when requesting ZERO or ONE.
                resultSwizzle = value;
                break;

            default:
                UNREACHABLE();
                break;
        }
    }
    else if (levelInfo.depthStencilWorkaround)
    {
        switch (value)
        {
            case GL_RED:
                // Don't modify the swizzle state when requesting the red channel.
                resultSwizzle = value;
                break;

            case GL_GREEN:
            case GL_BLUE:
                if (context->getClientVersion() >= gl::ES_3_0)
                {
                    // In GLES 3.0, depth textures are treated as RED
                    // textures, so green and blue should be 0.
                    resultSwizzle = GL_ZERO;
                }
                else
                {
                    // In OES_depth_texture, depth
                    // textures are treated as luminance.
                    resultSwizzle = GL_RED;
                }
                break;

            case GL_ALPHA:
                // Depth textures should sample 1 from the alpha channel.
                resultSwizzle = GL_ONE;
                break;

            case GL_ZERO:
            case GL_ONE:
                // Don't modify the swizzle state when requesting ZERO or ONE.
                resultSwizzle = value;
                break;

            default:
                UNREACHABLE();
                break;
        }
    }
    else if (levelInfo.emulatedAlphaChannel)
    {
        if (value == GL_ALPHA)
        {
            resultSwizzle = GL_ONE;
        }
    }

    if (*currentlyAppliedValue != resultSwizzle)
    {
        *currentlyAppliedValue = resultSwizzle;
        ANGLE_GL_TRY(context, functions->texParameteri(ToGLenum(getType()), name, resultSwizzle));
    }

    return angle::Result::Continue;
}

void TextureGL::setLevelInfo(const gl::Context *context,
                             gl::TextureTarget target,
                             size_t level,
                             size_t levelCount,
                             const LevelInfoGL &levelInfo)
{
    ASSERT(levelCount > 0);

    bool updateWorkarounds = levelInfo.depthStencilWorkaround || levelInfo.lumaWorkaround.enabled ||
                             levelInfo.emulatedAlphaChannel;

    bool updateDepthStencilTextureMode = false;
    const bool setToDepthStencil       = levelInfo.sourceFormat == GL_DEPTH_STENCIL;

    bool updateBorderColor = false;
    const bool targetFormatHasBorderColorWorkarounds =
        FormatHasBorderColorWorkarounds(levelInfo.sourceFormat);

    for (size_t i = level; i < level + levelCount; i++)
    {
        size_t index = GetLevelInfoIndex(target, i);
        ASSERT(index < mLevelInfo.size());
        auto &curLevelInfo = mLevelInfo[index];

        updateWorkarounds |= curLevelInfo.depthStencilWorkaround;
        updateWorkarounds |= curLevelInfo.lumaWorkaround.enabled;
        updateWorkarounds |= curLevelInfo.emulatedAlphaChannel;

        // When redefining a level to or from DEPTH_STENCIL
        // format, ensure that the texture mode is synced.
        const bool setFromDepthStencil = curLevelInfo.sourceFormat == GL_DEPTH_STENCIL;
        if (setFromDepthStencil != setToDepthStencil)
        {
            updateDepthStencilTextureMode = true;
        }

        // When redefining a level to or from a format that has border color workarounds,
        // ensure that the texture border color is synced.
        if (FormatHasBorderColorWorkarounds(curLevelInfo.sourceFormat) ||
            targetFormatHasBorderColorWorkarounds)
        {
            updateBorderColor = true;
        }

        curLevelInfo = levelInfo;
    }

    // Skip this step when unsupported
    updateDepthStencilTextureMode =
        updateDepthStencilTextureMode && (context->getClientVersion() >= gl::ES_3_1 ||
                                          context->getExtensions().stencilTexturingANGLE);

    // Skip this step when unsupported
    updateBorderColor = updateBorderColor && (context->getClientVersion() >= gl::ES_3_2 ||
                                              context->getExtensions().textureBorderClampAny());

    if (updateWorkarounds || updateDepthStencilTextureMode || updateBorderColor)
    {
        if (updateWorkarounds)
        {
            mLocalDirtyBits |= GetLevelWorkaroundDirtyBits();
        }
        if (updateDepthStencilTextureMode)
        {
            mLocalDirtyBits.set(gl::Texture::DIRTY_BIT_DEPTH_STENCIL_TEXTURE_MODE);
        }
        if (updateBorderColor)
        {
            mLocalDirtyBits.set(gl::Texture::DIRTY_BIT_BORDER_COLOR);
        }
        onStateChange(angle::SubjectMessage::DirtyBitsFlagged);
    }
}

void TextureGL::setLevelInfo(const gl::Context *context,
                             gl::TextureType type,
                             size_t level,
                             size_t levelCount,
                             const LevelInfoGL &levelInfo)
{
    if (type == gl::TextureType::CubeMap)
    {
        for (gl::TextureTarget target : gl::AllCubeFaceTextureTargets())
        {
            setLevelInfo(context, target, level, levelCount, levelInfo);
        }
    }
    else
    {
        setLevelInfo(context, NonCubeTextureTypeToTarget(type), level, levelCount, levelInfo);
    }
}

const LevelInfoGL &TextureGL::getLevelInfo(gl::TextureTarget target, size_t level) const
{
    return mLevelInfo[GetLevelInfoIndex(target, level)];
}

const LevelInfoGL &TextureGL::getBaseLevelInfo() const
{
    GLint effectiveBaseLevel = mState.getEffectiveBaseLevel();
    gl::TextureTarget target = getType() == gl::TextureType::CubeMap
                                   ? gl::kCubeMapTextureTargetMin
                                   : gl::NonCubeTextureTypeToTarget(getType());
    return getLevelInfo(target, effectiveBaseLevel);
}

gl::TextureType TextureGL::getType() const
{
    return mState.getType();
}

angle::Result TextureGL::initializeContentsImpl(const gl::Context *context,
                                                GLenum binding,
                                                const gl::ImageIndex &imageIndex)
{
    ContextGL *contextGL              = GetImplAs<ContextGL>(context);
    const FunctionsGL *functions      = GetFunctionsGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);

    const gl::ImageDesc &desc                    = mState.getImageDesc(imageIndex);
    const gl::InternalFormat &internalFormatInfo = *desc.format.info;

    NativeLevelTarget nlt =
        getNativeLevelTarget(imageIndex.getTarget(), imageIndex.getLevelIndex());

    // Clearing cube maps with EXT_clear_texture has inconsistent results. The drivers often clear
    // the entire level when only one face is specified.
    if (functions->clearTexImage && !internalFormatInfo.compressed &&
        getType() != gl::TextureType::CubeMap &&
        !features.disableClearTexImageForRobustInit.enabled)
    {
        nativegl::TexSubImageFormat nativeSubImageFormat = nativegl::GetTexSubImageFormat(
            functions, features, internalFormatInfo.format, internalFormatInfo.type);

        // Some drivers may use color mask state when clearing textures.
        contextGL->getStateManager()->setColorMask(true, true, true, true);

        // The largest GL data format is 16 bytes (RGBA32F)
        angle::FixedVector<uint8_t, 16> data(16, 0);
        if (internalFormatInfo.depthBits > 0)
        {
            data = GetDepthOnePixel(nativeSubImageFormat.type);
        }
        CHECK(internalFormatInfo.pixelBytes <= data.size());
        if (imageIndex.hasLayer())
        {
            ANGLE_GL_TRY(context,
                         functions->clearTexSubImage(
                             nlt.textureID, nlt.level, 0, 0, imageIndex.getLayerIndex(),
                             desc.size.width, desc.size.height, imageIndex.getLayerCount(),
                             nativeSubImageFormat.format, nativeSubImageFormat.type, data.data()));
        }
        else
        {
            ANGLE_GL_TRY(context, functions->clearTexImage(nlt.textureID, nlt.level,
                                                           nativeSubImageFormat.format,
                                                           nativeSubImageFormat.type, data.data()));
        }

        contextGL->markWorkSubmitted();
        return angle::Result::Continue;
    }

    GLenum nativeInternalFormat =
        getLevelInfo(imageIndex.getTarget(), imageIndex.getLevelIndex()).nativeInternalFormat;
    if ((features.allowClearForRobustResourceInit.enabled ||
         !nativegl::SupportsTexImage(getType())) &&
        nativegl::SupportsNativeRendering(functions, mState.getType(), nativeInternalFormat))
    {
        BlitGL *blitter = GetBlitGL(context);

        int levelDepth = mState.getImageDesc(imageIndex).size.depth;

        bool clearSucceeded = false;
        ANGLE_TRY(blitter->clearRenderableTexture(context, this, nativeInternalFormat, levelDepth,
                                                  imageIndex, &clearSucceeded));
        if (clearSucceeded)
        {
            contextGL->markWorkSubmitted();
            return angle::Result::Continue;
        }
    }

    // Either the texture is not renderable or was incomplete when clearing, fall back to a data
    // upload
    ASSERT(nativegl::SupportsTexImage(getType()));

    gl::PixelUnpackState unpackState;
    unpackState.alignment = 1;
    ANGLE_TRY(stateManager->setPixelUnpackState(context, unpackState));

    GLuint prevUnpackBuffer = stateManager->getBufferID(gl::BufferBinding::PixelUnpack);
    stateManager->bindBuffer(gl::BufferBinding::PixelUnpack, 0, context->isHardenedContext());

    gl::TextureType bindType = gl::TextureTargetToType(nlt.target);
    stateManager->bindTexture(bindType, nlt.textureID);
    if (internalFormatInfo.compressed)
    {
        nativegl::CompressedTexSubImageFormat nativeSubImageFormat =
            nativegl::GetCompressedSubTexImageFormat(functions, features,
                                                     internalFormatInfo.internalFormat);

        GLuint imageSize = 0;
        ANGLE_CHECK_GL_MATH(contextGL,
                            internalFormatInfo.computeCompressedImageSize(desc.size, &imageSize));

        const angle::MemoryBuffer *zero;
        ANGLE_CHECK_GL_ALLOC(contextGL, context->getZeroFilledBuffer(imageSize, &zero));

        // WebGL spec requires that zero data is uploaded to compressed textures even if it might
        // not result in zero color data.
        if (nativegl::UseTexImage2D(bindType))
        {
            ANGLE_GL_TRY(context, functions->compressedTexSubImage2D(
                                      ToGLenum(nlt.target), nlt.level, 0, 0, desc.size.width,
                                      desc.size.height, nativeSubImageFormat.format, imageSize,
                                      zero->data()));
        }
        else
        {
            ASSERT(nativegl::UseTexImage3D(bindType));
            ANGLE_GL_TRY(context, functions->compressedTexSubImage3D(
                                      ToGLenum(nlt.target), nlt.level, 0, 0, 0, desc.size.width,
                                      desc.size.height, desc.size.depth,
                                      nativeSubImageFormat.format, imageSize, zero->data()));
        }
    }
    else
    {
        nativegl::TexSubImageFormat nativeSubImageFormat = nativegl::GetTexSubImageFormat(
            functions, features, internalFormatInfo.format, internalFormatInfo.type);

        GLuint imageSize = 0;
        ANGLE_CHECK_GL_MATH(contextGL, internalFormatInfo.computePackUnpackEndByte(
                                           nativeSubImageFormat.type, desc.size, unpackState,
                                           nativegl::UseTexImage3D(bindType), &imageSize));

        const angle::MemoryBuffer *zero = nullptr;
        GLuint pboId                    = 0;
        bool usePBO                     = false;
        if (internalFormatInfo.depthBits > 0)
        {
            DisplayGL *displayGL = GetImplAs<DisplayGL>(context->getDisplay());
            if ((nativegl::UseTexImage2D(bindType) && features.uploadTextureDataInChunks.enabled) ||
                displayGL->getMaxSupportedESVersion() < gl::Version(3, 0))
            {
                // Fall back to client memory buffer if PBO cannot be used.
                ANGLE_TRY(
                    GetDepthOneFilledBuffer(context, imageSize, nativeSubImageFormat.type, &zero));
            }
            else
            {
                ANGLE_TRY(contextGL->getDepthInitPBO(context, imageSize, nativeSubImageFormat.type,
                                                     &pboId));
                usePBO = true;
            }
        }
        else
        {
            ANGLE_CHECK_GL_ALLOC(contextGL, context->getZeroFilledBuffer(imageSize, &zero));
        }

        angle::Span<const uint8_t> uploadSpan;
        if (usePBO)
        {
            stateManager->bindBuffer(gl::BufferBinding::PixelUnpack, pboId,
                                     context->isHardenedContext());
        }
        else
        {
            uploadSpan = ANGLE_UNSAFE_TODO({zero->data(), imageSize});
        }

        if (nativegl::UseTexImage2D(bindType))
        {
            if (features.uploadTextureDataInChunks.enabled)
            {
                ASSERT(!usePBO);
                gl::Box area(0, 0, 0, desc.size.width, desc.size.height, 1);
                ANGLE_TRY(setSubImageRowByRowWorkaround(
                    context, imageIndex.getTarget(), imageIndex.getLevelIndex(), area,
                    nativeSubImageFormat.format, nativeSubImageFormat.type, unpackState, nullptr,
                    kUploadTextureDataInChunksUploadSize, uploadSpan.data()));
            }
            else
            {
                ANGLE_GL_TRY(context, functions->texSubImage2D(
                                          ToGLenum(nlt.target), nlt.level, 0, 0, desc.size.width,
                                          desc.size.height, nativeSubImageFormat.format,
                                          nativeSubImageFormat.type, uploadSpan.data()));
            }
        }
        else
        {
            ASSERT(nativegl::UseTexImage3D(bindType));
            ANGLE_GL_TRY(context,
                         functions->texSubImage3D(ToGLenum(nlt.target), nlt.level, 0, 0, 0,
                                                  desc.size.width, desc.size.height,
                                                  desc.size.depth, nativeSubImageFormat.format,
                                                  nativeSubImageFormat.type, uploadSpan.data()));
        }
    }

    // Reset the pixel unpack state.  Because this call is made after synchronizing dirty bits in a
    // glTexImage call, we need to make sure that the texture data to be uploaded later has the
    // expected unpack state.
    ANGLE_TRY(stateManager->setPixelUnpackState(context, context->getState().getUnpackState()));
    stateManager->bindBuffer(gl::BufferBinding::PixelUnpack, prevUnpackBuffer,
                             context->isHardenedContext());

    contextGL->markWorkSubmitted();
    return angle::Result::Continue;
}

angle::Result TextureGL::initializeContents(const gl::Context *context,
                                            GLenum binding,
                                            const gl::ImageIndex &imageIndex)
{
    ANGLE_TRY(initializeContentsImpl(context, binding, imageIndex));

    if (hasEmulatedAlphaChannel(imageIndex))
    {
        ContextGL *contextGL         = GetImplAs<ContextGL>(context);
        const FunctionsGL *functions = GetFunctionsGL(context);
        GLenum nativeInternalFormat  = getNativeInternalFormat(imageIndex);

        if (nativegl::SupportsNativeRendering(functions, mState.getType(), nativeInternalFormat))
        {
            BlitGL *blitter = GetBlitGL(context);
            NativeLevelTarget nlt =
                getNativeLevelTarget(imageIndex.getTarget(), imageIndex.getLevelIndex());
            ANGLE_TRY(blitter->clearRenderableTextureAlphaToOne(context, nlt.textureID, nlt.target,
                                                                nlt.level));
            contextGL->markWorkSubmitted();
        }
        else
        {
            // For emulated alpha formats that are not renderable, they are compressed formats.
            // Initializing them with zeroes already produces the correct values (e.g. opaque black
            // for DXT1).
            const gl::ImageDesc &desc = mState.getImageDesc(imageIndex);
            CHECK(desc.format.info->compressed);
        }
    }

    return angle::Result::Continue;
}

GLint TextureGL::getRequiredExternalTextureImageUnits(const gl::Context *context)
{
    const FunctionsGL *functions = GetFunctionsGL(context);
    StateManagerGL *stateManager = GetStateManagerGL(context);

    ASSERT(getType() == gl::TextureType::External);
    stateManager->bindTexture(getType(), mTextureID);

    GLint result = 0;
    functions->getTexParameteriv(ToGLenum(gl::NonCubeTextureTypeToTarget(getType())),
                                 GL_REQUIRED_TEXTURE_IMAGE_UNITS_OES, &result);
    return result;
}

angle::Result TextureGL::recreateTextureOnTexImage3DDepthIncreaseWorkaround(
    const gl::Context *context,
    gl::TextureTarget target,
    size_t level,
    const gl::Extents &size)
{
    const FunctionsGL *functions      = GetFunctionsGL(context);
    const angle::FeaturesGL &features = GetFeaturesGL(context);
    StateManagerGL *stateManager      = GetStateManagerGL(context);

    const gl::ImageDesc &currentDesc = mState.getImageDesc(target, level);
    if (currentDesc.size.empty() || size.depth <= currentDesc.size.depth)
    {
        return angle::Result::Continue;
    }

    GLuint oldTextureID = mTextureID;

    stateManager->bindTexture(getType(), oldTextureID);
    functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    functions->genTextures(1, &mTextureID);
    stateManager->bindTexture(getType(), mTextureID);
    functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    mAppliedSwizzle = gl::SwizzleState();
    mAppliedSampler = gl::SamplerState::CreateDefaultForTarget(getType());
    mAppliedSampler.setMinFilter(GL_NEAREST);
    mAppliedSampler.setMagFilter(GL_NEAREST);
    mAppliedBaseLevel = 0;
    mAppliedMaxLevel  = gl::kInitialMaxLevel;

    for (size_t l = 0; l < mState.getImageDescs().size(); ++l)
    {
        if (l == level)
        {
            continue;
        }

        const gl::ImageDesc &desc = mState.getImageDesc(target, l);
        if (desc.size.empty())
        {
            continue;
        }

        nativegl::TexImageFormat levelTexImageFormat =
            nativegl::GetTexImageFormat(functions, features, desc.format.info->sizedInternalFormat,
                                        desc.format.info->format, desc.format.info->type);
        ANGLE_GL_TRY_ALWAYS_CHECK(
            context,
            functions->texImage3D(ToGLenum(target), static_cast<GLint>(l),
                                  levelTexImageFormat.internalFormat, desc.size.width,
                                  desc.size.height, desc.size.depth, 0, levelTexImageFormat.format,
                                  levelTexImageFormat.type, nullptr));

        stateManager->bindTexture(getType(), oldTextureID);
        functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(l));
        functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_BASE_LEVEL, static_cast<GLint>(l));

        stateManager->bindTexture(getType(), mTextureID);
        functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(l));
        functions->texParameteri(ToGLenum(getType()), GL_TEXTURE_BASE_LEVEL, static_cast<GLint>(l));
        mAppliedMaxLevel  = static_cast<GLuint>(l);
        mAppliedBaseLevel = static_cast<GLuint>(l);

        ANGLE_GL_TRY_ALWAYS_CHECK(
            context,
            functions->copyImageSubData(oldTextureID, ToGLenum(target), static_cast<GLint>(l), 0, 0,
                                        0, mTextureID, ToGLenum(target), static_cast<GLint>(l), 0,
                                        0, 0, desc.size.width, desc.size.height, desc.size.depth));
    }

    stateManager->deleteTexture(oldTextureID);

    mLocalDirtyBits = mAllModifiedDirtyBits;
    onStateChange(angle::SubjectMessage::SubjectChanged);

    return angle::Result::Continue;
}

}  // namespace rx
