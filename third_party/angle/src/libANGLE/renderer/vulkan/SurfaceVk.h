//
// Copyright 2016 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// SurfaceVk.h:
//    Defines the class interface for SurfaceVk, implementing SurfaceImpl.
//

#ifndef LIBANGLE_RENDERER_VULKAN_SURFACEVK_H_
#define LIBANGLE_RENDERER_VULKAN_SURFACEVK_H_

#include <optional>
#include "common/CircularBuffer.h"
#include "common/SimpleMutex.h"
#include "common/vulkan/vk_headers.h"
#include "libANGLE/renderer/SurfaceImpl.h"
#include "libANGLE/renderer/vulkan/RenderTargetVk.h"
#include "libANGLE/renderer/vulkan/vk_helpers.h"

namespace rx
{
class SurfaceVk : public SurfaceImpl, public angle::ObserverInterface, public vk::Resource
{
  public:
    angle::Result getAttachmentRenderTarget(const gl::Context *context,
                                            GLenum binding,
                                            const gl::ImageIndex &imageIndex,
                                            GLsizei samples,
                                            FramebufferAttachmentRenderTarget **rtOut) override;

  protected:
    SurfaceVk(const egl::SurfaceState &surfaceState);
    ~SurfaceVk() override;

    void destroy(const egl::ThreadSafeDisplay *display) override;
    // We monitor the staging buffer for changes. This handles staged data from outside this class.
    virtual void onSubjectStateChange(angle::SubjectIndex index,
                                      angle::SubjectMessage message) override;

    // size can change with client window resizing
    gl::Extents getSize() const override;

    EGLint mWidth;
    EGLint mHeight;

    RenderTargetVk mColorRenderTarget;
    RenderTargetVk mDepthStencilRenderTarget;
};

class OffscreenSurfaceVk : public SurfaceVk
{
  public:
    OffscreenSurfaceVk(const egl::SurfaceState &surfaceState, vk::Renderer *renderer);
    ~OffscreenSurfaceVk() override;

    egl::Error initialize(const egl::ThreadSafeDisplay *display) override;
    void destroy(const egl::ThreadSafeDisplay *display) override;

    egl::Error unMakeCurrent(const gl::Context *context) override;
    const vk::ImageHelper *getColorImage() const { return &mColorAttachment.image; }

    egl::Error swap(const gl::Context *context, SurfaceSwapFeedback *feedback) override;
    egl::Error postSubBuffer(const gl::Context *context,
                             EGLint x,
                             EGLint y,
                             EGLint width,
                             EGLint height) override;
    egl::Error querySurfacePointerANGLE(EGLint attribute, void **value) override;
    egl::Error bindTexImage(const gl::Context *context,
                            gl::Texture *texture,
                            EGLint buffer) override;
    egl::Error releaseTexImage(const gl::Context *context, EGLint buffer) override;
    egl::Error getSyncValues(EGLuint64KHR *ust, EGLuint64KHR *msc, EGLuint64KHR *sbc) override;
    egl::Error getMscRate(EGLint *numerator, EGLint *denominator) override;
    void setSwapInterval(const egl::ThreadSafeDisplay *display, EGLint interval) override;

    EGLint isPostSubBufferSupported() const override;
    EGLint getSwapBehavior() const override;

    angle::Result initializeContents(const gl::Context *context,
                                     GLenum binding,
                                     const gl::ImageIndex &imageIndex) override;

    vk::ImageHelper *getColorAttachmentImage();

    egl::Error lockSurface(const egl::ThreadSafeDisplay *display,
                           EGLint usageHint,
                           bool preservePixels,
                           uint8_t **bufferPtrOut,
                           EGLint *bufferPitchOut) override;
    egl::Error unlockSurface(const egl::ThreadSafeDisplay *display, bool preservePixels) override;
    EGLint origin() const override;

  protected:
    struct AttachmentImage final : angle::NonCopyable
    {
        AttachmentImage(SurfaceVk *surfaceVk);
        ~AttachmentImage();

        angle::Result initialize(ThreadSafeDisplayVk *displayVk,
                                 EGLint width,
                                 EGLint height,
                                 const vk::Format &vkFormat,
                                 GLint samples,
                                 bool isRobustResourceInitEnabled,
                                 bool hasProtectedContent);

        void destroy(const egl::ThreadSafeDisplay *display);

        vk::ImageHelper image;
        vk::ImageViewHelper imageViews;
        angle::ObserverBinding imageObserverBinding;
    };

