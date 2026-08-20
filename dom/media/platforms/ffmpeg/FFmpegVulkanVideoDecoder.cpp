/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FFmpegLog.h"
#include "FFmpegVideoDecoder.h"
#include "H264.h"
#include "mozilla/DataMutex.h"
#include "mozilla/ScopeExit.h"
#include "nsPrintfCString.h"
#if LIBAVCODEC_VERSION_MAJOR >= 60 && !defined(FFVPX_VERSION)
#  if defined(MOZ_USE_HWDECODE) && defined(MOZ_WIDGET_GTK)
#    include <dlfcn.h>
#    include <errno.h>
// mozilla/widget/DMABufFormats.h (via FFmpegVideoDecoder.h -> DMABufDevice.h)
// may define DRM_FORMAT_MOD_INVALID before libdrm; same pattern as
// DMABufSurface.cpp / FFmpegVideoFramePool.cpp.
#    ifdef DRM_FORMAT_MOD_INVALID
#      undef DRM_FORMAT_MOD_INVALID
#    endif
#    include <libdrm/drm_fourcc.h>
#    ifndef DRM_FORMAT_MOD_INVALID
#      define DRM_FORMAT_MOD_INVALID ((1ULL << 56) - 1)
#    endif
#    include <string.h>
#    include <sys/stat.h>

#    include <algorithm>
#    include <vector>

#    include "libavutil/hwcontext.h"
#    include "libavutil/hwcontext_vulkan.h"
#    include "libavutil/macros.h"
#    include "libavutil/pixfmt.h"
#    include "libavutil/version.h"
#    include "mozilla/StaticPrefs_media.h"
#    ifdef __linux__
#      include <sys/sysmacros.h>
#    elif defined(XP_SOLARIS) || defined(__sun)
#      include <sys/mkdev.h>  // major(), minor() for st_rdev
#    elif defined(XP_FREEBSD) || defined(XP_OPENBSD) || defined(XP_NETBSD)
#      include <sys/types.h>  // major(), minor() for st_rdev (BSD)
#    endif
#  endif  // MOZ_USE_HWDECODE && MOZ_WIDGET_GTK
#endif    // LIBAVCODEC_VERSION_MAJOR >= 60 && !defined(FFVPX_VERSION)

namespace mozilla {

#if defined(MOZ_USE_HWDECODE) && defined(MOZ_WIDGET_GTK)
#  if LIBAVCODEC_VERSION_MAJOR >= 60 && !defined(FFVPX_VERSION)

FFmpegVideoDecoder<
    LIBAV_VER>::FFmpegVulkanVideoDecoder::~FFmpegVulkanVideoDecoder() {
  if (!StaticPrefs::media_ffvpx_hw_enabled()) {
    return;
  }
  // Resources should already be cleaned up by ProcessShutdown()
  // If mDevice is not null here, it means ProcessShutdown wasn't called
  // and the device may already be destroyed - don't try to clean up
  if (mDevice != VK_NULL_HANDLE) {
    NS_WARNING(
        "~FFmpegVulkanVideoDecoder called with device still set - resources "
        "may leak");
  }
}

void FFmpegVideoDecoder<LIBAV_VER>::FFmpegVulkanVideoDecoder::Cleanup() {
  FFMPEGV_LOG("FFmpegVulkanVideoDecoder::Cleanup()");
  if (mDevice != VK_NULL_HANDLE) {
    // Wait on per-decoder copy fences instead of vkDeviceWaitIdle, so we
    // don't stall the shared VkDevice and block other decoders.
    if (mWaitForFences) {
      for (uint32_t qi = 0; qi < mCopyQueueCount; qi++) {
        if (mCopyFence[qi] != VK_NULL_HANDLE) {
          mWaitForFences(mDevice, 1, &mCopyFence[qi], VK_TRUE, UINT64_MAX);
        }
      }
    }
    for (uint32_t qi = 0; qi < mCopyQueueCount; qi++) {
      if ((mCopyCmdBuf[qi] != VK_NULL_HANDLE) &&
          (mCopyCmdPool[qi] != VK_NULL_HANDLE) && mFreeCommandBuffers) {
        mFreeCommandBuffers(mDevice, mCopyCmdPool[qi], 1, &mCopyCmdBuf[qi]);
      }
      if (mCopyCmdPool[qi] != VK_NULL_HANDLE && mDestroyCommandPool) {
        mDestroyCommandPool(mDevice, mCopyCmdPool[qi], nullptr);
      }
      if (mCopyFence[qi] != VK_NULL_HANDLE && mDestroyFence) {
        mDestroyFence(mDevice, mCopyFence[qi], nullptr);
      }
    }

    for (int i = 0; i < kNumBuffers; i++) {
      if (mCopyDoneSemFd[i] >= 0) {
        close(mCopyDoneSemFd[i]);
        mCopyDoneSemFd[i] = -1;
      }
      mCopyDoneSemValue[i] = 0;
      mCopyDoneSemSignaled[i] = false;
      if ((mCopyDoneSem[i] != VK_NULL_HANDLE) && mDestroySemaphore) {
        mDestroySemaphore(mDevice, mCopyDoneSem[i], nullptr);
        mCopyDoneSem[i] = VK_NULL_HANDLE;
      }
      if (mNv12BaseFd[i] >= 0) {
        close(mNv12BaseFd[i]);
      }
      if ((mNv12Image[i] != VK_NULL_HANDLE) && mDestroyImage) {
        mDestroyImage(mDevice, mNv12Image[i], nullptr);
      }
      if ((mNv12Mem[i] != VK_NULL_HANDLE) && mFreeMemory) {
        mFreeMemory(mDevice, mNv12Mem[i], nullptr);
      }
    }
  }

  mDevice = VK_NULL_HANDLE;
  mCopyQueueCount = 0;
  mCopyQueueIsDedicatedTransfer = false;
  mCopyQueueRoundRobin = 0;
  mCopyQueue.Clear();
  mCopyCmdPool.Clear();
  mCopyCmdBuf.Clear();
  mCopyFence.Clear();
  mDeviceFunctions.Clear();

  for (int i = 0; i < kNumBuffers; i++) {
    mNv12Image[i] = VK_NULL_HANDLE;
    mNv12Mem[i] = VK_NULL_HANDLE;
    mNv12BaseFd[i] = -1;
    mCopyDoneSem[i] = VK_NULL_HANDLE;
    mCopyDoneSemFd[i] = -1;
    mCopyDoneSemValue[i] = 0;
    mCopyDoneSemSignaled[i] = false;
  }
  mCurrentBuffer = 0;

  mWidth = 0;
  mHeight = 0;
  mTotalSize = 0;
  mUvOffset = 0;
  mYPitch = 0;
  mUvPitch = 0;
}

namespace {

// Cached instance-level Vulkan function pointers, shared across all decoders
// for the lifetime of the process as long as the VkInstance doesn't change.
// Keyed by (VkInstance, generation): the shared VkInstance is destroyed
// once no decoder references it, and VkInstance is loader-owned heap
// memory a later-created instance could, in principle, reuse the address
// of. Keying on the address alone risks serving stale pointers from the
// old, freed instance. Unconfirmed in practice; see
// VulkanDeviceHolder::Generation().
struct InstanceFunctionCache {
  VkInstance mInstance = VK_NULL_HANDLE;
  uint64_t mGeneration = 0;
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
  PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR
      mGetPhysicalDeviceVideoCapabilitiesKHR = nullptr;
  template <typename CodecCaps>
  struct CachedVideoCaps {
    bool mQueried = false;
    bool mValid = false;
    uint32_t mProfileIdc = 0;
    uint32_t mLayout = 0;
    uint32_t mExtra = 0;
    uint8_t mBitDepth = 0;
    uint8_t mChromaBitDepth = 0;
    VkPhysicalDevice mPhysDev = VK_NULL_HANDLE;
    VkVideoChromaSubsamplingFlagBitsKHR mChroma{};
    VkVideoCapabilitiesKHR mCaps{};
    VkVideoDecodeCapabilitiesKHR mDecodeCaps{};
    CodecCaps mCodecCaps{};

