/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FFmpegVideoDecoder.h"

#include "EncoderConfig.h"
#include "FFmpegLibWrapper.h"
#include "FFmpegLog.h"
#include "FFmpegUtils.h"
#include "FFmpegVideoUtils.h"
#include "ImageContainer.h"
#include "MP4Decoder.h"
#include "MediaInfo.h"
#include "VALibWrapper.h"
#include "VPXDecoder.h"
#include "VideoUtils.h"
#if LIBAVCODEC_VERSION_MAJOR >= 58
#  include "libavutil/buffer.h"
#  include "libavutil/frame.h"
#  include "libavutil/hwcontext.h"
#  include "libavutil/pixfmt.h"
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 60 && !defined(FFVPX_VERSION)
#  include "libavutil/hwcontext_vulkan.h"
#  include "libavutil/macros.h"
#  include "libavutil/version.h"
#endif

#include <string.h>
#ifdef XP_UNIX
#  include <unistd.h>
#endif

#include <algorithm>

#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/KnowsCompositor.h"
#include "nsPrintfCString.h"
#if LIBAVCODEC_VERSION_MAJOR >= 57
#  include "mozilla/layers/TextureClient.h"
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 58
#  include "mozilla/ProfilerMarkers.h"
#endif
#ifdef MOZ_USE_HWDECODE
#  include "H264.h"
#  include "H265.h"
#endif
#if defined(MOZ_USE_HWDECODE) && defined(MOZ_WIDGET_GTK)
#  if LIBAVCODEC_VERSION_MAJOR >= 60 && !defined(FFVPX_VERSION)
// DMABufDevice defines its own version of this which collides with the
// official version in drm_fourcc.h
#    ifdef DRM_FORMAT_MOD_INVALID
#      undef DRM_FORMAT_MOD_INVALID
#    endif
#    include <libdrm/drm_fourcc.h>
#    ifndef DRM_FORMAT_MOD_INVALID
#      define DRM_FORMAT_MOD_INVALID ((1ULL << 56) - 1)
#    endif
#  endif

#  include "FFmpegVideoFramePool.h"
#  include "mozilla/layers/DMABUFSurfaceImage.h"
#  include "va/va.h"
#endif

#if defined(FFVPX_VERSION) || LIBAVCODEC_VERSION_MAJOR >= 59
#  define FFMPEG_AV1_DECODE 1
#  include "AOMDecoder.h"
#endif

#if LIBAVCODEC_VERSION_MAJOR < 54
#  define AV_PIX_FMT_YUV420P PIX_FMT_YUV420P
#  define AV_PIX_FMT_YUVJ420P PIX_FMT_YUVJ420P
#  define AV_PIX_FMT_YUV420P10LE PIX_FMT_YUV420P10LE
#  define AV_PIX_FMT_YUV422P PIX_FMT_YUV422P
#  define AV_PIX_FMT_YUV422P10LE PIX_FMT_YUV422P10LE
#  define AV_PIX_FMT_YUV444P PIX_FMT_YUV444P
#  define AV_PIX_FMT_YUVJ444P PIX_FMT_YUVJ444P
#  define AV_PIX_FMT_YUV444P10LE PIX_FMT_YUV444P10LE
#  define AV_PIX_FMT_GBRP PIX_FMT_GBRP
#  define AV_PIX_FMT_GBRP10LE PIX_FMT_GBRP10LE
#  define AV_PIX_FMT_NONE PIX_FMT_NONE
#  define AV_PIX_FMT_VAAPI_VLD PIX_FMT_VAAPI_VLD
#endif
#if LIBAVCODEC_VERSION_MAJOR > 58
#  define AV_PIX_FMT_VAAPI_VLD AV_PIX_FMT_VAAPI
#endif
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/TaskQueue.h"
#include "nsThreadUtils.h"
#include "prsystem.h"

#ifdef XP_WIN
#  include "mozilla/gfx/DeviceManagerDx.h"
#endif

#ifdef MOZ_ENABLE_D3D11VA
#  include "D3D11TextureWrapper.h"
#  include "DXVA2Manager.h"
#  include "ffvpx/hwcontext_d3d11va.h"
#endif

#ifdef MOZ_WIDGET_ANDROID
#  include "ffvpx/hwcontext_mediacodec.h"
#  include "ffvpx/mediacodec.h"
#  include "mozilla/java/CodecProxyWrappers.h"
#  include "mozilla/java/GeckoSurfaceWrappers.h"
#  include "mozilla/java/SampleBufferWrappers.h"
#  include "mozilla/java/SampleWrappers.h"
#  include "mozilla/java/SurfaceAllocatorWrappers.h"
#  include "mozilla/layers/AndroidImageReader.h"
#  include "mozilla/layers/TextureClientOGL.h"
#endif

#if defined(MOZ_WIDGET_ANDROID) && defined(FFVPX_VERSION)
#  include "AnnexB.h"
#  include "mozilla/MediaDrmRemoteCDMParent.h"
#endif

#if defined(MOZ_USE_HWDECODE) && defined(MOZ_WIDGET_GTK)
// Forward declare from va.h
typedef int VAStatus;
#  define VA_EXPORT_SURFACE_READ_ONLY 0x0001
#  define VA_EXPORT_SURFACE_SEPARATE_LAYERS 0x0004
#  define VA_STATUS_SUCCESS 0x00000000
#endif

// Use some extra HW frames for potential rendering lags.
// AV1 and VP9 can have maximum 8 frames for reference frames, so 1 base + 8
// references.
#define EXTRA_HW_FRAMES 9

#if LIBAVCODEC_VERSION_MAJOR >= 57 && LIBAVUTIL_VERSION_MAJOR >= 56
#  define CUSTOMIZED_BUFFER_ALLOCATION 1
#endif

#define AV_LOG_DEBUG 48

typedef mozilla::layers::Image Image;
typedef mozilla::layers::PlanarYCbCrImage PlanarYCbCrImage;
typedef mozilla::layers::BufferRecycleBin BufferRecycleBin;

namespace mozilla {

#if defined(MOZ_USE_HWDECODE) && defined(MOZ_WIDGET_GTK)
constinit nsTArray<AVCodecID>
    FFmpegVideoDecoder<LIBAV_VER>::mAcceleratedFormats;
#endif

using media::TimeUnit;

/**
 * FFmpeg calls back to this function with a list of pixel formats it supports.
 * We choose a pixel format that we support and return it.
 * For now, we just look for YUV420P, YUVJ420P, YUV444 and YUVJ444 as
 * those are the only non-HW accelerated format supported by FFmpeg's H264 and
 * VP9 decoder.
 */
static AVPixelFormat ChoosePixelFormat(AVCodecContext* aCodecContext,
                                       const AVPixelFormat* aFormats) {
  FFMPEGV_LOG("Choosing FFmpeg pixel format for video decoding.");
  for (; *aFormats > -1; aFormats++) {
    switch (*aFormats) {
      case AV_PIX_FMT_YUV420P:
        FFMPEGV_LOG("Requesting pixel format YUV420P.");
        return AV_PIX_FMT_YUV420P;
      case AV_PIX_FMT_YUVJ420P:
        FFMPEGV_LOG("Requesting pixel format YUVJ420P.");
        return AV_PIX_FMT_YUVJ420P;
      case AV_PIX_FMT_YUV420P10LE:
        FFMPEGV_LOG("Requesting pixel format YUV420P10LE.");
        return AV_PIX_FMT_YUV420P10LE;
      case AV_PIX_FMT_YUV422P:
        FFMPEGV_LOG("Requesting pixel format YUV422P.");
        return AV_PIX_FMT_YUV422P;
      case AV_PIX_FMT_YUV422P10LE:
        FFMPEGV_LOG("Requesting pixel format YUV422P10LE.");
        return AV_PIX_FMT_YUV422P10LE;
      case AV_PIX_FMT_YUV444P:
        FFMPEGV_LOG("Requesting pixel format YUV444P.");
        return AV_PIX_FMT_YUV444P;
      case AV_PIX_FMT_YUVJ444P:
        FFMPEGV_LOG("Requesting pixel format YUVJ444P.");
        return AV_PIX_FMT_YUVJ444P;
      case AV_PIX_FMT_YUV444P10LE:
        FFMPEGV_LOG("Requesting pixel format YUV444P10LE.");
        return AV_PIX_FMT_YUV444P10LE;
#if LIBAVCODEC_VERSION_MAJOR >= 57
      case AV_PIX_FMT_YUV420P12LE:
        FFMPEGV_LOG("Requesting pixel format YUV420P12LE.");
        return AV_PIX_FMT_YUV420P12LE;
      case AV_PIX_FMT_YUV422P12LE:
        FFMPEGV_LOG("Requesting pixel format YUV422P12LE.");
        return AV_PIX_FMT_YUV422P12LE;
      case AV_PIX_FMT_YUV444P12LE:
        FFMPEGV_LOG("Requesting pixel format YUV444P12LE.");
        return AV_PIX_FMT_YUV444P12LE;
#endif
      case AV_PIX_FMT_GBRP:
        FFMPEGV_LOG("Requesting pixel format GBRP.");
        return AV_PIX_FMT_GBRP;
      case AV_PIX_FMT_GBRP10LE:
        FFMPEGV_LOG("Requesting pixel format GBRP10LE.");
        return AV_PIX_FMT_GBRP10LE;
      default:
        break;
    }
  }

  NS_WARNING("FFmpeg does not share any supported pixel formats.");
  return AV_PIX_FMT_NONE;
}

#ifdef MOZ_USE_HWDECODE
static AVPixelFormat ChooseVAAPIPixelFormat(AVCodecContext* aCodecContext,
                                            const AVPixelFormat* aFormats) {
  FFMPEGV_LOG("Choosing FFmpeg pixel format for VA-API video decoding.");
  for (; *aFormats > -1; aFormats++) {
    switch (*aFormats) {
      case AV_PIX_FMT_VAAPI_VLD:
        FFMPEGV_LOG("Requesting pixel format VAAPI_VLD");
        return AV_PIX_FMT_VAAPI_VLD;
      default:
        break;
    }
  }
  NS_WARNING("FFmpeg does not share any supported pixel formats.");
  return AV_PIX_FMT_NONE;
}

static AVPixelFormat ChooseV4L2PixelFormat(AVCodecContext* aCodecContext,
                                           const AVPixelFormat* aFormats) {
  FFMPEGV_LOG("Choosing FFmpeg pixel format for V4L2 video decoding.");
  for (; *aFormats > -1; aFormats++) {
    switch (*aFormats) {
      case AV_PIX_FMT_DRM_PRIME:
        FFMPEGV_LOG("Requesting pixel format DRM PRIME");
        return AV_PIX_FMT_DRM_PRIME;
      default:
        break;
    }
  }
  NS_WARNING("FFmpeg does not share any supported V4L2 pixel formats.");
  return AV_PIX_FMT_NONE;
}

#  if LIBAVCODEC_VERSION_MAJOR >= 60 && !defined(FFVPX_VERSION)
static bool VulkanDirectDecodeExportEnabled() {
  return StaticPrefs::
      media_hardware_video_decoding_vulkan_direct_export_enabled_AtStartup();
}

static AVPixelFormat ChooseVulkanPixelFormat(AVCodecContext* aCodecContext,
                                             const AVPixelFormat* aFormats) {
  auto* decoder =
      static_cast<FFmpegVideoDecoder<LIBAV_VER>*>(aCodecContext->opaque);
  if (decoder) {
    return (AVPixelFormat)decoder->ChooseVulkanPixelFormatFromContext(
        aCodecContext, (const int*)aFormats);
  }
  return AV_PIX_FMT_NONE;
}
#  endif

static AVPixelFormat ChooseD3D11VAPixelFormat(AVCodecContext* aCodecContext,
                                              const AVPixelFormat* aFormats) {
#  ifdef MOZ_ENABLE_D3D11VA
  FFMPEGV_LOG("Choosing FFmpeg pixel format for D3D11VA video decoding {}. ",
              static_cast<int>(*aFormats));
  for (; *aFormats > -1; aFormats++) {
    switch (*aFormats) {
      case AV_PIX_FMT_D3D11:
        FFMPEGV_LOG("Requesting pixel format D3D11");
        return AV_PIX_FMT_D3D11;
      default:
        break;
    }
  }
  NS_WARNING("FFmpeg does not share any supported D3D11 pixel formats.");
#  endif  // MOZ_ENABLE_D3D11VA
  return AV_PIX_FMT_NONE;
}
#endif

#ifdef MOZ_USE_HWDECODE
static AVPixelFormat ChooseMediaCodecPixelFormat(
    AVCodecContext* aCodecContext, const AVPixelFormat* aFormats) {
#  ifdef MOZ_WIDGET_ANDROID
  FFMPEGV_LOG("Choosing FFmpeg pixel format for MediaCodec video decoding {}. ",
              static_cast<int>(*aFormats));
  for (; *aFormats > -1; aFormats++) {
    switch (*aFormats) {
      case AV_PIX_FMT_MEDIACODEC:
        FFMPEGV_LOG("Requesting pixel format MediaCodec");
        return AV_PIX_FMT_MEDIACODEC;
      default:
        break;
    }
  }
  NS_WARNING("FFmpeg does not share any supported MediaCodec pixel formats.");
#  endif  // MOZ_WIDGET_ANDROID
  return AV_PIX_FMT_NONE;
}
#endif

#if defined(MOZ_USE_HWDECODE) && defined(MOZ_WIDGET_GTK)
static void VAAPIDisplayReleaseCallback(struct AVHWDeviceContext* hwctx) {
  auto displayHolder = static_cast<VADisplayHolder*>(hwctx->user_opaque);
  displayHolder->Release();
}

bool FFmpegVideoDecoder<LIBAV_VER>::CreateVAAPIDeviceContext() {
  mVAAPIDeviceContext = mLib->av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
  if (!mVAAPIDeviceContext) {
    FFMPEG_LOG("  av_hwdevice_ctx_alloc failed.");
    return false;
  }

  auto releaseVAAPIcontext =
      MakeScopeExit([&] { mLib->av_buffer_unref(&mVAAPIDeviceContext); });

  AVHWDeviceContext* hwctx = (AVHWDeviceContext*)mVAAPIDeviceContext->data;
  AVVAAPIDeviceContext* vactx = (AVVAAPIDeviceContext*)hwctx->hwctx;

  RefPtr displayHolder = VADisplayHolder::GetSingleton();
  if (!displayHolder) {
    return false;
  }

  mDisplay = displayHolder->Display();
  hwctx->user_opaque = displayHolder.forget().take();
  hwctx->free = VAAPIDisplayReleaseCallback;

  vactx->display = mDisplay;
  if (mLib->av_hwdevice_ctx_init(mVAAPIDeviceContext) < 0) {
    FFMPEG_LOG("  av_hwdevice_ctx_init failed.");
    return false;
  }

  mCodecContext->hw_device_ctx = mLib->av_buffer_ref(mVAAPIDeviceContext);
  releaseVAAPIcontext.release();
  return true;
}

#  if LIBAVCODEC_VERSION_MAJOR >= 60 && !defined(FFVPX_VERSION)
static uint32_t VulkanTransferQueueFamily(const AVVulkanDeviceContext* aVkCtx) {
#    if LIBAVCODEC_VERSION_MAJOR >= 63
  // FFmpeg 63 replaced queue_family_tx_index with the qf array.
  for (int i = 0; i < aVkCtx->nb_qf; i++) {
    if (aVkCtx->qf[i].flags & VK_QUEUE_TRANSFER_BIT) {
      return (uint32_t)std::max(aVkCtx->qf[i].idx, 0);
    }
  }
  return 0;
#    else
  return (uint32_t)std::max<int>(aVkCtx->queue_family_tx_index, 0);
#    endif
}

bool FFmpegVideoDecoder<LIBAV_VER>::CreateVulkanDeviceContext(
    const StaticMutexAutoLock& aProofOfLock) {
  nsAutoCString rendererNode(gfx::gfxVars::DrmRenderDevice());
  if (!mVulkanDecoder.SelectVulkanDecoderPhysicalDevice(aProofOfLock,
                                                        rendererNode)) {
    FFMPEG_LOG("Failed to select Vulkan decoder physical device");
    return false;
  }

  const char* device_extensions =
      "VK_KHR_timeline_semaphore+"
      "VK_KHR_external_memory_fd+"
      "VK_EXT_external_memory_dma_buf+"
      "VK_EXT_image_drm_format_modifier+"
      "VK_KHR_video_queue+"
      "VK_KHR_video_decode_queue+"
      "VK_KHR_video_decode_h264+"
      "VK_KHR_video_decode_h265+"
#    if LIBAVCODEC_VERSION_MAJOR >= 62
      "VK_KHR_video_decode_vp9+"
#    endif
#    if defined(FF_API_VULKAN_SYNC_QUEUES) && !FF_API_VULKAN_SYNC_QUEUES
      "VK_KHR_internally_synchronized_queues+"
#    endif
      "VK_KHR_video_decode_av1";

  // Share one VkDevice across all decoders in the process to avoid paying
  // vkCreateDevice per stream.
  mVulkanDeviceHolder = VulkanDeviceHolder::GetOrCreate(
      mLib, mVulkanDecoder.mNegotiatedVulkanDeviceName, device_extensions);
  if (!mVulkanDeviceHolder) {
    FFMPEG_LOG("VulkanDeviceHolder::GetOrCreate failed for {}",
               mVulkanDecoder.mNegotiatedVulkanDeviceName);
    return false;
  }

  mVulkanDeviceContext = mVulkanDeviceHolder->Ref();
  mCodecContext->hw_device_ctx = mVulkanDeviceHolder->Ref();

  AVHWDeviceContext* devCtx = (AVHWDeviceContext*)mVulkanDeviceContext->data;
  AVVulkanDeviceContext* vkCtx = (AVVulkanDeviceContext*)devCtx->hwctx;
  mVulkanDecoder.LoadInstanceFunctions(vkCtx->get_proc_addr, vkCtx->inst,
                                       vkCtx->phys_dev,
                                       mVulkanDeviceHolder->Generation());

  // Some drivers (e.g. experimental RADV video decode) fail to load Vulkan
  // device functions. Check that up front, so a failure is a normal Init()
  // failure the PDM can fall back from instead of a fatal error later.
  // Pass queue_flags here (same as CreateImageVulkan): InitCtx only fetches
  // queues when the device changes, so a first call with flags=0 would stick.
  VkDeviceQueueCreateFlags queueCreateFlags = 0;
#    if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(60, 32, 100)
  queueCreateFlags = vkCtx->queue_flags;
#    endif
  if (!mVulkanDecoder.InitCtx(
          vkCtx->act_dev, vkCtx->phys_dev, vkCtx->get_proc_addr, vkCtx->inst,
          mVulkanDeviceHolder->Generation(), VulkanTransferQueueFamily(vkCtx),
          queueCreateFlags)) {
    FFMPEG_LOG("Failed to init Vulkan Context structure");
    return false;
  }

  return true;
}

int FFmpegVideoDecoder<LIBAV_VER>::ChooseVulkanPixelFormatFromContext(
    struct AVCodecContext* aCodecContext, const int* aFormats) {
  FFMPEGV_LOG("Choosing FFmpeg pixel format for Vulkan video decoding.");
  for (; *aFormats > -1; aFormats++) {
    if (*aFormats != AV_PIX_FMT_VULKAN) {
      continue;
    }
    if (!mLib->avcodec_get_hw_frames_parameters) {
      FFMPEGV_LOG("Requesting pixel format VULKAN (no hw_frames_parameters)");
      return AV_PIX_FMT_VULKAN;
    }
    AVBufferRef* frames_ref = nullptr;
    int ret = mLib->avcodec_get_hw_frames_parameters(
        aCodecContext, aCodecContext->hw_device_ctx, AV_PIX_FMT_VULKAN,
        &frames_ref);
    if (ret < 0 || !frames_ref) {
      if (frames_ref) {
        mLib->av_buffer_unref(&frames_ref);
      }
      FFMPEGV_LOG(
          "Requesting pixel format VULKAN (get_hw_frames_parameters failed)");
      return AV_PIX_FMT_VULKAN;
    }
    AVHWFramesContext* frames_ctx = (AVHWFramesContext*)frames_ref->data;

    VkImageUsageFlags imageUsages =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (VulkanDirectDecodeExportEnabled()) {
      imageUsages |= VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
      imageUsages |= VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
    }
    PrepareVulkanDrmModifiersForSwFormat(frames_ctx->sw_format, imageUsages);
    bool drmModsAreLinearOrEmpty = true;
    if (!mVulkanDecoder.mDrmModifiers.empty()) {
      drmModsAreLinearOrEmpty =
          (mVulkanDecoder.mDrmModifiers[0] == DRM_FORMAT_MOD_LINEAR) &&
          (mVulkanDecoder.mDrmModifiers.size() == 1);
    }
    // Forced BL means ImageFormatProperties2 rejected YCbCr tiled modifiers, so
    // the decode image cannot reliably use DRM-modifier tiling. Keep BL only
    // for the copy-path export buffers; skip direct export (avoids GL import
    // hangs and a useless LINEAR decode path that would double-copy).
    if (VulkanDirectDecodeExportEnabled() &&
        mVulkanDecoder.mForcedNvidiaBlockLinear) {
      FFMPEGV_LOG(
          "[VULKAN] Direct export disabled: forced NVIDIA BL after "
          "ImageFormatProperties2 left only LINEAR");
    }
    if (VulkanDirectDecodeExportEnabled() &&
        !mVulkanDecoder.mForcedNvidiaBlockLinear && !drmModsAreLinearOrEmpty) {
      AVVulkanFramesContext* hwfc = (AVVulkanFramesContext*)frames_ctx->hwctx;
      void* const originalCreatePnext = hwfc->create_pnext;
      int formatCount = 0;
      while (formatCount < AV_NUM_DATA_POINTERS &&
             hwfc->format[formatCount] != VK_FORMAT_UNDEFINED) {
        formatCount++;
      }
      mVulkanImageFormatList.sType =
          VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
      mVulkanImageFormatList.pNext = originalCreatePnext;
      mVulkanImageFormatList.viewFormatCount = formatCount;
      mVulkanImageFormatList.pViewFormats = hwfc->format;
      mVulkanDrmModifierList.sType =
          VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT;
      mVulkanDrmModifierList.pNext = &mVulkanImageFormatList;
      mVulkanDrmModifierList.drmFormatModifierCount =
          mVulkanDecoder.mDrmModifiers.size();
      mVulkanDrmModifierList.pDrmFormatModifiers =
          mVulkanDecoder.mDrmModifiers.data();
      hwfc->create_pnext = &mVulkanDrmModifierList;
      hwfc->nb_layers = 1;
      hwfc->tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
      hwfc->usage = (VkImageUsageFlagBits)imageUsages;
      constexpr int chroma_align = 2;
      frames_ctx->width = FFALIGN(aCodecContext->coded_width, chroma_align);
      frames_ctx->height = FFALIGN(aCodecContext->coded_height, chroma_align);
      frames_ctx->initial_pool_size = FFmpegVulkanVideoDecoder::kNumBuffers;
    }
    if (mLib->av_hwframe_ctx_init(frames_ref) >= 0) {
      aCodecContext->hw_frames_ctx = frames_ref;
      FFMPEGV_LOG(
          "Requesting pixel format VULKAN (pool from "
          "get_hw_frames_parameters)");
      return AV_PIX_FMT_VULKAN;
    }
    mLib->av_buffer_unref(&frames_ref);
    FFMPEGV_LOG("Requesting pixel format VULKAN (hwframe_ctx_init failed)");
    return AV_PIX_FMT_VULKAN;
  }
  NS_WARNING("FFmpeg does not share any supported Vulkan pixel formats.");
  return AV_PIX_FMT_NONE;
}

void FFmpegVideoDecoder<LIBAV_VER>::PrepareVulkanDrmModifiersForSwFormat(
    int aSwFormat, VkImageUsageFlags aImageUsages) {
  if (!mVulkanDeviceContext || !mImageAllocator) {
    return;
  }
  AVHWDeviceContext* devCtx = (AVHWDeviceContext*)mVulkanDeviceContext->data;
  AVVulkanDeviceContext* vkCtx = (AVVulkanDeviceContext*)devCtx->hwctx;
  bool useP010 =
      (aSwFormat == AV_PIX_FMT_P010) || (aSwFormat == AV_PIX_FMT_P016);
#    if LIBAVCODEC_VERSION_MAJOR >= 60
  useP010 = useP010 || (aSwFormat == AV_PIX_FMT_P012);
#    endif
  VkFormat vkFormat = useP010
                          ? VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16
                          : VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
  const int32_t drmFormat = useP010 ? DRM_FORMAT_P010 : DRM_FORMAT_NV12;
  const nsTArray<uint64_t>* compositorMods = nullptr;
  if (auto* globalFormats = widget::GetGlobalDMABufFormats()) {
    if (widget::DRMFormat* format = globalFormats->GetDRMFormat(drmFormat)) {
      compositorMods = format->GetModifiers();
    }
  }
  mVulkanDecoder.InitDrmModifiers(vkCtx->phys_dev, vkFormat, compositorMods,
                                  aImageUsages);
}

#  endif

void FFmpegVideoDecoder<LIBAV_VER>::AdjustHWDecodeLogging() {
  if (!getenv("LIBVA_MESSAGING_LEVEL")) {
    if (MOZ_LOG_TEST(sFFmpegVideoLog, LogLevel::Debug)) {
      setenv("LIBVA_MESSAGING_LEVEL", "1", false);
    } else if (MOZ_LOG_TEST(sFFmpegVideoLog, LogLevel::Info)) {
      setenv("LIBVA_MESSAGING_LEVEL", "2", false);
    } else {
      setenv("LIBVA_MESSAGING_LEVEL", "0", false);
    }
  }
}

MediaResult FFmpegVideoDecoder<LIBAV_VER>::InitVAAPIDecoder() {
  FFMPEG_LOG("Initialising VA-API FFmpeg decoder");

  StaticMutexAutoLock mon(sMutex);

  // mAcceleratedFormats is already configured so check supported
  // formats before we do anything.
  if (mAcceleratedFormats.Length()) {
    if (!IsFormatAccelerated(mCodecID)) {
      FFMPEG_LOG("  Format {} is not accelerated",
                 mLib->avcodec_get_name(mCodecID));
      return NS_ERROR_NOT_AVAILABLE;
    } else {
      FFMPEG_LOG("  Format {} is accelerated",
                 mLib->avcodec_get_name(mCodecID));
    }
  }

  if (!mLib->IsVAAPIAvailable()) {
    FFMPEG_LOG("  libva library or symbols are missing.");
    return NS_ERROR_NOT_AVAILABLE;
  }

  AVCodec* codec =
      FindVideoHardwareAVCodec(mLib, mCodecID, AV_HWDEVICE_TYPE_VAAPI);
  if (!codec) {
    FFMPEG_LOG("  couldn't find ffmpeg VA-API decoder");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }
  // This logic is mirrored in FFmpegDecoderModule::Supports. We prefer to use
  // our own OpenH264 decoder through the plugin over ffmpeg by default due to
  // broken decoding with some versions. openh264 has broken decoding of some
  // h264 videos so don't use it unless explicitly allowed for now.
  if (!strcmp(codec->name, "libopenh264") &&
      !StaticPrefs::media_ffmpeg_allow_openh264()) {
    FFMPEG_LOG("  unable to find codec (openh264 disabled by pref)");
    return MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        RESULT_DETAIL("unable to find codec (openh264 disabled by pref)"));
  }
  FFMPEG_LOG("  codec {} : {}", codec->name, codec->long_name);

