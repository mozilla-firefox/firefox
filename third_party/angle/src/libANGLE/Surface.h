//
// Copyright 2002 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Surface.h: Defines the egl::Surface class, representing a drawing surface
// such as the client area of a window, including any back buffers.
// Implements EGLSurface and related functionality. [EGL 1.4] section 2.2 page 3.

#ifndef LIBANGLE_SURFACE_H_
#define LIBANGLE_SURFACE_H_

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <utility>

#include <EGL/egl.h>

#include "common/PackedEnums.h"
#include "common/angleutils.h"
#include "libANGLE/AttributeMap.h"
#include "libANGLE/Debug.h"
#include "libANGLE/Error.h"
#include "libANGLE/FramebufferAttachment.h"
#include "libANGLE/HandleAllocator.h"
#include "libANGLE/Image.h"
#include "libANGLE/ObjectMap.h"
#include "libANGLE/RefCountObject.h"
#include "libANGLE/formatutils.h"
#include "libANGLE/renderer/SurfaceImpl.h"

namespace gl
{
class Context;
class Framebuffer;
class Texture;
}  // namespace gl

namespace rx
{
class EGLImplFactory;
}

namespace egl
{
class Display;
class ThreadSafeDisplay;
struct Config;

using SupportedCompositorTiming = angle::PackedEnumBitSet<CompositorTiming>;
using SupportedTimestamps       = angle::PackedEnumBitSet<Timestamp>;

struct SurfaceState final : private angle::NonCopyable
{
    SurfaceState(SurfaceID idIn, const egl::Config *configIn, const AttributeMap &attributesIn);
    ~SurfaceState();

    bool isRobustResourceInitEnabled() const;
    bool hasProtectedContent() const;

    gl::OwnerImageIndex toOwnerIndex(const gl::ImageIndex &index) const
    {
        // Surfaces cannot be an EGL image sibling, so the translation is no-op.
        return ImageSourceAttributes{}.toOwnerIndex(index);
    }

    SurfaceID id;

    EGLLabelKHR label;
    const egl::Config *config;
    AttributeMap attributes;

    bool timestampsEnabled;
    bool autoRefreshEnabled;
    SupportedCompositorTiming supportedCompositorTimings;
    SupportedTimestamps supportedTimestamps;
    bool directComposition;
    EGLenum swapBehavior;
    EGLint swapInterval;
};

class Surface : public LabeledObject, public gl::FramebufferAttachmentObject
{
  public:
    rx::SurfaceImpl *getImplementation() const { return mImplementation; }

    void setLabel(EGLLabelKHR label) override;
    EGLLabelKHR getLabel() const override;

    EGLint getType() const;

    Error initialize(const ThreadSafeDisplay *display);
    Error makeCurrent(const gl::Context *context);
    Error unMakeCurrent(const gl::Context *context);
    Error prepareSwap(const gl::Context *context);
    Error swap(gl::Context *context);
    Error swapWithDamage(gl::Context *context, const EGLint *rects, EGLint n_rects);
    Error postSubBuffer(const gl::Context *context,
                        EGLint x,
                        EGLint y,
                        EGLint width,
                        EGLint height);
    Error setPresentationTime(EGLnsecsANDROID time);
    Error querySurfacePointerANGLE(EGLint attribute, void **value);
    Error bindTexImage(gl::Context *context, gl::Texture *texture, EGLint buffer);
    Error releaseTexImage(const gl::Context *context, EGLint buffer);

    Error getSyncValues(EGLuint64KHR *ust, EGLuint64KHR *msc, EGLuint64KHR *sbc);
    Error getMscRate(EGLint *numerator, EGLint *denominator);

    EGLint isPostSubBufferSupported() const;

    void setRequestedSwapInterval(EGLint interval);
    void setSwapInterval(const ThreadSafeDisplay *display, EGLint interval);
    Error onDestroy(const ThreadSafeDisplay *display);

    void setMipmapLevel(EGLint level);
    void setMultisampleResolve(EGLenum resolve);
    void setRequestedSwapBehavior(EGLenum behavior);
    void setSwapBehavior(EGLenum behavior);

    void setFixedWidth(EGLint width);
    void setFixedHeight(EGLint height);

    const Config *getConfig() const;

