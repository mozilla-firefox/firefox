/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FFmpegVideoEncoder.h"

#include <aom/aomcx.h>

#include <algorithm>

#include "AnnexB.h"
#include "BufferReader.h"
#include "EncoderConfig.h"
#include "FFmpegEncoderModule.h"
#include "FFmpegLog.h"
#include "FFmpegRuntimeLinker.h"
#include "FFmpegUtils.h"
#include "H264.h"
#include "ImageContainer.h"
#include "ImageConversion.h"
#include "libavutil/error.h"
#include "libavutil/pixfmt.h"
#include "libyuv.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/dom/ImageBitmapBinding.h"
#include "mozilla/dom/ImageUtils.h"
#include "mozilla/dom/VideoFrameBinding.h"
#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/gfx/gfxVars.h"
#endif
#include "nsStringFwd.h"
#include "nsXULAppAPI.h"

// The ffmpeg namespace is introduced to avoid the PixelFormat's name conflicts
// with MediaDataEncoder::PixelFormat in MediaDataEncoder class scope.
namespace ffmpeg {

// TODO: WebCodecs' I420A should map to MediaDataEncoder::PixelFormat and then
// to AV_PIX_FMT_YUVA420P here.
#if LIBAVCODEC_VERSION_MAJOR < 54
using FFmpegPixelFormat = enum PixelFormat;
const FFmpegPixelFormat FFMPEG_PIX_FMT_NONE = FFmpegPixelFormat::PIX_FMT_NONE;
const FFmpegPixelFormat FFMPEG_PIX_FMT_RGBA = FFmpegPixelFormat::PIX_FMT_RGBA;
const FFmpegPixelFormat FFMPEG_PIX_FMT_BGRA = FFmpegPixelFormat::PIX_FMT_BGRA;
const FFmpegPixelFormat FFMPEG_PIX_FMT_RGB24 = FFmpegPixelFormat::PIX_FMT_RGB24;
const FFmpegPixelFormat FFMPEG_PIX_FMT_BGR24 = FFmpegPixelFormat::PIX_FMT_BGR24;
const FFmpegPixelFormat FFMPEG_PIX_FMT_YUV444P =
    FFmpegPixelFormat::PIX_FMT_YUV444P;
const FFmpegPixelFormat FFMPEG_PIX_FMT_YUV422P =
    FFmpegPixelFormat::PIX_FMT_YUV422P;
const FFmpegPixelFormat FFMPEG_PIX_FMT_YUV420P =
    FFmpegPixelFormat::PIX_FMT_YUV420P;
const FFmpegPixelFormat FFMPEG_PIX_FMT_NV12 = FFmpegPixelFormat::PIX_FMT_NV12;
const FFmpegPixelFormat FFMPEG_PIX_FMT_NV21 = FFmpegPixelFormat::PIX_FMT_NV21;
#else
using FFmpegPixelFormat = enum AVPixelFormat;
const FFmpegPixelFormat FFMPEG_PIX_FMT_NONE =
    FFmpegPixelFormat::AV_PIX_FMT_NONE;
const FFmpegPixelFormat FFMPEG_PIX_FMT_RGBA =
    FFmpegPixelFormat::AV_PIX_FMT_RGBA;
const FFmpegPixelFormat FFMPEG_PIX_FMT_BGRA =
    FFmpegPixelFormat::AV_PIX_FMT_BGRA;
const FFmpegPixelFormat FFMPEG_PIX_FMT_RGB24 =
    FFmpegPixelFormat::AV_PIX_FMT_RGB24;
const FFmpegPixelFormat FFMPEG_PIX_FMT_BGR24 =
    FFmpegPixelFormat::AV_PIX_FMT_BGR24;
const FFmpegPixelFormat FFMPEG_PIX_FMT_YUV444P =
    FFmpegPixelFormat::AV_PIX_FMT_YUV444P;
const FFmpegPixelFormat FFMPEG_PIX_FMT_YUV422P =
    FFmpegPixelFormat::AV_PIX_FMT_YUV422P;
const FFmpegPixelFormat FFMPEG_PIX_FMT_YUV420P =
    FFmpegPixelFormat::AV_PIX_FMT_YUV420P;
const FFmpegPixelFormat FFMPEG_PIX_FMT_NV12 =
    FFmpegPixelFormat::AV_PIX_FMT_NV12;
const FFmpegPixelFormat FFMPEG_PIX_FMT_NV21 =
    FFmpegPixelFormat::AV_PIX_FMT_NV21;
#endif

static const char* GetPixelFormatString(FFmpegPixelFormat aFormat) {
  switch (aFormat) {
    case FFMPEG_PIX_FMT_NONE:
      return "none";
    case FFMPEG_PIX_FMT_RGBA:
      return "packed RGBA 8:8:8:8 (32bpp, RGBARGBA...)";
    case FFMPEG_PIX_FMT_BGRA:
      return "packed BGRA 8:8:8:8 (32bpp, BGRABGRA...)";
    case FFMPEG_PIX_FMT_RGB24:
      return "packed RGB 8:8:8 (24bpp, RGBRGB...)";
    case FFMPEG_PIX_FMT_BGR24:
      return "packed RGB 8:8:8 (24bpp, BGRBGR...)";
    case FFMPEG_PIX_FMT_YUV444P:
      return "planar YUV 4:4:4 (24bpp, 1 Cr & Cb sample per 1x1 Y samples)";
    case FFMPEG_PIX_FMT_YUV422P:
      return "planar YUV 4:2:2 (16bpp, 1 Cr & Cb sample per 2x1 Y samples)";
    case FFMPEG_PIX_FMT_YUV420P:
      return "planar YUV 4:2:0 (12bpp, 1 Cr & Cb sample per 2x2 Y samples)";
    case FFMPEG_PIX_FMT_NV12:
      return "planar YUV 4:2:0 (12bpp, 1 interleaved UV components per 1x1 Y "
             "samples)";
    case FFMPEG_PIX_FMT_NV21:
      return "planar YUV 4:2:0 (12bpp, 1 interleaved VU components per 1x1 Y "
             "samples)";
    default:
      break;
  }
  MOZ_ASSERT_UNREACHABLE("Unsupported pixel format");
  return "unsupported";
}

};  // namespace ffmpeg

