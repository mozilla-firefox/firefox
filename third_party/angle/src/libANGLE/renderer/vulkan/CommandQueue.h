//
// Copyright 2020 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// CommandQueue.h:
//    A class to process and submit Vulkan command buffers.
//

#ifndef LIBANGLE_RENDERER_VULKAN_COMMAND_Queue_H_
#define LIBANGLE_RENDERER_VULKAN_COMMAND_Queue_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include "common/FixedQueue.h"
#include "common/SimpleMutex.h"
#include "common/vulkan/vk_headers.h"
#include "libANGLE/renderer/vulkan/PersistentCommandPool.h"
#include "libANGLE/renderer/vulkan/vk_helpers.h"
#include "vulkan/vulkan_core.h"

namespace rx
{
namespace vk
{
class ExternalFence;
using SharedExternalFence = std::shared_ptr<ExternalFence>;

constexpr size_t kInFlightCommandsLimit    = 50u;
constexpr size_t kMaxFinishedCommandsLimit = 64u;
static_assert(kInFlightCommandsLimit <= kMaxFinishedCommandsLimit);

struct CommandQueuePerfCounters
{
    CommandQueuePerfCounters() = default;
    CommandQueuePerfCounters(const CommandQueuePerfCounters &other)
        : queueSubmitCallsTotal(other.queueSubmitCallsTotal.load(std::memory_order_relaxed)),
          vkQueueSubmitCallsTotal(other.vkQueueSubmitCallsTotal.load(std::memory_order_relaxed)),
          queueWaitSemaphoresTotal(other.queueWaitSemaphoresTotal.load(std::memory_order_relaxed))
    {}

    CommandQueuePerfCounters &operator=(const CommandQueuePerfCounters &other)
    {
        queueSubmitCallsTotal.store(other.queueSubmitCallsTotal.load(std::memory_order_relaxed),
                                    std::memory_order_relaxed);
        vkQueueSubmitCallsTotal.store(other.vkQueueSubmitCallsTotal.load(std::memory_order_relaxed),
                                      std::memory_order_relaxed);
        queueWaitSemaphoresTotal.store(
            other.queueWaitSemaphoresTotal.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        return *this;
    }

    std::atomic<uint64_t> queueSubmitCallsTotal{0};
    std::atomic<uint64_t> vkQueueSubmitCallsTotal{0};
    std::atomic<uint64_t> queueWaitSemaphoresTotal{0};
};

struct Error
{
    VkResult errorCode;
    const char *file;
    const char *function;
    uint32_t line;
};

class FenceRecycler
{
  public:
    FenceRecycler() {}
    ~FenceRecycler() {}
    void destroy(ErrorContext *context);

    void fetch(VkDevice device, Fence *fenceOut);
    void recycle(Fence &&fence);

  private:
    angle::SimpleMutex mMutex;
    Recycler<Fence> mRecycler;
};

class RecyclableFence final : angle::NonCopyable
{
  public:
    RecyclableFence();
    ~RecyclableFence();

    VkResult init(VkDevice device, FenceRecycler *recycler);
    // Returns fence back to the recycler if it is still attached, destroys the fence otherwise.
    // Do NOT call directly when object is controlled by a shared pointer.
    void destroy(VkDevice device);
    void detachRecycler() { mRecycler = nullptr; }

    bool valid() const { return mFence.valid(); }
    const Fence &get() const { return mFence; }

  private:
    Fence mFence;
    FenceRecycler *mRecycler;
};

using SharedFence = AtomicSharedPtr<RecyclableFence>;

class CommandPoolAccess;
class CommandBatch final : angle::NonCopyable
{
  public:
    CommandBatch();
    ~CommandBatch();
    CommandBatch(CommandBatch &&other);
    CommandBatch &operator=(CommandBatch &&other);

    void destroy(VkDevice device);
    angle::Result release(ErrorContext *context, WhenToResetCommandBuffer whenToReset);

    void setQueueSerial(const QueueSerial &serial);
    void setProtectionType(ProtectionType protectionType);
    void setPrimaryCommands(PrimaryCommandBuffer &&primaryCommands,
                            CommandPoolAccess *commandPoolAccess);
    void setSecondaryCommands(SecondaryCommandBufferCollector &&secondaryCommands);
    VkResult initFence(VkDevice device, FenceRecycler *recycler);
    void setExternalFence(SharedExternalFence &&externalFence);

