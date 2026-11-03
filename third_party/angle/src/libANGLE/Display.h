//
// Copyright 2002 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Display.h: Defines the egl::Display class, representing the abstract
// display on which graphics are drawn. Implements EGLDisplay.
// [EGL 1.4] section 2.1.2 page 3.

#ifndef LIBANGLE_DISPLAY_H_
#define LIBANGLE_DISPLAY_H_

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

#include "common/SimpleMutex.h"
#include "common/WorkerThread.h"
#include "common/platform.h"
#include "libANGLE/AttributeMap.h"
#include "libANGLE/BlobCache.h"
#include "libANGLE/Caps.h"
#include "libANGLE/Config.h"
#include "libANGLE/Context.h"
#include "libANGLE/Debug.h"
#include "libANGLE/Error.h"
#include "libANGLE/HandleAllocator.h"
#include "libANGLE/MemoryProgramCache.h"
#include "libANGLE/MemoryShaderCache.h"
#include "libANGLE/Observer.h"
#include "libANGLE/ShareGroup.h"
#include "libANGLE/Surface.h"
#include "libANGLE/Version.h"
#include "platform/Feature.h"
#include "platform/autogen/FrontendFeatures_autogen.h"

namespace angle
{
class FrameCaptureShared;
}  // namespace angle

namespace gl
{
class Context;
class TextureManager;
class SemaphoreManager;
}  // namespace gl

namespace rx
{
class ThreadSafeDisplayImpl;
class DisplayImpl;
class EGLImplFactory;
}  // namespace rx

namespace egl
{
class Device;
class Image;
class Stream;
class Surface;
class Sync;
class ScopedSyncRef;
class Thread;
class ThreadSafeDisplay;

template <typename DisplayT>
class ScopedDisplayRefT;
using ScopedDisplayRef           = ScopedDisplayRefT<Display>;
using ScopedConstDisplayRef      = ScopedDisplayRefT<const Display>;
using ScopedThreadSafeDisplayRef = ScopedDisplayRefT<ThreadSafeDisplay>;

class ScopedDisplayMutexLock;

template <typename DisplayT>
class ScopedDisplayLockAndRefT;
using ScopedDisplayLockAndRef      = ScopedDisplayLockAndRefT<Display>;
using ScopedConstDisplayLockAndRef = ScopedDisplayLockAndRefT<const Display>;

using ThreadSet = angle::HashSet<Thread *>;

// Size of a Vulkan device or driver UUID, matching VK_UUID_SIZE.  Spelled out
// here because this header must not depend on the Vulkan headers.
constexpr size_t kVulkanUUIDSize = 16;
using VulkanUUID                 = std::array<uint8_t, kVulkanUUIDSize>;

struct DisplayState final : private angle::NonCopyable
{
    DisplayState(EGLNativeDisplayType nativeDisplayId);
    ~DisplayState();

    void notifyDeviceLost() const;

    EGLLabelKHR label;
    ContextMap contextMap;
    SurfaceMap surfaceMap;
    angle::FeatureOverrides featureOverrides;
    EGLNativeDisplayType displayId;

    // EGL_PLATFORM_ANGLE_VULKAN_DEVICE_UUID_ANGLE and
    // EGL_PLATFORM_ANGLE_VULKAN_DRIVER_UUID_ANGLE name caller-owned buffers
    // that EGL does not require to outlive eglGetPlatformDisplay, so their
    // bytes are copied here while that call is still running.  Unset means the
    // attribute was absent, which is not the same as an all-zero UUID.
    std::optional<VulkanUUID> vulkanDeviceUUID;
    std::optional<VulkanUUID> vulkanDriverUUID;

    // Single-threaded and multithread pools for use by various parts of ANGLE, such as shader
    // compilation.  These pools are internally synchronized.
    std::shared_ptr<angle::WorkerThreadPool> singleThreadPool;
    std::shared_ptr<angle::WorkerThreadPool> multiThreadPool;