    // size can change with client window resizing
    // Size must be resolved before the call either during state synchronization or explicitly.
    gl::Extents getSize() const;
    // Unresolved Surface size until render target is first accessed (e.g. after draw).
    egl::Error getUserSize(const egl::ThreadSafeDisplay *display,
                           EGLint *width,
                           EGLint *height) const;
    EGLint getPixelAspectRatio() const;
    EGLenum getRenderBuffer() const;
    EGLenum getRequestedRenderBuffer() const;
    EGLenum getSwapBehavior() const;
    EGLenum getRequestedSwapBehavior() const;
    TextureFormat getTextureFormat() const;
    EGLenum getTextureTarget() const;
    bool getLargestPbuffer() const;
    EGLenum getGLColorspace() const;
    EGLenum getVGAlphaFormat() const;
    EGLenum getVGColorspace() const;
    bool getMipmapTexture() const;
    EGLint getMipmapLevel() const;
    EGLint getHorizontalResolution() const;
    EGLint getVerticalResolution() const;
    EGLenum getMultisampleResolve() const;
    bool hasProtectedContent() const override;
    bool hasFoveatedRendering() const override { return false; }
    const gl::FoveationState *getFoveationState() const override { return nullptr; }

    // For lock surface buffer
    EGLint getBitmapPitch() const;
    EGLint getBitmapOrigin() const;
    EGLint getRedOffset() const;
    EGLint getGreenOffset() const;
    EGLint getBlueOffset() const;
    EGLint getAlphaOffset() const;
    EGLint getLuminanceOffset() const;
    EGLint getBitmapPixelSize() const;
    EGLAttribKHR getBitmapPointer() const;
    egl::Error getCompressionRate(const egl::ThreadSafeDisplay *display,
                                  const gl::Context *context,
                                  EGLint *rate);
    egl::Error lockSurfaceKHR(const egl::ThreadSafeDisplay *display,
                              const AttributeMap &attributes);
    egl::Error unlockSurfaceKHR(const egl::ThreadSafeDisplay *display);

    bool isLocked() const;
    bool isCurrentOnAnyContext() const
    {
        mMutex.assertLocked();
        return mCurrentRefCount > 0;
    }

    gl::Texture *getBoundTexture() const { return mTexture; }

    EGLint isFixedSize() const;

    // FramebufferAttachmentObject implementation
    // Explicitly resolves surface size to use before state synchronization (e.g. validation).
    angle::Result ensureSizeResolved(const gl::Context *context) const override;
    bool isAttachmentSpecified(const gl::ImageIndex &imageIndex) const override;
    gl::Extents getAttachmentSize(const gl::ImageIndex &imageIndex) const override;
    gl::Format getAttachmentFormat(GLenum binding, const gl::ImageIndex &imageIndex) const override;
    GLsizei getAttachmentSamples(const gl::ImageIndex &imageIndex) const override;
    bool isRenderable(const gl::Context *context,
                      GLenum binding,
                      const gl::ImageIndex &imageIndex) const override;
    bool isYUV() const override;
    bool isExternalImageWithoutIndividualSync() const override;
    bool hasFrontBufferUsage() const override;

    void onAttach(const gl::Context *context, rx::UniqueSerial framebufferSerial) override {}
    void onDetach(const gl::Context *context, rx::UniqueSerial framebufferSerial) override {}
    SurfaceID id() const { return mState.id; }
    GLuint getId() const override;

    EGLint getOrientation() const { return mOrientation; }

    bool directComposition() const
    {
        mMutex.assertLocked();
        return mState.directComposition;
    }

    gl::InitState initState(GLenum binding, const gl::ImageIndex &imageIndex) const override;
    void setInitState(GLenum binding,
                      const gl::ImageIndex &imageIndex,
                      gl::InitState initState) override;

    bool isRobustResourceInitEnabled() const
    {
        mMutex.assertLocked();
        return mRobustResourceInitialization;
    }

    const gl::Format &getBindTexImageFormat() const { return mColorFormat; }

    // EGL_ANDROID_get_frame_timestamps entry points
    void setTimestampsEnabled(bool enabled);
    bool isTimestampsEnabled() const;

    // EGL_ANDROID_front_buffer_auto_refresh entry points
    Error setAutoRefreshEnabled(bool enabled);

    const SupportedCompositorTiming &getSupportedCompositorTimings() const;
    Error getCompositorTiming(EGLint numTimestamps,
                              const EGLint *names,
                              EGLnsecsANDROID *values) const;