namespace mozilla {

struct H264Setting {
  int mValue;
  nsCString mString;
};

struct H264LiteralSetting {
  int mValue;
  nsLiteralCString mString;
  H264Setting get() const { return {mValue, mString.AsString()}; }
};

#if LIBAVCODEC_VERSION_MAJOR < 62
static constexpr H264LiteralSetting H264Profiles[]{
    {FF_PROFILE_H264_BASELINE, "baseline"_ns},
    {FF_PROFILE_H264_MAIN, "main"_ns},
    {FF_PROFILE_H264_EXTENDED, ""_ns},
    {FF_PROFILE_H264_HIGH, "high"_ns}};
#else
static constexpr H264LiteralSetting H264Profiles[]{
    {AV_PROFILE_H264_BASELINE, "baseline"_ns},
    {AV_PROFILE_H264_MAIN, "main"_ns},
    {AV_PROFILE_H264_EXTENDED, ""_ns},
    {AV_PROFILE_H264_HIGH, "high"_ns}};
#endif

static AVColorRange ToAVColorRange(const gfx::ColorRange& aRange) {
  return aRange == gfx::ColorRange::FULL ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
}

// Older libavcodec lacks some AVCOL_* constants; those map to unspecified.
static AVColorSpace ToAVColorSpace(const gfx::YUVColorSpace& aMatrix) {
  switch (aMatrix) {
    case gfx::YUVColorSpace::BT601:
      return AVCOL_SPC_SMPTE170M;
    case gfx::YUVColorSpace::BT709:
      return AVCOL_SPC_BT709;
#if LIBAVCODEC_VERSION_MAJOR >= 57
    case gfx::YUVColorSpace::BT2020:
      return AVCOL_SPC_BT2020_NCL;
#endif
    case gfx::YUVColorSpace::Identity:
      return AVCOL_SPC_RGB;
    default:
      return AVCOL_SPC_UNSPECIFIED;
  }
}

static AVColorPrimaries ToAVColorPrimaries(const gfx::ColorSpace2& aPrimaries) {
  switch (aPrimaries) {
    case gfx::ColorSpace2::SRGB:
    case gfx::ColorSpace2::BT709:
      return AVCOL_PRI_BT709;
#if LIBAVCODEC_VERSION_MAJOR >= 58
    case gfx::ColorSpace2::DISPLAY_P3:
      return AVCOL_PRI_SMPTE432;
#endif
    case gfx::ColorSpace2::BT601_525:
      return AVCOL_PRI_SMPTE170M;
#if LIBAVCODEC_VERSION_MAJOR >= 57
    case gfx::ColorSpace2::BT2020:
      return AVCOL_PRI_BT2020;
#endif
    default:
      return AVCOL_PRI_UNSPECIFIED;
  }
}

static AVColorTransferCharacteristic ToAVColorTransfer(
    const gfx::TransferFunction& aTransfer) {
  switch (aTransfer) {
    case gfx::TransferFunction::BT709:
      return AVCOL_TRC_BT709;
#if LIBAVCODEC_VERSION_MAJOR >= 57
    case gfx::TransferFunction::SRGB:
      return AVCOL_TRC_IEC61966_2_1;
    case gfx::TransferFunction::LINEAR:
      return AVCOL_TRC_LINEAR;
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 58
    case gfx::TransferFunction::PQ:
      return AVCOL_TRC_SMPTE2084;
    case gfx::TransferFunction::HLG:
      return AVCOL_TRC_ARIB_STD_B67;
#endif
    default:
      return AVCOL_TRC_UNSPECIFIED;
  }
}

static Maybe<H264Setting> GetH264Profile(const H264_PROFILE& aProfile) {
  switch (aProfile) {
    case H264_PROFILE::H264_PROFILE_UNKNOWN:
      return Nothing();
    case H264_PROFILE::H264_PROFILE_BASE:
      return Some(H264Profiles[0].get());
    case H264_PROFILE::H264_PROFILE_MAIN:
      return Some(H264Profiles[1].get());
    case H264_PROFILE::H264_PROFILE_EXTENDED:
      return Some(H264Profiles[2].get());
    case H264_PROFILE::H264_PROFILE_HIGH:
      return Some(H264Profiles[3].get());
    default:
      break;
  }
  MOZ_ASSERT_UNREACHABLE("undefined profile");
  return Nothing();
}

static Maybe<H264Setting> GetH264Level(const H264_LEVEL& aLevel) {
  int val = static_cast<int>(aLevel);
  nsFmtCString str("{}", val);
  str.Insert('.', 1);
  return Some(H264Setting{val, std::move(str)});
}

struct VPXSVCAppendix {
  uint8_t mLayeringMode;
};

struct SVCLayerSettings {
  using CodecAppendix = Variant<VPXSVCAppendix, aom_svc_params_t>;
  size_t mNumberSpatialLayers;
  size_t mNumberTemporalLayers;
  uint8_t mPeriodicity;
  nsTArray<uint8_t> mLayerIds;
  // libvpx: ts_rate_decimator, libaom: framerate_factor
  nsTArray<uint8_t> mRateDecimators;
  nsTArray<uint32_t> mTargetBitrates;
  Maybe<CodecAppendix> mCodecAppendix;
};

static SVCLayerSettings GetSVCLayerSettings(CodecType aCodec,
                                            const ScalabilityMode& aMode,
                                            uint32_t aBitPerSec) {
  // TODO: Apply more sophisticated bitrate allocation, like SvcRateAllocator:
  // https://searchfox.org/mozilla-central/rev/3bd65516eb9b3a9568806d846ba8c81a9402a885/third_party/libwebrtc/modules/video_coding/svc/svc_rate_allocator.h#26

  size_t layers = 0;
  const uint32_t kbps = aBitPerSec / 1000;  // ts_target_bitrate requies kbps.

  uint8_t periodicity;
  nsTArray<uint8_t> layerIds;
  nsTArray<uint8_t> rateDecimators;
  nsTArray<uint32_t> bitrates;

  Maybe<SVCLayerSettings::CodecAppendix> appendix;

  if (aMode == ScalabilityMode::L1T2) {
    // Two temporal layers. 0-1...
    //
    // Frame pattern:
    // Layer 0: |0| |2| |4| |6| |8|
    // Layer 1: | |1| |3| |5| |7| |

    layers = 2;

    // 2 frames per period.
    periodicity = 2;

    // Assign layer ids.
    layerIds.AppendElement(0);
    layerIds.AppendElement(1);

    // Set rate decimators.
    rateDecimators.AppendElement(2);
    rateDecimators.AppendElement(1);

    // Bitrate allocation: L0 - 60%, L1 - 40%.
    bitrates.AppendElement(kbps * 3 / 5);
    bitrates.AppendElement(kbps);

    if (aCodec == CodecType::VP8 || aCodec == CodecType::VP9) {
      appendix.emplace(VPXSVCAppendix{
          .mLayeringMode = 2 /* VP9E_TEMPORAL_LAYERING_MODE_0101 */
      });
    }
  } else {
    MOZ_ASSERT(aMode == ScalabilityMode::L1T3);
    // Three temporal layers. 0-2-1-2...
    //
    // Frame pattern:
    // Layer 0: |0| | | |4| | | |8| |  |  |12|
    // Layer 1: | | |2| | | |6| | | |10|  |  |
    // Layer 2: | |1| |3| |5| |7| |9|  |11|  |

    layers = 3;

    // 4 frames per period
    periodicity = 4;

    // Assign layer ids.
    layerIds.AppendElement(0);
    layerIds.AppendElement(2);
    layerIds.AppendElement(1);
    layerIds.AppendElement(2);

    // Set rate decimators.
    rateDecimators.AppendElement(4);
    rateDecimators.AppendElement(2);
    rateDecimators.AppendElement(1);

    // Bitrate allocation: L0 - 50%, L1 - 20%, L2 - 30%.
    bitrates.AppendElement(kbps / 2);
    bitrates.AppendElement(kbps * 7 / 10);
    bitrates.AppendElement(kbps);

    if (aCodec == CodecType::VP8 || aCodec == CodecType::VP9) {
      appendix.emplace(VPXSVCAppendix{
          .mLayeringMode = 3 /* VP9E_TEMPORAL_LAYERING_MODE_0212 */
      });
    }
  }

  MOZ_ASSERT(layers == bitrates.Length(),
             "Bitrate must be assigned to each layer");
  return SVCLayerSettings{1,
                          layers,
                          periodicity,
                          std::move(layerIds),
                          std::move(rateDecimators),
                          std::move(bitrates),
                          appendix};
}

static bool CodecManagesTemporalIds(CodecType aCodec) {
  return aCodec == CodecType::VP8 || aCodec == CodecType::VP9;
}

void FFmpegVideoEncoder<LIBAV_VER>::SVCInfo::UpdateTemporalLayerId() {
  MOZ_ASSERT(!mTemporalLayerIds.IsEmpty());
  mCurrentIndex = (mCurrentIndex + 1) % mTemporalLayerIds.Length();
}

uint8_t FFmpegVideoEncoder<LIBAV_VER>::SVCInfo::CurrentTemporalLayerId() {
  MOZ_ASSERT(!mTemporalLayerIds.IsEmpty());
  return mTemporalLayerIds[mCurrentIndex];
}

void FFmpegVideoEncoder<LIBAV_VER>::SVCInfo::ResetTemporalLayerId() {
  MOZ_ASSERT(!mTemporalLayerIds.IsEmpty());
  mCurrentIndex = 0;
}

FFmpegVideoEncoder<LIBAV_VER>::FFmpegVideoEncoder(
    const FFmpegLibWrapper* aLib, AVCodecID aCodecID,
    const RefPtr<TaskQueue>& aTaskQueue, const EncoderConfig& aConfig)
    : FFmpegDataEncoder(aLib, aCodecID, aTaskQueue, aConfig) {}

RefPtr<MediaDataEncoder::InitPromise> FFmpegVideoEncoder<LIBAV_VER>::Init() {
  FFMPEGV_LOG("Init");
  return InvokeAsync(mTaskQueue, __func__, [self = RefPtr(this)]() {
    // FFmpegEncoderModule may have already initialized us in the GPU process.
    if (self->mCodecContext) {
      return InitPromise::CreateAndResolve(true, __func__);
    }
    MediaResult r = self->InitEncoder();
    if (NS_FAILED(r.Code())) {
      FFMPEGV_LOG("{}", r.Description().get());
      return InitPromise::CreateAndReject(r, __func__);
    }
    return InitPromise::CreateAndResolve(true, __func__);
  });
}

nsCString FFmpegVideoEncoder<LIBAV_VER>::GetDescriptionName() const {
#ifdef USING_MOZFFVPX
  return mIsHardwareAccelerated ? "ffvpx hardware video encoder"_ns
                                : "ffvpx software video encoder"_ns;
#else
  const char* lib =
#  if defined(MOZ_FFMPEG)
      FFmpegRuntimeLinker::LinkStatusLibraryName();
#  else
      "no library: ffmpeg disabled during build";
#  endif
  return nsFmtCString("ffmpeg {} video encoder ({})",
                      mIsHardwareAccelerated ? "hardware" : "software", lib);
#endif
}

bool FFmpegVideoEncoder<LIBAV_VER>::SvcEnabled() const {
  return mConfig.mScalabilityMode != ScalabilityMode::None;
}

bool FFmpegVideoEncoder<LIBAV_VER>::ShouldTryHardware() const {
#ifdef MOZ_WIDGET_ANDROID
  if (FFmpegEncoderModule<LIBAV_VER>::ShouldEncodeInGPUProcess()) {
    FFMPEGV_LOG("Platform encoder must be used in the GPU process");
    return false;
  }

  // mediacodecenc doesn't expose MediaCodec temporal layering (ts-schema), and
  // per-frame layer ids are only reported on Android 17+ by encoders supporting
  // a webrtc.svc schema.
  if (SvcEnabled()) {
    FFMPEGV_LOG("MediaCodec encoders do not support SVC");
    return false;
  }

  // On Android, the MediaCodec encoders are the only ones available to us,
  // which may be implemented in hardware or software.
  if (mCodecID == AV_CODEC_ID_H264 || mCodecID == AV_CODEC_ID_HEVC) {
    return StaticPrefs::media_ffvpx_hw_enabled();
  }

  if (StaticPrefs::media_ffvpx_hw_minimal()) {
    return false;
  }
#endif

  if (mConfig.mHardwarePreference == HardwarePreference::RequireSoftware) {
    return false;
  }

  RefPtr<PlatformEncoderModule> pem =
      FFmpegEncoderModule<LIBAV_VER>::Create(mLib);
  return pem->SupportsCodec(mConfig.mCodec)
      .contains(media::EncodeSupport::HardwareEncode);
}

MediaResult FFmpegVideoEncoder<LIBAV_VER>::InitEncoder() {
  MediaResult result(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR);
  if (ShouldTryHardware()) {
    result = InitEncoderInternal(/* aHardware */ true);
  }
  // TODO(aosmond): We should be checking here for RequireHardware, but we fail
  // encoding tests if we don't allow fallback to software on Linux in CI. Pure
  // software encoding is never permitted in the GPU process however, so we fail
  // there instead.
  if (NS_FAILED(result.Code()) && !XRE_IsGPUProcess()) {
    result = InitEncoderInternal(/* aHardware */ false);
  }
  return result;
}

MediaResult FFmpegVideoEncoder<LIBAV_VER>::InitEncoderInternal(bool aHardware) {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());