    bool Matches(VkPhysicalDevice aPhysDev, uint32_t aProfileIdc,
                 uint32_t aLayout, uint32_t aExtra, uint8_t aBitDepth,
                 uint8_t aChromaBitDepth,
                 VkVideoChromaSubsamplingFlagBitsKHR aChroma) const {
      return mQueried && mPhysDev == aPhysDev && mProfileIdc == aProfileIdc &&
             mLayout == aLayout && mExtra == aExtra && mBitDepth == aBitDepth &&
             mChromaBitDepth == aChromaBitDepth && mChroma == aChroma;
    }
  };
  CachedVideoCaps<VkVideoDecodeH264CapabilitiesKHR> mH264;
  CachedVideoCaps<VkVideoDecodeH265CapabilitiesKHR> mHevc;
#    if LIBAVCODEC_VERSION_MAJOR >= 61
  CachedVideoCaps<VkVideoDecodeAV1CapabilitiesKHR> mAv1;
#    endif
#    if LIBAVCODEC_VERSION_MAJOR >= 62
  CachedVideoCaps<VkVideoDecodeVP9CapabilitiesKHR> mVp9;
#    endif
  // Flat array of all the above pointers for use by IsLoaded().
  nsTArray<PFN_vkVoidFunction> mFnPtrs;
};

}  // namespace

constinit static StaticDataMutex<InstanceFunctionCache> sInstanceFnCache{
    "VulkanInstanceFunctions"};

void FFmpegVideoDecoder<LIBAV_VER>::FFmpegVulkanVideoDecoder::
    LoadInstanceFunctions(PFN_vkGetInstanceProcAddr aGetProcAddr,
                          VkInstance aInst, VkPhysicalDevice aPhysDev,
                          uint64_t aGeneration) {
  auto cache = sInstanceFnCache.Lock();
  if (cache->mInstance == aInst && cache->mGeneration == aGeneration &&
      cache->mGetDeviceProcAddr) {
    mGetDeviceProcAddr = cache->mGetDeviceProcAddr;
    mGetPhysicalDeviceProperties = cache->mGetPhysicalDeviceProperties;
    mGetPhysicalDeviceQueueFamilyProperties =
        cache->mGetPhysicalDeviceQueueFamilyProperties;
    mGetPhysicalDeviceMemoryProperties =
        cache->mGetPhysicalDeviceMemoryProperties;
    mGetPhysicalDeviceFormatProperties2 =
        cache->mGetPhysicalDeviceFormatProperties2;
    mGetPhysicalDeviceImageFormatProperties2 =
        cache->mGetPhysicalDeviceImageFormatProperties2;
    mGetPhysicalDeviceExternalSemaphoreProperties =
        cache->mGetPhysicalDeviceExternalSemaphoreProperties;
    mInstanceFunctions = cache->mFnPtrs.Clone();
    return;
  }

  FFMPEGV_LOG(
      "[VULKAN] (Re)loading instance functions for instance {} (gen {}, "
      "previously cached: instance {} gen {})",
      (void*)aInst, aGeneration, (void*)cache->mInstance, cache->mGeneration);

  mInstanceFunctions.Clear();
  auto load = [&]<typename T>(T& fn, const char* name) {
    fn = reinterpret_cast<T>(aGetProcAddr(aInst, name));
    if (!fn) {
      NS_WARNING(nsPrintfCString("[VULKAN] Failed to load %s", name).get());
    }
    mInstanceFunctions.AppendElement((PFN_vkVoidFunction)fn);
  };

  load(mGetDeviceProcAddr, "vkGetDeviceProcAddr");
  load(mGetPhysicalDeviceProperties, "vkGetPhysicalDeviceProperties");
  load(mGetPhysicalDeviceQueueFamilyProperties,
       "vkGetPhysicalDeviceQueueFamilyProperties");
  load(mGetPhysicalDeviceMemoryProperties,
       "vkGetPhysicalDeviceMemoryProperties");
  load(mGetPhysicalDeviceFormatProperties2,
       "vkGetPhysicalDeviceFormatProperties2");
  load(mGetPhysicalDeviceImageFormatProperties2,
       "vkGetPhysicalDeviceImageFormatProperties2");
  load(mGetPhysicalDeviceExternalSemaphoreProperties,
       "vkGetPhysicalDeviceExternalSemaphoreProperties");

  cache->mInstance = aInst;
  cache->mGeneration = aGeneration;
  cache->mGetDeviceProcAddr = mGetDeviceProcAddr;
  cache->mGetPhysicalDeviceProperties = mGetPhysicalDeviceProperties;
  cache->mGetPhysicalDeviceQueueFamilyProperties =
      mGetPhysicalDeviceQueueFamilyProperties;
  cache->mGetPhysicalDeviceMemoryProperties =
      mGetPhysicalDeviceMemoryProperties;
  cache->mGetPhysicalDeviceFormatProperties2 =
      mGetPhysicalDeviceFormatProperties2;
  cache->mGetPhysicalDeviceImageFormatProperties2 =
      mGetPhysicalDeviceImageFormatProperties2;
  cache->mGetPhysicalDeviceExternalSemaphoreProperties =
      mGetPhysicalDeviceExternalSemaphoreProperties;
  cache->mGetPhysicalDeviceVideoCapabilitiesKHR =
      reinterpret_cast<PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR>(
          aGetProcAddr(aInst, "vkGetPhysicalDeviceVideoCapabilitiesKHR"));
  cache->mFnPtrs = mInstanceFunctions.Clone();
  cache->mH264 = {};
  cache->mHevc = {};
#    if LIBAVCODEC_VERSION_MAJOR >= 61
  cache->mAv1 = {};
#    endif
#    if LIBAVCODEC_VERSION_MAJOR >= 62
  cache->mVp9 = {};
#    endif
}

namespace {

VkVideoComponentBitDepthFlagBitsKHR VkComponentBitDepth(uint8_t aDepth) {
  switch (aDepth) {
    case 8:
      return VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    case 10:
      return VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR;
    case 12:
      return VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR;
    default:
      return static_cast<VkVideoComponentBitDepthFlagBitsKHR>(0);
  }
}

uint8_t ColorDepthToBits(gfx::ColorDepth aDepth) {
  switch (aDepth) {
    case gfx::ColorDepth::COLOR_10:
      return 10;
    case gfx::ColorDepth::COLOR_12:
      return 12;
    default:
      return 8;
  }
}

template <typename CodecCaps>
bool QueryVideoCaps(PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR aGetCaps,
                    VkPhysicalDevice aPhysDev,
                    VkVideoCodecOperationFlagBitsKHR aOp,
                    const void* aCodecProfile, VkStructureType aCodecCapsSType,
                    VkVideoChromaSubsamplingFlagBitsKHR aChroma,
                    VkVideoComponentBitDepthFlagBitsKHR aLumaBits,
                    VkVideoComponentBitDepthFlagBitsKHR aChromaBits,
                    InstanceFunctionCache::CachedVideoCaps<CodecCaps>* aOut) {
  if (!aOut) {
    return false;
  }
  *aOut = {};
  if (!aGetCaps || aPhysDev == VK_NULL_HANDLE || !aCodecProfile || !aChroma ||
      !aLumaBits) {
    return false;
  }
  if (aChroma != VK_VIDEO_CHROMA_SUBSAMPLING_MONOCHROME_BIT_KHR &&
      !aChromaBits) {
    return false;
  }

  VkVideoDecodeUsageInfoKHR usage{};
  usage.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_USAGE_INFO_KHR;
  usage.pNext = aCodecProfile;
  usage.videoUsageHints = VK_VIDEO_DECODE_USAGE_DEFAULT_KHR;

  VkVideoProfileInfoKHR profile{};
  profile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
  profile.pNext = &usage;
  profile.videoCodecOperation = aOp;
  profile.chromaSubsampling = aChroma;
  profile.lumaBitDepth = aLumaBits;
  profile.chromaBitDepth = aChromaBits;

  aOut->mCodecCaps.sType = aCodecCapsSType;
  aOut->mDecodeCaps.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR;
  aOut->mDecodeCaps.pNext = &aOut->mCodecCaps;
  aOut->mCaps.sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;
  aOut->mCaps.pNext = &aOut->mDecodeCaps;

  const VkResult res = aGetCaps(aPhysDev, &profile, &aOut->mCaps);
  if (res != VK_SUCCESS) {
    FFMPEGV_LOG("vkGetPhysicalDeviceVideoCapabilitiesKHR failed ({}) op=0x{:x}",
                (int)res, (unsigned)aOp);
    *aOut = {};
    return false;
  }
  return true;
}

template <typename CodecCaps>
void EnsureVideoCapsSlot(
    PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR aGetCaps,
    VkPhysicalDevice aPhysDev, VkVideoCodecOperationFlagBitsKHR aOp,
    const void* aCodecProfile, VkStructureType aCodecCapsSType,
    VkVideoChromaSubsamplingFlagBitsKHR aChroma,
    VkVideoComponentBitDepthFlagBitsKHR aLumaBits,
    VkVideoComponentBitDepthFlagBitsKHR aChromaBits, uint32_t aProfileIdc,
    uint32_t aLayout, uint32_t aExtra, uint8_t aBitDepth,
    uint8_t aChromaBitDepth,
    InstanceFunctionCache::CachedVideoCaps<CodecCaps>* aSlot) {
  if (!aSlot || aSlot->Matches(aPhysDev, aProfileIdc, aLayout, aExtra,
                               aBitDepth, aChromaBitDepth, aChroma)) {
    return;
  }
  const bool valid =
      QueryVideoCaps(aGetCaps, aPhysDev, aOp, aCodecProfile, aCodecCapsSType,
                     aChroma, aLumaBits, aChromaBits, aSlot);
  aSlot->mQueried = true;
  aSlot->mValid = valid;
  aSlot->mPhysDev = aPhysDev;
  aSlot->mProfileIdc = aProfileIdc;
  aSlot->mLayout = aLayout;
  aSlot->mExtra = aExtra;
  aSlot->mBitDepth = aBitDepth;
  aSlot->mChromaBitDepth = aChromaBitDepth;
  aSlot->mChroma = aChroma;
}

void EnsureVideoCaps(InstanceFunctionCache& aCache, VkPhysicalDevice aPhysDev,
                     gfx::ColorDepth aColorDepth, gfx::ColorDepth aChromaDepth,
                     VkVideoChromaSubsamplingFlagBitsKHR aChroma,
                     AVCodecID aCodecID, uint32_t aProfileIdc,
                     uint32_t aPictureLayout) {
  const uint8_t bitDepth = ColorDepthToBits(aColorDepth);
  const uint8_t chromaBitDepth = ColorDepthToBits(aChromaDepth);
  const auto lumaBits = VkComponentBitDepth(bitDepth);
  const auto chromaBits =
      aChroma == VK_VIDEO_CHROMA_SUBSAMPLING_MONOCHROME_BIT_KHR
          ? static_cast<VkVideoComponentBitDepthFlagBitsKHR>(0)
          : VkComponentBitDepth(chromaBitDepth);
  auto* getCaps = aCache.mGetPhysicalDeviceVideoCapabilitiesKHR;

  switch (aCodecID) {
    case AV_CODEC_ID_H264: {
      VkVideoDecodeH264ProfileInfoKHR h264Profile{};
      h264Profile.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR;
      h264Profile.stdProfileIdc =
          static_cast<StdVideoH264ProfileIdc>(aProfileIdc);
      h264Profile.pictureLayout =
          static_cast<VkVideoDecodeH264PictureLayoutFlagBitsKHR>(
              aPictureLayout);
      EnsureVideoCapsSlot(
          getCaps, aPhysDev, VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
          &h264Profile, VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR,
          aChroma, lumaBits, chromaBits, h264Profile.stdProfileIdc,
          h264Profile.pictureLayout, 0, bitDepth, chromaBitDepth,
          &aCache.mH264);
      break;
    }
    case AV_CODEC_ID_HEVC: {
      VkVideoDecodeH265ProfileInfoKHR hevcProfile{};
      hevcProfile.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PROFILE_INFO_KHR;
      hevcProfile.stdProfileIdc = STD_VIDEO_H265_PROFILE_IDC_MAIN;
      EnsureVideoCapsSlot(
          getCaps, aPhysDev, VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR,
          &hevcProfile, VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_CAPABILITIES_KHR,
          aChroma, lumaBits, chromaBits, hevcProfile.stdProfileIdc, 0, 0,
          bitDepth, chromaBitDepth, &aCache.mHevc);
      break;
    }
#    if LIBAVCODEC_VERSION_MAJOR >= 61
    case AV_CODEC_ID_AV1: {
      VkVideoDecodeAV1ProfileInfoKHR av1Profile{};
      av1Profile.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_PROFILE_INFO_KHR;
      av1Profile.stdProfile = STD_VIDEO_AV1_PROFILE_MAIN;
      av1Profile.filmGrainSupport = VK_FALSE;
      EnsureVideoCapsSlot(
          getCaps, aPhysDev, VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR,
          &av1Profile, VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_CAPABILITIES_KHR,
          aChroma, lumaBits, chromaBits, av1Profile.stdProfile, 0,
          av1Profile.filmGrainSupport, bitDepth, chromaBitDepth, &aCache.mAv1);
      break;
    }
#    endif
#    if LIBAVCODEC_VERSION_MAJOR >= 62
    case AV_CODEC_ID_VP9: {
      VkVideoDecodeVP9ProfileInfoKHR vp9Profile{};
      vp9Profile.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_VP9_PROFILE_INFO_KHR;
      vp9Profile.stdProfile = STD_VIDEO_VP9_PROFILE_0;
      EnsureVideoCapsSlot(
          getCaps, aPhysDev, VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR,
          &vp9Profile, VK_STRUCTURE_TYPE_VIDEO_DECODE_VP9_CAPABILITIES_KHR,
          aChroma, lumaBits, chromaBits, vp9Profile.stdProfile, 0, 0, bitDepth,
          chromaBitDepth, &aCache.mVp9);
      break;
    }
#    endif
    default:
      break;
  }
}

uint32_t MapH264VulkanProfileIdc(uint8_t aIdc) {
  switch (aIdc) {
    case STD_VIDEO_H264_PROFILE_IDC_BASELINE:
    case STD_VIDEO_H264_PROFILE_IDC_MAIN:
    case STD_VIDEO_H264_PROFILE_IDC_HIGH:
    case STD_VIDEO_H264_PROFILE_IDC_HIGH_444_PREDICTIVE:
      return aIdc;
#    if LIBAVCODEC_VERSION_MAJOR >= 61
    case AV_PROFILE_H264_HIGH_10:
    case AV_PROFILE_H264_HIGH_422:
    case AV_PROFILE_H264_HIGH_444:
#    else
    case FF_PROFILE_H264_HIGH_10:
    case FF_PROFILE_H264_HIGH_422:
    case FF_PROFILE_H264_HIGH_444:
#    endif
      return STD_VIDEO_H264_PROFILE_IDC_HIGH;
    default:
      return STD_VIDEO_H264_PROFILE_IDC_MAIN;
  }
}

void H264VulkanProbeFormat(const MediaByteBuffer* aExtraData,
                           gfx::ColorDepth aLumaDepth,
                           gfx::ColorDepth* aChromaDepth,
                           VkVideoChromaSubsamplingFlagBitsKHR* aChroma,
                           uint32_t* aProfileIdc, uint32_t* aPictureLayout) {
  SPSData sps;
  if (!aExtraData || !H264::DecodeSPSFromExtraData(aExtraData, sps)) {
    *aChroma = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    *aChromaDepth = aLumaDepth;
    *aProfileIdc = STD_VIDEO_H264_PROFILE_IDC_MAIN;
    *aPictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR;
    return;
  }
  *aProfileIdc = MapH264VulkanProfileIdc(sps.profile_idc);
  *aPictureLayout =
      sps.interlaced
          ? VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_INTERLACED_INTERLEAVED_LINES_BIT_KHR
          : VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR;
  switch (sps.chroma_format_idc) {
    case 0:
      *aChroma = VK_VIDEO_CHROMA_SUBSAMPLING_MONOCHROME_BIT_KHR;
      break;
    case 2:
      *aChroma = VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR;
      break;
    case 3:
      *aChroma = VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR;
      break;
    default:
      *aChroma = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
      break;
  }
  *aChromaDepth =
      sps.chroma_format_idc == 0
          ? aLumaDepth
          : gfx::ColorDepthForBitDepth(sps.bit_depth_chroma_minus8 + 8);
}

}  // namespace

bool FFmpegVideoDecoder<LIBAV_VER>::FFmpegVulkanVideoDecoder::
    VulkanCanDecodeFormat(AVCodecID aCodecID, unsigned aAvcodecVersion,
                          AVBufferRef* aVulkanDeviceContext,
                          const MediaByteBuffer* aExtraData,
                          gfx::ColorDepth aColorDepth) const {
  if (aCodecID != AV_CODEC_ID_H264 ||
      aAvcodecVersion >= AV_VERSION_INT(61, 19, 100) || !aVulkanDeviceContext) {
    return true;
  }

  const auto* devCtx =
      reinterpret_cast<AVHWDeviceContext*>(aVulkanDeviceContext->data);
  const auto* vkCtx = reinterpret_cast<AVVulkanDeviceContext*>(devCtx->hwctx);

  gfx::ColorDepth chromaDepth;
  VkVideoChromaSubsamplingFlagBitsKHR chroma;
  uint32_t profileIdc;
  uint32_t pictureLayout;
  H264VulkanProbeFormat(aExtraData, aColorDepth, &chromaDepth, &chroma,
                        &profileIdc, &pictureLayout);

  // FFmpeg < n7.1 (libavcodec < 61.19.100) mishandles COINCIDE without
  // SEPARATE_REFERENCE_IMAGES and SIGSEGVs in ff_vk_exec_add_dep_frame.
  auto cache = sInstanceFnCache.Lock();
  EnsureVideoCaps(*cache, vkCtx->phys_dev, aColorDepth, chromaDepth, chroma,
                  aCodecID, profileIdc, pictureLayout);
  if (cache->mH264.mValid) {
    const bool coincide =
        cache->mH264.mDecodeCaps.flags &
        VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR;
    const bool separate = cache->mH264.mCaps.flags &
                          VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR;
    if (coincide && !separate) {
      FFMPEGV_LOG(
          "Vulkan H.264 skipped: FFmpeg < n7.1 and driver reports "
          "COINCIDE without SEPARATE_REFERENCE_IMAGES");
      return false;
    }
  }
  return true;
}

void FFmpegVideoDecoder<
    LIBAV_VER>::FFmpegVulkanVideoDecoder::LoadDeviceFunctions(VkDevice aDev) {
  mDeviceFunctions.Clear();
  auto load = [&]<typename T>(T& fn, const char* name) {
    fn = (T)(void*)mGetDeviceProcAddr(aDev, name);
    if (!fn) {
      NS_WARNING(nsPrintfCString("[VULKAN] Failed to load %s", name).get());
    }
    mDeviceFunctions.AppendElement((PFN_vkVoidFunction)fn);
  };

  load(mCreateCommandPool, "vkCreateCommandPool");
  load(mDestroyCommandPool, "vkDestroyCommandPool");
  load(mAllocateCommandBuffers, "vkAllocateCommandBuffers");
  load(mFreeCommandBuffers, "vkFreeCommandBuffers");
  load(mBeginCommandBuffer, "vkBeginCommandBuffer");
  load(mEndCommandBuffer, "vkEndCommandBuffer");
  load(mGetDeviceQueue2, "vkGetDeviceQueue2");
  load(mQueueSubmit, "vkQueueSubmit");
  load(mCmdPipelineBarrier, "vkCmdPipelineBarrier");
  load(mCmdCopyImage, "vkCmdCopyImage");

  load(mCreateImage, "vkCreateImage");
  load(mDestroyImage, "vkDestroyImage");
  load(mGetImageMemoryRequirements, "vkGetImageMemoryRequirements");
  load(mGetImageMemoryRequirements2, "vkGetImageMemoryRequirements2");
  load(mGetImageSubresourceLayout, "vkGetImageSubresourceLayout");
  load(mBindImageMemory, "vkBindImageMemory");
  load(mAllocateMemory, "vkAllocateMemory");
  load(mFreeMemory, "vkFreeMemory");

  load(mCreateFence, "vkCreateFence");
  load(mDestroyFence, "vkDestroyFence");
  load(mResetFences, "vkResetFences");
  load(mWaitForFences, "vkWaitForFences");
  load(mGetSemaphoreCounterValue, "vkGetSemaphoreCounterValue");
  load(mGetMemoryFdKHR, "vkGetMemoryFdKHR");
  load(mGetImageDrmFormatModifierPropertiesEXT,
       "vkGetImageDrmFormatModifierPropertiesEXT");
  load(mCreateSemaphore, "vkCreateSemaphore");
  load(mDestroySemaphore, "vkDestroySemaphore");
  load(mWaitSemaphores, "vkWaitSemaphores");
  load(mGetSemaphoreFdKHR, "vkGetSemaphoreFdKHR");
}

bool FFmpegVideoDecoder<LIBAV_VER>::FFmpegVulkanVideoDecoder::IsLoaded() const {
  if (mInstanceFunctions.IsEmpty() || mDeviceFunctions.IsEmpty()) {
    return false;
  }
  auto allValid = [](const auto& aFuncs) {
    return std::all_of(aFuncs.begin(), aFuncs.end(),
                       [](auto aFn) { return aFn != nullptr; });
  };
  return allValid(mInstanceFunctions) && allValid(mDeviceFunctions);
}

void FFmpegVideoDecoder<LIBAV_VER>::FFmpegVulkanVideoDecoder::InitDrmModifiers(
    VkPhysicalDevice aPhysDev, VkFormat aFormatForModifiers,
    const nsTArray<uint64_t>* aCompositorMods, VkImageUsageFlags aImageUsages) {
  mDrmModifiers.clear();
  mExportRequiresDedicatedByModifier.Clear();
  mForcedNvidiaBlockLinear = false;

  FFMPEGV_LOG("[VULKAN] Compositor {} modifier(s) for intersection",
              aCompositorMods ? aCompositorMods->Length() : 0);
  const bool isCompositorSupportsOnlyLinear =
      !aCompositorMods || aCompositorMods->IsEmpty() ||
      (aCompositorMods->Length() == 1 &&
       aCompositorMods->ElementAt(0) == DRM_FORMAT_MOD_LINEAR);
  if (isCompositorSupportsOnlyLinear) {
    FFMPEGV_LOG(
        "[VULKAN] Compositor supports only LINEAR modifier; negotiation will "
        "intersect with decoder (result at most LINEAR)");
  }

  // Query decoder device for supported DRM modifiers, then intersect with
  // compositor. Format must match decoded stream (NV12 or P010) so we use
  // aFormatForModifiers from decoded info. Do not add
  // VK_IMAGE_USAGE_STORAGE_BIT to aImageUsages; the query must use only
  // transfer and video-decode usage bits.
  if (mGetPhysicalDeviceFormatProperties2) {
    const VkFormat formatForModifiers = aFormatForModifiers;
    VkDrmFormatModifierPropertiesListEXT modList = {};
    modList.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;

    VkFormatProperties2 formatProps = {};
    formatProps.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    formatProps.pNext = &modList;

    mGetPhysicalDeviceFormatProperties2(aPhysDev, formatForModifiers,
                                        &formatProps);

    if (modList.drmFormatModifierCount > 0) {
      std::vector<VkDrmFormatModifierPropertiesEXT> modProps(
          modList.drmFormatModifierCount);
      modList.pDrmFormatModifierProperties = modProps.data();
      mGetPhysicalDeviceFormatProperties2(aPhysDev, formatForModifiers,
                                          &formatProps);

      NS_WARNING(
          nsPrintfCString("[VULKAN] Found %u DRM modifiers for format 0x%x",
                          modList.drmFormatModifierCount,
                          (unsigned)formatForModifiers)
              .get());

      for (uint32_t i = 0; i < modList.drmFormatModifierCount; i++) {
        NS_WARNING(
            nsPrintfCString("[VULKAN]   0x%llx (planes=%u, features=0x%x)",
                            (unsigned long long)modProps[i].drmFormatModifier,
                            modProps[i].drmFormatModifierPlaneCount,
                            modProps[i].drmFormatModifierTilingFeatures)
                .get());
        if (aCompositorMods) {
          if (!aCompositorMods->Contains(modProps[i].drmFormatModifier)) {
            FFMPEGV_LOG(
                "[VULKAN]   modifier 0x{:x}: not supported by compositor",
                (unsigned long long)modProps[i].drmFormatModifier);
            continue;
          }
        } else if (modProps[i].drmFormatModifier != DRM_FORMAT_MOD_LINEAR) {
          FFMPEGV_LOG(
              "[VULKAN]   modifier 0x{:x}: skipped without compositor list",
              (unsigned long long)modProps[i].drmFormatModifier);
          continue;
        }
        if (!(modProps[i].drmFormatModifierTilingFeatures &
              (VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
               VK_FORMAT_FEATURE_TRANSFER_DST_BIT))) {
          FFMPEGV_LOG(
              "[VULKAN]   modifier 0x{:x}: skipped, missing transfer "
              "src/dst tiling features",
              (unsigned long long)modProps[i].drmFormatModifier);
          continue;
        }
        if (mGetPhysicalDeviceImageFormatProperties2) {
          VkPhysicalDeviceImageDrmFormatModifierInfoEXT modInfo = {};
          modInfo.sType =
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
          modInfo.drmFormatModifier = modProps[i].drmFormatModifier;
          modInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

          VkPhysicalDeviceExternalImageFormatInfo extFormatInfo = {};
          extFormatInfo.sType =
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
          extFormatInfo.pNext = &modInfo;
          extFormatInfo.handleType =
              VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

          VkPhysicalDeviceImageFormatInfo2 formatInfo = {};
          formatInfo.sType =
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
          formatInfo.pNext = &extFormatInfo;
          formatInfo.format = formatForModifiers;
          formatInfo.type = VK_IMAGE_TYPE_2D;
          formatInfo.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
          formatInfo.usage = aImageUsages;
          formatInfo.flags = 0;

          VkExternalImageFormatProperties extProps2 = {
              .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES_KHR,
          };

          VkImageFormatProperties2 props2 = {};
          props2.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
          props2.pNext = &extProps2;

          VkResult isFormatPropsSupported =
              mGetPhysicalDeviceImageFormatProperties2(aPhysDev, &formatInfo,
                                                       &props2);

          const bool exportRequiresDedicated =
              !!(extProps2.externalMemoryProperties.externalMemoryFeatures &
                 VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT);
          FFMPEGV_LOG("modifier 0x{:x}: DEDICATED_ONLY_BIT: {}",
                      (unsigned long long)modProps[i].drmFormatModifier,
                      exportRequiresDedicated ? "YES" : "NO");
          FFMPEGV_LOG("modifier 0x{:x}: DMA_BUF_BIT_EXT: {}",
                      (unsigned long long)modProps[i].drmFormatModifier,
                      extProps2.externalMemoryProperties.compatibleHandleTypes &
                              VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
                          ? "YES"
                          : "NO");
          FFMPEGV_LOG(
              "[VULKAN]   modifier 0x{:x}: image format props supported for "
              "usage 0x{:x}? {}",
              (unsigned long long)modProps[i].drmFormatModifier,
              (unsigned)aImageUsages,
              isFormatPropsSupported == VK_SUCCESS ? "YES" : "NO");

          if (isFormatPropsSupported != VK_SUCCESS) {
            continue;
          }
          mExportRequiresDedicatedByModifier.InsertOrUpdate(
              modProps[i].drmFormatModifier, exportRequiresDedicated);
        }
        mDrmModifiers.push_back(modProps[i].drmFormatModifier);
      }
    }
  }

  if (mDrmModifiers.empty()) {
    mDrmModifiers.push_back(DRM_FORMAT_MOD_LINEAR);
    FFMPEGV_LOG("[VULKAN] No suitable modifiers found, using LINEAR");
  }

  // NVIDIA: Vulkan may under-report / fail validation for tiled modifiers on
  // older drivers. If negotiation left only LINEAR, force a known-working one
  // when the compositor already advertises it
  if (aCompositorMods && mNegotiatedCompositorDecoderVendorID == 0x10de &&
      mDecoderMatchesCompositor && mDrmModifiers.size() == 1 &&
      mDrmModifiers[0] == DRM_FORMAT_MOD_LINEAR) {
    // TU102 (Turing) starts at deviceID 0x1E00; everything below is Fermi-Volta
    // (including GV100). 0xfe is not a valid kind on Turing+.
    const uint64_t nvidiaMod =
        mNegotiatedCompositorDecoderDeviceID < 0x1E00
            ? DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 0, 0xfe, 1)
            : DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 2, 6, 1);
    FFMPEGV_LOG(
        "[VULKAN] ImageFormatProperties2 left only LINEAR; considering NVIDIA "
        "BL override 0x{:x} (deviceID=0x{:x})",
        (unsigned long long)nvidiaMod, mNegotiatedCompositorDecoderDeviceID);
    if (aCompositorMods->Contains(nvidiaMod)) {
      mDrmModifiers[0] = nvidiaMod;
      // ImageFormatProperties2 failed for YCbCr tiled mods; keep BL for the
      // copy path but do not attempt direct decode export.
      mForcedNvidiaBlockLinear = true;
      FFMPEGV_LOG("[VULKAN] Using forced NVIDIA BL modifier 0x{:x}",
                  (unsigned long long)nvidiaMod);
    }
  }

  FFMPEGV_LOG("[VULKAN] Using {} modifiers, first=0x{:x}", mDrmModifiers.size(),
              (unsigned long long)mDrmModifiers[0]);
}