    Error getNextFrameId(EGLuint64KHR *frameId) const;
    const SupportedTimestamps &getSupportedTimestamps() const;
    Error getFrameTimestamps(EGLuint64KHR frameId,
                             EGLint numTimestamps,
                             const EGLint *timestamps,
                             EGLnsecsANDROID *values) const;

    // Returns the offset into the texture backing the surface if specified via texture offset
    // attributes (see EGL_ANGLE_d3d_texture_client_buffer extension). Returns zero offset
    // otherwise.
    const gl::Offset &getTextureOffset() const { return mTextureOffset; }

    Error getBufferAge(const gl::Context *context, EGLint *age);

    Error setRenderBuffer(EGLint renderBuffer);
    void setRequestedRenderBuffer(EGLint requestedRenderBuffer);

    bool bufferAgeQueriedSinceLastSwap() const
    {
        mMutex.assertLocked();
        return mBufferAgeQueriedSinceLastSwap;
    }
    void setDamageRegion(const EGLint *rects, EGLint n_rects);
    bool isDamageRegionSet() const
    {
        mMutex.assertLocked();
        return mIsDamageRegionSet;
    }

    void addRef() const { mRefCount.fetch_add(1, std::memory_order_relaxed); }
    void release() const
    {
        uint32_t prev = mRefCount.fetch_sub(1, std::memory_order_acq_rel);
        ASSERT(prev > 0);
    }
    Error releaseRef(const ThreadSafeDisplay *display);
    uint32_t getRefCount() const { return mRefCount.load(std::memory_order_acquire); }

  protected:
    Surface(EGLint surfaceType,
            SurfaceID id,
            const egl::Config *config,
            const AttributeMap &attributes,
            bool forceRobustResourceInit,
            EGLenum buftype = EGL_NONE);
    ~Surface() override;
    rx::FramebufferAttachmentObjectImpl *getAttachmentImpl() const override;

    // ANGLE-only method, used internally
    friend class gl::Texture;
    friend class ScopedSurfaceLockAndRef;
    Error releaseTexImageFromTexture(const gl::Context *context);

    SurfaceState mState;
    rx::SurfaceImpl *mImplementation;
    mutable std::atomic<uint32_t> mRefCount;
    mutable angle::SimpleMutex mMutex;

    EGLint mType;
    EGLenum mBuftype;

    bool mPostSubBufferRequested;

    bool mLargestPbuffer;
    EGLenum mGLColorspace;
    EGLenum mVGAlphaFormat;
    EGLenum mVGColorspace;
    bool mMipmapTexture;
    EGLint mMipmapLevel;
    EGLint mHorizontalResolution;
    EGLint mVerticalResolution;
    EGLenum mMultisampleResolve;

    bool mFixedSize;
    size_t mFixedWidth;
    size_t mFixedHeight;

    bool mRobustResourceInitialization;

    TextureFormat mTextureFormat;
    EGLenum mTextureTarget;

    EGLint mPixelAspectRatio;        // Display aspect ratio
    EGLenum mRenderBuffer;           // Render buffer
    EGLenum mRequestedRenderBuffer;  // Requested render buffer
    EGLenum mRequestedSwapBehavior;
    EGLint mRequestedSwapInterval;

    EGLint mOrientation;

    // We don't use a binding pointer here. We don't ever want to own an orphaned texture. If a
    // Texture is deleted the Surface is unbound in onDestroy.
    gl::Texture *mTexture;

    gl::Format mColorFormat;
    gl::Format mDSFormat;

    gl::Offset mTextureOffset;

    uint32_t mCurrentRefCount;  // The surface is current to a context/client API
    uint8_t *mLockBufferPtr;    // Memory owned by backend.
    EGLint mLockBufferPitch;

    bool mBufferAgeQueriedSinceLastSwap;
    bool mIsDamageRegionSet;

  private:
    Error destroyImpl(const ThreadSafeDisplay *display);

    void postSwap(const gl::Context *context, const rx::SurfaceSwapFeedback &feedback);

    // ObserverInterface implementation.
    void onSubjectStateChange(angle::SubjectIndex index, angle::SubjectMessage message) override;

    Error updatePropertiesOnSwap(const gl::Context *context);

