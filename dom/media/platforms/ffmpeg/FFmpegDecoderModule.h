/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef FFmpegDecoderModule_h_
#define FFmpegDecoderModule_h_

#include "FFmpegAudioDecoder.h"
#include "FFmpegLibWrapper.h"
#include "FFmpegUtils.h"
#include "FFmpegVideoDecoder.h"
#include "MP4Decoder.h"
#include "PlatformDecoderModule.h"
#include "VPXDecoder.h"
#include "VideoUtils.h"
#include "mozilla/DataMutex.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/gfx/gfxVars.h"
#include "prenv.h"

#ifdef DEBUG
#  include "mozilla/AppShutdown.h"
#endif

namespace mozilla {

template <int V>
class FFmpegDecoderModule : public PlatformDecoderModule {
 public:
  const char* Name() const override {
#ifdef FFVPX_VERSION
    return "FFmpeg(FFVPX)";
#else
    return "FFmpeg(OS library)";
#endif
  }
  static void Init(const FFmpegLibWrapper* aLib) {
#if (defined(XP_WIN) || defined(MOZ_WIDGET_GTK) || \
     defined(MOZ_WIDGET_ANDROID)) &&               \
    defined(MOZ_USE_HWDECODE) && !defined(MOZ_FFVPX_AUDIOONLY)
#  ifdef XP_WIN
    if (!XRE_IsGPUProcess()) {
      return;
    }
#  else
    if (!XRE_IsRDDProcess() && !XRE_IsUtilityProcess() &&
#    ifdef MOZ_WIDGET_ANDROID
        !XRE_IsGPUProcess() &&
#    endif
        !(XRE_IsParentProcess() && PR_GetEnv("MOZ_RUN_GTEST"))) {
      return;
    }
#  endif

    if (!gfx::gfxVars::IsInitialized()) {
      MOZ_ASSERT(AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdown));
      return;
    }

    const AVHWDeviceType kDeviceTypes[] = {
#  ifdef XP_WIN
        AV_HWDEVICE_TYPE_D3D11VA,
#  endif
#  ifdef MOZ_WIDGET_GTK
#    ifdef MOZ_USE_HWDECODE_VULKAN
        AV_HWDEVICE_TYPE_VULKAN,
#    endif
        AV_HWDEVICE_TYPE_VAAPI,
        AV_HWDEVICE_TYPE_NONE,  // Placeholder for V4L2.
#  endif
#  ifdef MOZ_WIDGET_ANDROID
        AV_HWDEVICE_TYPE_MEDIACODEC,
        AV_HWDEVICE_TYPE_NONE,  // Placeholder for audio.
#  endif
    };

    struct CodecEntry {
      AVCodecID mId;
      bool mHwAllowed;
    };

    const CodecEntry kCodecIDs[] = {
    // The following open video codecs can be decoded via hardware by using the
    // system ffmpeg or ffvpx.
#  if LIBAVCODEC_VERSION_MAJOR >= 59
        {AV_CODEC_ID_AV1, gfx::gfxVars::UseAV1HwDecode()},
#  endif
#  if LIBAVCODEC_VERSION_MAJOR >= 55
        {AV_CODEC_ID_VP9, gfx::gfxVars::UseVP9HwDecode()},
#  endif
#  if (defined(MOZ_WIDGET_GTK) || defined(MOZ_WIDGET_ANDROID)) && \
      LIBAVCODEC_VERSION_MAJOR >= 54
        {AV_CODEC_ID_VP8, gfx::gfxVars::UseVP8HwDecode()},
#  endif

#  if defined(MOZ_WIDGET_GTK) && !defined(FFVPX_VERSION)
    // These proprietary video codecs can only be decoded via hardware by using
    // the system ffmpeg, not supported by ffvpx.
#    if LIBAVCODEC_VERSION_MAJOR >= 55
        {AV_CODEC_ID_HEVC, gfx::gfxVars::UseHEVCHwDecode()},
#    endif
        {AV_CODEC_ID_H264, gfx::gfxVars::UseH264HwDecode()},
#  endif
#  ifdef MOZ_WIDGET_ANDROID
        // These proprietary codecs can only be decoded via MediaCodec decoders,
        // but the underlying implementation may be software or hardware.
        {AV_CODEC_ID_HEVC, true},
        {AV_CODEC_ID_H264, true},
        {AV_CODEC_ID_AAC, true},
#  endif
    };