    const QueueSerial &getQueueSerial() const;
    const PrimaryCommandBuffer &getPrimaryCommands() const;
    const SharedExternalFence &getExternalFence();

    // Accessing the shared fence is prioritized before the shared external fence, since the shared
    // fence may be used in an extra empty submission after the external fence (via a feature flag).
    bool hasFence() const;
    VkFence getFenceHandle() const;
    VkResult getFenceStatus(VkDevice device) const;
    VkResult waitFence(VkDevice device, uint64_t timeout) const;
    VkResult waitFenceUnlocked(VkDevice device,
                               uint64_t timeout,
                               std::unique_lock<angle::SimpleMutex> *lock) const;

  private:
    QueueSerial mQueueSerial;
    ProtectionType mProtectionType;
    PrimaryCommandBuffer mPrimaryCommands;
    CommandPoolAccess *mCommandPoolAccess;  // reference to CommandPoolAccess that is responsible
                                            // for deleting primaryCommands with a lock
    SecondaryCommandBufferCollector mSecondaryCommands;
    SharedFence mFence;
    SharedExternalFence mExternalFence;
};
using CommandBatchQueue = angle::FixedQueue<CommandBatch>;

class DeviceQueueMap;

class QueueFamily final : angle::NonCopyable
{
  public:
    static const uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

    static uint32_t FindIndex(const std::vector<VkQueueFamilyProperties2> &queueFamilyProperties2,
                              VkQueueFlags includeFlags,
                              VkQueueFlags optionalFlags,
                              VkQueueFlags excludeFlags,
                              uint32_t *matchCount);

    static constexpr float kQueuePriorityLow      = 0.0f;
    static constexpr float kQueuePriorityMedium   = 0.4f;
    static constexpr float kQueuePriorityHigh     = 0.8f;
    static constexpr float kQueuePriorityRealtime = 1.0f;

    static constexpr std::array<float, static_cast<uint32_t>(egl::ContextPriority::EnumCount)>
        kQueuePriorities = {kQueuePriorityMedium, kQueuePriorityHigh, kQueuePriorityRealtime,
                            kQueuePriorityLow};

    QueueFamily() : mProperties{}, mQueueFamilyIndex(kInvalidIndex) {}
    ~QueueFamily() {}

    void initialize(const VkQueueFamilyProperties &queueFamilyProperties,
                    uint32_t queueFamilyIndex);
    bool valid() const { return (mQueueFamilyIndex != kInvalidIndex); }
    uint32_t getQueueFamilyIndex() const { return mQueueFamilyIndex; }
    const VkQueueFamilyProperties *getProperties() const { return &mProperties; }
    bool isGraphics() const { return ((mProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) > 0); }
    bool isCompute() const { return ((mProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) > 0); }
    bool supportsProtected() const
    {
        return ((mProperties.queueFlags & VK_QUEUE_PROTECTED_BIT) > 0);
    }
    uint32_t getDeviceQueueCount() const { return mProperties.queueCount; }

  private:
    VkQueueFamilyProperties mProperties;
    uint32_t mQueueFamilyIndex;
};

class DeviceQueueMap final
{
  public:
    DeviceQueueMap() : mQueueFamilyIndex(QueueFamily::kInvalidIndex), mIsProtected(false) {}
    ~DeviceQueueMap();

    void initialize(VkDevice device,
                    const QueueFamily &queueFamily,
                    bool makeProtected,
                    uint32_t queueIndex,
                    uint32_t queueCount);
    void destroy();

    bool valid() const { return (mQueueFamilyIndex != QueueFamily::kInvalidIndex); }
    uint32_t getQueueFamilyIndex() const { return mQueueFamilyIndex; }
    bool isProtected() const { return mIsProtected; }
    egl::ContextPriority getDevicePriority(egl::ContextPriority priority) const
    {
        return mQueueAndIndices[priority].devicePriority;
    }
    DeviceQueueIndex getDeviceQueueIndex(egl::ContextPriority priority) const
    {
        return DeviceQueueIndex(mQueueFamilyIndex, mQueueAndIndices[priority].index);
    }
    const VkQueue &getQueue(egl::ContextPriority priority) const
    {
        return mQueueAndIndices[priority].queue;
    }

    // Wait for all queues to be idle, called on device loss and destruction.
    void waitAllQueuesIdle();