    // Written by the backend thread that detects device loss and read by any thread performing EGL
    // validation, with no lock in common: atomic is required to avoid a data race.  Relaxed
    // ordering is sufficient because the flag only ever goes false -> true via notifyDeviceLost()
    // for the lifetime of an initialized Display; the single false write is in Display::terminate()
    // and is ordered after waitUntilUnreferenced(), so it cannot be observed by an in-flight
    // reader.  A stale read is therefore always a stale false: the caller misses EGL_CONTEXT_LOST
    // on this call and sees it on the next one.  A lock would not help, since device loss is
    // detected asynchronously on another thread and the check is inherently racy regardless.
    mutable std::atomic<bool> deviceLost;
};

// Constant coded here as a reasonable limit.
constexpr EGLAttrib kProgramCacheSizeAbsoluteMax = 0x4000000;

using ImageMap  = angle::HashMap<GLuint, Image *>;
using StreamSet = angle::HashSet<Stream *>;
using SyncMap   = angle::HashMap<GLuint, Sync *>;

class [[nodiscard]] ScopedSyncMap final : angle::NonCopyable
{
  public:
    ScopedSyncMap(angle::SimpleMutex &mutex, const SyncMap &syncMap)
        : mLock(mutex), mSyncMap(syncMap)
    {}

    ScopedSyncMap(ScopedSyncMap &&other) noexcept
        : mLock(std::move(other.mLock)), mSyncMap(other.mSyncMap)
    {}

    SyncMap::const_iterator begin() const { return mSyncMap.begin(); }
    SyncMap::const_iterator end() const { return mSyncMap.end(); }
    bool empty() const { return mSyncMap.empty(); }
    size_t size() const { return mSyncMap.size(); }

  private:
    std::unique_lock<angle::SimpleMutex> mLock;
    const SyncMap &mSyncMap;
};

class SyncSet final : angle::NonCopyable
{
  public:
    SyncSet();
    ~SyncSet();

    Error createSync(const ThreadSafeDisplay *display,
                     const gl::Context *currentContext,
                     EGLenum type,
                     const AttributeMap &attribs,
                     Sync **outSync);

    // The returned ScopedSyncRef holds a reference to the display and releases the sync through it
    // when it goes out of scope, so a mutable display is required here.
    ScopedSyncRef getSync(ThreadSafeDisplay *display, SyncID syncID) const;

    Sync *removeSync(SyncID syncID);
    void releaseSync(const ThreadSafeDisplay *display, Sync *sync);

    void invalidateAllSyncs();
    void destroyAllInvalidSyncs(Display *display);
    void clearPools();

    ScopedSyncMap getSyncsForCapture() const { return ScopedSyncMap(mMutex, mSyncMap); }

  private:
    static constexpr size_t kMaxSyncPoolSizePerType = 32;
    using SyncPool = angle::FixedVector<std::unique_ptr<Sync>, kMaxSyncPoolSizePerType>;

    void releaseSyncImpl(const ThreadSafeDisplay *display, Sync *sync);

    mutable angle::SimpleMutex mMutex;
    SyncMap mSyncMap;
    SyncMap mInvalidSyncMap;
    std::map<EGLenum, SyncPool> mSyncPools;
    gl::HandleAllocator mHandleAllocator;
};

class ScratchBuffers final : angle::NonCopyable
{
  public:
    ScratchBuffers()  = default;
    ~ScratchBuffers() = default;

    angle::ScratchBuffer requestScratchBuffer();
    void returnScratchBuffer(angle::ScratchBuffer scratchBuffer);
    void tick();

  private:
    angle::SimpleMutex mMutex;
    std::vector<angle::ScratchBuffer> mScratchBuffers;
};

// Access to this class is thread safe, i.e. can be called from multiple threads without holding the
// display's mutex lock.
class ThreadSafeDisplay : public LabeledObject, public angle::NonCopyable
{
  public:
    ThreadSafeDisplay(EGLNativeDisplayType displayId)
        : mState(displayId),
          mThreadSafeImpl(nullptr),
          mTerminatedByApi(false),
          mPendingInternalCleanup(false),
          mRefCount(0)
    {}
    ~ThreadSafeDisplay() override = default;