  FFMPEGV_LOG("FFmpegVideoEncoder::InitEncoder");
  mLastFrameColorConfig.reset();
  mCanChangeColorPerFrame = false;

  // Initialize the common members of the encoder instance
  auto r = AllocateCodecContext(aHardware);
  if (r.isErr()) {
    return r.inspectErr();
  }
  mCodecContext = r.unwrap();
  mCodecName = mCodecContext->codec->name;

#ifdef MOZ_WIDGET_ANDROID
  // We need to create a MediaCodec encoder for H264/HEVC but it may or may not
  // be backed by actual hardware.
  switch (mCodecID) {
    case AV_CODEC_ID_H264:
      mIsHardwareAccelerated = aHardware && gfx::gfxVars::UseH264HwEncode();
      break;
    case AV_CODEC_ID_HEVC:
      mIsHardwareAccelerated = aHardware && gfx::gfxVars::UseHEVCHwEncode();
      break;
    default:
      mIsHardwareAccelerated = aHardware;
      break;
  }
  mUseDurationMap = aHardware;
#else
#  ifdef MOZ_FFMPEG_ENCODER_USE_DURATION_MAP
  mUseDurationMap = true;
#  endif
  mIsHardwareAccelerated = aHardware;
#endif

  // And now the video-specific part
#ifdef MOZ_WIDGET_ANDROID
  // COLOR_FormatYUV420SemiPlanar(NV12) is the most widely supported
  // format by the Android hardware encoders.
  mCodecContext->pix_fmt =
      aHardware ? ffmpeg::FFMPEG_PIX_FMT_NV12 : ffmpeg::FFMPEG_PIX_FMT_YUV420P;
#else
  mCodecContext->pix_fmt = ffmpeg::FFMPEG_PIX_FMT_YUV420P;
#endif

