//
// Copyright 2014 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// TextureD3D.cpp: Implementations of the Texture interfaces shared betweeen the D3D backends.

#include "libANGLE/renderer/d3d/TextureD3D.h"
#include "common/unsafe_buffers.h"

#include <memory>

#include "common/mathutil.h"
#include "common/utilities.h"
#include "libANGLE/Buffer.h"
#include "libANGLE/Config.h"
#include "libANGLE/Context.h"
#include "libANGLE/Framebuffer.h"
#include "libANGLE/Image.h"
#include "libANGLE/Surface.h"
#include "libANGLE/Texture.h"
#include "libANGLE/formatutils.h"
#include "libANGLE/renderer/BufferImpl.h"
#include "libANGLE/renderer/d3d/BufferD3D.h"
#include "libANGLE/renderer/d3d/ContextD3D.h"
#include "libANGLE/renderer/d3d/EGLImageD3D.h"
#include "libANGLE/renderer/d3d/ImageD3D.h"
#include "libANGLE/renderer/d3d/RenderTargetD3D.h"
#include "libANGLE/renderer/d3d/SurfaceD3D.h"
#include "libANGLE/renderer/d3d/TextureStorage.h"

namespace rx
{

namespace
{

angle::Result GetUnpackPointer(const gl::Context *context,
                               const gl::PixelUnpackState &unpack,
                               gl::Buffer *unpackBuffer,
                               const uint8_t *pixels,
                               ptrdiff_t layerOffset,
                               const uint8_t **pointerOut)
{
    if (unpackBuffer)
    {
        // Do a CPU readback here, if we have an unpack buffer bound and the fast GPU path is not
        // supported
        ptrdiff_t offset = reinterpret_cast<ptrdiff_t>(pixels);

        // TODO: this is the only place outside of renderer that asks for a buffers raw data.
        // This functionality should be moved into renderer and the getData method of BufferImpl
        // removed.
        BufferD3D *bufferD3D = GetImplAs<BufferD3D>(unpackBuffer);
        ASSERT(bufferD3D);
        const uint8_t *bufferData = nullptr;
        ANGLE_TRY(bufferD3D->getData(context, &bufferData));
        *pointerOut = ANGLE_UNSAFE_TODO(bufferData + offset);
    }
    else
    {
        *pointerOut = pixels;
    }

    // Offset the pointer for 2D array layer (if it's valid)
    if (*pointerOut != nullptr)
    {
        ANGLE_UNSAFE_TODO(*pointerOut += layerOffset);
    }

    return angle::Result::Continue;
}

bool IsRenderTargetUsage(GLenum usage)
{
    return (usage == GL_FRAMEBUFFER_ATTACHMENT_ANGLE);
}

angle::Result SnapshotReadFramebufferToImage(const gl::Context *context,
                                             RendererD3D *renderer,
                                             gl::TextureType type,
                                             GLenum sizedInternalFormat,
                                             const gl::Rectangle &sourceArea,
                                             const gl::Framebuffer *sourceFBO,
                                             std::unique_ptr<ImageD3D> *outImage)
{
    ASSERT(outImage != nullptr);

    ContextD3D *contextD3D = GetImplAs<ContextD3D>(context);
    outImage->reset(renderer->createImage());
    ANGLE_CHECK_GL_ALLOC(contextD3D, outImage->get() != nullptr);

    const bool redefineResult = (*outImage)->redefine(
        type, sizedInternalFormat, gl::Extents(sourceArea.width, sourceArea.height, 1), true);
    ANGLE_CHECK_GL_ALLOC(contextD3D, redefineResult);

    ANGLE_TRY(
        (*outImage)->copyFromFramebuffer(context, gl::Offset(0, 0, 0), sourceArea, sourceFBO));
    return angle::Result::Continue;
}

}  // namespace

TextureD3D::TextureD3D(const gl::TextureState &state, RendererD3D *renderer)
    : TextureImpl(state),
      mRenderer(renderer),
      mDirtyImages(true),
      mImmutable(false),
      mEGLImageTarget(false),
      mTexStorage(nullptr),
      mTexStorageObserverBinding(this, kTextureStorageObserverMessageIndex)
{}

TextureD3D::~TextureD3D()
{
    ASSERT(!mTexStorage);
}

angle::Result TextureD3D::handleCopyImageSelfCopyRedefine(
    const gl::Context *context,
    gl::TextureType snapshotType,
    GLenum sizedInternalFormat,
    const gl::Rectangle &sourceArea,
    const gl::Extents &destExtents,
    bool outside,
    const gl::ImageIndex &destIndex,
    gl::Framebuffer *source,
    const std::function<angle::Result(const gl::Extents &)> &redefineDest)
{
    gl::Extents fbSize = source->getReadColorAttachment()->getSize();
    gl::Rectangle clippedArea;
    if (!ClipRectangle(sourceArea, gl::Rectangle(0, 0, fbSize.width, fbSize.height), &clippedArea))
    {
        // We won't be copying, but redefine the destination texture in case sourceArea is larger
        ANGLE_TRY(redefineDest(destExtents));
        if (context->isWebGL() || context->isRobustResourceInitEnabled())
        {
            ANGLE_TRY(initializeContents(context, GL_NONE, destIndex));
        }
        return angle::Result::Continue;
    }

    const gl::Offset destOffset(clippedArea.x - sourceArea.x, clippedArea.y - sourceArea.y, 0);

    std::unique_ptr<ImageD3D> sourceCopy;
    ANGLE_TRY(SnapshotReadFramebufferToImage(context, mRenderer, snapshotType, sizedInternalFormat,
                                             clippedArea, source, &sourceCopy));

    ANGLE_TRY(redefineDest(destExtents));

    if (outside && (context->isWebGL() || context->isRobustResourceInitEnabled()))
    {
        ANGLE_TRY(initializeContents(context, GL_NONE, destIndex));
    }

    ImageD3D *destImage = nullptr;
    ANGLE_TRY(getImageAndSyncFromStorageIfNeeded(context, destIndex, &destImage));

    const gl::Box sourceBox(0, 0, 0, clippedArea.width, clippedArea.height, 1);
    ANGLE_TRY(mRenderer->copyImage(context, destImage, sourceCopy.get(), sourceBox, destOffset,
                                   false, false, false));

    mDirtyImages = true;
    const gl::Box destRegion(destOffset.x, destOffset.y, 0, clippedArea.width, clippedArea.height,
                             1);
    ANGLE_TRY(commitRegion(context, destIndex, destRegion));
    return angle::Result::Continue;
}

angle::Result TextureD3D::getNativeTexture(const gl::Context *context, TextureStorage **outStorage)
{
    // ensure the underlying texture is created
    ANGLE_TRY(initializeStorage(context, BindFlags()));

    if (mTexStorage)
    {
        ANGLE_TRY(updateStorage(context));
    }

    ASSERT(outStorage);

    *outStorage = mTexStorage;
    return angle::Result::Continue;
}

angle::Result TextureD3D::getImageAndSyncFromStorageIfNeeded(const gl::Context *context,
                                                             const gl::ImageIndex &index,
                                                             ImageD3D **outImage)
{
    ImageD3D *image = getImage(index);
    // Syncing from mTexStorage is needed only when mTexStorage exists/is complete and
    // !image->isDirty(). In TextureD3D, !image->isDirty() means mTexStorage holds the latest
    // authoritative data (e.g. from FBO rendering/clears or direct TextureStorage11::setData
    // uploads) while the CPU staging buffer does not. Conversely, when image->isDirty() is true,
    // the CPU staging image already holds newer uncommitted data, so we must not overwrite it from
    // mTexStorage (and we defer flushing staging to mTexStorage until commitRegion/updateStorage).
    if (mTexStorage && isValidIndex(index) && isImageComplete(index) && !image->isDirty())
    {
        ANGLE_TRY(image->copyFromTexStorage(context, index, mTexStorage));
        image->markClean();
    }
    *outImage = image;
    return angle::Result::Continue;
}

// For immutable textures (glTexStorage*D), all mip levels from 0 to levels-1 are fixed at creation
// time and mTexStorage is not released when GL_TEXTURE_BASE_LEVEL changes. If storage is later
// recreated (e.g. via ensureBindFlags to add RenderTarget bind flags) while getBaseLevel() > 0, we
// must use mTexStorage's actual level-0 dimensions rather than extrapolating via
// `getBaseLevel*() << getBaseLevel()`, which can produce wrong level-0 dimensions in two cases:
// 1. A dimension already clamped to 1 at getBaseLevel() (e.g. a 16384x1 texture at baseLevel 13 has
//    height 1, so `1 << 13` would compute 8192 instead of 1).
// 2. Non-power-of-two dimensions where low bits were shifted out at getBaseLevel() (e.g. a 13x13
//    texture at baseLevel 1 has width 6, so `6 << 1` would compute 12 instead of 13).
GLint TextureD3D::getLevelZeroWidth() const
{
    if (isImmutable())
    {
        ASSERT(mTexStorage);
        return mTexStorage->getLevelWidth(0);
    }
    ASSERT(gl::CountLeadingZeros(static_cast<uint32_t>(getBaseLevelWidth())) > getBaseLevel());
    return getBaseLevelWidth() << getBaseLevel();
}

GLint TextureD3D::getLevelZeroHeight() const
{
    if (isImmutable())
    {
        ASSERT(mTexStorage);
        return mTexStorage->getLevelHeight(0);
    }
    ASSERT(gl::CountLeadingZeros(static_cast<uint32_t>(getBaseLevelHeight())) > getBaseLevel());
    return getBaseLevelHeight() << getBaseLevel();
}

GLint TextureD3D::getLevelZeroDepth() const
{
    return 1;
}

GLint TextureD3D::getBaseLevelStorageDepth() const
{
    return getLevelZeroDepth();
}

GLint TextureD3D::getBaseLevelWidth() const
{
    const ImageD3D *baseImage = getBaseLevelImage();
    return (baseImage ? baseImage->getWidth() : 0);
}

GLint TextureD3D::getBaseLevelHeight() const
{
    const ImageD3D *baseImage = getBaseLevelImage();
    return (baseImage ? baseImage->getHeight() : 0);
}

GLint TextureD3D::getBaseLevelDepth() const
{
    const ImageD3D *baseImage = getBaseLevelImage();
    return (baseImage ? baseImage->getDepth() : 0);
}

// Note: "base level image" is loosely defined to be any image from the base level,
// where in the base of 2D array textures and cube maps there are several. Don't use
// the base level image for anything except querying texture format and size.
GLenum TextureD3D::getBaseLevelInternalFormat() const
{
    const ImageD3D *baseImage = getBaseLevelImage();
    return (baseImage ? baseImage->getInternalFormat() : GL_NONE);
}

angle::Result TextureD3D::setStorage(const gl::Context *context,
                                     gl::TextureType type,
                                     size_t levels,
                                     GLenum internalFormat,
                                     const gl::Extents &size)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D::setStorageImpl(const gl::Context *context,
                                         gl::TextureType type,
                                         size_t levels,
                                         GLenum internalFormat,
                                         const gl::Extents &size)
{
    // TODO(geofflang): Verify storage creation had no errors
    BindFlags bindFlags;
    bindFlags.renderTarget = IsRenderTargetUsage(mState.getUsage());

    TexStoragePointer storage;
    switch (type)
    {
        case gl::TextureType::_2D:
            storage = {mRenderer->createTextureStorage2D(internalFormat, bindFlags, size.width,
                                                         size.height, static_cast<int>(levels),
                                                         mState.getLabel()),
                       context};
            break;
        case gl::TextureType::CubeMap:
            storage = {
                mRenderer->createTextureStorageCube(internalFormat, bindFlags, size.width,
                                                    static_cast<int>(levels), mState.getLabel()),
                context};
            break;
        case gl::TextureType::_3D:
            storage = {mRenderer->createTextureStorage3D(
                           internalFormat, bindFlags, size.width, size.height, size.depth,
                           static_cast<int>(levels), mState.getLabel()),
                       context};
            break;
        case gl::TextureType::_2DArray:
            storage = {mRenderer->createTextureStorage2DArray(
                           internalFormat, bindFlags, size.width, size.height, size.depth,
                           static_cast<int>(levels), mState.getLabel()),
                       context};
            break;
        default:
            UNREACHABLE();
            return angle::Result::Stop;
    }

    ANGLE_TRY(setCompleteTexStorage(context, storage.get()));
    storage.release();

    mImmutable = true;

    ANGLE_TRY(updateStorage(context));

    return angle::Result::Continue;
}

angle::Result TextureD3D::setStorageMultisample(const gl::Context *context,
                                                gl::TextureType type,
                                                GLsizei samples,
                                                GLint internalformat,
                                                const gl::Extents &size,
                                                bool fixedSampleLocations)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D::setBuffer(const gl::Context *context, GLenum internalFormat)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D::setStorageExternalMemory(const gl::Context *context,
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
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

bool TextureD3D::couldUseSetData() const
{
    if (!mRenderer->getFeatures().setDataFasterThanImageUpload.enabled)
    {
        return false;
    }

    if (!mRenderer->getFeatures().setDataFasterThanImageUploadOn128bitFormats.enabled)
    {
        gl::InternalFormat internalFormat =
            gl::GetSizedInternalFormatInfo(getBaseLevelInternalFormat());
        return internalFormat.pixelBytes < 16;
    }

    return true;
}

bool TextureD3D::shouldUseSetData(const gl::ImageIndex &index, const ImageD3D *image) const
{
    if (!isValidIndex(index) || !isImageComplete(index))
    {
        return false;
    }

    if (!couldUseSetData())
    {
        return false;
    }

    if (image->isDirty())
    {
        return false;
    }

    gl::InternalFormat internalFormat = gl::GetSizedInternalFormatInfo(image->getInternalFormat());

    // We can only handle full updates for depth-stencil textures, so to avoid complications
    // disable them entirely.
    if (internalFormat.depthBits > 0 || internalFormat.stencilBits > 0)
    {
        return false;
    }

    // Emulated RGBX/BGRX formats must always use the slow path to ensure the alpha channel is set
    // to 1.0.
    if (gl::IsRGBXOrBGRXFormat(internalFormat.sizedInternalFormat))
    {
        return false;
    }

    // TODO(jmadill): Handle compressed internal formats
    return (mTexStorage && !internalFormat.compressed);
}

bool TextureD3D::isImageSubresourceMatchingStorage(const gl::ImageIndex &index,
                                                   const ImageD3D *image,
                                                   int storageWidth0,
                                                   int storageHeight0,
                                                   int storageDepth0,
                                                   GLenum storageFormat,
                                                   size_t storageLevels) const
{
    if (!image)
    {
        return false;
    }

    ASSERT(storageWidth0 > 0 && storageHeight0 > 0 && storageDepth0 > 0 &&
           storageFormat != GL_NONE);

    if (index.getLevelIndex() < 0 || index.getLevelIndex() >= static_cast<int>(storageLevels))
    {
        return false;
    }

    if (mState.getType() == gl::TextureType::_2DArray && index.getLayerIndex() >= storageDepth0)
    {
        return false;
    }

    const int storageWidth  = std::max(1, storageWidth0 >> index.getLevelIndex());
    const int storageHeight = std::max(1, storageHeight0 >> index.getLevelIndex());
    const int storageDepth  = (mState.getType() == gl::TextureType::_3D)
                                  ? std::max(1, storageDepth0 >> index.getLevelIndex())
                                  : 1;

    return image->getWidth() == storageWidth && image->getHeight() == storageHeight &&
           image->getDepth() == storageDepth && image->getInternalFormat() == storageFormat;
}

angle::Result TextureD3D::releaseTexStorageIfMismatched(const gl::Context *context,
                                                        GLint level,
                                                        GLenum internalformat,
                                                        const gl::Extents &size,
                                                        bool forceReleaseStorage)
{
    return releaseTexStorageIfMismatched(context, 0 /*faceIndex*/, level, internalformat, size,
                                         forceReleaseStorage);
}

angle::Result TextureD3D::releaseTexStorageIfMismatched(const gl::Context *context,
                                                        size_t faceIndex,
                                                        GLint level,
                                                        GLenum internalformat,
                                                        const gl::Extents &size,
                                                        bool forceReleaseStorage)
{
    if (!mTexStorage)
    {
        return angle::Result::Continue;
    }

    auto shouldReleaseStorage = [&]() {
        const int storageLevels = mTexStorage->getLevelCount();
        return (level >= storageLevels && storageLevels != 0) ||
               size.width != mTexStorage->getLevelWidth(level) ||
               size.height != mTexStorage->getLevelHeight(level) ||
               size.depth != mTexStorage->getLevelDepth(level) ||
               internalformat != mTexStorage->getFormat();
    };

    if (forceReleaseStorage || shouldReleaseStorage())
    {
        markAllImagesDirty();

        gl::CubeFaceArray<gl::TexLevelMask> copyImageMasks;
        copyImageMasks.fill(gl::TexLevelMask().set());
        copyImageMasks[faceIndex].set(level, false);

        ANGLE_TRY(releaseTexStorage(context, copyImageMasks));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D::setImageImpl(const gl::Context *context,
                                       const gl::ImageIndex &index,
                                       GLenum type,
                                       const gl::PixelUnpackState &unpack,
                                       gl::Buffer *unpackBuffer,
                                       const uint8_t *pixels,
                                       ptrdiff_t layerOffset)
{
    ImageD3D *image = getImage(index);
    ASSERT(image);

    // No-op
    if (image->getWidth() == 0 || image->getHeight() == 0 || image->getDepth() == 0)
    {
        return angle::Result::Continue;
    }

    // We no longer need the "GLenum format" parameter to TexImage to determine what data format
    // "pixels" contains. From our image internal format we know how many channels to expect, and
    // "type" gives the format of pixel's components.
    const uint8_t *pixelData = nullptr;
    ANGLE_TRY(GetUnpackPointer(context, unpack, unpackBuffer, pixels, layerOffset, &pixelData));

    if (pixelData != nullptr)
    {
        if (shouldUseSetData(index, image))
        {
            ANGLE_TRY(
                mTexStorage->setData(context, index, image, nullptr, type, unpack, pixelData));
        }
        else
        {
            gl::Box fullImageArea(0, 0, 0, image->getWidth(), image->getHeight(),
                                  image->getDepth());
            ANGLE_TRY(image->loadData(context, fullImageArea, unpack, type, pixelData,
                                      index.usesTex3D()));
        }

        mDirtyImages = true;
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D::subImage(const gl::Context *context,
                                   const gl::ImageIndex &index,
                                   const gl::Box &area,
                                   GLenum format,
                                   GLenum type,
                                   const gl::PixelUnpackState &unpack,
                                   gl::Buffer *unpackBuffer,
                                   const uint8_t *pixels,
                                   ptrdiff_t layerOffset)
{
    // CPU readback & copy where direct GPU copy is not supported
    const uint8_t *pixelData = nullptr;
    ANGLE_TRY(GetUnpackPointer(context, unpack, unpackBuffer, pixels, layerOffset, &pixelData));

    if (pixelData != nullptr)
    {
        ImageD3D *image = getImage(index);
        ASSERT(image);

        if (shouldUseSetData(index, image) && !mTexStorage->isMultiplanar(context))
        {
            return mTexStorage->setData(context, index, image, &area, type, unpack, pixelData);
        }

        ANGLE_TRY(image->loadData(context, area, unpack, type, pixelData, index.usesTex3D()));
        ANGLE_TRY(commitRegion(context, index, area));
        mDirtyImages = true;
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D::setCompressedImageImpl(const gl::Context *context,
                                                 const gl::ImageIndex &index,
                                                 const gl::PixelUnpackState &unpack,
                                                 const uint8_t *pixels,
                                                 ptrdiff_t layerOffset)
{
    ImageD3D *image = getImage(index);
    ASSERT(image);

    if (image->getWidth() == 0 || image->getHeight() == 0 || image->getDepth() == 0)
    {
        return angle::Result::Continue;
    }

    // We no longer need the "GLenum format" parameter to TexImage to determine what data format
    // "pixels" contains. From our image internal format we know how many channels to expect, and
    // "type" gives the format of pixel's components.
    const uint8_t *pixelData = nullptr;
    gl::Buffer *unpackBuffer = context->getState().getTargetBuffer(gl::BufferBinding::PixelUnpack);
    ANGLE_TRY(GetUnpackPointer(context, unpack, unpackBuffer, pixels, layerOffset, &pixelData));

    if (pixelData != nullptr)
    {
        gl::Box fullImageArea(0, 0, 0, image->getWidth(), image->getHeight(), image->getDepth());
        ANGLE_TRY(image->loadCompressedData(context, fullImageArea, pixelData));

        mDirtyImages = true;
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D::subImageCompressed(const gl::Context *context,
                                             const gl::ImageIndex &index,
                                             const gl::Box &area,
                                             GLenum format,
                                             const gl::PixelUnpackState &unpack,
                                             const uint8_t *pixels,
                                             ptrdiff_t layerOffset)
{
    const uint8_t *pixelData = nullptr;
    gl::Buffer *unpackBuffer = context->getState().getTargetBuffer(gl::BufferBinding::PixelUnpack);
    ANGLE_TRY(GetUnpackPointer(context, unpack, unpackBuffer, pixels, layerOffset, &pixelData));

    if (pixelData != nullptr)
    {
        ImageD3D *image = getImage(index);
        ASSERT(image);

        ANGLE_TRY(image->loadCompressedData(context, area, pixelData));

        mDirtyImages = true;
    }

    return angle::Result::Continue;
}

bool TextureD3D::isFastUnpackable(const gl::Buffer *unpackBuffer,
                                  const gl::PixelUnpackState &unpack,
                                  GLenum sizedInternalFormat)
{
    return unpackBuffer != nullptr && unpack.skipRows == 0 && unpack.skipPixels == 0 &&
           unpack.imageHeight == 0 && unpack.skipImages == 0 &&
           mRenderer->supportsFastCopyBufferToTexture(sizedInternalFormat);
}

angle::Result TextureD3D::fastUnpackPixels(const gl::Context *context,
                                           const gl::PixelUnpackState &unpack,
                                           gl::Buffer *unpackBuffer,
                                           const uint8_t *pixels,
                                           const gl::Box &destArea,
                                           GLenum sizedInternalFormat,
                                           GLenum type,
                                           RenderTargetD3D *destRenderTarget)
{
    bool check = (unpack.skipRows != 0 || unpack.skipPixels != 0 || unpack.imageHeight != 0 ||
                  unpack.skipImages != 0);
    ANGLE_CHECK(GetImplAs<ContextD3D>(context), !check,
                "Unimplemented pixel store parameters in fastUnpackPixels", GL_INVALID_OPERATION);

    // No-op
    if (destArea.width <= 0 && destArea.height <= 0 && destArea.depth <= 0)
    {
        return angle::Result::Continue;
    }

    // In order to perform the fast copy through the shader, we must have the right format, and be
    // able to create a render target.
    ASSERT(mRenderer->supportsFastCopyBufferToTexture(sizedInternalFormat));

    uintptr_t offset = reinterpret_cast<uintptr_t>(pixels);

    ANGLE_TRY(mRenderer->fastCopyBufferToTexture(
        context, unpack, unpackBuffer, static_cast<unsigned int>(offset), destRenderTarget,
        sizedInternalFormat, type, destArea));

    return angle::Result::Continue;
}

GLint TextureD3D::creationLevels(GLsizei width, GLsizei height, GLsizei depth) const
{
    if ((gl::isPow2(width) && gl::isPow2(height) && gl::isPow2(depth)) ||
        mRenderer->getNativeExtensions().textureNpotOES)
    {
        // Maximum number of levels
        return gl::log2(std::max({width, height, depth})) + 1;
    }
    else
    {
        // OpenGL ES 2.0 without GL_OES_texture_npot does not permit NPOT mipmaps.
        return 1;
    }
}

bool TextureD3D::isValidLevel(int level) const
{
    return mTexStorage && level >= 0 && level < mTexStorage->getLevelCount();
}

bool TextureD3D::isLevelComplete(int level) const
{
    if (isImmutable())
    {
        return isValidLevel(level);
    }

    GLsizei width  = getLevelZeroWidth();
    GLsizei height = getLevelZeroHeight();
    GLsizei depth  = getLevelZeroDepth();

    if (width <= 0 || height <= 0 || depth <= 0)
    {
        return false;
    }

    if (level >= creationLevels(width, height, depth))
    {
        return false;
    }

    if (level == static_cast<int>(getBaseLevel()))
    {
        return true;
    }

    return isNonBaseLevelComplete(level);
}

bool TextureD3D::isNonBaseLevelComplete(int level) const
{
    return false;
}

TextureStorage *TextureD3D::getStorage()
{
    ASSERT(mTexStorage);
    return mTexStorage;
}

ImageD3D *TextureD3D::getBaseLevelImage() const
{
    const GLuint baseLevel = getBaseLevel();
    if (baseLevel >= gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS)
    {
        return nullptr;
    }
    return getImage(getImageIndex(baseLevel, 0));
}

angle::Result TextureD3D::setImageExternal(const gl::Context *context,
                                           gl::TextureType type,
                                           egl::Stream *stream,
                                           const egl::Stream::GLTextureDescription &desc)
{
    // Only external images can accept external textures
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D::generateMipmap(const gl::Context *context)
{
    const GLuint baseLevel = mState.getEffectiveBaseLevel();
    const GLuint maxLevel  = mState.getMipmapMaxLevel();
    ASSERT(maxLevel > baseLevel);  // Should be checked before calling this.

    // Set up proper mipmap chain in our Image array.
    ANGLE_TRY(initMipmapImages(context));

    if (mTexStorage && mTexStorage->supportsNativeMipmapFunction())
    {
        ANGLE_TRY(updateStorage(context));

        // Generate the mipmap chain using the ad-hoc DirectX function.
        ANGLE_TRY(mRenderer->generateMipmapUsingD3D(context, mTexStorage, mState));
    }
    else
    {
        // Generate the mipmap chain, one level at a time.
        ANGLE_TRY(generateMipmapUsingImages(context, maxLevel));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D::generateMipmapUsingImages(const gl::Context *context,
                                                    const GLuint maxLevel)
{
    const GLuint baseLevel = getBaseLevel();
    // We know that all layers have the same dimension, for the texture to be complete
    GLint layerCount = static_cast<GLint>(getLayerCount(baseLevel));

    if (mTexStorage && !mTexStorage->isRenderTarget() &&
        canCreateRenderTargetForImage(getImageIndex(baseLevel, 0)) &&
        mRenderer->getRendererClass() == RENDERER_D3D11)
    {
        if (!couldUseSetData())
        {
            ANGLE_TRY(updateStorage(context));
        }
        ANGLE_TRY(ensureRenderTarget(context));
    }
    else if (couldUseSetData() && mTexStorage)
    {
        // When making mipmaps with the setData workaround enabled, the texture storage has
        // the image data already. For non-render-target storage, we have to pull it out into
        // an image layer.
        if (!mTexStorage->isRenderTarget())
        {
            // Copy from the storage mip 0 to Image mip 0
            for (GLint layer = 0; layer < layerCount; ++layer)
            {
                gl::ImageIndex srcIndex = getImageIndex(baseLevel, layer);

                ImageD3D *image = getImage(srcIndex);
                ANGLE_TRY(image->copyFromTexStorage(context, srcIndex, mTexStorage));
            }
        }
        else
        {
            ANGLE_TRY(updateStorage(context));
        }
    }

    bool renderableStorage = (mTexStorage && mTexStorage->isRenderTarget());
    if (renderableStorage)
    {
        ANGLE_TRY(updateStorage(context));
    }

    for (GLint layer = 0; layer < layerCount; ++layer)
    {
        for (GLuint mip = baseLevel + 1; mip <= maxLevel; ++mip)
        {
            ASSERT(getLayerCount(mip) == layerCount);

            gl::ImageIndex sourceIndex = getImageIndex(mip - 1, layer);
            gl::ImageIndex destIndex   = getImageIndex(mip, layer);

            if (renderableStorage)
            {
                // GPU-side mipmapping
                ANGLE_TRY(mTexStorage->generateMipmap(context, sourceIndex, destIndex));
            }
            else
            {
                // CPU-side mipmapping
                ANGLE_TRY(
                    mRenderer->generateMipmap(context, getImage(destIndex), getImage(sourceIndex)));
            }
        }
    }

    mDirtyImages = !renderableStorage;

    if (mTexStorage && mDirtyImages)
    {
        ANGLE_TRY(updateStorage(context));
    }

    return angle::Result::Continue;
}

bool TextureD3D::isBaseImageZeroSize() const
{
    ImageD3D *baseImage = getBaseLevelImage();

    if (!baseImage || baseImage->getWidth() <= 0 || baseImage->getHeight() <= 0)
    {
        return true;
    }

    if (baseImage->getType() == gl::TextureType::_3D && baseImage->getDepth() <= 0)
    {
        return true;
    }

    if (baseImage->getType() == gl::TextureType::_2DArray && getLayerCount(getBaseLevel()) <= 0)
    {
        return true;
    }

    return false;
}

angle::Result TextureD3D::ensureBindFlags(const gl::Context *context, BindFlags bindFlags)
{
    ANGLE_TRY(initializeStorage(context, bindFlags));

    // initializeStorage can fail with NoError if the texture is not complete. This is not
    // an error for incomplete sampling, but it is a big problem for rendering.
    if (!mTexStorage)
    {
        ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
        return angle::Result::Stop;
    }

    if (!isBaseImageZeroSize())
    {
        ASSERT(mTexStorage);
        if ((bindFlags.renderTarget && !mTexStorage->isRenderTarget()) ||
            (bindFlags.unorderedAccess && !mTexStorage->isUnorderedAccess()))
        {
            // Preserve all the texture's previous bind flags when creating a new storage.
            BindFlags newBindFlags = bindFlags;
            if (mTexStorage->isRenderTarget())
            {
                newBindFlags.renderTarget = true;
            }
            if (mTexStorage->isUnorderedAccess())
            {
                newBindFlags.unorderedAccess = true;
            }

            TexStoragePointer newStorage;
            ANGLE_TRY(createCompleteStorage(context, newBindFlags, &newStorage));

            ANGLE_TRY(mTexStorage->copyToStorage(context, newStorage.get()));
            ANGLE_TRY(setCompleteTexStorage(context, newStorage.get()));
            newStorage.release();
            // If this texture is used in compute shader, we should invalidate this texture so that
            // the UAV/SRV is rebound again with this new texture storage in next dispatch call.
            mTexStorage->invalidateTextures();
        }
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D::ensureRenderTarget(const gl::Context *context)
{
    return ensureBindFlags(context, BindFlags::RenderTarget());
}

angle::Result TextureD3D::ensureUnorderedAccess(const gl::Context *context)
{
    return ensureBindFlags(context, BindFlags::UnorderedAccess());
}

bool TextureD3D::canCreateRenderTargetForImage(const gl::ImageIndex &index) const
{
    if (index.getType() == gl::TextureType::_2DMultisample ||
        index.getType() == gl::TextureType::_2DMultisampleArray)
    {
        ASSERT(index.getType() != gl::TextureType::_2DMultisampleArray || index.hasLayer());
        return true;
    }

    ImageD3D *image = getImage(index);
    ASSERT(image);
    bool levelsComplete =
        (isImageComplete(index) && isImageComplete(getImageIndex(getBaseLevel(), 0)));
    return (image->isRenderableFormat() && levelsComplete);
}

angle::Result TextureD3D::copyImageFromFramebufferToStaging(const gl::Context *context,
                                                            const gl::ImageIndex &index,
                                                            const gl::Offset &destOffset,
                                                            const gl::Rectangle &clippedArea,
                                                            gl::Framebuffer *source)
{
    ASSERT(!canCreateRenderTargetForImage(index));
    // canCreateRenderTargetForImage(index) checks isRenderableFormat(), isImageComplete(index),
    // and base-level completeness. Because all formats valid for CopyTexImage/CopyTexSubImage are
    // renderable and isImageComplete(index) implies the base level is also complete,
    // !canCreateRenderTargetForImage(index) means !isImageComplete(index).
    //
    // Since mTexStorage only stores mip-complete levels (and updateStorage() skips incomplete
    // levels), mTexStorage never holds newer data for an incomplete index, so no sync from
    // mTexStorage to the staging image is needed.
    //
    // onStateChange(DirtyBitsFlagged) is not needed here: CopyTexImage already signals
    // SubjectChanged via signalDirtyStorage(), and for CopyTexSubImage an incomplete level cannot
    // be uploaded to mTexStorage or sampled until a subsequent level redefinition or texture
    // parameter change (e.g. GL_TEXTURE_BASE_LEVEL) makes it complete, which itself signals
    // SubjectChanged.
    ASSERT(!isImageComplete(index));

    ImageD3D *image = getImage(index);
    ASSERT(image);
    gl::Offset stagingOffset(destOffset.x, destOffset.y, 0);
    ANGLE_TRY(image->copyFromFramebuffer(context, stagingOffset, clippedArea, source));
    mDirtyImages = true;
    return angle::Result::Continue;
}

angle::Result TextureD3D::commitRegion(const gl::Context *context,
                                       const gl::ImageIndex &index,
                                       const gl::Box &region)
{
    if (isValidIndex(index) && isImageComplete(index))
    {
        ImageD3D *image = getImage(index);
        ANGLE_TRY(image->copyToStorage(context, mTexStorage, index, region));
        image->markClean();
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D::getAttachmentRenderTarget(const gl::Context *context,
                                                    GLenum binding,
                                                    const gl::ImageIndex &imageIndex,
                                                    GLsizei samples,
                                                    FramebufferAttachmentRenderTarget **rtOut)
{
    RenderTargetD3D *rtD3D = nullptr;
    ANGLE_TRY(getRenderTarget(context, imageIndex, samples, &rtD3D));
    *rtOut = static_cast<FramebufferAttachmentRenderTarget *>(rtD3D);
    return angle::Result::Continue;
}

angle::Result TextureD3D::setBaseLevel(const gl::Context *context, GLuint baseLevel)
{
    // gl::Texture::setBaseLevel updates mState before calling into the backend, so getBaseLevel()
    // (which queries mState.getEffectiveBaseLevel()) already returns the new baseLevel.
    ASSERT(baseLevel == getBaseLevel());

    // If mTexStorage has not been created yet, there is no existing storage to back up or release;
    // when storage is later initialized, it will use the updated getBaseLevel().
    // For immutable textures (created via glTexStorage*D), the storage already covers all mip
    // levels with the correct dimensions, so it should never be released on base level change.
    // For EGLImage targets, mTexStorage wraps the external EGLImage (TextureStorage11_EGLImage)
    // and cannot be backed up to mImageArray or recreated by initializeStorage, so it must also
    // remain intact on base level change (completeness is handled by the frontend).
    if (!mTexStorage || isImmutable() || mEGLImageTarget)
    {
        return angle::Result::Continue;
    }

    const int oldStorageWidth     = mTexStorage->getLevelWidth(0);
    const int oldStorageHeight    = mTexStorage->getLevelHeight(0);
    const int oldStorageDepth     = mTexStorage->getLevelDepth(0);
    const GLenum oldStorageFormat = mTexStorage->getFormat();

    // When the base level changes, the texture storage might not be valid anymore, since it could
    // have been created based on the dimensions of the previous specified level range.
    // getLevelZeroWidth() can compute incorrect dimensions for NPOT textures because
    // (width >> N) << N != width when width is not a power of two, which would cause a spurious
    // dimension mismatch and lead to recreating the storage with wrong dimensions.
    const int newStorageWidth     = getLevelZeroWidth();
    const int newStorageHeight    = getLevelZeroHeight();
    const int newStorageDepth     = getBaseLevelStorageDepth();
    const GLenum newStorageFormat = getBaseLevelInternalFormat();
    if (newStorageWidth <= 0 || newStorageHeight <= 0 || newStorageDepth <= 0 ||
        newStorageWidth != oldStorageWidth || newStorageHeight != oldStorageHeight ||
        newStorageDepth != oldStorageDepth || newStorageFormat != oldStorageFormat)
    {
        // Even if the new base level has zero/invalid dimensions (making the texture incomplete),
        // changing GL_TEXTURE_BASE_LEVEL does not delete or invalidate pixel data in other mip
        // levels (e.g. level 0). Because we must release mTexStorage now, we first back up any
        // mip levels that match oldStorage into their respective ImageD3D staging buffers so their
        // contents are preserved if the base level is later restored.
        //
        // We need to copy for both:
        // 1. Render targets: GPU rendering modified the storage directly.
        // 2. Fast-path uploads: TextureStorage11::setData wrote pixel data directly to the GPU
        //    texture without creating an Image11 staging buffer (leaving the image clean and
        //    unassociated). If storage is destroyed without backing this up, subsequent storage
        //    recreation will not re-upload the data, leading to uninitialized memory reads.
        const size_t storageLevels = mTexStorage->getLevelCount();

        gl::ImageIndexIterator iterator = imageIterator();
        while (iterator.hasNext())
        {
            const gl::ImageIndex index = iterator.next();
            ImageD3D *image            = getImage(index);

            RenderTargetD3D *renderTarget = nullptr;
            if (mTexStorage->isRenderTarget())
            {
                ANGLE_TRY(mTexStorage->findRenderTarget(context, index, &renderTarget));
            }

            const bool dimensionsMatch =
                isImageSubresourceMatchingStorage(index, image, oldStorageWidth, oldStorageHeight,
                                                  oldStorageDepth, oldStorageFormat, storageLevels);

            const bool hasNoStagingCopy =
                image && !image->isDirty() &&
                mState.getImageDesc(index).initState == gl::InitState::Initialized;

            if (dimensionsMatch && (renderTarget || hasNoStagingCopy))
            {
                ANGLE_TRY(image->copyFromTexStorage(context, index, mTexStorage));
            }
        }

        markAllImagesDirty();

        ANGLE_TRY(releaseTexStorage(context, gl::TexLevelMask()));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D::onLabelUpdate(const gl::Context *context)
{
    if (mTexStorage)
    {
        mTexStorage->setLabel(mState.getLabel());
    }
    return angle::Result::Continue;
}

angle::Result TextureD3D::syncState(const gl::Context *context,
                                    const gl::Texture::DirtyBits &dirtyBits,
                                    gl::Command source)
{
    // This could be improved using dirty bits.
    return angle::Result::Continue;
}

angle::Result TextureD3D::releaseTexStorage(const gl::Context *context,
                                            const gl::TexLevelMask &copyStorageToImagesMask)
{
    gl::CubeFaceArray<gl::TexLevelMask> copyMasks;
    copyMasks.fill(copyStorageToImagesMask);
    return releaseTexStorage(context, copyMasks);
}

angle::Result TextureD3D::releaseTexStorage(
    const gl::Context *context,
    const gl::CubeFaceArray<gl::TexLevelMask> &copyStorageToImagesMask)
{
    if (!mTexStorage)
    {
        return angle::Result::Continue;
    }

    bool anyCopyMask = false;
    for (const gl::TexLevelMask &mask : copyStorageToImagesMask)
    {
        if (mask.any())
        {
            anyCopyMask = true;
            break;
        }
    }

    if (anyCopyMask)
    {
        const int storageWidth0    = mTexStorage->getLevelWidth(0);
        const int storageHeight0   = mTexStorage->getLevelHeight(0);
        const int storageDepth0    = mTexStorage->getLevelDepth(0);
        const GLenum storageFormat = mTexStorage->getFormat();
        const size_t storageLevels = mTexStorage->getLevelCount();

        gl::ImageIndexIterator iterator = imageIterator();
        while (iterator.hasNext())
        {
            const gl::ImageIndex index = iterator.next();
            ImageD3D *image            = getImage(index);
            bool copyImage = false;
            if (mState.getType() == gl::TextureType::CubeMap)
            {
                copyImage =
                    copyStorageToImagesMask[index.cubeMapFaceIndex()][index.getLevelIndex()];
            }
            else
            {
                copyImage = copyStorageToImagesMask[0][index.getLevelIndex()];
            }

            const bool dimensionsMatch =
                isImageSubresourceMatchingStorage(index, image, storageWidth0, storageHeight0,
                                                  storageDepth0, storageFormat, storageLevels);

            if (dimensionsMatch && copyImage)
            {
                ANGLE_TRY(image->copyFromTexStorage(context, index, mTexStorage));
            }
        }
    }

    onStateChange(angle::SubjectMessage::StorageReleased);

    auto err = mTexStorage->onDestroy(context);
    if (!IsError(err))
    {
        SafeDelete(mTexStorage);
    }
    return err;
}

void TextureD3D::onDestroy(const gl::Context *context)
{
    (void)releaseTexStorage(context, gl::TexLevelMask());
}

angle::Result TextureD3D::initializeContents(const gl::Context *context,
                                             GLenum binding,
                                             const gl::ImageIndex &imageIndex)
{
    gl::ImageIndex index = imageIndex;

    // Special case for D3D11 3D textures. We can't create render targets for individual layers of a
    // 3D texture, so force the clear to the entire mip. There shouldn't ever be a case where we
    // would lose existing data.
    if (index.getType() == gl::TextureType::_3D)
    {
        index = gl::ImageIndex::Make3D(index.getLevelIndex(), gl::ImageIndex::kEntireLevel);
    }
    else if (index.getType() == gl::TextureType::_2DArray && !index.hasLayer())
    {
        std::array<GLint, gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS> tempLayerCounts;

        GLint levelIndex            = index.getLevelIndex();
        tempLayerCounts[levelIndex] = getLayerCount(levelIndex);
        gl::ImageIndexIterator iterator =
            gl::ImageIndexIterator::Make2DArray(levelIndex, levelIndex + 1, tempLayerCounts.data());
        while (iterator.hasNext())
        {
            ANGLE_TRY(initializeContents(context, GL_NONE, iterator.next()));
        }
        return angle::Result::Continue;
    }
    else if (index.getType() == gl::TextureType::_2DMultisampleArray && !index.hasLayer())
    {
        std::array<GLint, gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS> tempLayerCounts;

        ASSERT(index.getLevelIndex() == 0);
        tempLayerCounts[0] = getLayerCount(0);
        gl::ImageIndexIterator iterator =
            gl::ImageIndexIterator::Make2DMultisampleArray(tempLayerCounts.data());
        while (iterator.hasNext())
        {
            ANGLE_TRY(initializeContents(context, GL_NONE, iterator.next()));
        }
        return angle::Result::Continue;
    }

    // Force image clean.
    ImageD3D *image = getImage(index);
    if (image)
    {
        image->markClean();
    }

    // Fast path: can use a render target clear.
    const angle::FeaturesD3D &features = mRenderer->getFeatures();
    bool shouldUseClear                = (image == nullptr);
    if (canCreateRenderTargetForImage(index) &&
        (shouldUseClear || features.allowClearForRobustResourceInit.enabled))
    {
        ANGLE_TRY(ensureRenderTarget(context));
        ASSERT(mTexStorage);
        RenderTargetD3D *renderTarget = nullptr;
        ANGLE_TRY(mTexStorage->getRenderTarget(context, index, &renderTarget));
        ANGLE_TRY(mRenderer->initRenderTarget(context, renderTarget));

        // Force image clean again, the texture storage may have been re-created and the image used.
        if (image)
        {
            image->markClean();
        }

        mTexStorage->markLevelDirty(index.getLevelIndex());

        return angle::Result::Continue;
    }

    ASSERT(image != nullptr);

    // Slow path: non-renderable texture, incomplete level, or texture storage doesn't exist.
    ANGLE_TRY(image->initializeContents(context));

    if (isValidIndex(index) && isImageComplete(index))
    {
        gl::Box fullImageArea(0, 0, 0, image->getWidth(), image->getHeight(), image->getDepth());
        ANGLE_TRY(commitRegion(context, index, fullImageArea));
        image->markClean();
    }
    else
    {
        mDirtyImages = true;
    }
    return angle::Result::Continue;
}

void TextureD3D::onSubjectStateChange(angle::SubjectIndex index, angle::SubjectMessage message)
{
    onStateChange(message);
}

TextureD3D_2D::TextureD3D_2D(const gl::TextureState &state, RendererD3D *renderer)
    : TextureD3D(state, renderer)
{
    for (auto &image : mImageArray)
    {
        image.reset(renderer->createImage());
    }
}

void TextureD3D_2D::onDestroy(const gl::Context *context)
{
    // Delete the Images before the TextureStorage. Images might be relying on the TextureStorage
    // for some of their data. If TextureStorage is deleted before the Images, then their data will
    // be wastefully copied back from the GPU before we delete the Images.
    for (auto &image : mImageArray)
    {
        image.reset();
    }
    return TextureD3D::onDestroy(context);
}

TextureD3D_2D::~TextureD3D_2D() {}

ImageD3D *TextureD3D_2D::getImage(int level, int layer) const
{
    ASSERT(level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS);
    ASSERT(layer == 0);
    return mImageArray[level].get();
}

ImageD3D *TextureD3D_2D::getImage(const gl::ImageIndex &index) const
{
    ASSERT(index.getLevelIndex() < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS);
    ASSERT(!index.hasLayer());
    ASSERT(index.getType() == gl::TextureType::_2D);
    return mImageArray[index.getLevelIndex()].get();
}

GLsizei TextureD3D_2D::getLayerCount(int level) const
{
    ASSERT(level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS);
    return 1;
}

GLsizei TextureD3D_2D::getWidth(GLint level) const
{
    if (level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS)
        return mImageArray[level]->getWidth();
    else
        return 0;
}

GLsizei TextureD3D_2D::getHeight(GLint level) const
{
    if (level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS)
        return mImageArray[level]->getHeight();
    else
        return 0;
}

GLenum TextureD3D_2D::getInternalFormat(GLint level) const
{
    if (level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS)
        return mImageArray[level]->getInternalFormat();
    else
        return GL_NONE;
}

bool TextureD3D_2D::isDepth(GLint level) const
{
    return gl::GetSizedInternalFormatInfo(getInternalFormat(level)).depthBits > 0;
}

bool TextureD3D_2D::isSRGB(GLint level) const
{
    return gl::GetSizedInternalFormatInfo(getInternalFormat(level)).colorEncoding == GL_SRGB;
}

angle::Result TextureD3D_2D::setImage(const gl::Context *context,
                                      const gl::ImageIndex &index,
                                      GLenum internalFormat,
                                      const gl::Extents &size,
                                      GLenum format,
                                      GLenum type,
                                      const gl::PixelUnpackState &unpack,
                                      gl::Buffer *unpackBuffer,
                                      const uint8_t *pixels)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_2D && size.depth == 1);

    const gl::InternalFormat &internalFormatInfo = gl::GetInternalFormatInfo(internalFormat, type);

    bool fastUnpacked = false;

    ANGLE_TRY(redefineImage(context, index.getLevelIndex(), internalFormatInfo.sizedInternalFormat,
                            size, false));

    // Attempt a fast gpu copy of the pixel data to the surface
    if (mTexStorage)
    {
    }
    if (isFastUnpackable(unpackBuffer, unpack, internalFormatInfo.sizedInternalFormat) &&
        isLevelComplete(index.getLevelIndex()))
    {
        // Will try to create RT storage if it does not exist
        RenderTargetD3D *destRenderTarget = nullptr;
        ANGLE_TRY(getRenderTarget(context, index, 0, &destRenderTarget));

        gl::Box destArea(0, 0, 0, getWidth(index.getLevelIndex()), getHeight(index.getLevelIndex()),
                         1);

        ANGLE_TRY(fastUnpackPixels(context, unpack, unpackBuffer, pixels, destArea,
                                   internalFormatInfo.sizedInternalFormat, type, destRenderTarget));

        // Ensure we don't overwrite our newly initialized data
        mImageArray[index.getLevelIndex()]->markClean();

        fastUnpacked = true;
    }

    if (!fastUnpacked)
    {
        ANGLE_TRY(setImageImpl(context, index, type, unpack, unpackBuffer, pixels, 0));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::setSubImage(const gl::Context *context,
                                         const gl::ImageIndex &index,
                                         const gl::Box &area,
                                         GLenum format,
                                         GLenum type,
                                         const gl::PixelUnpackState &unpack,
                                         gl::Buffer *unpackBuffer,
                                         const uint8_t *pixels)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_2D && area.depth == 1 && area.z == 0);

    GLenum mipFormat = getInternalFormat(index.getLevelIndex());
    if (mTexStorage)
    {
    }
    if (isFastUnpackable(unpackBuffer, unpack, mipFormat) && isLevelComplete(index.getLevelIndex()))
    {
        RenderTargetD3D *renderTarget = nullptr;
        ANGLE_TRY(getRenderTarget(context, index, 0, &renderTarget));
        ASSERT(!mImageArray[index.getLevelIndex()]->isDirty());

        return fastUnpackPixels(context, unpack, unpackBuffer, pixels, area, mipFormat, type,
                                renderTarget);
    }
    else
    {
        return TextureD3D::subImage(context, index, area, format, type, unpack, unpackBuffer,
                                    pixels, 0);
    }
}

angle::Result TextureD3D_2D::setCompressedImage(const gl::Context *context,
                                                const gl::ImageIndex &index,
                                                GLenum internalFormat,
                                                const gl::Extents &size,
                                                const gl::PixelUnpackState &unpack,
                                                size_t imageSize,
                                                const uint8_t *pixels)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_2D && size.depth == 1);

    // compressed formats don't have separate sized internal formats-- we can just use the
    // compressed format directly
    ANGLE_TRY(redefineImage(context, index.getLevelIndex(), internalFormat, size, false));

    return setCompressedImageImpl(context, index, unpack, pixels, 0);
}

angle::Result TextureD3D_2D::setCompressedSubImage(const gl::Context *context,
                                                   const gl::ImageIndex &index,
                                                   const gl::Box &area,
                                                   GLenum format,
                                                   const gl::PixelUnpackState &unpack,
                                                   size_t imageSize,
                                                   const uint8_t *pixels)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_2D && area.depth == 1 && area.z == 0);
    ANGLE_TRY(TextureD3D::subImageCompressed(context, index, area, format, unpack, pixels, 0));

    return commitRegion(context, index, area);
}

angle::Result TextureD3D_2D::copyImage(const gl::Context *context,
                                       const gl::ImageIndex &index,
                                       const gl::Rectangle &sourceArea,
                                       GLenum internalFormat,
                                       gl::Framebuffer *source)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_2D);

    const gl::InternalFormat &internalFormatInfo =
        gl::GetInternalFormatInfo(internalFormat, GL_UNSIGNED_BYTE);
    gl::Extents sourceExtents(sourceArea.width, sourceArea.height, 1);

    gl::Extents fbSize = source->getReadColorAttachment()->getSize();

    // Does the read area extend beyond the framebuffer?
    bool outside = sourceArea.x < 0 || sourceArea.y < 0 ||
                   sourceArea.x + sourceArea.width > fbSize.width ||
                   sourceArea.y + sourceArea.height > fbSize.height;

    const gl::FramebufferAttachment *readAttachment = source->getReadColorAttachment();
    const bool isSelfCopy = readAttachment && readAttachment->type() == GL_TEXTURE &&
                            GetImplAs<TextureD3D>(readAttachment->getTexture()) == this &&
                            readAttachment->mipLevel() == index.getLevelIndex();

    // If redefining the destination would invalidate the source attachment, snapshot the source
    // first.
    if (isSelfCopy)
    {
        return handleCopyImageSelfCopyRedefine(
            context, gl::TextureType::_2D, internalFormatInfo.sizedInternalFormat, sourceArea,
            sourceExtents, outside, index, source, [&](const gl::Extents &extents) {
                return redefineImage(context, index.getLevelIndex(),
                                     internalFormatInfo.sizedInternalFormat, extents, false);
            });
    }

    ANGLE_TRY(redefineImage(context, index.getLevelIndex(), internalFormatInfo.sizedInternalFormat,
                            sourceExtents, false));

    // WebGL requires that pixels that would be outside the framebuffer are treated as zero values,
    // so clear the mip level to 0 prior to making the copy if any pixel would be sampled outside.
    // Same thing for robust resource init.
    if (outside && (context->isWebGL() || context->isRobustResourceInitEnabled()))
    {
        ANGLE_TRY(initializeContents(context, GL_NONE, index));
    }

    gl::Rectangle clippedArea;
    if (!ClipRectangle(sourceArea, gl::Rectangle(0, 0, fbSize.width, fbSize.height), &clippedArea))
    {
        // Empty source area, nothing to do.
        return angle::Result::Continue;
    }

    gl::Offset destOffset(clippedArea.x - sourceArea.x, clippedArea.y - sourceArea.y, 0);

    if (!canCreateRenderTargetForImage(index))
    {
        ANGLE_TRY(
            copyImageFromFramebufferToStaging(context, index, destOffset, clippedArea, source));
    }
    else
    {
        ANGLE_TRY(ensureRenderTarget(context));

        if (clippedArea.width != 0 && clippedArea.height != 0 &&
            isValidLevel(index.getLevelIndex()))
        {
            ANGLE_TRY(updateStorageLevel(context, index.getLevelIndex()));
            ANGLE_TRY(mRenderer->copyImage2D(context, source, clippedArea,
                                             internalFormatInfo.format, destOffset, mTexStorage,
                                             index.getLevelIndex()));
        }
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::copySubImage(const gl::Context *context,
                                          const gl::ImageIndex &index,
                                          const gl::Offset &destOffset,
                                          const gl::Rectangle &sourceArea,
                                          gl::Framebuffer *source)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_2D && destOffset.z == 0);

    gl::Extents fbSize = source->getReadColorAttachment()->getSize();
    gl::Rectangle clippedArea;
    if (!ClipRectangle(sourceArea, gl::Rectangle(0, 0, fbSize.width, fbSize.height), &clippedArea))
    {
        return angle::Result::Continue;
    }
    const gl::Offset clippedOffset(destOffset.x + clippedArea.x - sourceArea.x,
                                   destOffset.y + clippedArea.y - sourceArea.y, 0);

    // can only make our texture storage to a render target if the base level is defined (with a
    // width & height) and the current level we're copying to is defined (with appropriate format,
    // width & height)

    if (!canCreateRenderTargetForImage(index))
    {
        ANGLE_TRY(
            copyImageFromFramebufferToStaging(context, index, clippedOffset, clippedArea, source));
    }
    else
    {
        ANGLE_TRY(ensureRenderTarget(context));

        if (isValidLevel(index.getLevelIndex()))
        {
            ANGLE_TRY(updateStorageLevel(context, index.getLevelIndex()));
            ANGLE_TRY(mRenderer->copyImage2D(context, source, clippedArea,
                                             gl::GetUnsizedFormat(getBaseLevelInternalFormat()),
                                             clippedOffset, mTexStorage, index.getLevelIndex()));
        }
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::copyTexture(const gl::Context *context,
                                         const gl::ImageIndex &index,
                                         GLenum internalFormat,
                                         GLenum type,
                                         gl::LevelIndex sourceLevel,
                                         bool unpackFlipY,
                                         bool unpackPremultiplyAlpha,
                                         bool unpackUnmultiplyAlpha,
                                         const gl::Texture *source)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_2D);

    gl::TextureType sourceType = source->getType();

    const gl::InternalFormat &internalFormatInfo = gl::GetInternalFormatInfo(internalFormat, type);
    gl::Extents size(static_cast<int>(source->getWidth(NonCubeTextureTypeToTarget(sourceType),
                                                       sourceLevel.get())),
                     static_cast<int>(source->getHeight(NonCubeTextureTypeToTarget(sourceType),
                                                        sourceLevel.get())),
                     1);
    ANGLE_TRY(redefineImage(context, index.getLevelIndex(), internalFormatInfo.sizedInternalFormat,
                            size, false));

    gl::Box sourceBox(0, 0, 0, size.width, size.height, 1);
    gl::Offset destOffset(0, 0, 0);

    if (!isSRGB(index.getLevelIndex()) && canCreateRenderTargetForImage(index))
    {
        ANGLE_TRY(ensureRenderTarget(context));
        ASSERT(isValidLevel(index.getLevelIndex()));
        ANGLE_TRY(updateStorageLevel(context, index.getLevelIndex()));

        ANGLE_TRY(mRenderer->copyTexture(context, source, sourceLevel.get(), gl::TextureTarget::_2D,
                                         sourceBox, internalFormatInfo.format,
                                         internalFormatInfo.type, destOffset, mTexStorage,
                                         index.getTarget(), index.getLevelIndex(), unpackFlipY,
                                         unpackPremultiplyAlpha, unpackUnmultiplyAlpha));
    }
    else
    {
        gl::ImageIndex sourceImageIndex = gl::ImageIndex::Make2D(sourceLevel.get());
        TextureD3D *sourceD3D           = GetImplAs<TextureD3D>(source);
        ImageD3D *sourceImage           = nullptr;
        ANGLE_TRY(
            sourceD3D->getImageAndSyncFromStorageIfNeeded(context, sourceImageIndex, &sourceImage));

        ImageD3D *destImage = nullptr;
        ANGLE_TRY(getImageAndSyncFromStorageIfNeeded(context, index, &destImage));

        ANGLE_TRY(mRenderer->copyImage(context, destImage, sourceImage, sourceBox, destOffset,
                                       unpackFlipY, unpackPremultiplyAlpha, unpackUnmultiplyAlpha));

        mDirtyImages = true;

        gl::Box destRegion(destOffset, size);
        ANGLE_TRY(commitRegion(context, index, destRegion));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::copySubTexture(const gl::Context *context,
                                            const gl::ImageIndex &index,
                                            const gl::Offset &destOffset,
                                            gl::LevelIndex sourceLevel,
                                            const gl::Box &sourceBox,
                                            bool unpackFlipY,
                                            bool unpackPremultiplyAlpha,
                                            bool unpackUnmultiplyAlpha,
                                            const gl::Texture *source)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_2D);

    if (!isSRGB(index.getLevelIndex()) && canCreateRenderTargetForImage(index))
    {
        ANGLE_TRY(ensureRenderTarget(context));
        ASSERT(isValidLevel(index.getLevelIndex()));
        ANGLE_TRY(updateStorageLevel(context, index.getLevelIndex()));

        const gl::InternalFormat &internalFormatInfo =
            gl::GetSizedInternalFormatInfo(getInternalFormat(index.getLevelIndex()));
        ANGLE_TRY(mRenderer->copyTexture(context, source, sourceLevel.get(), gl::TextureTarget::_2D,
                                         sourceBox, internalFormatInfo.format,
                                         internalFormatInfo.type, destOffset, mTexStorage,
                                         index.getTarget(), index.getLevelIndex(), unpackFlipY,
                                         unpackPremultiplyAlpha, unpackUnmultiplyAlpha));
    }
    else
    {
        gl::ImageIndex sourceImageIndex = gl::ImageIndex::Make2D(sourceLevel.get());
        TextureD3D *sourceD3D           = GetImplAs<TextureD3D>(source);
        ImageD3D *sourceImage           = nullptr;
        ANGLE_TRY(
            sourceD3D->getImageAndSyncFromStorageIfNeeded(context, sourceImageIndex, &sourceImage));

        ImageD3D *destImage = nullptr;
        ANGLE_TRY(getImageAndSyncFromStorageIfNeeded(context, index, &destImage));

        ANGLE_TRY(mRenderer->copyImage(context, destImage, sourceImage, sourceBox, destOffset,
                                       unpackFlipY, unpackPremultiplyAlpha, unpackUnmultiplyAlpha));

        mDirtyImages = true;

        gl::Box destRegion(destOffset.x, destOffset.y, 0, sourceBox.width, sourceBox.height, 1);
        ANGLE_TRY(commitRegion(context, index, destRegion));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::copyCompressedTexture(const gl::Context *context,
                                                   const gl::Texture *source)
{
    gl::TextureTarget sourceTarget = NonCubeTextureTypeToTarget(source->getType());
    GLint sourceLevel              = 0;

    GLint destLevel = 0;

    GLenum sizedInternalFormat =
        source->getFormat(sourceTarget, sourceLevel).info->sizedInternalFormat;
    gl::Extents size(static_cast<int>(source->getWidth(sourceTarget, sourceLevel)),
                     static_cast<int>(source->getHeight(sourceTarget, sourceLevel)), 1);
    ANGLE_TRY(redefineImage(context, destLevel, sizedInternalFormat, size, false));

    ANGLE_TRY(initializeStorage(context, BindFlags()));
    ASSERT(mTexStorage);

    ANGLE_TRY(
        mRenderer->copyCompressedTexture(context, source, sourceLevel, mTexStorage, destLevel));

    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::setStorage(const gl::Context *context,
                                        gl::TextureType type,
                                        size_t levels,
                                        GLenum internalFormat,
                                        const gl::Extents &size)
{
    ASSERT(type == gl::TextureType::_2D && size.depth == 1);

    for (size_t level = 0; level < levels; level++)
    {
        gl::Extents levelSize(std::max(1, size.width >> level), std::max(1, size.height >> level),
                              1);
        ANGLE_TRY(redefineImage(context, level, internalFormat, levelSize, true));
    }

    for (size_t level = levels; level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS; level++)
    {
        ANGLE_TRY(redefineImage(context, level, GL_NONE, gl::Extents(0, 0, 1), true));
    }

    return setStorageImpl(context, type, levels, internalFormat, size);
}

angle::Result TextureD3D_2D::bindTexImage(const gl::Context *context, egl::Surface *surface)
{
    GLenum internalformat = surface->getConfig()->renderTargetFormat;

    ANGLE_TRY(redefineImage(context, 0, internalformat, surface->getSize(), true));

    ANGLE_TRY(releaseTexStorage(context, gl::TexLevelMask()));

    SurfaceD3D *surfaceD3D = GetImplAs<SurfaceD3D>(surface);
    ASSERT(surfaceD3D);

    mTexStorage = mRenderer->createTextureStorage2D(surfaceD3D->getSwapChain(), mState.getLabel());
    // Even though an EGLSurface is an external buffer, it is an EGLSurface (egl::Surface), not an
    // EGLImage (egl::Image), so mEGLImageTarget is set to false.
    mEGLImageTarget = false;

    mDirtyImages = false;
    mImageArray[0]->markClean();

    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::releaseTexImage(const gl::Context *context)
{
    if (mTexStorage)
    {
        ANGLE_TRY(releaseTexStorage(context, gl::TexLevelMask()));
    }

    for (int i = 0; i < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS; i++)
    {
        ANGLE_TRY(redefineImage(context, i, GL_NONE, gl::Extents(0, 0, 1), true));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::setEGLImageTarget(const gl::Context *context,
                                               gl::TextureType type,
                                               egl::Image *image)
{
    EGLImageD3D *eglImaged3d = GetImplAs<EGLImageD3D>(image);

    // Set the properties of the base mip level from the EGL image
    const auto &format = image->getFormat();
    gl::Extents size(static_cast<int>(image->getWidth()), static_cast<int>(image->getHeight()), 1);
    ANGLE_TRY(redefineImage(context, 0, format.info->sizedInternalFormat, size, true));

    // Clear all other images.
    for (size_t level = 1; level < mImageArray.size(); level++)
    {
        ANGLE_TRY(redefineImage(context, level, GL_NONE, gl::Extents(0, 0, 1), true));
    }

    ANGLE_TRY(releaseTexStorage(context, gl::TexLevelMask()));
    mImageArray[0]->markClean();

    // Pass in the RenderTargetD3D here: createTextureStorage can't generate an error.
    RenderTargetD3D *renderTargetD3D = nullptr;
    ANGLE_TRY(eglImaged3d->getRenderTarget(context, &renderTargetD3D));

    mTexStorage =
        mRenderer->createTextureStorageEGLImage(eglImaged3d, renderTargetD3D, mState.getLabel());
    mEGLImageTarget = true;

    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::initMipmapImages(const gl::Context *context)
{
    const GLuint baseLevel = mState.getEffectiveBaseLevel();
    const GLuint maxLevel  = mState.getMipmapMaxLevel();
    // Purge array levels baseLevel + 1 through q and reset them to represent the generated mipmap
    // levels.
    for (GLuint level = baseLevel + 1; level <= maxLevel; level++)
    {
        gl::Extents levelSize(std::max(getLevelZeroWidth() >> level, 1),
                              std::max(getLevelZeroHeight() >> level, 1), 1);

        ANGLE_TRY(redefineImage(context, level, getBaseLevelInternalFormat(), levelSize, false));
    }

    // We should be mip-complete now so generate the storage.
    ANGLE_TRY(initializeStorage(context, BindFlags::RenderTarget()));

    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::getRenderTarget(const gl::Context *context,
                                             const gl::ImageIndex &index,
                                             GLsizei samples,
                                             RenderTargetD3D **outRT)
{
    ASSERT(!index.hasLayer());

    // ensure the underlying texture is created
    ANGLE_TRY(ensureRenderTarget(context));
    ANGLE_TRY(updateStorageLevel(context, index.getLevelIndex()));

    return mTexStorage->getRenderTarget(context, index, outRT);
}

bool TextureD3D_2D::isNonBaseLevelComplete(int level) const
{
    ASSERT(level >= 0 && level <= static_cast<int>(mImageArray.size()) &&
           mImageArray[level] != nullptr);
    ImageD3D *image = mImageArray[level].get();

    if (image->getInternalFormat() != getBaseLevelInternalFormat())
    {
        return false;
    }

    if (image->getWidth() != std::max(1, getLevelZeroWidth() >> level))
    {
        return false;
    }

    if (image->getHeight() != std::max(1, getLevelZeroHeight() >> level))
    {
        return false;
    }

    return true;
}

bool TextureD3D_2D::isImageComplete(const gl::ImageIndex &index) const
{
    return isLevelComplete(index.getLevelIndex());
}

// Constructs a native texture resource from the texture images
angle::Result TextureD3D_2D::initializeStorage(const gl::Context *context, BindFlags bindFlags)
{
    // Only initialize the first time this texture is used as a render target or shader resource
    if (mTexStorage)
    {
        return angle::Result::Continue;
    }

    // do not attempt to create storage for nonexistant data
    if (!isLevelComplete(getBaseLevel()))
    {
        return angle::Result::Continue;
    }

    bindFlags.renderTarget |= IsRenderTargetUsage(mState.getUsage());

    TexStoragePointer storage;
    ANGLE_TRY(createCompleteStorage(context, bindFlags, &storage));

    ANGLE_TRY(setCompleteTexStorage(context, storage.get()));
    storage.release();

    ASSERT(mTexStorage);

    // flush image data to the storage
    ANGLE_TRY(updateStorage(context));

    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::createCompleteStorage(const gl::Context *context,
                                                   BindFlags bindFlags,
                                                   TexStoragePointer *outStorage) const
{
    GLsizei width         = getLevelZeroWidth();
    GLsizei height        = getLevelZeroHeight();
    GLenum internalFormat = getBaseLevelInternalFormat();

    ASSERT(width > 0 && height > 0);

    // use existing storage level count, when previously specified by TexStorage*D
    GLint levels = (mTexStorage ? mTexStorage->getLevelCount() : creationLevels(width, height, 1));

    // TODO(geofflang): Determine if the texture creation succeeded
    *outStorage = {mRenderer->createTextureStorage2D(internalFormat, bindFlags, width, height,
                                                     levels, mState.getLabel()),
                   context};

    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::setCompleteTexStorage(const gl::Context *context,
                                                   TextureStorage *newCompleteTexStorage)
{
    if (newCompleteTexStorage && newCompleteTexStorage->isManaged())
    {
        for (int level = 0; level < newCompleteTexStorage->getLevelCount(); level++)
        {
            ANGLE_TRY(
                mImageArray[level]->setManagedSurface2D(context, newCompleteTexStorage, level));
        }
    }

    gl::TexLevelMask copyImageMask;
    copyImageMask.set();

    ANGLE_TRY(releaseTexStorage(context, copyImageMask));
    mTexStorage = newCompleteTexStorage;
    mTexStorageObserverBinding.bind(mTexStorage);

    mDirtyImages = true;

    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::updateStorage(const gl::Context *context)
{
    if (!mDirtyImages)
    {
        return angle::Result::Continue;
    }

    ASSERT(mTexStorage != nullptr);
    GLint storageLevels = mTexStorage->getLevelCount();
    for (int level = 0; level < storageLevels; level++)
    {
        if (mImageArray[level]->isDirty() && isLevelComplete(level))
        {
            ANGLE_TRY(updateStorageLevel(context, level));
        }
    }

    mDirtyImages = false;
    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::updateStorageLevel(const gl::Context *context, int level)
{
    ASSERT(level <= static_cast<int>(mImageArray.size()) && mImageArray[level] != nullptr);
    ASSERT(isLevelComplete(level));

    if (mImageArray[level]->isDirty())
    {
        gl::ImageIndex index = gl::ImageIndex::Make2D(level);
        gl::Box region(0, 0, 0, getWidth(level), getHeight(level), 1);
        ANGLE_TRY(commitRegion(context, index, region));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_2D::redefineImage(const gl::Context *context,
                                           size_t level,
                                           GLenum internalformat,
                                           const gl::Extents &size,
                                           bool forceRelease)
{
    ASSERT(size.depth == 1);

    if (mTexStorage && level != 0 && mEGLImageTarget)
    {
        // If the storage was from an EGL image, copy it back into local images to preserve it
        // while orphaning
        ANGLE_TRY(
            mImageArray[0]->copyFromTexStorage(context, gl::ImageIndex::Make2D(0), mTexStorage));
    }

    ANGLE_TRY(releaseTexStorageIfMismatched(context, static_cast<GLint>(level), internalformat,
                                            size, mEGLImageTarget /*forceReleaseStorage*/));

    mImageArray[level]->redefine(gl::TextureType::_2D, internalformat, size, forceRelease);
    mDirtyImages = mDirtyImages || mImageArray[level]->isDirty();

    // Can't be an EGL image target after being redefined
    mEGLImageTarget = false;

    return angle::Result::Continue;
}

gl::ImageIndexIterator TextureD3D_2D::imageIterator() const
{
    return gl::ImageIndexIterator::Make2D(0, mTexStorage->getLevelCount());
}

gl::ImageIndex TextureD3D_2D::getImageIndex(GLint mip, GLint /*layer*/) const
{
    // "layer" does not apply to 2D Textures.
    return gl::ImageIndex::Make2D(mip);
}

bool TextureD3D_2D::isValidIndex(const gl::ImageIndex &index) const
{
    return (mTexStorage && index.getType() == gl::TextureType::_2D && index.getLevelIndex() >= 0 &&
            index.getLevelIndex() < mTexStorage->getLevelCount());
}

void TextureD3D_2D::markAllImagesDirty()
{
    for (size_t i = 0; i < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS; i++)
    {
        mImageArray[i]->markDirty();
    }
    mDirtyImages = true;
}

TextureD3D_Cube::TextureD3D_Cube(const gl::TextureState &state, RendererD3D *renderer)
    : TextureD3D(state, renderer)
{
    for (auto &face : mImageArray)
    {
        for (auto &image : face)
        {
            image.reset(renderer->createImage());
        }
    }
}

void TextureD3D_Cube::onDestroy(const gl::Context *context)
{
    // Delete the Images before the TextureStorage. Images might be relying on the TextureStorage
    // for some of their data. If TextureStorage is deleted before the Images, then their data will
    // be wastefully copied back from the GPU before we delete the Images.
    for (auto &face : mImageArray)
    {
        for (auto &image : face)
        {
            image.reset();
        }
    }
    return TextureD3D::onDestroy(context);
}

TextureD3D_Cube::~TextureD3D_Cube() {}

ImageD3D *TextureD3D_Cube::getImage(int level, int layer) const
{
    ASSERT(level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS);
    ASSERT(layer >= 0 && static_cast<size_t>(layer) < gl::kCubeFaceCount);
    return mImageArray[layer][level].get();
}

ImageD3D *TextureD3D_Cube::getImage(const gl::ImageIndex &index) const
{
    ASSERT(index.getLevelIndex() < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS);
    ASSERT(gl::IsCubeMapFaceTarget(index.getTarget()));
    return mImageArray[index.cubeMapFaceIndex()][index.getLevelIndex()].get();
}

GLsizei TextureD3D_Cube::getLayerCount(int level) const
{
    ASSERT(level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS);
    return gl::kCubeFaceCount;
}

GLenum TextureD3D_Cube::getInternalFormat(GLint level, GLint layer) const
{
    if (level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS)
        return mImageArray[layer][level]->getInternalFormat();
    else
        return GL_NONE;
}

bool TextureD3D_Cube::isDepth(GLint level, GLint layer) const
{
    return gl::GetSizedInternalFormatInfo(getInternalFormat(level, layer)).depthBits > 0;
}

bool TextureD3D_Cube::isSRGB(GLint level, GLint layer) const
{
    return gl::GetSizedInternalFormatInfo(getInternalFormat(level, layer)).colorEncoding == GL_SRGB;
}

angle::Result TextureD3D_Cube::setEGLImageTarget(const gl::Context *context,
                                                 gl::TextureType type,
                                                 egl::Image *image)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_Cube::setImage(const gl::Context *context,
                                        const gl::ImageIndex &index,
                                        GLenum internalFormat,
                                        const gl::Extents &size,
                                        GLenum format,
                                        GLenum type,
                                        const gl::PixelUnpackState &unpack,
                                        gl::Buffer *unpackBuffer,
                                        const uint8_t *pixels)
{
    ASSERT(size.depth == 1);

    const gl::InternalFormat &internalFormatInfo = gl::GetInternalFormatInfo(internalFormat, type);
    ANGLE_TRY(redefineImage(context, index.cubeMapFaceIndex(), index.getLevelIndex(),
                            internalFormatInfo.sizedInternalFormat, size, false));

    return setImageImpl(context, index, type, unpack, unpackBuffer, pixels, 0);
}

angle::Result TextureD3D_Cube::setSubImage(const gl::Context *context,
                                           const gl::ImageIndex &index,
                                           const gl::Box &area,
                                           GLenum format,
                                           GLenum type,
                                           const gl::PixelUnpackState &unpack,
                                           gl::Buffer *unpackBuffer,
                                           const uint8_t *pixels)
{
    ASSERT(area.depth == 1 && area.z == 0);
    return TextureD3D::subImage(context, index, area, format, type, unpack, unpackBuffer, pixels,
                                0);
}

angle::Result TextureD3D_Cube::setCompressedImage(const gl::Context *context,
                                                  const gl::ImageIndex &index,
                                                  GLenum internalFormat,
                                                  const gl::Extents &size,
                                                  const gl::PixelUnpackState &unpack,
                                                  size_t imageSize,
                                                  const uint8_t *pixels)
{
    ASSERT(size.depth == 1);

    // compressed formats don't have separate sized internal formats-- we can just use the
    // compressed format directly
    ANGLE_TRY(redefineImage(context, index.cubeMapFaceIndex(), index.getLevelIndex(),
                            internalFormat, size, false));

    return setCompressedImageImpl(context, index, unpack, pixels, 0);
}

angle::Result TextureD3D_Cube::setCompressedSubImage(const gl::Context *context,
                                                     const gl::ImageIndex &index,
                                                     const gl::Box &area,
                                                     GLenum format,
                                                     const gl::PixelUnpackState &unpack,
                                                     size_t imageSize,
                                                     const uint8_t *pixels)
{
    ASSERT(area.depth == 1 && area.z == 0);

    ANGLE_TRY(TextureD3D::subImageCompressed(context, index, area, format, unpack, pixels, 0));
    return commitRegion(context, index, area);
}

angle::Result TextureD3D_Cube::copyImage(const gl::Context *context,
                                         const gl::ImageIndex &index,
                                         const gl::Rectangle &sourceArea,
                                         GLenum internalFormat,
                                         gl::Framebuffer *source)
{
    GLint faceIndex = index.cubeMapFaceIndex();
    const gl::InternalFormat &internalFormatInfo =
        gl::GetInternalFormatInfo(internalFormat, GL_UNSIGNED_BYTE);

    gl::Extents size(sourceArea.width, sourceArea.height, 1);

    gl::Extents fbSize = source->getReadColorAttachment()->getSize();

    // Does the read area extend beyond the framebuffer?
    bool outside = sourceArea.x < 0 || sourceArea.y < 0 ||
                   sourceArea.x + sourceArea.width > fbSize.width ||
                   sourceArea.y + sourceArea.height > fbSize.height;

    const gl::FramebufferAttachment *readAttachment = source->getReadColorAttachment();
    const bool isSelfCopy = readAttachment && readAttachment->type() == GL_TEXTURE &&
                            GetImplAs<TextureD3D>(readAttachment->getTexture()) == this &&
                            readAttachment->mipLevel() == index.getLevelIndex() &&
                            readAttachment->cubeMapFace() == index.getTarget();

    if (isSelfCopy)
    {
        return handleCopyImageSelfCopyRedefine(
            context, gl::TextureType::CubeMap, internalFormatInfo.sizedInternalFormat, sourceArea,
            size, outside, index, source, [&](const gl::Extents &extents) {
                return redefineImage(context, faceIndex, index.getLevelIndex(),
                                     internalFormatInfo.sizedInternalFormat, extents, false);
            });
    }

    ANGLE_TRY(redefineImage(context, faceIndex, index.getLevelIndex(),
                            internalFormatInfo.sizedInternalFormat, size, false));

    // WebGL requires that pixels that would be outside the framebuffer are treated as zero values,
    // so clear the mip level to 0 prior to making the copy if any pixel would be sampled outside.
    // Same thing for robust resource init.
    if (outside && (context->isWebGL() || context->isRobustResourceInitEnabled()))
    {
        ANGLE_TRY(initializeContents(context, GL_NONE, index));
    }

    gl::Rectangle clippedArea;
    if (!ClipRectangle(sourceArea, gl::Rectangle(0, 0, fbSize.width, fbSize.height), &clippedArea))
    {
        // Empty source area, nothing to do.
        return angle::Result::Continue;
    }

    gl::Offset destOffset(clippedArea.x - sourceArea.x, clippedArea.y - sourceArea.y, 0);

    if (!canCreateRenderTargetForImage(index))
    {
        ANGLE_TRY(
            copyImageFromFramebufferToStaging(context, index, destOffset, clippedArea, source));
    }
    else
    {
        ANGLE_TRY(ensureRenderTarget(context));

        ASSERT(size.width == size.height);

        if (size.width > 0 && isValidLevel(index.getLevelIndex()))
        {
            ANGLE_TRY(updateStorageFaceLevel(context, faceIndex, index.getLevelIndex()));
            ANGLE_TRY(mRenderer->copyImageCube(context, source, clippedArea, internalFormat,
                                               destOffset, mTexStorage, index.getTarget(),
                                               index.getLevelIndex()));
        }
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_Cube::copySubImage(const gl::Context *context,
                                            const gl::ImageIndex &index,
                                            const gl::Offset &destOffset,
                                            const gl::Rectangle &sourceArea,
                                            gl::Framebuffer *source)
{
    gl::Extents fbSize = source->getReadColorAttachment()->getSize();
    gl::Rectangle clippedArea;
    if (!ClipRectangle(sourceArea, gl::Rectangle(0, 0, fbSize.width, fbSize.height), &clippedArea))
    {
        return angle::Result::Continue;
    }
    const gl::Offset clippedOffset(destOffset.x + clippedArea.x - sourceArea.x,
                                   destOffset.y + clippedArea.y - sourceArea.y, 0);

    GLint faceIndex = index.cubeMapFaceIndex();

    if (!canCreateRenderTargetForImage(index))
    {
        ANGLE_TRY(
            copyImageFromFramebufferToStaging(context, index, clippedOffset, clippedArea, source));
    }
    else
    {
        ANGLE_TRY(ensureRenderTarget(context));
        if (isValidLevel(index.getLevelIndex()))
        {
            ANGLE_TRY(updateStorageFaceLevel(context, faceIndex, index.getLevelIndex()));
            ANGLE_TRY(mRenderer->copyImageCube(
                context, source, clippedArea, gl::GetUnsizedFormat(getBaseLevelInternalFormat()),
                clippedOffset, mTexStorage, index.getTarget(), index.getLevelIndex()));
        }
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_Cube::copyTexture(const gl::Context *context,
                                           const gl::ImageIndex &index,
                                           GLenum internalFormat,
                                           GLenum type,
                                           gl::LevelIndex sourceLevel,
                                           bool unpackFlipY,
                                           bool unpackPremultiplyAlpha,
                                           bool unpackUnmultiplyAlpha,
                                           const gl::Texture *source)
{
    ASSERT(gl::IsCubeMapFaceTarget(index.getTarget()));

    gl::TextureTarget sourceTarget = NonCubeTextureTypeToTarget(source->getType());

    GLint faceIndex = index.cubeMapFaceIndex();

    const gl::InternalFormat &internalFormatInfo = gl::GetInternalFormatInfo(internalFormat, type);
    gl::Extents size(static_cast<int>(source->getWidth(sourceTarget, sourceLevel.get())),
                     static_cast<int>(source->getHeight(sourceTarget, sourceLevel.get())), 1);
    ANGLE_TRY(redefineImage(context, faceIndex, index.getLevelIndex(),
                            internalFormatInfo.sizedInternalFormat, size, false));

    gl::Box sourceBox(0, 0, 0, size.width, size.height, 1);
    gl::Offset destOffset(0, 0, 0);

    if (!isSRGB(index.getLevelIndex(), faceIndex) && canCreateRenderTargetForImage(index))
    {

        ANGLE_TRY(ensureRenderTarget(context));
        ASSERT(isValidLevel(index.getLevelIndex()));
        ANGLE_TRY(updateStorageFaceLevel(context, faceIndex, index.getLevelIndex()));

        ANGLE_TRY(mRenderer->copyTexture(context, source, sourceLevel.get(), gl::TextureTarget::_2D,
                                         sourceBox, internalFormatInfo.format,
                                         internalFormatInfo.type, destOffset, mTexStorage,
                                         index.getTarget(), index.getLevelIndex(), unpackFlipY,
                                         unpackPremultiplyAlpha, unpackUnmultiplyAlpha));
    }
    else
    {
        gl::ImageIndex sourceImageIndex = gl::ImageIndex::Make2D(sourceLevel.get());
        TextureD3D *sourceD3D           = GetImplAs<TextureD3D>(source);
        ImageD3D *sourceImage           = nullptr;
        ANGLE_TRY(
            sourceD3D->getImageAndSyncFromStorageIfNeeded(context, sourceImageIndex, &sourceImage));

        ImageD3D *destImage = nullptr;
        ANGLE_TRY(getImageAndSyncFromStorageIfNeeded(context, index, &destImage));

        ANGLE_TRY(mRenderer->copyImage(context, destImage, sourceImage, sourceBox, destOffset,
                                       unpackFlipY, unpackPremultiplyAlpha, unpackUnmultiplyAlpha));

        mDirtyImages = true;

        gl::Box destRegion(destOffset, size);
        ANGLE_TRY(commitRegion(context, index, destRegion));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_Cube::copySubTexture(const gl::Context *context,
                                              const gl::ImageIndex &index,
                                              const gl::Offset &destOffset,
                                              gl::LevelIndex sourceLevel,
                                              const gl::Box &sourceBox,
                                              bool unpackFlipY,
                                              bool unpackPremultiplyAlpha,
                                              bool unpackUnmultiplyAlpha,
                                              const gl::Texture *source)
{
    ASSERT(gl::IsCubeMapFaceTarget(index.getTarget()));

    GLint faceIndex = index.cubeMapFaceIndex();

    if (!isSRGB(index.getLevelIndex(), faceIndex) && canCreateRenderTargetForImage(index))
    {
        ANGLE_TRY(ensureRenderTarget(context));
        ASSERT(isValidLevel(index.getLevelIndex()));
        ANGLE_TRY(updateStorageFaceLevel(context, faceIndex, index.getLevelIndex()));

        const gl::InternalFormat &internalFormatInfo =
            gl::GetSizedInternalFormatInfo(getInternalFormat(index.getLevelIndex(), faceIndex));
        ANGLE_TRY(mRenderer->copyTexture(context, source, sourceLevel.get(), gl::TextureTarget::_2D,
                                         sourceBox, internalFormatInfo.format,
                                         internalFormatInfo.type, destOffset, mTexStorage,
                                         index.getTarget(), index.getLevelIndex(), unpackFlipY,
                                         unpackPremultiplyAlpha, unpackUnmultiplyAlpha));
    }
    else
    {
        gl::ImageIndex sourceImageIndex = gl::ImageIndex::Make2D(sourceLevel.get());
        TextureD3D *sourceD3D           = GetImplAs<TextureD3D>(source);
        ImageD3D *sourceImage           = nullptr;
        ANGLE_TRY(
            sourceD3D->getImageAndSyncFromStorageIfNeeded(context, sourceImageIndex, &sourceImage));

        ImageD3D *destImage = nullptr;
        ANGLE_TRY(getImageAndSyncFromStorageIfNeeded(context, index, &destImage));

        ANGLE_TRY(mRenderer->copyImage(context, destImage, sourceImage, sourceBox, destOffset,
                                       unpackFlipY, unpackPremultiplyAlpha, unpackUnmultiplyAlpha));

        mDirtyImages = true;

        gl::Box destRegion(destOffset.x, destOffset.y, 0, sourceBox.width, sourceBox.height, 1);
        ANGLE_TRY(commitRegion(context, index, destRegion));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_Cube::setStorage(const gl::Context *context,
                                          gl::TextureType type,
                                          size_t levels,
                                          GLenum internalFormat,
                                          const gl::Extents &size)
{
    ASSERT(size.width == size.height);
    ASSERT(size.depth == 1);

    for (size_t level = 0; level < levels; level++)
    {
        GLsizei mipSize = std::max(1, size.width >> level);
        for (size_t faceIndex = 0; faceIndex < gl::kCubeFaceCount; faceIndex++)
        {
            mImageArray[faceIndex][level]->redefine(gl::TextureType::CubeMap, internalFormat,
                                                    gl::Extents(mipSize, mipSize, 1), true);
        }
    }

    for (size_t level = levels; level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS; level++)
    {
        for (size_t faceIndex = 0; faceIndex < gl::kCubeFaceCount; faceIndex++)
        {
            mImageArray[faceIndex][level]->redefine(gl::TextureType::CubeMap, GL_NONE,
                                                    gl::Extents(0, 0, 0), true);
        }
    }

    return setStorageImpl(context, type, levels, internalFormat, size);
}

// Tests for cube texture completeness. [OpenGL ES 2.0.24] section 3.7.10 page 81.
bool TextureD3D_Cube::isCubeComplete() const
{
    int baseWidth     = getBaseLevelWidth();
    int baseHeight    = getBaseLevelHeight();
    GLenum baseFormat = getBaseLevelInternalFormat();

    if (baseWidth <= 0 || baseWidth != baseHeight)
    {
        return false;
    }

    for (size_t faceIndex = 1; faceIndex < gl::kCubeFaceCount; faceIndex++)
    {
        const ImageD3D &faceBaseImage = *mImageArray[faceIndex][getBaseLevel()];

        if (faceBaseImage.getWidth() != baseWidth || faceBaseImage.getHeight() != baseHeight ||
            faceBaseImage.getInternalFormat() != baseFormat)
        {
            return false;
        }
    }

    return true;
}

angle::Result TextureD3D_Cube::bindTexImage(const gl::Context *context, egl::Surface *surface)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_Cube::releaseTexImage(const gl::Context *context)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_Cube::initMipmapImages(const gl::Context *context)
{
    const GLuint baseLevel = mState.getEffectiveBaseLevel();
    const GLuint maxLevel  = mState.getMipmapMaxLevel();
    // Purge array levels baseLevel + 1 through q and reset them to represent the generated mipmap
    // levels.
    for (int faceIndex = 0; faceIndex < static_cast<int>(gl::kCubeFaceCount); faceIndex++)
    {
        for (GLuint level = baseLevel + 1; level <= maxLevel; level++)
        {
            int faceLevelSize =
                (std::max(mImageArray[faceIndex][baseLevel]->getWidth() >> (level - baseLevel), 1));
            ANGLE_TRY(redefineImage(context, faceIndex, level,
                                    mImageArray[faceIndex][baseLevel]->getInternalFormat(),
                                    gl::Extents(faceLevelSize, faceLevelSize, 1), false));
        }
    }

    // We should be mip-complete now so generate the storage.
    ANGLE_TRY(initializeStorage(context, BindFlags::RenderTarget()));

    return angle::Result::Continue;
}

angle::Result TextureD3D_Cube::getRenderTarget(const gl::Context *context,
                                               const gl::ImageIndex &index,
                                               GLsizei samples,
                                               RenderTargetD3D **outRT)
{
    ASSERT(gl::IsCubeMapFaceTarget(index.getTarget()));

    // ensure the underlying texture is created
    ANGLE_TRY(ensureRenderTarget(context));
    ANGLE_TRY(updateStorageFaceLevel(context, index.cubeMapFaceIndex(), index.getLevelIndex()));

    return mTexStorage->getRenderTarget(context, index, outRT);
}

angle::Result TextureD3D_Cube::initializeStorage(const gl::Context *context, BindFlags bindFlags)
{
    // Only initialize the first time this texture is used as a render target or shader resource
    if (mTexStorage)
    {
        return angle::Result::Continue;
    }

    // do not attempt to create storage for nonexistant data
    if (!isFaceLevelComplete(0, getBaseLevel()))
    {
        return angle::Result::Continue;
    }

    bindFlags.renderTarget |= IsRenderTargetUsage(mState.getUsage());

    TexStoragePointer storage;
    ANGLE_TRY(createCompleteStorage(context, bindFlags, &storage));

    ANGLE_TRY(setCompleteTexStorage(context, storage.get()));
    storage.release();

    ASSERT(mTexStorage);

    // flush image data to the storage
    ANGLE_TRY(updateStorage(context));

    return angle::Result::Continue;
}

angle::Result TextureD3D_Cube::createCompleteStorage(const gl::Context *context,
                                                     BindFlags bindFlags,
                                                     TexStoragePointer *outStorage) const
{
    GLsizei size = getLevelZeroWidth();

    ASSERT(size > 0);

    // use existing storage level count, when previously specified by TexStorage*D
    GLint levels = (mTexStorage ? mTexStorage->getLevelCount() : creationLevels(size, size, 1));

    // TODO (geofflang): detect if storage creation succeeded
    *outStorage = {mRenderer->createTextureStorageCube(getBaseLevelInternalFormat(), bindFlags,
                                                       size, levels, mState.getLabel()),
                   context};

    return angle::Result::Continue;
}

angle::Result TextureD3D_Cube::setCompleteTexStorage(const gl::Context *context,
                                                     TextureStorage *newCompleteTexStorage)
{
    if (newCompleteTexStorage && newCompleteTexStorage->isManaged())
    {
        for (int faceIndex = 0; faceIndex < static_cast<int>(gl::kCubeFaceCount); faceIndex++)
        {
            for (int level = 0; level < newCompleteTexStorage->getLevelCount(); level++)
            {
                ANGLE_TRY(mImageArray[faceIndex][level]->setManagedSurfaceCube(
                    context, newCompleteTexStorage, faceIndex, level));
            }
        }
    }

    gl::TexLevelMask copyImageMask;
    copyImageMask.set();

    ANGLE_TRY(releaseTexStorage(context, copyImageMask));
    mTexStorage = newCompleteTexStorage;
    mTexStorageObserverBinding.bind(mTexStorage);

    mDirtyImages = true;
    return angle::Result::Continue;
}

angle::Result TextureD3D_Cube::updateStorage(const gl::Context *context)
{
    if (!mDirtyImages)
    {
        return angle::Result::Continue;
    }

    ASSERT(mTexStorage != nullptr);
    GLint storageLevels = mTexStorage->getLevelCount();
    for (int face = 0; face < static_cast<int>(gl::kCubeFaceCount); face++)
    {
        for (int level = 0; level < storageLevels; level++)
        {
            if (mImageArray[face][level]->isDirty() && isFaceLevelComplete(face, level))
            {
                ANGLE_TRY(updateStorageFaceLevel(context, face, level));
            }
        }
    }

    mDirtyImages = false;
    return angle::Result::Continue;
}

bool TextureD3D_Cube::isFaceLevelComplete(int faceIndex, int level) const
{
    if (getBaseLevel() >= gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS)
    {
        return false;
    }
    ASSERT(level >= 0 && static_cast<size_t>(faceIndex) < gl::kCubeFaceCount &&
           level < static_cast<int>(mImageArray[faceIndex].size()) &&
           mImageArray[faceIndex][level] != nullptr);

    if (isImmutable())
    {
        return isValidLevel(level);
    }

    int levelZeroSize = getLevelZeroWidth();
    if (levelZeroSize <= 0 || level >= creationLevels(levelZeroSize, levelZeroSize, 1))
    {
        return false;
    }

    // Check that non-zero levels are consistent with the base level.
    const ImageD3D *faceLevelImage = mImageArray[faceIndex][level].get();

    if (faceLevelImage->getInternalFormat() != getBaseLevelInternalFormat())
    {
        return false;
    }

    if (faceLevelImage->getWidth() != std::max(1, levelZeroSize >> level))
    {
        return false;
    }

    return true;
}

bool TextureD3D_Cube::isImageComplete(const gl::ImageIndex &index) const
{
    return isFaceLevelComplete(index.cubeMapFaceIndex(), index.getLevelIndex());
}

angle::Result TextureD3D_Cube::updateStorageFaceLevel(const gl::Context *context,
                                                      int faceIndex,
                                                      int level)
{
    ASSERT(level >= 0 && static_cast<size_t>(faceIndex) < gl::kCubeFaceCount &&
           level < static_cast<int>(mImageArray[faceIndex].size()) &&
           mImageArray[faceIndex][level] != nullptr);
    ImageD3D *image = mImageArray[faceIndex][level].get();

    if (image->isDirty())
    {
        gl::TextureTarget faceTarget = gl::CubeFaceIndexToTextureTarget(faceIndex);
        gl::ImageIndex index         = gl::ImageIndex::MakeCubeMapFace(faceTarget, level);
        gl::Box region(0, 0, 0, image->getWidth(), image->getHeight(), 1);
        ANGLE_TRY(commitRegion(context, index, region));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_Cube::redefineImage(const gl::Context *context,
                                             int faceIndex,
                                             GLint level,
                                             GLenum internalformat,
                                             const gl::Extents &size,
                                             bool forceRelease)
{
    ANGLE_TRY(releaseTexStorageIfMismatched(context, faceIndex, level, internalformat, size,
                                            false /*forceReleaseStorage*/));

    mImageArray[faceIndex][level]->redefine(gl::TextureType::CubeMap, internalformat, size,
                                            forceRelease);
    mDirtyImages = mDirtyImages || mImageArray[faceIndex][level]->isDirty();

    return angle::Result::Continue;
}

gl::ImageIndexIterator TextureD3D_Cube::imageIterator() const
{
    return gl::ImageIndexIterator::MakeCube(0, mTexStorage->getLevelCount());
}

gl::ImageIndex TextureD3D_Cube::getImageIndex(GLint mip, GLint layer) const
{
    // The "layer" of the image index corresponds to the cube face
    return gl::ImageIndex::MakeCubeMapFace(gl::CubeFaceIndexToTextureTarget(layer), mip);
}

bool TextureD3D_Cube::isValidIndex(const gl::ImageIndex &index) const
{
    return (mTexStorage && index.getType() == gl::TextureType::CubeMap &&
            gl::IsCubeMapFaceTarget(index.getTarget()) && index.getLevelIndex() >= 0 &&
            index.getLevelIndex() < mTexStorage->getLevelCount());
}

void TextureD3D_Cube::markAllImagesDirty()
{
    for (int dirtyLevel = 0; dirtyLevel < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS; dirtyLevel++)
    {
        for (size_t dirtyFace = 0; dirtyFace < gl::kCubeFaceCount; dirtyFace++)
        {
            mImageArray[dirtyFace][dirtyLevel]->markDirty();
        }
    }
    mDirtyImages = true;
}

TextureD3D_3D::TextureD3D_3D(const gl::TextureState &state, RendererD3D *renderer)
    : TextureD3D(state, renderer)
{
    for (int i = 0; i < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS; ++i)
    {
        mImageArray[i].reset(renderer->createImage());
    }
}

void TextureD3D_3D::onDestroy(const gl::Context *context)
{
    // Delete the Images before the TextureStorage. Images might be relying on the TextureStorage
    // for some of their data. If TextureStorage is deleted before the Images, then their data will
    // be wastefully copied back from the GPU before we delete the Images.
    for (auto &image : mImageArray)
    {
        image.reset();
    }
    return TextureD3D::onDestroy(context);
}

TextureD3D_3D::~TextureD3D_3D() {}

ImageD3D *TextureD3D_3D::getImage(int level, int layer) const
{
    ASSERT(level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS);
    ASSERT(layer == 0);
    return mImageArray[level].get();
}

ImageD3D *TextureD3D_3D::getImage(const gl::ImageIndex &index) const
{
    ASSERT(index.getLevelIndex() < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS);
    ASSERT(!index.hasLayer());
    ASSERT(index.getType() == gl::TextureType::_3D);
    return mImageArray[index.getLevelIndex()].get();
}

GLsizei TextureD3D_3D::getLayerCount(int level) const
{
    ASSERT(level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS);
    return 1;
}

GLsizei TextureD3D_3D::getWidth(GLint level) const
{
    if (level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS)
        return mImageArray[level]->getWidth();
    else
        return 0;
}

GLsizei TextureD3D_3D::getHeight(GLint level) const
{
    if (level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS)
        return mImageArray[level]->getHeight();
    else
        return 0;
}

GLsizei TextureD3D_3D::getDepth(GLint level) const
{
    if (level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS)
        return mImageArray[level]->getDepth();
    else
        return 0;
}

GLenum TextureD3D_3D::getInternalFormat(GLint level) const
{
    if (level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS)
        return mImageArray[level]->getInternalFormat();
    else
        return GL_NONE;
}

bool TextureD3D_3D::isDepth(GLint level) const
{
    return gl::GetSizedInternalFormatInfo(getInternalFormat(level)).depthBits > 0;
}

bool TextureD3D_3D::isSRGB(GLint level) const
{
    return gl::GetSizedInternalFormatInfo(getInternalFormat(level)).colorEncoding == GL_SRGB;
}

angle::Result TextureD3D_3D::setEGLImageTarget(const gl::Context *context,
                                               gl::TextureType type,
                                               egl::Image *image)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_3D::setImage(const gl::Context *context,
                                      const gl::ImageIndex &index,
                                      GLenum internalFormat,
                                      const gl::Extents &size,
                                      GLenum format,
                                      GLenum type,
                                      const gl::PixelUnpackState &unpack,
                                      gl::Buffer *unpackBuffer,
                                      const uint8_t *pixels)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_3D);
    const gl::InternalFormat &internalFormatInfo = gl::GetInternalFormatInfo(internalFormat, type);

    ANGLE_TRY(redefineImage(context, index.getLevelIndex(), internalFormatInfo.sizedInternalFormat,
                            size, false));

    bool fastUnpacked = false;

    // Attempt a fast gpu copy of the pixel data to the surface if the app bound an unpack buffer
    if (isFastUnpackable(unpackBuffer, unpack, internalFormatInfo.sizedInternalFormat) &&
        !size.empty() && isLevelComplete(index.getLevelIndex()))
    {
        // Will try to create RT storage if it does not exist
        RenderTargetD3D *destRenderTarget = nullptr;
        ANGLE_TRY(getRenderTarget(context, index, 0, &destRenderTarget));

        gl::Box destArea(0, 0, 0, getWidth(index.getLevelIndex()), getHeight(index.getLevelIndex()),
                         getDepth(index.getLevelIndex()));

        ANGLE_TRY(fastUnpackPixels(context, unpack, unpackBuffer, pixels, destArea,
                                   internalFormatInfo.sizedInternalFormat, type, destRenderTarget));

        // Ensure we don't overwrite our newly initialized data
        mImageArray[index.getLevelIndex()]->markClean();

        fastUnpacked = true;
    }

    if (!fastUnpacked)
    {
        ANGLE_TRY(setImageImpl(context, index, type, unpack, unpackBuffer, pixels, 0));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_3D::setSubImage(const gl::Context *context,
                                         const gl::ImageIndex &index,
                                         const gl::Box &area,
                                         GLenum format,
                                         GLenum type,
                                         const gl::PixelUnpackState &unpack,
                                         gl::Buffer *unpackBuffer,
                                         const uint8_t *pixels)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_3D);

    // Attempt a fast gpu copy of the pixel data to the surface if the app bound an unpack buffer
    GLenum mipFormat = getInternalFormat(index.getLevelIndex());
    if (isFastUnpackable(unpackBuffer, unpack, mipFormat) && isLevelComplete(index.getLevelIndex()))
    {
        RenderTargetD3D *destRenderTarget = nullptr;
        ANGLE_TRY(getRenderTarget(context, index, 0, &destRenderTarget));
        ASSERT(!mImageArray[index.getLevelIndex()]->isDirty());

        return fastUnpackPixels(context, unpack, unpackBuffer, pixels, area, mipFormat, type,
                                destRenderTarget);
    }
    else
    {
        return TextureD3D::subImage(context, index, area, format, type, unpack, unpackBuffer,
                                    pixels, 0);
    }
}

angle::Result TextureD3D_3D::setCompressedImage(const gl::Context *context,
                                                const gl::ImageIndex &index,
                                                GLenum internalFormat,
                                                const gl::Extents &size,
                                                const gl::PixelUnpackState &unpack,
                                                size_t imageSize,
                                                const uint8_t *pixels)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_3D);

    // compressed formats don't have separate sized internal formats-- we can just use the
    // compressed format directly
    ANGLE_TRY(redefineImage(context, index.getLevelIndex(), internalFormat, size, false));

    return setCompressedImageImpl(context, index, unpack, pixels, 0);
}

angle::Result TextureD3D_3D::setCompressedSubImage(const gl::Context *context,
                                                   const gl::ImageIndex &index,
                                                   const gl::Box &area,
                                                   GLenum format,
                                                   const gl::PixelUnpackState &unpack,
                                                   size_t imageSize,
                                                   const uint8_t *pixels)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_3D);

    ANGLE_TRY(TextureD3D::subImageCompressed(context, index, area, format, unpack, pixels, 0));
    return commitRegion(context, index, area);
}

angle::Result TextureD3D_3D::copyImage(const gl::Context *context,
                                       const gl::ImageIndex &index,
                                       const gl::Rectangle &sourceArea,
                                       GLenum internalFormat,
                                       gl::Framebuffer *source)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_3D::copySubImage(const gl::Context *context,
                                          const gl::ImageIndex &index,
                                          const gl::Offset &destOffset,
                                          const gl::Rectangle &sourceArea,
                                          gl::Framebuffer *source)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_3D);

    gl::Extents fbSize = source->getReadColorAttachment()->getSize();
    gl::Rectangle clippedSourceArea;
    if (!ClipRectangle(sourceArea, gl::Rectangle(0, 0, fbSize.width, fbSize.height),
                       &clippedSourceArea))
    {
        return angle::Result::Continue;
    }
    const gl::Offset clippedDestOffset(destOffset.x + clippedSourceArea.x - sourceArea.x,
                                       destOffset.y + clippedSourceArea.y - sourceArea.y,
                                       destOffset.z);

    // Currently, copying directly to the storage is not possible because it's not possible to
    // create an SRV from a single layer of a 3D texture.  Instead, make sure the image is up to
    // date before the copy and then copy back to the storage afterwards if needed.
    // TODO: Investigate 3D blits in D3D11.

    gl::ImageIndex levelIndex = gl::ImageIndex::Make3D(index.getLevelIndex());
    ImageD3D *image           = nullptr;
    ANGLE_TRY(getImageAndSyncFromStorageIfNeeded(context, levelIndex, &image));
    ANGLE_TRY(image->copyFromFramebuffer(context, clippedDestOffset, clippedSourceArea, source));
    mDirtyImages = true;
    onStateChange(angle::SubjectMessage::DirtyBitsFlagged);

    if (mTexStorage && isLevelComplete(index.getLevelIndex()))
    {
        ANGLE_TRY(updateStorageLevel(context, index.getLevelIndex()));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_3D::copyTexture(const gl::Context *context,
                                         const gl::ImageIndex &index,
                                         GLenum internalFormat,
                                         GLenum type,
                                         gl::LevelIndex sourceLevel,
                                         bool unpackFlipY,
                                         bool unpackPremultiplyAlpha,
                                         bool unpackUnmultiplyAlpha,
                                         const gl::Texture *source)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_3D);

    gl::TextureType sourceType = source->getType();

    const gl::InternalFormat &internalFormatInfo = gl::GetInternalFormatInfo(internalFormat, type);
    gl::Extents size(static_cast<int>(source->getWidth(NonCubeTextureTypeToTarget(sourceType),
                                                       sourceLevel.get())),
                     static_cast<int>(source->getHeight(NonCubeTextureTypeToTarget(sourceType),
                                                        sourceLevel.get())),
                     static_cast<int>(source->getDepth(NonCubeTextureTypeToTarget(sourceType),
                                                       sourceLevel.get())));

    ANGLE_TRY(redefineImage(context, index.getLevelIndex(), internalFormatInfo.sizedInternalFormat,
                            size, false));

    gl::Box sourceBox(0, 0, 0, size.width, size.height, size.depth);
    gl::Offset destOffset(0, 0, 0);
    gl::ImageIndex destIndex = gl::ImageIndex::Make3D(static_cast<GLint>(index.getLevelIndex()));

    if (!isSRGB(index.getLevelIndex()) && canCreateRenderTargetForImage(destIndex))
    {
        ANGLE_TRY(ensureRenderTarget(context));
        ASSERT(isValidLevel(index.getLevelIndex()));
        ANGLE_TRY(updateStorageLevel(context, index.getLevelIndex()));

        ANGLE_TRY(mRenderer->copyTexture(context, source, sourceLevel.get(), gl::TextureTarget::_3D,
                                         sourceBox, internalFormatInfo.format,
                                         internalFormatInfo.type, destOffset, mTexStorage,
                                         index.getTarget(), index.getLevelIndex(), unpackFlipY,
                                         unpackPremultiplyAlpha, unpackUnmultiplyAlpha));
    }
    else
    {
        gl::ImageIndex sourceIndex = gl::ImageIndex::Make3D(sourceLevel.get());
        ImageD3D *sourceImage      = nullptr;
        ImageD3D *destImage        = nullptr;
        TextureD3D *sourceD3D      = GetImplAs<TextureD3D>(source);

        ANGLE_TRY(getImageAndSyncFromStorageIfNeeded(context, destIndex, &destImage));
        ANGLE_TRY(
            sourceD3D->getImageAndSyncFromStorageIfNeeded(context, sourceIndex, &sourceImage));

        ANGLE_TRY(mRenderer->copyImage(context, destImage, sourceImage, sourceBox, destOffset,
                                       unpackFlipY, unpackPremultiplyAlpha, unpackUnmultiplyAlpha));

        mDirtyImages = true;

        gl::Box destRegion(0, 0, 0, sourceBox.width, sourceBox.height, sourceBox.depth);
        ANGLE_TRY(commitRegion(context, destIndex, destRegion));
    }

    return angle::Result::Continue;
}
angle::Result TextureD3D_3D::copySubTexture(const gl::Context *context,
                                            const gl::ImageIndex &index,
                                            const gl::Offset &destOffset,
                                            gl::LevelIndex sourceLevel,
                                            const gl::Box &sourceBox,
                                            bool unpackFlipY,
                                            bool unpackPremultiplyAlpha,
                                            bool unpackUnmultiplyAlpha,
                                            const gl::Texture *source)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_3D);

    gl::ImageIndex destIndex = gl::ImageIndex::Make3D(static_cast<GLint>(index.getLevelIndex()));

    if (!isSRGB(index.getLevelIndex()) && canCreateRenderTargetForImage(destIndex))
    {
        ANGLE_TRY(ensureRenderTarget(context));
        ASSERT(isValidLevel(index.getLevelIndex()));
        ANGLE_TRY(updateStorageLevel(context, index.getLevelIndex()));

        const gl::InternalFormat &internalFormatInfo =
            gl::GetSizedInternalFormatInfo(getInternalFormat(index.getLevelIndex()));
        ANGLE_TRY(mRenderer->copyTexture(context, source, sourceLevel.get(), gl::TextureTarget::_3D,
                                         sourceBox, internalFormatInfo.format,
                                         internalFormatInfo.type, destOffset, mTexStorage,
                                         index.getTarget(), index.getLevelIndex(), unpackFlipY,
                                         unpackPremultiplyAlpha, unpackUnmultiplyAlpha));
    }
    else
    {
        gl::ImageIndex sourceImageIndex = gl::ImageIndex::Make3D(sourceLevel.get());
        TextureD3D *sourceD3D           = GetImplAs<TextureD3D>(source);
        ImageD3D *sourceImage           = nullptr;
        ANGLE_TRY(
            sourceD3D->getImageAndSyncFromStorageIfNeeded(context, sourceImageIndex, &sourceImage));

        ImageD3D *destImage = nullptr;
        ANGLE_TRY(getImageAndSyncFromStorageIfNeeded(context, destIndex, &destImage));

        ANGLE_TRY(mRenderer->copyImage(context, destImage, sourceImage, sourceBox, destOffset,
                                       unpackFlipY, unpackPremultiplyAlpha, unpackUnmultiplyAlpha));

        mDirtyImages = true;

        gl::Box destRegion(destOffset.x, destOffset.y, destOffset.z, sourceBox.width,
                           sourceBox.height, sourceBox.depth);
        ANGLE_TRY(commitRegion(context, destIndex, destRegion));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_3D::setStorage(const gl::Context *context,
                                        gl::TextureType type,
                                        size_t levels,
                                        GLenum internalFormat,
                                        const gl::Extents &size)
{
    ASSERT(type == gl::TextureType::_3D);

    for (size_t level = 0; level < levels; level++)
    {
        gl::Extents levelSize(std::max(1, size.width >> level), std::max(1, size.height >> level),
                              std::max(1, size.depth >> level));
        mImageArray[level]->redefine(gl::TextureType::_3D, internalFormat, levelSize, true);
    }

    for (size_t level = levels; level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS; level++)
    {
        mImageArray[level]->redefine(gl::TextureType::_3D, GL_NONE, gl::Extents(0, 0, 0), true);
    }

    return setStorageImpl(context, type, levels, internalFormat, size);
}

angle::Result TextureD3D_3D::bindTexImage(const gl::Context *context, egl::Surface *surface)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_3D::releaseTexImage(const gl::Context *context)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_3D::initMipmapImages(const gl::Context *context)
{
    const GLuint baseLevel = mState.getEffectiveBaseLevel();
    const GLuint maxLevel  = mState.getMipmapMaxLevel();
    // Purge array levels baseLevel + 1 through q and reset them to represent the generated mipmap
    // levels.
    for (GLuint level = baseLevel + 1; level <= maxLevel; level++)
    {
        gl::Extents levelSize(std::max(getLevelZeroWidth() >> level, 1),
                              std::max(getLevelZeroHeight() >> level, 1),
                              std::max(getLevelZeroDepth() >> level, 1));
        ANGLE_TRY(redefineImage(context, level, getBaseLevelInternalFormat(), levelSize, false));
    }

    // We should be mip-complete now so generate the storage.
    ANGLE_TRY(initializeStorage(context, BindFlags::RenderTarget()));

    return angle::Result::Continue;
}

angle::Result TextureD3D_3D::getRenderTarget(const gl::Context *context,
                                             const gl::ImageIndex &index,
                                             GLsizei samples,
                                             RenderTargetD3D **outRT)
{
    // ensure the underlying texture is created
    ANGLE_TRY(ensureRenderTarget(context));

    if (index.hasLayer())
    {
        ANGLE_TRY(updateStorage(context));
    }
    else
    {
        ANGLE_TRY(updateStorageLevel(context, index.getLevelIndex()));
    }

    return mTexStorage->getRenderTarget(context, index, outRT);
}

angle::Result TextureD3D_3D::initializeStorage(const gl::Context *context, BindFlags bindFlags)
{
    // Only initialize the first time this texture is used as a render target or shader resource
    if (mTexStorage)
    {
        return angle::Result::Continue;
    }

    // do not attempt to create storage for nonexistant data
    if (!isLevelComplete(getBaseLevel()))
    {
        return angle::Result::Continue;
    }

    TexStoragePointer storage;
    ANGLE_TRY(createCompleteStorage(context, bindFlags, &storage));

    ANGLE_TRY(setCompleteTexStorage(context, storage.get()));
    storage.release();

    ASSERT(mTexStorage);

    // flush image data to the storage
    ANGLE_TRY(updateStorage(context));

    return angle::Result::Continue;
}

angle::Result TextureD3D_3D::createCompleteStorage(const gl::Context *context,
                                                   BindFlags bindFlags,
                                                   TexStoragePointer *outStorage) const
{
    GLsizei width         = getLevelZeroWidth();
    GLsizei height        = getLevelZeroHeight();
    GLsizei depth         = getLevelZeroDepth();
    GLenum internalFormat = getBaseLevelInternalFormat();

    ASSERT(width > 0 && height > 0 && depth > 0);

    // use existing storage level count, when previously specified by TexStorage*D
    GLint levels =
        (mTexStorage ? mTexStorage->getLevelCount() : creationLevels(width, height, depth));

    // TODO: Verify creation of the storage succeeded
    *outStorage = {mRenderer->createTextureStorage3D(internalFormat, bindFlags, width, height,
                                                     depth, levels, mState.getLabel()),
                   context};

    return angle::Result::Continue;
}

angle::Result TextureD3D_3D::setCompleteTexStorage(const gl::Context *context,
                                                   TextureStorage *newCompleteTexStorage)
{
    gl::TexLevelMask copyImageMask;
    copyImageMask.set();

    ANGLE_TRY(releaseTexStorage(context, copyImageMask));
    mTexStorage = newCompleteTexStorage;
    mTexStorageObserverBinding.bind(mTexStorage);
    mDirtyImages = true;

    // We do not support managed 3D storage, as that is D3D9/ES2-only
    ASSERT(!mTexStorage->isManaged());

    return angle::Result::Continue;
}

angle::Result TextureD3D_3D::updateStorage(const gl::Context *context)
{
    if (!mDirtyImages)
    {
        return angle::Result::Continue;
    }

    ASSERT(mTexStorage != nullptr);
    GLint storageLevels = mTexStorage->getLevelCount();
    for (int level = 0; level < storageLevels; level++)
    {
        if (mImageArray[level]->isDirty() && isLevelComplete(level))
        {
            ANGLE_TRY(updateStorageLevel(context, level));
        }
    }

    mDirtyImages = false;
    return angle::Result::Continue;
}

bool TextureD3D_3D::isNonBaseLevelComplete(int level) const
{
    ASSERT(level >= 0 && level < static_cast<int>(mImageArray.size()) &&
           mImageArray[level] != nullptr);

    ImageD3D *levelImage = mImageArray[level].get();

    if (levelImage->getInternalFormat() != getBaseLevelInternalFormat())
    {
        return false;
    }

    if (levelImage->getWidth() != std::max(1, getLevelZeroWidth() >> level))
    {
        return false;
    }

    if (levelImage->getHeight() != std::max(1, getLevelZeroHeight() >> level))
    {
        return false;
    }

    if (levelImage->getDepth() != std::max(1, getLevelZeroDepth() >> level))
    {
        return false;
    }

    return true;
}

bool TextureD3D_3D::isImageComplete(const gl::ImageIndex &index) const
{
    return isLevelComplete(index.getLevelIndex());
}

angle::Result TextureD3D_3D::updateStorageLevel(const gl::Context *context, int level)
{
    ASSERT(level >= 0 && level < static_cast<int>(mImageArray.size()) &&
           mImageArray[level] != nullptr);
    ASSERT(isLevelComplete(level));

    if (mImageArray[level]->isDirty())
    {
        gl::ImageIndex index = gl::ImageIndex::Make3D(level);
        gl::Box region(0, 0, 0, getWidth(level), getHeight(level), getDepth(level));
        ANGLE_TRY(commitRegion(context, index, region));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_3D::redefineImage(const gl::Context *context,
                                           GLint level,
                                           GLenum internalformat,
                                           const gl::Extents &size,
                                           bool forceRelease)
{
    ANGLE_TRY(releaseTexStorageIfMismatched(context, level, internalformat, size,
                                            false /*forceReleaseStorage*/));

    mImageArray[level]->redefine(gl::TextureType::_3D, internalformat, size, forceRelease);
    mDirtyImages = mDirtyImages || mImageArray[level]->isDirty();

    return angle::Result::Continue;
}

gl::ImageIndexIterator TextureD3D_3D::imageIterator() const
{
    return gl::ImageIndexIterator::Make3D(0, mTexStorage->getLevelCount(),
                                          gl::ImageIndex::kEntireLevel,
                                          gl::ImageIndex::kEntireLevel);
}

gl::ImageIndex TextureD3D_3D::getImageIndex(GLint mip, GLint /*layer*/) const
{
    // The "layer" here does not apply to 3D images. We use one Image per mip.
    return gl::ImageIndex::Make3D(mip);
}

bool TextureD3D_3D::isValidIndex(const gl::ImageIndex &index) const
{
    return (mTexStorage && index.getType() == gl::TextureType::_3D && index.getLevelIndex() >= 0 &&
            index.getLevelIndex() < mTexStorage->getLevelCount());
}

void TextureD3D_3D::markAllImagesDirty()
{
    for (int i = 0; i < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS; i++)
    {
        mImageArray[i]->markDirty();
    }
    mDirtyImages = true;
}

GLint TextureD3D_3D::getLevelZeroDepth() const
{
    // See comment in TextureD3D::getLevelZeroWidth().
    if (isImmutable())
    {
        ASSERT(mTexStorage);
        return mTexStorage->getLevelDepth(0);
    }
    ASSERT(gl::CountLeadingZeros(static_cast<uint32_t>(getBaseLevelDepth())) > getBaseLevel());
    return getBaseLevelDepth() << getBaseLevel();
}

TextureD3D_2DArray::TextureD3D_2DArray(const gl::TextureState &state, RendererD3D *renderer)
    : TextureD3D(state, renderer)
{}

void TextureD3D_2DArray::onDestroy(const gl::Context *context)
{
    // Delete the Images before the TextureStorage. Images might be relying on the TextureStorage
    // for some of their data. If TextureStorage is deleted before the Images, then their data will
    // be wastefully copied back from the GPU before we delete the Images.
    deleteImages();
    return TextureD3D::onDestroy(context);
}

TextureD3D_2DArray::~TextureD3D_2DArray() {}

ImageD3D *TextureD3D_2DArray::getImage(int level, int layer) const
{
    ASSERT(level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS);
    ASSERT((layer == 0 && mLayerCounts[level] == 0) || layer < mLayerCounts[level]);
    return !mImageArray[level].empty() ? mImageArray[level][layer] : nullptr;
}

ImageD3D *TextureD3D_2DArray::getImage(const gl::ImageIndex &index) const
{
    ASSERT(index.getLevelIndex() < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS);
    ASSERT(index.hasLayer());
    ASSERT((index.getLayerIndex() == 0 && mLayerCounts[index.getLevelIndex()] == 0) ||
           index.getLayerIndex() < mLayerCounts[index.getLevelIndex()]);
    ASSERT(index.getType() == gl::TextureType::_2DArray);
    return !mImageArray[index.getLevelIndex()].empty()
               ? mImageArray[index.getLevelIndex()][index.getLayerIndex()]
               : nullptr;
}

GLsizei TextureD3D_2DArray::getLayerCount(int level) const
{
    ASSERT(level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS);
    return mLayerCounts[level];
}

GLsizei TextureD3D_2DArray::getWidth(GLint level) const
{
    return (level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS && mLayerCounts[level] > 0)
               ? mImageArray[level][0]->getWidth()
               : 0;
}

GLsizei TextureD3D_2DArray::getHeight(GLint level) const
{
    return (level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS && mLayerCounts[level] > 0)
               ? mImageArray[level][0]->getHeight()
               : 0;
}

GLenum TextureD3D_2DArray::getInternalFormat(GLint level) const
{
    return (level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS && mLayerCounts[level] > 0)
               ? mImageArray[level][0]->getInternalFormat()
               : GL_NONE;
}

bool TextureD3D_2DArray::isDepth(GLint level) const
{
    return gl::GetSizedInternalFormatInfo(getInternalFormat(level)).depthBits > 0;
}

bool TextureD3D_2DArray::isSRGB(GLint level) const
{
    return gl::GetSizedInternalFormatInfo(getInternalFormat(level)).colorEncoding == GL_SRGB;
}

angle::Result TextureD3D_2DArray::setEGLImageTarget(const gl::Context *context,
                                                    gl::TextureType type,
                                                    egl::Image *image)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::setImage(const gl::Context *context,
                                           const gl::ImageIndex &index,
                                           GLenum internalFormat,
                                           const gl::Extents &size,
                                           GLenum format,
                                           GLenum type,
                                           const gl::PixelUnpackState &unpack,
                                           gl::Buffer *unpackBuffer,
                                           const uint8_t *pixels)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_2DArray);

    const gl::InternalFormat &formatInfo = gl::GetInternalFormatInfo(internalFormat, type);

    ANGLE_TRY(
        redefineImage(context, index.getLevelIndex(), formatInfo.sizedInternalFormat, size, false));

    ContextD3D *contextD3D = GetImplAs<ContextD3D>(context);

    GLuint inputDepthPitch = 0;
    ANGLE_CHECK_GL_MATH(contextD3D, formatInfo.computeDepthPitch(
                                        type, size.width, size.height, unpack.alignment,
                                        unpack.rowLength, unpack.imageHeight, &inputDepthPitch));

    for (int i = 0; i < size.depth; i++)
    {
        const ptrdiff_t layerOffset = (inputDepthPitch * i);
        gl::ImageIndex layerIndex   = gl::ImageIndex::Make2DArray(index.getLevelIndex(), i);
        ANGLE_TRY(
            setImageImpl(context, layerIndex, type, unpack, unpackBuffer, pixels, layerOffset));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::setSubImage(const gl::Context *context,
                                              const gl::ImageIndex &index,
                                              const gl::Box &area,
                                              GLenum format,
                                              GLenum type,
                                              const gl::PixelUnpackState &unpack,
                                              gl::Buffer *unpackBuffer,
                                              const uint8_t *pixels)
{
    ContextD3D *contextD3D = GetImplAs<ContextD3D>(context);

    ASSERT(index.getTarget() == gl::TextureTarget::_2DArray);
    const gl::InternalFormat &formatInfo =
        gl::GetInternalFormatInfo(getInternalFormat(index.getLevelIndex()), type);
    GLuint inputDepthPitch = 0;
    ANGLE_CHECK_GL_MATH(contextD3D, formatInfo.computeDepthPitch(
                                        type, area.width, area.height, unpack.alignment,
                                        unpack.rowLength, unpack.imageHeight, &inputDepthPitch));

    for (int i = 0; i < area.depth; i++)
    {
        int layer                   = area.z + i;
        const ptrdiff_t layerOffset = (inputDepthPitch * i);

        gl::Box layerArea(area.x, area.y, 0, area.width, area.height, 1);

        gl::ImageIndex layerIndex = gl::ImageIndex::Make2DArray(index.getLevelIndex(), layer);
        ANGLE_TRY(TextureD3D::subImage(context, layerIndex, layerArea, format, type, unpack,
                                       unpackBuffer, pixels, layerOffset));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::setCompressedImage(const gl::Context *context,
                                                     const gl::ImageIndex &index,
                                                     GLenum internalFormat,
                                                     const gl::Extents &size,
                                                     const gl::PixelUnpackState &unpack,
                                                     size_t imageSize,
                                                     const uint8_t *pixels)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_2DArray);

    ContextD3D *contextD3D = GetImplAs<ContextD3D>(context);

    // compressed formats don't have separate sized internal formats-- we can just use the
    // compressed format directly
    ANGLE_TRY(redefineImage(context, index.getLevelIndex(), internalFormat, size, false));

    const gl::InternalFormat &formatInfo = gl::GetSizedInternalFormatInfo(internalFormat);
    GLuint inputDepthPitch               = 0;
    ANGLE_CHECK_GL_MATH(
        contextD3D, formatInfo.computeDepthPitch(GL_UNSIGNED_BYTE, size.width, size.height, 1, 0, 0,
                                                 &inputDepthPitch));

    for (int i = 0; i < size.depth; i++)
    {
        const ptrdiff_t layerOffset = (inputDepthPitch * i);

        gl::ImageIndex layerIndex = gl::ImageIndex::Make2DArray(index.getLevelIndex(), i);
        ANGLE_TRY(setCompressedImageImpl(context, layerIndex, unpack, pixels, layerOffset));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::setCompressedSubImage(const gl::Context *context,
                                                        const gl::ImageIndex &index,
                                                        const gl::Box &area,
                                                        GLenum format,
                                                        const gl::PixelUnpackState &unpack,
                                                        size_t imageSize,
                                                        const uint8_t *pixels)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_2DArray);

    ContextD3D *contextD3D = GetImplAs<ContextD3D>(context);

    const gl::InternalFormat &formatInfo = gl::GetSizedInternalFormatInfo(format);
    GLuint inputDepthPitch               = 0;
    ANGLE_CHECK_GL_MATH(
        contextD3D, formatInfo.computeDepthPitch(GL_UNSIGNED_BYTE, area.width, area.height, 1, 0, 0,
                                                 &inputDepthPitch));

    for (int i = 0; i < area.depth; i++)
    {
        int layer                   = area.z + i;
        const ptrdiff_t layerOffset = (inputDepthPitch * i);

        gl::Box layerArea(area.x, area.y, 0, area.width, area.height, 1);

        gl::ImageIndex layerIndex = gl::ImageIndex::Make2DArray(index.getLevelIndex(), layer);
        ANGLE_TRY(TextureD3D::subImageCompressed(context, layerIndex, layerArea, format, unpack,
                                                 pixels, layerOffset));
        ANGLE_TRY(commitRegion(context, layerIndex, layerArea));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::copyImage(const gl::Context *context,
                                            const gl::ImageIndex &index,
                                            const gl::Rectangle &sourceArea,
                                            GLenum internalFormat,
                                            gl::Framebuffer *source)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::copySubImage(const gl::Context *context,
                                               const gl::ImageIndex &index,
                                               const gl::Offset &destOffset,
                                               const gl::Rectangle &sourceArea,
                                               gl::Framebuffer *source)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_2DArray);

    gl::Extents fbSize = source->getReadColorAttachment()->getSize();
    gl::Rectangle clippedSourceArea;
    if (!ClipRectangle(sourceArea, gl::Rectangle(0, 0, fbSize.width, fbSize.height),
                       &clippedSourceArea))
    {
        return angle::Result::Continue;
    }
    const gl::Offset clippedDestOffset(destOffset.x + clippedSourceArea.x - sourceArea.x,
                                       destOffset.y + clippedSourceArea.y - sourceArea.y,
                                       destOffset.z);

    if (!canCreateRenderTargetForImage(index))
    {
        ANGLE_TRY(copyImageFromFramebufferToStaging(context, index, clippedDestOffset,
                                                    clippedSourceArea, source));
    }
    else
    {
        ANGLE_TRY(ensureRenderTarget(context));

        if (isValidLevel(index.getLevelIndex()))
        {
            ANGLE_TRY(updateStorageLevel(context, index.getLevelIndex()));
            ANGLE_TRY(
                mRenderer->copyImage2DArray(context, source, clippedSourceArea,
                                            gl::GetUnsizedFormat(getInternalFormat(getBaseLevel())),
                                            clippedDestOffset, mTexStorage, index.getLevelIndex()));
        }
    }
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::copyTexture(const gl::Context *context,
                                              const gl::ImageIndex &index,
                                              GLenum internalFormat,
                                              GLenum type,
                                              gl::LevelIndex sourceLevel,
                                              bool unpackFlipY,
                                              bool unpackPremultiplyAlpha,
                                              bool unpackUnmultiplyAlpha,
                                              const gl::Texture *source)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_2DArray);

    gl::TextureType sourceType = source->getType();

    const gl::InternalFormat &internalFormatInfo = gl::GetInternalFormatInfo(internalFormat, type);
    gl::Extents size(static_cast<int>(source->getWidth(NonCubeTextureTypeToTarget(sourceType),
                                                       sourceLevel.get())),
                     static_cast<int>(source->getHeight(NonCubeTextureTypeToTarget(sourceType),
                                                        sourceLevel.get())),
                     static_cast<int>(source->getDepth(NonCubeTextureTypeToTarget(sourceType),
                                                       sourceLevel.get())));

    ANGLE_TRY(redefineImage(context, index.getLevelIndex(), internalFormatInfo.sizedInternalFormat,
                            size, false));

    gl::Box sourceBox(0, 0, 0, size.width, size.height, size.depth);
    gl::Offset destOffset(0, 0, 0);

    gl::ImageIndex destIndex =
        gl::ImageIndex::Make2DArrayRange(index.getLevelIndex(), 0, size.depth);

    if (!isSRGB(index.getLevelIndex()) &&
        canCreateRenderTargetForImage(
            gl::ImageIndex::Make2DArrayRange(index.getLevelIndex(), 0, size.depth)))
    {
        ANGLE_TRY(ensureRenderTarget(context));
        ASSERT(isValidLevel(index.getLevelIndex()));
        ANGLE_TRY(updateStorageLevel(context, index.getLevelIndex()));
        ANGLE_TRY(mRenderer->copyTexture(
            context, source, sourceLevel.get(), gl::TextureTarget::_2DArray, sourceBox,
            internalFormatInfo.format, internalFormatInfo.type, destOffset, mTexStorage,
            index.getTarget(), index.getLevelIndex(), unpackFlipY, unpackPremultiplyAlpha,
            unpackUnmultiplyAlpha));
    }
    else
    {
        for (int i = 0; i < size.depth; i++)
        {
            gl::ImageIndex currentSourceDepthIndex =
                gl::ImageIndex::Make2DArray(sourceLevel.get(), i);
            gl::ImageIndex currentDestDepthIndex =
                gl::ImageIndex::Make2DArray(index.getLevelIndex(), i);
            ImageD3D *sourceImage = nullptr;
            ImageD3D *destImage   = nullptr;
            TextureD3D *sourceD3D = GetImplAs<TextureD3D>(source);

            ANGLE_TRY(
                getImageAndSyncFromStorageIfNeeded(context, currentDestDepthIndex, &destImage));
            ANGLE_TRY(sourceD3D->getImageAndSyncFromStorageIfNeeded(
                context, currentSourceDepthIndex, &sourceImage));
            gl::Box imageBox(sourceBox.x, sourceBox.y, 0, sourceBox.width, sourceBox.height, 1);
            ANGLE_TRY(mRenderer->copyImage(context, destImage, sourceImage, imageBox, destOffset,
                                           unpackFlipY, unpackPremultiplyAlpha,
                                           unpackUnmultiplyAlpha));
        }

        mDirtyImages = true;

        gl::Box destRegion(destOffset.x, destOffset.y, destOffset.z, sourceBox.width,
                           sourceBox.height, sourceBox.depth);
        ANGLE_TRY(commitRegion(context, destIndex, destRegion));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::copySubTexture(const gl::Context *context,
                                                 const gl::ImageIndex &index,
                                                 const gl::Offset &destOffset,
                                                 gl::LevelIndex sourceLevel,
                                                 const gl::Box &sourceBox,
                                                 bool unpackFlipY,
                                                 bool unpackPremultiplyAlpha,
                                                 bool unpackUnmultiplyAlpha,
                                                 const gl::Texture *source)
{
    ASSERT(index.getTarget() == gl::TextureTarget::_2DArray);

    gl::ImageIndex destIndex = gl::ImageIndex::Make2DArrayRange(
        static_cast<GLint>(index.getLevelIndex()), destOffset.z, sourceBox.depth - destOffset.z);

    if (!isSRGB(destIndex.getLevelIndex()) && canCreateRenderTargetForImage(destIndex))
    {
        ANGLE_TRY(ensureRenderTarget(context));
        ASSERT(isValidLevel(destIndex.getLevelIndex()));
        ANGLE_TRY(updateStorageLevel(context, destIndex.getLevelIndex()));

        const gl::InternalFormat &internalFormatInfo =
            gl::GetSizedInternalFormatInfo(getInternalFormat(destIndex.getLevelIndex()));
        ANGLE_TRY(mRenderer->copyTexture(
            context, source, sourceLevel.get(), gl::TextureTarget::_2DArray, sourceBox,
            internalFormatInfo.format, internalFormatInfo.type, destOffset, mTexStorage,
            index.getTarget(), index.getLevelIndex(), unpackFlipY, unpackPremultiplyAlpha,
            unpackUnmultiplyAlpha));
    }
    else
    {
        for (int i = 0; i < sourceBox.depth; i++)
        {
            gl::ImageIndex currentSourceIndex =
                gl::ImageIndex::Make2DArray(sourceLevel.get(), i + sourceBox.z);
            gl::ImageIndex currentDestIndex =
                gl::ImageIndex::Make2DArray(index.getLevelIndex(), i + destOffset.z);

            gl::Box currentLayerBox(sourceBox.x, sourceBox.y, 0, sourceBox.width, sourceBox.height,
                                    1);

            TextureD3D *sourceD3D = GetImplAs<TextureD3D>(source);
            ImageD3D *sourceImage = nullptr;
            ANGLE_TRY(sourceD3D->getImageAndSyncFromStorageIfNeeded(context, currentSourceIndex,
                                                                    &sourceImage));

            ImageD3D *destImage = nullptr;
            ANGLE_TRY(getImageAndSyncFromStorageIfNeeded(context, currentDestIndex, &destImage));

            ANGLE_TRY(mRenderer->copyImage(context, destImage, sourceImage, currentLayerBox,
                                           destOffset, unpackFlipY, unpackPremultiplyAlpha,
                                           unpackUnmultiplyAlpha));
        }

        mDirtyImages = true;

        gl::Box destRegion(destOffset.x, destOffset.y, destOffset.z, sourceBox.width,
                           sourceBox.height, sourceBox.depth);
        ANGLE_TRY(commitRegion(context, destIndex, destRegion));
    }

    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::setStorage(const gl::Context *context,
                                             gl::TextureType type,
                                             size_t levels,
                                             GLenum internalFormat,
                                             const gl::Extents &size)
{
    ASSERT(type == gl::TextureType::_2DArray);

    deleteImages();

    for (size_t level = 0; level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS; level++)
    {
        gl::Extents levelLayerSize(std::max(1, size.width >> level),
                                   std::max(1, size.height >> level), 1);

        mLayerCounts[level] = (level < levels ? size.depth : 0);

        if (mLayerCounts[level] > 0)
        {
            // Create new images for this level
            mImageArray[level].resize(mLayerCounts[level]);

            for (int layer = 0; layer < mLayerCounts[level]; layer++)
            {
                mImageArray[level][layer] = mRenderer->createImage();
                mImageArray[level][layer]->redefine(gl::TextureType::_2DArray, internalFormat,
                                                    levelLayerSize, true);
            }
        }
    }

    return setStorageImpl(context, type, levels, internalFormat, size);
}

angle::Result TextureD3D_2DArray::bindTexImage(const gl::Context *context, egl::Surface *surface)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::releaseTexImage(const gl::Context *context)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::initMipmapImages(const gl::Context *context)
{
    const GLuint baseLevel = mState.getEffectiveBaseLevel();
    const GLuint maxLevel  = mState.getMipmapMaxLevel();
    int baseWidth          = getLevelZeroWidth();
    int baseHeight         = getLevelZeroHeight();
    int baseDepth          = getLayerCount(getBaseLevel());
    GLenum baseFormat      = getBaseLevelInternalFormat();

    // Purge array levels baseLevel + 1 through q and reset them to represent the generated mipmap
    // levels.
    for (GLuint level = baseLevel + 1u; level <= maxLevel; level++)
    {
        ASSERT((baseWidth >> level) > 0 || (baseHeight >> level) > 0);
        gl::Extents levelLayerSize(std::max(baseWidth >> level, 1),
                                   std::max(baseHeight >> level, 1), baseDepth);
        ANGLE_TRY(redefineImage(context, level, baseFormat, levelLayerSize, false));
    }

    // We should be mip-complete now so generate the storage.
    ANGLE_TRY(initializeStorage(context, BindFlags::RenderTarget()));

    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::getRenderTarget(const gl::Context *context,
                                                  const gl::ImageIndex &index,
                                                  GLsizei samples,
                                                  RenderTargetD3D **outRT)
{
    // ensure the underlying texture is created
    ANGLE_TRY(ensureRenderTarget(context));
    ANGLE_TRY(updateStorageLevel(context, index.getLevelIndex()));
    return mTexStorage->getRenderTarget(context, index, outRT);
}

angle::Result TextureD3D_2DArray::initializeStorage(const gl::Context *context, BindFlags bindFlags)
{
    // Only initialize the first time this texture is used as a render target or shader resource
    if (mTexStorage)
    {
        return angle::Result::Continue;
    }

    // do not attempt to create storage for nonexistant data
    if (!isLevelComplete(getBaseLevel()))
    {
        return angle::Result::Continue;
    }

    bindFlags.renderTarget |= IsRenderTargetUsage(mState.getUsage());

    TexStoragePointer storage;
    ANGLE_TRY(createCompleteStorage(context, bindFlags, &storage));

    ANGLE_TRY(setCompleteTexStorage(context, storage.get()));
    storage.release();

    ASSERT(mTexStorage);

    // flush image data to the storage
    ANGLE_TRY(updateStorage(context));

    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::createCompleteStorage(const gl::Context *context,
                                                        BindFlags bindFlags,
                                                        TexStoragePointer *outStorage) const
{
    GLsizei width         = getLevelZeroWidth();
    GLsizei height        = getLevelZeroHeight();
    GLsizei depth         = getBaseLevelStorageDepth();
    GLenum internalFormat = getBaseLevelInternalFormat();

    ASSERT(width > 0 && height > 0 && depth > 0);

    // use existing storage level count, when previously specified by TexStorage*D
    GLint levels = (mTexStorage ? mTexStorage->getLevelCount() : creationLevels(width, height, 1));

    // TODO(geofflang): Verify storage creation succeeds
    *outStorage = {mRenderer->createTextureStorage2DArray(internalFormat, bindFlags, width, height,
                                                          depth, levels, mState.getLabel()),
                   context};

    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::setCompleteTexStorage(const gl::Context *context,
                                                        TextureStorage *newCompleteTexStorage)
{
    gl::TexLevelMask copyImageMask;
    copyImageMask.set();

    ANGLE_TRY(releaseTexStorage(context, copyImageMask));
    mTexStorage = newCompleteTexStorage;
    mTexStorageObserverBinding.bind(mTexStorage);
    mDirtyImages = true;

    // We do not support managed 2D array storage, as managed storage is ES2/D3D9 only
    ASSERT(!mTexStorage->isManaged());

    return angle::Result::Continue;
}

angle::Result TextureD3D_2DArray::updateStorage(const gl::Context *context)
{
    if (!mDirtyImages)
    {
        return angle::Result::Continue;
    }

    ASSERT(mTexStorage != nullptr);
    GLint storageLevels = mTexStorage->getLevelCount();
    for (int level = 0; level < storageLevels; level++)
    {
        if (isLevelComplete(level))
        {
            ANGLE_TRY(updateStorageLevel(context, level));
        }
    }

    mDirtyImages = false;
    return angle::Result::Continue;
}

bool TextureD3D_2DArray::isNonBaseLevelComplete(int level) const
{
    ASSERT(level >= 0 && level < (int)ArraySize(mImageArray));

    // Layers check needs to happen after the base checks in TextureD3D::isLevelComplete, otherwise
    // an out-of-range base level may be queried.
    GLsizei layers = getLayerCount(getBaseLevel());

    if (layers <= 0)
    {
        return false;
    }

    if (getInternalFormat(level) != getInternalFormat(getBaseLevel()))
    {
        return false;
    }

    if (getWidth(level) != std::max(1, getLevelZeroWidth() >> level))
    {
        return false;
    }

    if (getHeight(level) != std::max(1, getLevelZeroHeight() >> level))
    {
        return false;
    }

    if (getLayerCount(level) != layers)
    {
        return false;
    }

    return true;
}

bool TextureD3D_2DArray::isImageComplete(const gl::ImageIndex &index) const
{
    return isLevelComplete(index.getLevelIndex());
}

angle::Result TextureD3D_2DArray::updateStorageLevel(const gl::Context *context, int level)
{
    ASSERT(level >= 0 && level < static_cast<int>(ArraySize(mLayerCounts)));
    ASSERT(isLevelComplete(level));

    for (int layer = 0; layer < mLayerCounts[level]; layer++)
    {
        ASSERT(!mImageArray[level].empty() && mImageArray[level][layer] != nullptr);
        if (mImageArray[level][layer]->isDirty())
        {
            gl::ImageIndex index = gl::ImageIndex::Make2DArray(level, layer);
            gl::Box region(0, 0, 0, getWidth(level), getHeight(level), 1);
            ANGLE_TRY(commitRegion(context, index, region));
        }
    }

    return angle::Result::Continue;
}

GLint TextureD3D_2DArray::getBaseLevelStorageDepth() const
{
    const GLuint baseLevel = getBaseLevel();
    if (baseLevel < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS)
    {
        return getLayerCount(baseLevel);
    }
    return 0;
}

void TextureD3D_2DArray::deleteImages()
{
    for (int level = 0; level < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS; ++level)
    {
        for (int layer = 0; layer < mLayerCounts[level]; ++layer)
        {
            delete mImageArray[level][layer];
        }
        mImageArray[level].clear();
        mLayerCounts[level] = 0;
    }
}

angle::Result TextureD3D_2DArray::redefineImage(const gl::Context *context,
                                                GLint level,
                                                GLenum internalformat,
                                                const gl::Extents &size,
                                                bool forceRelease)
{
    // Only reallocate the layers if the size doesn't match
    if (size.depth != mLayerCounts[level])
    {
        for (int layer = 0; layer < mLayerCounts[level]; layer++)
        {
            SafeDelete(mImageArray[level][layer]);
        }
        mImageArray[level].clear();
        mLayerCounts[level] = size.depth;

        if (size.depth > 0)
        {
            mImageArray[level].resize(size.depth);
            for (int layer = 0; layer < mLayerCounts[level]; layer++)
            {
                mImageArray[level][layer] = mRenderer->createImage();
            }
        }
    }

    ANGLE_TRY(releaseTexStorageIfMismatched(context, level, internalformat, size,
                                            false /*forceReleaseStorage*/));

    if (size.depth > 0)
    {
        for (int layer = 0; layer < mLayerCounts[level]; layer++)
        {
            mImageArray[level][layer]->redefine(gl::TextureType::_2DArray, internalformat,
                                                gl::Extents(size.width, size.height, 1),
                                                forceRelease);
            mDirtyImages = mDirtyImages || mImageArray[level][layer]->isDirty();
        }
    }

    return angle::Result::Continue;
}

gl::ImageIndexIterator TextureD3D_2DArray::imageIterator() const
{
    return gl::ImageIndexIterator::Make2DArray(0, mTexStorage->getLevelCount(),
                                               mLayerCounts.data());
}

gl::ImageIndex TextureD3D_2DArray::getImageIndex(GLint mip, GLint layer) const
{
    return gl::ImageIndex::Make2DArray(mip, layer);
}

bool TextureD3D_2DArray::isValidIndex(const gl::ImageIndex &index) const
{
    // Check for having a storage and the right type of index
    if (!mTexStorage || index.getType() != gl::TextureType::_2DArray)
    {
        return false;
    }

    // Check the mip index
    if (index.getLevelIndex() < 0 || index.getLevelIndex() >= mTexStorage->getLevelCount())
    {
        return false;
    }

    // Check the layer index
    return !index.hasLayer() || (index.getLayerIndex() >= 0 &&
                                 index.getLayerIndex() < mLayerCounts[index.getLevelIndex()]);
}

void TextureD3D_2DArray::markAllImagesDirty()
{
    for (int dirtyLevel = 0; dirtyLevel < gl::IMPLEMENTATION_MAX_TEXTURE_LEVELS; dirtyLevel++)
    {
        for (int dirtyLayer = 0; dirtyLayer < mLayerCounts[dirtyLevel]; dirtyLayer++)
        {
            mImageArray[dirtyLevel][dirtyLayer]->markDirty();
        }
    }
    mDirtyImages = true;
}

TextureD3DImmutableBase::TextureD3DImmutableBase(const gl::TextureState &state,
                                                 RendererD3D *renderer)
    : TextureD3D(state, renderer)
{}

TextureD3DImmutableBase::~TextureD3DImmutableBase() {}

ImageD3D *TextureD3DImmutableBase::getImage(const gl::ImageIndex &index) const
{
    return nullptr;
}

angle::Result TextureD3DImmutableBase::setImage(const gl::Context *context,
                                                const gl::ImageIndex &index,
                                                GLenum internalFormat,
                                                const gl::Extents &size,
                                                GLenum format,
                                                GLenum type,
                                                const gl::PixelUnpackState &unpack,
                                                gl::Buffer *unpackBuffer,
                                                const uint8_t *pixels)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3DImmutableBase::setSubImage(const gl::Context *context,
                                                   const gl::ImageIndex &index,
                                                   const gl::Box &area,
                                                   GLenum format,
                                                   GLenum type,
                                                   const gl::PixelUnpackState &unpack,
                                                   gl::Buffer *unpackBuffer,
                                                   const uint8_t *pixels)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3DImmutableBase::setCompressedImage(const gl::Context *context,
                                                          const gl::ImageIndex &index,
                                                          GLenum internalFormat,
                                                          const gl::Extents &size,
                                                          const gl::PixelUnpackState &unpack,
                                                          size_t imageSize,
                                                          const uint8_t *pixels)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3DImmutableBase::setCompressedSubImage(const gl::Context *context,
                                                             const gl::ImageIndex &index,
                                                             const gl::Box &area,
                                                             GLenum format,
                                                             const gl::PixelUnpackState &unpack,
                                                             size_t imageSize,
                                                             const uint8_t *pixels)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3DImmutableBase::copyImage(const gl::Context *context,
                                                 const gl::ImageIndex &index,
                                                 const gl::Rectangle &sourceArea,
                                                 GLenum internalFormat,
                                                 gl::Framebuffer *source)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3DImmutableBase::copySubImage(const gl::Context *context,
                                                    const gl::ImageIndex &index,
                                                    const gl::Offset &destOffset,
                                                    const gl::Rectangle &sourceArea,
                                                    gl::Framebuffer *source)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3DImmutableBase::bindTexImage(const gl::Context *context,
                                                    egl::Surface *surface)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3DImmutableBase::releaseTexImage(const gl::Context *context)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

TextureD3D_External::TextureD3D_External(const gl::TextureState &state, RendererD3D *renderer)
    : TextureD3DImmutableBase(state, renderer)
{}

TextureD3D_External::~TextureD3D_External() {}

GLsizei TextureD3D_External::getLayerCount(int level) const
{
    return 1;
}

angle::Result TextureD3D_External::setImageExternal(const gl::Context *context,
                                                    gl::TextureType type,
                                                    egl::Stream *stream,
                                                    const egl::Stream::GLTextureDescription &desc)
{
    ASSERT(type == gl::TextureType::External);

    ANGLE_TRY(releaseTexStorage(context, gl::TexLevelMask()));

    // If the stream is null, the external image is unbound and we release the storage
    if (stream != nullptr)
    {
        mTexStorage = mRenderer->createTextureStorageExternal(stream, desc, mState.getLabel());
    }
    // An EGLStream (egl::Stream) is not an EGLImage (egl::Image), so mEGLImageTarget is set to
    // false.
    mEGLImageTarget = false;

    return angle::Result::Continue;
}

angle::Result TextureD3D_External::setEGLImageTarget(const gl::Context *context,
                                                     gl::TextureType type,
                                                     egl::Image *image)
{
    EGLImageD3D *eglImaged3d = GetImplAs<EGLImageD3D>(image);

    // Pass in the RenderTargetD3D here: createTextureStorage can't generate an error.
    RenderTargetD3D *renderTargetD3D = nullptr;
    ANGLE_TRY(eglImaged3d->getRenderTarget(context, &renderTargetD3D));

    ANGLE_TRY(releaseTexStorage(context, gl::TexLevelMask()));
    mTexStorage =
        mRenderer->createTextureStorageEGLImage(eglImaged3d, renderTargetD3D, mState.getLabel());
    mEGLImageTarget = true;

    return angle::Result::Continue;
}

angle::Result TextureD3D_External::initMipmapImages(const gl::Context *context)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Stop;
}

angle::Result TextureD3D_External::getRenderTarget(const gl::Context *context,
                                                   const gl::ImageIndex &index,
                                                   GLsizei samples,
                                                   RenderTargetD3D **outRT)
{
    UNREACHABLE();
    return angle::Result::Stop;
}

bool TextureD3D_External::isImageComplete(const gl::ImageIndex &index) const
{
    return (index.getLevelIndex() == 0) ? (mTexStorage != nullptr) : false;
}

angle::Result TextureD3D_External::initializeStorage(const gl::Context *context,
                                                     BindFlags bindFlags)
{
    // Texture storage is created when an external image is bound
    ASSERT(mTexStorage);
    return angle::Result::Continue;
}

angle::Result TextureD3D_External::createCompleteStorage(const gl::Context *context,
                                                         BindFlags bindFlags,
                                                         TexStoragePointer *outStorage) const
{
    UNREACHABLE();
    return angle::Result::Continue;
}

angle::Result TextureD3D_External::setCompleteTexStorage(const gl::Context *context,
                                                         TextureStorage *newCompleteTexStorage)
{
    UNREACHABLE();
    return angle::Result::Continue;
}

angle::Result TextureD3D_External::updateStorage(const gl::Context *context)
{
    // Texture storage does not need to be updated since it is already loaded with the latest
    // external image
    ASSERT(mTexStorage);
    return angle::Result::Continue;
}

gl::ImageIndexIterator TextureD3D_External::imageIterator() const
{
    return gl::ImageIndexIterator::Make2D(0, mTexStorage->getLevelCount());
}

gl::ImageIndex TextureD3D_External::getImageIndex(GLint mip, GLint /*layer*/) const
{
    // "layer" does not apply to 2D Textures.
    return gl::ImageIndex::Make2D(mip);
}

bool TextureD3D_External::isValidIndex(const gl::ImageIndex &index) const
{
    return (mTexStorage && index.getType() == gl::TextureType::External &&
            index.getLevelIndex() == 0);
}

void TextureD3D_External::markAllImagesDirty()
{
    UNREACHABLE();
}

TextureD3D_2DMultisample::TextureD3D_2DMultisample(const gl::TextureState &state,
                                                   RendererD3D *renderer)
    : TextureD3DImmutableBase(state, renderer)
{}

TextureD3D_2DMultisample::~TextureD3D_2DMultisample() {}

angle::Result TextureD3D_2DMultisample::setStorageMultisample(const gl::Context *context,
                                                              gl::TextureType type,
                                                              GLsizei samples,
                                                              GLint internalformat,
                                                              const gl::Extents &size,
                                                              bool fixedSampleLocations)
{
    ASSERT(type == gl::TextureType::_2DMultisample && size.depth == 1);

    // We allocate storage immediately instead of doing it lazily like other TextureD3D classes do.
    // This requires less state in this class.
    TexStoragePointer storage = {mRenderer->createTextureStorage2DMultisample(
                                     internalformat, size.width, size.height, static_cast<int>(0),
                                     samples, fixedSampleLocations, mState.getLabel()),
                                 context};

    ANGLE_TRY(setCompleteTexStorage(context, storage.get()));
    storage.release();

    mImmutable = true;

    return angle::Result::Continue;
}

angle::Result TextureD3D_2DMultisample::setEGLImageTarget(const gl::Context *context,
                                                          gl::TextureType type,
                                                          egl::Image *image)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DMultisample::getRenderTarget(const gl::Context *context,
                                                        const gl::ImageIndex &index,
                                                        GLsizei samples,
                                                        RenderTargetD3D **outRT)
{
    ASSERT(!index.hasLayer());

    // ensure the underlying texture is created
    ANGLE_TRY(ensureRenderTarget(context));

    return mTexStorage->getRenderTarget(context, index, outRT);
}

gl::ImageIndexIterator TextureD3D_2DMultisample::imageIterator() const
{
    return gl::ImageIndexIterator::Make2DMultisample();
}

gl::ImageIndex TextureD3D_2DMultisample::getImageIndex(GLint mip, GLint layer) const
{
    return gl::ImageIndex::Make2DMultisample();
}

bool TextureD3D_2DMultisample::isValidIndex(const gl::ImageIndex &index) const
{
    return (mTexStorage && index.getType() == gl::TextureType::_2DMultisample &&
            index.getLevelIndex() == 0);
}

GLsizei TextureD3D_2DMultisample::getLayerCount(int level) const
{
    return 1;
}

void TextureD3D_2DMultisample::markAllImagesDirty() {}

angle::Result TextureD3D_2DMultisample::initializeStorage(const gl::Context *context,
                                                          BindFlags bindFlags)
{
    // initializeStorage should only be called in a situation where the texture already has storage
    // associated with it (storage is created in setStorageMultisample).
    ASSERT(mTexStorage);
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DMultisample::createCompleteStorage(const gl::Context *context,
                                                              BindFlags bindFlags,
                                                              TexStoragePointer *outStorage) const
{
    UNREACHABLE();
    *outStorage = {mTexStorage, context};
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DMultisample::setCompleteTexStorage(const gl::Context *context,
                                                              TextureStorage *newCompleteTexStorage)
{
    // These textures are immutable, so this should only be ever called once.
    ASSERT(!mTexStorage);
    mTexStorage = newCompleteTexStorage;
    mTexStorageObserverBinding.bind(mTexStorage);
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DMultisample::updateStorage(const gl::Context *context)
{
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DMultisample::initMipmapImages(const gl::Context *context)
{
    UNREACHABLE();
    return angle::Result::Continue;
}

bool TextureD3D_2DMultisample::isImageComplete(const gl::ImageIndex &index) const
{
    return true;
}

TextureD3D_2DMultisampleArray::TextureD3D_2DMultisampleArray(const gl::TextureState &state,
                                                             RendererD3D *renderer)
    : TextureD3DImmutableBase(state, renderer)
{}

TextureD3D_2DMultisampleArray::~TextureD3D_2DMultisampleArray() {}

angle::Result TextureD3D_2DMultisampleArray::setStorageMultisample(const gl::Context *context,
                                                                   gl::TextureType type,
                                                                   GLsizei samples,
                                                                   GLint internalformat,
                                                                   const gl::Extents &size,
                                                                   bool fixedSampleLocations)
{
    ASSERT(type == gl::TextureType::_2DMultisampleArray);

    mLayerCount = size.depth;

    TexStoragePointer storage = {
        mRenderer->createTextureStorage2DMultisampleArray(internalformat, size.width, size.height,
                                                          size.depth, static_cast<int>(0), samples,
                                                          fixedSampleLocations, mState.getLabel()),
        context};

    ANGLE_TRY(setCompleteTexStorage(context, storage.get()));
    storage.release();

    mImmutable = true;

    return angle::Result::Continue;
}

angle::Result TextureD3D_2DMultisampleArray::setEGLImageTarget(const gl::Context *context,
                                                               gl::TextureType type,
                                                               egl::Image *image)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DMultisampleArray::getRenderTarget(const gl::Context *context,
                                                             const gl::ImageIndex &index,
                                                             GLsizei samples,
                                                             RenderTargetD3D **outRT)
{
    // ensure the underlying texture is created
    ANGLE_TRY(ensureRenderTarget(context));

    return mTexStorage->getRenderTarget(context, index, outRT);
}

gl::ImageIndexIterator TextureD3D_2DMultisampleArray::imageIterator() const
{
    return gl::ImageIndexIterator::Make2DMultisampleArray(&mLayerCount);
}

gl::ImageIndex TextureD3D_2DMultisampleArray::getImageIndex(GLint mip, GLint layer) const
{
    return gl::ImageIndex::Make2DMultisampleArray(layer);
}

bool TextureD3D_2DMultisampleArray::isValidIndex(const gl::ImageIndex &index) const
{
    return (mTexStorage && index.getType() == gl::TextureType::_2DMultisampleArray &&
            index.getLevelIndex() == 0);
}

GLsizei TextureD3D_2DMultisampleArray::getLayerCount(int level) const
{
    return mLayerCount;
}

void TextureD3D_2DMultisampleArray::markAllImagesDirty() {}

angle::Result TextureD3D_2DMultisampleArray::initializeStorage(const gl::Context *context,
                                                               BindFlags bindFlags)
{
    // initializeStorage should only be called in a situation where the texture already has storage
    // associated with it (storage is created in setStorageMultisample).
    ASSERT(mTexStorage);
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DMultisampleArray::createCompleteStorage(
    const gl::Context *context,
    BindFlags bindFlags,
    TexStoragePointer *outStorage) const
{
    UNREACHABLE();
    *outStorage = {mTexStorage, context};
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DMultisampleArray::setCompleteTexStorage(
    const gl::Context *context,
    TextureStorage *newCompleteTexStorage)
{
    // These textures are immutable, so this should only be ever called once.
    ASSERT(!mTexStorage);
    mTexStorage = newCompleteTexStorage;
    mTexStorageObserverBinding.bind(mTexStorage);
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DMultisampleArray::updateStorage(const gl::Context *context)
{
    return angle::Result::Continue;
}

angle::Result TextureD3D_2DMultisampleArray::initMipmapImages(const gl::Context *context)
{
    UNIMPLEMENTED();
    return angle::Result::Continue;
}

bool TextureD3D_2DMultisampleArray::isImageComplete(const gl::ImageIndex &index) const
{
    return true;
}

TextureD3D_Buffer::TextureD3D_Buffer(const gl::TextureState &state, RendererD3D *renderer)
    : TextureD3D(state, renderer), mInternalFormat(GL_INVALID_ENUM)
{}

TextureD3D_Buffer::~TextureD3D_Buffer() {}

angle::Result TextureD3D_Buffer::setImage(const gl::Context *context,
                                          const gl::ImageIndex &index,
                                          GLenum internalFormat,
                                          const gl::Extents &size,
                                          GLenum format,
                                          GLenum type,
                                          const gl::PixelUnpackState &unpack,
                                          gl::Buffer *unpackBuffer,
                                          const uint8_t *pixels)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_Buffer::setSubImage(const gl::Context *context,
                                             const gl::ImageIndex &index,
                                             const gl::Box &area,
                                             GLenum format,
                                             GLenum type,
                                             const gl::PixelUnpackState &unpack,
                                             gl::Buffer *unpackBuffer,
                                             const uint8_t *pixels)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_Buffer::setCompressedImage(const gl::Context *context,
                                                    const gl::ImageIndex &index,
                                                    GLenum internalFormat,
                                                    const gl::Extents &size,
                                                    const gl::PixelUnpackState &unpack,
                                                    size_t imageSize,
                                                    const uint8_t *pixels)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_Buffer::setCompressedSubImage(const gl::Context *context,
                                                       const gl::ImageIndex &index,
                                                       const gl::Box &area,
                                                       GLenum format,
                                                       const gl::PixelUnpackState &unpack,
                                                       size_t imageSize,
                                                       const uint8_t *pixels)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_Buffer::copyImage(const gl::Context *context,
                                           const gl::ImageIndex &index,
                                           const gl::Rectangle &sourceArea,
                                           GLenum internalFormat,
                                           gl::Framebuffer *source)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_Buffer::copySubImage(const gl::Context *context,
                                              const gl::ImageIndex &index,
                                              const gl::Offset &destOffset,
                                              const gl::Rectangle &sourceArea,
                                              gl::Framebuffer *source)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_Buffer::bindTexImage(const gl::Context *context, egl::Surface *surface)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_Buffer::releaseTexImage(const gl::Context *context)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

GLsizei TextureD3D_Buffer::getLayerCount(int level) const
{
    return 1;
}

angle::Result TextureD3D_Buffer::initMipmapImages(const gl::Context *context)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Stop;
}

bool TextureD3D_Buffer::isImageComplete(const gl::ImageIndex &index) const
{
    return (index.getLevelIndex() == 0) ? (mTexStorage != nullptr) : false;
}

angle::Result TextureD3D_Buffer::initializeStorage(const gl::Context *context, BindFlags bindFlags)
{
    ASSERT(mTexStorage);
    return angle::Result::Continue;
}

angle::Result TextureD3D_Buffer::createCompleteStorage(const gl::Context *context,
                                                       BindFlags bindFlags,
                                                       TexStoragePointer *outStorage) const
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_Buffer::setCompleteTexStorage(const gl::Context *context,
                                                       TextureStorage *newCompleteTexStorage)
{
    ANGLE_TRY(releaseTexStorage(context, gl::TexLevelMask()));
    mTexStorage = newCompleteTexStorage;
    mTexStorageObserverBinding.bind(mTexStorage);

    mDirtyImages = true;

    return angle::Result::Continue;
}

angle::Result TextureD3D_Buffer::updateStorage(const gl::Context *context)
{
    ASSERT(mTexStorage);
    return angle::Result::Continue;
}

gl::ImageIndexIterator TextureD3D_Buffer::imageIterator() const
{
    return gl::ImageIndexIterator::MakeBuffer();
}

gl::ImageIndex TextureD3D_Buffer::getImageIndex(GLint mip, GLint layer) const
{
    return gl::ImageIndex::MakeBuffer();
}

bool TextureD3D_Buffer::isValidIndex(const gl::ImageIndex &index) const
{
    return (mTexStorage && index.getType() == gl::TextureType::Buffer &&
            index.getLevelIndex() == 0);
}

void TextureD3D_Buffer::markAllImagesDirty()
{
    UNREACHABLE();
}

angle::Result TextureD3D_Buffer::setEGLImageTarget(const gl::Context *context,
                                                   gl::TextureType type,
                                                   egl::Image *image)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

angle::Result TextureD3D_Buffer::getRenderTarget(const gl::Context *context,
                                                 const gl::ImageIndex &index,
                                                 GLsizei samples,
                                                 RenderTargetD3D **outRT)
{
    ANGLE_HR_UNREACHABLE(GetImplAs<ContextD3D>(context));
    return angle::Result::Continue;
}

ImageD3D *TextureD3D_Buffer::getImage(const gl::ImageIndex &index) const
{
    return nullptr;
}

angle::Result TextureD3D_Buffer::setBuffer(const gl::Context *context, GLenum internalFormat)
{
    ASSERT(mState.getType() == gl::TextureType::Buffer);
    TexStoragePointer storage;
    storage.reset(mRenderer->createTextureStorageBuffer(mState.getBuffer(), internalFormat,
                                                        mState.getLabel()));
    ANGLE_TRY(setCompleteTexStorage(context, storage.get()));
    storage.release();
    mInternalFormat = internalFormat;
    mImmutable      = false;
    return angle::Result::Continue;
}

angle::Result TextureD3D_Buffer::syncState(const gl::Context *context,
                                           const gl::Texture::DirtyBits &dirtyBits,
                                           gl::Command source)
{
    ASSERT(mState.getType() == gl::TextureType::Buffer);
    if (dirtyBits.test(gl::Texture::DirtyBitType::DIRTY_BIT_IMPLEMENTATION) &&
        mState.getBuffer().get() != nullptr)
    {
        // buffer data have been changed. Buffer data may out of sync
        // give up the old TexStorage, create a new one.
        // this may not efficient, since staging buffer may be patially updated.
        ANGLE_TRY(setBuffer(context, mInternalFormat));
    }
    return angle::Result::Continue;
}

}  // namespace rx