  if (!(mCodecContext = mLib->avcodec_alloc_context3(codec))) {
    FFMPEG_LOG("  couldn't init VA-API ffmpeg context");
    return NS_ERROR_OUT_OF_MEMORY;
  }
  mCodecContext->opaque = this;

  InitHWCodecContext(ContextType::VAAPI);

  // MOZ_REQUIRES isn't recognized in MakeScopeExit, but InitVAAPIDecoder
  // already locks sMutex at the start, so just escape thread analysis.
  auto releaseVAAPIdecoder = MakeScopeExit([&]() MOZ_NO_THREAD_SAFETY_ANALYSIS {
    if (mVAAPIDeviceContext) {
      mLib->av_buffer_unref(&mVAAPIDeviceContext);
    }
    ReleaseCodecContext();
  });

  if (!CreateVAAPIDeviceContext()) {
    FFMPEG_LOG("  Failed to create VA-API device context");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  MediaResult ret = AllocateExtraData();
  if (NS_FAILED(ret)) {
    return ret;
  }

  if (mLib->avcodec_open2(mCodecContext, codec, nullptr) < 0) {
    FFMPEG_LOG("  Couldn't initialise VA-API decoder");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  if (mAcceleratedFormats.IsEmpty()) {
    mAcceleratedFormats = GetAcceleratedFormats();
    if (!IsFormatAccelerated(mCodecID)) {
      FFMPEG_LOG("  Format {} is not accelerated",
                 mLib->avcodec_get_name(mCodecID));
      return NS_ERROR_NOT_AVAILABLE;
    }
  }

  AdjustHWDecodeLogging();

  FFMPEG_LOG("  VA-API FFmpeg init successful");
  releaseVAAPIdecoder.release();
  return NS_OK;
}

#  if LIBAVCODEC_VERSION_MAJOR >= 60 && !defined(FFVPX_VERSION)
MediaResult FFmpegVideoDecoder<LIBAV_VER>::InitVulkanDecoder() {
  if (!gfx::gfxVars::CanUseVulkanHardwareVideoDecoding()) {
    FFMPEG_LOG("Vulkan FFmpeg decoder disabled");
    return NS_ERROR_NOT_AVAILABLE;
  }

#    if LIBAVCODEC_VERSION_MAJOR == 60
  // libavcodec 60 only supports the experimental VK_MESA_video_decode_av1,
  // but RADV replaced it with stable VK_KHR_video_decode_av1 in Mesa 24.1.
  // Skip Vulkan AV1 decoding so VA-API can take over.
  if (mCodecID == AV_CODEC_ID_AV1) {
    FFMPEG_LOG(
        "Vulkan AV1 decode KHR extension is unavailable with libavcodec 60; "
        "falling back");
    return NS_ERROR_NOT_AVAILABLE;
  }
#    endif

#    if LIBAVCODEC_VERSION_MAJOR < 62
  if (mCodecID == AV_CODEC_ID_VP9) {
    FFMPEG_LOG(
        "Vulkan VP9 decoding requires libavcodec 62 or newer; trying another "
        "hardware decoder");
    return NS_ERROR_NOT_AVAILABLE;
  }
#    endif

  FFMPEG_LOG("Initialising Vulkan FFmpeg decoder");

  StaticMutexAutoLock mon(sMutex);

  // TODO : change to VkGetPhysicalDeviceVideoCapabilitiesKHR when appropriate
  // vulkan video extensions headers are included in mozilla
  // GetAcceleratedFormats() is VAAPI-only, so we need to check if the codec is
  // accelerated by Vulkan

  AVCodec* codec =
      FindVideoHardwareAVCodec(mLib, mCodecID, AV_HWDEVICE_TYPE_VULKAN);
  if (!codec) {
    FFMPEG_LOG("  couldn't find ffmpeg Vulkan decoder");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }
  FFMPEG_LOG("  codec {} : {}", codec->name, codec->long_name);

  if (!(mCodecContext = mLib->avcodec_alloc_context3(codec))) {
    FFMPEG_LOG("  couldn't init Vulkan ffmpeg context");
    return NS_ERROR_OUT_OF_MEMORY;
  }
  mCodecContext->opaque = this;

  InitHWCodecContext(ContextType::Vulkan);

  auto releaseVulkanDecoder =
      MakeScopeExit([&]() MOZ_NO_THREAD_SAFETY_ANALYSIS {
        if (mVulkanDeviceContext) {
          mLib->av_buffer_unref(&mVulkanDeviceContext);
        }
        ReleaseCodecContext();
      });

  if (!CreateVulkanDeviceContext(mon)) {
    FFMPEG_LOG("  Failed to create Vulkan device context");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  if (!mVulkanDecoder.VulkanCanDecodeFormat(
          mCodecID, mLib->avcodec_version(), mVulkanDeviceContext,
          mInfo.mExtraData, mInfo.mColorDepth)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  MediaResult ret = AllocateExtraData();
  if (NS_FAILED(ret)) {
    return ret;
  }

  if (mCodecID == AV_CODEC_ID_AV1) {
    mCodecContext->export_side_data |= AV_CODEC_EXPORT_DATA_FILM_GRAIN;
    // FFmpeg uses this to set filmGrainSupport=0 in the Vulkan AV1 profile
    // query; Intel accepts "Main without film grain", not "Main with".
  }

  if (mLib->avcodec_open2(mCodecContext, codec, nullptr) < 0) {
    if (mCodecContext->hw_frames_ctx) {
      FFMPEG_LOG(
          "  Vulkan: avcodec_open2 failed with app-provided pool, retrying "
          "with FFmpeg "
          "pool");
      mLib->av_buffer_unref(&mCodecContext->hw_frames_ctx);
      if (mLib->avcodec_open2(mCodecContext, codec, nullptr) < 0) {
        FFMPEG_LOG("  Couldn't initialise Vulkan decoder");
        return NS_ERROR_DOM_MEDIA_FATAL_ERR;
      }
    } else {
      FFMPEG_LOG("  Couldn't initialise Vulkan decoder");
      return NS_ERROR_DOM_MEDIA_FATAL_ERR;
    }
  }

  if (mAcceleratedFormats.IsEmpty()) {
    mAcceleratedFormats.AppendElement(mCodecID);
  }

  AdjustHWDecodeLogging();

  FFMPEG_LOG("  Vulkan FFmpeg init successful");
  releaseVulkanDecoder.release();
  return NS_OK;
}
#  endif

MediaResult FFmpegVideoDecoder<LIBAV_VER>::InitV4L2Decoder() {
  FFMPEG_LOG("Initialising V4L2-DRM FFmpeg decoder");

  StaticMutexAutoLock mon(sMutex);

  // mAcceleratedFormats is already configured so check supported
  // formats before we do anything.
  if (mAcceleratedFormats.Length()) {
    if (!IsFormatAccelerated(mCodecID)) {
      FFMPEG_LOG("  Format {} is not accelerated",
                 mLib->avcodec_get_name(mCodecID));
      return NS_ERROR_NOT_AVAILABLE;
    }
    FFMPEG_LOG("  Format {} is accelerated", mLib->avcodec_get_name(mCodecID));
  }

  // Select the appropriate v4l2 codec
  AVCodec* codec = FindVideoHardwareAVCodec(mLib, mCodecID);
  if (!codec) {
    FFMPEG_LOG("No appropriate v4l2 codec found");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }
  FFMPEG_LOG("  V4L2 codec {} : {}", codec->name, codec->long_name);

  if (!(mCodecContext = mLib->avcodec_alloc_context3(codec))) {
    FFMPEG_LOG("  couldn't init HW ffmpeg context");
    return NS_ERROR_OUT_OF_MEMORY;
  }
  mCodecContext->opaque = this;

  InitHWCodecContext(ContextType::V4L2);

  // Disable cropping in FFmpeg.  Because our frames are opaque DRM buffers
  // FFmpeg can't actually crop them and it tries to do so by just modifying
  // the width and height.  This causes problems because V4L2 outputs a single
  // buffer/layer/plane with all three planes stored contiguously.  We need to
  // know the offsets to each plane, and if FFmpeg applies cropping (and then
  // we can't find out what the original uncropped width/height was) then we
  // can't work out the offsets.
  mCodecContext->apply_cropping = 0;

  // MOZ_REQUIRES isn't recognized in MakeScopeExit, but InitV4L2Decoder
  // already locks sMutex at the start, so just escape thread analysis.
  auto releaseDecoder = MakeScopeExit(
      [&]() MOZ_NO_THREAD_SAFETY_ANALYSIS { ReleaseCodecContext(); });

  MediaResult ret = AllocateExtraData();
  if (NS_FAILED(ret)) {
    return ret;
  }

  if (mLib->avcodec_open2(mCodecContext, codec, nullptr) < 0) {
    FFMPEG_LOG("  Couldn't initialise V4L2 decoder");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  // Set mAcceleratedFormats
  if (mAcceleratedFormats.IsEmpty()) {
    // FFmpeg does not correctly report that the V4L2 wrapper decoders are
    // hardware accelerated, but we know they always are.  If we've gotten
    // this far then we know this codec has a V4L2 wrapper decoder and so is
    // accelerateed.
    mAcceleratedFormats.AppendElement(mCodecID);
  }

  AdjustHWDecodeLogging();

  FFMPEG_LOG("  V4L2 FFmpeg init successful");
  mUsingV4L2 = true;
  releaseDecoder.release();
  return NS_OK;
}
#endif

#if LIBAVCODEC_VERSION_MAJOR < 58
FFmpegVideoDecoder<LIBAV_VER>::PtsCorrectionContext::PtsCorrectionContext()
    : mNumFaultyPts(0),
      mNumFaultyDts(0),
      mLastPts(INT64_MIN),
      mLastDts(INT64_MIN) {}

int64_t FFmpegVideoDecoder<LIBAV_VER>::PtsCorrectionContext::GuessCorrectPts(
    int64_t aPts, int64_t aDts) {
  int64_t pts = AV_NOPTS_VALUE;

  if (aDts != int64_t(AV_NOPTS_VALUE)) {
    mNumFaultyDts += aDts <= mLastDts;
    mLastDts = aDts;
  }
  if (aPts != int64_t(AV_NOPTS_VALUE)) {
    mNumFaultyPts += aPts <= mLastPts;
    mLastPts = aPts;
  }
  if ((mNumFaultyPts <= mNumFaultyDts || aDts == int64_t(AV_NOPTS_VALUE)) &&
      aPts != int64_t(AV_NOPTS_VALUE)) {
    pts = aPts;
  } else {
    pts = aDts;
  }
  return pts;
}

void FFmpegVideoDecoder<LIBAV_VER>::PtsCorrectionContext::Reset() {
  mNumFaultyPts = 0;
  mNumFaultyDts = 0;
  mLastPts = INT64_MIN;
  mLastDts = INT64_MIN;
}
#endif

#if defined(MOZ_USE_HWDECODE)
bool FFmpegVideoDecoder<LIBAV_VER>::ShouldDisableHWDecoding(
    bool aDisableHardwareDecoding) const {
#  ifdef MOZ_WIDGET_ANDROID
#    ifdef FFVPX_VERSION
  // We only support decrypt and decode with MediaCodec.
  if (mCDM) {
    FFMPEG_LOG("CDM requires platform decoder");
    return false;
  }
#    endif
  switch (mCodecID) {
    case AV_CODEC_ID_H264:
    case AV_CODEC_ID_HEVC:
      // We only support decoding H264/HEVC with MediaCodec.
      FFMPEG_LOG("Codec {} requires platform decoder",
                 AVCodecToString(mCodecID));
      return false;
    case AV_CODEC_ID_AV1:
      // We only support main profile AV1 with MediaCodec. See bug 1967752.
      if (!AOMDecoder::IsMainProfile(mInfo.mExtraData)) {
        FFMPEG_LOG("Cannot use platfrom decoder AV1 without main profile");
        return true;
      }
      break;
    default:
      break;
  }
#  endif

#  if defined(MOZ_WIDGET_GTK) || defined(MOZ_WIDGET_ANDROID)
  bool supported = false;
  switch (mCodecID) {
    case AV_CODEC_ID_H264:
      supported = gfx::gfxVars::UseH264HwDecode();
      break;
    case AV_CODEC_ID_VP8:
      supported = gfx::gfxVars::UseVP8HwDecode();
      break;
    case AV_CODEC_ID_VP9:
      supported = gfx::gfxVars::UseVP9HwDecode();
      break;
    case AV_CODEC_ID_AV1:
      supported = gfx::gfxVars::UseAV1HwDecode();
      break;
    case AV_CODEC_ID_HEVC:
      supported = gfx::gfxVars::UseHEVCHwDecode();
      break;
    default:
      break;
  }
  if (!supported) {
    FFMPEG_LOG("Codec {} is not accelerated", AVCodecToString(mCodecID));
    return true;
  }
  if (!XRE_IsRDDProcess() && !XRE_IsGPUProcess()) {
    FFMPEG_LOG("Platform decoder works in RDD/GPU process only");
    return true;
  }
#  endif

#  ifdef MOZ_WIDGET_GTK
  bool isHardwareWebRenderUsed = mImageAllocator &&
                                 (mImageAllocator->GetCompositorBackendType() ==
                                  layers::LayersBackend::LAYERS_WR) &&
                                 !mImageAllocator->UsingSoftwareWebRender();
  if (!isHardwareWebRenderUsed) {
    FFMPEG_LOG("Hardware WebRender is off, VAAPI is disabled");
    return true;
  }
#  endif
  return aDisableHardwareDecoding;
}
#endif

#if defined(MOZ_WIDGET_GTK) && defined(MOZ_USE_HWDECODE)
bool FFmpegVideoDecoder<LIBAV_VER>::UploadSWDecodeToDMABuf() const {
  // Use direct DMABuf upload for GL backend Wayland compositor only.
  return mImageAllocator && (mImageAllocator->GetCompositorBackendType() ==
                                 layers::LayersBackend::LAYERS_WR &&
                             !mImageAllocator->UsingSoftwareWebRender() &&
                             mImageAllocator->GetWebRenderCompositorType() ==
                                 layers::WebRenderCompositor::WAYLAND);
}
#endif

FFmpegVideoDecoder<LIBAV_VER>::FFmpegVideoDecoder(
    const FFmpegLibWrapper* aLib, const VideoInfo& aConfig,
    KnowsCompositor* aAllocator, ImageContainer* aImageContainer,
    bool aLowLatency, bool aDisableHardwareDecoding, bool a8BitOutput,
    Maybe<TrackingId> aTrackingId, PRemoteCDMActor* aCDM)
    : FFmpegDataDecoder(aLib, GetCodecId(aConfig.mMimeType), aCDM),
      mImageAllocator(aAllocator),
      mImageContainer(aImageContainer),
      mInfo(aConfig),
#ifdef MOZ_USE_HWDECODE
      mHardwareDecodingDisabled(
          ShouldDisableHWDecoding(aDisableHardwareDecoding)),
#endif  // MOZ_USE_HWDECODE
      mLowLatency(aLowLatency),
      mTrackingId(std::move(aTrackingId)),
      // Value may be changed later when codec is known after initialization.
      m8BitOutput(a8BitOutput) {
  FFMPEG_LOG("FFmpegVideoDecoder::FFmpegVideoDecoder MIME {} Codec ID {}",
             aConfig.mMimeType.get(), static_cast<int>(mCodecID));
  // Use a new MediaByteBuffer as the object will be modified during
  // initialization.
  mExtraData = new MediaByteBuffer;
  mExtraData->AppendElements(*aConfig.mExtraData);
#if defined(MOZ_WIDGET_GTK) && defined(MOZ_USE_HWDECODE)
  mUploadSWDecodeToDMABuf = UploadSWDecodeToDMABuf();
#endif
#ifdef MOZ_USE_HWDECODE
  InitHWDecoderIfAllowed();
#endif  // MOZ_USE_HWDECODE
}

FFmpegVideoDecoder<LIBAV_VER>::~FFmpegVideoDecoder() {
#ifdef CUSTOMIZED_BUFFER_ALLOCATION_ASSERT_ENABLED
  // ffmpeg should have cleared all of its strong references to the decoded data
  // buffers.
  if (mImageTracker) {
    MOZ_DIAGNOSTIC_ASSERT(mImageTracker->IsEmpty(),
                          "Should release all shmem buffers before destroy!");
  }
#endif
}

#ifdef MOZ_USE_HWDECODE
void FFmpegVideoDecoder<LIBAV_VER>::InitHWDecoderIfAllowed() {
  if (mHardwareDecodingDisabled) {
    return;
  }

#  if defined(MOZ_ENABLE_VULKAN_VIDEO) && LIBAVCODEC_VERSION_MAJOR >= 60 && \
      !defined(FFVPX_VERSION)
  if (NS_SUCCEEDED(InitVulkanDecoder())) {
    return;
  }
#  endif

#  ifdef FFVPX_VERSION
  if (!StaticPrefs::media_ffvpx_hw_enabled()) {
    return;
  }
#  endif

#  ifdef MOZ_ENABLE_VAAPI
  if (NS_SUCCEEDED(InitVAAPIDecoder())) {
    return;
  }
#  endif  // MOZ_ENABLE_VAAPI

#  ifdef MOZ_ENABLE_V4L2
  // VAAPI didn't work or is disabled, so try V4L2 with DRM
  if (NS_SUCCEEDED(InitV4L2Decoder())) {
    return;
  }
#  endif  // MOZ_ENABLE_V4L2

#  ifdef MOZ_ENABLE_D3D11VA
  if (XRE_IsGPUProcess() && NS_SUCCEEDED(InitD3D11VADecoder())) {
    return;
  }
#  endif  // MOZ_ENABLE_D3D11VA

#  ifdef MOZ_WIDGET_ANDROID
  if ((XRE_IsRDDProcess() || XRE_IsGPUProcess() ||
       (XRE_IsParentProcess() && PR_GetEnv("MOZ_RUN_GTEST"))) &&
      NS_SUCCEEDED(InitMediaCodecDecoder())) {
    return;
  }
#  endif
}
#endif  // MOZ_USE_HWDECODE

static bool ShouldEnable8BitConversion(const struct AVCodec* aCodec) {
  return 0 == strncmp(aCodec->name, "libdav1d", 8) ||
         0 == strncmp(aCodec->name, "vp9", 3);
}

RefPtr<MediaDataDecoder::InitPromise> FFmpegVideoDecoder<LIBAV_VER>::Init() {
  AUTO_PROFILER_LABEL("FFmpegVideoDecoder::Init", MEDIA_PLAYBACK);
  FFMPEG_LOG("FFmpegVideoDecoder, init, IsHardwareAccelerated={}\n",
             IsHardwareAccelerated());
  // We've finished the HW decoder initialization in the ctor.
  if (IsHardwareAccelerated()) {
    return InitPromise::CreateAndResolve(TrackInfo::kVideoTrack, __func__);
  }
  MediaResult rv = InitSWDecoder(nullptr);
  if (NS_FAILED(rv)) {
    return InitPromise::CreateAndReject(rv, __func__);
  }
  m8BitOutput = m8BitOutput && ShouldEnable8BitConversion(mCodecContext->codec);
  if (m8BitOutput) {
    FFMPEG_LOG("Enable 8-bit output for {}", mCodecContext->codec->name);
    m8BitRecycleBin = MakeRefPtr<BufferRecycleBin>();
  }
  return InitPromise::CreateAndResolve(TrackInfo::kVideoTrack, __func__);
}

static gfx::ColorRange GetColorRange(enum AVColorRange& aColorRange) {
  return aColorRange == AVCOL_RANGE_JPEG ? gfx::ColorRange::FULL
                                         : gfx::ColorRange::LIMITED;
}

static bool IsYUVFormat(const AVPixelFormat& aFormat) {
  return aFormat != AV_PIX_FMT_GBRP && aFormat != AV_PIX_FMT_GBRP10LE;
}

static gfx::YUVColorSpace TransferAVColorSpaceToColorSpace(
    const AVColorSpace aSpace, const AVPixelFormat aFormat,
    const gfx::IntSize& aSize) {
  if (!IsYUVFormat(aFormat)) {
    return gfx::YUVColorSpace::Identity;
  }
  switch (aSpace) {
#if LIBAVCODEC_VERSION_MAJOR >= 55
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
      return gfx::YUVColorSpace::BT2020;
#endif
    case AVCOL_SPC_BT709:
      return gfx::YUVColorSpace::BT709;
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT470BG:
      return gfx::YUVColorSpace::BT601;
    default:
      return DefaultColorSpace(aSize);
  }
}

#ifdef CUSTOMIZED_BUFFER_ALLOCATION
static int GetVideoBufferWrapper(struct AVCodecContext* aCodecContext,
                                 AVFrame* aFrame, int aFlags) {
  auto* decoder =
      static_cast<FFmpegVideoDecoder<LIBAV_VER>*>(aCodecContext->opaque);
  int rv = decoder->GetVideoBuffer(aCodecContext, aFrame, aFlags);
  return rv < 0 ? decoder->GetVideoBufferDefault(aCodecContext, aFrame, aFlags)
                : rv;
}

static void ReleaseVideoBufferWrapper(void* opaque, uint8_t* data) {
  if (opaque) {
    FFMPEGV_LOG("ReleaseVideoBufferWrapper: ImageBufferWrapper={}",
                fmt::ptr(opaque));
    RefPtr image = dont_AddRef(static_cast<ImageBufferWrapper*>(opaque));
    image->StopTracking();
  }
}

static bool IsColorFormatSupportedForUsingCustomizedBuffer(
    const AVPixelFormat& aFormat) {
#  if XP_WIN
  // Currently the web render doesn't support uploading R16 surface, so we can't
  // use the shmem texture for 10 bit+ videos which would be uploaded by the
  // web render. See Bug 1751498.
  return aFormat == AV_PIX_FMT_YUV420P || aFormat == AV_PIX_FMT_YUVJ420P ||
         aFormat == AV_PIX_FMT_YUV444P || aFormat == AV_PIX_FMT_YUVJ444P;
#  else
  // For now, we only support for YUV420P, YUVJ420P, YUV444P and YUVJ444P which
  // are the only non-HW accelerated format supported by FFmpeg's H264 and VP9
  // decoder.
  return aFormat == AV_PIX_FMT_YUV420P || aFormat == AV_PIX_FMT_YUVJ420P ||
         aFormat == AV_PIX_FMT_YUV420P10LE ||
         aFormat == AV_PIX_FMT_YUV420P12LE || aFormat == AV_PIX_FMT_YUV444P ||
         aFormat == AV_PIX_FMT_YUVJ444P || aFormat == AV_PIX_FMT_YUV444P10LE ||
         aFormat == AV_PIX_FMT_YUV444P12LE;
#  endif
}

static bool IsYUV420Sampling(const AVPixelFormat& aFormat) {
  return aFormat == AV_PIX_FMT_YUV420P || aFormat == AV_PIX_FMT_YUVJ420P ||
         aFormat == AV_PIX_FMT_YUV420P10LE || aFormat == AV_PIX_FMT_YUV420P12LE;
}

#  if defined(MOZ_WIDGET_GTK)
bool FFmpegVideoDecoder<LIBAV_VER>::IsLinuxHDR() const {
  if (!mInfo.mColorPrimaries || !mInfo.mTransferFunction) {
    return false;
  }
  return mInfo.mColorPrimaries.value() == gfx::ColorSpace2::BT2020 &&
         (mInfo.mTransferFunction.value() == gfx::TransferFunction::PQ ||
          mInfo.mTransferFunction.value() == gfx::TransferFunction::HLG);
}
#  endif

layers::TextureClient*
FFmpegVideoDecoder<LIBAV_VER>::AllocateTextureClientForImage(
    struct AVCodecContext* aCodecContext, PlanarYCbCrImage* aImage) {
  MOZ_ASSERT(
      IsColorFormatSupportedForUsingCustomizedBuffer(aCodecContext->pix_fmt));

  // FFmpeg will store images with color depth > 8 bits in 16 bits with extra
  // padding.
  const int32_t bytesPerChannel =
      GetColorDepth(aCodecContext->pix_fmt) == gfx::ColorDepth::COLOR_8 ? 1 : 2;

  // If adjusted Ysize is larger than the actual image size (coded_width *
  // coded_height), that means ffmpeg decoder needs extra padding on both width
  // and height. If that happens, the planes will need to be cropped later in
  // order to avoid visible incorrect border on the right and bottom of the
  // actual image.
  //
  // Here are examples of various sizes video in YUV420P format, the width and
  // height would need to be adjusted in order to align padding.
  //
  // Eg1. video (1920*1080)
  // plane Y
  // width 1920 height 1080 -> adjusted-width 1920 adjusted-height 1088
  // plane Cb/Cr
  // width 960  height  540 -> adjusted-width 1024 adjusted-height 544
  //
  // Eg2. video (2560*1440)
  // plane Y
  // width 2560 height 1440 -> adjusted-width 2560 adjusted-height 1440
  // plane Cb/Cr
  // width 1280 height  720 -> adjusted-width 1280 adjusted-height 736
  layers::PlanarYCbCrData data;
  const auto yDims =
      gfx::IntSize{aCodecContext->coded_width, aCodecContext->coded_height};
  auto paddedYSize = yDims;
  mLib->avcodec_align_dimensions(aCodecContext, &paddedYSize.width,
                                 &paddedYSize.height);
  data.mYStride = paddedYSize.Width() * bytesPerChannel;

  MOZ_ASSERT(
      IsColorFormatSupportedForUsingCustomizedBuffer(aCodecContext->pix_fmt));
  auto uvDims = yDims;
  if (IsYUV420Sampling(aCodecContext->pix_fmt)) {
    uvDims.width = (uvDims.width + 1) / 2;
    uvDims.height = (uvDims.height + 1) / 2;
    data.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
  }
  auto paddedCbCrSize = uvDims;
  mLib->avcodec_align_dimensions(aCodecContext, &paddedCbCrSize.width,
                                 &paddedCbCrSize.height);
  data.mCbCrStride = paddedCbCrSize.Width() * bytesPerChannel;

  // Setting other attributes
  data.mPictureRect = gfx::IntRect(
      mInfo.ScaledImageRect(aCodecContext->width, aCodecContext->height)
          .TopLeft(),
      gfx::IntSize(aCodecContext->width, aCodecContext->height));
  data.mStereoMode = mInfo.mStereoMode;
  if (aCodecContext->colorspace != AVCOL_SPC_UNSPECIFIED) {
    data.mYUVColorSpace = TransferAVColorSpaceToColorSpace(
        aCodecContext->colorspace, aCodecContext->pix_fmt,
        data.mPictureRect.Size());
  } else {
    data.mYUVColorSpace = mInfo.mColorSpace
                              ? *mInfo.mColorSpace
                              : DefaultColorSpace(data.mPictureRect.Size());
  }
  data.mColorDepth = GetColorDepth(aCodecContext->pix_fmt);
  data.mColorRange = GetColorRange(aCodecContext->color_range);
  if (mInfo.mTransferFunction) {
    data.mTransferFunction = *mInfo.mTransferFunction;
  }
  data.mHDRMetadata = mInfo.mHDRMetadata;

  FFMPEG_LOGV(
      "Created plane data, YSize=({}, {}), CbCrSize=({}, {}), "
      "CroppedYSize=({}, {}), CroppedCbCrSize=({}, {}), ColorDepth={}",
      paddedYSize.Width(), paddedYSize.Height(), paddedCbCrSize.Width(),
      paddedCbCrSize.Height(), data.YPictureSize().Width(),
      data.YPictureSize().Height(), data.CbCrPictureSize().Width(),
      data.CbCrPictureSize().Height(), static_cast<uint8_t>(data.mColorDepth));

  // Allocate a shmem buffer for image.
  if (NS_FAILED(aImage->CreateEmptyBuffer(data, paddedYSize, paddedCbCrSize))) {
    return nullptr;
  }
  return aImage->GetTextureClient(mImageAllocator);
}

int FFmpegVideoDecoder<LIBAV_VER>::GetVideoBuffer(
    struct AVCodecContext* aCodecContext, AVFrame* aFrame, int aFlags) {
  FFMPEG_LOGV("GetVideoBuffer: aCodecContext={} aFrame={}",
              fmt::ptr(aCodecContext), fmt::ptr(aFrame));

  if (mIsUsingShmemBufferForDecode && !*mIsUsingShmemBufferForDecode) {
    return AVERROR(EINVAL);
  }

  // Codec doesn't support custom allocator.
  if (!(aCodecContext->codec->capabilities & AV_CODEC_CAP_DR1)) {
    return AVERROR(EINVAL);
  }

  // Pre-allocation is only for sw decoding. During decoding, ffmpeg decoder
  // will need to reference decoded frames, if those frames are on shmem buffer,
  // then it would cause a need to read CPU data from GPU, which is slow.
  if (IsHardwareAccelerated()) {
    return AVERROR(EINVAL);
  }

#  if defined(MOZ_WIDGET_GTK) && defined(MOZ_USE_HWDECODE)
  if (mUploadSWDecodeToDMABuf) {
    FFMPEG_LOG("DMABuf upload doesn't use shm buffers");
    return AVERROR(EINVAL);
  }
#  endif

  if (!IsColorFormatSupportedForUsingCustomizedBuffer(aCodecContext->pix_fmt)) {
    FFMPEG_LOG("Not support color format {}",
               static_cast<int>(aCodecContext->pix_fmt));
    return AVERROR(EINVAL);
  }

  if (aCodecContext->lowres != 0) {
    FFMPEG_LOG("Not support low resolution decoding");
    return AVERROR(EINVAL);
  }

  const gfx::IntSize size(aCodecContext->width, aCodecContext->height);
  int rv = mLib->av_image_check_size(size.Width(), size.Height(), 0, nullptr);
  if (rv < 0) {
    FFMPEG_LOG("Invalid image size");
    return rv;
  }

  CheckedInt32 dataSize = mLib->av_image_get_buffer_size(
      aCodecContext->pix_fmt, aCodecContext->coded_width,
      aCodecContext->coded_height, 32);
  if (!dataSize.isValid()) {
    FFMPEG_LOG("Data size overflow!");
    return AVERROR(EINVAL);
  }

  if (!mImageContainer) {
    FFMPEG_LOG("No Image container!");
    return AVERROR(EINVAL);
  }

  RefPtr<PlanarYCbCrImage> image = mImageContainer->CreatePlanarYCbCrImage();
  if (!image) {
    FFMPEG_LOG("Failed to create YCbCr image");
    return AVERROR(EINVAL);
  }
  image->SetColorDepth(mInfo.mColorDepth);

  RefPtr<layers::TextureClient> texture =
      AllocateTextureClientForImage(aCodecContext, image);
  if (!texture) {
    FFMPEG_LOG("Failed to allocate a texture client");
    return AVERROR(EINVAL);
  }

  if (!texture->Lock(layers::OpenMode::OPEN_WRITE)) {
    FFMPEG_LOG("Failed to lock the texture");
    return AVERROR(EINVAL);
  }
  auto autoUnlock = MakeScopeExit([&] { texture->Unlock(); });

  layers::MappedYCbCrTextureData mapped;
  if (!texture->BorrowMappedYCbCrData(mapped)) {
    FFMPEG_LOG("Failed to borrow mapped data for the texture");
    return AVERROR(EINVAL);
  }

  aFrame->data[0] = mapped.y.data;
  aFrame->data[1] = mapped.cb.data;
  aFrame->data[2] = mapped.cr.data;

  aFrame->linesize[0] = mapped.y.stride;
  aFrame->linesize[1] = mapped.cb.stride;
  aFrame->linesize[2] = mapped.cr.stride;

  aFrame->width = aCodecContext->coded_width;
  aFrame->height = aCodecContext->coded_height;
  aFrame->format = aCodecContext->pix_fmt;
  aFrame->extended_data = aFrame->data;
#  if LIBAVCODEC_VERSION_MAJOR < 61
  aFrame->reordered_opaque = aCodecContext->reordered_opaque;
#  endif
  MOZ_ASSERT(aFrame->data[0] && aFrame->data[1] && aFrame->data[2]);

#  ifdef CUSTOMIZED_BUFFER_ALLOCATION_ASSERT_ENABLED
  if (!mImageTracker) {
    mImageTracker = MakeRefPtr<ImageBufferTracker>();
  }
  auto imageWrapper =
      MakeRefPtr<ImageBufferWrapper>(std::move(image), mImageTracker);
#  else
  auto imageWrapper = MakeRefPtr<ImageBufferWrapper>(std::move(image));
#  endif

  // This will hold a strong reference to imageWrapper, and the reference will
  // be dropped when ffmpeg tells us that the buffer is no longer needed. Since
  // the wrapper keeps image alive, it will prevent recycling until ffmpeg is
  // done with the buffer.
  aFrame->buf[0] =
      mLib->av_buffer_create(aFrame->data[0], dataSize.value(),
                             ReleaseVideoBufferWrapper, imageWrapper.get(), 0);
  if (!aFrame->buf[0]) {
    FFMPEG_LOG("Failed to allocate buffer");
    return AVERROR(EINVAL);
  }

  auto* imageWrapperPtr = imageWrapper.forget().take();
  imageWrapperPtr->StartTracking();

  FFMPEG_LOG("Created av buffer, buf={}, data={}, image={}, sz={}",
             fmt::ptr(aFrame->buf[0]), fmt::ptr(aFrame->data[0]),
             fmt::ptr(imageWrapperPtr), dataSize.value());
  mIsUsingShmemBufferForDecode = Some(true);
  return 0;
}
#endif

void FFmpegVideoDecoder<LIBAV_VER>::InitCodecContext() {
  mCodecContext->width = mInfo.mImage.width;
  mCodecContext->height = mInfo.mImage.height;

  // We use the same logic as libvpx in determining the number of threads to use
  // so that we end up behaving in the same fashion when using ffmpeg as
  // we would otherwise cause various crashes (see bug 1236167)
  int decode_threads = 1;
  if (mInfo.mDisplay.width >= 2048) {
    decode_threads = 8;
  } else if (mInfo.mDisplay.width >= 1024) {
    decode_threads = 4;
  } else if (mInfo.mDisplay.width >= 320) {
    decode_threads = 2;
  }

  if (mLowLatency) {
    mCodecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
    // ffvp9 and ffvp8 at this stage do not support slice threading, but it may
    // help with the h264 decoder if there's ever one.
    mCodecContext->thread_type = FF_THREAD_SLICE;
  } else {
    decode_threads = std::min(decode_threads, PR_GetNumberOfProcessors() - 1);
    decode_threads = std::max(decode_threads, 1);
    mCodecContext->thread_count = decode_threads;
    if (decode_threads > 1) {
      mCodecContext->thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME;
    }
  }

  // FFmpeg will call back to this to negotiate a video pixel format.
  mCodecContext->get_format = ChoosePixelFormat;
#ifdef CUSTOMIZED_BUFFER_ALLOCATION
  FFMPEG_LOG("Set get_buffer2 for customized buffer allocation");
  mCodecContext->get_buffer2 = GetVideoBufferWrapper;
  mCodecContext->opaque = this;
#  if FF_API_THREAD_SAFE_CALLBACKS
  mCodecContext->thread_safe_callbacks = 1;
#  endif
#endif
}

nsCString FFmpegVideoDecoder<LIBAV_VER>::GetCodecName() const {
#if LIBAVCODEC_VERSION_MAJOR > 53
  return nsCString(mLib->avcodec_descriptor_get(mCodecID)->name);
#else
  return nsLiteralCString("FFmpegAudioDecoder");
#endif
}

#ifdef MOZ_USE_HWDECODE
void FFmpegVideoDecoder<LIBAV_VER>::InitHWCodecContext(ContextType aType) {
  mCodecContext->width = mInfo.mImage.width;
  mCodecContext->height = mInfo.mImage.height;
  mCodecContext->thread_count = 1;
  // Mirror the SW-path cap from FFmpegDataDecoder::InitDecoder. The four
  // HW init paths (VAAPI/V4L2/D3D11VA/MediaCodec) each call this helper
  // between avcodec_alloc_context3 and avcodec_open2, so one assignment
  // here covers all of them.
  mCodecContext->max_pixels = MAX_VIDEO_WIDTH * MAX_VIDEO_HEIGHT;

  switch (aType) {
    case ContextType::V4L2:
      mCodecContext->get_format = ChooseV4L2PixelFormat;
      break;
    case ContextType::VAAPI:
      mCodecContext->get_format = ChooseVAAPIPixelFormat;
      break;
#  if LIBAVCODEC_VERSION_MAJOR >= 60 && !defined(FFVPX_VERSION)
    case ContextType::Vulkan:
      mCodecContext->get_format = ChooseVulkanPixelFormat;
      break;
#  endif
    case ContextType::D3D11VA:
      MOZ_DIAGNOSTIC_ASSERT(aType == ContextType::D3D11VA);
      mCodecContext->get_format = ChooseD3D11VAPixelFormat;
      break;
    case ContextType::MediaCodec:
      mCodecContext->get_format = ChooseMediaCodecPixelFormat;
      break;
    default:
      break;
  }

  if (mCodecID == AV_CODEC_ID_H264) {
    mCodecContext->extra_hw_frames =
        AssertedCast<int>(H264::ComputeMaxRefFrames(mInfo.mExtraData));
  } else if (mCodecID == AV_CODEC_ID_HEVC) {
    mCodecContext->extra_hw_frames =
        AssertedCast<int>(H265::ComputeMaxRefFrames(mInfo.mExtraData));
  } else {
    mCodecContext->extra_hw_frames = EXTRA_HW_FRAMES;
  }
  if (mLowLatency) {
    mCodecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
  }
}
#endif

static int64_t GetFramePts(const AVFrame* aFrame) {
#if LIBAVCODEC_VERSION_MAJOR > 57
  return aFrame->pts;
#else
  return aFrame->pkt_pts;
#endif
}

static bool IsKeyFrame(const AVFrame* aFrame) {
#if LIBAVCODEC_VERSION_MAJOR > 61
  return !!(aFrame->flags & AV_FRAME_FLAG_KEY);
#else
  return !!aFrame->key_frame;
#endif
}

MediaResult FFmpegVideoDecoder<LIBAV_VER>::DoDecode(
    MediaRawData* aSample, uint8_t* aData, int aSize, bool* aGotFrame,
    MediaDataDecoder::DecodedData& aResults) {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
  AVPacket* packet;

#if LIBAVCODEC_VERSION_MAJOR >= 61
  packet = mLib->av_packet_alloc();
  auto raii = MakeScopeExit([&]() { mLib->av_packet_free(&packet); });
#else
  AVPacket packet_mem;
  packet = &packet_mem;
  mLib->av_init_packet(packet);
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 58
  mDecodeStats.DecodeStart();
#endif

  packet->data = aData;
  packet->size = aSize;
  packet->dts = aSample->mTimecode.ToMicroseconds();
  packet->pts = aSample->mTime.ToMicroseconds();
  packet->flags = aSample->mKeyframe ? AV_PKT_FLAG_KEY : 0;
  packet->pos = aSample->mOffset;

  mTrackingId.apply([&](const auto& aId) {
    MediaInfoFlag flag = MediaInfoFlag::None;
    flag |= (aSample->mKeyframe ? MediaInfoFlag::KeyFrame
                                : MediaInfoFlag::NonKeyFrame);
    flag |= (IsHardwareAccelerated() ? MediaInfoFlag::HardwareDecoding
                                     : MediaInfoFlag::SoftwareDecoding);
    switch (mCodecID) {
      case AV_CODEC_ID_H264:
        flag |= MediaInfoFlag::VIDEO_H264;
        break;
#if LIBAVCODEC_VERSION_MAJOR >= 54
      case AV_CODEC_ID_VP8:
        flag |= MediaInfoFlag::VIDEO_VP8;
        break;
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 55
      case AV_CODEC_ID_VP9:
        flag |= MediaInfoFlag::VIDEO_VP9;
        break;
      case AV_CODEC_ID_HEVC:
        flag |= MediaInfoFlag::VIDEO_HEVC;
        break;
#endif
#ifdef FFMPEG_AV1_DECODE
      case AV_CODEC_ID_AV1:
        flag |= MediaInfoFlag::VIDEO_AV1;
        break;
#endif
      default:
        break;
    }
    mPerformanceRecorder.Start(
        packet->dts,
        nsPrintfCString("FFmpegVideoDecoder(%d)", LIBAVCODEC_VERSION_MAJOR),
        aId, flag);
  });

#if defined(MOZ_WIDGET_ANDROID) && defined(USING_MOZFFVPX)
  MediaResult ret = MaybeAttachCryptoInfo(aSample, packet);
  if (NS_FAILED(ret)) {
    return ret;
  }
#endif

#ifdef MOZ_FFMPEG_USE_INPUT_INFO_MAP
#  ifdef MOZ_WIDGET_ANDROID
  if (IsHardwareAccelerated())
#  endif
  {
    InsertInputInfo(aSample);
  }
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 58
#  ifdef MOZ_WIDGET_ANDROID
  if (!aData) {
    mShouldResumeDrain = true;
  }
#  endif
  if (aData || !mHasSentDrainPacket) {
    packet->duration = aSample->mDuration.ToMicroseconds();
    int res = mLib->avcodec_send_packet(mCodecContext, packet);
    if (res < 0) {
      // In theory, avcodec_send_packet could sent -EAGAIN should its internal
      // buffers be full. In practice this can't happen as we only feed one
      // frame at a time, and we immediately call avcodec_receive_frame right
      // after.
      char errStr[AV_ERROR_MAX_STRING_SIZE];
      mLib->av_strerror(res, errStr, AV_ERROR_MAX_STRING_SIZE);
      FFMPEG_LOG("avcodec_send_packet error: {} (code={})", errStr, res);
      nsresult rv;
      if (res == int(AVERROR_EOF)) {
        rv = MaybeQueueDrain(aResults) ? NS_ERROR_DOM_MEDIA_END_OF_STREAM
                                       : NS_ERROR_NOT_AVAILABLE;
      } else {
        rv = NS_ERROR_DOM_MEDIA_DECODE_ERR;
      }
      return MediaResult(
          rv, RESULT_DETAIL("avcodec_send_packet error: %s", errStr));
    }
  }
  if (!aData) {
    // On some platforms (e.g. Android), there are a limited number of output
    // buffers available. When draining, we may reach this limit, so we must
    // return what we have, and allow the caller to try again. We don't need to
    // resend the null packet in that case since the codec is still in the
    // draining state.
    mHasSentDrainPacket = true;
  }
  if (aGotFrame) {
    *aGotFrame = false;
  }
  do {
    if (!PrepareFrame()) {
      NS_WARNING("FFmpeg decoder failed to allocate frame.");
      return MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__);
    }

#  if defined(MOZ_USE_HWDECODE) && defined(MOZ_WIDGET_GTK)
    // Release unused VA-API surfaces before avcodec_receive_frame() as
    // ffmpeg recycles VASurface for HW decoding.
    if (mVideoFramePool) {
      mVideoFramePool->ReleaseUnusedVAAPIFrames();
    }
#  endif

    int res = mLib->avcodec_receive_frame(mCodecContext, mFrame);
    int64_t fpos =
#  if LIBAVCODEC_VERSION_MAJOR > 61
        packet->pos;
#  else
        mFrame->pkt_pos;
#  endif
    if (res == int(AVERROR_EOF)) {
      if (MaybeQueueDrain(aResults)) {
        FFMPEG_LOG("  Output buffer shortage.");
        return NS_ERROR_NOT_AVAILABLE;
      }
      FFMPEG_LOG("  End of stream.");
      return NS_ERROR_DOM_MEDIA_END_OF_STREAM;
    }
    if (res == AVERROR(EAGAIN)) {
      return NS_OK;
    }
    if (res < 0) {
      char errStr[AV_ERROR_MAX_STRING_SIZE];
      mLib->av_strerror(res, errStr, AV_ERROR_MAX_STRING_SIZE);
      FFMPEG_LOG("  avcodec_receive_frame error: {}", errStr);
      return MediaResult(
          NS_ERROR_DOM_MEDIA_DECODE_ERR,
          RESULT_DETAIL("avcodec_receive_frame error: %s", errStr));
    }

    MediaResult rv;
#  ifdef MOZ_USE_HWDECODE
    if (IsHardwareAccelerated()) {
#    ifdef MOZ_WIDGET_GTK
      mDecodeStats.UpdateDecodeTimes(Duration(mFrame));
      if (mDecodeStats.IsDecodingSlow() &&
          !StaticPrefs::media_ffmpeg_disable_software_fallback()) {
        PROFILER_MARKER_TEXT("FFmpegVideoDecoder::DoDecode", MEDIA_PLAYBACK, {},
                             "Fallback to SW decode");
        FFMPEG_LOG("  HW decoding is slow, switching back to SW decode");
        return MediaResult(
            NS_ERROR_DOM_MEDIA_DECODE_ERR,
            RESULT_DETAIL("HW decoding is slow, switching back to SW decode"));
      }
      if (mUsingV4L2) {
        rv = CreateImageV4L2(fpos, GetFramePts(mFrame), Duration(mFrame),
                             aResults);
      }
#      if LIBAVCODEC_VERSION_MAJOR >= 60 && !defined(FFVPX_VERSION)
      else if (mVulkanDeviceContext) {
        rv = CreateImageVulkan(fpos, GetFramePts(mFrame), Duration(mFrame),
                               aResults);
      }
#      endif
      else {
        rv = CreateImageVAAPI(fpos, GetFramePts(mFrame), Duration(mFrame),
                              aResults);
      }

      // If VA-API/V4L2 playback failed, just quit. Decoder is going to be
      // restarted without hardware acceleration
      if (NS_FAILED(rv)) {
        // Explicitly remove dmabuf surface pool as it's configured
        // for VA-API/V4L2 support.
        mVideoFramePool = nullptr;
        return rv;
      }
#    elif defined(MOZ_ENABLE_D3D11VA)
      mDecodeStats.UpdateDecodeTimes(Duration(mFrame));
      rv = CreateImageD3D11(fpos, GetFramePts(mFrame), Duration(mFrame),
                            aResults);
#    elif defined(MOZ_WIDGET_ANDROID)
      InputInfo info(aSample);
      info.mTimecode = -1;
      TakeInputInfo(mFrame, info);
      mDecodeStats.UpdateDecodeTimes(info.mDuration);
      rv = CreateImageMediaCodec(fpos, GetFramePts(mFrame), info.mTimecode,
                                 info.mDuration, aResults);
#    else
      mDecodeStats.UpdateDecodeTimes(Duration(mFrame));
      return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                         RESULT_DETAIL("No HW decoding implementation!"));
#    endif
    } else
#  endif
    {
      mDecodeStats.UpdateDecodeTimes(Duration(mFrame));
      rv = CreateImage(fpos, GetFramePts(mFrame), Duration(mFrame), aResults);
    }
    if (NS_FAILED(rv)) {
      return rv;
    }

    RecordFrame(aSample, aResults.LastElement());
    if (aGotFrame) {
      *aGotFrame = true;
    }
  } while (true);
#else
  if (!PrepareFrame()) {
    NS_WARNING("FFmpeg decoder failed to allocate frame.");
    return MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__);
  }

  // Required with old version of FFmpeg/LibAV
  mFrame->reordered_opaque = AV_NOPTS_VALUE;

  int decoded;
  int bytesConsumed =
      mLib->avcodec_decode_video2(mCodecContext, mFrame, &decoded, packet);

  FFMPEG_LOG(
      "DoDecodeFrame:decode_video: rv={} decoded={} "
      "(Input: pts({}) dts({}) Output: pts({}) "
      "opaque({}) pts({}) pkt_dts({}))",
      bytesConsumed, decoded, packet->pts, packet->dts, mFrame->pts,
      mFrame->reordered_opaque, mFrame->pts, mFrame->pkt_dts);

  if (bytesConsumed < 0) {
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                       RESULT_DETAIL("FFmpeg video error: %d", bytesConsumed));
  }

  if (!decoded) {
    if (aGotFrame) {
      *aGotFrame = false;
    }
    return NS_OK;
  }

  // If we've decoded a frame then we need to output it
  int64_t pts =
      mPtsContext.GuessCorrectPts(GetFramePts(mFrame), mFrame->pkt_dts);

  InputInfo info(aSample);
  TakeInputInfo(mFrame, info);

  MediaResult rv = CreateImage(aSample->mOffset, pts, info.mDuration, aResults);
  if (NS_FAILED(rv)) {
    return rv;
  }

  mTrackingId.apply(
      [&](const auto&) { RecordFrame(aSample, aResults.LastElement()); });

  if (aGotFrame) {
    *aGotFrame = true;
  }
  return rv;
#endif
}

void FFmpegVideoDecoder<LIBAV_VER>::RecordFrame(const MediaRawData* aSample,
                                                const MediaData* aData) {
  mPerformanceRecorder.Record(
      aData->mTimecode.ToMicroseconds(), [&](auto& aStage) {
        aStage.SetResolution(mFrame->width, mFrame->height);
        auto format = [&]() -> Maybe<DecodeStage::ImageFormat> {
          switch (mCodecContext->pix_fmt) {
            case AV_PIX_FMT_YUV420P:
            case AV_PIX_FMT_YUVJ420P:
            case AV_PIX_FMT_YUV420P10LE:
#if LIBAVCODEC_VERSION_MAJOR >= 57
            case AV_PIX_FMT_YUV420P12LE:
#endif
              return Some(DecodeStage::YUV420P);
            case AV_PIX_FMT_YUV422P:
            case AV_PIX_FMT_YUV422P10LE:
#if LIBAVCODEC_VERSION_MAJOR >= 57
            case AV_PIX_FMT_YUV422P12LE:
#endif
              return Some(DecodeStage::YUV422P);
            case AV_PIX_FMT_YUV444P:
            case AV_PIX_FMT_YUVJ444P:
            case AV_PIX_FMT_YUV444P10LE:
#if LIBAVCODEC_VERSION_MAJOR >= 57
            case AV_PIX_FMT_YUV444P12LE:
#endif
              return Some(DecodeStage::YUV444P);
            case AV_PIX_FMT_GBRP:
            case AV_PIX_FMT_GBRP10LE:
              return Some(DecodeStage::GBRP);
            case AV_PIX_FMT_VAAPI_VLD:
              return Some(DecodeStage::VAAPI_SURFACE);
#ifdef MOZ_ENABLE_D3D11VA
            case AV_PIX_FMT_D3D11:
              return Some(DecodeStage::D3D11_SURFACE);
#endif
            default:
              return Nothing();
          }
        }();
        format.apply([&](auto& aFmt) { aStage.SetImageFormat(aFmt); });
        aStage.SetColorDepth(GetColorDepth(mCodecContext->pix_fmt));
        aStage.SetYUVColorSpace(GetFrameColorSpace());
        aStage.SetColorRange(GetFrameColorRange());
        aStage.SetStartTimeAndEndTime(aSample->mTime.ToMicroseconds(),
                                      aSample->GetEndTime().ToMicroseconds());
      });
}

#ifdef MOZ_WIDGET_ANDROID
void FFmpegVideoDecoder<LIBAV_VER>::ResumeDrain() {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());

