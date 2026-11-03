//
// Copyright 2002 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// SurfaceImpl.cpp: Implementation of Surface stub method class

#include "libANGLE/renderer/SurfaceImpl.h"

namespace rx
{

SurfaceImpl::SurfaceImpl(const egl::SurfaceState &state) : mState(state) {}

SurfaceImpl::~SurfaceImpl() {}

egl::Error SurfaceImpl::makeCurrent(const gl::Context *context)
{
    return egl::NoError();
}

egl::Error SurfaceImpl::unMakeCurrent(const gl::Context *context)
{
    return egl::NoError();
}

egl::Error SurfaceImpl::prepareSwap(const gl::Context *)
{
    return angle::ResultToEGL(angle::Result::Continue);
}

egl::Error SurfaceImpl::swapWithDamage(const gl::Context *context,
                                       const EGLint *rects,
                                       EGLint n_rects,
                                       SurfaceSwapFeedback *feedback)
{
    UNREACHABLE();
    return egl::Error(EGL_BAD_SURFACE, "swapWithDamage implementation missing.");
}

egl::Error SurfaceImpl::postSubBuffer(const gl::Context *context,
                                      EGLint x,
                                      EGLint y,
                                      EGLint width,
                                      EGLint height)
{
    UNREACHABLE();
    return egl::Error(EGL_BAD_SURFACE, "getMscRate implementation missing.");
}

egl::Error SurfaceImpl::setPresentationTime(EGLnsecsANDROID time)
{
    UNREACHABLE();
    return egl::Error(EGL_BAD_SURFACE, "setPresentationTime implementation missing.");
}

egl::Error SurfaceImpl::querySurfacePointerANGLE(EGLint attribute, void **value)
{
    UNREACHABLE();
    return egl::Error(EGL_BAD_SURFACE, "querySurfacePointerANGLE implementation missing.");
}

egl::Error SurfaceImpl::getSyncValues(EGLuint64KHR *ust, EGLuint64KHR *msc, EGLuint64KHR *sbc)
{
    UNREACHABLE();
    return egl::Error(EGL_BAD_SURFACE, "getSyncValues implementation missing.");
}

egl::Error SurfaceImpl::getMscRate(EGLint *numerator, EGLint *denominator)
{
    UNREACHABLE();
    return egl::Error(EGL_BAD_SURFACE, "getMscRate implementation missing.");
}

void SurfaceImpl::setFixedWidth(EGLint width)
{
    UNREACHABLE();
}

void SurfaceImpl::setFixedHeight(EGLint height)
{
    UNREACHABLE();
}

void SurfaceImpl::setSwapBehavior(EGLenum behavior)
{
    // Behavior is ignored for backends that don't implement it.
    UNIMPLEMENTED();
}

void SurfaceImpl::setTimestampsEnabled(bool enabled)
{
    UNREACHABLE();
}

const angle::Format *SurfaceImpl::getClientBufferTextureColorFormat() const
{
    UNREACHABLE();
    return nullptr;
}

egl::SupportedCompositorTimings SurfaceImpl::getSupportedCompositorTimings() const
{
    UNREACHABLE();
    return egl::SupportedCompositorTimings();
}

egl::Error SurfaceImpl::getCompositorTiming(EGLint numTimestamps,
                                            const EGLint *names,
                                            EGLnsecsANDROID *values) const
{
    UNREACHABLE();
    return egl::Error(EGL_BAD_DISPLAY);
}

egl::Error SurfaceImpl::getNextFrameId(EGLuint64KHR *frameId) const
{
    UNREACHABLE();
    return egl::Error(EGL_BAD_DISPLAY);
}

egl::SupportedTimestamps SurfaceImpl::getSupportedTimestamps() const
{
    UNREACHABLE();
    return egl::SupportedTimestamps();
}

egl::Error SurfaceImpl::getFrameTimestamps(EGLuint64KHR frameId,
                                           EGLint numTimestamps,
                                           const EGLint *timestamps,
                                           EGLnsecsANDROID *values) const
{
    UNREACHABLE();
    return egl::Error(EGL_BAD_DISPLAY);
}

angle::Result SurfaceImpl::ensureSizeResolved(const gl::Context *context)
{
    return angle::Result::Continue;
}

egl::Error SurfaceImpl::getUserSize(const egl::ThreadSafeDisplay *display,
                                    EGLint *width,
                                    EGLint *height) const
{
    // Override getUserSize() if it is not the same as getSize() or if its usage is suboptimal.
    // In case of override use "final" for both methods when possible to prevent accidental bugs.
    const gl::Extents size = getSize();
    ASSERT(size.depth == 1);
    if (width != nullptr)
    {
        *width = size.width;
    }
    if (height != nullptr)
    {
        *height = size.height;
    }
    return egl::NoError();
}

EGLint SurfaceImpl::isPostSubBufferSupported() const
{
    UNREACHABLE();
    return EGL_FALSE;
}

egl::Error SurfaceImpl::getBufferAge(const gl::Context *context, EGLint *age)
{
    *age = 0;
    return egl::NoError();
}

egl::Error SurfaceImpl::setAutoRefreshEnabled(bool enabled)
{
    return egl::Error(EGL_BAD_MATCH);
}

egl::Error SurfaceImpl::lockSurface(const egl::ThreadSafeDisplay *display,
                                    EGLint usageHint,
                                    bool preservePixels,
                                    uint8_t **bufferPtrOut,
                                    EGLint *bufferPitchOut)
{
    UNREACHABLE();
    return egl::Error(EGL_BAD_MATCH);
}

egl::Error SurfaceImpl::unlockSurface(const egl::ThreadSafeDisplay *display, bool preservePixels)
{
    UNREACHABLE();
    return egl::Error(EGL_BAD_MATCH);
}

EGLint SurfaceImpl::origin() const
{
    return EGL_LOWER_LEFT_KHR;
}

egl::Error SurfaceImpl::setRenderBuffer(EGLint renderBuffer)
{
    return egl::NoError();
}

egl::Error SurfaceImpl::getCompressionRate(const egl::ThreadSafeDisplay *display,
                                           const gl::Context *context,
                                           EGLint *rate)
{
    *rate = EGL_SURFACE_COMPRESSION_FIXED_RATE_NONE_EXT;
    return egl::NoError();
}

bool SurfaceImpl::supportsSingleRenderBuffer() const
{
    return false;
}

void SurfaceImpl::attachToFramebuffer(const gl::Context *context, gl::Framebuffer *framebuffer) {}
void SurfaceImpl::detachFromFramebuffer(gl::Framebuffer *framebuffer) {}

}  // namespace rx