    virtual angle::Result initializeImpl(ThreadSafeDisplayVk *displayVk);

    AttachmentImage mColorAttachment;
    AttachmentImage mDepthStencilAttachment;

    // EGL_KHR_lock_surface3
    vk::BufferHelper mLockBufferHelper;
};

// Data structures used in WindowSurfaceVk
namespace impl
{
static constexpr size_t kSwapHistorySize = 2;

// Old swapchain and associated present fences/semaphores that need to be scheduled for
// recycling/destruction when appropriate.
struct SwapchainCleanupData : angle::NonCopyable
{
    SwapchainCleanupData();
    SwapchainCleanupData(SwapchainCleanupData &&other);
    ~SwapchainCleanupData();

    // Fences must not be empty (VK_EXT_swapchain_maintenance1 is supported).
    VkResult getFencesStatus(VkDevice device) const;
    // Waits fences if any. Use before force destroying the swapchain.
    void waitFences(VkDevice device, uint64_t timeout) const;
    void destroy(VkDevice device,
                 vk::Recycler<vk::Fence> *fenceRecycler,
                 vk::Recycler<vk::Semaphore> *semaphoreRecycler);

    // The swapchain to be destroyed.
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    // Any present fences/semaphores that were pending recycle at the time the swapchain was
    // recreated will be scheduled for recycling at the same time as the swapchain's destruction.
    // fences must be in the present operation order.
    std::vector<vk::Fence> fences;
    std::vector<vk::Semaphore> semaphores;
};

// Each present operation is associated with a wait semaphore.  To know when that semaphore can be
// recycled, a swapSerial is used.  When that swapSerial is finished, the semaphore used in the
// previous present operation involving imageIndex can be recycled.  See doc/PresentSemaphores.md
// for details.
// When VK_EXT_swapchain_maintenance1 is supported, present fence is used instead of the swapSerial.
//
// Old swapchains are scheduled to be destroyed at the same time as the last wait semaphore used to
// present an image to the old swapchains can be recycled (only relevant when
// VK_EXT_swapchain_maintenance1 is not supported).
struct ImagePresentOperation : angle::NonCopyable
{
    ImagePresentOperation();
    ImagePresentOperation(ImagePresentOperation &&other);
    ImagePresentOperation &operator=(ImagePresentOperation &&other);
    ~ImagePresentOperation();

    void destroy(VkDevice device,
                 vk::Recycler<vk::Fence> *fenceRecycler,
                 vk::Recycler<vk::Semaphore> *semaphoreRecycler);

    // fence is only used when VK_EXT_swapchain_maintenance1 is supported.
    vk::Fence fence;
    vk::Semaphore semaphore;

    // Below members only relevant when VK_EXT_swapchain_maintenance1 is not supported.
    // Used to associate a swapSerial with the previous present operation of the image.
    uint32_t imageIndex;
    QueueSerial queueSerial;
    std::deque<SwapchainCleanupData> oldSwapchains;
};

using FramebufferIndex = uint8_t;
union SwapchainImageFramebufferDesc final
{
    struct
    {
        uint8_t framebufferFetchMode : 4;
        uint8_t writeControlMode : 4;
    };
    FramebufferIndex index;
};
static_assert(sizeof(SwapchainImageFramebufferDesc) == 1, "Size check failed");
using SwapchainImageFramebuffers = angle::HashMap<FramebufferIndex, vk::Framebuffer>;

// Swapchain images and their associated objects.
struct SwapchainImage : angle::NonCopyable
{
    SwapchainImage();
    SwapchainImage(SwapchainImage &&other);
    ~SwapchainImage();

    std::unique_ptr<vk::ImageHelper> image;
    vk::ImageViewHelper imageViews;
    SwapchainImageFramebuffers framebuffers;