  if (mDrainPromise.IsEmpty()) {
    FFMPEG_LOGV("Resume drain but promise already fulfilled");
    return;
  }

  FFMPEG_LOGV("Resume drain");
  mShouldResumeDrain = true;
  ProcessDrain();
}

void FFmpegVideoDecoder<LIBAV_VER>::QueueResumeDrain() {
  if (!mShouldResumeDrain.exchange(false)) {
    return;
  }

  // It is possible we may attempt to dispatch after the task queue is shutdown
  // because we still had frames we had yet to release. We can ignore the error.
  (void)mTaskQueue->Dispatch(NS_NewRunnableFunction(
      __func__, [self = RefPtr{this}] { self->ResumeDrain(); }));
}
#endif

bool FFmpegVideoDecoder<LIBAV_VER>::MaybeQueueDrain(
    const MediaDataDecoder::DecodedData& aData) {
#if defined(MOZ_WIDGET_ANDROID) && defined(USING_MOZFFVPX)
  if (aData.IsEmpty() && mMediaCodecDeviceContext &&
      !mLib->moz_avcodec_mediacodec_is_eos(mCodecContext)) {
    FFMPEG_LOGV("Schedule drain");
    return true;
  }
  mShouldResumeDrain = false;
#endif
  return false;
}

gfx::ColorDepth FFmpegVideoDecoder<LIBAV_VER>::GetColorDepth(
    const AVPixelFormat& aFormat) const {
  switch (aFormat) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVJ444P:
      return gfx::ColorDepth::COLOR_8;
    case AV_PIX_FMT_YUV420P10LE:
    case AV_PIX_FMT_YUV422P10LE:
    case AV_PIX_FMT_YUV444P10LE:
    case AV_PIX_FMT_GBRP10LE:
      return gfx::ColorDepth::COLOR_10;
#if LIBAVCODEC_VERSION_MAJOR >= 57
    case AV_PIX_FMT_YUV420P12LE:
    case AV_PIX_FMT_YUV422P12LE:
    case AV_PIX_FMT_YUV444P12LE:
      return gfx::ColorDepth::COLOR_12;
#endif
#ifdef MOZ_ENABLE_D3D11VA
    case AV_PIX_FMT_D3D11:
#endif
    case AV_PIX_FMT_VAAPI_VLD:
      return mInfo.mColorDepth;
    default:
      MOZ_ASSERT_UNREACHABLE("Not supported format?");
      return gfx::ColorDepth::COLOR_8;
  }
}