    gl::InitState mColorInitState;
    gl::InitState mDepthStencilInitState;
    angle::ObserverBinding mImplObserverBinding;
};

class WindowSurface final : public Surface
{
  public:
    WindowSurface(rx::EGLImplFactory *implFactory,
                  SurfaceID id,
                  const Config *config,
                  EGLNativeWindowType window,
                  const AttributeMap &attribs,
                  bool robustResourceInit);
    ~WindowSurface() override;
};

class PbufferSurface final : public Surface
{
  public:
    PbufferSurface(rx::EGLImplFactory *implFactory,
                   SurfaceID id,
                   const Config *config,
                   const AttributeMap &attribs,
                   bool robustResourceInit);
    PbufferSurface(rx::EGLImplFactory *implFactory,
                   SurfaceID id,
                   const Config *config,
                   EGLenum buftype,
                   EGLClientBuffer clientBuffer,
                   const AttributeMap &attribs,
                   bool robustResourceInit);

  protected:
    ~PbufferSurface() override;
};

class PixmapSurface final : public Surface
{
  public:
    PixmapSurface(rx::EGLImplFactory *implFactory,
                  SurfaceID id,
                  const Config *config,
                  NativePixmapType nativePixmap,
                  const AttributeMap &attribs,
                  bool robustResourceInit);

  protected:
    ~PixmapSurface() override;
};

class [[nodiscard]] ScopedSurfaceRef final
{
  public:
    ScopedSurfaceRef() = default;
    ScopedSurfaceRef(const ThreadSafeDisplay *display, Surface *surface)
        : mDisplay(display), mSurface(surface)
    {
        if (mSurface)
        {
            ASSERT(mDisplay != nullptr);
            mSurface->addRef();
        }
    }
    ~ScopedSurfaceRef()
    {
        if (mSurface)
        {
            ANGLE_SWALLOW_ERR(mSurface->releaseRef(mDisplay));
        }
    }
    ScopedSurfaceRef(const ScopedSurfaceRef &other)            = delete;
    ScopedSurfaceRef &operator=(const ScopedSurfaceRef &other) = delete;
    ScopedSurfaceRef(ScopedSurfaceRef &&other) noexcept
        : mDisplay(other.mDisplay), mSurface(other.mSurface)
    {
        other.mDisplay = nullptr;
        other.mSurface = nullptr;
    }
    ScopedSurfaceRef &operator=(ScopedSurfaceRef &&other) noexcept
    {
        std::swap(mDisplay, other.mDisplay);
        std::swap(mSurface, other.mSurface);
        return *this;
    }

    Surface *get() const { return mSurface; }

  private:
    const ThreadSafeDisplay *mDisplay = nullptr;
    Surface *mSurface                 = nullptr;
};

class [[nodiscard]] ScopedSurfaceLockAndRef final
{
  public:
    ScopedSurfaceLockAndRef() = default;
    ScopedSurfaceLockAndRef(const ThreadSafeDisplay *display, Surface *surface)
        : ScopedSurfaceLockAndRef(ScopedSurfaceRef(display, surface))
    {}
    explicit ScopedSurfaceLockAndRef(ScopedSurfaceRef &&surfaceRef)
        : mSurfaceRef(std::move(surfaceRef))
    {
        if (mSurfaceRef.get() != nullptr)
        {
            mLock = std::unique_lock<angle::SimpleMutex>(mSurfaceRef.get()->mMutex);
        }
    }
    ScopedSurfaceLockAndRef(const ScopedSurfaceLockAndRef &other)            = delete;
    ScopedSurfaceLockAndRef &operator=(const ScopedSurfaceLockAndRef &other) = delete;
    ScopedSurfaceLockAndRef(ScopedSurfaceLockAndRef &&other) noexcept        = default;
    ScopedSurfaceLockAndRef &operator=(ScopedSurfaceLockAndRef &&other) noexcept
    {
        std::swap(mSurfaceRef, other.mSurfaceRef);
        std::swap(mLock, other.mLock);
        return *this;
    }

    Surface *get() const { return mSurfaceRef.get(); }