static void* sVulkanLib = nullptr;
static bool sVulkanEnumerated = false;

static bool PhysicalDeviceHasVulkanVideoDecodeStack(
    PFN_vkEnumerateDeviceExtensionProperties aEnumerateExt,
    VkPhysicalDevice aDevice, const char* aDeviceName) {
  if (!aEnumerateExt) {
    return false;
  }
  static const char* const kRequired[] = {"VK_KHR_video_queue",
                                          "VK_KHR_video_decode_queue"};
  uint32_t extCount = 0;
  if (aEnumerateExt(aDevice, nullptr, &extCount, nullptr) != VK_SUCCESS ||
      extCount == 0) {
    return false;
  }
  std::vector<VkExtensionProperties> props(extCount);
  if (aEnumerateExt(aDevice, nullptr, &extCount, props.data()) != VK_SUCCESS) {
    return false;
  }
  for (const char* req : kRequired) {
    bool found = false;
    for (uint32_t i = 0; i < extCount; i++) {
      if (strcmp(props[i].extensionName, req) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      FFMPEGV_LOG("Skipping {}: missing required extension {}", aDeviceName,
                  req);
      return false;
    }
  }
  return true;
}

bool FFmpegVideoDecoder<LIBAV_VER>::FFmpegVulkanVideoDecoder::
    SelectVulkanDecoderPhysicalDevice(const StaticMutexAutoLock& aProofOfLock,
                                      const nsCString& aRendererNode) {
  uint32_t rendererDrmMajor = 0, rendererDrmMinor = 0;
#    if defined(MOZ_WIDGET_GTK)
  if (!aRendererNode.IsEmpty()) {
    struct stat st = {};
    if (stat(aRendererNode.get(), &st) == 0) {
      rendererDrmMajor = major(st.st_rdev);
      rendererDrmMinor = minor(st.st_rdev);
      FFMPEGV_LOG("Renderer device from GPU: {} (major={}, minor={})",
                  aRendererNode.get(), rendererDrmMajor, rendererDrmMinor);
    } else {
      FFMPEGV_LOG("Renderer device from GPU: {} - stat() failed (errno={})",
                  aRendererNode.get(), errno);
    }
  } else {
    // Empty when renderer is llvmpipe or glxtest failed to detect a DRM device
    FFMPEGV_LOG("Renderer device from GPU: empty (gfxVars::DrmRenderDevice)");
  }
#    endif

  const bool useCache = (rendererDrmMajor == 0 && rendererDrmMinor == 0);
  if (!sVulkanEnumerated || !useCache) {
    if (useCache) {
      sVulkanEnumerated = true;
    }

    if (!sVulkanLib) {
      sVulkanLib = dlopen("libvulkan.so.1", RTLD_LAZY);
      if (!sVulkanLib) {
        FFMPEGV_LOG("Failed to load libvulkan.so.1");
        return false;
      }
    }

    auto vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr)dlsym(sVulkanLib, "vkGetInstanceProcAddr");
    if (!vkGetInstanceProcAddr) {
      FFMPEGV_LOG("Failed to get vkGetInstanceProcAddr");
      return false;
    }

    auto vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(
        nullptr, "vkCreateInstance");
    if (!vkCreateInstance) {
      FFMPEGV_LOG("Failed to get vkCreateInstance");
      return false;
    }

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
      FFMPEGV_LOG("Failed to create Vulkan instance");
      return false;
    }

    auto vkDestroyInstance = (PFN_vkDestroyInstance)vkGetInstanceProcAddr(
        instance, "vkDestroyInstance");
    auto destroyInstance = MakeScopeExit([&] {
      if (vkDestroyInstance && instance) {
        vkDestroyInstance(instance, nullptr);
      }
    });

    auto vkEnumeratePhysicalDevices =
        (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(
            instance, "vkEnumeratePhysicalDevices");
    auto vkGetPhysicalDeviceProperties =
        (PFN_vkGetPhysicalDeviceProperties)vkGetInstanceProcAddr(
            instance, "vkGetPhysicalDeviceProperties");
    auto vkGetPhysicalDeviceProperties2 =
        (PFN_vkGetPhysicalDeviceProperties2)vkGetInstanceProcAddr(
            instance, "vkGetPhysicalDeviceProperties2");
    auto vkEnumerateDeviceExtensionProperties =
        (PFN_vkEnumerateDeviceExtensionProperties)vkGetInstanceProcAddr(
            instance, "vkEnumerateDeviceExtensionProperties");
    if (!vkEnumeratePhysicalDevices || !vkGetPhysicalDeviceProperties ||
        !vkEnumerateDeviceExtensionProperties) {
      NS_WARNING("Failed to get Vulkan enumeration functions");
      return false;
    }

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) {
      FFMPEGV_LOG("No Vulkan devices found");
      return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    // Collect valid devices (non-CPU, Vulkan 1.3+), sorted by type (discrete
    // first).
    std::vector<std::pair<VkPhysicalDeviceProperties, bool>> validDevices;
    for (uint32_t i = 0; i < count; i++) {
      VkPhysicalDeviceProperties p = {};
      bool isDecoderMatchesRendererFound = false;
      if (rendererDrmMajor && rendererDrmMinor &&
          vkGetPhysicalDeviceProperties2) {
        VkPhysicalDeviceDrmPropertiesEXT drmProps = {};
        drmProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;
        VkPhysicalDeviceProperties2 props2 = {};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &drmProps;
        vkGetPhysicalDeviceProperties2(devices[i], &props2);
        p = props2.properties;
        isDecoderMatchesRendererFound =
            (drmProps.hasRender && rendererDrmMajor == drmProps.renderMajor &&
             rendererDrmMinor == drmProps.renderMinor) ||
            (drmProps.hasPrimary && rendererDrmMajor == drmProps.primaryMajor &&
             rendererDrmMinor == drmProps.primaryMinor);
      } else {
        vkGetPhysicalDeviceProperties(devices[i], &p);
      }
      if (p.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) {
        uint32_t major = VK_API_VERSION_MAJOR(p.apiVersion);
        uint32_t minor = VK_API_VERSION_MINOR(p.apiVersion);
        if (major > 1 || (major == 1 && minor >= 3)) {
          if (!PhysicalDeviceHasVulkanVideoDecodeStack(
                  vkEnumerateDeviceExtensionProperties, devices[i],
                  p.deviceName)) {
            continue;
          }
          validDevices.push_back(
              std::make_pair(p, isDecoderMatchesRendererFound));
        }
      }
    }

    auto deviceTypePriority = [](VkPhysicalDeviceType t) -> int {
      switch (t) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
          return 3;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
          return 2;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
          return 1;
        default:
          return 0;
      }
    };
    std::sort(
        validDevices.begin(), validDevices.end(),
        [&deviceTypePriority](const auto& p1, const auto& p2) {
          if (p1.second != p2.second) {
            return p1.second > p2.second;  // renderer-matching device first
          }
          return deviceTypePriority(p1.first.deviceType) >
                 deviceTypePriority(p2.first.deviceType);  // discrete first
        });

    if (validDevices.empty()) {
      FFMPEGV_LOG(
          "No suitable Vulkan device found (need 1.3+, non-CPU, "
          "VK_KHR_video_queue + VK_KHR_video_decode_queue)");
      return false;
    }

    memcpy(mNegotiatedVulkanDeviceName, validDevices[0].first.deviceName,
           VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
    mNegotiatedCompositorDecoderVendorID = validDevices[0].first.vendorID;
    mNegotiatedCompositorDecoderDeviceID = validDevices[0].first.deviceID;
    mDecoderMatchesCompositor = validDevices[0].second;
    FFMPEGV_LOG(
        "Selected Vulkan device for video decoding: {} (vendorID=0x{:x}, "
        "deviceID=0x{:x}), matches renderer: {}",
        mNegotiatedVulkanDeviceName, mNegotiatedCompositorDecoderVendorID,
        mNegotiatedCompositorDecoderDeviceID,
        mDecoderMatchesCompositor ? "true" : "false");
  }
  return true;
}

