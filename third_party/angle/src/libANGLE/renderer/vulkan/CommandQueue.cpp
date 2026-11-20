//
// Copyright 2020 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// CommandQueue.cpp:
//    Implements the class methods for CommandQueue.
//

#include "libANGLE/renderer/vulkan/CommandQueue.h"
#include <algorithm>
#include "common/system_utils.h"
#include "libANGLE/renderer/vulkan/SyncVk.h"
#include "libANGLE/renderer/vulkan/vk_renderer.h"
#include "vulkan/vulkan_core.h"

namespace rx
{
namespace vk
{
namespace
{
constexpr bool kOutputVmaStatsString = false;
// When suballocation garbages is more than this, we may wait for GPU to finish and free up some
// memory for allocation.
constexpr VkDeviceSize kMaxBufferSuballocationGarbageSize = 64 * 1024 * 1024;

void InitializeSubmitInfo(VkSubmitInfo *submitInfo,
                          const PrimaryCommandBuffer &commandBuffer,
                          const std::vector<VkSemaphore> &waitSemaphores,
                          const std::vector<VkPipelineStageFlags> &waitSemaphoreStageMasks,
                          const VkSemaphore &signalSemaphore)
{
    // Verify that the submitInfo has been zero'd out.
    ASSERT(submitInfo->signalSemaphoreCount == 0);
    ASSERT(waitSemaphores.size() == waitSemaphoreStageMasks.size());
    submitInfo->sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo->commandBufferCount = commandBuffer.valid() ? 1 : 0;
    submitInfo->pCommandBuffers    = commandBuffer.ptr();
    submitInfo->waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
    submitInfo->pWaitSemaphores    = waitSemaphores.empty() ? nullptr : waitSemaphores.data();
    submitInfo->pWaitDstStageMask  = waitSemaphoreStageMasks.data();

    if (signalSemaphore != VK_NULL_HANDLE)
    {
        submitInfo->signalSemaphoreCount = 1;
        submitInfo->pSignalSemaphores    = &signalSemaphore;
    }
}

void GetDeviceQueue(VkDevice device,
                    bool makeProtected,
                    uint32_t queueFamilyIndex,
                    uint32_t queueIndex,
                    VkQueue *queue)
{
    if (makeProtected)
    {
        VkDeviceQueueInfo2 queueInfo2 = {};
        queueInfo2.sType              = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2;
        queueInfo2.flags              = VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT;
        queueInfo2.queueFamilyIndex   = queueFamilyIndex;
        queueInfo2.queueIndex         = queueIndex;

        VK_CALL(vkGetDeviceQueue2, device, &queueInfo2, queue);
    }
    else
    {
        VK_CALL(vkGetDeviceQueue, device, queueFamilyIndex, queueIndex, queue);
    }
}
}  // namespace

// RecyclableFence implementation
RecyclableFence::RecyclableFence() : mRecycler(nullptr) {}

RecyclableFence::~RecyclableFence()
{
    ASSERT(!valid());
}

VkResult RecyclableFence::init(VkDevice device, FenceRecycler *recycler)
{
    ASSERT(!valid());
    ASSERT(mRecycler == nullptr);

    // First try to fetch from recycler. If that failed, try to create a new VkFence
    recycler->fetch(device, &mFence);
    if (!valid())
    {
        VkFenceCreateInfo fenceCreateInfo = {};
        fenceCreateInfo.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags             = 0;
        VkResult result                   = mFence.init(device, fenceCreateInfo);
        if (result != VK_SUCCESS)
        {
            ASSERT(!valid());
            return result;
        }
        ASSERT(valid());
    }

    mRecycler = recycler;

    return VK_SUCCESS;
}

void RecyclableFence::destroy(VkDevice device)
{
    if (valid())
    {
        if (mRecycler != nullptr)
        {
            mFence.reset(device);
            mRecycler->recycle(std::move(mFence));
        }
        else
        {
            // Recycler was detached - destroy the fence.
            mFence.destroy(device);
        }
        ASSERT(!valid());
    }
}

// FenceRecycler implementation
void FenceRecycler::destroy(ErrorContext *context)
{
    std::lock_guard<angle::SimpleMutex> lock(mMutex);
    mRecycler.destroy(context->getDevice());
}

void FenceRecycler::fetch(VkDevice device, Fence *fenceOut)
{
    ASSERT(fenceOut != nullptr && !fenceOut->valid());
    std::lock_guard<angle::SimpleMutex> lock(mMutex);
    if (!mRecycler.empty())
    {
        mRecycler.fetch(fenceOut);
    }
}

void FenceRecycler::recycle(Fence &&fence)
{
    std::lock_guard<angle::SimpleMutex> lock(mMutex);
    mRecycler.recycle(std::move(fence));
}

// CommandBatch implementation.
CommandBatch::CommandBatch()
    : mProtectionType(ProtectionType::InvalidEnum), mCommandPoolAccess(nullptr)
{}

CommandBatch::~CommandBatch() = default;

CommandBatch::CommandBatch(CommandBatch &&other) : CommandBatch()
{
    *this = std::move(other);
}

CommandBatch &CommandBatch::operator=(CommandBatch &&other)
{
    std::swap(mQueueSerial, other.mQueueSerial);
    std::swap(mProtectionType, other.mProtectionType);
    std::swap(mPrimaryCommands, other.mPrimaryCommands);
    std::swap(mCommandPoolAccess, other.mCommandPoolAccess);
    std::swap(mSecondaryCommands, other.mSecondaryCommands);
    std::swap(mFence, other.mFence);
    std::swap(mExternalFence, other.mExternalFence);
    return *this;
}

ANGLE_INLINE void CommandBatch::destroy(VkDevice device)
{
    if (mPrimaryCommands.valid())
    {
        ASSERT(mCommandPoolAccess != nullptr);
        mCommandPoolAccess->destroyPrimaryCommandBuffer(device, &mPrimaryCommands);
    }
    mSecondaryCommands.releaseCommandBuffers();
    if (mFence)
    {
        mFence->detachRecycler();
        mFence.reset();
    }
    mExternalFence.reset();
    // Do not clean other members to catch invalid reuse attempt with ASSERTs.
}

ANGLE_INLINE angle::Result CommandBatch::release(ErrorContext *context,
                                                 WhenToResetCommandBuffer whenToReset)
{
    if (mPrimaryCommands.valid())
    {
        ASSERT(mCommandPoolAccess != nullptr);
        ANGLE_TRY(mCommandPoolAccess->collectPrimaryCommandBuffer(context, mProtectionType,
                                                                  &mPrimaryCommands, whenToReset));
    }
    mSecondaryCommands.releaseCommandBuffers();
    mFence.reset();
    mExternalFence.reset();
    // Do not clean other members to catch invalid reuse attempt with ASSERTs.
    return angle::Result::Continue;
}

ANGLE_INLINE void CommandBatch::setQueueSerial(const QueueSerial &serial)
{
    ASSERT(serial.valid());
    ASSERT(!mQueueSerial.valid());
    mQueueSerial = serial;
}

ANGLE_INLINE void CommandBatch::setProtectionType(ProtectionType protectionType)
{
    ASSERT(protectionType != ProtectionType::InvalidEnum);
    ASSERT(mProtectionType == ProtectionType::InvalidEnum);
    mProtectionType = protectionType;
}

ANGLE_INLINE void CommandBatch::setPrimaryCommands(PrimaryCommandBuffer &&primaryCommands,
                                                   CommandPoolAccess *commandPoolAccess)
{
    // primaryCommands is optional.
    ASSERT(!(primaryCommands.valid() && commandPoolAccess == nullptr));
    ASSERT(!mPrimaryCommands.valid());
    ASSERT(mCommandPoolAccess == nullptr);
    mPrimaryCommands   = std::move(primaryCommands);
    mCommandPoolAccess = commandPoolAccess;
}

ANGLE_INLINE void CommandBatch::setSecondaryCommands(
    SecondaryCommandBufferCollector &&secondaryCommands)
{
    // secondaryCommands is optional.
    ASSERT(mSecondaryCommands.empty());
    mSecondaryCommands = std::move(secondaryCommands);
}

ANGLE_INLINE VkResult CommandBatch::initFence(VkDevice device, FenceRecycler *recycler)
{
    ASSERT(!hasFence());
    auto fence            = SharedFence::MakeShared(device);
    const VkResult result = fence->init(device, recycler);
    if (result == VK_SUCCESS)
    {
        ASSERT(fence->valid());
        mFence = std::move(fence);
    }
    return result;
}

ANGLE_INLINE void CommandBatch::setExternalFence(SharedExternalFence &&externalFence)
{
    ASSERT(!mExternalFence);
    mExternalFence = std::move(externalFence);
}

ANGLE_INLINE const QueueSerial &CommandBatch::getQueueSerial() const
{
    ASSERT(mQueueSerial.valid());
    return mQueueSerial;
}

ANGLE_INLINE const PrimaryCommandBuffer &CommandBatch::getPrimaryCommands() const
{
    return mPrimaryCommands;
}

ANGLE_INLINE const SharedExternalFence &CommandBatch::getExternalFence()
{
    return mExternalFence;
}

ANGLE_INLINE bool CommandBatch::hasFence() const
{
    ASSERT(!mFence || mFence->valid());
    return mFence || mExternalFence;
}

ANGLE_INLINE VkFence CommandBatch::getFenceHandle() const
{
    ASSERT(hasFence());
    return mFence ? mFence->get().getHandle() : mExternalFence->getHandle();
}

ANGLE_INLINE VkResult CommandBatch::getFenceStatus(VkDevice device) const
{
    ASSERT(hasFence());
    return mFence ? mFence->get().getStatus(device) : mExternalFence->getStatus(device);
}

ANGLE_INLINE VkResult CommandBatch::waitFence(VkDevice device, uint64_t timeout) const
{
    ASSERT(hasFence());
    return mFence ? mFence->get().wait(device, timeout) : mExternalFence->wait(device, timeout);
}

ANGLE_INLINE VkResult
CommandBatch::waitFenceUnlocked(VkDevice device,
                                uint64_t timeout,
                                std::unique_lock<angle::SimpleMutex> *lock) const
{
    ASSERT(hasFence());
    VkResult status;
    // You can only use the local copy of the fence without lock.
    // Do not access "this" after unlock() because object might be deleted from other thread.
    if (mFence)
    {
        const SharedFence localFenceToWaitOn = mFence;
        lock->unlock();
        status = localFenceToWaitOn->get().wait(device, timeout);
        lock->lock();
    }
    else
    {
        const SharedExternalFence localFenceToWaitOn = mExternalFence;
        lock->unlock();
        status = localFenceToWaitOn->wait(device, timeout);
        lock->lock();
    }
    return status;
}

// CleanUpThread implementation.
void CleanUpThread::handleError(VkResult errorCode,
                                const char *file,
                                const char *function,
                                unsigned int line)
{
    ASSERT(errorCode != VK_SUCCESS);

    std::stringstream errorStream;
    errorStream << "Internal Vulkan error (" << errorCode << "): " << VulkanResultString(errorCode)
                << ".";

    if (errorCode == VK_ERROR_DEVICE_LOST)
    {
        WARN() << errorStream.str();
        mCommandQueue->handleDeviceLost(mRenderer);
    }

    std::lock_guard<angle::SimpleMutex> queueLock(mErrorMutex);
    Error error = {errorCode, file, function, line};
    mErrors.emplace(error);
}

CleanUpThread::CleanUpThread(Renderer *renderer, CommandQueue *commandQueue)
    : ErrorContext(renderer),
      mCommandQueue(commandQueue),
      mTaskThreadShouldExit(false),
      mNeedCleanUp(false)
{}

CleanUpThread::~CleanUpThread() = default;

angle::Result CleanUpThread::checkAndPopPendingError(ErrorContext *errorHandlingContext)
{
    std::lock_guard<angle::SimpleMutex> queueLock(mErrorMutex);
    if (mErrors.empty())
    {
        return angle::Result::Continue;
    }

    while (!mErrors.empty())
    {
        Error err = mErrors.front();
        mErrors.pop();
        errorHandlingContext->handleError(err.errorCode, err.file, err.function, err.line);
    }
    return angle::Result::Stop;
}

void CleanUpThread::requestCleanUp()
{
    if (!mNeedCleanUp.exchange(true))
    {
        // request clean up in async thread
        std::unique_lock<std::mutex> enqueueLock(mMutex);
        mWorkAvailableCondition.notify_one();
    }
}

void CleanUpThread::processTasks()
{
    angle::SetCurrentThreadName("ANGLE-GC");

    while (true)
    {
        bool exitThread = false;
        (void)processTasksImpl(&exitThread);
        if (exitThread)
        {
            // We are doing a controlled exit of the thread, break out of the while loop.
            break;
        }
    }
}

angle::Result CleanUpThread::processTasksImpl(bool *exitThread)
{
    WhenToResetCommandBuffer whenToReset = mRenderer->getFeatures().asyncCommandBufferReset.enabled
                                               ? WhenToResetCommandBuffer::Now
                                               : WhenToResetCommandBuffer::Defer;
    while (true)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mWorkAvailableCondition.wait(lock,
                                     [this] { return mTaskThreadShouldExit || mNeedCleanUp; });

        if (mTaskThreadShouldExit)
        {
            break;
        }
        lock.unlock();

        if (mNeedCleanUp.exchange(false))
        {
            // Always check completed commands again in case anything new has been finished.
            ANGLE_TRY(mCommandQueue->checkCompletedCommands(this));

            // Reset command buffer and clean up garbage
            if (mRenderer->getFeatures().asyncGarbageCleanup.enabled &&
                mCommandQueue->hasFinishedCommands())
            {
                ANGLE_TRY(mCommandQueue->releaseFinishedCommands(this, whenToReset));
            }
            mRenderer->cleanupGarbage(nullptr);
        }
    }
    *exitThread = true;
    return angle::Result::Continue;
}

angle::Result CleanUpThread::init()
{
    mTaskThread = std::thread(&CleanUpThread::processTasks, this);

    return angle::Result::Continue;
}

void CleanUpThread::destroy(ErrorContext *context)
{
    {
        // Request to terminate the worker thread
        std::lock_guard<std::mutex> lock(mMutex);
        mTaskThreadShouldExit = true;
        mNeedCleanUp          = false;
        mWorkAvailableCondition.notify_one();
    }

    // Perform any lingering clean up right away.
    if (mRenderer->getFeatures().asyncGarbageCleanup.enabled)
    {
        (void)mCommandQueue->releaseFinishedCommands(context, WhenToResetCommandBuffer::Now);
        mRenderer->cleanupGarbage(nullptr);
    }

    if (mTaskThread.joinable())
    {
        mTaskThread.join();
    }
}

// CommandsState implementation.
CommandsState::CommandsState(Renderer *renderer,
                             ProtectionType protectionType,
                             egl::ContextPriority priority)
    : mCmdPoolMutex(renderer->getCommandPoolAccess().mCmdPoolMutex),
      mProtectionType(protectionType),
      mPriority(priority)
{}

CommandsState::~CommandsState()
{
    ASSERT(mWaitSemaphores.empty());
    ASSERT(mWaitSemaphoreStageMasks.empty());
    ASSERT(!mPrimaryCommands.valid());
}

void CommandsState::destroy(VkDevice device)
{
    std::lock_guard<angle::SimpleMutex> lock(mCmdPoolMutex);
    mWaitSemaphores.clear();
    mWaitSemaphoreStageMasks.clear();
    mPrimaryCommands.destroy(device);
    mSecondaryCommands.releaseCommandBuffers();
}

angle::Result CommandsState::getCommandsAndWaitSemaphores(
    ErrorContext *context,
    CommandPoolAccess *commandPoolAccess,
    CommandBatch *batch,
    std::vector<VkSemaphore> *waitSemaphoresOut,
    std::vector<VkPipelineStageFlags> *waitSemaphoreStageMasksOut)
{
    std::lock_guard<angle::SimpleMutex> lock(mCmdPoolMutex);

    ASSERT(mPrimaryCommands.valid() || mSecondaryCommands.empty());

    // Store the primary CommandBuffer and the reference to CommandPoolAccess in the in-flight list.
    if (mPrimaryCommands.valid())
    {
        ANGLE_VK_TRY(context, mPrimaryCommands.end());
    }

    batch->setPrimaryCommands(std::move(mPrimaryCommands), commandPoolAccess);
    // Store secondary Command Buffers.
    batch->setSecondaryCommands(std::move(mSecondaryCommands));

    // Store wait semaphores.
    *waitSemaphoresOut          = std::move(mWaitSemaphores);
    *waitSemaphoreStageMasksOut = std::move(mWaitSemaphoreStageMasks);

    return angle::Result::Continue;
}

angle::Result CommandsState::ensurePrimaryCommandBufferValidLocked(ErrorContext *context)
{
    Renderer *renderer = context->getRenderer();

    if (mPrimaryCommands.valid())
    {
        return angle::Result::Continue;
    }
    else
    {
        ANGLE_TRY(renderer->getCommandPoolAccess().allocatePrimaryCommandBufferLocked(
            context, mProtectionType, &mPrimaryCommands));

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo         = nullptr;
        ANGLE_VK_TRY(context, mPrimaryCommands.begin(beginInfo));
    }

    return angle::Result::Continue;
}

angle::Result CommandsState::insertSubmitDebugMarker(ErrorContext *context,
                                                     QueueSubmitReason reason)
{
    Renderer *renderer = context->getRenderer();
    if (!renderer->enableDebugUtils() && !renderer->angleDebuggerMode())
    {
        return angle::Result::Continue;
    }

    std::lock_guard<angle::SimpleMutex> lock(mCmdPoolMutex);
    ANGLE_TRY(ensurePrimaryCommandBufferValidLocked(context));

    renderer->insertSubmitDebugMarkerInCommandBuffer(mPrimaryCommands, reason);
    return angle::Result::Continue;
}

// CommandPoolAccess public API implementation. These must be thread safe and never called from
// CommandPoolAccess class itself.
CommandPoolAccess::CommandPoolAccess()  = default;
CommandPoolAccess::~CommandPoolAccess() = default;

angle::Result CommandPoolAccess::initCommandPool(ErrorContext *context,
                                                 ProtectionType protectionType,
                                                 const uint32_t queueFamilyIndex)
{
    std::lock_guard<angle::SimpleMutex> lock(mCmdPoolMutex);
    PersistentCommandPool &commandPool = mPrimaryCommandPoolMap[protectionType];
    return commandPool.init(context, protectionType, queueFamilyIndex);
}

void CommandPoolAccess::destroy(VkDevice device)
{
    std::lock_guard<angle::SimpleMutex> lock(mCmdPoolMutex);
    for (PersistentCommandPool &commandPool : mPrimaryCommandPoolMap)
    {
        commandPool.destroy(device);
    }
}

void CommandPoolAccess::destroyPrimaryCommandBuffer(VkDevice device,
                                                    PrimaryCommandBuffer *primaryCommands) const
{
    ASSERT(primaryCommands->valid());

    // Does not require a pool mutex lock.
    primaryCommands->destroy(device);
}

angle::Result CommandPoolAccess::collectPrimaryCommandBuffer(ErrorContext *context,
                                                             const ProtectionType protectionType,
                                                             PrimaryCommandBuffer *primaryCommands,
                                                             WhenToResetCommandBuffer whenToReset)
{
    ASSERT(primaryCommands->valid());
    std::lock_guard<angle::SimpleMutex> lock(mCmdPoolMutex);

    PersistentCommandPool &commandPool = mPrimaryCommandPoolMap[protectionType];
    ANGLE_TRY(commandPool.collect(context, std::move(*primaryCommands), whenToReset));

    return angle::Result::Continue;
}

// CommandQueue public API implementation. These must be thread safe and never called from
// CommandQueue class itself.
CommandQueue::CommandQueue()
    : mInFlightCommands(kInFlightCommandsLimit),
      mFinishedCommandBatches(kMaxFinishedCommandsLimit),
      mNumAllCommands(0),
      mPerfCounters{}
{}

CommandQueue::~CommandQueue() = default;

void CommandQueue::destroy(ErrorContext *context)
{
    std::lock_guard<angle::SimpleMutex> queueSubmitLock(mQueueSubmitMutex);
    std::lock_guard<angle::SimpleMutex> cmdCompleteLock(mCmdCompleteMutex);
    std::lock_guard<angle::SimpleMutex> cmdReleaseLock(mCmdReleaseMutex);

    mQueueMap.destroy();

    // Assigns an infinite "last completed" serial to force garbage to delete.
    mLastCompletedSerials.fill(Serial::Infinite());

    mCommandPoolAccess.destroy(context->getDevice());

    mFenceRecycler.destroy(context);

    ASSERT(mInFlightCommands.empty());
    ASSERT(mFinishedCommandBatches.empty());
    ASSERT(mNumAllCommands == 0);
}

angle::Result CommandQueue::init(ErrorContext *context,
                                 const QueueFamily &queueFamily,
                                 bool enableProtectedContent,
                                 uint32_t queueCount)
{
    std::lock_guard<angle::SimpleMutex> queueSubmitLock(mQueueSubmitMutex);
    std::lock_guard<angle::SimpleMutex> cmdCompleteLock(mCmdCompleteMutex);
    std::lock_guard<angle::SimpleMutex> cmdReleaseLock(mCmdReleaseMutex);

    // In case Renderer gets re-initialized, we can't rely on constructor to do initialization.
    mLastSubmittedSerials.fill(kZeroSerial);
    mLastCompletedSerials.fill(kZeroSerial);

    // Assign before initializing the command pools in order to get the queue family index.
    mQueueMap.initialize(context->getDevice(), queueFamily, enableProtectedContent, 0, queueCount);
    ANGLE_TRY(mCommandPoolAccess.initCommandPool(context, ProtectionType::Unprotected,
                                                 mQueueMap.getQueueFamilyIndex()));

    if (mQueueMap.isProtected())
    {
        ANGLE_TRY(mCommandPoolAccess.initCommandPool(context, ProtectionType::Protected,
                                                     mQueueMap.getQueueFamilyIndex()));
    }
    return angle::Result::Continue;
}

void CommandQueue::handleDeviceLost(Renderer *renderer)
{
    ANGLE_TRACE_EVENT0("gpu.angle", "CommandQueue::handleDeviceLost");
    VkDevice device = renderer->getDevice();
    // Hold all locks while clean up mInFlightCommands.
    std::lock_guard<angle::SimpleMutex> queueSubmitLock(mQueueSubmitMutex);
    std::lock_guard<angle::SimpleMutex> cmdCompleteLock(mCmdCompleteMutex);
    std::lock_guard<angle::SimpleMutex> cmdReleaseLock(mCmdReleaseMutex);

    // Work around a driver bug where resource clean up would cause a crash without vkQueueWaitIdle.
    mQueueMap.waitAllQueuesIdle();

    while (!mInFlightCommands.empty())
    {
        CommandBatch &batch = mInFlightCommands.front();
        // On device loss we need to wait for fence to be signaled before destroying it
        if (batch.hasFence())
        {
            VkResult status = batch.waitFence(device, renderer->getMaxFenceWaitTimeNs());
            // If the wait times out, it is probably not possible to recover from lost device
            ASSERT(status == VK_SUCCESS || status == VK_ERROR_DEVICE_LOST);
        }
        mLastCompletedSerials.setQueueSerial(batch.getQueueSerial());
        batch.destroy(device);
        popInFlightBatchLocked();
    }
}

angle::Result CommandQueue::postSubmitCheck(ErrorContext *context)
{
    Renderer *renderer = context->getRenderer();

    // Update mLastCompletedQueueSerial immediately in case any command has been finished.
    ANGLE_TRY(checkAndCleanupCompletedCommands(context));

    VkDeviceSize suballocationGarbageSize = renderer->getSuballocationGarbageSize();
    while (suballocationGarbageSize > kMaxBufferSuballocationGarbageSize)
    {
        // CPU should be throttled to avoid accumulating too much memory garbage waiting to be
        // destroyed. This is important to keep peak memory usage at check when game launched and a
        // lot of staging buffers used for textures upload and then gets released. But if there is
        // only one command buffer in flight, we do not wait here to ensure we keep GPU busy.
        constexpr size_t kMinInFlightBatchesToKeep = 1;
        bool anyGarbageCleaned                     = false;
        ANGLE_TRY(cleanupSomeGarbage(context, kMinInFlightBatchesToKeep, &anyGarbageCleaned));
        if (!anyGarbageCleaned)
        {
            break;
        }
        suballocationGarbageSize = renderer->getSuballocationGarbageSize();
    }

    if (kOutputVmaStatsString)
    {
        renderer->outputVmaStatString();
    }

    return angle::Result::Continue;
}

angle::Result CommandQueue::finishResourceUse(ErrorContext *context,
                                              const ResourceUse &use,
                                              uint64_t timeout)
{
    VkDevice device = context->getDevice();
    {
        std::unique_lock<angle::SimpleMutex> lock(mCmdCompleteMutex);
        while (!mInFlightCommands.empty() && !hasResourceUseFinished(use))
        {
            bool finished;
            ANGLE_TRY(checkOneCommandBatchLocked(context, &finished));
            if (!finished)
            {
                ANGLE_VK_TRY(context,
                             mInFlightCommands.front().waitFenceUnlocked(device, timeout, &lock));
            }
        }
        // Check the rest of the commands in case they are also finished.
        ANGLE_TRY(checkCompletedCommandsLocked(context));
    }
    ASSERT(hasResourceUseFinished(use));

    if (!mFinishedCommandBatches.empty())
    {
        ANGLE_TRY(releaseFinishedCommandsAndCleanupGarbage(context));
    }

    return angle::Result::Continue;
}

angle::Result CommandQueue::finishQueueSerial(ErrorContext *context,
                                              const QueueSerial &queueSerial,
                                              uint64_t timeout)
{
    ResourceUse use(queueSerial);
    return finishResourceUse(context, use, timeout);
}

angle::Result CommandQueue::waitIdle(ErrorContext *context, uint64_t timeout)
{
    // Fill the local variable with lock
    ResourceUse use;
    {
        std::lock_guard<angle::SimpleMutex> lock(mQueueSubmitMutex);
        if (mInFlightCommands.empty())
        {
            return angle::Result::Continue;
        }
        use.setQueueSerial(mInFlightCommands.back().getQueueSerial());
    }

    return finishResourceUse(context, use, timeout);
}

angle::Result CommandQueue::waitForResourceUseToFinishWithUserTimeout(ErrorContext *context,
                                                                      const ResourceUse &use,
                                                                      uint64_t timeout,
                                                                      VkResult *result)
{
    // Serial is not yet submitted. This is undefined behaviour, so we can do anything.
    if (!hasResourceUseSubmitted(use))
    {
        WARN() << "Waiting on an unsubmitted serial.";
        *result = VK_TIMEOUT;
        return angle::Result::Continue;
    }

    VkDevice device      = context->getDevice();
    size_t finishedCount = 0;
    {
        std::unique_lock<angle::SimpleMutex> lock(mCmdCompleteMutex);
        *result = hasResourceUseFinished(use) ? VK_SUCCESS : VK_NOT_READY;
        while (!mInFlightCommands.empty() && !hasResourceUseFinished(use))
        {
            bool finished;
            ANGLE_TRY(checkOneCommandBatchLocked(context, &finished));
            if (!finished)
            {
                *result = mInFlightCommands.front().waitFenceUnlocked(device, timeout, &lock);
                // Don't trigger an error on timeout.
                if (*result == VK_TIMEOUT)
                {
                    break;
                }
                else
                {
                    ANGLE_VK_TRY(context, *result);
                }
            }
            else
            {
                *result = hasResourceUseFinished(use) ? VK_SUCCESS : VK_NOT_READY;
            }
        }
        // Do one more check in case more commands also finished.
        ANGLE_TRY(checkCompletedCommandsLocked(context));
        finishedCount = mFinishedCommandBatches.size();
    }

    if (finishedCount > 0)
    {
        ANGLE_TRY(releaseFinishedCommandsAndCleanupGarbage(context));
    }

    return angle::Result::Continue;
}

bool CommandQueue::isBusy(Renderer *renderer) const
{
    // No lock is needed here since we are accessing atomic variables only.
    size_t maxIndex = renderer->getLargestQueueSerialIndexEverAllocated();
    for (SerialIndex i = 0; i <= maxIndex; ++i)
    {
        if (mLastSubmittedSerials[i] > mLastCompletedSerials[i])
        {
            return true;
        }
    }
    return false;
}

angle::Result CommandQueue::submitCommands(ErrorContext *context,
                                           VkSemaphore signalSemaphore,
                                           SharedExternalFence &&externalFence,
                                           const QueueSerial &submitQueueSerial,
                                           CommandsState &&commandsState)
{
    ANGLE_TRACE_EVENT0("gpu.angle", "CommandQueue::submitCommands");
    std::lock_guard<angle::SimpleMutex> lock(mQueueSubmitMutex);
    Renderer *renderer = context->getRenderer();
    VkDevice device    = renderer->getDevice();

    mPerfCounters.queueSubmitCallsTotal.fetch_add(1, std::memory_order_relaxed);

    DeviceScoped<CommandBatch> scopedBatch(device);
    CommandBatch &batch = scopedBatch.get();

    batch.setQueueSerial(submitQueueSerial);
    batch.setProtectionType(commandsState.getProtectionType());

    std::vector<VkSemaphore> waitSemaphores;
    std::vector<VkPipelineStageFlags> waitSemaphoreStageMasks;

    ANGLE_TRY(commandsState.getCommandsAndWaitSemaphores(
        context, &mCommandPoolAccess, &batch, &waitSemaphores, &waitSemaphoreStageMasks));
    mPerfCounters.queueWaitSemaphoresTotal.fetch_add(waitSemaphores.size(),
                                                     std::memory_order_relaxed);

    // Don't make a submission if there is nothing to submit.
    const bool needsQueueSubmit = batch.getPrimaryCommands().valid() ||
                                  signalSemaphore != VK_NULL_HANDLE || externalFence ||
                                  !waitSemaphores.empty();
    VkSubmitInfo submitInfo                   = {};
    VkProtectedSubmitInfo protectedSubmitInfo = {};

    if (needsQueueSubmit)
    {
        InitializeSubmitInfo(&submitInfo, batch.getPrimaryCommands(), waitSemaphores,
                             waitSemaphoreStageMasks, signalSemaphore);

        // No need protected submission if no commands to submit.
        if (commandsState.getProtectionType() == ProtectionType::Protected &&
            batch.getPrimaryCommands().valid())
        {
            protectedSubmitInfo.sType           = VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO;
            protectedSubmitInfo.pNext           = nullptr;
            protectedSubmitInfo.protectedSubmit = true;
            submitInfo.pNext                    = &protectedSubmitInfo;
        }

        // Initializing a fence is not required if the batch already has an external fence and does
        // not need an extra fence after its submission.
        const bool needsOwnedFence =
            renderer->getFeatures().enableExtraSubmitFence.enabled || !externalFence;
        if (needsOwnedFence)
        {
            ANGLE_VK_TRY(context, batch.initFence(device, &mFenceRecycler));
        }
        if (externalFence)
        {
            batch.setExternalFence(std::move(externalFence));
        }
    }

    return queueSubmitLocked(context, commandsState.getPriority(), submitInfo, scopedBatch,
                             submitQueueSerial);
}

angle::Result CommandQueue::queueSubmitOneOff(ErrorContext *context,
                                              ProtectionType protectionType,
                                              egl::ContextPriority contextPriority,
                                              VkCommandBuffer commandBufferHandle,
                                              VkSemaphore waitSemaphore,
                                              VkPipelineStageFlags waitSemaphoreStageMask,
                                              const QueueSerial &submitQueueSerial)
{
    std::unique_lock<angle::SimpleMutex> lock(mQueueSubmitMutex);
    DeviceScoped<CommandBatch> scopedBatch(context->getDevice());
    CommandBatch &batch = scopedBatch.get();
    batch.setQueueSerial(submitQueueSerial);
    batch.setProtectionType(protectionType);

    ANGLE_VK_TRY(context, batch.initFence(context->getDevice(), &mFenceRecycler));

    VkSubmitInfo submitInfo = {};
    submitInfo.sType        = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkProtectedSubmitInfo protectedSubmitInfo = {};
    ASSERT(protectionType == ProtectionType::Unprotected ||
           protectionType == ProtectionType::Protected);
    if (protectionType == ProtectionType::Protected)
    {
        protectedSubmitInfo.sType           = VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO;
        protectedSubmitInfo.pNext           = nullptr;
        protectedSubmitInfo.protectedSubmit = true;
        submitInfo.pNext                    = &protectedSubmitInfo;
    }

    if (commandBufferHandle != VK_NULL_HANDLE)
    {
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &commandBufferHandle;
    }

    if (waitSemaphore != VK_NULL_HANDLE)
    {
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores    = &waitSemaphore;
        submitInfo.pWaitDstStageMask  = &waitSemaphoreStageMask;
    }

    return queueSubmitLocked(context, contextPriority, submitInfo, scopedBatch, submitQueueSerial);
}

angle::Result CommandQueue::queueSubmitLocked(ErrorContext *context,
                                              egl::ContextPriority contextPriority,
                                              const VkSubmitInfo &submitInfo,
                                              DeviceScoped<CommandBatch> &commandBatch,
                                              const QueueSerial &submitQueueSerial)
{
    ANGLE_TRACE_EVENT0("gpu.angle", "CommandQueue::queueSubmitLocked");
    Renderer *renderer = context->getRenderer();

    // CPU should be throttled to avoid mInFlightCommands from growing too fast. Important for
    // off-screen scenarios.
    if (mInFlightCommands.full())
    {
        std::unique_lock<angle::SimpleMutex> lock(mCmdCompleteMutex);
        // Check once more inside the lock in case other thread already finished some/all commands.
        if (mInFlightCommands.full())
        {
            ANGLE_TRY(finishOneCommandBatch(context, renderer->getMaxFenceWaitTimeNs(), &lock));
        }
    }
    // Assert will succeed since new batch is pushed only in this method below.
    ASSERT(!mInFlightCommands.full());

    // Also ensure that all mInFlightCommands may be moved into the mFinishedCommandBatches without
    // need of the releaseFinishedCommandsLocked() call.
    ASSERT(mNumAllCommands <= mFinishedCommandBatches.capacity());
    if (mNumAllCommands == mFinishedCommandBatches.capacity())
    {
        std::lock_guard<angle::SimpleMutex> lock(mCmdReleaseMutex);
        ANGLE_TRY(releaseFinishedCommandsLocked(context, WhenToResetCommandBuffer::Now));
    }
    // Assert will succeed since mNumAllCommands is incremented only in this method below.
    ASSERT(mNumAllCommands < mFinishedCommandBatches.capacity());

    if (submitInfo.sType == VK_STRUCTURE_TYPE_SUBMIT_INFO)
    {
        CommandBatch &batch = commandBatch.get();

        VkQueue queue = getQueue(contextPriority);
        if (batch.getExternalFence())
        {
            VkFence externalFenceHandle = batch.getExternalFence()->getHandle();
            ASSERT(externalFenceHandle != VK_NULL_HANDLE);
            ANGLE_VK_TRY(context,
                         VK_CALL(vkQueueSubmit, queue, 1, &submitInfo, externalFenceHandle));
            mPerfCounters.vkQueueSubmitCallsTotal.fetch_add(1, std::memory_order_relaxed);

            // If enabled, there will be an extra fence submitted after the primary commands.
            if (renderer->getFeatures().enableExtraSubmitFence.enabled)
            {
                VkFence extraSubmitFence     = batch.getFenceHandle();
                VkSubmitInfo fenceSubmitInfo = {};
                fenceSubmitInfo.sType        = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                ANGLE_VK_TRY(context,
                             VK_CALL(vkQueueSubmit, queue, 1, &fenceSubmitInfo, extraSubmitFence));
                mPerfCounters.vkQueueSubmitCallsTotal.fetch_add(1, std::memory_order_relaxed);
            }

            // exportFd is exporting VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT_KHR type handle which
            // obeys copy semantics. This means that the fence must already be signaled or the work
            // to signal it is in the graphics pipeline at the time we export the fd.
            // In other words, must call exportFd() after successful vkQueueSubmit() call.
            ExternalFence &externalFence       = *batch.getExternalFence();
            VkFenceGetFdInfoKHR fenceGetFdInfo = {};
            fenceGetFdInfo.sType               = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR;
            fenceGetFdInfo.fence               = externalFenceHandle;
            fenceGetFdInfo.handleType          = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT_KHR;
            externalFence.exportFd(renderer->getDevice(), fenceGetFdInfo);
        }
        else
        {
            VkFence fence = batch.getFenceHandle();
            ASSERT(fence != VK_NULL_HANDLE);
            ANGLE_VK_TRY(context, VK_CALL(vkQueueSubmit, queue, 1, &submitInfo, fence));
            mPerfCounters.vkQueueSubmitCallsTotal.fetch_add(1, std::memory_order_relaxed);
        }
    }

    pushInFlightBatchLocked(commandBatch.release());

    // This must set last so that when this submission appears submitted, it actually already
    // submitted and enqueued to mInFlightCommands.
    mLastSubmittedSerials.setQueueSerial(submitQueueSerial);
    return angle::Result::Continue;
}

VkResult CommandQueue::queuePresent(egl::ContextPriority contextPriority,
                                    const VkPresentInfoKHR &presentInfo)
{
    std::lock_guard<angle::SimpleMutex> lock(mQueueSubmitMutex);
    VkQueue queue = getQueue(contextPriority);
    return VK_CALL(vkQueuePresentKHR, queue, &presentInfo);
}

const CommandQueuePerfCounters CommandQueue::getPerfCounters() const
{
    return mPerfCounters;
}

angle::Result CommandQueue::releaseFinishedCommandsAndCleanupGarbage(ErrorContext *context)
{
    Renderer *renderer = context->getRenderer();
    if (renderer->getFeatures().asyncGarbageCleanup.enabled)
    {
        renderer->requestAsyncCommandsAndGarbageCleanup(context);
    }
    else
    {
        // Do immediate command buffer reset and garbage cleanup
        ANGLE_TRY(releaseFinishedCommands(context, WhenToResetCommandBuffer::Now));
        renderer->cleanupGarbage(nullptr);
    }

    return angle::Result::Continue;
}

angle::Result CommandQueue::cleanupSomeGarbage(ErrorContext *context,
                                               size_t minInFlightBatchesToKeep,
                                               bool *anyGarbageCleanedOut)
{
    Renderer *renderer = context->getRenderer();

    bool anyGarbageCleaned = false;

    renderer->cleanupGarbage(&anyGarbageCleaned);

    while (!anyGarbageCleaned)
    {
        {
            std::unique_lock<angle::SimpleMutex> lock(mCmdCompleteMutex);
            if (mInFlightCommands.size() <= minInFlightBatchesToKeep)
            {
                break;
            }
            ANGLE_TRY(finishOneCommandBatch(context, renderer->getMaxFenceWaitTimeNs(), &lock));
        }
        renderer->cleanupGarbage(&anyGarbageCleaned);
    }

    if (anyGarbageCleanedOut != nullptr)
    {
        *anyGarbageCleanedOut = anyGarbageCleaned;
    }

    return angle::Result::Continue;
}

// CommandQueue private API implementation. These are called by public API, so lock already held.
angle::Result CommandQueue::checkOneCommandBatchLocked(ErrorContext *context, bool *finished)
{
    ASSERT(!mInFlightCommands.empty());

    CommandBatch &batch = mInFlightCommands.front();
    *finished           = false;
    if (batch.hasFence())
    {
        VkResult status = batch.getFenceStatus(context->getDevice());
        if (status == VK_NOT_READY)
        {
            return angle::Result::Continue;
        }
        ANGLE_VK_TRY(context, status);
    }

    onCommandBatchFinishedLocked(std::move(batch));
    *finished = true;

    return angle::Result::Continue;
}

angle::Result CommandQueue::finishOneCommandBatch(ErrorContext *context,
                                                  uint64_t timeout,
                                                  std::unique_lock<angle::SimpleMutex> *lock)
{
    ASSERT(!mInFlightCommands.empty());
    ASSERT(lock->owns_lock());

    CommandBatch &batch = mInFlightCommands.front();
    // Save queue serial since the batch may be destroyed during possible unlocked fence wait.
    const QueueSerial batchSerial = batch.getQueueSerial();
    if (batch.hasFence())
    {
        VkResult status = batch.waitFenceUnlocked(context->getDevice(), timeout, lock);
        ANGLE_VK_TRY(context, status);
    }

    // Other thread might already finish the batch, in that case do not touch the |batch| reference.
    if (!hasQueueSerialFinished(batchSerial))
    {
        onCommandBatchFinishedLocked(std::move(batch));
    }

    return angle::Result::Continue;
}

void CommandQueue::onCommandBatchFinishedLocked(CommandBatch &&batch)
{
    // Finished.
    mLastCompletedSerials.setQueueSerial(batch.getQueueSerial());

    // Move command batch to mFinishedCommandBatches.
    moveInFlightBatchToFinishedQueueLocked(std::move(batch));
}

angle::Result CommandQueue::releaseFinishedCommandsLocked(ErrorContext *context,
                                                          WhenToResetCommandBuffer whenToReset)
{
    ANGLE_TRACE_EVENT0("gpu.angle", "releaseFinishedCommandsLocked");

    while (!mFinishedCommandBatches.empty())
    {
        CommandBatch &batch = mFinishedCommandBatches.front();
        ASSERT(batch.getQueueSerial() <= mLastCompletedSerials);
        ANGLE_TRY(batch.release(context, whenToReset));
        popFinishedBatchLocked();
    }

    return angle::Result::Continue;
}

angle::Result CommandQueue::checkCompletedCommandsLocked(ErrorContext *context)
{
    while (!mInFlightCommands.empty())
    {
        bool finished;
        ANGLE_TRY(checkOneCommandBatchLocked(context, &finished));
        if (!finished)
        {
            break;
        }
    }
    return angle::Result::Continue;
}

void CommandQueue::pushInFlightBatchLocked(CommandBatch &&batch)
{
    // Need to increment before the push to prevent possible decrement from 0.
    ++mNumAllCommands;
    mInFlightCommands.push(std::move(batch));
}

void CommandQueue::moveInFlightBatchToFinishedQueueLocked(CommandBatch &&batch)
{
    // This must not happen, since we always leave space in the queue during queueSubmitLocked.
    ASSERT(!mFinishedCommandBatches.full());
    ASSERT(&batch == &mInFlightCommands.front());

    mFinishedCommandBatches.push(std::move(batch));
    mInFlightCommands.pop();
    // No mNumAllCommands update since batch was simply moved to the other queue.
}

void CommandQueue::popFinishedBatchLocked()
{
    mFinishedCommandBatches.pop();
    // Need to decrement after the pop to prevent possible push over the limit.
    ASSERT(mNumAllCommands > 0);
    --mNumAllCommands;
}

void CommandQueue::popInFlightBatchLocked()
{
    mInFlightCommands.pop();
    // Need to decrement after the pop to prevent possible push over the limit.
    ASSERT(mNumAllCommands > 0);
    --mNumAllCommands;
}

DeviceQueueMap::~DeviceQueueMap() {}

void DeviceQueueMap::destroy()
{
    waitAllQueuesIdle();
}

void DeviceQueueMap::waitAllQueuesIdle()
{
    // Force all commands to finish by flushing all queues.
    for (const QueueAndIndex &queueAndIndex : mQueueAndIndices)
    {
        if (queueAndIndex.queue != VK_NULL_HANDLE)
        {
            VK_CALL(vkQueueWaitIdle, queueAndIndex.queue);
        }
    }
}

void DeviceQueueMap::initialize(VkDevice device,
                                const QueueFamily &queueFamily,
                                bool makeProtected,
                                uint32_t queueIndex,
                                uint32_t queueCount)
{
    // QueueIndexing:
    constexpr uint32_t kQueueIndexMedium   = 0;
    constexpr uint32_t kQueueIndexHigh     = 1;
    constexpr uint32_t kQueueIndexRealtime = 2;
    constexpr uint32_t kQueueIndexLow      = 3;

    ASSERT(queueCount);
    ASSERT((queueIndex + queueCount) <= queueFamily.getProperties()->queueCount);
    mQueueFamilyIndex = queueFamily.getQueueFamilyIndex();
    mIsProtected      = makeProtected;

    VkQueue queue = VK_NULL_HANDLE;
    GetDeviceQueue(device, makeProtected, mQueueFamilyIndex, queueIndex + kQueueIndexMedium,
                   &queue);
    mQueueAndIndices[egl::ContextPriority::Medium] = {egl::ContextPriority::Medium, queue,
                                                      queueIndex + kQueueIndexMedium};

    // If at least 2 queues, High has its own queue
    if (queueCount > 1)
    {
        GetDeviceQueue(device, makeProtected, mQueueFamilyIndex, queueIndex + kQueueIndexHigh,
                       &queue);
        mQueueAndIndices[egl::ContextPriority::High] = {egl::ContextPriority::High, queue,
                                                        queueIndex + kQueueIndexHigh};
    }
    else
    {
        mQueueAndIndices[egl::ContextPriority::High] =
            mQueueAndIndices[egl::ContextPriority::Medium];
    }

    // If at least 3 queues, Realtime has its own queue
    if (queueCount > 2)
    {
        GetDeviceQueue(device, makeProtected, mQueueFamilyIndex, queueIndex + kQueueIndexRealtime,
                       &queue);
        mQueueAndIndices[egl::ContextPriority::Realtime] = {egl::ContextPriority::Realtime, queue,
                                                            queueIndex + kQueueIndexRealtime};
    }
    else if (queueCount > 1)
    {
        // If only 2 queues, Realtime uses queue of High priority
        mQueueAndIndices[egl::ContextPriority::Realtime] =
            mQueueAndIndices[egl::ContextPriority::High];
    }
    else
    {
        mQueueAndIndices[egl::ContextPriority::Realtime] =
            mQueueAndIndices[egl::ContextPriority::Medium];
    }

    // If at least 4 queues, Low has its own queue. Adjust Low priority.
    if (queueCount > 3)
    {
        GetDeviceQueue(device, makeProtected, mQueueFamilyIndex, queueIndex + kQueueIndexLow,
                       &queue);
        mQueueAndIndices[egl::ContextPriority::Low] = {egl::ContextPriority::Low, queue,
                                                       queueIndex + kQueueIndexLow};
    }
    else
    {
        mQueueAndIndices[egl::ContextPriority::Low] =
            mQueueAndIndices[egl::ContextPriority::Medium];
    }
}

void QueueFamily::initialize(const VkQueueFamilyProperties &queueFamilyProperties,
                             uint32_t queueFamilyIndex)
{
    mProperties       = queueFamilyProperties;
    mQueueFamilyIndex = queueFamilyIndex;
}

uint32_t QueueFamily::FindIndex(const std::vector<VkQueueFamilyProperties2> &queueFamilyProperties2,
                                VkQueueFlags includeFlags,
                                VkQueueFlags optionalFlags,
                                VkQueueFlags excludeFlags,
                                uint32_t *matchCount)
{
    // check with both include and optional flags
    VkQueueFlags preferredFlags = includeFlags | optionalFlags;
    auto findIndexPredicate     = [&preferredFlags,
                                   &excludeFlags](const VkQueueFamilyProperties2 &queueInfo) {
        return (queueInfo.queueFamilyProperties.queueFlags & excludeFlags) == 0 &&
               (queueInfo.queueFamilyProperties.queueFlags & preferredFlags) == preferredFlags;
    };

    auto it = std::find_if(queueFamilyProperties2.begin(), queueFamilyProperties2.end(),
                           findIndexPredicate);
    if (it == queueFamilyProperties2.end())
    {
        // didn't find a match, exclude the optional flags from the list
        preferredFlags = includeFlags;
        it             = std::find_if(queueFamilyProperties2.begin(), queueFamilyProperties2.end(),
                                      findIndexPredicate);
    }
    if (it == queueFamilyProperties2.end())
    {
        *matchCount = 0;
        return QueueFamily::kInvalidIndex;
    }

    *matchCount = 1;
    return static_cast<uint32_t>(std::distance(queueFamilyProperties2.begin(), it));
}

}  // namespace vk
}  // namespace rx