  // Signal what the config knows; the codec writes it into the bitstream.
  // Unknown fields stay unspecified.
  const EncoderConfig::VideoColorSpace& colors = mConfig.mFormat.mColorSpace;
  if (colors.mRange) {
    mCodecContext->color_range = ToAVColorRange(colors.mRange.ref());
  }
  if (colors.mMatrix) {
    mCodecContext->colorspace = ToAVColorSpace(colors.mMatrix.ref());
  }
  if (colors.mPrimaries) {
    mCodecContext->color_primaries =
        ToAVColorPrimaries(colors.mPrimaries.ref());
  }
  if (colors.mTransferFunction) {
    mCodecContext->color_trc =
        ToAVColorTransfer(colors.mTransferFunction.ref());
  }
  FFMPEGV_LOG(
      "Configured encoder color metadata: {} -> FFmpeg [range={}, matrix={}, "
      "primaries={}, transfer={}]",
      colors.ToString().get(), static_cast<int>(mCodecContext->color_range),
      static_cast<int>(mCodecContext->colorspace),
      static_cast<int>(mCodecContext->color_primaries),
      static_cast<int>(mCodecContext->color_trc));

  mCodecContext->width = static_cast<int>(mConfig.mSize.width);
  mCodecContext->height = static_cast<int>(mConfig.mSize.height);
  // Reasonnable default for the quantization range.
  mCodecContext->qmin =
      AssertedCast<int>(StaticPrefs::media_ffmpeg_encoder_quantizer_min());
  mCodecContext->qmax =
      AssertedCast<int>(StaticPrefs::media_ffmpeg_encoder_quantizer_max());
  if (mConfig.mUsage == Usage::Realtime) {
    mCodecContext->thread_count = 1;
  } else {
    int64_t pixels = mCodecContext->width * mCodecContext->height;
    int threads = 1;
    // Select a thread count that depends on the frame size, and cap to the
    // number of available threads minus one
    if (pixels >= 3840 * 2160) {
      threads = 16;
    } else if (pixels >= 1920 * 1080) {
      threads = 8;
    } else if (pixels >= 1280 * 720) {
      threads = 4;
    } else if (pixels >= 640 * 480) {
      threads = 2;
    }
    mCodecContext->thread_count = std::clamp<int>(
        threads, 1, AssertedCast<int>(GetNumberOfProcessors()) - 1);
  }
  // TODO(bug 1869560): The recommended time_base is the reciprocal of the frame
  // rate, but we set it to microsecond for now.
  mCodecContext->time_base =
      AVRational{.num = 1, .den = AssertedCast<int>(USECS_PER_S)};
#if LIBAVCODEC_VERSION_MAJOR >= 57
  // Note that sometimes framerate can be zero (from webcodecs).
  mCodecContext->framerate =
      AVRational{.num = AssertedCast<int>(mConfig.mFramerate), .den = 1};
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 60
  mCodecContext->flags |= AV_CODEC_FLAG_FRAME_DURATION;
#endif

  // Setting 0 here disable inter-frames: all frames are keyframes
  mCodecContext->gop_size = mConfig.mKeyframeInterval
                                ? AssertedCast<int>(mConfig.mKeyframeInterval)
                                : 10000;
  mCodecContext->keyint_min = 0;

