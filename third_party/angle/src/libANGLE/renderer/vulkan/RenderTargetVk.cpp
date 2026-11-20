//
// Copyright 2016 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// RenderTargetVk:
//   Wrapper around a Vulkan renderable resource, using an ImageView.
//

#include "libANGLE/renderer/vulkan/RenderTargetVk.h"

#include "libANGLE/renderer/vulkan/ContextVk.h"
#include "libANGLE/renderer/vulkan/TextureVk.h"
#include "libANGLE/renderer/vulkan/vk_format_utils.h"
#include "libANGLE/renderer/vulkan/vk_helpers.h"
#include "libANGLE/renderer/vulkan/vk_resource.h"

namespace rx
{

RenderTargetVk::RenderTargetVk()
{
    reset();
}

RenderTargetVk::~RenderTargetVk()
{
    ASSERT(mFramebufferCacheManager.empty());
}

RenderTargetVk::RenderTargetVk(RenderTargetVk &&other)
    : mImage(other.mImage),
      mImageViews(other.mImageViews),
      mResolveImage(other.mResolveImage),
      mResolveImageViews(other.mResolveImageViews),
      mLevelIndexGL(other.mLevelIndexGL),
      mLayerIndex(other.mLayerIndex),
      mLayerCount(other.mLayerCount),
      mFramebufferCacheManager(other.mFramebufferCacheManager)
{
    other.reset();
}

void RenderTargetVk::init(vk::ImageHelper *image,
                          vk::ImageViewHelper *imageViews,
                          vk::ImageHelper *resolveImage,
                          vk::ImageViewHelper *resolveImageViews,
                          gl::OwnerLevel levelIndexGL,
                          gl::OwnerLayer layerIndex,
                          uint32_t layerCount,
                          RenderTargetTransience transience)
{
    // If this comes from SurfaceVk, the usage bit will be 0 (image init are deferred).
    ASSERT(image->getUsage() == 0 ||
           (image->getUsage() & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0);
    // image and resolveImage can't both uses tile memory.
    ASSERT(image == nullptr || resolveImage == nullptr || !image->useTileMemory() ||
           !resolveImage->useTileMemory());
    mImage              = image;
    mImageViews         = imageViews;
    mResolveImage       = resolveImage;
    mResolveImageViews  = resolveImageViews;
    mLevelIndexGL       = levelIndexGL;
    mLayerIndex         = layerIndex;
    mLayerCount         = layerCount;

    mTransience = transience;
}

void RenderTargetVk::reset()
{
    mImage              = nullptr;
    mImageViews         = nullptr;
    mResolveImage       = nullptr;
    mResolveImageViews  = nullptr;
    mLevelIndexGL       = gl::OwnerLevel(0);
    mLayerIndex         = gl::OwnerLayer(0);
    mLayerCount         = 0;
}

vk::ImageOrBufferViewSubresourceSerial RenderTargetVk::getSubresourceSerialImpl(
    vk::ImageViewHelper *imageViews) const
{
    ASSERT(imageViews);
    ASSERT(mLayerIndex.get() < std::numeric_limits<uint16_t>::max());
    ASSERT(mLevelIndexGL.get() < std::numeric_limits<uint16_t>::max());

    vk::LayerMode layerMode = vk::GetLayerMode(*mImage, mLayerCount);
    vk::ImageOrBufferViewSubresourceSerial imageViewSerial =
        imageViews->getSubresourceSerial(mLevelIndexGL, 1, mLayerIndex, layerMode);
    return imageViewSerial;
}

vk::ImageOrBufferViewSubresourceSerial RenderTargetVk::getDrawSubresourceSerial() const
{
    return getSubresourceSerialImpl(mImageViews);
}

vk::ImageOrBufferViewSubresourceSerial RenderTargetVk::getResolveSubresourceSerial() const
{
    return getSubresourceSerialImpl(mResolveImageViews);
}

void RenderTargetVk::onColorDraw(ContextVk *contextVk,
                                 uint32_t framebufferLayerCount,
                                 vk::PackedAttachmentIndex packedAttachmentIndex)
{
    ASSERT(!mImage->getActualFormat().hasDepthOrStencilBits());
    ASSERT(framebufferLayerCount <= mLayerCount);

    contextVk->onColorDraw(mLevelIndexGL, mLayerIndex, framebufferLayerCount, mImage, mResolveImage,
                           packedAttachmentIndex);

    // Multisampled render to texture framebuffers cannot be layered.
    ASSERT(mResolveImage == nullptr || framebufferLayerCount == 1);
}

void RenderTargetVk::onColorResolve(ContextVk *contextVk,
                                    uint32_t framebufferLayerCount,
                                    size_t readColorIndexGL,
                                    const vk::ImageView &view)
{
    ASSERT(!mImage->getActualFormat().hasDepthOrStencilBits());
    ASSERT(framebufferLayerCount <= mLayerCount);
    ASSERT(mResolveImage == nullptr);

    // The currently open render pass is from the read framebuffer.  This is the draw framebuffer's
    // render target.  Ask the context to add this image as the resolve attachment to the read
    // framebuffer's render pass, at the given color index.
    contextVk->onColorResolve(mLevelIndexGL, mLayerIndex, framebufferLayerCount, mImage,
                              view.getHandle(), readColorIndexGL);
}

void RenderTargetVk::onDepthStencilDraw(ContextVk *contextVk, uint32_t framebufferLayerCount)
{
    const angle::Format &format = mImage->getActualFormat();
    ASSERT(format.hasDepthOrStencilBits());
    ASSERT(framebufferLayerCount <= mLayerCount);

    contextVk->onDepthStencilDraw(mLevelIndexGL, mLayerIndex, framebufferLayerCount, mImage,
                                  mResolveImage);
}

void RenderTargetVk::onDepthStencilResolve(ContextVk *contextVk,
                                           uint32_t framebufferLayerCount,
                                           VkImageAspectFlags aspects,
                                           const vk::ImageView &view)
{
    ASSERT(mImage->getActualFormat().hasDepthOrStencilBits());
    ASSERT(framebufferLayerCount <= mLayerCount);
    ASSERT(mResolveImage == nullptr);

    contextVk->onDepthStencilResolve(mLevelIndexGL, mLayerIndex, framebufferLayerCount, aspects,
                                     mImage, view.getHandle());
}

vk::ImageHelper &RenderTargetVk::getImageForRenderPass()
{
    ASSERT(mImage && mImage->valid());
    return *mImage;
}

const vk::ImageHelper &RenderTargetVk::getImageForRenderPass() const
{
    ASSERT(mImage && mImage->valid());
    return *mImage;
}

vk::ImageHelper &RenderTargetVk::getResolveImageForRenderPass()
{
    ASSERT(mResolveImage && mResolveImage->valid());
    return *mResolveImage;
}

const vk::ImageHelper &RenderTargetVk::getResolveImageForRenderPass() const
{
    ASSERT(mResolveImage && mResolveImage->valid());
    return *mResolveImage;
}

angle::Result RenderTargetVk::getImageViewImpl(ContextVk *contextVk,
                                               const vk::ImageHelper &image,
                                               vk::ImageViewHelper *imageViews,
                                               const vk::ImageView **imageViewOut) const
{
    ASSERT(image.valid() && imageViews);
    vk::LevelIndex levelVk = image.toVkLevel(getLevelIndexForImage(image));
    if (mLayerCount == 1)
    {
        return imageViews->getLevelLayerDrawImageView(contextVk, image, levelVk, mLayerIndex,
                                                      imageViewOut);
    }

    // Layered render targets view the whole level or a handful of layers in case of multiview.
    return imageViews->getLevelDrawImageView(contextVk, image, levelVk, mLayerIndex, mLayerCount,
                                             imageViewOut);
}

angle::Result RenderTargetVk::getImageView(ContextVk *contextVk,
                                           const vk::ImageView **imageViewOut) const
{
    ASSERT(mImage);
    return getImageViewImpl(contextVk, *mImage, mImageViews, imageViewOut);
}

angle::Result RenderTargetVk::getImageViewWithColorspace(ContextVk *contextVk,
                                                         gl::SrgbWriteControlMode mode,
                                                         const vk::ImageView **imageViewOut) const
{
    ASSERT(mImage);
    mImageViews->updateSrgbWriteControlMode(mImage->getActualFormat(), mode);
    return getImageViewImpl(contextVk, *mImage, mImageViews, imageViewOut);
}

angle::Result RenderTargetVk::getResolveImageView(ContextVk *contextVk,
                                                  const vk::ImageView **imageViewOut) const
{
    ASSERT(mResolveImage);
    return getImageViewImpl(contextVk, *mResolveImage, mResolveImageViews, imageViewOut);
}

angle::Result RenderTargetVk::getDepthOrStencilImageView(ContextVk *contextVk,
                                                         VkImageAspectFlagBits aspect,
                                                         const vk::ImageView **imageViewOut) const
{
    ASSERT(mImage);
    return getDepthOrStencilImageViewImpl(contextVk, *mImage, mImageViews, aspect, imageViewOut);
}

angle::Result RenderTargetVk::getDepthOrStencilImageViewForCopy(
    ContextVk *contextVk,
    VkImageAspectFlagBits aspect,
    const vk::ImageView **imageViewOut) const
{
    return isResolveImageOwnerOfData()
               ? getResolveDepthOrStencilImageView(contextVk, aspect, imageViewOut)
               : getDepthOrStencilImageView(contextVk, aspect, imageViewOut);
}

angle::Result RenderTargetVk::getResolveDepthOrStencilImageView(
    ContextVk *contextVk,
    VkImageAspectFlagBits aspect,
    const vk::ImageView **imageViewOut) const
{
    ASSERT(mResolveImage);
    return getDepthOrStencilImageViewImpl(contextVk, *mResolveImage, mResolveImageViews, aspect,
                                          imageViewOut);
}

angle::Result RenderTargetVk::getDepthOrStencilImageViewImpl(
    ContextVk *contextVk,
    const vk::ImageHelper &image,
    vk::ImageViewHelper *imageViews,
    VkImageAspectFlagBits aspect,
    const vk::ImageView **imageViewOut) const
{
    // If the image has only one aspect, the usual view is sufficient.
    if (image.getAspectFlags() == aspect)
    {
        return getImageViewImpl(contextVk, image, imageViews, imageViewOut);
    }

    // Otherwise, for images with both the depth and stencil aspects, need to create special views
    // that select only one such aspect.
    ASSERT(image.valid() && imageViews);
    vk::LevelIndex levelVk = image.toVkLevel(getLevelIndexForImage(image));
    if (mLayerCount == 1)
    {
        return imageViews->getLevelLayerDepthOrStencilImageView(contextVk, image, levelVk,
                                                                mLayerIndex, aspect, imageViewOut);
    }

    // Layered render targets view the whole level or a handful of layers in case of multiview.
    return imageViews->getLevelDepthOrStencilImageView(contextVk, image, levelVk, mLayerIndex,
                                                       mLayerCount, aspect, imageViewOut);
}

bool RenderTargetVk::isResolveImageOwnerOfData() const
{
    // If there's a resolve attachment and the image itself is transient, it's the resolve
    // attachment that owns the data, so all non-render-pass accesses to the render target data
    // should go through the resolve attachment.
    return isImageTransient();
}

vk::ImageHelper *RenderTargetVk::getOwnerOfData() const
{
    return isResolveImageOwnerOfData() ? mResolveImage : mImage;
}

angle::Result RenderTargetVk::getCopyImageView(ContextVk *contextVk,
                                               const vk::ImageView **imageViewOut) const
{
    const vk::ImageViewHelper *imageViews =
        isResolveImageOwnerOfData() ? mResolveImageViews : mImageViews;

    // If the source of render target is a texture or renderbuffer, this will always be valid.  This
    // is also where 3D or 2DArray images could be the source of the render target.
    if (imageViews->hasCopyImageView())
    {
        *imageViewOut = &imageViews->getCopyImageView();
        return angle::Result::Continue;
    }

    // Otherwise, this must come from the surface, in which case the image is 2D, so the image view
    // used to draw is just as good for fetching.  If resolve attachment is present, fetching is
    // done from that.
    return isResolveImageOwnerOfData() ? getResolveImageView(contextVk, imageViewOut)
                                       : getImageView(contextVk, imageViewOut);
}

angle::FormatID RenderTargetVk::getImageActualFormatID() const
{
    ASSERT(mImage && mImage->valid());
    return mImage->getActualFormatID();
}

angle::FormatID RenderTargetVk::getImageIntendedFormatID() const
{
    ASSERT(mImage && mImage->valid());
    return mImage->getIntendedFormatID();
}

const angle::Format &RenderTargetVk::getImageActualFormat() const
{
    ASSERT(mImage && mImage->valid());
    return mImage->getActualFormat();
}

const angle::Format &RenderTargetVk::getImageIntendedFormat() const
{
    ASSERT(mImage && mImage->valid());
    return mImage->getIntendedFormat();
}

gl::Extents RenderTargetVk::getExtents() const
{
    ASSERT(mImage && mImage->valid());
    vk::LevelIndex levelVk = mImage->toVkLevel(mLevelIndexGL);
    return mImage->getLevelExtents2D(levelVk);
}

gl::Extents RenderTargetVk::getRotatedExtents() const
{
    ASSERT(mImage && mImage->valid());
    vk::LevelIndex levelVk = mImage->toVkLevel(mLevelIndexGL);
    return mImage->getRotatedLevelExtents2D(levelVk);
}

gl::OwnerLevel RenderTargetVk::getLevelIndexForImage(const vk::ImageHelper &image) const
{
    return (getOwnerOfData()->getImageSerial() == image.getImageSerial()) ? mLevelIndexGL
                                                                          : gl::OwnerLevel(0);
}

void RenderTargetVk::updateSwapchainImage(vk::ImageHelper *image,
                                          vk::ImageViewHelper *imageViews,
                                          vk::ImageHelper *resolveImage,
                                          vk::ImageViewHelper *resolveImageViews)
{
    ASSERT(image && image->valid() && imageViews);
    ASSERT(mLevelIndexGL == gl::OwnerLevel(0));
    ASSERT(mLayerIndex == gl::OwnerLayer(0));
    mImage             = image;
    mImageViews        = imageViews;
    mResolveImage      = resolveImage;
    mResolveImageViews = resolveImageViews;
    mLayerCount        = 1;
}

vk::ImageHelper &RenderTargetVk::getImageForCopy() const
{
    ASSERT(mImage && mImage->valid() && (mResolveImage == nullptr || mResolveImage->valid()));
    return *getOwnerOfData();
}

vk::ImageHelper &RenderTargetVk::getImageForWrite() const
{
    ASSERT(mImage && mImage->valid() && (mResolveImage == nullptr || mResolveImage->valid()));
    return *getOwnerOfData();
}

angle::Result RenderTargetVk::flushStagedUpdates(ContextVk *contextVk,
                                                 vk::ClearValuesArray *deferredClears,
                                                 uint32_t deferredClearIndex,
                                                 uint32_t framebufferLayerCount)
{
    ASSERT(mImage->valid() && (!isResolveImageOwnerOfData() || mResolveImage->valid()));
    ASSERT(framebufferLayerCount != 0);

    vk::ImageHelper *image = getOwnerOfData();

    // All updates should be staged on the image that owns the data as the source of truth.  With
    // multisampled-render-to-texture framebuffers, that is the resolve image.  In that case, even
    // though deferred clears set the loadOp of the transient multisampled image, the clears
    // themselves are staged on the resolve image.  The |flushSingleSubresourceStagedUpdates| call
    // below will either flush all staged updates to the resolve image, or if the only staged update
    // is a clear, it will accumulate it in the |deferredClears| array.  Later, when the render pass
    // is started, the deferred clears are applied to the transient multisampled image.
    ASSERT(!isResolveImageOwnerOfData() ||
           !mImage->hasStagedUpdatesForSubresource(mLevelIndexGL, mLayerIndex, mLayerCount));
    ASSERT(isResolveImageOwnerOfData() || mResolveImage == nullptr ||
           !mResolveImage->hasStagedUpdatesForSubresource(mLevelIndexGL, mLayerIndex, mLayerCount));

    if (!image->hasStagedUpdatesForSubresource(mLevelIndexGL, mLayerIndex, framebufferLayerCount))
    {
        return angle::Result::Continue;
    }

    return image->flushSingleSubresourceStagedUpdates(contextVk, mLevelIndexGL, mLayerIndex,
                                                      framebufferLayerCount, deferredClears,
                                                      deferredClearIndex);
}

bool RenderTargetVk::hasDefinedContent() const
{
    vk::ImageHelper *image = getOwnerOfData();
    return image->hasSubresourceDefinedContent(mLevelIndexGL, mLayerIndex, mLayerCount);
}

bool RenderTargetVk::hasDefinedStencilContent() const
{
    vk::ImageHelper *image = getOwnerOfData();
    return image->hasSubresourceDefinedStencilContent(mLevelIndexGL, mLayerIndex, mLayerCount);
}

void RenderTargetVk::invalidateEntireContent(ContextVk *contextVk,
                                             bool *preferToKeepContentsDefinedOut)
{
    vk::ImageHelper *image = getOwnerOfData();
    image->invalidateSubresourceContent(contextVk, mLevelIndexGL, mLayerIndex, mLayerCount,
                                        preferToKeepContentsDefinedOut);
}

void RenderTargetVk::invalidateEntireStencilContent(ContextVk *contextVk,
                                                    bool *preferToKeepContentsDefinedOut)
{
    vk::ImageHelper *image = getOwnerOfData();
    image->invalidateSubresourceStencilContent(contextVk, mLevelIndexGL, mLayerIndex, mLayerCount,
                                               preferToKeepContentsDefinedOut);
}

gl::OwnerImageIndex RenderTargetVk::getImageIndexForClear(uint32_t layerCount) const
{
    // Determine the GL type from the Vk Image properties.
    if (mImage->getLayerCount() > 1)
    {
        // We don't need to distinguish 2D array and cube.
        return gl::OwnerImageIndex::Make2DArrayRange(mLevelIndexGL, mLayerIndex, layerCount);
    }
    if (mImage->getType() == VK_IMAGE_TYPE_3D)
    {
        // The depth slices of the 3D image are treated as layers for this purpose.
        return gl::OwnerImageIndex::MakeFromType(gl::TextureType::_3D, mLevelIndexGL, mLayerIndex,
                                                 layerCount);
    }

    ASSERT(mLayerIndex == gl::OwnerLayer(0));
    ASSERT(mLayerCount == 1);
    ASSERT(layerCount == 1);
    return gl::OwnerImageIndex::Make2D(mLevelIndexGL);
}
}  // namespace rx