  private:
    uint32_t mQueueFamilyIndex;
    bool mIsProtected;
    struct QueueAndIndex
    {
        // The actual priority that used
        egl::ContextPriority devicePriority;
        VkQueue queue;
        // The queueIndex used for VkGetDeviceQueue
        uint32_t index;
    };
    angle::PackedEnumMap<egl::ContextPriority, QueueAndIndex> mQueueAndIndices;
};

class CommandsState : angle::NonCopyable
{
  public:
    CommandsState(Renderer *renderer,
                  ProtectionType protectionType,
                  egl::ContextPriority contextPriority);
    ~CommandsState();

    void destroy(VkDevice device);

    angle::Result flushOutsideRPCommands(Context *context,
                                         OutsideRenderPassCommandBufferHelper **outsideRPCommands)
    {
        ANGLE_TRACE_EVENT0("gpu.angle", "CommandsState::flushOutsideRPCommands");
        std::lock_guard<angle::SimpleMutex> lock(mCmdPoolMutex);
        ANGLE_TRY(ensurePrimaryCommandBufferValidLocked(context));
        ANGLE_TRY((*outsideRPCommands)->flushToPrimary(context, this, &mPrimaryCommands));
        // Restart the command buffer.
        return (*outsideRPCommands)->reset(context, &mSecondaryCommands);
    }

    angle::Result flushRenderPassCommands(Context *context,
                                          const RenderPass &renderPass,
                                          VkFramebuffer framebufferOverride,
                                          RenderPassCommandBufferHelper **renderPassCommands)
    {
        ANGLE_TRACE_EVENT0("gpu.angle", "CommandsState::flushRenderPassCommands");
        std::lock_guard<angle::SimpleMutex> lock(mCmdPoolMutex);
        ANGLE_TRY(ensurePrimaryCommandBufferValidLocked(context));
        ANGLE_TRY((*renderPassCommands)
                      ->flushToPrimary(context, this, &mPrimaryCommands, renderPass,
                                       framebufferOverride));
        // Restart the command buffer.
        return (*renderPassCommands)->reset(context, &mSecondaryCommands);
    }

    angle::Result flushImagesTransitionToForeign(
        Context *context,
        std::vector<VkImageMemoryBarrier> &&imagesToTransitionToForeign)
    {
        std::lock_guard<angle::SimpleMutex> lock(mCmdPoolMutex);
        // Usually we have foreign images to transit, we must have already issued some barrier,
        // which means command buffer can't be empty. But if we flush and submit outsideRPCommands
        // only, we may still end up with invalid primary command buffer.
        ANGLE_TRY(ensurePrimaryCommandBufferValidLocked(context));

        mPrimaryCommands.pipelineBarrier(
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
            nullptr, static_cast<uint32_t>(imagesToTransitionToForeign.size()),
            imagesToTransitionToForeign.data());
        imagesToTransitionToForeign.clear();
        return angle::Result::Continue;
    }

    angle::Result getCommandsAndWaitSemaphores(
        ErrorContext *context,
        CommandPoolAccess *commandPoolAccess,
        CommandBatch *batch,
        std::vector<VkSemaphore> *waitSemaphoresOut,
        std::vector<VkPipelineStageFlags> *waitSemaphoreStageMasksOut);

    void addWaitSemaphore(VkSemaphore waitSemaphores, VkPipelineStageFlags waitSemaphoreStageMasks)
    {
        mWaitSemaphores.emplace_back(waitSemaphores);
        mWaitSemaphoreStageMasks.emplace_back(waitSemaphoreStageMasks);
    }

    bool hasWaitSemaphoresPendingSubmission() const { return !mWaitSemaphores.empty(); }

    void setPriority(egl::ContextPriority newPriority) { mPriority = newPriority; }
    egl::ContextPriority getPriority() const { return mPriority; }
    ProtectionType getProtectionType() const { return mProtectionType; }

    angle::Result insertSubmitDebugMarker(ErrorContext *context, QueueSubmitReason reason);

  private:
    angle::Result ensurePrimaryCommandBufferValidLocked(ErrorContext *context);

    // Command pool mutex lock shared with CommandPoolAccess
    angle::SimpleMutex &mCmdPoolMutex;
    // This is immutable
    const vk::ProtectionType mProtectionType;
    egl::ContextPriority mPriority;