  const bool useLowLatency = mConfig.mUsage == Usage::Realtime || SvcEnabled();
  // When either real-time or SVC is enabled via config, the general settings of
  // the encoder are set to be more appropriate for real-time usage
  if (useLowLatency) {
    if (mConfig.mUsage != Usage::Realtime) {
      FFMPEGV_LOG(
          "SVC enabled but low latency encoding mode not enabled, forcing low "
          "latency mode");
    }
    mLib->av_opt_set(mCodecContext->priv_data, "deadline", "realtime", 0);
    const int lagResult =
        mLib->av_opt_set(mCodecContext->priv_data, "lag-in-frames", "0", 0);
    mCanChangeColorPerFrame =
        mCodecName.EqualsLiteral("libaom-av1") && lagResult == 0;
    if (mConfig.mCodec == CodecType::VP8 || mConfig.mCodec == CodecType::VP9) {
      mLib->av_opt_set(mCodecContext->priv_data, "error-resilient", "1", 0);
    }
    if (mConfig.mCodec == CodecType::AV1) {
      mLib->av_opt_set(mCodecContext->priv_data, "error-resilience", "1", 0);
      // This sets usage to AOM_USAGE_REALTIME
      mLib->av_opt_set(mCodecContext->priv_data, "usage", "1", 0);
      // Allow the bitrate to swing 50% up and down the target
      mLib->av_opt_set(mCodecContext->priv_data, "rc_undershoot_percent", "50",
                       0);
      mLib->av_opt_set(mCodecContext->priv_data, "rc_overshoot_percent", "50",
                       0);
      // Row multithreading -- note that we do single threaded encoding for now,
      // so this doesn't do much
      mLib->av_opt_set(mCodecContext->priv_data, "row_mt", "1", 0);
      // Cyclic refresh adaptive quantization
      mLib->av_opt_set(mCodecContext->priv_data, "aq-mode", "3", 0);
      // optimized for real-time, 7 for regular, lower: more cpu use -> higher
      // compression ratio
      mLib->av_opt_set(mCodecContext->priv_data, "cpu-used", "9", 0);
      // disable, this is to handle camera motion, unlikely for our use case
      mLib->av_opt_set(mCodecContext->priv_data, "enable-global-motion", "0",
                       0);
      mLib->av_opt_set(mCodecContext->priv_data, "enable-cfl-intra", "0", 0);
      // TODO: Set a number of tiles appropriate for the number of threads used
      // -- disable tiling if using a single thread.
      mLib->av_opt_set(mCodecContext->priv_data, "tile-columns", "0", 0);
      mLib->av_opt_set(mCodecContext->priv_data, "tile-rows", "0", 0);
    }
  } else {
    if (mConfig.mCodec == CodecType::AV1) {
      mLib->av_opt_set_int(
          mCodecContext->priv_data, "cpu-used",
          AssertedCast<int>(StaticPrefs::media_ffmpeg_encoder_cpu_used()), 0);
    }
  }

  if (SvcEnabled()) {
    if (Maybe<SVCSettings> settings = GetSVCSettings()) {
      if (mCodecName == "libaom-av1") {
        if (mConfig.mBitrateMode != BitrateMode::Constant) {
          return MediaResult(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR,
                             "AV1 with SVC only supports constant bitrate"_ns);
        }
      }

      SVCSettings s = settings.extract();
      FFMPEGV_LOG("SVC options string: {}={}", s.mSettingKeyValue.first.get(),
                  s.mSettingKeyValue.second.get());
      mLib->av_opt_set(mCodecContext->priv_data, s.mSettingKeyValue.first.get(),
                       s.mSettingKeyValue.second.get(), 0);

      // FFmpegVideoEncoder is reset after Drain(), so mSVCInfo should be
      // reset() before emplace().
      mSVCInfo.reset();
      mSVCInfo.emplace(std::move(s.mTemporalLayerIds));

      // TODO: layer settings should be changed dynamically when the frame's
      // color space changed.
    }
  }

#ifdef MOZ_WIDGET_ANDROID
  // If we are using MediaCodec, we can set more options.
  if (aHardware) {
    if (mConfig.mBitrateMode == BitrateMode::Constant) {
      mLib->av_opt_set(mCodecContext->priv_data, "bitrate_mode", "cbr", 0);
    } else {
      mLib->av_opt_set(mCodecContext->priv_data, "bitrate_mode", "vbr", 0);
    }
  }
#endif

  nsAutoCString h264Log;
  if (mConfig.mCodecSpecific.is<H264Specific>()) {
    const H264Specific& h264Specific =
        mConfig.mCodecSpecific.as<H264Specific>();
    H264Settings s = GetH264Settings(h264Specific);
    mCodecContext->profile = s.mProfile;
    mCodecContext->level = s.mLevel;
    for (const auto& pair : s.mSettingKeyValuePairs) {
      mLib->av_opt_set(mCodecContext->priv_data, pair.first.get(),
                       pair.second.get(), 0);
    }

    // Log the settings.
    const char* formatStr =
        h264Specific.mFormat == H264BitStreamFormat::AVC ? "AVCC" : "AnnexB";
    if (mCodecName.Equals("libx264"_ns)) {
      // When using profile other than EXTENDED, the profile string is in the
      // first element of mSettingKeyValuePairs, while EXTENDED profile has no
      // profile string.

      MOZ_ASSERT_IF(
          s.mSettingKeyValuePairs.Length() != 3,
          h264Specific.mProfile == H264_PROFILE::H264_PROFILE_EXTENDED);
      const char* profileStr = s.mSettingKeyValuePairs.Length() == 3
                                   ? s.mSettingKeyValuePairs[0].second.get()
                                   : "extended";
      const char* levelStr = s.mSettingKeyValuePairs.Length() == 3
                                 ? s.mSettingKeyValuePairs[1].second.get()
                                 : s.mSettingKeyValuePairs[0].second.get();
      h264Log.AppendFmt(", H264: profile - {} ({}), level {} ({}), {}",
                        mCodecContext->profile, profileStr,
                        mCodecContext->level, levelStr, formatStr);
    } else {
      h264Log.AppendFmt(", H264: profile - {}, level {}, {}",
                        mCodecContext->profile, mCodecContext->level,
                        formatStr);
    }
  }

  // - if mConfig.mDenoising is set: av_opt_set_int(mCodecContext->priv_data,
  // "noise_sensitivity", x, 0), where the x is from 0(disabled) to 6.
  // - if mConfig.mAdaptiveQp is set: av_opt_set_int(mCodecContext->priv_data,
  // "aq_mode", x, 0), where x is from 0 to 3: 0 - Disabled, 1 - Variance
  // AQ(default), 2 - Complexity AQ, 3 - Cycle AQ.

  // Our old version of libaom-av1 is considered experimental by the recent
  // ffmpeg we use. Allow experimental codecs for now until we decide on an AV1
  // encoder.
  mCodecContext->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

  SetContextBitrate();

  AVDictionary* options = nullptr;
  if (int ret = OpenCodecContext(mCodecContext->codec, &options); ret < 0) {
    return MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        RESULT_DETAIL("failed to open %s avcodec: %s", mCodecName.get(),
                      MakeErrorString(mLib, ret).get()));
  }
  mLib->av_dict_free(&options);

  FFMPEGV_LOG(
      "{} has been initialized with format: {}, bitrate: {}, width: {}, "
      "height: {}, quantizer: [{}, {}], time_base: {}/{}{}",
      mCodecName.get(), ffmpeg::GetPixelFormatString(mCodecContext->pix_fmt),
      static_cast<int64_t>(mCodecContext->bit_rate), mCodecContext->width,
      mCodecContext->height, mCodecContext->qmin, mCodecContext->qmax,
      mCodecContext->time_base.num, mCodecContext->time_base.den,
      h264Log.IsEmpty() ? "" : h264Log.get());

  return NS_OK;
}

