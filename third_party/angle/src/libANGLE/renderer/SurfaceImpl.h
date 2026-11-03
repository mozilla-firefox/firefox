//
// Copyright 2014 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// SurfaceImpl.h: Implementation methods of egl::Surface

#ifndef LIBANGLE_RENDERER_SURFACEIMPL_H_
#define LIBANGLE_RENDERER_SURFACEIMPL_H_

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "common/angleutils.h"
#include "libANGLE/Error.h"
#include "libANGLE/FramebufferAttachment.h"
#include "libANGLE/renderer/FramebufferAttachmentObjectImpl.h"

namespace angle
{
struct Format;
}

namespace gl
{
class Context;
class FramebufferState;
}  // namespace gl

namespace egl
{
class ThreadSafeDisplay;
struct Config;
struct SurfaceState;
class Thread;

using SupportedTimestamps        = angle::PackedEnumBitSet<Timestamp>;
using SupportedCompositorTimings = angle::PackedEnumBitSet<CompositorTiming>;
}  // namespace egl

namespace rx
{
struct SurfaceSwapFeedback
{
    bool swapChainImageChanged = false;
};

class SurfaceImpl : public FramebufferAttachmentObjectImpl
{
  public:
    SurfaceImpl(const egl::SurfaceState &surfaceState);
    ~SurfaceImpl() override;
    virtual void destroy(const egl::ThreadSafeDisplay *display) {}

    virtual egl::Error initialize(const egl::ThreadSafeDisplay *display) = 0;
    virtual egl::Error makeCurrent(const gl::Context *context);
    virtual egl::Error unMakeCurrent(const gl::Context *context);
    virtual egl::Error prepareSwap(const gl::Context *);
    virtual egl::Error swap(const gl::Context *context, SurfaceSwapFeedback *feedback) = 0;
    virtual egl::Error swapWithDamage(const gl::Context *context,
                                      const EGLint *rects,
                                      EGLint n_rects,
                                      SurfaceSwapFeedback *feedback);
    virtual egl::Error postSubBuffer(const gl::Context *context,
                                     EGLint x,
                                     EGLint y,
                                     EGLint width,
                                     EGLint height);
    virtual egl::Error setPresentationTime(EGLnsecsANDROID time);
    virtual egl::Error querySurfacePointerANGLE(EGLint attribute, void **value);
    virtual egl::Error bindTexImage(const gl::Context *context,
                                    gl::Texture *texture,
                                    EGLint buffer)                                = 0;
    virtual egl::Error releaseTexImage(const gl::Context *context, EGLint buffer) = 0;
    virtual egl::Error getSyncValues(EGLuint64KHR *ust, EGLuint64KHR *msc, EGLuint64KHR *sbc);
    virtual egl::Error getMscRate(EGLint *numerator, EGLint *denominator);
    virtual void setSwapInterval(const egl::ThreadSafeDisplay *display, EGLint interval) = 0;
    virtual void setFixedWidth(EGLint width);
    virtual void setFixedHeight(EGLint height);
    virtual void setSwapBehavior(EGLenum behavior);

    // Explicitly resolves surface size to use before state synchronization (e.g. validation). This
    // must be implemented in a thread safe way since it may get called from multiple threads
    // without lock.
    virtual angle::Result ensureSizeResolved(const gl::Context *context);
    // size can change with client window resizing
    // Size must be resolved before the call either during state synchronization or explicitly.
    virtual gl::Extents getSize() const = 0;
    // Unresolved Surface size until render target is first accessed (e.g. after draw).
    virtual egl::Error getUserSize(const egl::ThreadSafeDisplay *display,
                                   EGLint *width,
                                   EGLint *height) const;

    virtual EGLint isPostSubBufferSupported() const;
    virtual EGLint getSwapBehavior() const = 0;

    virtual void attachToFramebuffer(const gl::Context *context, gl::Framebuffer *framebuffer);
    virtual void detachFromFramebuffer(gl::Framebuffer *framebuffer);

    // Used to query color format from pbuffers created from D3D and WebGPU textures.
    virtual const angle::Format *getClientBufferTextureColorFormat() const;

    // EGL_ANDROID_get_frame_timestamps
    virtual void setTimestampsEnabled(bool enabled);
    virtual egl::SupportedCompositorTimings getSupportedCompositorTimings() const;
    virtual egl::Error getCompositorTiming(EGLint numTimestamps,
                                           const EGLint *names,
                                           EGLnsecsANDROID *values) const;
    virtual egl::Error getNextFrameId(EGLuint64KHR *frameId) const;
    virtual egl::SupportedTimestamps getSupportedTimestamps() const;
    virtual egl::Error getFrameTimestamps(EGLuint64KHR frameId,
                                          EGLint numTimestamps,
                                          const EGLint *timestamps,
                                          EGLnsecsANDROID *values) const;
    virtual egl::Error getBufferAge(const gl::Context *context, EGLint *age);

    // EGL_ANDROID_front_buffer_auto_refresh
    virtual egl::Error setAutoRefreshEnabled(bool enabled);

    // EGL_KHR_lock_surface3
    virtual egl::Error lockSurface(const egl::ThreadSafeDisplay *display,
                                   EGLint usageHint,
                                   bool preservePixels,
                                   uint8_t **bufferPtrOut,
                                   EGLint *bufferPitchOut);
    virtual egl::Error unlockSurface(const egl::ThreadSafeDisplay *display, bool preservePixels);
    virtual EGLint origin() const;

    virtual egl::Error setRenderBuffer(EGLint renderBuffer);

    virtual egl::Error getCompressionRate(const egl::ThreadSafeDisplay *display,
                                          const gl::Context *context,
                                          EGLint *rate);

    virtual bool supportsSingleRenderBuffer() const;

  protected:
    const egl::SurfaceState &mState;
};

}  // namespace rx

#endif  // LIBANGLE_RENDERER_SURFACEIMPL_H_