    {
      // Reset the list of supported hardware codecs and reevaluate them.
      auto hwCodecs = sSupportedHWCodecs.Lock();
      hwCodecs->Clear();
      for (const auto& entry : kCodecIDs) {
        if (!entry.mHwAllowed) {
          MOZ_LOG_FMT(sPDMLog, LogLevel::Debug,
                      "Hw codec disabled by gfxVars for {}",
                      AVCodecToString(entry.mId));
          continue;
        }

        const AVCodec* codec = nullptr;
        for (const auto& deviceType : kDeviceTypes) {
          codec = FFmpegVideoDecoder<V>::FindVideoHardwareAVCodec(
              aLib, entry.mId, deviceType);
          if (codec) {
            break;
          }
        }

        if (!codec) {
          MOZ_LOG_FMT(sPDMLog, LogLevel::Debug, "No hw codec or decoder for {}",
                      AVCodecToString(entry.mId));
          continue;
        }

        hwCodecs->AppendElement(entry.mId);
        MOZ_LOG_FMT(sPDMLog, LogLevel::Debug, "Support {} for hw decoding",
                    AVCodecToString(entry.mId));
      }
    }
#endif  // (XP_WIN || MOZ_WIDGET_GTK || MOZ_WIDGET_ANDROID) && MOZ_USE_HWDECODE
        // && !MOZ_FFVPX_AUDIOONLY
  }

  static already_AddRefed<PlatformDecoderModule> Create(
      const FFmpegLibWrapper* aLib) {
    RefPtr<PlatformDecoderModule> pdm = new FFmpegDecoderModule(aLib);

    return pdm.forget();
  }

  explicit FFmpegDecoderModule(const FFmpegLibWrapper* aLib) : mLib(aLib) {}
  virtual ~FFmpegDecoderModule() = default;

  RefPtr<CreateDecoderPromise> AsyncCreateDecoder(
      const CreateDecoderParams& aParams) override {
    if (aParams.IsAudio()) {
      return CreateDecoderPromiseFrom(CreateAudioDecoder(aParams), aParams);
    }
    if (!aParams.IsVideo()) {
      return CreateDecoderPromiseFrom(nullptr, aParams);
    }
    RefPtr<FFmpegVideoDecoder<V>> decoder = CreateVideoDecoder(aParams);
    if (!decoder) {
      return CreateDecoderPromiseFrom(nullptr, aParams);
    }

    return decoder->InitHWDecoderIfAllowed();
  }

  media::DecodeSupportSet SupportsMimeType(
      const nsACString& aMimeType,
      DecoderDoctorDiagnostics* aDiagnostics) const override {
    UniquePtr<TrackInfo> trackInfo = CreateTrackInfoWithMIMEType(aMimeType);
    if (!trackInfo) {
      return media::DecodeSupportSet{};
    }
    return Supports(SupportDecoderParams(*trackInfo), aDiagnostics);
  }

  media::DecodeSupportSet Supports(
      const SupportDecoderParams& aParams,
      DecoderDoctorDiagnostics* aDiagnostics) const override {
    // This should only be supported by MFMediaEngineDecoderModule.
    if (aParams.mMediaEngineId) {
      return media::DecodeSupportSet{};
    }

    // In GPU process, only support decoding if video, or audio with a CDM on
    // Android. This only gives a hint of what the GPU decoder *may* support.
    // The actual check will occur when creating the decoder.
    const auto& trackInfo = aParams.mConfig;
    if (XRE_IsGPUProcess() && !trackInfo.GetAsVideoInfo()) {
#if defined(MOZ_WIDGET_ANDROID) && defined(FFVPX_VERSION)
      if (!aParams.mCDM) {
        return media::DecodeSupportSet{};
      }
#else
      return media::DecodeSupportSet{};
#endif
    }

    // Temporary - forces use of VPXDecoder when alpha is present.
    // Bug 1263836 will handle alpha scenario once implemented. It will shift
    // the check for alpha to PDMFactory but not itself remove the need for a
    // check.
    const nsACString& mimeType = trackInfo.mMimeType;
    if (VPXDecoder::IsVPX(mimeType) && trackInfo.GetAsVideoInfo()->HasAlpha()) {
      MOZ_LOG_FMT(sPDMLog, LogLevel::Debug,
                  "FFmpeg decoder rejects requested type '{}'",
                  PromiseFlatCString(mimeType).get());
      return media::DecodeSupportSet{};
    }

    if (VPXDecoder::IsVP9(mimeType) &&
        aParams.mOptions.contains(CreateDecoderParams::Option::LowLatency)) {
      // SVC layers are unsupported, and may be used in low latency use cases
      // (WebRTC).
      MOZ_LOG_FMT(
          sPDMLog, LogLevel::Debug,
          "FFmpeg decoder rejects requested type '{}' due to low latency",
          PromiseFlatCString(mimeType).get());
      return media::DecodeSupportSet{};
    }

    if (MP4Decoder::IsHEVC(mimeType) && !StaticPrefs::media_hevc_enabled()) {
      MOZ_LOG_FMT(
          sPDMLog, LogLevel::Debug,
          "FFmpeg decoder rejects requested type '{}' due to being disabled "
          "by the pref",
          PromiseFlatCString(mimeType).get());
      return media::DecodeSupportSet{};
    }

    AVCodecID videoCodec = FFmpegVideoDecoder<V>::GetCodecId(mimeType);
    AVCodecID audioCodec = FFmpegAudioDecoder<V>::GetCodecId(
        mimeType,
        trackInfo.GetAsAudioInfo() ? *trackInfo.GetAsAudioInfo() : AudioInfo());
    if (audioCodec == AV_CODEC_ID_NONE && videoCodec == AV_CODEC_ID_NONE) {
      MOZ_LOG_FMT(sPDMLog, LogLevel::Debug,
                  "FFmpeg decoder rejects requested type '{}'",
                  PromiseFlatCString(mimeType).get());
      return media::DecodeSupportSet{};
    }
    AVCodecID codecId =
        audioCodec != AV_CODEC_ID_NONE ? audioCodec : videoCodec;

    media::DecodeSupportSet supports;
    if (IsSWDecodingSupported(codecId, aParams)) {
      supports += media::DecodeSupport::SoftwareDecode;
    }
    if (IsHWDecodingSupported(codecId)) {
#ifdef MOZ_WIDGET_ANDROID
      const auto* videoInfo = trackInfo.GetAsVideoInfo();
      const gfx::ColorDepth colorDepth =
          videoInfo ? videoInfo->mColorDepth : gfx::ColorDepth::COLOR_8;
      const gfx::IntSize size = videoInfo ? videoInfo->mImage : gfx::IntSize();
      supports +=
          GetMediaCodecDecodeSupport(codecId, mimeType, colorDepth, size);
#else
      supports += media::DecodeSupport::HardwareDecode;
#endif
    }

#if defined(MOZ_WIDGET_ANDROID) && defined(FFVPX_VERSION) && \
    defined(MOZ_USE_HWDECODE)
    if (!supports.contains(media::DecodeSupport::HardwareDecode) &&
        aParams.mCDM && audioCodec != AV_CODEC_ID_NONE &&
        StaticPrefs::media_ffvpx_hw_enabled() &&
        FFmpegDataDecoder<V>::FindHardwareAVCodec(mLib, audioCodec,
                                                  AV_HWDEVICE_TYPE_NONE)) {
      // Audio decoding with a CDM requires MediaCodec, which may support more
      // codecs than we otherwise use it for.
      supports += media::DecodeSupport::HardwareDecode;
    }

    const auto* videoInfo = trackInfo.GetAsVideoInfo();
    if (!supports.contains(media::DecodeSupport::SoftwareDecode) &&
        aParams.mCDM && videoInfo && StaticPrefs::media_ffvpx_hw_enabled() &&
        FFmpegDataDecoder<V>::FindHardwareAVCodec(
            mLib, videoCodec, AV_HWDEVICE_TYPE_MEDIACODEC)) {
      // Video decoding with a CDM always uses MediaCodec, so its software
      // decoders may be used even where pure software decoding is not
      // permitted, such as in the GPU process.
      const media::MediaCodec codec =
          media::MCSInfo::GetMediaCodecFromMimeType(mimeType);
      if (media::MCSInfo::SupportsSoftwareDecode(
              gfx::gfxVars::PlatformMediaCodecsSupported(), codec)) {
        supports += media::MCSInfo::GetMediaCodecDecodeSupportForSize(
            codec,
            media::MCSInfo::GetMediaCodecDecodeSupportForColorDepth(
                codec, media::DecodeSupport::SoftwareDecode,
                videoInfo->mColorDepth),
            videoInfo->mImage);
      }
    }
#endif

#ifndef MOZ_WIDGET_ANDROID
    // We need to disable this assert on Android because we can advertise
    // H264/HEVC software support backed by MediaCodec, which does need to run
    // in the GPU process.
    MOZ_ASSERT_IF(XRE_IsGPUProcess(),
                  !supports.contains(media::DecodeSupport::SoftwareDecode));
#endif

    MOZ_LOG_FMT(sPDMLog, LogLevel::Debug,
                "FFmpeg decoder {} requested type '{}'",
                supports.isEmpty() ? "rejects" : "supports",
                PromiseFlatCString(mimeType).get());
    return supports;
  }

 protected:
  bool SupportsColorDepth(
      gfx::ColorDepth aColorDepth,
      DecoderDoctorDiagnostics* aDiagnostics) const override {
    return true;
  }