gfx::YUVColorSpace FFmpegVideoDecoder<LIBAV_VER>::GetFrameColorSpace() const {
  AVColorSpace colorSpace = AVCOL_SPC_UNSPECIFIED;
#if LIBAVCODEC_VERSION_MAJOR > 58
  colorSpace = mFrame->colorspace;
#else
  if (mLib->av_frame_get_colorspace) {
    colorSpace = (AVColorSpace)mLib->av_frame_get_colorspace(mFrame);
  }
#endif
  return TransferAVColorSpaceToColorSpace(
      colorSpace, (AVPixelFormat)mFrame->format,
      gfx::IntSize{mFrame->width, mFrame->height});
}

gfx::ColorSpace2 FFmpegVideoDecoder<LIBAV_VER>::GetFrameColorPrimaries() const {
  AVColorPrimaries colorPrimaries = AVCOL_PRI_UNSPECIFIED;
#if LIBAVCODEC_VERSION_MAJOR > 57
  colorPrimaries = mFrame->color_primaries;
#endif
  switch (colorPrimaries) {
#if LIBAVCODEC_VERSION_MAJOR >= 55
    case AVCOL_PRI_BT2020:
      return gfx::ColorSpace2::BT2020;
#endif
    case AVCOL_PRI_BT709:
      return gfx::ColorSpace2::BT709;
    default:
      return gfx::ColorSpace2::BT709;
  }
}