bool FFmpegVideoDecoder<LIBAV_VER>::FFmpegVulkanVideoDecoder::InitCtx(
    VkDevice aDevice, VkPhysicalDevice aPhysDev,
    PFN_vkGetInstanceProcAddr aGetProcAddr, VkInstance aInstance,
    uint64_t aGeneration, uint32_t aCopyQueueFamilyIndex,
    VkDeviceQueueCreateFlags aQueueCreateFlags) {
  // Load instance-level functions once
  if (!mGetDeviceProcAddr) {
    LoadInstanceFunctions(aGetProcAddr, aInstance, aPhysDev, aGeneration);
  }

  // Reload mDevice-level functions when mDevice changes
  if (mDevice != aDevice) {
    // Cleanup old resources before switching mDevice
    PFN_vkGetDeviceProcAddr savedGetDeviceProcAddr = mGetDeviceProcAddr;
    PFN_vkGetPhysicalDeviceMemoryProperties savedGetMemProps =
        mGetPhysicalDeviceMemoryProperties;
    Cleanup();
    mGetDeviceProcAddr = savedGetDeviceProcAddr;
    mGetPhysicalDeviceMemoryProperties = savedGetMemProps;

    LoadDeviceFunctions(aDevice);

    if (!IsLoaded()) {
      FFMPEGV_LOG("Failed to load required Vulkan device functions");
      return false;
    }

    mDevice = aDevice;

    // Instead of forcing LINEAR tiling when the selected Vulkan device does
    // not match the renderer device, we negotiate DRM modifiers via IPC:
    // the decoder queries supported modifiers from the compositor and uses
    // the intersection. This removes the need for a forceLinear flag.

    uint32_t copyQueueFamilyIndex = aCopyQueueFamilyIndex;
    uint32_t transferOnlyQueueCount = 0;
    int32_t transferOnlyQueueFamilyIndex = -1;
    if (mGetPhysicalDeviceQueueFamilyProperties) {
      uint32_t queueFamilyCount = 0;
      mGetPhysicalDeviceQueueFamilyProperties(aPhysDev, &queueFamilyCount,
                                              nullptr);
      AutoTArray<VkQueueFamilyProperties, 8> props;
      if (queueFamilyCount > 0) {
        props.SetLength(queueFamilyCount);
        mGetPhysicalDeviceQueueFamilyProperties(aPhysDev, &queueFamilyCount,
                                                props.Elements());
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
          if (props[i].queueCount > 0 &&
              (props[i].queueFlags &
               (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == 0 &&
              (props[i].queueFlags & VK_QUEUE_TRANSFER_BIT)) {
            copyQueueFamilyIndex = i;
            transferOnlyQueueFamilyIndex = static_cast<int32_t>(i);
            transferOnlyQueueCount = static_cast<uint32_t>(props[i].queueCount);
            break;
          }
        }
      }
    }
    if (transferOnlyQueueFamilyIndex >= 0) {
      mQueueFamilyIndex = transferOnlyQueueFamilyIndex;
      mCopyQueueCount = transferOnlyQueueCount;
    } else {
      mQueueFamilyIndex = copyQueueFamilyIndex;
    }
    mCopyQueueCount = std::max(1u, mCopyQueueCount);
    mCopyQueue.SetLength(mCopyQueueCount);
    mCopyCmdPool.SetLength(mCopyQueueCount);
    mCopyCmdBuf.SetLength(mCopyQueueCount);
    mCopyFence.SetLength(mCopyQueueCount);
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = mQueueFamilyIndex;
    auto cleanUp = MakeScopeExit([&] { Cleanup(); });
    for (uint32_t qi = 0; qi < mCopyQueueCount; qi++) {
      VkResult poolRes =
          mCreateCommandPool(aDevice, &poolInfo, nullptr, &mCopyCmdPool[qi]);
      if (poolRes != VK_SUCCESS &&
          copyQueueFamilyIndex != aCopyQueueFamilyIndex) {
        copyQueueFamilyIndex = aCopyQueueFamilyIndex;
        mQueueFamilyIndex = copyQueueFamilyIndex;
        poolInfo.queueFamilyIndex = mQueueFamilyIndex;
        poolRes =
            mCreateCommandPool(aDevice, &poolInfo, nullptr, &mCopyCmdPool[qi]);
      }
      if (poolRes != VK_SUCCESS) {
        FFMPEGV_LOG("Failed to create Vulkan command pool for queue {}", qi);
        return false;
      }
      VkDeviceQueueInfo2 queueInfo = {};
      queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2;
      queueInfo.flags = aQueueCreateFlags;
      queueInfo.queueFamilyIndex = mQueueFamilyIndex;
      queueInfo.queueIndex = qi;
      mGetDeviceQueue2(aDevice, &queueInfo, &mCopyQueue[qi]);
      if (mCopyQueue[qi] == VK_NULL_HANDLE) {
        FFMPEGV_LOG(
            "vkGetDeviceQueue2 returned NULL (family={}, index={}, "
            "flags=0x{:x})",
            mQueueFamilyIndex, qi, static_cast<unsigned>(aQueueCreateFlags));
        return false;
      }
      VkCommandBufferAllocateInfo cmdAllocInfo = {};
      cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      cmdAllocInfo.commandPool = mCopyCmdPool[qi];
      cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cmdAllocInfo.commandBufferCount = 1;
      if (mAllocateCommandBuffers(aDevice, &cmdAllocInfo, &mCopyCmdBuf[qi]) !=
          VK_SUCCESS) {
        FFMPEGV_LOG("Failed to allocate Vulkan command buffer for queue {}",
                    qi);
        return false;
      }
      VkFenceCreateInfo fenceInfo = {};
      fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
      if (mCreateFence(aDevice, &fenceInfo, nullptr, &mCopyFence[qi]) !=
          VK_SUCCESS) {
        FFMPEGV_LOG("Failed to create Vulkan copy fence for queue {}", qi);
        return false;
      }
    }
    cleanUp.release();

    NS_WARNING(nsPrintfCString("[VULKAN] Initialized Vulkan Firefox context, "
                               "vkGetMemoryFdKHR=%p\n",
                               (void*)mGetMemoryFdKHR)
                   .get());
  }

  return mDevice != VK_NULL_HANDLE;
}

MediaResult
FFmpegVideoDecoder<LIBAV_VER>::FFmpegVulkanVideoDecoder::InitCopyRingBuffer(
    uint32_t aWidth, uint32_t aHeight, AVPixelFormat aSwFormat,
    AVBufferRef* aVulkanDevCtx) {
  VkPhysicalDevice physDev =
      ((AVVulkanDeviceContext*)((AVHWDeviceContext*)aVulkanDevCtx->data)->hwctx)
          ->phys_dev;

  if (mWidth == aWidth && mHeight == aHeight) {
    return MediaResult(NS_OK);
  }

  for (int buf = 0; buf < kNumBuffers; buf++) {
    if (mNv12BaseFd[buf] >= 0) {
      close(mNv12BaseFd[buf]);
      mNv12BaseFd[buf] = -1;
    }
    if (mNv12Image[buf] != VK_NULL_HANDLE) {
      mDestroyImage(mDevice, mNv12Image[buf], nullptr);
      mNv12Image[buf] = VK_NULL_HANDLE;
    }
    if (mNv12Mem[buf] != VK_NULL_HANDLE) {
      mFreeMemory(mDevice, mNv12Mem[buf], nullptr);
      mNv12Mem[buf] = VK_NULL_HANDLE;
    }
  }

  VkExternalMemoryImageCreateInfo extImgInfo = {};
  extImgInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
  extImgInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

  VkExportMemoryAllocateInfo exportInfo = {};
  exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
  exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

  bool useP010 =
      (aSwFormat == AV_PIX_FMT_P010) || (aSwFormat == AV_PIX_FMT_P016);
#    if LIBAVCODEC_VERSION_MAJOR >= 60
  useP010 = useP010 || (aSwFormat == AV_PIX_FMT_P012);
#    endif
  const VkFormat vkFormat =
      useP010 ? VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16
              : VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;

  VkImageDrmFormatModifierListCreateInfoEXT drmModInfo = {};
  drmModInfo.sType =
      VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT;
  drmModInfo.drmFormatModifierCount = mDrmModifiers.size();
  drmModInfo.pDrmFormatModifiers = mDrmModifiers.data();
  extImgInfo.pNext = &drmModInfo;

  VkPhysicalDeviceMemoryProperties memProps;
  mGetPhysicalDeviceMemoryProperties(physDev, &memProps);

  int buf = 0;

  // Clean up any partially-allocated buffers on failure.
  auto cleanup = mozilla::MakeScopeExit([&] {
    for (int b = 0; b <= buf; b++) {
      if (mNv12BaseFd[b] >= 0) {
        close(mNv12BaseFd[b]);
        mNv12BaseFd[b] = -1;
      }
      if (mNv12Mem[b] != VK_NULL_HANDLE) {
        mFreeMemory(mDevice, mNv12Mem[b], nullptr);
        mNv12Mem[b] = VK_NULL_HANDLE;
      }
      if (mNv12Image[b] != VK_NULL_HANDLE) {
        mDestroyImage(mDevice, mNv12Image[b], nullptr);
        mNv12Image[b] = VK_NULL_HANDLE;
      }
    }
  });

  VkMemoryRequirements memReqs;
  bool useDedicated = true;

  for (buf = 0; buf < kNumBuffers; buf++) {
    VkImageCreateInfo imgInfo = {};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.pNext = &extImgInfo;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = vkFormat;
    imgInfo.extent = {aWidth, aHeight, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    imgInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult res = mCreateImage(mDevice, &imgInfo, nullptr, &mNv12Image[buf]);
    if (res != VK_SUCCESS) {
      NS_WARNING(
          nsPrintfCString(
              "[VULKAN] ERROR: Failed to create NV12 image[%d]: %d\n", buf, res)
              .get());
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         RESULT_DETAIL("Failed to create NV12 image"));
    }

    if (buf == 0 && mGetImageDrmFormatModifierPropertiesEXT) {
      VkImageDrmFormatModifierPropertiesEXT modProps = {};
      modProps.sType =
          VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT;
      if (mGetImageDrmFormatModifierPropertiesEXT(mDevice, mNv12Image[0],
                                                  &modProps) == VK_SUCCESS) {
        mDrmModifier = modProps.drmFormatModifier;
        NS_WARNING(nsPrintfCString("[VULKAN] DRM modifier: 0x%llx\n",
                                   (unsigned long long)mDrmModifier)
                       .get());
      }
    }

    if (buf == 0) {
      if (mGetImageMemoryRequirements2) {
        VkMemoryDedicatedRequirements dedReq = {
            VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
        VkMemoryRequirements2 memReqs2 = {
            VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedReq};
        VkImageMemoryRequirementsInfo2 reqInfo = {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2, nullptr,
            mNv12Image[buf]};
        mGetImageMemoryRequirements2(mDevice, &reqInfo, &memReqs2);
        memReqs = memReqs2.memoryRequirements;
        useDedicated = dedReq.prefersDedicatedAllocation ||
                       dedReq.requiresDedicatedAllocation;
      } else {
        mGetImageMemoryRequirements(mDevice, mNv12Image[buf], &memReqs);
      }
      const auto entry =
          mExportRequiresDedicatedByModifier.Lookup(mDrmModifier);
      const bool exportRequiresDedicated = entry ? entry.Data() : true;
      useDedicated = useDedicated || exportRequiresDedicated;
    }

    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
      if (memReqs.memoryTypeBits & (1 << i)) {
        memTypeIndex = i;
        break;
      }
    }
    if (memTypeIndex == UINT32_MAX) {
      mDestroyImage(mDevice, mNv12Image[buf], nullptr);
      mNv12Image[buf] = VK_NULL_HANDLE;
      return MediaResult(
          NS_ERROR_DOM_MEDIA_FATAL_ERR,
          RESULT_DETAIL("No compatible memory type for NV12 image"));
    }

    VkMemoryDedicatedAllocateInfo dedicatedInfo = {};
    dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicatedInfo.pNext = &exportInfo;
    dedicatedInfo.image = mNv12Image[buf];

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = useDedicated ? (void*)&dedicatedInfo : (void*)&exportInfo;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memTypeIndex;

    res = mAllocateMemory(mDevice, &allocInfo, nullptr, &mNv12Mem[buf]);
    if (res != VK_SUCCESS) {
      mDestroyImage(mDevice, mNv12Image[buf], nullptr);
      mNv12Image[buf] = VK_NULL_HANDLE;
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         RESULT_DETAIL("Failed to alloc NV12 memory"));
    }

    res = mBindImageMemory(mDevice, mNv12Image[buf], mNv12Mem[buf], 0);
    if (res != VK_SUCCESS) {
      mFreeMemory(mDevice, mNv12Mem[buf], nullptr);
      mDestroyImage(mDevice, mNv12Image[buf], nullptr);
      mNv12Mem[buf] = VK_NULL_HANDLE;
      mNv12Image[buf] = VK_NULL_HANDLE;
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         RESULT_DETAIL("Failed to bind NV12 memory"));
    }

    mTotalSize = memReqs.size;

    VkImageSubresource subresY = {VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT, 0, 0};
    VkSubresourceLayout layoutY = {};
    mGetImageSubresourceLayout(mDevice, mNv12Image[buf], &subresY, &layoutY);
    mYPitch = layoutY.rowPitch;

    VkImageSubresource subresUV = {VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT, 0,
                                   0};
    VkSubresourceLayout layoutUV = {};
    mGetImageSubresourceLayout(mDevice, mNv12Image[buf], &subresUV, &layoutUV);
    mUvPitch = layoutUV.rowPitch;
    mUvOffset = layoutUV.offset;

    VkMemoryGetFdInfoKHR fdInfo = {};
    fdInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    fdInfo.memory = mNv12Mem[buf];
    res = mGetMemoryFdKHR(mDevice, &fdInfo, &mNv12BaseFd[buf]);
    if (res != VK_SUCCESS) {
      mFreeMemory(mDevice, mNv12Mem[buf], nullptr);
      mDestroyImage(mDevice, mNv12Image[buf], nullptr);
      mNv12Mem[buf] = VK_NULL_HANDLE;
      mNv12Image[buf] = VK_NULL_HANDLE;
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         RESULT_DETAIL("Failed to export NV12 FD"));
    }

    NS_WARNING(nsPrintfCString("[VULKAN] Created NV12 buffer[%d]: fd=%d", buf,
                               mNv12BaseFd[buf])
                   .get());
  }
  NS_WARNING(nsPrintfCString(
                 "[VULKAN] NV12 images: %ux%u, Y pitch=%u, UV offset=%zu, UV "
                 "pitch=%u, total=%zu, modifier=0x%llx",
                 aWidth, aHeight, mYPitch, mUvOffset, mUvPitch, mTotalSize,
                 (unsigned long long)mDrmModifier)
                 .get());

  mWidth = aWidth;
  mHeight = aHeight;
  mCurrentBuffer = 0;
  cleanup.release();
  return MediaResult(NS_OK);
}