    // Returns whether the display is initialized and is not concurrently being terminated.  This
    // is what callers outside the display want: a display that is being terminated must already be
    // treated as no longer initialized.
    bool isInitializedAndNotTerminating() const;
    bool isDeviceLost() const;

    const DisplayExtensions &getExtensions() const { return mDisplayExtensions; }
    const angle::FrontendFeatures &getFrontendFeatures() const { return mFrontendFeatures; }

    // Note that Display::getImplementation() hides this one and returns the rx::DisplayImpl
    // instead, so the impl that is retrieved depends on the static type of the display.
    rx::ThreadSafeDisplayImpl *getImplementation() const { return mThreadSafeImpl; }

    Error createSync(const gl::Context *currentContext,
                     EGLenum type,
                     const AttributeMap &attribs,
                     Sync **outSync);
    Sync *removeSync(egl::SyncID syncID);
    void destroySync(Sync *sync);
    void releaseSync(Sync *sync);

    ScopedSyncRef getSync(egl::SyncID syncID) const;
    ScopedSyncMap getSyncsForCapture() const { return mSyncSet.getSyncsForCapture(); }
    bool isValidSync(SyncID sync) const;

    ScopedSurfaceRef getSurfaceRef(egl::SurfaceID surfaceID) const;
    ScopedSurfaceLockAndRef getSurface(egl::SurfaceID surfaceID) const;
    Error destroySurface(Surface *surface);

    const gl::Context *getContext(gl::ContextID contextID) const;
    gl::Context *getContext(gl::ContextID contextID);
    bool isValidContext(gl::ContextID contextID) const;

    angle::ScratchBuffer requestScratchBuffer() { return mScratchBuffers.requestScratchBuffer(); }
    void returnScratchBuffer(angle::ScratchBuffer scratchBuffer)
    {
        mScratchBuffers.returnScratchBuffer(std::move(scratchBuffer));
    }

    angle::ScratchBuffer requestZeroFilledBuffer()
    {
        return mZeroFilledBuffers.requestScratchBuffer();
    }
    void returnZeroFilledBuffer(angle::ScratchBuffer zeroFilledBuffer)
    {
        mZeroFilledBuffers.returnScratchBuffer(std::move(zeroFilledBuffer));
    }

    Error makeCurrent(Thread *thread,
                      gl::Context *previousContext,
                      Surface *drawSurface,
                      Surface *readSurface,
                      gl::Context *context);

    Error destroySurfaceImpl(Surface *surface);

    virtual void releaseDisplayShareGroupResource(const gl::Context &context) = 0;

  protected:
    [[nodiscard]] bool addRefIfNotTerminating() const
    {
        // std::memory_order_relaxed might be sufficient here, but std::memory_order_acquire is used
        // for safety since there is no performance difference.
        uint32_t count = mRefCount.load(std::memory_order_acquire);
        while (!(count & kTerminatingBit))
        {
            if (mRefCount.compare_exchange_weak(count, count + 1, std::memory_order_acquire,
                                                std::memory_order_acquire))
            {
                return true;
            }
        }
        return false;
    }
    void releaseRef() const
    {
        uint32_t prev = mRefCount.fetch_sub(1, std::memory_order_release);
        ASSERT((prev & kRefCountMask) > 0);
    }
    void onPendingInternalCleanup() const;
    bool isTerminating() const;

    // Returns whether the display has been initialized, disregarding whether it is currently being
    // terminated.  This remains stable for as long as a display reference is held, since
    // terminate() only clears the bit after waitUntilUnreferenced().  Callers that must also
    // account for a concurrent terminate() want isInitializedAndNotTerminating() instead.
    bool isInitialized() const;
    // Both of these must be called with mDisplayMutex held.
    void setInitialized();
    void setUninitialized();

    Error restoreLostDevice() const;