    uint64_t frameNumber = 0;
};

enum class ImageAcquireState
{
    Unacquired,
    NeedToProcessResult,
    Ready,
};

// Associated data for a call to vkAcquireNextImageKHR without necessarily holding the share group
// and global locks but ONLY from a thread where Surface is current.
struct UnlockedAcquireData : angle::NonCopyable
{
    // Given that the CPU is throttled after a number of swaps, there is an upper bound to the
    // number of semaphores that are used to acquire swapchain images, and that is
    // kSwapHistorySize+1:
    //
    //             Unrelated submission in     Submission as part of
    //               the middle of frame          buffer swap
    //                              |                 |
    //                              V                 V
    //     Frame i:     ... ANI ... QS (fence Fa) ... QS (Fence Fb) QP Wait(..)
    //     Frame i+1:   ... ANI ... QS (fence Fc) ... QS (Fence Fd) QP Wait(..) <--\
    //     Frame i+2:   ... ANI ... QS (fence Fe) ... QS (Fence Ff) QP Wait(Fb)    |
    //                                                                  ^          |
    //                                                                  |          |
    //                                                           CPU throttling    |
    //                                                                             |
    //                               Note: app should throttle itself here (equivalent of Wait(Fb))
    //
    // In frame i+2 (2 is kSwapHistorySize), ANGLE waits on fence Fb which means that the semaphore
    // used for Frame i's ANI can be reused (because Fb-is-signalled implies Fa-is-signalled).
    // Before this wait, there were three acquire semaphores in use corresponding to frames i, i+1
    // and i+2.  Frame i+3 can reuse the semaphore of frame i.
    angle::CircularBuffer<vk::Semaphore, impl::kSwapHistorySize + 1> acquireImageSemaphores;
};

struct UnlockedAcquireResult : angle::NonCopyable
{
    // The result of the call to vkAcquireNextImageKHR.
    VkResult result = VK_SUCCESS;

    // Semaphore to signal.
    VkSemaphore acquireSemaphore = VK_NULL_HANDLE;

    // Image index that was acquired
    uint32_t imageIndex = std::numeric_limits<uint32_t>::max();
};

struct ImageAcquireOperation : angle::NonCopyable
{
    // Initially image needs to be acquired.
    ImageAcquireState state = ImageAcquireState::Unacquired;

    // No synchronization is necessary when making the vkAcquireNextImageKHR call since it is ONLY
    // possible on a thread where Surface is current.
    UnlockedAcquireData unlockedAcquireData;
    UnlockedAcquireResult unlockedAcquireResult;
};

enum class SurfaceSizeState
{
    InvalidSwapchain,
    Unresolved,
    Resolved,
};
}  // namespace impl

class WindowSurfaceVk : public SurfaceVk
{
  public:
    WindowSurfaceVk(const egl::SurfaceState &surfaceState, EGLNativeWindowType window);
    ~WindowSurfaceVk() override;

    void destroy(const egl::ThreadSafeDisplay *display) override;

    egl::Error initialize(const egl::ThreadSafeDisplay *display) override;

    egl::Error makeCurrent(const gl::Context *context) override;
    egl::Error unMakeCurrent(const gl::Context *context) override;

    angle::Result getAttachmentRenderTarget(const gl::Context *context,
                                            GLenum binding,
                                            const gl::ImageIndex &imageIndex,
                                            GLsizei samples,
                                            FramebufferAttachmentRenderTarget **rtOut) override;
    egl::Error prepareSwap(const gl::Context *context) override;
    egl::Error swap(const gl::Context *context, SurfaceSwapFeedback *feedback) override;
    egl::Error swapWithDamage(const gl::Context *context,
                              const EGLint *rects,
                              EGLint n_rects,
                              SurfaceSwapFeedback *feedback) override;
    egl::Error postSubBuffer(const gl::Context *context,
                             EGLint x,
                             EGLint y,
                             EGLint width,
                             EGLint height) override;
    egl::Error querySurfacePointerANGLE(EGLint attribute, void **value) override;
    egl::Error bindTexImage(const gl::Context *context,
                            gl::Texture *texture,
                            EGLint buffer) override;
    egl::Error releaseTexImage(const gl::Context *context, EGLint buffer) override;
    egl::Error getSyncValues(EGLuint64KHR *ust, EGLuint64KHR *msc, EGLuint64KHR *sbc) override;
    egl::Error getMscRate(EGLint *numerator, EGLint *denominator) override;
    void setSwapInterval(const egl::ThreadSafeDisplay *display, EGLint interval) override;
    void setSwapBehavior(EGLenum behavior) override;

    // Explicitly resolves surface size to use before state synchronization (e.g. validation).
    angle::Result ensureSizeResolved(const gl::Context *context) final;
    gl::Extents getSize() const final;

    // Unresolved Surface size until render target is first accessed (e.g. after draw).
    egl::Error getUserSize(const egl::ThreadSafeDisplay *display,
                           EGLint *width,
                           EGLint *height) const final;