  private:
    // Order is important: mLock must be unlocked before mSurfaceRef releases its reference (and
    // potentially destroys the Surface).
    ScopedSurfaceRef mSurfaceRef;
    std::unique_lock<angle::SimpleMutex> mLock;
};

template <size_t N>
class [[nodiscard]] ScopedSurfacesLockAndRef final
{
  public:
    ScopedSurfacesLockAndRef() = default;
    explicit ScopedSurfacesLockAndRef(std::array<ScopedSurfaceRef, N> &&surfaceRefs)
    {
        for (size_t i = 0; i < N; ++i)
        {
            mSurfaces[i] = surfaceRefs[i].get();
        }

        // Acquire surface locks in canonical Surface address order to avoid lock-order inversion
        // when the same surfaces are passed in different orders across entry points.
        std::sort(
            surfaceRefs.begin(), surfaceRefs.end(),
            [](const ScopedSurfaceRef &a, const ScopedSurfaceRef &b) { return a.get() < b.get(); });

        size_t lockCount          = 0;
        const Surface *lastLocked = nullptr;
        for (ScopedSurfaceRef &surfaceRef : surfaceRefs)
        {
            Surface *surface = surfaceRef.get();
            if (surface != nullptr && surface != lastLocked)
            {
                mLocks[lockCount++] = ScopedSurfaceLockAndRef(std::move(surfaceRef));
                lastLocked          = surface;
            }
        }
    }
    ScopedSurfacesLockAndRef(const ScopedSurfacesLockAndRef &other)                = delete;
    ScopedSurfacesLockAndRef &operator=(const ScopedSurfacesLockAndRef &other)     = delete;
    ScopedSurfacesLockAndRef(ScopedSurfacesLockAndRef &&other) noexcept            = default;
    ScopedSurfacesLockAndRef &operator=(ScopedSurfacesLockAndRef &&other) noexcept = default;

    Surface *get(size_t index) const { return mSurfaces[index]; }

  private:
    std::array<ScopedSurfaceLockAndRef, N> mLocks;
    std::array<Surface *, N> mSurfaces = {};
};

class SurfaceDeleter final
{
  public:
    SurfaceDeleter(const ThreadSafeDisplay *display);
    ~SurfaceDeleter();
    void operator()(Surface *surface);

  private:
    const ThreadSafeDisplay *mDisplay;
};

using SurfacePointer = std::unique_ptr<Surface, SurfaceDeleter>;

class SurfaceMap final : public priv::ObjectMap<Surface, angle::SimpleMutex>
{
  public:
    SurfaceMap() : mHandleAllocator(gl::IMPLEMENTATION_MAX_OBJECT_HANDLES, 64) {}

    Surface *find(SurfaceID handle) const = delete;

    bool allocateHandle(SurfaceID *outHandle)
    {
        std::lock_guard<angle::SimpleMutex> lock(mMutex);
        return mHandleAllocator.allocate(&outHandle->value);
    }

    void insert(SurfaceID handle, Surface *surface)
    {
        surface->addRef();
        priv::ObjectMap<Surface, angle::SimpleMutex>::insert(handle, surface);
    }

    bool erase(SurfaceID handle)
    {
        std::lock_guard<angle::SimpleMutex> lock(mMutex);
        auto iter = mObjects.find(handle.value);
        if (iter != mObjects.end())
        {
            mObjects.erase(iter);
            mHandleAllocator.release(handle.value);
            return true;
        }
        return false;
    }

    void invalidateAll()
    {
        std::lock_guard<angle::SimpleMutex> lock(mMutex);
        mInvalidSurfaces.reserve(mInvalidSurfaces.size() + mObjects.size());
        for (const auto &pair : mObjects)
        {
            mInvalidSurfaces.push_back(pair.second);
        }
        mObjects.clear();
    }

    Error destroyAllInvalidSurfaces(ThreadSafeDisplay *display);

    ScopedSurfaceLockAndRef getSurface(const ThreadSafeDisplay *display, SurfaceID handle) const
    {
        return ScopedSurfaceLockAndRef(getSurfaceRef(display, handle));
    }

    ScopedSurfaceRef getSurfaceRef(const ThreadSafeDisplay *display, SurfaceID handle) const
    {
        std::lock_guard<angle::SimpleMutex> lock(mMutex);
        auto iter = mObjects.find(handle.value);
        return ScopedSurfaceRef(display, iter != mObjects.end() ? iter->second : nullptr);
    }

  private:
    std::vector<Surface *> mInvalidSurfaces;
    gl::HandleAllocator mHandleAllocator;
};

}  // namespace egl

#endif  // LIBANGLE_SURFACE_H_