MediaResult
FFmpegVideoDecoder<LIBAV_VER>::FFmpegVulkanVideoDecoder::InitExternalSemaphores(
    AVBufferRef* aVulkanDevCtx) {
  if (!mCreateSemaphore) {
    return MediaResult(NS_OK);
  }
  bool opaqueFdSupported = false;
  if (mGetPhysicalDeviceExternalSemaphoreProperties) {
    auto* devCtx = (AVHWDeviceContext*)aVulkanDevCtx->data;
    auto* vkDevCtx = (AVVulkanDeviceContext*)devCtx->hwctx;
    VkPhysicalDevice physDev = vkDevCtx->phys_dev;
    VkPhysicalDeviceExternalSemaphoreInfo extSemInfo = {};
    extSemInfo.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO;
    extSemInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkExternalSemaphoreProperties extSemProps = {};
    extSemProps.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
    mGetPhysicalDeviceExternalSemaphoreProperties(physDev, &extSemInfo,
                                                  &extSemProps);
    opaqueFdSupported = (extSemProps.compatibleHandleTypes &
                         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT) != 0;
  }
  VkExportSemaphoreCreateInfo exportSemInfo = {};
  exportSemInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
  exportSemInfo.handleTypes =
      (opaqueFdSupported &&
       mDecoderMatchesCompositor)  // &&
                                   // mCompositorSupportsOpaqueFdSemaphore)
          ? VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT
          : VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
  mSemHandleType = exportSemInfo.handleTypes;
  VkSemaphoreTypeCreateInfo semTypeInfo = {};
  semTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  semTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_BINARY;
  semTypeInfo.pNext = &exportSemInfo;
  bool created[kNumBuffers] = {};
  for (int buf = 0; buf < kNumBuffers; buf++) {
    if (mCopyDoneSem[buf] != VK_NULL_HANDLE) {
      continue;
    }
    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semInfo.pNext = &semTypeInfo;
    VkResult res =
        mCreateSemaphore(mDevice, &semInfo, nullptr, &mCopyDoneSem[buf]);
    if (res != VK_SUCCESS) {
      for (int b = 0; b < kNumBuffers; b++) {
        if (created[b] && (mCopyDoneSem[b] != VK_NULL_HANDLE) &&
            mDestroySemaphore) {
          mDestroySemaphore(mDevice, mCopyDoneSem[b], nullptr);
          mCopyDoneSem[b] = VK_NULL_HANDLE;
        }
      }
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         RESULT_DETAIL("Failed to create copyDone semaphore"));
    }
    created[buf] = true;
  }
  return MediaResult(NS_OK);
}