// avcodec_send_frame and avcodec_receive_packet were introduced in version 58.
#if LIBAVCODEC_VERSION_MAJOR >= 58
Result<MediaDataEncoder::EncodedData, MediaResult> FFmpegVideoEncoder<
    LIBAV_VER>::EncodeInputWithModernAPIs(RefPtr<const MediaData> aSample) {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
  MOZ_ASSERT(mCodecContext);
  MOZ_ASSERT(aSample);

  RefPtr<const VideoData> sample(aSample->As<VideoData>());

  // Validate input.
  if (!sample->mImage) {
    return Err(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, "No image"_ns));
  }
  if (sample->mImage->GetSize().IsEmpty()) {
    return Err(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                           "image width or height is invalid"_ns));
  }

  // Allocate AVFrame.
  if (!PrepareFrame()) {
    return Err(
        MediaResult(NS_ERROR_OUT_OF_MEMORY, "failed to allocate frame"_ns));
  }

  // Set AVFrame properties for its internal data allocation. For now, we always
  // convert into ffmpeg's buffer.
  mFrame->format = mCodecContext->pix_fmt;
  mFrame->width = static_cast<int>(mConfig.mSize.width);
  mFrame->height = static_cast<int>(mConfig.mSize.height);
  mFrame->pict_type =
      sample->mKeyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
#  if LIBAVCODEC_VERSION_MAJOR > 58
  mFrame->color_range = mCodecContext->color_range;
  mFrame->colorspace = mCodecContext->colorspace;
  mFrame->color_primaries = mCodecContext->color_primaries;
  mFrame->color_trc = mCodecContext->color_trc;

  const layers::PlanarYCbCrData* yuv = nullptr;
  if (layers::PlanarYCbCrImage* image = sample->mImage->AsPlanarYCbCrImage()) {
    yuv = image->GetData();
  } else if (layers::NVImage* image = sample->mImage->AsNVImage()) {
    yuv = image->GetData();
  }
  if (yuv) {
    mFrame->color_range = ToAVColorRange(yuv->mColorRange);
    mFrame->colorspace = ToAVColorSpace(yuv->mYUVColorSpace);
    mFrame->color_primaries = ToAVColorPrimaries(yuv->mColorPrimaries);
    mFrame->color_trc = ToAVColorTransfer(yuv->mTransferFunction);
  }

  if (mCanChangeColorPerFrame) {
    FrameColorConfig color{mFrame->color_primaries, mFrame->color_trc,
                           mFrame->colorspace, mFrame->color_range};
    if (mLastFrameColorConfig && !mLastFrameColorConfig->Equals(color)) {
      mFrame->pict_type = AV_PICTURE_TYPE_I;
    }
    mLastFrameColorConfig = Some(color);
  }
#  endif

  // Allocate AVFrame data.
  if (int ret = mLib->av_frame_get_buffer(mFrame, 0); ret < 0) {
    return Err(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                           RESULT_DETAIL("failed to allocate frame data: %s",
                                         MakeErrorString(mLib, ret).get())));
  }

  // Make sure AVFrame is writable.
  if (int ret = mLib->av_frame_make_writable(mFrame); ret < 0) {
    return Err(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                           RESULT_DETAIL("failed to make frame writable: %s",
                                         MakeErrorString(mLib, ret).get())));
  }

  nsresult rv;
  switch (mFrame->format) {
    case ffmpeg::FFMPEG_PIX_FMT_YUV420P:
      rv = ConvertToI420(sample->mImage, mFrame->data[0], mFrame->linesize[0],
                         mFrame->data[1], mFrame->linesize[1], mFrame->data[2],
                         mFrame->linesize[2], mConfig.mSize);
      break;
    case ffmpeg::FFMPEG_PIX_FMT_NV12:
      rv = ConvertToNV12(sample->mImage, mFrame->data[0], mFrame->linesize[0],
                         mFrame->data[1], mFrame->linesize[1], mConfig.mSize);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unhandled ffmpeg format!");
      rv = NS_ERROR_DOM_MEDIA_FATAL_ERR;
      break;
  }
  if (NS_FAILED(rv)) {
    return Err(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                           "failed to convert format to ffmpeg format"_ns));
  }

  // Set presentation timestamp and duration of the AVFrame. The unit of pts is
  // time_base.
  // TODO(bug 1869560): The recommended time_base is the reciprocal of the frame
  // rate, but we set it to microsecond for now.
#  if LIBAVCODEC_VERSION_MAJOR >= 59
  mFrame->time_base =
      AVRational{.num = 1, .den = AssertedCast<int>(USECS_PER_S)};
#  endif
  // Provide fake pts, see header file.
  if (mConfig.mCodec == CodecType::AV1) {
    mFrame->pts = mFakePts;
    mPtsMap.Insert(mFakePts, aSample->mTime.ToMicroseconds());
    mFakePts += aSample->mDuration.ToMicroseconds();
    mCurrentFramePts = aSample->mTime.ToMicroseconds();
  } else {
    mFrame->pts = aSample->mTime.ToMicroseconds();
  }
#  ifdef MOZ_FFMPEG_ENCODER_USE_DURATION_MAP
  if (mUseDurationMap) {
    mDurationMap.Insert(mFrame->pts, aSample->mDuration.ToMicroseconds());
  }
#  else
  mFrame->duration = aSample->mDuration.ToMicroseconds();
#  endif
  Duration(mFrame) = aSample->mDuration.ToMicroseconds();

  AVDictionary* dict = nullptr;
  // VP8/VP9 use a mode that handles the temporal layer id sequence internally,
  // and don't require setting explicitly setting the metadata. Other codecs
  // such as AV1 via libaom however requires manual frame tagging.
  if (SvcEnabled() && !CodecManagesTemporalIds(mConfig.mCodec)) {
    if (mFrame->pict_type == AV_PICTURE_TYPE_I) {
      FFMPEGV_LOG("Resetting temporal layer id for key frame");
      mSVCInfo->ResetTemporalLayerId();
    }
    nsFmtCString str("{}", mSVCInfo->CurrentTemporalLayerId());
    mLib->av_dict_set(&dict, "temporal_id", str.get(), 0);
    mFrame->metadata = dict;
  }

  // Now send the AVFrame to ffmpeg for encoding, same code for audio and video.
  return FFmpegDataEncoder<LIBAV_VER>::EncodeWithModernAPIs();
}
#endif  // if LIBAVCODEC_VERSION_MAJOR >= 58