    // The high bits of mRefCount hold flags; the remaining bits hold the reference count itself.
    // Keeping the initialized flag in the same word as the terminating flag lets
    // isInitializedAndNotTerminating() observe the two as a consistent pair with a single load.
    static constexpr uint32_t kTerminatingBit = 1u << 31;
    static constexpr uint32_t kInitializedBit = 1u << 30;
    static constexpr uint32_t kRefCountMask   = ~(kTerminatingBit | kInitializedBit);

    DisplayState mState;

    rx::ThreadSafeDisplayImpl *mThreadSafeImpl;

    DisplayExtensions mDisplayExtensions;
    angle::FrontendFeatures mFrontendFeatures;

    SyncSet mSyncSet;

    ScratchBuffers mScratchBuffers;
    ScratchBuffers mZeroFilledBuffers;

    Error releaseContext(gl::Context *context, Thread *thread);
    std::unique_ptr<gl::Context> eraseContextImpl(gl::Context *context, ContextMap *contexts);

    std::atomic<bool> mTerminatedByApi;
    mutable std::atomic<bool> mPendingInternalCleanup;

    // Only these ScopedDisplay classes could directly access the RefCount.
    template <typename DisplayT>
    friend class ScopedDisplayLockAndRefT;
    template <typename DisplayT>
    friend class ScopedDisplayRefT;
    mutable std::atomic<uint32_t> mRefCount;
};

class Display final : public angle::ObserverInterface, public ThreadSafeDisplay
{
  public:
    ~Display() override;

    void setLabel(EGLLabelKHR label) override;
    EGLLabelKHR getLabel() const override;

    // Observer implementation.
    void onSubjectStateChange(angle::SubjectIndex index, angle::SubjectMessage message) override;

    Error initialize();

    enum class TerminateReason
    {
        Api,
        InternalCleanup,

        InvalidEnum,
        EnumCount = InvalidEnum,
    };
    Error terminate(Thread *thread, TerminateReason terminateReason);

    // Called on eglReleaseThread. Backends can tear down thread-specific backend state through
    // this function.
    Error releaseThread();

    static Display *GetDisplayFromDevice(Device *device, const AttributeMap &attribMap);
    static Display *GetDisplayFromNativeDisplay(EGLenum platform,
                                                EGLNativeDisplayType nativeDisplay,
                                                const AttributeMap &attribMap);
    static Display *GetExistingDisplayFromNativeDisplay(EGLNativeDisplayType nativeDisplay);

    using EglDisplaySet = angle::HashSet<Display *>;

    static const ClientExtensions &GetClientExtensions();
    static const std::string &GetClientExtensionString();

    std::vector<const Config *> getConfigs(const AttributeMap &attribs) const;
    std::vector<const Config *> chooseConfig(const AttributeMap &attribs) const;

    // eglCreatePlatformWindowSurface and its EXT variant take the native window as a void * whose
    // interpretation is platform specific: the EGL/X11 platform extension specifies a pointer to
    // an X11 Window, while EGLNativeWindowType for X11 is the Window (XID) itself.  For the other
    // window systems the void * is already the EGLNativeWindowType value.
    // https://www.khronos.org/registry/EGL/extensions/KHR/EGL_KHR_platform_x11.txt
    EGLNativeWindowType getNativeWindowForCreatePlatformWindowSurface(
        const void *nativeWindow) const;

    Error createWindowSurface(const Config *configuration,
                              EGLNativeWindowType window,
                              const AttributeMap &attribs,
                              Surface **outSurface);
    Error createPbufferSurface(const Config *configuration,
                               const AttributeMap &attribs,
                               Surface **outSurface);
    Error createPbufferFromClientBuffer(const Config *configuration,
                                        EGLenum buftype,
                                        EGLClientBuffer clientBuffer,
                                        const AttributeMap &attribs,
                                        Surface **outSurface);
    Error createPixmapSurface(const Config *configuration,
                              NativePixmapType nativePixmap,
                              const AttributeMap &attribs,
                              Surface **outSurface);