#ifdef MOZ_WIDGET_ANDROID
  // Returns the support for decoding with MediaCodec given the color depth and
  // frame size.
  static media::DecodeSupportSet GetMediaCodecDecodeSupport(
      AVCodecID aCodec, const nsACString& aMimeType,
      gfx::ColorDepth aColorDepth, const gfx::IntSize& aSize) {
    // Because we don't provide software implementations of H264 or HEVC on
    // Android, we must use the platform software decoders even if true
    // hardware decoding support is missing. These are permitted in the GPU
    // process because they still use MediaCodec.
    const media::MediaCodec codec =
        media::MCSInfo::GetMediaCodecFromMimeType(aMimeType);
    media::DecodeSupportSet support;
    switch (aCodec) {
      case AV_CODEC_ID_H264:
      case AV_CODEC_ID_HEVC:
        if (media::MCSInfo::SupportsSoftwareDecode(
                gfx::gfxVars::PlatformMediaCodecsSupported(), codec)) {
          support += media::DecodeSupport::SoftwareDecode;
        }
        if (aCodec == AV_CODEC_ID_H264 ? gfx::gfxVars::UseH264HwDecode()
                                       : gfx::gfxVars::UseHEVCHwDecode()) {
          support += media::DecodeSupport::HardwareDecode;
        }
        break;
      default:
        support += media::DecodeSupport::HardwareDecode;
        break;
    }
    return media::MCSInfo::GetMediaCodecDecodeSupportForSize(
        codec,
        media::MCSInfo::GetMediaCodecDecodeSupportForColorDepth(codec, support,
                                                                aColorDepth),
        aSize);
  }