gfx::ColorRange FFmpegVideoDecoder<LIBAV_VER>::GetFrameColorRange() const {
  AVColorRange range = AVCOL_RANGE_UNSPECIFIED;
#if LIBAVCODEC_VERSION_MAJOR > 58
  range = mFrame->color_range;
#else
  if (mLib->av_frame_get_color_range) {
    range = (AVColorRange)mLib->av_frame_get_color_range(mFrame);
  }
#endif
  return GetColorRange(range);
}

gfx::SurfaceFormat FFmpegVideoDecoder<LIBAV_VER>::GetSurfaceFormat() const {
  switch (mInfo.mColorDepth) {
    case gfx::ColorDepth::COLOR_8:
      return gfx::SurfaceFormat::NV12;
    case gfx::ColorDepth::COLOR_10:
      return gfx::SurfaceFormat::P010;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected surface type");
      return gfx::SurfaceFormat::NV12;
  }
}

#if defined(MOZ_WIDGET_GTK) && defined(MOZ_USE_HWDECODE)
// Convert AVChromaLocation to
// wp_color_representation_surface_v1_chroma_location
static uint32_t AVChromaLocationToWPChromaLocation(uint32_t aAVChromaLocation) {
  switch (aAVChromaLocation) {
    case AVCHROMA_LOC_UNSPECIFIED:
    default:
      return 0;  // No chroma location specified
    case AVCHROMA_LOC_LEFT:
      return 1;
    case AVCHROMA_LOC_CENTER:
      return 2;
    case AVCHROMA_LOC_TOPLEFT:
      return 3;
    case AVCHROMA_LOC_TOP:
      return 4;
    case AVCHROMA_LOC_BOTTOMLEFT:
      return 5;
    case AVCHROMA_LOC_BOTTOM:
      return 6;
  }
}
#endif

MediaResult FFmpegVideoDecoder<LIBAV_VER>::CreateImage(
    int64_t aOffset, int64_t aPts, int64_t aDuration,
    MediaDataDecoder::DecodedData& aResults) {
  FFMPEG_LOG("Got one frame output with pts={} dts={} duration={}", aPts,
             mFrame->pkt_dts, aDuration);

  VideoData::QuantizableBuffer b;
  b.mPlanes[0].mData = mFrame->data[0];
  b.mPlanes[1].mData = mFrame->data[1];
  b.mPlanes[2].mData = mFrame->data[2];

  b.mPlanes[0].mStride = mFrame->linesize[0];
  b.mPlanes[1].mStride = mFrame->linesize[1];
  b.mPlanes[2].mStride = mFrame->linesize[2];

  b.mPlanes[0].mSkip = 0;
  b.mPlanes[1].mSkip = 0;
  b.mPlanes[2].mSkip = 0;

  b.mPlanes[0].mWidth = mFrame->width;
  b.mPlanes[0].mHeight = mFrame->height;
  SetChromaPlaneGeometryFromAVFormat(b, static_cast<int>(mFrame->format),
                                     mFrame->width, mFrame->height);
  b.mYUVColorSpace = GetFrameColorSpace();
  b.mColorRange = GetFrameColorRange();

  RefPtr<VideoData> v;
#ifdef CUSTOMIZED_BUFFER_ALLOCATION
  bool requiresCopy = false;
#  ifdef XP_MACOSX
  // Bug 1765388: macOS needs to generate a MacIOSurfaceImage in order to
  // properly display HDR video. The later call to ::CreateAndCopyData does
  // that. If this shared memory buffer path also generated a
  // MacIOSurfaceImage, then we could use it for HDR.
  requiresCopy = (b.mColorDepth != gfx::ColorDepth::COLOR_8);
#  endif
#  ifdef MOZ_WIDGET_ANDROID
  // Some Android devices can only render 8-bit images and cannot use high
  // bit-depth decoded data directly.
  requiresCopy = m8BitOutput && b.mColorDepth != gfx::ColorDepth::COLOR_8;
#  endif
  if (mIsUsingShmemBufferForDecode && *mIsUsingShmemBufferForDecode &&
      !requiresCopy) {
    auto* wrapper = static_cast<ImageBufferWrapper*>(
        mLib->av_buffer_get_opaque(mFrame->buf[0]));
    MOZ_ASSERT(wrapper);
    FFMPEG_LOGV("Create a video data from a shmem image={}", fmt::ptr(wrapper));
    v = VideoData::CreateFromImage(
        mInfo.mDisplay, aOffset, TimeUnit::FromMicroseconds(aPts),
        TimeUnit::FromMicroseconds(aDuration), wrapper->AsImage(),
        IsKeyFrame(mFrame), TimeUnit::FromMicroseconds(-1));
  }
#endif
#if defined(MOZ_WIDGET_GTK) && defined(MOZ_USE_HWDECODE)
  if (mUploadSWDecodeToDMABuf) {
    MOZ_DIAGNOSTIC_ASSERT(!v);
    if (!mVideoFramePool) {
      mVideoFramePool = MakeUnique<VideoFramePool<LIBAV_VER>>(10);
    }
    const auto yuvData = layers::PlanarYCbCrData::From(b);
    if (yuvData) {
      auto surface =
          mVideoFramePool->GetVideoFrameSurface(*yuvData, mCodecContext);
      if (surface) {
        surface->SetYUVColorSpace(GetFrameColorSpace());
        surface->SetColorRange(GetFrameColorRange());
        if (mInfo.mColorPrimaries) {
          surface->SetColorPrimaries(mInfo.mColorPrimaries.value());
        }
        if (mInfo.mTransferFunction) {
          surface->SetTransferFunction(mInfo.mTransferFunction.value());
        }
        if (mInfo.mHDRMetadata) {
          surface->SetHDRMetadata(mInfo.mHDRMetadata.value());
        }
        surface->SetWPChromaLocation(
            AVChromaLocationToWPChromaLocation(mFrame->chroma_location));
        FFMPEG_LOGV(
            "Uploaded frame DMABuf surface UID {} HDR {} color space {}/{} "
            "transfer {}",
            surface->GetDMABufSurface()->GetUID(), IsLinuxHDR(),
            YUVColorSpaceToString(GetFrameColorSpace()),
            mInfo.mColorPrimaries
                ? ColorSpace2ToString(mInfo.mColorPrimaries.value())
                : "unknown",
            mInfo.mTransferFunction
                ? TransferFunctionToString(mInfo.mTransferFunction.value())
                : "unknown");
        v = VideoData::CreateFromImage(
            mInfo.mDisplay, aOffset, TimeUnit::FromMicroseconds(aPts),
            TimeUnit::FromMicroseconds(aDuration), surface->GetAsImage(),
            IsKeyFrame(mFrame), TimeUnit::FromMicroseconds(-1));
      } else {
        FFMPEG_LOG("Failed to uploaded video data to DMABuf");
      }
    } else {
      FFMPEG_LOG("Failed to convert PlanarYCbCrData");
    }
  }
#endif
  if (!v) {
    if (m8BitOutput && b.mColorDepth != gfx::ColorDepth::COLOR_8) {
      MediaResult ret = b.To8BitPerChannel(m8BitRecycleBin);
      if (NS_FAILED(ret.Code())) {
        FFMPEG_LOG("{}: {}", __func__, ret.Message().get());
        return ret;
      }
    }
    Result<already_AddRefed<VideoData>, MediaResult> r =
        VideoData::CreateAndCopyData(
            mInfo, mImageContainer, aOffset, TimeUnit::FromMicroseconds(aPts),
            TimeUnit::FromMicroseconds(aDuration), b, IsKeyFrame(mFrame),
            TimeUnit::FromMicroseconds(mFrame->pkt_dts),
            mInfo.ScaledImageRect(mFrame->width, mFrame->height),
            mImageAllocator);
    if (r.isErr()) {
      return r.unwrapErr();
    }
    v = r.unwrap();
  }
  MOZ_ASSERT(v);
  aResults.AppendElement(std::move(v));
  return NS_OK;
}