    Error createImage(const gl::Context *context,
                      EGLenum target,
                      EGLClientBuffer buffer,
                      const AttributeMap &attribs,
                      Image **outImage);

    Error createStream(const AttributeMap &attribs, Stream **outStream);

    Error createContext(const Config *configuration,
                        gl::Context *shareContext,
                        const AttributeMap &attribs,
                        gl::Context **outContext);

    void destroyImage(Image *image);
    void destroyStream(Stream *stream);
    Error destroyContext(Thread *thread, gl::Context *context);

    bool isValidConfig(const Config *config) const;
    bool isValidImage(ImageID imageID) const;
    bool isValidStream(const Stream *stream) const;
    bool isValidNativeWindow(EGLNativeWindowType window) const;

    Error validateClientBuffer(const Config *configuration,
                               EGLenum buftype,
                               EGLClientBuffer clientBuffer,
                               const AttributeMap &attribs) const;
    Error validateImageClientBuffer(const gl::Context *context,
                                    EGLenum target,
                                    EGLClientBuffer clientBuffer,
                                    const egl::AttributeMap &attribs) const;
    Error valdiatePixmap(const Config *config,
                         EGLNativePixmapType pixmap,
                         const AttributeMap &attributes) const;

    static bool isValidDisplay(const Display *display);
    static bool isValidNativeDisplay(EGLNativeDisplayType display);
    static bool hasExistingWindowSurface(EGLNativeWindowType window);

    bool testDeviceLost();
    void notifyDeviceLost();

    void setBlobCacheFuncs(EGLSetBlobFuncANDROID set, EGLGetBlobFuncANDROID get);
    bool areBlobCacheFuncsSet() const { return mBlobCache.areBlobCacheFuncsSet(); }
    BlobCache &getBlobCache() { return mBlobCache; }

    static EGLClientBuffer GetNativeClientBuffer(const struct AHardwareBuffer *buffer);
    static Error CreateNativeClientBuffer(const egl::AttributeMap &attribMap,
                                          EGLClientBuffer *eglClientBuffer);

    Error waitClient(const gl::Context *context);
    Error waitNative(const gl::Context *context, EGLint engine);

    const Caps &getCaps() const;

    const std::string &getExtensionString() const;
    const std::string &getVendorString() const;
    const std::string &getVersionString() const;
    const std::string &getClientAPIString() const;

    std::string getBackendRendererDescription() const;
    std::string getBackendVendorString() const;
    std::string getBackendVersionString(bool includeFullVersion) const;

    EGLint programCacheGetAttrib(EGLenum attrib) const;
    Error programCacheQuery(EGLint index,
                            void *key,
                            EGLint *keysize,
                            void *binary,
                            EGLint *binarysize);
    Error programCachePopulate(const void *key,
                               EGLint keysize,
                               const void *binary,
                               EGLint binarysize);
    EGLint programCacheResize(EGLint limit, EGLenum mode);

    const AttributeMap &getAttributeMap() const { return mAttributeMap; }
    EGLNativeDisplayType getNativeDisplayId() const { return mState.displayId; }

    rx::DisplayImpl *getImplementation() const { return mImplementation; }
    Device *getDevice() const;
    Surface *getWGLSurface() const;
    EGLenum getPlatform() const { return mPlatform; }

    gl::Version getMaxSupportedESVersion() const;

    const DisplayState &getState() const { return mState; }

    void overrideFrontendFeatures(const std::vector<std::string> &featureNames, bool enabled);

    const angle::FeatureList &getFeatures() const { return mFeatures; }

    const char *queryStringi(const EGLint name, const EGLint index);

    EGLAttrib queryAttrib(const EGLint attribute);

    egl::Error handleGPUSwitch();
    egl::Error forceGPUSwitch(EGLint gpuIDHigh, EGLint gpuIDLow);

    egl::Error waitUntilWorkScheduled();

    void lockVulkanQueue();
    void unlockVulkanQueue();