MediaResult
FFmpegVideoDecoder<LIBAV_VER>::FFmpegVulkanVideoDecoder::PrepareImageToDRM(
    AVFrame* aSrcFrame, int* aOutFd, size_t* aOutSize, uint32_t* aOutYPitch,
    uint32_t* aOutUVPitch, size_t* aOutUVOffset, AVBufferRef* aVulkanDevCtx,
    VideoFramePool<LIBAV_VER>* aFramePool, int32_t* aOutBufIdx, bool aIsCopy) {
  uint32_t width = aSrcFrame->width;
  uint32_t height = aSrcFrame->height;
  uint32_t uvWidth = (width + 1) / 2;
  uint32_t uvHeight = (height + 1) / 2;

  AVVkFrame* srcVkFrame = (AVVkFrame*)aSrcFrame->data[0];
  if (!srcVkFrame) {
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("Missing source Vulkan frame"));
  }

  // Lock the frame while accessing it (FFmpeg is threaded)
  AVHWFramesContext* framesCtx =
      (AVHWFramesContext*)aSrcFrame->hw_frames_ctx->data;
  if (framesCtx->sw_format != AV_PIX_FMT_NV12 &&
      framesCtx->sw_format != AV_PIX_FMT_P010 &&
#    if LIBAVCODEC_VERSION_MAJOR >= 60
      framesCtx->sw_format != AV_PIX_FMT_P012 &&
#    endif
      framesCtx->sw_format != AV_PIX_FMT_P016) {
    return MediaResult(
        NS_ERROR_DOM_MEDIA_DECODE_ERR,
        RESULT_DETAIL(
            "Vulkan copy only supports NV12/P010/P012/P016 decode output"));
  }
  AVVulkanFramesContext* vkFramesCtx = (AVVulkanFramesContext*)framesCtx->hwctx;
  vkFramesCtx->lock_frame(framesCtx, srcVkFrame);
  auto unlockGuard =
      MakeScopeExit([&] { vkFramesCtx->unlock_frame(framesCtx, srcVkFrame); });

  if (mWidth != width || mHeight != height) {
    if (aIsCopy) {
      MediaResult initRes = InitCopyRingBuffer(
          width, height, framesCtx->sw_format, aVulkanDevCtx);
      if (NS_FAILED(initRes)) {
        return initRes;
      }
    }
    MediaResult semRes = InitExternalSemaphores(aVulkanDevCtx);
    if (NS_FAILED(semRes)) {
      return semRes;
    }
  }

  int bufIdx = -1;
  int i = 0;
  int retries = 0;
  constexpr int kMaxRetries = 1000;  // 1000 × 1 ms ≈ 1 s
  do {
    if (!aFramePool->IsVulkanFrameSlotInUseByRenderer(i)) {
      bufIdx = i;
      break;
    }
    if (++i >= kNumBuffers) {
      i = 0;
      if (retries++ >= kMaxRetries) {
        FFMPEGV_LOG("No free Vulkan frame copy slot after {} retries",
                    kMaxRetries);
        return NS_ERROR_DOM_MEDIA_DECODE_ERR;
      }
      PR_Sleep(PR_MillisecondsToInterval(1));
    }
  } while (bufIdx < 0);

  const uint32_t copySlot =
      aIsCopy ? (mCopyQueueRoundRobin++ % mCopyQueueCount) : 0;
  if (aIsCopy) {
    const uint64_t kFenceWaitNs = 100 * 1000 * 1000;  // 100 ms
    VkResult waitRes = mWaitForFences(mDevice, 1, &mCopyFence[copySlot],
                                      VK_TRUE, kFenceWaitNs);
    if (waitRes == VK_TIMEOUT) {
      NS_WARNING(
          "[VULKAN] Copy fence wait timed out; previous copy may be stuck.");
      return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                         RESULT_DETAIL("Vulkan copy fence wait timed out"));
    }
    if (waitRes != VK_SUCCESS) {
      return MediaResult(
          NS_ERROR_DOM_MEDIA_DECODE_ERR,
          RESULT_DETAIL("Vulkan waitForFences failed: %d", waitRes));
    }
    mResetFences(mDevice, 1, &mCopyFence[copySlot]);
  }

  if (aIsCopy) {
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    mBeginCommandBuffer(mCopyCmdBuf[copySlot], &beginInfo);

    // Barrier for source and destination (both NV12 multi-planar).
    // Use one barrier per plane so aspectMask has a single bit (avoids driver
    // bug with combined PLANE_0|PLANE_1 in layout transition/clear paths).
    VkImageMemoryBarrier barriers[4] = {};
    for (auto& barrier : barriers) {
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
          VK_QUEUE_FAMILY_IGNORED;
    }

    // Source image: plane 0
    barriers[0].image = srcVkFrame->img[0];
    barriers[0].oldLayout = (VkImageLayout)srcVkFrame->layout[0];
    barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].srcAccessMask =
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 1, 0, 1};

    // Source image: plane 1
    barriers[1].image = srcVkFrame->img[0];
    barriers[1].oldLayout = (VkImageLayout)srcVkFrame->layout[0];
    barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[1].srcAccessMask =
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[1].subresourceRange = {VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 1, 0, 1};

    // Destination NV12 image: plane 0
    barriers[2].image = mNv12Image[bufIdx];
    barriers[2].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[2].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[2].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[2].subresourceRange = {VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 1, 0, 1};

    // Destination NV12 image: plane 1
    barriers[3].image = mNv12Image[bufIdx];
    barriers[3].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[3].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[3].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[3].subresourceRange = {VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 1, 0, 1};

    mCmdPipelineBarrier(
        mCopyCmdBuf[copySlot], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 4, barriers);

    // Copy Y plane: src PLANE_0 -> dst PLANE_0
    VkImageCopy yRegion = {};
    yRegion.srcSubresource = {VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 0, 1};
    yRegion.dstSubresource = {VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 0, 1};
    yRegion.extent = {width, height, 1};
    mCmdCopyImage(mCopyCmdBuf[copySlot], srcVkFrame->img[0],
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mNv12Image[bufIdx],
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &yRegion);

    // Copy UV plane: src PLANE_1 -> dst PLANE_1
    VkImageCopy uvRegion = {};
    uvRegion.srcSubresource = {VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 0, 1};
    uvRegion.dstSubresource = {VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 0, 1};
    uvRegion.extent = {uvWidth, uvHeight, 1};
    mCmdCopyImage(mCopyCmdBuf[copySlot], srcVkFrame->img[0],
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mNv12Image[bufIdx],
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &uvRegion);

    const VkImageLayout srcRestoreLayout = (VkImageLayout)srcVkFrame->layout[0];
    barriers[0].image = srcVkFrame->img[0];
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].newLayout = srcRestoreLayout;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].dstAccessMask =
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 1, 0, 1};

    barriers[1].image = srcVkFrame->img[0];
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[1].newLayout = srcRestoreLayout;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[1].dstAccessMask =
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barriers[1].subresourceRange = {VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 1, 0, 1};

    mCmdPipelineBarrier(mCopyCmdBuf[copySlot], VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                        nullptr, 2, barriers);

    barriers[0].image = mNv12Image[bufIdx];
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[0].dstAccessMask = 0;
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 1, 0, 1};

    barriers[1].image = mNv12Image[bufIdx];
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = 0;
    barriers[1].subresourceRange = {VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 1, 0, 1};

    mCmdPipelineBarrier(mCopyCmdBuf[copySlot], VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                        nullptr, 2, barriers);

    mEndCommandBuffer(mCopyCmdBuf[copySlot]);
  }

  const uint64_t decodeWaitValue = srcVkFrame->sem_value[0];
  const uint64_t decodeSignalValue = decodeWaitValue + 1;
  srcVkFrame->sem_value[0] = decodeSignalValue;

  // copyDoneSem is binary (we only signal it). The compositor waits on it (in
  // MaybeSemaphoreWait when Lock()ing the texture) before using the buffer;
  // when the slot is released the compositor is done with it and we can reuse.
  // We never wait on it here (VUID-vkQueueSubmit-pWaitSemaphores-03238).
  // We only wait on the decode timeline semaphore when present.
  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkSemaphore signalSems[2] = {mCopyDoneSem[bufIdx], srcVkFrame->sem[0]};
  uint64_t signalValues[2] = {0, decodeSignalValue};  // binary value ignored
  uint32_t signalCount = 1;
  uint32_t waitCount = 0;
  const VkSemaphore* pWaitSemaphores = &srcVkFrame->sem[0];
  const uint64_t* pWaitValues = &decodeWaitValue;
  const VkPipelineStageFlags* pWaitStages = &waitStage;

  if (srcVkFrame->sem[0] != VK_NULL_HANDLE && srcVkFrame->sem_value[0] > 0) {
    signalCount = 2;
    waitCount = 1;
  }

  VkTimelineSemaphoreSubmitInfo timelineInfo = {};
  timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
  timelineInfo.waitSemaphoreValueCount = waitCount;
  timelineInfo.pWaitSemaphoreValues = pWaitValues;
  timelineInfo.signalSemaphoreValueCount = signalCount;
  timelineInfo.pSignalSemaphoreValues = signalValues;

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.pNext = &timelineInfo;
  submitInfo.waitSemaphoreCount = waitCount;
  submitInfo.pWaitSemaphores = pWaitSemaphores;
  submitInfo.pWaitDstStageMask = pWaitStages;
  submitInfo.signalSemaphoreCount = signalCount;
  submitInfo.pSignalSemaphores = signalSems;
  submitInfo.commandBufferCount = aIsCopy ? 1u : 0u;
  submitInfo.pCommandBuffers = &mCopyCmdBuf[copySlot];

  AVHWDeviceContext* devCtx = (AVHWDeviceContext*)aVulkanDevCtx->data;
  AVVulkanDeviceContext* vkCtx = (AVVulkanDeviceContext*)devCtx->hwctx;