    EGLint isPostSubBufferSupported() const override;
    EGLint getSwapBehavior() const override;

    angle::Result initializeContents(const gl::Context *context,
                                     GLenum binding,
                                     const gl::ImageIndex &imageIndex) override;

    vk::Framebuffer &chooseFramebuffer();

    angle::Result getCurrentFramebuffer(ContextVk *context,
                                        vk::FramebufferFetchMode fetchMode,
                                        gl::SrgbWriteControlMode writeControlMode,
                                        const vk::RenderPass &compatibleRenderPass,
                                        vk::Framebuffer *framebufferOut);

    VkSurfaceTransformFlagBitsKHR getPreTransform() const
    {
        if (mEmulatedPreTransform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        {
            return mEmulatedPreTransform;
        }
        return mPreTransform;
    }

    egl::Error setAutoRefreshEnabled(bool enabled) override;

    egl::Error getBufferAge(const gl::Context *context, EGLint *age) override;

    egl::Error setRenderBuffer(EGLint renderBuffer) override;
    bool supportsSingleRenderBuffer() const override;

    bool isSharedPresentMode() const { return IsSharedPresentMode(mSwapchainPresentMode); }

    bool isSharedPresentModeDesired() const
    {
        return IsSharedPresentMode(getDesiredSwapchainPresentMode());
    }

    egl::Error lockSurface(const egl::ThreadSafeDisplay *display,
                           EGLint usageHint,
                           bool preservePixels,
                           uint8_t **bufferPtrOut,
                           EGLint *bufferPitchOut) override;
    egl::Error unlockSurface(const egl::ThreadSafeDisplay *display, bool preservePixels) override;
    EGLint origin() const override;

    void attachToFramebuffer(const gl::Context *context, gl::Framebuffer *framebuffer) override;
    void detachFromFramebuffer(gl::Framebuffer *framebuffer) override;

    angle::Result onSharedPresentContextFlush(ContextVk *contextVk);

    bool hasStagedUpdates() const;

    void setTimestampsEnabled(bool enabled) override;
    egl::Error setPresentationTime(EGLnsecsANDROID time) override;

    egl::Error getCompressionRate(const egl::ThreadSafeDisplay *display,
                                  const gl::Context *context,
                                  EGLint *rate) override;

  protected:
    angle::Result swapImpl(ContextVk *contextVk,
                           const EGLint *rects,
                           EGLint n_rects,
                           const void *pNextChain,
                           SurfaceSwapFeedback *feedback);

    EGLNativeWindowType mNativeWindowType;
    VkSurfaceKHR mSurface;
    VkBool32 mSupportsProtectedSwapchain;

  private:
    // Present modes that are compatible with the current mode.  If mDesiredSwapchainPresentMode is
    // in this list, mode switch can happen without the need to recreate the swapchain.
    // There are currently only 6 possible present modes but vector is bigger for a workaround.
    static constexpr uint32_t kCompatiblePresentModesSize = 10;
    using CompatiblePresentModes =
        angle::FixedVector<VkPresentModeKHR, kCompatiblePresentModesSize>;

    static bool IsSharedPresentMode(vk::PresentMode presentMode)
    {
        return (presentMode == vk::PresentMode::SharedDemandRefreshKHR ||
                presentMode == vk::PresentMode::SharedContinuousRefreshKHR);
    }

    virtual angle::Result createSurfaceVk(vk::ErrorContext *context)          = 0;
    virtual angle::Result getCurrentWindowSize(vk::ErrorContext *context,
                                               gl::Extents *extentsOut) const = 0;
    virtual angle::Result getWindowVisibility(vk::ErrorContext *context, bool *isVisibleOut) const;

    impl::SurfaceSizeState getSizeState() const;
    void setSizeState(impl::SurfaceSizeState sizeState);
    angle::Result getUserExtentsImpl(vk::ErrorContext *context, VkExtent2D *extentOut) const;

    vk::PresentMode getDesiredSwapchainPresentMode() const;
    void setDesiredSwapchainPresentMode(vk::PresentMode presentMode);
    void setDesiredSwapInterval(EGLint interval);

    angle::Result initializeImpl(ThreadSafeDisplayVk *displayVk, bool *anyMatchesOut);
    // Invalidates the swapchain pointer, releases images, and defers acquire next swapchain image.
    void invalidateSwapchain(vk::Renderer *renderer);
    angle::Result recreateSwapchain(vk::ErrorContext *context);
    angle::Result createSwapchain(vk::ErrorContext *context);
    angle::Result collectOldSwapchain(vk::ErrorContext *context, VkSwapchainKHR swapchain);
    angle::Result queryAndAdjustSurfaceCaps(
        vk::ErrorContext *context,
        vk::PresentMode presentMode,
        VkSurfaceCapabilitiesKHR *surfaceCapsOut,
        CompatiblePresentModes *compatiblePresentModesOut) const;
    void adjustSurfaceExtent(VkExtent2D *extent) const;
    // This method will invalidate the swapchain (if needed due to present returning OUT_OF_DATE, or
    // swap interval changing).  Updates the current swapchain present mode.
    angle::Result prepareSwapchainForAcquireNextImage(vk::ErrorContext *context);
    void createSwapchainImages(uint32_t imageCount);
    void releaseSwapchainImages(vk::Renderer *renderer);
    void destroySwapchainImages(ThreadSafeDisplayVk *displayVk);
    angle::Result createAncillaryColorImage(vk::ErrorContext *context);
    // Called when a swapchain image whose acquisition was deferred must be acquired or unlocked
    // ANI result processed.  This method will recreate the swapchain (if needed due to surface size
    // changing etc, by calling prepareSwapchainForAcquireNextImage()) and call the
    // acquireNextSwapchainImage().  On some platforms, vkAcquireNextImageKHR() returns OUT_OF_DATE
    // instead of present, which is also recreates the swapchain.  It is scheduled to be called
    // later by deferAcquireNextImage().
    angle::Result doDeferredAcquireNextImage(vk::ErrorContext *context);
    // This method calls vkAcquireNextImageKHR() to acquire the next swapchain image or processes
    // unlocked ANI result, then sets up the acquired image.
    VkResult acquireNextSwapchainImage(vk::ErrorContext *context);
    // This method is called when a swapchain image is presented.  It schedules
    // doDeferredAcquireNextImage() to be called later.
    void deferAcquireNextImage();
    bool skipAcquireNextSwapchainImageForSharedPresentMode() const;

    angle::Result checkSwapchainOutOfDate(vk::ErrorContext *context, VkResult presentResult);
    angle::Result prePresentSubmit(ContextVk *contextVk, const vk::Semaphore &presentSemaphore);
    angle::Result recordPresentLayoutBarrierIfNecessary(ContextVk *contextVk);
    angle::Result present(ContextVk *contextVk,
                          const EGLint *rects,
                          EGLint n_rects,
                          const void *pNextChain,
                          SurfaceSwapFeedback *feedback);

    angle::Result cleanUpPresentHistory(vk::ErrorContext *context);
    angle::Result cleanUpOldSwapchains(vk::ErrorContext *context);

    bool shouldRetainColor() const;

    // Throttle the CPU such that application's logic and command buffer recording doesn't get more
    // than two frame ahead of the frame being rendered (and three frames ahead of the one being
    // presented).  This is a failsafe, as the application should ensure command buffer recording is
    // not ahead of the frame being rendered by *one* frame.
    angle::Result throttleCPU(vk::ErrorContext *context, const QueueSerial &currentSubmitSerial);

    void mergeImageResourceUses();
    // Finish all GPU operations on the surface
    angle::Result finish(vk::ErrorContext *context);

    bool isMultisampledSurface() const;
    bool hasAncillaryColor() const;

    bool supportsPresentMode(vk::PresentMode presentMode) const;

    bool updateColorSpace(ThreadSafeDisplayVk *displayVk);

    angle::FormatID getIntendedFormatID(vk::Renderer *renderer);
    angle::FormatID getActualFormatID(vk::Renderer *renderer);

    void onSubjectStateChange(angle::SubjectIndex index, angle::SubjectMessage message) override;

    bool mIsSurfaceSizedBySwapchain;

    // Atomic is to allow update state without necessarily locking the mSizeMutex.
    std::atomic<impl::SurfaceSizeState> mSizeState;
    // Protects mWidth and mHeight against getUserSize() call.
    mutable angle::SimpleMutex mSizeMutex;

    std::vector<vk::PresentMode> mPresentModes;

    VkSwapchainKHR mSwapchain;      // Current swapchain (same as last created or NULL)
    VkSwapchainKHR mLastSwapchain;  // Last created non retired swapchain (or NULL if retired)
    // Cached information used to recreate swapchains.
    vk::PresentMode mSwapchainPresentMode;                      // Current swapchain mode
    std::atomic<vk::PresentMode> mDesiredSwapchainPresentMode;  // Desired swapchain mode
    bool mPreserveOnSwap;                                       // Current swapchain behavior
    std::atomic_bool mDesiredPreserveOnSwap;                    // Desired swapchain behavior
    uint32_t mMinImageCount;
    VkSurfaceTransformFlagBitsKHR mPreTransform;
    VkSurfaceTransformFlagBitsKHR mEmulatedPreTransform;
    VkCompositeAlphaFlagBitsKHR mCompositeAlpha;
    VkColorSpaceKHR mSurfaceColorSpace;
    VkImageCompressionFlagBitsEXT mCompressionFlags;
    VkImageCompressionFixedRateFlagsEXT mFixedRateFlags;

    CompatiblePresentModes mCompatiblePresentModes;

    // A circular buffer that stores the serial of the submission fence of the context on every
    // swap. The CPU is throttled by waiting for the 2nd previous serial to finish.  This should
    // normally be a no-op, as the application should pace itself to avoid input lag, and is
    // implemented in ANGLE as a fail safe.  Removing this throttling requires untangling it from
    // acquire semaphore recycling (see mAcquireImageSemaphores above)
    angle::CircularBuffer<QueueSerial, impl::kSwapHistorySize> mSwapHistory;

    // The previous swapchain which needs to be scheduled for destruction when appropriate.  This
    // will be done when the first image of the current swapchain is presented or when fences are
    // signaled (when VK_EXT_swapchain_maintenance1 is supported).  If there were older swapchains
    // pending destruction when the swapchain is recreated, they will accumulate and be destroyed
    // with the previous swapchain.
    //
    // Note that if the user resizes the window such that the swapchain is recreated every frame,
    // this array can go grow indefinitely.
    std::deque<impl::SwapchainCleanupData> mOldSwapchains;

    std::vector<impl::SwapchainImage> mSwapchainImages;
    std::vector<angle::ObserverBinding> mSwapchainImageBindings;
    uint32_t mCurrentSwapchainImageIndex;

    // There is no direct signal from Vulkan regarding when a Present semaphore can be be reused.
    // During window resizing when swapchains are recreated every frame, the number of in-flight
    // present semaphores can grow indefinitely.  See doc/PresentSemaphores.md.
    vk::Recycler<vk::Semaphore> mPresentSemaphoreRecycler;
    // Fences are associated with present semaphores to know when they can be recycled.
    vk::Recycler<vk::Fence> mPresentFenceRecycler;

    // The presentation history, used to recycle semaphores and destroy old swapchains.
    std::deque<impl::ImagePresentOperation> mPresentHistory;

    // Depth/stencil image.  Possibly multisampled.
    vk::ImageHelper mDepthStencilImage;
    vk::ImageViewHelper mDepthStencilImageViews;
    angle::ObserverBinding mDepthStencilImageBinding;

    // Ancillary color image, view and framebuffer, if multisampling or preserved swap behavior is
    // enabled.
    vk::ImageHelper mAncillaryColorImage;
    vk::ImageViewHelper mAncillaryColorImageViews;
    angle::ObserverBinding mAncillaryColorImageBinding;
    vk::Framebuffer mAncillaryFramebuffer;

    impl::ImageAcquireOperation mAcquireOperation;

    // EGL_EXT_buffer_age: Track frame count.
    uint64_t mFrameCount;
    // The frame in which swap behavior is set to PRESERVE.
    uint64_t mPreserveStartFrame;
    // EGL_ANDROID_presentation_time: Next frame's id and presentation time
    // used for VK_GOOGLE_display_timing.
    uint32_t mPresentID;
    std::optional<EGLnsecsANDROID> mDesiredPresentTime;

    // EGL_KHR_lock_surface3
    vk::BufferHelper mLockBufferHelper;

    // EGL_KHR_partial_update
    bool mIsBufferAgeQueried;

    // GL_EXT_sRGB_write_control
    gl::SrgbWriteControlMode mWriteControlMode = gl::SrgbWriteControlMode::Default;

    // GL_EXT_shader_framebuffer_fetch
    vk::FramebufferFetchMode mFramebufferFetchMode = vk::FramebufferFetchMode::None;

    vk::Renderer *mRenderer;
};

}  // namespace rx

#endif  // LIBANGLE_RENDERER_VULKAN_SURFACEVK_H_