    std::vector<VkSemaphore> mWaitSemaphores;
    std::vector<VkPipelineStageFlags> mWaitSemaphoreStageMasks;
    PrimaryCommandBuffer mPrimaryCommands;
    SecondaryCommandBufferCollector mSecondaryCommands;
};

class CommandPoolAccess : angle::NonCopyable
{
  public:
    CommandPoolAccess();
    ~CommandPoolAccess();
    angle::Result initCommandPool(ErrorContext *context,
                                  ProtectionType protectionType,
                                  const uint32_t queueFamilyIndex);
    void destroy(VkDevice device);
    void destroyPrimaryCommandBuffer(VkDevice device, PrimaryCommandBuffer *primaryCommands) const;
    angle::Result collectPrimaryCommandBuffer(ErrorContext *context,
                                              const ProtectionType protectionType,
                                              PrimaryCommandBuffer *primaryCommands,
                                              WhenToResetCommandBuffer whenToReset);

  private:
    angle::Result allocatePrimaryCommandBufferLocked(ErrorContext *context,
                                                     const ProtectionType &protectionType,
                                                     PrimaryCommandBuffer *primaryCommandsOut)
    {
        return mPrimaryCommandPoolMap[protectionType].allocate(context, primaryCommandsOut);
    }

    // This mutex ensures vulkan command pool is externally synchronized.
    // This means no two threads are operating on command buffers allocated from
    // the same command pool at the same time. The operations that this mutex
    // protect include:
    // 1) recording commands on any command buffers allocated from the same command pool
    // 2) allocate, free, reset command buffers from the same command pool.
    // 3) any operations on the command pool itself
    friend class CommandsState;
    mutable angle::SimpleMutex mCmdPoolMutex;

    using PrimaryCommandPoolMap = angle::PackedEnumMap<ProtectionType, PersistentCommandPool>;
    // Keeps a free list of reusable primary command buffers.
    PrimaryCommandPoolMap mPrimaryCommandPoolMap;
};

// Note all public APIs of CommandQueue class must be thread safe.
class CommandQueue : angle::NonCopyable
{
  public:
    CommandQueue();
    ~CommandQueue();

    angle::Result init(ErrorContext *context,
                       const QueueFamily &queueFamily,
                       bool enableProtectedContent,
                       uint32_t queueCount);

    void destroy(ErrorContext *context);

    void handleDeviceLost(Renderer *renderer);

    // These public APIs are inherently thread safe. Thread unsafe methods must be protected methods
    // that are only accessed via ThreadSafeCommandQueue API.
    egl::ContextPriority getDriverPriority(egl::ContextPriority priority) const
    {
        return mQueueMap.getDevicePriority(priority);
    }

    DeviceQueueIndex getDeviceQueueIndex(egl::ContextPriority priority) const
    {
        return mQueueMap.getDeviceQueueIndex(priority);
    }

    VkQueue getQueue(egl::ContextPriority priority) const { return mQueueMap.getQueue(priority); }
    // The following are used to implement EGL_ANGLE_device_vulkan, and are called by the
    // application when it wants to access the VkQueue previously retrieved from ANGLE.  Do not call
    // these for synchronization within ANGLE.
    void lockVulkanQueueForExternalAccess() { mQueueSubmitMutex.lock(); }
    void unlockVulkanQueueForExternalAccess() { mQueueSubmitMutex.unlock(); }

    Serial getLastSubmittedSerial(SerialIndex index) const { return mLastSubmittedSerials[index]; }

    // The ResourceUse still have unfinished queue serial by ANGLE or vulkan.
    bool hasResourceUseFinished(const ResourceUse &use) const
    {
        return use <= mLastCompletedSerials;
    }
    bool hasQueueSerialFinished(const QueueSerial &queueSerial) const
    {
        return queueSerial <= mLastCompletedSerials;
    }
    // The ResourceUse still have queue serial not yet submitted to vulkan.
    bool hasResourceUseSubmitted(const ResourceUse &use) const
    {
        return use <= mLastSubmittedSerials;
    }
    bool hasQueueSerialSubmitted(const QueueSerial &queueSerial) const
    {
        return queueSerial <= mLastSubmittedSerials;
    }

    // Wait until the desired serial has been completed.
    angle::Result finishResourceUse(ErrorContext *context,
                                    const ResourceUse &use,
                                    uint64_t timeout);
    angle::Result finishQueueSerial(ErrorContext *context,
                                    const QueueSerial &queueSerial,
                                    uint64_t timeout);
    angle::Result waitIdle(ErrorContext *context, uint64_t timeout);
    angle::Result waitForResourceUseToFinishWithUserTimeout(ErrorContext *context,
                                                            const ResourceUse &use,
                                                            uint64_t timeout,
                                                            VkResult *result);
    bool isBusy(Renderer *renderer) const;

