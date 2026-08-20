/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file is a body-include: it is #included inside the FFmpegVideoDecoder
// class definition (inside MOZ_USE_HWDECODE + MOZ_WIDGET_GTK +
// LIBAVCODEC_VERSION_MAJOR >= 60 guards). It defines the
// FFmpegVulkanVideoDecoder nested struct. All required types are provided by
// the enclosing translation unit. Do NOT include this file directly.

#ifndef DOM_MEDIA_PLATFORMS_FFMPEG_FFMPEGVULKANVIDEODECODER_H_
#define DOM_MEDIA_PLATFORMS_FFMPEG_FFMPEGVULKANVIDEODECODER_H_

struct FFmpegVulkanVideoDecoder {
  static constexpr int kNumBuffers = 32;

  // Device & copy queues (decode queue is owned by FFmpeg)
  VkDevice mDevice = VK_NULL_HANDLE;
  uint32_t mQueueFamilyIndex = 0;
  uint32_t mCopyQueueCount = 0;
  bool mCopyQueueIsDedicatedTransfer = false;
  std::atomic<uint32_t> mCopyQueueRoundRobin{0};
  nsTArray<VkQueue> mCopyQueue;
  nsTArray<VkCommandPool> mCopyCmdPool;
  nsTArray<VkCommandBuffer> mCopyCmdBuf;
  nsTArray<VkFence> mCopyFence;
  uint64_t mDrmModifier = 0;

  // Multi-buffered NV12 images
  VkImage mNv12Image[kNumBuffers] = {};
  VkDeviceMemory mNv12Mem[kNumBuffers] = {};
  int mNv12BaseFd[kNumBuffers] = {-1};
  int mCurrentBuffer = 0;
  VkSemaphore mCopyDoneSem[kNumBuffers] = {};
  VkExternalSemaphoreHandleTypeFlags mSemHandleType =
      VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
  int mCopyDoneSemFd[kNumBuffers] = {-1};
  uint64_t mCopyDoneSemValue[kNumBuffers] = {0};
  bool mCopyDoneSemSignaled[kNumBuffers] = {false};  // binary: wait on reuse

  // Image layout
  uint32_t mWidth = 0;
  uint32_t mHeight = 0;
  size_t mTotalSize = 0;
  size_t mUvOffset = 0;
  uint32_t mYPitch = 0;
  uint32_t mUvPitch = 0;

  // Function pointers - device query (instance-level)
  PFN_vkGetDeviceProcAddr mGetDeviceProcAddr = nullptr;
  PFN_vkGetPhysicalDeviceProperties mGetPhysicalDeviceProperties = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties
      mGetPhysicalDeviceQueueFamilyProperties = nullptr;
  PFN_vkGetPhysicalDeviceMemoryProperties mGetPhysicalDeviceMemoryProperties =
      nullptr;
  PFN_vkGetPhysicalDeviceFormatProperties2 mGetPhysicalDeviceFormatProperties2 =
      nullptr;
  PFN_vkGetPhysicalDeviceImageFormatProperties2
      mGetPhysicalDeviceImageFormatProperties2 = nullptr;
  PFN_vkGetPhysicalDeviceExternalSemaphoreProperties
      mGetPhysicalDeviceExternalSemaphoreProperties = nullptr;

  // Function pointers - command buffer
  PFN_vkCreateCommandPool mCreateCommandPool = nullptr;
  PFN_vkDestroyCommandPool mDestroyCommandPool = nullptr;
  PFN_vkAllocateCommandBuffers mAllocateCommandBuffers = nullptr;
  PFN_vkFreeCommandBuffers mFreeCommandBuffers = nullptr;
  PFN_vkBeginCommandBuffer mBeginCommandBuffer = nullptr;
  PFN_vkEndCommandBuffer mEndCommandBuffer = nullptr;
  // Must use GetDeviceQueue2 with the same flags used at vkCreateDevice
  // (AVVulkanDeviceContext.queue_flags). GetDeviceQueue is illegal whenever
  // those flags are non-zero (VUID-vkGetDeviceQueue-flags-01841) — bug 2058347.
  PFN_vkGetDeviceQueue2 mGetDeviceQueue2 = nullptr;
  PFN_vkQueueSubmit mQueueSubmit = nullptr;
  PFN_vkCmdPipelineBarrier mCmdPipelineBarrier = nullptr;
  PFN_vkCmdCopyImage mCmdCopyImage = nullptr;

  // Function pointers - image & memory
  PFN_vkCreateImage mCreateImage = nullptr;
  PFN_vkDestroyImage mDestroyImage = nullptr;
  PFN_vkGetImageMemoryRequirements mGetImageMemoryRequirements = nullptr;
  PFN_vkGetImageMemoryRequirements2 mGetImageMemoryRequirements2 = nullptr;
  PFN_vkGetImageSubresourceLayout mGetImageSubresourceLayout = nullptr;
  PFN_vkBindImageMemory mBindImageMemory = nullptr;
  PFN_vkAllocateMemory mAllocateMemory = nullptr;
  PFN_vkFreeMemory mFreeMemory = nullptr;