Result<RefPtr<MediaRawData>, MediaResult>
FFmpegVideoEncoder<LIBAV_VER>::ToMediaRawData(AVPacket* aPacket) {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
  MOZ_ASSERT(aPacket);

  auto extradataResult = GetExtraData(aPacket);
  if (extradataResult.isOk()) {
    mLastExtraData = extradataResult.unwrap();
  } else if (extradataResult.isErr()) {
    MediaResult e = extradataResult.unwrapErr();
    if (e.Code() != NS_ERROR_NOT_AVAILABLE &&
        e.Code() != NS_ERROR_NOT_IMPLEMENTED) {
      return Err(e);
    }
    FFMPEGV_LOG("GetExtraData failed with {}, but we can ignore it for now",
                e.Description().get());
  }

  auto data = MakeRefPtr<MediaRawData>();
  data->mKeyframe = (aPacket->flags & AV_PKT_FLAG_KEY) != 0;
  UniquePtr<MediaRawDataWriter> writer(data->CreateWriter());

  const bool isH264 =
      mCodecID == AV_CODEC_ID_H264 && mConfig.mCodecSpecific.is<H264Specific>();

#ifdef MOZ_WIDGET_ANDROID
  // If we have an AnnexB keyframe, and we failed to extract extradata from the
  // packet, then we know that the SPS/PPS data is missing from the data. In
  // that case we need to prepend our cached extradata to supply it in-band.
  //
  // When our minimum supported Android version is >= 9, then we can just use
  // the prepend-sps-pps-to-idr-frames parameter with AMediaFormat.
  if (data->mKeyframe && !extradataResult.isOk() && mLastExtraData && isH264 &&
      mConfig.mCodecSpecific.as<H264Specific>().mFormat ==
          H264BitStreamFormat::ANNEXB) {
    if (!writer->Append(mLastExtraData->Elements(), mLastExtraData->Length())) {
      return Err(
          MediaResult(NS_ERROR_OUT_OF_MEMORY,
                      "fail to append extradata to MediaRawData buffer"_ns));
    }
  }
#endif

  if (!writer->Append(aPacket->data, aPacket->size)) {
    return Err(MediaResult(NS_ERROR_OUT_OF_MEMORY,
                           "fail to append packet to MediaRawData buffer"_ns));
  }

  // With AVC, we are expected to provide the extradata out-of-band. This will
  // either be done as part of our conversion from AnnexB, or we will attach the
  // extradata we cached/extracted from the packet earlier.
  if (isH264 && mConfig.mCodecSpecific.as<H264Specific>().mFormat ==
                    H264BitStreamFormat::AVC) {
    if (!mCodecName.Equals("libx264"_ns) && AnnexB::IsAnnexB(*data)) {
      if (!AnnexB::ConvertSampleToAVCC(data, mLastExtraData)) {
        return Err(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                               "Failed to convert to AVCC"_ns));
      }
    } else {
      data->mExtraData = mLastExtraData;
    }
  }

  // TODO(bug 1869560): The unit of pts, dts, and duration is time_base, which
  // is recommended to be the reciprocal of the frame rate, but we set it to
  // microsecond for now.
  data->mTime = media::TimeUnit::FromMicroseconds(aPacket->pts);
#ifdef MOZ_FFMPEG_ENCODER_USE_DURATION_MAP
  Maybe<int64_t> duration;
  if (mUseDurationMap && (duration = mDurationMap.Take(aPacket->pts))) {
    data->mDuration = media::TimeUnit::FromMicroseconds(*duration);
  } else
#endif
  {
    data->mDuration = media::TimeUnit::FromMicroseconds(aPacket->duration);
  }
  data->mTimecode = media::TimeUnit::FromMicroseconds(aPacket->dts);

  if (mConfig.mCodec == CodecType::AV1) {
    auto found = mPtsMap.Take(aPacket->pts);
    data->mTime = media::TimeUnit::FromMicroseconds(found.value());
  }

  if (mSVCInfo) {
    if (data->mKeyframe && CodecManagesTemporalIds(mConfig.mCodec)) {
      FFMPEGV_LOG(
          "Encoded packet is key frame, resetting temporal layer id sequence");
      mSVCInfo->ResetTemporalLayerId();
    }
    uint8_t temporalLayerId = mSVCInfo->CurrentTemporalLayerId();
    data->mTemporalLayerId.emplace(temporalLayerId);
    mSVCInfo->UpdateTemporalLayerId();
  }

  return data;
}

Result<already_AddRefed<MediaByteBuffer>, MediaResult>
FFmpegVideoEncoder<LIBAV_VER>::GetExtraData(AVPacket* aPacket) {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
  MOZ_ASSERT(aPacket);

  // We only extract the extra data when encoding into AVCC format.
  if (mCodecID != AV_CODEC_ID_H264 ||
      !mConfig.mCodecSpecific.is<H264Specific>()) {
    return Err(
        MediaResult(NS_ERROR_NOT_AVAILABLE, "Extra data unnecessary"_ns));
  }

  const bool wantAVCC = mConfig.mCodecSpecific.as<H264Specific>().mFormat ==
                        H264BitStreamFormat::AVC;

  Span<const uint8_t> packetBuf(aPacket->data,
                                AssertedCast<size_t>(aPacket->size));
  if (!mCodecName.Equals("libx264"_ns) && AnnexB::IsAnnexB(packetBuf)) {
    auto extraData = wantAVCC ? AnnexB::ExtractExtraDataForAVCC(packetBuf)
                              : AnnexB::ExtractExtraData(packetBuf);
    if (!extraData) {
      return Err(MediaResult(NS_ERROR_NOT_AVAILABLE,
                             "Extra data missing from packet"_ns));
    }
    return extraData.forget();
  }

  if (!(aPacket->flags & AV_PKT_FLAG_KEY)) {
    return Err(MediaResult(NS_ERROR_NOT_AVAILABLE,
                           "Extra data only comes with key frame"_ns));
  }

  if (!mCodecName.Equals("libx264"_ns)) {
    return Err(MediaResult(
        NS_ERROR_NOT_IMPLEMENTED,
        RESULT_DETAIL(
            "Get extra data from codec %s has not been implemented yet",
            mCodecName.get())));
  }

  if (!wantAVCC) {
    return Err(
        MediaResult(NS_ERROR_NOT_AVAILABLE, "Extra data unnecessary"_ns));
  }

  bool useGlobalHeader =
#if LIBAVCODEC_VERSION_MAJOR >= 57
      mCodecContext->flags & AV_CODEC_FLAG_GLOBAL_HEADER;
#else
      false;
#endif

  Span<const uint8_t> buf;
  if (useGlobalHeader) {
    buf = Span<const uint8_t>(
        mCodecContext->extradata,
        AssertedCast<size_t>(mCodecContext->extradata_size));
  } else {
    buf = packetBuf;
  }
  if (buf.empty()) {
    return Err(MediaResult(NS_ERROR_UNEXPECTED,
                           "fail to get H264 AVCC header in key frame!"_ns));
  }

  BufferReader reader(buf);

  // The first part is sps.
  uint32_t spsSize = MOZ_TRY(reader.ReadU32());
  Span<const uint8_t> spsData =
      MOZ_TRY(reader.ReadSpan<const uint8_t>(static_cast<size_t>(spsSize)));

  // The second part is pps.
  uint32_t ppsSize = MOZ_TRY(reader.ReadU32());
  Span<const uint8_t> ppsData =
      MOZ_TRY(reader.ReadSpan<const uint8_t>(static_cast<size_t>(ppsSize)));

  // Ensure we have profile, constraints and level needed to create the extra
  // data.
  if (spsData.Length() < 4) {
    return Err(MediaResult(NS_ERROR_UNEXPECTED, "spsData is too short"_ns));
  }

  FFMPEGV_LOG(
      "Generate extra data: profile - {}, constraints: {}, level: {} for pts @ "
      "{}",
      spsData[1], spsData[2], spsData[3], aPacket->pts);

  // Create extra data.
  auto extraData = MakeRefPtr<MediaByteBuffer>();
  H264::WriteExtraData(extraData, spsData[1], spsData[2], spsData[3], spsData,
                       ppsData);
  MOZ_ASSERT(extraData);
  return extraData.forget();
}