    bool supportsDmaBufFormat(EGLint format) const;
    Error queryDmaBufFormats(EGLint max_formats, EGLint *formats, EGLint *num_formats);
    Error queryDmaBufModifiers(EGLint format,
                               EGLint max_modifiers,
                               EGLuint64KHR *modifiers,
                               EGLBoolean *external_only,
                               EGLint *num_modifiers);

    Error querySupportedCompressionRates(const Config *configuration,
                                         const AttributeMap &attributes,
                                         EGLint *rates,
                                         EGLint rate_size,
                                         EGLint *num_rates) const;

    std::shared_ptr<angle::WorkerThreadPool> getSingleThreadPool() const
    {
        return mState.singleThreadPool;
    }
    std::shared_ptr<angle::WorkerThreadPool> getMultiThreadPool() const
    {
        return mState.multiThreadPool;
    }

    angle::ImageLoadContext getImageLoadContext() const;

    const egl::Image *getImage(egl::ImageID imageID) const;
    egl::Image *getImage(egl::ImageID imageID);

    const ImageMap &getImagesForCapture() const { return mImageMap; }

    // Initialize thread-local variables used by the Display and its backing implementations.  This
    // includes:
    //
    // - The unlocked tail call to be run at the end of the entry point.
    // - Scratch space for an egl::Error used by the backends (this is not used by all backends, and
    //   access *must* be restricted to backends that use it).
    //
    static void InitTLS();
    static angle::UnlockedTailCall *GetCurrentThreadUnlockedTailCall();
    static Error *GetCurrentThreadErrorScratchSpace();

    void releaseDisplayShareGroupResource(const gl::Context &context) override;

  private:
    friend class ThreadSafeDisplay;

    Display(EGLenum platform, EGLNativeDisplayType displayId, Device *eglDevice);

    void setAttributes(const AttributeMap &attribMap) { mAttributeMap = attribMap; }
    void setupDisplayPlatform(rx::DisplayImpl *impl);

    void initDisplayExtensions();
    void initVendorString();
    void initVersionString();
    void initClientAPIString();
    void initializeFrontendFeatures();

    Error destroyInvalidEglObjects();

    void destroyImageImpl(Image *image, ImageMap *images);
    void destroyStreamImpl(Stream *stream, StreamSet *streams);

    void initFromDevice(Device *device, const AttributeMap &attribMap);
    bool initFromNativeDisplay(const AttributeMap &attribMap, const EGLAttrib nativePlatformType);

    void waitUntilUnreferenced(uint32_t expectedCount);

    rx::DisplayImpl *mImplementation;
    angle::ObserverBinding mGPUSwitchedBinding;

    AttributeMap mAttributeMap;

    ConfigSet mConfigSet;

    ImageMap mImageMap;
    StreamSet mStreamSet;

    ContextMap mInvalidContextMap;
    ImageMap mInvalidImageMap;
    StreamSet mInvalidStreamSet;

    Caps mCaps;

    std::string mDisplayExtensionString;

    std::string mVendorString;
    std::string mVersionString;
    std::string mClientAPIString;

    Device *mDevice;
    Surface *mSurface;
    EGLenum mPlatform;

    // mManagersMutex protects mTextureManager and mSemaphoreManager
    ContextMutex *mManagersMutex;
    gl::TextureManager *mTextureManager;
    gl::SemaphoreManager *mSemaphoreManager;

    BlobCache mBlobCache;
    gl::MemoryProgramCache mMemoryProgramCache;
    gl::MemoryShaderCache mMemoryShaderCache;
    size_t mGlobalTextureShareGroupUsers;
    size_t mGlobalSemaphoreShareGroupUsers;

    gl::HandleAllocator mImageHandleAllocator;

    angle::FeatureList mFeatures;

    void onUnlockMutex();