  // Function pointers - sync & extensions
  PFN_vkCreateFence mCreateFence = nullptr;
  PFN_vkDestroyFence mDestroyFence = nullptr;
  PFN_vkResetFences mResetFences = nullptr;
  PFN_vkWaitForFences mWaitForFences = nullptr;
  PFN_vkGetSemaphoreCounterValue mGetSemaphoreCounterValue = nullptr;
  PFN_vkGetMemoryFdKHR mGetMemoryFdKHR = nullptr;
  PFN_vkGetImageDrmFormatModifierPropertiesEXT
      mGetImageDrmFormatModifierPropertiesEXT = nullptr;
  PFN_vkCreateSemaphore mCreateSemaphore = nullptr;
  PFN_vkDestroySemaphore mDestroySemaphore = nullptr;
  PFN_vkWaitSemaphores mWaitSemaphores = nullptr;
  PFN_vkGetSemaphoreFdKHR mGetSemaphoreFdKHR = nullptr;

  std::vector<uint64_t> mDrmModifiers;
  nsTHashMap<uint64_t, bool> mExportRequiresDedicatedByModifier;

  // aGeneration guards against a prior, already-destroyed VkInstance whose
  // address a new one could in principle reuse (see
  // VulkanDeviceHolder::Generation()).
  void LoadInstanceFunctions(PFN_vkGetInstanceProcAddr aGetProcAddr,
                             VkInstance aInst, VkPhysicalDevice aPhysDev,
                             uint64_t aGeneration);
  void LoadDeviceFunctions(VkDevice aDev);
  bool IsLoaded() const;
  // Populated by LoadInstanceFunctions / LoadDeviceFunctions respectively;
  // checked by IsLoaded().
  nsTArray<PFN_vkVoidFunction> mInstanceFunctions;
  nsTArray<PFN_vkVoidFunction> mDeviceFunctions;
  void InitDrmModifiers(VkPhysicalDevice aPhysDev, VkFormat aFormatForModifiers,
                        const nsTArray<uint64_t>* aCompositorMods = nullptr,
                        VkImageUsageFlags aImageUsages = 0);
  bool SelectVulkanDecoderPhysicalDevice(
      const StaticMutexAutoLock& aProofOfLock, const nsCString& aRendererNode);
  bool VulkanCanDecodeFormat(AVCodecID aCodecID, unsigned aAvcodecVersion,
                             AVBufferRef* aVulkanDeviceContext,
                             const MediaByteBuffer* aExtraData,
                             gfx::ColorDepth aColorDepth) const;
  uint32_t mNegotiatedCompositorDecoderVendorID = 0;
  uint32_t mNegotiatedCompositorDecoderDeviceID = 0;
  char mNegotiatedVulkanDeviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {
      '\0',
  };
  bool mDecoderMatchesCompositor = false;
  // Set when ImageFormatProperties2 left only LINEAR and we forced NVIDIA BL.
  bool mForcedNvidiaBlockLinear = false;
  ~FFmpegVulkanVideoDecoder();
  void Cleanup();

  // Per-frame Vulkan operations: these methods access the outer decoder's
  // AVBufferRef* and VideoFramePool via explicit parameters to avoid
  // a back-pointer into the enclosing FFmpegVideoDecoder object.
  bool InitCtx(VkDevice aDevice, VkPhysicalDevice aPhysDev,
               PFN_vkGetInstanceProcAddr aGetProcAddr, VkInstance aInstance,
               uint64_t aGeneration, uint32_t aCopyQueueFamilyIndex,
               VkDeviceQueueCreateFlags aQueueCreateFlags);
  MediaResult InitCopyRingBuffer(uint32_t aWidth, uint32_t aHeight,
                                 AVPixelFormat aSwFormat,
                                 AVBufferRef* aVulkanDevCtx);
  MediaResult InitExternalSemaphores(AVBufferRef* aVulkanDevCtx);
  // Export the decoded Vulkan frame as a DMA-BUF fd (AVDRMFrameDescriptor) so
  // the compositor can consume the GPU buffer zero-copy. Used for the copy
  // path, when FFmpeg decoded into opaque/optimal tiling that the compositor
  // cannot directly import. When the decoded frame already carries a DRM
  // modifier the compositor supports, av_hwframe_map is used instead (wrapping
  // the existing Vulkan buffer without a copy). The copy path is the default
  // and serves as a reliable fallback for all currently released FFmpeg
  // versions (e.g. Ubuntu-shipped packages).
  MediaResult PrepareImageToDRM(AVFrame* aSrcFrame, int* aOutFd,
                                size_t* aOutSize, uint32_t* aOutYPitch,
                                uint32_t* aOutUVPitch, size_t* aOutUVOffset,
                                AVBufferRef* aVulkanDevCtx,
                                VideoFramePool<LIBAV_VER>* aFramePool,
                                int32_t* aOutBufIdx = nullptr,
                                bool aIsCopy = true);
};

#endif  // DOM_MEDIA_PLATFORMS_FFMPEG_FFMPEGVULKANVIDEODECODER_H_
