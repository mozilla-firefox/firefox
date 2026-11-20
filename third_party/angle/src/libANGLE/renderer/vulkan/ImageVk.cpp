//
// Copyright 2016 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// ImageVk.cpp:
//    Implements the class methods for ImageVk.
//

#include "libANGLE/renderer/vulkan/ImageVk.h"

#include "common/debug.h"
#include "libANGLE/Context.h"
#include "libANGLE/Display.h"
#include "libANGLE/renderer/vulkan/ContextVk.h"
#include "libANGLE/renderer/vulkan/DisplayVk.h"
#include "libANGLE/renderer/vulkan/RenderbufferVk.h"
#include "libANGLE/renderer/vulkan/TextureVk.h"
#include "libANGLE/renderer/vulkan/vk_utils.h"

namespace rx
{

ImageVk::ImageVk(const egl::ImageState &state, const gl::Context *context)
    : ImageImpl(state), mOwnsImage(false), mImage(nullptr), mContext(context)
{}

ImageVk::~ImageVk() {}

void ImageVk::onDestroy(const egl::Display *display)
{
    DisplayVk *displayVk   = vk::GetImpl(display);
    vk::Renderer *renderer = displayVk->getRenderer();

    if (mImage != nullptr && mOwnsImage)
    {
        // TODO: We need to handle the case that EGLImage used in two context that aren't shared.
        // https://issuetracker.google.com/169868803
        mImage->releaseImage(renderer);
        mImage->releaseStagedUpdates(renderer);
        SafeDelete(mImage);
    }
    else if (egl::IsExternalImageTarget(mState.target))
    {
        ASSERT(mState.source != nullptr);
        ExternalImageSiblingVk *externalImageSibling =
            GetImplAs<ExternalImageSiblingVk>(GetAs<egl::ExternalImageSibling>(mState.source));
        externalImageSibling->release(renderer);
        mImage = nullptr;

        // This is called as a special case where resources may be allocated by the caller, without
        // the caller ever issuing a draw command to free them. Specifically, SurfaceFlinger
        // optimistically allocates EGLImages that it may never draw to.
        renderer->cleanupGarbage(nullptr);
    }
}

egl::Error ImageVk::initialize(const egl::Display *display)
{
    if (mContext != nullptr)
    {
        ContextVk *contextVk = vk::GetImpl(mContext);
        ANGLE_TRY(ResultToEGL(contextVk->getShareGroup()->lockDefaultContextsPriority(contextVk)));
    }

    if (egl::IsTextureTarget(mState.target))
    {
        ASSERT(mContext != nullptr);
        ContextVk *contextVk = vk::GetImpl(mContext);
        TextureVk *textureVk = GetImplAs<TextureVk>(GetAs<gl::Texture>(mState.source));

        // Make sure the texture uses renderable format
        TextureUpdateResult updateResult = TextureUpdateResult::ImageUnaffected;
        ANGLE_TRY(ResultToEGL(textureVk->ensureRenderable(contextVk, &updateResult)));

        // Make sure the texture has created its backing storage
        ANGLE_TRY(ResultToEGL(textureVk->ensureImageAndReadViewsInitialized(
            contextVk, ImageMipLevels::EnabledLevels)));

        mImage = &textureVk->getImage();

        // The staging buffer for a texture source should already be initialized

        mOwnsImage = false;
    }
    else
    {
        if (egl::IsRenderbufferTarget(mState.target))
        {
            RenderbufferVk *renderbufferVk =
                GetImplAs<RenderbufferVk>(GetAs<gl::Renderbuffer>(mState.source));
            mImage = renderbufferVk->getImage();

            ASSERT(mContext != nullptr);
        }
        else if (egl::IsExternalImageTarget(mState.target))
        {
            const ExternalImageSiblingVk *externalImageSibling =
                GetImplAs<ExternalImageSiblingVk>(GetAs<egl::ExternalImageSibling>(mState.source));
            mImage = externalImageSibling->getImage();

            ASSERT(mContext == nullptr);
        }
        else
        {
            UNREACHABLE();
            return egl::Error(EGL_BAD_ACCESS);
        }

        mOwnsImage = false;
    }

    // mContext is no longer needed, make sure it's not used by accident.
    mContext = nullptr;

    return egl::NoError();
}

angle::Result ImageVk::orphan(const gl::Context *context, egl::ImageSibling *sibling)
{
    if (sibling == mState.source)
    {
        if (egl::IsTextureTarget(mState.target))
        {
            TextureVk *textureVk = GetImplAs<TextureVk>(GetAs<gl::Texture>(mState.source));
            ASSERT(mImage == &textureVk->getImage());
            textureVk->releaseOwnershipOfImage(context);
            mOwnsImage = true;
        }
        else if (egl::IsRenderbufferTarget(mState.target))
        {
            RenderbufferVk *renderbufferVk =
                GetImplAs<RenderbufferVk>(GetAs<gl::Renderbuffer>(mState.source));
            ASSERT(mImage == renderbufferVk->getImage());
            renderbufferVk->releaseOwnershipOfImage(context);
            mOwnsImage = true;
        }
        else
        {
            ANGLE_VK_UNREACHABLE(vk::GetImpl(context));
            return angle::Result::Stop;
        }
    }

    return angle::Result::Continue;
}

egl::Error ImageVk::exportVkImage(void *vkImage, void *vkImageCreateInfo)
{
    *reinterpret_cast<VkImage *>(vkImage) = mImage->getImage().getHandle();
    auto *info = reinterpret_cast<VkImageCreateInfo *>(vkImageCreateInfo);
    *info      = mImage->getVkImageCreateInfo();
    return egl::NoError();
}

bool ImageVk::isFixedRatedCompression(const gl::Context *context)
{
    ContextVk *contextVk   = vk::GetImpl(context);
    vk::Renderer *renderer = contextVk->getRenderer();

    ASSERT(mImage != nullptr && mImage->valid());
    ASSERT(renderer->getFeatures().supportsImageCompressionControl.enabled);

    VkImageCompressionPropertiesEXT compressionProperties = {};
    compressionProperties.sType               = VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_PROPERTIES_EXT;
    VkSubresourceLayout2 subresourceLayout    = {};
    subresourceLayout.sType                   = VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2_EXT;
    subresourceLayout.pNext                   = &compressionProperties;

    mImage->getImageSubresourceLayout(renderer, &subresourceLayout);

    return compressionProperties.imageCompressionFixedRateFlags >
           VK_IMAGE_COMPRESSION_FIXED_RATE_NONE_EXT;
}

}  // namespace rx