    angle::Result submitCommands(ErrorContext *context,
                                 VkSemaphore signalSemaphore,
                                 SharedExternalFence &&externalFence,
                                 const QueueSerial &submitQueueSerial,
                                 CommandsState &&commandsState);

    angle::Result queueSubmitOneOff(ErrorContext *context,
                                    ProtectionType protectionType,
                                    egl::ContextPriority contextPriority,
                                    VkCommandBuffer commandBufferHandle,
                                    VkSemaphore waitSemaphore,
                                    VkPipelineStageFlags waitSemaphoreStageMask,
                                    const QueueSerial &submitQueueSerial);

    // Note: Some errors from present are not fatal.
    VkResult queuePresent(egl::ContextPriority contextPriority,
                          const VkPresentInfoKHR &presentInfo);

    angle::Result checkCompletedCommands(ErrorContext *context)
    {
        std::lock_guard<angle::SimpleMutex> lock(mCmdCompleteMutex);
        return checkCompletedCommandsLocked(context);
    }

    bool hasFinishedCommands() const { return !mFinishedCommandBatches.empty(); }

    angle::Result checkAndCleanupCompletedCommands(ErrorContext *context)
    {
        ANGLE_TRY(checkCompletedCommands(context));

        if (!mFinishedCommandBatches.empty())
        {
            ANGLE_TRY(releaseFinishedCommandsAndCleanupGarbage(context));
        }

        return angle::Result::Continue;
    }

    CommandPoolAccess &getCommandPoolAccess() { return mCommandPoolAccess; }

    const CommandQueuePerfCounters getPerfCounters() const;

    // Release finished commands and clean up garbage immediately, or request async clean up if
    // enabled.
    angle::Result releaseFinishedCommandsAndCleanupGarbage(ErrorContext *context);
    angle::Result releaseFinishedCommands(ErrorContext *context,
                                          WhenToResetCommandBuffer whenToReset)
    {
        std::lock_guard<angle::SimpleMutex> lock(mCmdReleaseMutex);
        return releaseFinishedCommandsLocked(context, whenToReset);
    }
    angle::Result postSubmitCheck(ErrorContext *context);

    bool isInFlightCommandsEmpty() const;

    // Try to cleanup garbage and return if something was cleaned.  Otherwise, wait for the
    // mInFlightCommands and retry.
    angle::Result cleanupSomeGarbage(ErrorContext *context,
                                     size_t minInFlightBatchesToKeep,
                                     bool *anyGarbageCleanedOut);

    // All these private APIs are called with mutex locked, so we must not take lock again.
  private:
    // Check the first command buffer in mInFlightCommands and update mLastCompletedSerials if
    // finished
    angle::Result checkOneCommandBatchLocked(ErrorContext *context, bool *finished);
    // Similar to checkOneCommandBatch, except we will wait for it to finish
    angle::Result finishOneCommandBatch(ErrorContext *context,
                                        uint64_t timeout,
                                        std::unique_lock<angle::SimpleMutex> *lock);
    void onCommandBatchFinishedLocked(CommandBatch &&batch);
    // Walk mFinishedCommands, reset and recycle all command buffers.
    angle::Result releaseFinishedCommandsLocked(ErrorContext *context,
                                                WhenToResetCommandBuffer whenToReset);
    // Walk mInFlightCommands, check and update mLastCompletedSerials for all commands that are
    // finished
    angle::Result checkCompletedCommandsLocked(ErrorContext *context);

    angle::Result queueSubmitLocked(ErrorContext *context,
                                    egl::ContextPriority contextPriority,
                                    const VkSubmitInfo &submitInfo,
                                    DeviceScoped<CommandBatch> &commandBatch,
                                    const QueueSerial &submitQueueSerial);

    void pushInFlightBatchLocked(CommandBatch &&batch);
    void moveInFlightBatchToFinishedQueueLocked(CommandBatch &&batch);
    void popFinishedBatchLocked();
    void popInFlightBatchLocked();

    CommandPoolAccess mCommandPoolAccess;