    // Only these ScopedDisplay classes could directly access the lock.
    friend class ScopedDisplayMutexLock;
    template <typename DisplayT>
    friend class ScopedDisplayLockAndRefT;
    mutable angle::SimpleMutex mDisplayMutex;
};

template <typename DisplayT>
class [[nodiscard]] ScopedDisplayRefT final
{
  public:
    ScopedDisplayRefT() : mDisplay(nullptr), mHoldingRef(false) {}
    explicit ScopedDisplayRefT(DisplayT &display)
        : mDisplay(&display), mHoldingRef(display.addRefIfNotTerminating())
    {}
    ~ScopedDisplayRefT()
    {
        if (mHoldingRef)
        {
            mDisplay->releaseRef();
            if constexpr (std::is_same_v<DisplayT, ThreadSafeDisplay>)
            {
                if (ANGLE_UNLIKELY(
                        mDisplay->mPendingInternalCleanup.load(std::memory_order_acquire)))
                {
                    mDisplay->onPendingInternalCleanup();
                }
            }
        }
    }

    ScopedDisplayRefT(const ScopedDisplayRefT &other)
        : mDisplay(other.mDisplay),
          mHoldingRef(other.mHoldingRef && other.mDisplay->addRefIfNotTerminating())
    {}

    template <typename OtherDisplayT,
              std::enable_if_t<std::is_convertible_v<OtherDisplayT *, DisplayT *>, bool> = true>
    ScopedDisplayRefT(const ScopedDisplayRefT<OtherDisplayT> &other)
        : mDisplay(other.mDisplay),
          mHoldingRef(other.mHoldingRef && other.mDisplay->addRefIfNotTerminating())
    {}

    ScopedDisplayRefT &operator=(const ScopedDisplayRefT &other)
    {
        if (this != &other)
        {
            if (mHoldingRef)
            {
                mDisplay->releaseRef();
            }
            mDisplay    = other.mDisplay;
            mHoldingRef = other.mHoldingRef && other.mDisplay->addRefIfNotTerminating();
        }
        return *this;
    }

    template <typename OtherDisplayT,
              std::enable_if_t<std::is_convertible_v<OtherDisplayT *, DisplayT *>, bool> = true>
    ScopedDisplayRefT &operator=(const ScopedDisplayRefT<OtherDisplayT> &other)
    {
        if (static_cast<const void *>(this) != static_cast<const void *>(&other))
        {
            if (mHoldingRef)
            {
                mDisplay->releaseRef();
            }
            mDisplay    = other.mDisplay;
            mHoldingRef = other.mHoldingRef && other.mDisplay->addRefIfNotTerminating();
        }
        return *this;
    }

    ScopedDisplayRefT(ScopedDisplayRefT &&other) noexcept
        : mDisplay(other.mDisplay), mHoldingRef(other.mHoldingRef)
    {
        other.mDisplay    = nullptr;
        other.mHoldingRef = false;
    }

    template <typename OtherDisplayT,
              std::enable_if_t<std::is_convertible_v<OtherDisplayT *, DisplayT *>, bool> = true>
    ScopedDisplayRefT(ScopedDisplayRefT<OtherDisplayT> &&other) noexcept
        : mDisplay(other.mDisplay), mHoldingRef(other.mHoldingRef)
    {
        other.mDisplay    = nullptr;
        other.mHoldingRef = false;
    }

    ScopedDisplayRefT &operator=(ScopedDisplayRefT &&other) noexcept
    {
        if (this != &other)
        {
            if (mHoldingRef)
            {
                mDisplay->releaseRef();
            }
            mDisplay          = other.mDisplay;
            mHoldingRef       = other.mHoldingRef;
            other.mDisplay    = nullptr;
            other.mHoldingRef = false;
        }
        return *this;
    }

    template <typename OtherDisplayT,
              std::enable_if_t<std::is_convertible_v<OtherDisplayT *, DisplayT *>, bool> = true>
    ScopedDisplayRefT &operator=(ScopedDisplayRefT<OtherDisplayT> &&other) noexcept
    {
        if (static_cast<const void *>(this) != static_cast<const void *>(&other))
        {
            if (mHoldingRef)
            {
                mDisplay->releaseRef();
            }
            mDisplay          = other.mDisplay;
            mHoldingRef       = other.mHoldingRef;
            other.mDisplay    = nullptr;
            other.mHoldingRef = false;
        }
        return *this;
    }