#endif

  bool IsSWDecodingSupported(const AVCodecID& aCodec,
                             const SupportDecoderParams& aParams) const {
    // FFmpeg SW decoding is not allowed in the GPU process.
    if (XRE_IsGPUProcess()) {
      return false;
    }
#ifdef MOZ_WIDGET_ANDROID
    // With a CDM, video is always decoded with MediaCodec, so it must support
    // the color depth.
    const auto* videoInfo = aParams.mConfig.GetAsVideoInfo();
    if (aParams.mCDM && videoInfo &&
        !FFmpegVideoDecoder<V>::MediaCodecSupportsColorDepth(
            aCodec, aParams.MimeType(), videoInfo->mColorDepth)) {
      return false;
    }
#endif
    return FFmpegDataDecoder<V>::FindSoftwareAVCodec(mLib, aCodec);
  }

  bool IsHWDecodingSupported(AVCodecID aCodec) const {
#ifdef FFVPX_VERSION
    if (!StaticPrefs::media_ffvpx_hw_enabled()) {
      return false;
    }
#endif
    // We don't need to check the gfxVars again because we check them when we
    // populated sSupportedHWCodecs.
    auto hwCodecs = sSupportedHWCodecs.Lock();
    return hwCodecs->Contains(aCodec);
  }

 private:
  already_AddRefed<FFmpegVideoDecoder<V>> CreateVideoDecoder(
      const CreateDecoderParams& aParams) {
    if (Supports(SupportDecoderParams(aParams), nullptr).isEmpty()) {
      return nullptr;
    }
#ifdef MOZ_WIDGET_ANDROID
    // If only a platform decoder can be used, then it must be used in the GPU
    // process instead, unless we were given a CDM in this process.
    if (!aParams.mCDM && FFmpegVideoDecoder<V>::ShouldDecodeInGPUProcess() &&
        !IsSWDecodingSupported(
            FFmpegVideoDecoder<V>::GetCodecId(aParams.mConfig.mMimeType),
            SupportDecoderParams(aParams))) {
      MOZ_LOG_FMT(sPDMLog, LogLevel::Debug,
                  "FFmpeg video decoder must be created in the GPU process");
      return nullptr;
    }
#endif
    return MakeAndAddRef<FFmpegVideoDecoder<V>>(
        mLib, aParams.VideoConfig(), aParams.mKnowsCompositor,
        aParams.mImageContainer,
        aParams.mOptions.contains(CreateDecoderParams::Option::LowLatency),
        aParams.mOptions.contains(
            CreateDecoderParams::Option::HardwareDecoderNotAllowed),
        aParams.mOptions.contains(
            CreateDecoderParams::Option::Output8BitPerChannel),
        aParams.mTrackingId, aParams.mCDM);
  }

  already_AddRefed<MediaDataDecoder> CreateAudioDecoder(
      const CreateDecoderParams& aParams) {
    if (Supports(SupportDecoderParams(aParams), nullptr).isEmpty()) {
      return nullptr;
    }
    RefPtr<MediaDataDecoder> decoder = new FFmpegAudioDecoder<V>(mLib, aParams);
    return decoder.forget();
  }

  const FFmpegLibWrapper* mLib;
  constinit static inline StaticDataMutex<nsTArray<AVCodecID>>
      sSupportedHWCodecs{"sSupportedHWCodecs"};
};

}  // namespace mozilla

#endif  // FFmpegDecoderModule_h_