#if defined(MOZ_USE_HWDECODE) && defined(MOZ_WIDGET_GTK)
bool FFmpegVideoDecoder<LIBAV_VER>::GetVAAPISurfaceDescriptor(
    VADRMPRIMESurfaceDescriptor* aVaDesc) {
  VASurfaceID surface_id = (VASurfaceID)(uintptr_t)mFrame->data[3];
  VAStatus vas = VALibWrapper::sFuncs.vaExportSurfaceHandle(
      mDisplay, surface_id, VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
      VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS, aVaDesc);
  if (vas != VA_STATUS_SUCCESS) {
    FFMPEG_LOG("GetVAAPISurfaceDescriptor(): vaExportSurfaceHandle failed");
    return false;
  }
  vas = VALibWrapper::sFuncs.vaSyncSurface(mDisplay, surface_id);
  if (vas != VA_STATUS_SUCCESS) {
    FFMPEG_LOG("GetVAAPISurfaceDescriptor(): vaSyncSurface failed");
  }
  return true;
}

MediaResult FFmpegVideoDecoder<LIBAV_VER>::CreateImageVAAPI(
    int64_t aOffset, int64_t aPts, int64_t aDuration,
    MediaDataDecoder::DecodedData& aResults) {
  VADRMPRIMESurfaceDescriptor vaDesc;
  if (!GetVAAPISurfaceDescriptor(&vaDesc)) {
    return MediaResult(
        NS_ERROR_DOM_MEDIA_DECODE_ERR,
        RESULT_DETAIL("Unable to get frame by vaExportSurfaceHandle()"));
  }
  auto releaseSurfaceDescriptor = MakeScopeExit(
      [&] { DMABufSurfaceYUV::ReleaseVADRMPRIMESurfaceDescriptor(vaDesc); });

  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
  if (!mVideoFramePool) {
    AVHWFramesContext* context =
        (AVHWFramesContext*)mCodecContext->hw_frames_ctx->data;
    mVideoFramePool =
        MakeUnique<VideoFramePool<LIBAV_VER>>(context->initial_pool_size);
  }
  auto surface = mVideoFramePool->GetVideoFrameSurface(
      vaDesc, mFrame->width, mFrame->height, mCodecContext, mFrame, mLib);
  if (!surface) {
    FFMPEG_LOG("CreateImageVAAPI(): failed to get VideoFrameSurface");
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                       RESULT_DETAIL("VAAPI dmabuf allocation error"));
  }

  surface->SetYUVColorSpace(GetFrameColorSpace());
  surface->SetColorRange(GetFrameColorRange());
  if (mInfo.mColorPrimaries) {
    surface->SetColorPrimaries(mInfo.mColorPrimaries.value());
  }
  if (mInfo.mTransferFunction) {
    surface->SetTransferFunction(mInfo.mTransferFunction.value());
  }
  surface->SetWPChromaLocation(
      AVChromaLocationToWPChromaLocation(mFrame->chroma_location));
  if (mInfo.mHDRMetadata) {
    surface->SetHDRMetadata(mInfo.mHDRMetadata.value());
  }

  FFMPEG_LOG(
      "VA-API frame pts={} dts={} duration={} color space {}/{} transfer {}",
      aPts, mFrame->pkt_dts, aDuration,
      YUVColorSpaceToString(GetFrameColorSpace()),
      mInfo.mColorPrimaries ? ColorSpace2ToString(mInfo.mColorPrimaries.value())
                            : "unknown",
      mInfo.mTransferFunction
          ? TransferFunctionToString(mInfo.mTransferFunction.value())
          : "unknown");
  RefPtr<VideoData> vp = VideoData::CreateFromImage(
      mInfo.mDisplay, aOffset, TimeUnit::FromMicroseconds(aPts),
      TimeUnit::FromMicroseconds(aDuration), surface->GetAsImage(),
      IsKeyFrame(mFrame), TimeUnit::FromMicroseconds(mFrame->pkt_dts));

  if (!vp) {
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                       RESULT_DETAIL("VAAPI image allocation error"));
  }

  aResults.AppendElement(std::move(vp));
  return NS_OK;
}

#  if LIBAVCODEC_VERSION_MAJOR >= 60 && !defined(FFVPX_VERSION)

static void FillDRMDescriptorYUVSingleObject(
    uint32_t aFormat, int aFd, size_t aSize, uint64_t aModifier,
    uint32_t aYPitch, uint32_t aUVPitch, uint32_t aYOffset, size_t aUVOffset,
    AVDRMFrameDescriptor* aOutDesc) {
  aOutDesc->nb_objects = 1;
  aOutDesc->objects[0].fd = aFd;
  aOutDesc->objects[0].size = aSize;
  aOutDesc->objects[0].format_modifier = aModifier;
  aOutDesc->nb_layers = 1;
  aOutDesc->layers[0].format = aFormat;
  aOutDesc->layers[0].nb_planes = 2;
  aOutDesc->layers[0].planes[0].object_index = 0;
  aOutDesc->layers[0].planes[0].offset = static_cast<ptrdiff_t>(aYOffset);
  aOutDesc->layers[0].planes[0].pitch = static_cast<ptrdiff_t>(aYPitch);
  aOutDesc->layers[0].planes[1].object_index = 0;
  aOutDesc->layers[0].planes[1].offset = static_cast<ptrdiff_t>(aUVOffset);
  aOutDesc->layers[0].planes[1].pitch = static_cast<ptrdiff_t>(aUVPitch);
}

static uint32_t DRMFormatForColorDepth(const gfx::ColorDepth aDepth) {
  switch (aDepth) {
    case gfx::ColorDepth::COLOR_8:
      return DRM_FORMAT_NV12;
    case gfx::ColorDepth::COLOR_10:
      return DRM_FORMAT_P010;
    case gfx::ColorDepth::COLOR_12:
      return DRM_FORMAT_P012;
    case gfx::ColorDepth::COLOR_16:
      return DRM_FORMAT_P016;
    default:
      return DRM_FORMAT_NV12;
  }
}

MediaResult FFmpegVideoDecoder<LIBAV_VER>::CreateImageVulkan(
    int64_t aOffset, int64_t aPts, int64_t aDuration,
    MediaDataDecoder::DecodedData& aResults) {
  if (!mFrame->hw_frames_ctx) {
    NS_WARNING("[VULKAN] ERROR: No hw_frames_ctx!");
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                       RESULT_DETAIL("Vulkan frame missing hw_frames_ctx"));
  }

  auto* devCtx = (AVHWDeviceContext*)mVulkanDeviceContext->data;
  auto* vkDevCtx = (AVVulkanDeviceContext*)devCtx->hwctx;
  // Match FFmpeg's vkCreateDevice queue flags on every driver. Non-zero flags
  // (e.g. INTERNALLY_SYNCHRONIZED when that extension is enabled) require
  // GetDeviceQueue2 with the same value; flags=0 is equivalent to the old
  // GetDeviceQueue path. queue_flags is lavu 60.32.100+ (9fe5758da5); older
  // public lavu exposes no queue_flags field, so Firefox must use 0 there.
  // FFmpeg applies this one global value to every created queue family. If
  // vkGetDeviceQueue2 fails to load, InitCtx fails via IsLoaded().
  VkDeviceQueueCreateFlags queueCreateFlags = 0;
#    if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(60, 32, 100)
  queueCreateFlags = vkDevCtx->queue_flags;
#    endif
  if (!mVulkanDecoder.InitCtx(
          vkDevCtx->act_dev, vkDevCtx->phys_dev, vkDevCtx->get_proc_addr,
          vkDevCtx->inst, mVulkanDeviceHolder->Generation(),
          VulkanTransferQueueFamily(vkDevCtx), queueCreateFlags)) {
    return MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        RESULT_DETAIL("Failed to init Vulkan Context structure"));
  }

  uint32_t width = mFrame->width;
  uint32_t height = mFrame->height;

  if (!mVulkanTilingSettled) {
    mVulkanTilingSettled = true;
    auto* fc = (AVHWFramesContext*)mFrame->hw_frames_ctx->data;
    auto* hf = (AVVulkanFramesContext*)fc->hwctx;
    auto* vkf = (AVVkFrame*)mFrame->data[0];
    // Direct decode export maps the Vulkan decoder output image directly as a
    // DMA-BUF, avoiding a GPU copy. This requires av_hwframe_map() to work
    // correctly (FFmpeg master / future releases). Controlled by pref
    // media.hardware-video-decoding-vulkan.direct-export.enabled (default off).
    if (hf->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT && vkf &&
        mVulkanDecoder.mGetImageDrmFormatModifierPropertiesEXT &&
        VulkanDirectDecodeExportEnabled()) {
      VkImageDrmFormatModifierPropertiesEXT modProps = {
          VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT};
      VkResult r = mVulkanDecoder.mGetImageDrmFormatModifierPropertiesEXT(
          vkDevCtx->act_dev, vkf->img[0], &modProps);
      mVulkanDecodeUsesDrmModifier = r == VK_SUCCESS;
      FFMPEG_LOG(
          "[VULKAN] DRM modifier query: 0x{:x} -> {}",
          (unsigned long long)(r == VK_SUCCESS ? modProps.drmFormatModifier
                                               : 0),
          mVulkanDecodeUsesDrmModifier ? "direct export" : "copy");
    } else {
      FFMPEG_LOG("[VULKAN] Tiling: OPTIMAL -> copy");
    }
  }

  bool directExport = mVulkanDecodeUsesDrmModifier;

  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
  if (!mVideoFramePool) {
    AVHWFramesContext* context =
        (AVHWFramesContext*)mCodecContext->hw_frames_ctx->data;
    mVideoFramePool =
        MakeUnique<VideoFramePool<LIBAV_VER>>(context->initial_pool_size);
  }

  int32_t vulkanCopyBufIdx = -1;
  AVDRMFrameDescriptor drmDesc = {};
  AVDRMFrameDescriptor* desc = &drmDesc;
  AVFrame* frameForPool = mFrame;
  AVFrame* mappedDrmFrame = nullptr;

  if (directExport) {
    AVFrame* drmFrame = mLib->av_frame_alloc();
    if (!drmFrame) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         RESULT_DETAIL("av_frame_alloc failed"));
    }
    drmFrame->format = AV_PIX_FMT_DRM_PRIME;
    int err = mLib->av_hwframe_map(drmFrame, mFrame, 0);
    if (err < 0) {
      mLib->av_frame_free(&drmFrame);
      directExport = false;
      FFMPEG_LOG(
          "[VULKAN] av_hwframe_map (Vulkan->DRM) failed, falling back to copy");
    } else {
      desc = (AVDRMFrameDescriptor*)drmFrame->data[0];
      frameForPool = drmFrame;
      mappedDrmFrame = drmFrame;
      if (desc->nb_layers == 1) {
        desc->layers[0].format = DRMFormatForColorDepth(mInfo.mColorDepth);
      }
    }
  } else {
    int baseFd = -1;
    size_t totalSize = 0;
    uint32_t yPitch = 0, uvPitch = 0;
    size_t uvOffset = 0;
    constexpr bool kIsCopy = true;
    MediaResult vkCopyResult = mVulkanDecoder.PrepareImageToDRM(
        mFrame, &baseFd, &totalSize, &yPitch, &uvPitch, &uvOffset,
        mVulkanDeviceContext, mVideoFramePool.get(), &vulkanCopyBufIdx,
        kIsCopy);
    if (NS_FAILED(vkCopyResult)) {
      return vkCopyResult;
    }
    constexpr size_t kYOffset = 0;
    uint32_t format = DRMFormatForColorDepth(mInfo.mColorDepth);
    FillDRMDescriptorYUVSingleObject(format, baseFd, totalSize,
                                     mVulkanDecoder.mDrmModifier, yPitch,
                                     uvPitch, kYOffset, uvOffset, &drmDesc);
    desc = &drmDesc;
    frameForPool = mFrame;
  }

  FFMPEG_LOG("[VULKAN] DRM descriptor: nb_objects={}, nb_layers={}, {}",
             desc->nb_objects, desc->nb_layers,
             directExport ? "direct export" : "copy");

  frameForPool->format = AV_PIX_FMT_DRM_PRIME;
  frameForPool->width = static_cast<int>(width);
  frameForPool->height = static_cast<int>(height);
  auto surface = mVideoFramePool->GetVideoFrameSurface(
      *desc, static_cast<int>(width), static_cast<int>(height), mCodecContext,
      frameForPool, mLib);
  if (mappedDrmFrame) {
    mLib->av_frame_free(&mappedDrmFrame);
  }
  if (!surface) {
    FFMPEG_LOG("[VULKAN] GetVideoFrameSurface failed!");
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                       RESULT_DETAIL("Vulkan dmabuf allocation error"));
  }

  if (!directExport && vulkanCopyBufIdx >= 0) {
    surface->SetVulkanCopySlotIndex(vulkanCopyBufIdx);
    if (mVulkanDecoder.mCopyDoneSemFd[vulkanCopyBufIdx] >= 0) {
      int fd = dup(mVulkanDecoder.mCopyDoneSemFd[vulkanCopyBufIdx]);
      if (fd >= 0) {
        const bool useSyncFd = mVulkanDecoder.mSemHandleType ==
                               VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
        surface->GetDMABufSurface()->SetSemaphoreFd(fd, useSyncFd);
      }
    }
  }

  surface->SetYUVColorSpace(GetFrameColorSpace());
  surface->SetColorRange(GetFrameColorRange());
  if (mInfo.mColorPrimaries) {
    surface->SetColorPrimaries(mInfo.mColorPrimaries.value());
  }
  if (mInfo.mTransferFunction) {
    surface->SetTransferFunction(mInfo.mTransferFunction.value());
  }
  surface->SetWPChromaLocation(
      AVChromaLocationToWPChromaLocation(mFrame->chroma_location));
  if (mInfo.mHDRMetadata) {
    surface->SetHDRMetadata(mInfo.mHDRMetadata.value());
  }

  RefPtr<VideoData> vp = VideoData::CreateFromImage(
      mInfo.mDisplay, aOffset, TimeUnit::FromMicroseconds(aPts),
      TimeUnit::FromMicroseconds(aDuration), surface->GetAsImage(),
      IsKeyFrame(mFrame), TimeUnit::FromMicroseconds(mFrame->pkt_dts));

  if (!vp) {
    NS_WARNING("[VULKAN] ERROR: VideoData::CreateFromImage failed!\n");
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                       RESULT_DETAIL("Vulkan image allocation error"));
  }

  aResults.AppendElement(std::move(vp));
  return NS_OK;
}
#  endif

MediaResult FFmpegVideoDecoder<LIBAV_VER>::CreateImageV4L2(
    int64_t aOffset, int64_t aPts, int64_t aDuration,
    MediaDataDecoder::DecodedData& aResults) {
  FFMPEG_LOG("V4L2 Got one frame output with pts={} dts={} duration={}", aPts,
             mFrame->pkt_dts, aDuration);

  AVDRMFrameDescriptor* desc = (AVDRMFrameDescriptor*)mFrame->data[0];
  if (!desc) {
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                       RESULT_DETAIL("Missing DRM PRIME descriptor in frame"));
  }

  // Note that the FDs in desc are owned by FFmpeg and it will reuse them
  // each time the same buffer is dequeued in future.  So we shouldn't close
  // them and so don't setup a clean-up handler for desc.

  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
  if (!mVideoFramePool) {
    // With the V4L2 wrapper codec we can't see the capture buffer pool size.
    // But, this value is only used for deciding when we are running out of
    // free buffers and so should start copying them.  So a rough estimate
    // is sufficient, and the codec defaults to 20 capture buffers.
    mVideoFramePool = MakeUnique<VideoFramePool<LIBAV_VER>>(20);
  }

  auto surface = mVideoFramePool->GetVideoFrameSurface(
      *desc, mFrame->width, mFrame->height, mCodecContext, mFrame, mLib);
  if (!surface) {
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                       RESULT_DETAIL("V4L2 dmabuf allocation error"));
  }
  surface->SetYUVColorSpace(GetFrameColorSpace());
  surface->SetColorRange(GetFrameColorRange());

  RefPtr<VideoData> vp = VideoData::CreateFromImage(
      mInfo.mDisplay, aOffset, TimeUnit::FromMicroseconds(aPts),
      TimeUnit::FromMicroseconds(aDuration), surface->GetAsImage(),
      IsKeyFrame(mFrame), TimeUnit::FromMicroseconds(mFrame->pkt_dts));

  if (!vp) {
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                       RESULT_DETAIL("V4L2 image creation error"));
  }

  aResults.AppendElement(std::move(vp));
  return NS_OK;
}
#endif

RefPtr<MediaDataDecoder::FlushPromise>
FFmpegVideoDecoder<LIBAV_VER>::ProcessFlush() {
  FFMPEG_LOG("ProcessFlush()");
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
#if LIBAVCODEC_VERSION_MAJOR >= 58
  mHasSentDrainPacket = false;
#endif
#if LIBAVCODEC_VERSION_MAJOR < 58
  mPtsContext.Reset();
#endif
#ifdef MOZ_FFMPEG_USE_DURATION_MAP
  mDurationMap.Clear();
#endif
#if defined(MOZ_USE_HWDECODE) && defined(MOZ_WIDGET_GTK)
  if (mVideoFramePool) {
    mVideoFramePool->FlushFFmpegFrames();
  }
#endif
#ifdef MOZ_WIDGET_ANDROID
  ReleaseFramesMediaCodec();
#endif
  mPerformanceRecorder.Record(std::numeric_limits<int64_t>::max());
  return FFmpegDataDecoder::ProcessFlush();
}

