//
// Copyright 2018 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// ImageEGL.cpp: Implements the rx::ImageEGL class.

#include "libANGLE/renderer/gl/egl/ImageEGL.h"

#include "common/utilities.h"
#include "libANGLE/Context.h"
#include "libANGLE/Display.h"
#include "libANGLE/renderer/gl/FunctionsGL.h"
#include "libANGLE/renderer/gl/RenderbufferGL.h"
#include "libANGLE/renderer/gl/StateManagerGL.h"
#include "libANGLE/renderer/gl/TextureGL.h"
#include "libANGLE/renderer/gl/egl/ContextEGL.h"
#include "libANGLE/renderer/gl/egl/ExternalImageSiblingEGL.h"
#include "libANGLE/renderer/gl/egl/FunctionsEGL.h"

namespace rx
{

ImageEGL::ImageEGL(const egl::ImageState &state,
                   const gl::Context *context,
                   EGLenum target,
                   const egl::AttributeMap &attribs,
                   const FunctionsEGL *egl)
    : ImageGL(state),
      mEGL(egl),
      mGLContext(context),
      mContext(EGL_NO_CONTEXT),
      mTarget(target),
      mPreserveImage(false)
{
    if (context)
    {
        mContext = GetImplAs<ContextEGL>(context)->getContext();
    }
    mImage         = std::make_shared<EGLImage>(EGL_NO_IMAGE);
    mPreserveImage = attribs.get(EGL_IMAGE_PRESERVED, EGL_FALSE) == EGL_TRUE;
}

ImageEGL::~ImageEGL()
{
    if (mImage)
    {
        mEGL->destroyImageKHR(*mImage);
        mImage.reset();
    }
}

egl::Error ImageEGL::initialize(const egl::Display *display)
{
    EGLClientBuffer buffer = nullptr;
    angle::FastVector<EGLint, 8> attributes;

    if (egl::IsTextureTarget(mTarget))
    {
        gl::Texture *texture = GetAs<gl::Texture>(mState.source);
        TextureGL *textureGL = GetImplAs<TextureGL>(texture);
        if (mGLContext != nullptr)
        {
            // Ensure any pending base level change or virtual mipchain sync is applied before
            // querying the native texture ID and level for the EGLImage source.
            (void)textureGL->setBaseLevel(mGLContext, texture->getState().getEffectiveBaseLevel());
        }

        TextureGL::NativeLevelTarget nlt = textureGL->getNativeLevelTarget(
            mState.imageIndex.getTarget(), mState.imageIndex.getLevelIndex().get());

        attributes.push_back(EGL_GL_TEXTURE_LEVEL);
        attributes.push_back(nlt.level);

        if (mState.imageIndex.has3DLayer())
        {
            attributes.push_back(EGL_GL_TEXTURE_ZOFFSET);
            attributes.push_back(mState.imageIndex.getLayerIndex().get());
        }

        // Map the native texture target back to an EGL image target. This helps with mapping
        // between texture types when the virtualizeBaseLevelAndMipchain workaround is enabled
        // (for example, when an unmerged cube map face is stored in a standalone 1-level
        // GL_TEXTURE_2D companion texture rather than a cube map texture).
        mTarget               = gl_egl::TextureTargetToEGLImageTarget(nlt.target);
        buffer                = gl_egl::GLObjectHandleToEGLClientBuffer(nlt.textureID);
        mNativeInternalFormat = textureGL->getNativeInternalFormat(mState.imageIndex.get());
    }
    else if (egl::IsRenderbufferTarget(mTarget))
    {
        const RenderbufferGL *renderbufferGL =
            GetImplAs<RenderbufferGL>(GetAs<gl::Renderbuffer>(mState.source));
        buffer = gl_egl::GLObjectHandleToEGLClientBuffer(renderbufferGL->getRenderbufferID());
        mNativeInternalFormat = renderbufferGL->getNativeInternalFormat();
    }
    else if (egl::IsExternalImageTarget(mTarget))
    {
        const ExternalImageSiblingEGL *externalImageSibling =
            GetImplAs<ExternalImageSiblingEGL>(GetAs<egl::ExternalImageSibling>(mState.source));
        buffer                = externalImageSibling->getBuffer();
        mNativeInternalFormat = externalImageSibling->getFormat().info->sizedInternalFormat;

        // Add any additional attributes this type of image sibline requires
        std::vector<EGLint> tmp_attributes;
        externalImageSibling->getImageCreationAttributes(&tmp_attributes);

        attributes.reserve(attributes.size() + tmp_attributes.size());
        for (EGLint attribute : tmp_attributes)
        {
            attributes.push_back(attribute);
        }
    }
    else
    {
        UNREACHABLE();
    }

    attributes.push_back(EGL_IMAGE_PRESERVED);
    attributes.push_back(mPreserveImage ? EGL_TRUE : EGL_FALSE);

    attributes.push_back(EGL_NONE);

    std::weak_ptr<EGLImage> imageRef(mImage);
    egl::Display::GetCurrentThreadUnlockedTailCall()->add(
        [egl = mEGL, imageRef = std::move(imageRef), context = mContext, target = mTarget, buffer,
         attributes](void *resultOut) {
            std::shared_ptr<EGLImage> image = imageRef.lock();
            // Protect against a racy thread deleting the image before the tail call is run.
            if (image)
            {
                *image = egl->createImageKHR(context, target, buffer, attributes.data());

                // If image creation failed, force the return value of eglCreateImage to
                // EGL_NO_IMAGE. This won't delete this image object but a driver error is
                // unexpected at this point.
                if (*image == EGL_NO_IMAGE)
                {
                    ERR() << "eglCreateImage failed with " << gl::FmtHex(egl->getError());
                    *static_cast<EGLImage *>(resultOut) = EGL_NO_IMAGE;
                }
            }
        });

    return egl::NoError();
}

angle::Result ImageEGL::orphan(const gl::Context *context, egl::ImageSibling *sibling)
{
    // Nothing to do, the native EGLImage will orphan automatically.
    return angle::Result::Continue;
}

angle::Result ImageEGL::setTexture2D(const gl::Context *context,
                                     gl::TextureType type,
                                     TextureGL *texture,
                                     GLenum *outInternalFormat)
{
    const FunctionsGL *functionsGL = GetFunctionsGL(context);
    StateManagerGL *stateManager   = GetStateManagerGL(context);

    // Make sure this texture is bound
    stateManager->bindTexture(type, texture->getTextureID());

    // Bind the image to the texture
    functionsGL->eGLImageTargetTexture2DOES(ToGLenum(type), *mImage);
    *outInternalFormat = mNativeInternalFormat;

    return angle::Result::Continue;
}

angle::Result ImageEGL::setRenderbufferStorage(const gl::Context *context,
                                               RenderbufferGL *renderbuffer,
                                               GLenum *outInternalFormat)
{
    const FunctionsGL *functionsGL = GetFunctionsGL(context);
    StateManagerGL *stateManager   = GetStateManagerGL(context);

    // Make sure this renderbuffer is bound
    stateManager->bindRenderbuffer(GL_RENDERBUFFER, renderbuffer->getRenderbufferID());

    // Bind the image to the renderbuffer
    functionsGL->eGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, *mImage);
    *outInternalFormat = mNativeInternalFormat;

    return angle::Result::Continue;
}

}  // namespace rx