    DisplayT *get() const { return mDisplay; }

  private:
    template <typename OtherDisplayT>
    friend class ScopedDisplayRefT;

    DisplayT *mDisplay;
    bool mHoldingRef;
};

class [[nodiscard]] ScopedDisplayMutexLock final
{
  public:
    ScopedDisplayMutexLock() = default;
    explicit ScopedDisplayMutexLock(const Display &display) : mLock(display.mDisplayMutex) {}

    ScopedDisplayMutexLock(const ScopedDisplayMutexLock &)            = delete;
    ScopedDisplayMutexLock &operator=(const ScopedDisplayMutexLock &) = delete;

    ScopedDisplayMutexLock(ScopedDisplayMutexLock &&) noexcept            = default;
    ScopedDisplayMutexLock &operator=(ScopedDisplayMutexLock &&) noexcept = default;

  private:
    std::unique_lock<angle::SimpleMutex> mLock;
};

// ScopedDisplayLockAndRefT locks mDisplayMutex before incrementing mRefCount on construction,
// and decrements mRefCount before unlocking mDisplayMutex on destruction (via C++ reverse member
// declaration destruction order).
// The lock must be taken before the reference count is increased to prevent deadlock with
// Display::waitUntilUnreferenced(), which holds mDisplayMutex while waiting for outstanding
// references from other threads to be released.
template <typename DisplayT>
class [[nodiscard]] ScopedDisplayLockAndRefT final
{
  public:
    ScopedDisplayLockAndRefT() = default;
    explicit ScopedDisplayLockAndRefT(DisplayT &display) : mLock(display), mDisplay(display) {}
    ~ScopedDisplayLockAndRefT()
    {
        if constexpr (!std::is_const_v<DisplayT>)
        {
            if (mDisplay.get() != nullptr)
            {
                mDisplay.get()->onUnlockMutex();
            }
        }
    }

    ScopedDisplayLockAndRefT(const ScopedDisplayLockAndRefT &)            = delete;
    ScopedDisplayLockAndRefT &operator=(const ScopedDisplayLockAndRefT &) = delete;

    ScopedDisplayLockAndRefT(ScopedDisplayLockAndRefT &&other) noexcept            = default;
    ScopedDisplayLockAndRefT &operator=(ScopedDisplayLockAndRefT &&other) noexcept = default;

    template <typename OtherDisplayT,
              std::enable_if_t<std::is_convertible_v<OtherDisplayT *, DisplayT *>, bool> = true>
    ScopedDisplayLockAndRefT(ScopedDisplayLockAndRefT<OtherDisplayT> &&other) noexcept
        : mLock(std::move(other.mLock)), mDisplay(std::move(other.mDisplay))
    {}

    template <typename OtherDisplayT,
              std::enable_if_t<std::is_convertible_v<OtherDisplayT *, DisplayT *>, bool> = true>
    ScopedDisplayLockAndRefT &operator=(ScopedDisplayLockAndRefT<OtherDisplayT> &&other) noexcept
    {
        if (static_cast<const void *>(this) != static_cast<const void *>(&other))
        {
            mLock    = std::move(other.mLock);
            mDisplay = std::move(other.mDisplay);
        }
        return *this;
    }

    DisplayT *get() const { return mDisplay.get(); }

  private:
    template <typename OtherDisplayT>
    friend class ScopedDisplayLockAndRefT;

    // mLock must be declared before mDisplay to ensure mDisplayMutex is locked before mRefCount
    // is incremented on construction, and mRefCount is decremented before mDisplayMutex is
    // unlocked on destruction.
    ScopedDisplayMutexLock mLock;
    ScopedDisplayRefT<DisplayT> mDisplay;
};

}  // namespace egl

#endif  // LIBANGLE_DISPLAY_H_