#ifdef MOZ_WIDGET_ANDROID
Maybe<MediaDataDecoder::PropertyValue> FFmpegVideoDecoder<
    LIBAV_VER>::GetDecodeProperty(MediaDataDecoder::PropertyName aName) const {
  // If we are using a software decoder, then we aren't subject to platform
  // limits. If we don't have mCodecContext, assume worst case.
  if (mCodecContext) {
    if (const auto* codec = mCodecContext->codec) {
      if (!(codec->capabilities & AV_CODEC_CAP_HARDWARE)) {
        return MediaDataDecoder::GetDecodeProperty(aName);
      }
    }
  }

  // Android has limited amount of output buffers. See Bug 794747.
  static constexpr uint32_t kNumOutputBuffers = 3;
  // SurfaceTexture can have only one current/renderable image at a time.
  // See Bug 1299068
  static constexpr uint32_t kNumCurrentImages = 1;
  switch (aName) {
    case PropertyName::MaxNumVideoBuffers:
      [[fallthrough]];
    case PropertyName::MinNumVideoBuffers:
      return Some(PropertyValue(kNumOutputBuffers));
    case PropertyName::MaxNumCurrentImages:
      return Some(PropertyValue(kNumCurrentImages));
    default:
      return MediaDataDecoder::GetDecodeProperty(aName);
  }
}
#endif

AVCodecID FFmpegVideoDecoder<LIBAV_VER>::GetCodecId(
    const nsACString& aMimeType) {
  if (MP4Decoder::IsH264(aMimeType)) {
    return AV_CODEC_ID_H264;
  }

#if LIBAVCODEC_VERSION_MAJOR >= 55
  if (MP4Decoder::IsHEVC(aMimeType)) {
    return AV_CODEC_ID_HEVC;
  }
#endif

  if (aMimeType.EqualsLiteral("video/x-vnd.on2.vp6")) {
    return AV_CODEC_ID_VP6F;
  }

#if LIBAVCODEC_VERSION_MAJOR >= 54
  if (VPXDecoder::IsVP8(aMimeType)) {
    return AV_CODEC_ID_VP8;
  }
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 55
  if (VPXDecoder::IsVP9(aMimeType)) {
    return AV_CODEC_ID_VP9;
  }
#endif

#if defined(FFMPEG_AV1_DECODE)
  if (AOMDecoder::IsAV1(aMimeType)) {
    return AV_CODEC_ID_AV1;
  }
#endif

  return AV_CODEC_ID_NONE;
}

void FFmpegVideoDecoder<LIBAV_VER>::ProcessShutdown() {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
#ifdef MOZ_WIDGET_ANDROID
  mShouldResumeDrain = false;
  ReleaseFramesMediaCodec();
  if (mMediaCodecDeviceContext) {
    mLib->av_buffer_unref(&mMediaCodecDeviceContext);
  }
#endif

#if defined(MOZ_USE_HWDECODE) && defined(MOZ_WIDGET_GTK)
  mVideoFramePool = nullptr;
#endif
  // Shutdown order for Vulkan hw decode:
  // 1. avcodec_free_context() via ProcessShutdown() — FFmpeg created the
  //    VkDevice (av_hwdevice_ctx_create) and must finish its own teardown
  //    (ff_vk_uninit) before we touch Firefox-owned Vulkan objects.
  // 2. mVulkanDecoder.Cleanup() — uses mDeviceWaitIdle and destroys our
  //    command pools/fences; must run while mVulkanDeviceContext still keeps
  //    the AVHWDeviceContext alive.
  // 3. av_buffer_unref(&mVulkanDeviceContext) — may be the last ref and call
  //    vkDestroyDevice, so it must come after Cleanup().
  FFmpegDataDecoder<LIBAV_VER>::ProcessShutdown();
#if defined(MOZ_USE_HWDECODE) && defined(MOZ_WIDGET_GTK)
  if (IsHardwareAccelerated()) {
#  if LIBAVCODEC_VERSION_MAJOR >= 60 && !defined(FFVPX_VERSION)
    if (mVulkanDecoder.mDevice) {
      mVulkanDecoder.Cleanup();
    }
#  endif
    mLib->av_buffer_unref(&mVAAPIDeviceContext);
    mLib->av_buffer_unref(&mVulkanDeviceContext);
  }
#endif
#ifdef MOZ_ENABLE_D3D11VA
  if (IsHardwareAccelerated()) {
    AVHWDeviceContext* hwctx =
        reinterpret_cast<AVHWDeviceContext*>(mD3D11VADeviceContext->data);
    AVD3D11VADeviceContext* d3d11vactx =
        reinterpret_cast<AVD3D11VADeviceContext*>(hwctx->hwctx);
    d3d11vactx->device = nullptr;
    mLib->av_buffer_unref(&mD3D11VADeviceContext);
    mD3D11VADeviceContext = nullptr;
  }
#endif
}

bool FFmpegVideoDecoder<LIBAV_VER>::IsHardwareAccelerated(
    nsACString& aFailureReason) const {
#if defined(MOZ_USE_HWDECODE) && defined(MOZ_WIDGET_GTK)
  return mUsingV4L2 || !!mVAAPIDeviceContext || !!mVulkanDeviceContext;
#elif defined(MOZ_ENABLE_D3D11VA)
  return !!mD3D11VADeviceContext;
#elif defined(MOZ_WIDGET_ANDROID)
  return !!mMediaCodecDeviceContext;
#else
  return false;
#endif
}

#if defined(MOZ_USE_HWDECODE) && defined(MOZ_WIDGET_GTK)
bool FFmpegVideoDecoder<LIBAV_VER>::IsFormatAccelerated(
    AVCodecID aCodecID) const {
  for (const auto& format : mAcceleratedFormats) {
    if (format == aCodecID) {
      return true;
    }
  }
  return false;
}

// See ffmpeg / vaapi_decode.c how CodecID is mapped to VAProfile.
static const struct {
  enum AVCodecID codec_id;
  VAProfile va_profile;
  char name[100];
} vaapi_profile_map[] = {
#  define MAP(c, v, n) {AV_CODEC_ID_##c, VAProfile##v, n}
    MAP(H264, H264ConstrainedBaseline, "H264ConstrainedBaseline"),
    MAP(H264, H264Main, "H264Main"),
    MAP(H264, H264High, "H264High"),
    MAP(VP8, VP8Version0_3, "VP8Version0_3"),
    MAP(VP9, VP9Profile0, "VP9Profile0"),
    MAP(VP9, VP9Profile2, "VP9Profile2"),
    MAP(AV1, AV1Profile0, "AV1Profile0"),
    MAP(AV1, AV1Profile1, "AV1Profile1"),
    MAP(HEVC, HEVCMain, "HEVCMain"),
    MAP(HEVC, HEVCMain10, "HEVCMain10"),
    MAP(HEVC, HEVCMain10, "HEVCMain12"),
#  undef MAP
};

static AVCodecID VAProfileToCodecID(VAProfile aVAProfile) {
  for (const auto& profile : vaapi_profile_map) {
    if (profile.va_profile == aVAProfile) {
      return profile.codec_id;
    }
  }
  return AV_CODEC_ID_NONE;
}

static const char* VAProfileName(VAProfile aVAProfile) {
  for (const auto& profile : vaapi_profile_map) {
    if (profile.va_profile == aVAProfile) {
      return profile.name;
    }
  }
  return nullptr;
}

// This code is adopted from mpv project va-api routine
// determine_working_formats()
void FFmpegVideoDecoder<LIBAV_VER>::AddAcceleratedFormats(
    nsTArray<AVCodecID>& aCodecList, AVCodecID aCodecID,
    AVVAAPIHWConfig* hwconfig) {
  AVHWFramesConstraints* fc =
      mLib->av_hwdevice_get_hwframe_constraints(mVAAPIDeviceContext, hwconfig);
  if (!fc) {
    FFMPEG_LOG("    failed to retrieve libavutil frame constraints");
    return;
  }
  auto autoRelease =
      MakeScopeExit([&] { mLib->av_hwframe_constraints_free(&fc); });

  bool foundSupportedFormat = false;
  for (int n = 0;
       fc->valid_sw_formats && fc->valid_sw_formats[n] != AV_PIX_FMT_NONE;
       n++) {
#  ifdef MOZ_LOGGING
    char formatDesc[1000];
    FFMPEG_LOG("    codec {} format {}", mLib->avcodec_get_name(aCodecID),
               mLib->av_get_pix_fmt_string(formatDesc, sizeof(formatDesc),
                                           fc->valid_sw_formats[n]));
#  endif
    if (fc->valid_sw_formats[n] == AV_PIX_FMT_NV12 ||
        fc->valid_sw_formats[n] == AV_PIX_FMT_YUV420P ||
        fc->valid_sw_formats[n] == AV_PIX_FMT_P010 ||
#  if LIBAVCODEC_VERSION_MAJOR >= 60
        fc->valid_sw_formats[n] == AV_PIX_FMT_P012 ||
#  endif
        fc->valid_sw_formats[n] == AV_PIX_FMT_P016) {
      foundSupportedFormat = true;
#  ifndef MOZ_LOGGING
      break;
#  endif
    }
  }

  if (!foundSupportedFormat) {
    FFMPEG_LOG("    {} target pixel format is not supported!",
               mLib->avcodec_get_name(aCodecID));
    return;
  }

  if (!aCodecList.Contains(aCodecID)) {
    aCodecList.AppendElement(aCodecID);
  }
}

nsTArray<AVCodecID> FFmpegVideoDecoder<LIBAV_VER>::GetAcceleratedFormats() {
  FFMPEG_LOG("FFmpegVideoDecoder::GetAcceleratedFormats()");

  VAProfile* profiles = nullptr;
  VAEntrypoint* entryPoints = nullptr;

  nsTArray<AVCodecID> supportedHWCodecs(AV_CODEC_ID_NONE);
#  ifdef MOZ_LOGGING
  auto printCodecs = MakeScopeExit([&] {
    FFMPEG_LOG("  Supported accelerated formats:");
    for (unsigned i = 0; i < supportedHWCodecs.Length(); i++) {
      FFMPEG_LOG("      {}", mLib->avcodec_get_name(supportedHWCodecs[i]));
    }
  });
#  endif

  AVVAAPIHWConfig* hwconfig =
      mLib->av_hwdevice_hwconfig_alloc(mVAAPIDeviceContext);
  if (!hwconfig) {
    FFMPEG_LOG("  failed to get AVVAAPIHWConfig");
    return supportedHWCodecs;
  }
  auto autoRelease = MakeScopeExit([&] {
    delete[] profiles;
    delete[] entryPoints;
    mLib->av_freep(&hwconfig);
  });

  int maxProfiles = vaMaxNumProfiles(mDisplay);
  int maxEntryPoints = vaMaxNumEntrypoints(mDisplay);
  if (MOZ_UNLIKELY(maxProfiles <= 0 || maxEntryPoints <= 0)) {
    return supportedHWCodecs;
  }

  profiles = new VAProfile[maxProfiles];
  int numProfiles = 0;
  VAStatus status = vaQueryConfigProfiles(mDisplay, profiles, &numProfiles);
  if (status != VA_STATUS_SUCCESS) {
    FFMPEG_LOG("  vaQueryConfigProfiles() failed {}", vaErrorStr(status));
    return supportedHWCodecs;
  }
  numProfiles = std::min(numProfiles, maxProfiles);

  entryPoints = new VAEntrypoint[maxEntryPoints];
  for (int p = 0; p < numProfiles; p++) {
    VAProfile profile = profiles[p];

    AVCodecID codecID = VAProfileToCodecID(profile);
    if (codecID == AV_CODEC_ID_NONE) {
      continue;
    }

    int numEntryPoints = 0;
    status = vaQueryConfigEntrypoints(mDisplay, profile, entryPoints,
                                      &numEntryPoints);
    if (status != VA_STATUS_SUCCESS) {
      FFMPEG_LOG("  vaQueryConfigEntrypoints() failed: '{}' for profile {}",
                 vaErrorStr(status), (int)profile);
      continue;
    }
    numEntryPoints = std::min(numEntryPoints, maxEntryPoints);

    FFMPEG_LOG("  Profile {}:", VAProfileName(profile));
    for (int e = 0; e < numEntryPoints; e++) {
      VAConfigID config = VA_INVALID_ID;
      status = vaCreateConfig(mDisplay, profile, entryPoints[e], nullptr, 0,
                              &config);
      if (status != VA_STATUS_SUCCESS) {
        FFMPEG_LOG("  vaCreateConfig() failed: '{}' for profile {}",
                   vaErrorStr(status), (int)profile);
        continue;
      }
      hwconfig->config_id = config;
      AddAcceleratedFormats(supportedHWCodecs, codecID, hwconfig);
      vaDestroyConfig(mDisplay, config);
    }
  }

  return supportedHWCodecs;
}

#endif

#ifdef MOZ_ENABLE_D3D11VA
MediaResult FFmpegVideoDecoder<LIBAV_VER>::InitD3D11VADecoder() {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsGPUProcess());
  FFMPEG_LOG("Initialising D3D11VA FFmpeg decoder");
  StaticMutexAutoLock mon(sMutex);

  if (!mImageAllocator || !mImageAllocator->SupportsD3D11()) {
    FFMPEG_LOG("  no KnowsCompositor or it doesn't support D3D11");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  if (mInfo.mColorDepth > gfx::ColorDepth::COLOR_10) {
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("not supported color depth"));
  }

  AVCodec* codec = FindVideoHardwareAVCodec(mLib, mCodecID);
  if (!codec) {
    FFMPEG_LOG("  couldn't find d3d11va decoder for {}",
               AVCodecToString(mCodecID));
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("unable to find codec"));
  }
  FFMPEG_LOG("  codec {} : {}", codec->name, codec->long_name);

  if (!(mCodecContext = mLib->avcodec_alloc_context3(codec))) {
    FFMPEG_LOG("  couldn't init d3d11va ffmpeg context");
    return NS_ERROR_OUT_OF_MEMORY;
  }
  mCodecContext->opaque = this;
  InitHWCodecContext(ContextType::D3D11VA);

  // MOZ_REQUIRES isn't recognized in MakeScopeExit, but InitD3D11VADecoder
  // already locks sMutex at the start, so just escape thread analysis.
  auto releaseResources = MakeScopeExit([&]() MOZ_NO_THREAD_SAFETY_ANALYSIS {
    ReleaseCodecContext();
    if (mD3D11VADeviceContext) {
      AVHWDeviceContext* hwctx =
          reinterpret_cast<AVHWDeviceContext*>(mD3D11VADeviceContext->data);
      AVD3D11VADeviceContext* d3d11vactx =
          reinterpret_cast<AVD3D11VADeviceContext*>(hwctx->hwctx);
      d3d11vactx->device = nullptr;
      mLib->av_buffer_unref(&mD3D11VADeviceContext);
      mD3D11VADeviceContext = nullptr;
    }
    mDXVA2Manager.reset();
  });

  FFMPEG_LOG("  creating device context");
  mD3D11VADeviceContext = mLib->av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
  if (!mD3D11VADeviceContext) {
    FFMPEG_LOG("  av_hwdevice_ctx_alloc failed.");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  nsAutoCString failureReason;
  mDXVA2Manager.reset(
      DXVA2Manager::CreateD3D11DXVA(mImageAllocator, failureReason));
  if (!mDXVA2Manager) {
    FFMPEG_LOG("  failed to create dxva manager.");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  ID3D11Device* device = mDXVA2Manager->GetD3D11Device();
  if (!device) {
    FFMPEG_LOG("  failed to get D3D11 device.");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  AVHWDeviceContext* hwctx = (AVHWDeviceContext*)mD3D11VADeviceContext->data;
  AVD3D11VADeviceContext* d3d11vactx = (AVD3D11VADeviceContext*)hwctx->hwctx;
  d3d11vactx->device = device;

  if (mLib->av_hwdevice_ctx_init(mD3D11VADeviceContext) < 0) {
    FFMPEG_LOG("  av_hwdevice_ctx_init failed.");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  mCodecContext->hw_device_ctx = mLib->av_buffer_ref(mD3D11VADeviceContext);
  MediaResult ret = AllocateExtraData();
  if (NS_FAILED(ret)) {
    FFMPEG_LOG("  failed to allocate extradata.");
    return ret;
  }

  if (mLib->avcodec_open2(mCodecContext, codec, nullptr) < 0) {
    FFMPEG_LOG("  avcodec_open2 failed for d3d11va decoder");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  FFMPEG_LOG("  D3D11VA FFmpeg init successful");
  releaseResources.release();
  return NS_OK;
}

MediaResult FFmpegVideoDecoder<LIBAV_VER>::CreateImageD3D11(
    int64_t aOffset, int64_t aPts, int64_t aDuration,
    MediaDataDecoder::DecodedData& aResults) {
  MOZ_DIAGNOSTIC_ASSERT(mFrame);
  MOZ_DIAGNOSTIC_ASSERT(mDXVA2Manager);

  gfx::TransferFunction transferFunction =
      mInfo.mTransferFunction.refOr(gfx::TransferFunction::BT709);
  bool isHDR = transferFunction == gfx::TransferFunction::PQ ||
               transferFunction == gfx::TransferFunction::HLG;
  HRESULT hr = mDXVA2Manager->ConfigureForSize(
      GetSurfaceFormat(), GetFrameColorSpace(), GetFrameColorRange(),
      mInfo.mColorDepth,
      mInfo.mTransferFunction.refOr(gfx::TransferFunction::BT709),
      mInfo.mHDRMetadata, mFrame->width, mFrame->height);
  if (FAILED(hr)) {
    nsPrintfCString msg("Failed to configure DXVA2Manager, hr=%lx", hr);
    FFMPEG_LOG("{}", msg.get());
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR, msg);
  }

  if (!mFrame->data[0]) {
    nsPrintfCString msg("Frame data shouldn't be null!");
    FFMPEG_LOG("{}", msg.get());
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR, msg);
  }

  ID3D11Resource* resource = reinterpret_cast<ID3D11Resource*>(mFrame->data[0]);
  RefPtr<ID3D11Texture2D> texture;
  hr = resource->QueryInterface(
      static_cast<ID3D11Texture2D**>(getter_AddRefs(texture)));
  if (FAILED(hr)) {
    nsPrintfCString msg("Failed to get ID3D11Texture2D, hr=%lx", hr);
    FFMPEG_LOG("{}", msg.get());
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR, msg);
  }

  D3D11_TEXTURE2D_DESC desc;
  texture->GetDesc(&desc);

  auto format = [&]() {
    if (desc.Format == DXGI_FORMAT_P010) {
      return gfx::SurfaceFormat::P010;
    }
    if (desc.Format == DXGI_FORMAT_P016) {
      return gfx::SurfaceFormat::P016;
    }
    if (isHDR) {
      return gfx::SurfaceFormat::P010;
    }
    MOZ_ASSERT(desc.Format == DXGI_FORMAT_NV12);
    return gfx::SurfaceFormat::NV12;
  }();

  RefPtr<Image> image;
  gfx::IntRect pictureRegion =
      mInfo.ScaledImageRect(mFrame->width, mFrame->height);
  UINT index = (uintptr_t)mFrame->data[1];

  // TODO(https://bugzilla.mozilla.org/show_bug.cgi?id=2008886)
  // Currently the zero-copy path supports NV12 but not P010 so it can't do HDR
  // yet, this can be implemented in future.
  if (format == gfx::SurfaceFormat::NV12 && CanUseZeroCopyVideoFrame()) {
    mNumOfHWTexturesInUse++;
    FFMPEGV_LOG("CreateImageD3D11, zero copy, index={} (texInUse={}), isHDR={}",
                index, mNumOfHWTexturesInUse.load(), (unsigned int)isHDR);
    hr = mDXVA2Manager->WrapTextureWithImage(
        new D3D11TextureWrapper(
            mFrame, mLib, texture, format, index,
            [self = RefPtr<FFmpegVideoDecoder>(this), this]() {
              MOZ_ASSERT(mNumOfHWTexturesInUse > 0);
              mNumOfHWTexturesInUse--;
            }),
        pictureRegion, getter_AddRefs(image));
  } else {
    FFMPEGV_LOG("CreateImageD3D11, copy output to a shared texture, isHDR={}",
                (unsigned int)isHDR);
    hr = mDXVA2Manager->CopyToImage(texture, index, pictureRegion,
                                    getter_AddRefs(image));
  }
  if (FAILED(hr)) {
    nsPrintfCString msg("Failed to create a D3D image");
    FFMPEG_LOG("{}", msg.get());
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR, msg);
  }
  MOZ_ASSERT(image);

  RefPtr<VideoData> v = VideoData::CreateFromImage(
      mInfo.mDisplay, aOffset, TimeUnit::FromMicroseconds(aPts),
      TimeUnit::FromMicroseconds(aDuration), image, IsKeyFrame(mFrame),
      TimeUnit::FromMicroseconds(mFrame->pkt_dts));
  if (!v) {
    nsPrintfCString msg("D3D image allocation error");
    FFMPEG_LOG("{}", msg.get());
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR, msg);
  }
  aResults.AppendElement(std::move(v));
  return NS_OK;
}