    // Warning: Mutexes must be locked in the order as declared below.
    // Protect multi-thread access to mInFlightCommands.push/back and ensure ordering of submission.
    mutable angle::SimpleMutex mQueueSubmitMutex;
    // Protect multi-thread access to mInFlightCommands.pop/front and
    // mFinishedCommandBatches.push/back.
    angle::SimpleMutex mCmdCompleteMutex;
    // Protect multi-thread access to mFinishedCommandBatches.pop/front.
    angle::SimpleMutex mCmdReleaseMutex;

    FenceRecycler mFenceRecycler;

    CommandBatchQueue mInFlightCommands;
    // Temporary storage for finished command batches that should be reset.
    CommandBatchQueue mFinishedCommandBatches;

    // Combined number of batches in mInFlightCommands and mFinishedCommandBatches queues.
    // Used instead of calculating the sum because doing this is not thread safe and will require
    // the mCmdCompleteMutex lock.
    std::atomic_size_t mNumAllCommands;

    // Queue serial management.
    AtomicQueueSerialFixedArray mLastSubmittedSerials;
    // This queue serial can be read/write from different threads, so we need to use atomic
    // operations to access the underlying value. Since we only do load/store on this value, it
    // should be just a normal uint64_t load/store on most platforms.
    AtomicQueueSerialFixedArray mLastCompletedSerials;

    // QueueMap
    DeviceQueueMap mQueueMap;

    CommandQueuePerfCounters mPerfCounters;
};

ANGLE_INLINE bool CommandQueue::isInFlightCommandsEmpty() const
{
    return mInFlightCommands.empty();
}

// A helper thread used to clean up garbage
class CleanUpThread : public ErrorContext
{
  public:
    CleanUpThread(Renderer *renderer, CommandQueue *commandQueue);
    ~CleanUpThread() override;

    // Context
    void handleError(VkResult result,
                     const char *file,
                     const char *function,
                     unsigned int line) override;

    angle::Result init();

    void destroy(ErrorContext *context);

    void requestCleanUp();

    std::thread::id getThreadId() const { return mTaskThread.get_id(); }

  private:
    bool hasPendingError() const
    {
        std::lock_guard<angle::SimpleMutex> queueLock(mErrorMutex);
        return !mErrors.empty();
    }
    angle::Result checkAndPopPendingError(ErrorContext *errorHandlingContext);

    // Entry point for clean up thread, calls processTasksImpl to do the
    // work. called by Renderer::initializeDevice on main thread
    void processTasks();

    // Clean up thread, called by processTasks. The loop waits for work to
    // be submitted from a separate thread.
    angle::Result processTasksImpl(bool *exitThread);

    CommandQueue *const mCommandQueue;

    mutable angle::SimpleMutex mErrorMutex;
    std::queue<Error> mErrors;

    // Command queue worker thread.
    std::thread mTaskThread;
    bool mTaskThreadShouldExit;
    std::mutex mMutex;
    std::condition_variable mWorkAvailableCondition;
    std::atomic<bool> mNeedCleanUp;
};

// Provides access to the PrimaryCommandBuffer while also locking the corresponding CommandPool
class [[nodiscard]] ScopedPrimaryCommandBuffer final
{
  public:
    explicit ScopedPrimaryCommandBuffer(VkDevice device) : mCommandBuffer(device) {}

    void assign(std::unique_lock<angle::SimpleMutex> &&poolLock,
                PrimaryCommandBuffer &&commandBuffer)
    {
        ASSERT(poolLock.owns_lock());
        ASSERT(commandBuffer.valid());
        ASSERT(mPoolLock.mutex() == nullptr);
        ASSERT(!mCommandBuffer.get().valid());
        mPoolLock            = std::move(poolLock);
        mCommandBuffer.get() = std::move(commandBuffer);
    }

    PrimaryCommandBuffer &get()
    {
        ASSERT(mPoolLock.owns_lock());
        ASSERT(mCommandBuffer.get().valid());
        return mCommandBuffer.get();
    }

    DeviceScoped<PrimaryCommandBuffer> unlockAndRelease()
    {
        ASSERT((mCommandBuffer.get().valid() && mPoolLock.owns_lock()) ||
               (!mCommandBuffer.get().valid() && mPoolLock.mutex() == nullptr));
        mPoolLock = {};
        return std::move(mCommandBuffer);
    }

  private:
    std::unique_lock<angle::SimpleMutex> mPoolLock;
    DeviceScoped<PrimaryCommandBuffer> mCommandBuffer;
};
}  // namespace vk

}  // namespace rx

#endif  // LIBANGLE_RENDERER_VULKAN_COMMAND_QUEUE_H_