Maybe<FFmpegVideoEncoder<LIBAV_VER>::SVCSettings>
FFmpegVideoEncoder<LIBAV_VER>::GetSVCSettings() {
  MOZ_ASSERT(!mCodecName.IsEmpty());
  MOZ_ASSERT(SvcEnabled());

  CodecType codecType = CodecType::Unknown;
  if (mCodecName == "libvpx") {
    codecType = CodecType::VP8;
  } else if (mCodecName == "libvpx-vp9") {
    codecType = CodecType::VP9;
  } else if (mCodecName == "libaom-av1") {
    codecType = CodecType::AV1;
  }

  if (codecType == CodecType::Unknown) {
    FFMPEGV_LOG("SVC setting is not implemented for {} codec",
                mCodecName.get());
    return Nothing();
  }

  SVCLayerSettings svc = GetSVCLayerSettings(
      codecType, mConfig.mScalabilityMode, mConfig.mBitrate);

  nsAutoCString name;
  nsAutoCString parameters;

  if (codecType == CodecType::VP8 || codecType == CodecType::VP9) {
    // Check if the number of temporal layers in codec specific settings
    // matches
    // the number of layers for the given scalability mode.
    if (mConfig.mCodecSpecific.is<VP8Specific>()) {
      MOZ_ASSERT(mConfig.mCodecSpecific.as<VP8Specific>().mNumTemporalLayers ==
                 svc.mNumberTemporalLayers);
    } else if (mConfig.mCodecSpecific.is<VP9Specific>()) {
      MOZ_ASSERT(mConfig.mCodecSpecific.as<VP9Specific>().mNumTemporalLayers ==
                 svc.mNumberTemporalLayers);
    }

    // Form an SVC setting string for libvpx.
    name = "ts-parameters"_ns;
    parameters.Append("ts_target_bitrate=");
    for (size_t i = 0; i < svc.mTargetBitrates.Length(); ++i) {
      if (i > 0) {
        parameters.Append(",");
      }
      parameters.AppendFmt("{}", svc.mTargetBitrates[i]);
    }
    parameters.AppendFmt(
        ":ts_layering_mode={}",
        svc.mCodecAppendix->as<VPXSVCAppendix>().mLayeringMode);
  }

  if (codecType == CodecType::AV1) {
    // Form an SVC setting string for libaom.
    name = "svc-parameters"_ns;
    parameters.AppendFmt("number_spatial_layers={}", svc.mNumberSpatialLayers);
    parameters.AppendFmt(":number_temporal_layers={}",
                         svc.mNumberTemporalLayers);
    parameters.Append(":framerate_factor=");
    for (size_t i = 0; i < svc.mRateDecimators.Length(); ++i) {
      if (i > 0) {
        parameters.Append(",");
      }
      parameters.AppendFmt("{}", svc.mRateDecimators[i]);
    }
    parameters.Append(":layer_target_bitrate=");
    for (size_t i = 0; i < svc.mTargetBitrates.Length(); ++i) {
      if (i > 0) {
        parameters.Append(",");
      }
      parameters.AppendFmt("{}", svc.mTargetBitrates[i]);
    }
  }

  return Some(
      SVCSettings{std::move(svc.mLayerIds),
                  std::make_pair(std::move(name), std::move(parameters))});
}

FFmpegVideoEncoder<LIBAV_VER>::H264Settings FFmpegVideoEncoder<
    LIBAV_VER>::GetH264Settings(const H264Specific& aH264Specific) {
  nsTArray<std::pair<nsCString, nsCString>> keyValuePairs;

  Maybe<H264Setting> profile = GetH264Profile(aH264Specific.mProfile);
  MOZ_RELEASE_ASSERT(profile.isSome());
  MOZ_RELEASE_ASSERT(!profile->mString.IsEmpty() ||
                     aH264Specific.mProfile ==
                         H264_PROFILE::H264_PROFILE_EXTENDED);

  Maybe<H264Setting> level = GetH264Level(aH264Specific.mLevel);
  MOZ_RELEASE_ASSERT(level.isSome());
  MOZ_RELEASE_ASSERT(!level->mString.IsEmpty());

  if (!profile->mString.IsEmpty()) {
    keyValuePairs.AppendElement(std::make_pair("profile"_ns, profile->mString));
  }
  keyValuePairs.AppendElement(std::make_pair("level"_ns, level->mString));

  if (mCodecName.Equals("libx264"_ns)) {
    // Set format: libx264's default format is annexb.
    if (aH264Specific.mFormat == H264BitStreamFormat::AVC) {
      keyValuePairs.AppendElement(std::make_pair("x264-params"_ns, "annexb=0"));
      // mCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER
      // if we don't want to append SPS/PPS data in all keyframe
      // (LIBAVCODEC_VERSION_MAJOR >= 57 only).
    } else {
      // Set annexb explicitly even if it's default format.
      keyValuePairs.AppendElement(std::make_pair("x264-params"_ns, "annexb=1"));
    }
  }

  return H264Settings{.mProfile = profile->mValue,
                      .mLevel = level->mValue,
                      .mSettingKeyValuePairs = std::move(keyValuePairs)};
}

}  // namespace mozilla