bool FFmpegVideoDecoder<LIBAV_VER>::CanUseZeroCopyVideoFrame() const {
  // When zero-copy is available, we use a hybrid approach that combines
  // zero-copy and texture copying. This prevents scenarios where all
  // zero-copy frames remain unreleased, which could block ffmpeg from
  // allocating new textures for subsequent frames. Zero-copy should only be
  // used when there is sufficient space available in the texture pool.
  return gfx::gfxVars::HwDecodedVideoZeroCopy() && mImageAllocator &&
         mImageAllocator->UsingHardwareWebRender() && mDXVA2Manager &&
         mDXVA2Manager->SupportsZeroCopyNV12Texture() &&
         mNumOfHWTexturesInUse <= EXTRA_HW_FRAMES / 2;
}
#endif

#ifdef MOZ_WIDGET_ANDROID
#  ifdef USING_MOZFFVPX
MediaResult FFmpegVideoDecoder<LIBAV_VER>::AllocateExtraData() {
  if (!mExtraData) {
    FFMPEG_LOG("  decoder has no extradata");
    mCodecContext->extradata_size = 0;
    return NS_OK;
  }

  switch (mCodecID) {
    case AV_CODEC_ID_H264:
      return AllocateH264ExtraData();
    case AV_CODEC_ID_HEVC:
      return AllocateHEVCExtraData();
    default:
      break;
  }

  return FFmpegDataDecoder<LIBAV_VER>::AllocateExtraData();
}

MediaResult FFmpegVideoDecoder<LIBAV_VER>::AllocateH264ExtraData() {
  // This will do any necessary reordering to place SPS first, then PPS.
  size_t spsLength = 0;
  RefPtr<MediaByteBuffer> extradata =
      AnnexB::ConvertAVCCExtraDataToAnnexB(mExtraData, &spsLength);
  if (!extradata || extradata->IsEmpty() || spsLength == 0) {
    FFMPEG_LOG("  H264 extradata is missing SPS/PPS");
    return NS_OK;
  }

  MediaResult rv = AssignCodecContextExtraData(extradata);
  if (NS_FAILED(rv)) {
    return rv;
  }
  mCodecContext->moz_extradata_offset = AssertedCast<int>(spsLength);
  FFMPEG_LOG("  extracted {} bytes of H264 extradata", extradata->Length());
  return NS_OK;
}

MediaResult FFmpegVideoDecoder<LIBAV_VER>::AllocateHEVCExtraData() {
  // This will not reorder, but we are supposed to receive this in the correct
  // order: SPS, PPS, then VPS.
  RefPtr<MediaByteBuffer> extradata =
      AnnexB::ConvertHVCCExtraDataToAnnexB(mExtraData);
  if (!extradata || extradata->IsEmpty()) {
    FFMPEG_LOG("  HEVC extradata is missing SPS/PPS/VPS");
    return NS_OK;
  }

  MediaResult rv = AssignCodecContextExtraData(extradata);
  if (NS_FAILED(rv)) {
    return rv;
  }
  FFMPEG_LOG("  extracted {} bytes of HEVC extradata", extradata->Length());
  return NS_OK;
}
#  endif

MediaResult FFmpegVideoDecoder<LIBAV_VER>::InitMediaCodecDecoder() {
  FFMPEG_LOG("Initialising MediaCodec FFmpeg decoder");
  StaticMutexAutoLock mon(sMutex);

  if (StaticPrefs::media_ffvpx_hw_minimal() && mCodecID != AV_CODEC_ID_H264 &&
      mCodecID != AV_CODEC_ID_HEVC) {
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("codec not allowed when minimal"));
  }

  if (mInfo.mColorDepth > gfx::ColorDepth::COLOR_10) {
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("not supported color depth"));
  }

  AVCodec* codec =
      FindHardwareAVCodec(mLib, mCodecID, AV_HWDEVICE_TYPE_MEDIACODEC);
  if (!codec) {
    FFMPEG_LOG("  couldn't find MediaCodec decoder for {}",
               AVCodecToString(mCodecID));
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("unable to find codec"));
  }
  FFMPEG_LOG("  codec {} : {}", codec->name, codec->long_name);

  if (!(mCodecContext = mLib->avcodec_alloc_context3(codec))) {
    FFMPEG_LOG("  couldn't alloc_context3 for MediaCodec");
    return MediaResult(NS_ERROR_OUT_OF_MEMORY,
                       RESULT_DETAIL("unable to alloc codec context"));
  }
  mCodecContext->opaque = this;
  InitHWCodecContext(ContextType::MediaCodec);

  // MOZ_REQUIRES isn't recognized in MakeScopeExit, but InitMediaCodecDecoder
  // already locks sMutex at the start, so just escape thread analysis.
  auto releaseResources = MakeScopeExit([&]() MOZ_NO_THREAD_SAFETY_ANALYSIS {
    ReleaseCodecContext();
    if (mMediaCodecDeviceContext) {
      mLib->av_buffer_unref(&mMediaCodecDeviceContext);
    }
  });

  FFMPEG_LOG("  creating device context");
  mMediaCodecDeviceContext =
      mLib->av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_MEDIACODEC);
  if (!mMediaCodecDeviceContext) {
    FFMPEG_LOG("  av_hwdevice_ctx_alloc failed.");
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("unable to alloc hwdevice context"));
  }

  AVHWDeviceContext* hwctx = (AVHWDeviceContext*)mMediaCodecDeviceContext->data;
  AVMediaCodecDeviceContext* mediacodecctx =
      (AVMediaCodecDeviceContext*)hwctx->hwctx;

  if (XRE_IsGPUProcess() &&
      gfx::gfxVars::UseAImageReaderVideoGpuProcessAndroid()) {
    MOZ_ASSERT(!mAndroidImageReader);
    mAndroidImageReader = layers::AndroidImageReader::Create();
  }

  if (mAndroidImageReader) {
    auto* window = mAndroidImageReader->GetANativeWindow();
    MOZ_ASSERT(window);
    if (!window) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         RESULT_DETAIL("unable to acquire ANativeWindow"));
    }

    JNIEnv* const env = jni::GetEnvForThread();
    jobject surface = ANativeWindow_toSurface(env, window);
    if (!surface) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         RESULT_DETAIL("unable to acquire Java surface"));
    }

    mImageReaderSurface = java::sdk::Surface::LocalRef::Adopt(env, surface);

    if (!mImageReaderSurface) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         RESULT_DETAIL("unable to acquire Java surface"));
    }

    mediacodecctx->surface = mImageReaderSurface.Get();
    // Acquire a reference on the ANativeWindow.
    // ANativeWindow_release() will be called on uninit the native_window.
    ANativeWindow_acquire(window);
    mediacodecctx->native_window = window;
    mediacodecctx->create_window = 0;  // default -- useful when encoding?
  } else {
    mSurfaceTextureSurface =
        java::GeckoSurface::LocalRef(java::SurfaceAllocator::AcquireSurface(
            mInfo.mImage.width, mInfo.mImage.height, false));
    if (!mSurfaceTextureSurface) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         RESULT_DETAIL("unable to acquire Java surface"));
    }

    mSurfaceHandle = mSurfaceTextureSurface->GetHandle();

    JNIEnv* const env = jni::GetEnvForThread();
    ANativeWindow* native_window = ANativeWindow_fromSurface(
        env, mSurfaceTextureSurface->GetSurface().Get());

    mediacodecctx->surface = mSurfaceTextureSurface->GetSurface().Get();
    mediacodecctx->native_window = native_window;
    mediacodecctx->create_window = 0;  // default -- useful when encoding?
  }

  if (mLib->av_hwdevice_ctx_init(mMediaCodecDeviceContext) < 0) {
    FFMPEG_LOG("  av_hwdevice_ctx_init failed.");
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("unable to init hwdevice context"));
  }

  mCodecContext->hw_device_ctx = mLib->av_buffer_ref(mMediaCodecDeviceContext);

  MediaResult ret = AllocateExtraData();
  if (NS_FAILED(ret)) {
    FFMPEG_LOG("  failed to allocate extradata.");
    return ret;
  }

#  ifdef USING_MOZFFVPX
  ret = MaybeAttachCDM();
  if (NS_FAILED(ret)) {
    FFMPEG_LOG("  failed to attach CDM.");
    return ret;
  }
#  endif

  if (mLib->avcodec_open2(mCodecContext, codec, nullptr) < 0) {
    FFMPEG_LOG("  avcodec_open2 failed for MediaCodec decoder");
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("avcodec_open2 failed for MediaCodec"));
  }

  FFMPEG_LOG("  MediaCodec FFmpeg init successful");
  releaseResources.release();
  return NS_OK;
}

MediaResult FFmpegVideoDecoder<LIBAV_VER>::CreateImageMediaCodec(
    int64_t aOffset, int64_t aPts, int64_t aTimecode, int64_t aDuration,
    MediaDataDecoder::DecodedData& aResults) {
  MOZ_DIAGNOSTIC_ASSERT(mFrame);

  if (!mFrame || !mFrame->buf[0]) {
    FFMPEG_LOG("  CreateImageMediaCodec failed, no valid frame");
    return NS_ERROR_INVALID_ARG;
  }

  AVFrame* frame = mLib->av_frame_clone(mFrame);
  if (!frame) {
    FFMPEG_LOG("  CreateImageMediaCodec failed, cannot clone frame");
    return NS_ERROR_INVALID_ARG;
  }

  class CompositeListener final
      : public layers::SurfaceTextureImage::SetCurrentCallback {
   public:
    explicit CompositeListener(FFmpegVideoDecoder<LIBAV_VER>* aDecoder)
        : mDecoder(aDecoder) {}

    ~CompositeListener() override { MaybeRelease(/* aRender */ false); }

    bool operator()(bool aRender) override { return MaybeRelease(aRender); }

    bool MaybeRelease(bool aRender) {
      if (!mDecoder) {
        return false;
      }

      if (mDecoder->ReleaseFrameMediaCodec(this, aRender)) {
        mDecoder->QueueResumeDrain();
        // Ensure we don't release the same frame multiple times
        mDecoder = nullptr;
        return true;
      }
      mDecoder = nullptr;
      return false;
    }

    RefPtr<FFmpegVideoDecoder<LIBAV_VER>> mDecoder;
  };

  RefPtr<layers::Image> img;
  if (mAndroidImageReader) {
    auto listener = MakeUnique<CompositeListener>(this);
    mFrameMap.Insert(listener.get(), frame);
    auto readerImage = MakeRefPtr<layers::AndroidImageReaderImage>(
        mAndroidImageReader->mImageReaderId,
        gfx::IntSize(mFrame->width, mFrame->height), mInfo.HasAlpha());
    readerImage->RegisterSetCurrentCallback(std::move(listener));
    mAndroidImageReader->RegisterReaderImage(readerImage);
    img = readerImage.forget();
  } else {
    img = MakeRefPtr<layers::SurfaceTextureImage>(
        mSurfaceHandle, gfx::IntSize(mFrame->width, mFrame->height),
        false /* NOT continuous */, gl::OriginPos::BottomLeft, mInfo.HasAlpha(),
        false /* force color space stuff */,
        /* aTransformOverride */ Nothing());
    auto listener = MakeUnique<CompositeListener>(this);
    mFrameMap.Insert(listener.get(), frame);
    img->AsSurfaceTextureImage()->RegisterSetCurrentCallback(
        std::move(listener));
  }

  RefPtr<VideoData> v = VideoData::CreateFromImage(
      {mFrame->width, mFrame->height}, aOffset,
      TimeUnit::FromMicroseconds(aPts), TimeUnit::FromMicroseconds(aDuration),
      img.forget().downcast<layers::Image>(), mFrame->flags & AV_FRAME_FLAG_KEY,
      TimeUnit::FromMicroseconds(aTimecode));

  aResults.AppendElement(std::move(v));
  return NS_OK;
}

bool FFmpegVideoDecoder<LIBAV_VER>::ReleaseFrameMediaCodec(void* aKey,
                                                           bool aRender) {
  // Note that we want to hold the lock while releasing the buffer. This will
  // ensure that the underlying AMediaCodec decoder is not flushed or stopped on
  // the task queue while we are in the middle of releasing on the
  // MediaSupervisor thread.
  return mFrameMap.Take(aKey, [&](AVFrame* aFrame) {
    FFMPEG_LOG("Release MediaCodec frame {}, render={}", fmt::ptr(aFrame),
               aRender);
    for (int i = 0; i < AV_NUM_DATA_POINTERS; ++i) {
      if (aFrame->data[i]) {
        mLib->av_mediacodec_release_buffer((AVMediaCodecBuffer*)aFrame->data[i],
                                           aRender ? 1 : 0);
      }
    }
    mLib->av_frame_free(&aFrame);
  });
}

void FFmpegVideoDecoder<LIBAV_VER>::ReleaseFramesMediaCodec() {
  // Since we take the lock to explicitly release/clear, we know that we can't
  // be attempting to release on the MediaSupervisor thread. Freeing
  // automatically calls AMediaCodec_releaseOutputBuffer with render=0.
  mFrameMap.Clear([&](void*, AVFrame* aFrame) {
    FFMPEG_LOG("Flush/shutdown release MediaCodec frame {}", fmt::ptr(aFrame));
    mLib->av_frame_free(&aFrame);
  });
}
#endif  // MOZ_WIDGET_ANDROID

#if MOZ_USE_HWDECODE
/* static */ AVCodec* FFmpegVideoDecoder<LIBAV_VER>::FindVideoHardwareAVCodec(
    const FFmpegLibWrapper* aLib, AVCodecID aCodec,
    AVHWDeviceType aDeviceType) {
#  ifdef MOZ_WIDGET_GTK
  if (aDeviceType == AV_HWDEVICE_TYPE_NONE) {
    switch (aCodec) {
      case AV_CODEC_ID_H264:
        return aLib->avcodec_find_decoder_by_name("h264_v4l2m2m");
      case AV_CODEC_ID_VP8:
        return aLib->avcodec_find_decoder_by_name("vp8_v4l2m2m");
      case AV_CODEC_ID_VP9:
        return aLib->avcodec_find_decoder_by_name("vp9_v4l2m2m");
      case AV_CODEC_ID_HEVC:
        return aLib->avcodec_find_decoder_by_name("hevc_v4l2m2m");
      case AV_CODEC_ID_AV1:
        return aLib->avcodec_find_decoder_by_name("av1_v4l2m2m");
      default:
        return nullptr;
    }
  }
#  endif
  return FindHardwareAVCodec(aLib, aCodec, aDeviceType);
}
#endif

}  // namespace mozilla