#    if !defined(FF_API_VULKAN_SYNC_QUEUES) || FF_API_VULKAN_SYNC_QUEUES
  const uint32_t qf = static_cast<uint32_t>(mQueueFamilyIndex);
  vkCtx->lock_queue(devCtx, qf, copySlot);
#    endif
  VkResult submitRes =
      mQueueSubmit(mCopyQueue[copySlot], 1, &submitInfo, mCopyFence[copySlot]);
#    if !defined(FF_API_VULKAN_SYNC_QUEUES) || FF_API_VULKAN_SYNC_QUEUES
  vkCtx->unlock_queue(devCtx, qf, copySlot);
#    endif

  if (submitRes != VK_SUCCESS) {
    NS_WARNING(
        nsPrintfCString("[VULKAN] ERROR: queueSubmit failed: %d", submitRes)
            .get());
    return MediaResult(
        NS_ERROR_DOM_MEDIA_DECODE_ERR,
        RESULT_DETAIL("Vulkan queue submit failed: %d", submitRes));
  }
  // Export with the handle type chosen at semaphore creation (Vulkan physical
  // mDevice OPAQUE_FD support + same mDevice; no compositor dependency).
  if (mGetSemaphoreFdKHR) {
    if (mCopyDoneSemFd[bufIdx] >= 0) {
      close(mCopyDoneSemFd[bufIdx]);
      mCopyDoneSemFd[bufIdx] = -1;
    }
    VkSemaphoreGetFdInfoKHR fdInfo = {};
    fdInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
    fdInfo.semaphore = mCopyDoneSem[bufIdx];
    fdInfo.handleType = (VkExternalSemaphoreHandleTypeFlagBits)mSemHandleType;
    if (mGetSemaphoreFdKHR(mDevice, &fdInfo, &mCopyDoneSemFd[bufIdx]) !=
        VK_SUCCESS) {
      mCopyDoneSemFd[bufIdx] = -1;
    }
  }

  // Do not block decoder on vkWaitSemaphores. The copy-done semaphore is
  // exported to an fd and passed to the surface; the compositor waits on it in
  // MaybeSemaphoreWait when using the buffer.

  if (aIsCopy) {
    *aOutFd = mNv12BaseFd[bufIdx];
    *aOutSize = mTotalSize;
    *aOutYPitch = mYPitch;
    *aOutUVPitch = mUvPitch;
    *aOutUVOffset = mUvOffset;
  }

  if (aOutBufIdx) {
    *aOutBufIdx = bufIdx;
  }

  return NS_OK;
}

#  endif  // LIBAVCODEC_VERSION_MAJOR >= 60 && !defined(FFVPX_VERSION)
#endif    // defined(MOZ_USE_HWDECODE) && defined(MOZ_WIDGET_GTK)

}  // namespace mozilla
