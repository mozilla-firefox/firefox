/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PEMFactory.h"

#include "PlatformEncoderModule.h"

#ifdef MOZ_APPLEMEDIA
#  include "AppleEncoderModule.h"
#endif

#ifdef MOZ_WIDGET_ANDROID
#  include "AndroidDecoderModule.h"
#  include "AndroidEncoderModule.h"
#endif

#ifdef XP_WIN
#  include "WMFEncoderModule.h"
#endif

#ifdef MOZ_FFMPEG
#  include "FFmpegRuntimeLinker.h"
#endif

#include "FFVPXRuntimeLinker.h"
#include "GMPEncoderModule.h"
#include "mozilla/RemoteEncoderModule.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/gfx/gfxVars.h"

using namespace mozilla::media;

namespace mozilla {

LazyLogModule sPEMLog("PlatformEncoderModule");

#define LOGE(fmt, ...)                                                    \
  MOZ_LOG_FMT(sPEMLog, mozilla::LogLevel::Error, "[PEMFactory] {}: " fmt, \
              __func__, ##__VA_ARGS__)
#define LOG(fmt, ...)                                                     \
  MOZ_LOG_FMT(sPEMLog, mozilla::LogLevel::Debug, "[PEMFactory] {}: " fmt, \
              __func__, ##__VA_ARGS__)

static CodecType MediaCodecToCodecType(MediaCodec aCodec) {
  switch (aCodec) {
    case MediaCodec::H264:
      return CodecType::H264;
    case MediaCodec::VP8:
      return CodecType::VP8;
    case MediaCodec::VP9:
      return CodecType::VP9;
    case MediaCodec::AV1:
      return CodecType::AV1;
    case MediaCodec::HEVC:
      return CodecType::H265;
    case MediaCodec::AAC:
      return CodecType::AAC;
    case MediaCodec::FLAC:
      return CodecType::Flac;
    case MediaCodec::Opus:
      return CodecType::Opus;
    case MediaCodec::Vorbis:
      return CodecType::Vorbis;
    case MediaCodec::MP3:
    case MediaCodec::Wave:
    case MediaCodec::SENTINEL:
      return CodecType::Unknown;
    default:
      MOZ_ASSERT_UNREACHABLE("Unhandled MediaCodec type!");
      return CodecType::Unknown;
  }
}

void PEMFactory::InitGpuPEMs() {
  if (!StaticPrefs::media_use_remote_encoder_video_platform()) {
    return;
  }

#ifdef MOZ_APPLEMEDIA
  if (StaticPrefs::media_gpu_process_encoder()) {
    RefPtr<PlatformEncoderModule> m(new AppleEncoderModule());
    mCurrentPEMs.AppendElement(m);
  }
#endif

#ifdef XP_WIN
  if (StaticPrefs::media_wmf_enabled() &&
      StaticPrefs::media_gpu_process_encoder()) {
    mCurrentPEMs.AppendElement(new WMFEncoderModule());
  }
#endif

#ifndef MOZ_FFVPX_AUDIOONLY
  if (StaticPrefs::media_ffmpeg_encoder_enabled() &&
      StaticPrefs::media_gpu_process_encoder()) {
    if (RefPtr<PlatformEncoderModule> pem =
            FFVPXRuntimeLinker::CreateEncoder()) {
      mCurrentPEMs.AppendElement(pem);
    }
  }
#endif

#ifdef MOZ_FFMPEG
  if (StaticPrefs::media_ffmpeg_encoder_enabled() &&
      StaticPrefs::media_gpu_process_encoder()) {
    if (RefPtr<PlatformEncoderModule> pem =
            FFmpegRuntimeLinker::CreateEncoder()) {
      mCurrentPEMs.AppendElement(pem);
    }
  }
#endif
}

void PEMFactory::InitRddPEMs() {
#ifdef MOZ_APPLEMEDIA
  if (StaticPrefs::media_use_remote_encoder_video_platform() &&
      StaticPrefs::media_rdd_applemedia_enabled()) {
    RefPtr<PlatformEncoderModule> m(new AppleEncoderModule());
    mCurrentPEMs.AppendElement(m);
  }
#endif

#ifdef XP_WIN
  if (StaticPrefs::media_use_remote_encoder_video_platform() &&
      StaticPrefs::media_wmf_enabled() &&
      StaticPrefs::media_rdd_wmf_enabled()) {
    mCurrentPEMs.AppendElement(new WMFEncoderModule());
  }
#endif

#ifdef MOZ_FFVPX_AUDIOONLY
  if (StaticPrefs::media_use_remote_encoder_audio_software() &&
      StaticPrefs::media_ffmpeg_encoder_enabled() &&
      !StaticPrefs::media_utility_process_enabled() &&
      StaticPrefs::media_rdd_ffvpx_enabled())
#else
  if (((StaticPrefs::media_use_remote_encoder_audio_software() &&
        !StaticPrefs::media_utility_process_enabled()) ||
       StaticPrefs::media_use_remote_encoder_video_software() ||
       StaticPrefs::media_use_remote_encoder_video_platform()) &&
      StaticPrefs::media_ffmpeg_encoder_enabled() &&
      StaticPrefs::media_rdd_ffvpx_enabled())
#endif
  {
    if (RefPtr<PlatformEncoderModule> pem =
            FFVPXRuntimeLinker::CreateEncoder()) {
      mCurrentPEMs.AppendElement(pem);
    }
  }

#ifdef MOZ_FFMPEG
#  ifdef MOZ_FFVPX_AUDIOONLY
  if (StaticPrefs::media_use_remote_encoder_audio_software() &&
      StaticPrefs::media_ffmpeg_encoder_enabled() &&
      !StaticPrefs::media_utility_process_enabled() &&
      StaticPrefs::media_rdd_ffmpeg_enabled())
#  else
  if (((StaticPrefs::media_use_remote_encoder_audio_software() &&
        !StaticPrefs::media_utility_process_enabled()) ||
       StaticPrefs::media_use_remote_encoder_video_software() ||
       StaticPrefs::media_use_remote_encoder_video_platform()) &&
      StaticPrefs::media_ffmpeg_encoder_enabled() &&
      StaticPrefs::media_rdd_ffmpeg_enabled())
#  endif
  {
    if (StaticPrefs::media_ffmpeg_enabled()) {
      if (RefPtr<PlatformEncoderModule> pem =
              FFmpegRuntimeLinker::CreateEncoder()) {
        mCurrentPEMs.AppendElement(pem);
      }
    }
  }
#endif
}

void PEMFactory::InitUtilityPEMs() {
  if (StaticPrefs::media_use_remote_encoder_audio_software() &&
      StaticPrefs::media_ffmpeg_encoder_enabled()) {
    if (RefPtr<PlatformEncoderModule> pem =
            FFVPXRuntimeLinker::CreateEncoder()) {
      mCurrentPEMs.AppendElement(pem);
    }
  }

#ifdef MOZ_FFMPEG
  if (StaticPrefs::media_use_remote_encoder_audio_software() &&
      StaticPrefs::media_ffmpeg_enabled()) {
    if (RefPtr<PlatformEncoderModule> pem =
            FFmpegRuntimeLinker::CreateEncoder()) {
      mCurrentPEMs.AppendElement(pem);
    }
  }
#endif

#ifdef MOZ_WIDGET_ANDROID
  if (StaticPrefs::media_utility_android_media_codec_enabled()) {
    mCurrentPEMs.AppendElement(new AndroidEncoderModule());
  }
#endif
}

void PEMFactory::InitContentPEMs() {
  if (StaticPrefs::media_use_remote_encoder_video_platform() &&
      StaticPrefs::media_gpu_process_encoder()) {
    if (RefPtr<PlatformEncoderModule> pem =
            RemoteEncoderModule::Create(RemoteMediaIn::GpuProcess)) {
      mCurrentPEMs.AppendElement(std::move(pem));
    }
  }

  if ((StaticPrefs::media_use_remote_encoder_video_platform() ||
       StaticPrefs::media_use_remote_encoder_video_software() ||
       StaticPrefs::media_use_remote_encoder_audio_software()) &&
      StaticPrefs::media_rdd_process_enabled()) {
    if (RefPtr<PlatformEncoderModule> pem =
            RemoteEncoderModule::Create(RemoteMediaIn::RddProcess)) {
      mCurrentPEMs.AppendElement(std::move(pem));
    }
  }

  if (StaticPrefs::media_use_remote_encoder_audio_software() &&
      StaticPrefs::media_utility_process_enabled()) {
#ifdef MOZ_APPLEMEDIA
    if (RefPtr<PlatformEncoderModule> pem = RemoteEncoderModule::Create(
            RemoteMediaIn::UtilityProcess_AppleMedia)) {
      mCurrentPEMs.AppendElement(std::move(pem));
    }
#endif

#ifdef XP_WIN
    if (RefPtr<PlatformEncoderModule> pem =
            RemoteEncoderModule::Create(RemoteMediaIn::UtilityProcess_WMF)) {
      mCurrentPEMs.AppendElement(std::move(pem));
    }
#endif

    if (RefPtr<PlatformEncoderModule> pem = RemoteEncoderModule::Create(
            RemoteMediaIn::UtilityProcess_Generic)) {
      mCurrentPEMs.AppendElement(std::move(pem));
    }
  }

  if (!StaticPrefs::media_use_remote_encoder_video_platform()) {
#ifdef MOZ_APPLEMEDIA
    RefPtr<PlatformEncoderModule> m(new AppleEncoderModule());
    mCurrentPEMs.AppendElement(m);
#endif

#ifdef MOZ_WIDGET_ANDROID
    if (AndroidDecoderModule::IsJavaDecoderModuleAllowed()) {
      mCurrentPEMs.AppendElement(new AndroidEncoderModule());
    }
#endif

#ifdef XP_WIN
    mCurrentPEMs.AppendElement(new WMFEncoderModule());
#endif
  }

#ifdef MOZ_FFVPX_AUDIOONLY
  if (!StaticPrefs::media_use_remote_encoder_audio_software() &&
      StaticPrefs::media_ffmpeg_encoder_enabled())
#else
  if ((!StaticPrefs::media_use_remote_encoder_audio_software() ||
       !StaticPrefs::media_use_remote_encoder_video_software()) &&
      StaticPrefs::media_ffmpeg_encoder_enabled())
#endif
  {
    if (RefPtr<PlatformEncoderModule> pem =
            FFVPXRuntimeLinker::CreateEncoder()) {
      mCurrentPEMs.AppendElement(pem);
    }
  }

#ifdef MOZ_FFMPEG
#  ifdef MOZ_FFVPX_AUDIOONLY
  if (!StaticPrefs::media_use_remote_encoder_audio_software() &&
      StaticPrefs::media_ffmpeg_enabled() &&
      StaticPrefs::media_ffmpeg_encoder_enabled())
#  else
  if ((!StaticPrefs::media_use_remote_encoder_audio_software() ||
       !StaticPrefs::media_use_remote_encoder_video_software()) &&
      StaticPrefs::media_ffmpeg_enabled() &&
      StaticPrefs::media_ffmpeg_encoder_enabled())
#  endif
  {
    if (RefPtr<PlatformEncoderModule> pem =
            FFmpegRuntimeLinker::CreateEncoder()) {
      mCurrentPEMs.AppendElement(pem);
    }
  }
#endif

  if (StaticPrefs::media_gmp_encoder_enabled()) {
    auto pem = MakeRefPtr<GMPEncoderModule>();
    if (StaticPrefs::media_gmp_encoder_preferred()) {
      mCurrentPEMs.InsertElementAt(0, std::move(pem));
    } else {
      mCurrentPEMs.AppendElement(std::move(pem));
    }
  }
}

void PEMFactory::InitDefaultPEMs() {
#ifdef MOZ_APPLEMEDIA
  RefPtr<PlatformEncoderModule> m(new AppleEncoderModule());
  mCurrentPEMs.AppendElement(m);
#endif

#ifdef MOZ_WIDGET_ANDROID
  if (AndroidDecoderModule::IsJavaDecoderModuleAllowed()) {
    mCurrentPEMs.AppendElement(new AndroidEncoderModule());
  }
#endif

#ifdef XP_WIN
  mCurrentPEMs.AppendElement(new WMFEncoderModule());
#endif

  if (StaticPrefs::media_ffmpeg_encoder_enabled()) {
    if (RefPtr<PlatformEncoderModule> pem =
            FFVPXRuntimeLinker::CreateEncoder()) {
      mCurrentPEMs.AppendElement(pem);
    }
  }

#ifdef MOZ_FFMPEG
  if (StaticPrefs::media_ffmpeg_enabled() &&
      StaticPrefs::media_ffmpeg_encoder_enabled()) {
    if (RefPtr<PlatformEncoderModule> pem =
            FFmpegRuntimeLinker::CreateEncoder()) {
      mCurrentPEMs.AppendElement(pem);
    }
  }
#endif

  if (StaticPrefs::media_gmp_encoder_enabled()) {
    auto pem = MakeRefPtr<GMPEncoderModule>();
    if (StaticPrefs::media_gmp_encoder_preferred()) {
      mCurrentPEMs.InsertElementAt(0, std::move(pem));
    } else {
      mCurrentPEMs.AppendElement(std::move(pem));
    }
  }
}

PEMFactory::PEMFactory() {
  gfx::gfxVars::Initialize();

  if (XRE_IsGPUProcess()) {
    InitGpuPEMs();
  } else if (XRE_IsRDDProcess()) {
    InitRddPEMs();
  } else if (XRE_IsUtilityProcess()) {
    InitUtilityPEMs();
  } else if (XRE_IsContentProcess()) {
    InitContentPEMs();
  } else {
    InitDefaultPEMs();
  }
}

already_AddRefed<MediaDataEncoder> PEMFactory::CreateEncoder(
    const EncoderConfig& aConfig, const RefPtr<TaskQueue>& aTaskQueue) {
  RefPtr<PlatformEncoderModule> m = FindPEM(aConfig);
  if (!m) {
    return nullptr;
  }

  if (aConfig.IsVideo()) {
    return m->CreateVideoEncoder(aConfig, aTaskQueue);
  }
  if (aConfig.IsAudio()) {
    return m->CreateAudioEncoder(aConfig, aTaskQueue);
  }
  return nullptr;
}

RefPtr<PlatformEncoderModule::CreateEncoderPromise>
PEMFactory::CreateEncoderAsync(const EncoderConfig& aConfig,
                               const RefPtr<TaskQueue>& aTaskQueue) {
  return CheckAndMaybeCreateEncoder(aConfig, 0, aTaskQueue);
}

RefPtr<PlatformEncoderModule::CreateEncoderPromise>
PEMFactory::CheckAndMaybeCreateEncoder(const EncoderConfig& aConfig,
                                       uint32_t aIndex,
                                       const RefPtr<TaskQueue>& aTaskQueue) {
  for (uint32_t i = aIndex; i < mCurrentPEMs.Length(); i++) {
    if (mCurrentPEMs[i]->Supports(aConfig).isEmpty()) {
      continue;
    }
    return CreateEncoderWithPEM(mCurrentPEMs[i], aConfig, aTaskQueue)
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [](RefPtr<MediaDataEncoder>&& aEncoder) {
              return PlatformEncoderModule::CreateEncoderPromise::
                  CreateAndResolve(std::move(aEncoder), __func__);
            },
            [self = RefPtr{this}, i, config = aConfig,
             aTaskQueue](const MediaResult& aError) mutable {
              // Try the next PEM.
              return self->CheckAndMaybeCreateEncoder(config, i + 1,
                                                      aTaskQueue);
            });
  }
  return PlatformEncoderModule::CreateEncoderPromise::CreateAndReject(
      MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                  nsPrintfCString("Error no encoder found for %s",
                                  EnumValueToString(aConfig.mCodec))
                      .get()),
      __func__);
}

RefPtr<PlatformEncoderModule::CreateEncoderPromise>
PEMFactory::CreateEncoderWithPEM(PlatformEncoderModule* aPEM,
                                 const EncoderConfig& aConfig,
                                 const RefPtr<TaskQueue>& aTaskQueue) {
  MOZ_ASSERT(aPEM);
  MediaResult result = NS_OK;

  if (aConfig.IsAudio()) {
    return aPEM->AsyncCreateEncoder(aConfig, aTaskQueue)
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [config = aConfig](RefPtr<MediaDataEncoder>&& aEncoder) {
              RefPtr<MediaDataEncoder> decoder = std::move(aEncoder);
              return PlatformEncoderModule::CreateEncoderPromise::
                  CreateAndResolve(decoder, __func__);
            },
            [](const MediaResult& aError) {
              return PlatformEncoderModule::CreateEncoderPromise::
                  CreateAndReject(aError, __func__);
            });
  }

  if (!aConfig.IsVideo()) {
    return PlatformEncoderModule::CreateEncoderPromise::CreateAndReject(
        MediaResult(
            NS_ERROR_DOM_MEDIA_FATAL_ERR,
            RESULT_DETAIL(
                "Encoder configuration error, expected audio or video.")),
        __func__);
  }

  return aPEM->AsyncCreateEncoder(aConfig, aTaskQueue);
}

EncodeSupportSet PEMFactory::Supports(const EncoderConfig& aConfig) const {
  RefPtr<PlatformEncoderModule> found;
  for (const auto& m : mCurrentPEMs) {
    EncodeSupportSet supports = m->Supports(aConfig);
    if (!supports.isEmpty()) {
      // TODO name
      LOG("Checking if {} supports codec {}: yes", m->GetName(),
          EnumValueToString(aConfig.mCodec));
      return supports;
    }
    LOG("Checking if {} supports codec {}: no", m->GetName(),
        EnumValueToString(aConfig.mCodec));
  }
  return EncodeSupportSet{};
}

RefPtr<PEMSupportsEncoderPromise> PEMFactory::SupportsAsync(
    const EncoderConfig& aConfig) const {
  nsTArray<RefPtr<PEMSupportsEncoderPromise>> promises(mCurrentPEMs.Length());
  for (const auto& m : mCurrentPEMs) {
    if (!m->Supports(aConfig).isEmpty()) {
      promises.AppendElement(m->SupportsAsync(aConfig));
    }
  }
  return PEMSupportsEncoderPromise::AllSettled(GetCurrentSerialEventTarget(),
                                               promises)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [](CopyableTArray<PEMSupportsEncoderPromise::ResolveOrRejectValue>&&
                 aValues) -> RefPtr<PEMSupportsEncoderPromise> {
            EncodeSupportSet support{};
            for (const auto& value : aValues) {
              if (value.IsResolve()) {
                support += value.ResolveValue();
              }
            }
            return PEMSupportsEncoderPromise::CreateAndResolve(support,
                                                               __func__);
          },
          [] {
            MOZ_CRASH("AllSettled does not reject");
            return RefPtr<PEMSupportsEncoderPromise>(nullptr);
          });
}

Maybe<uint32_t> PEMFactory::MaxPixelsPerFrame(CodecType aCodec) const {
  Maybe<uint32_t> max;
  for (const auto& m : mCurrentPEMs) {
    if (m->SupportsCodec(aCodec).isEmpty()) {
      continue;
    }
    Maybe<uint32_t> pemMax = m->MaxPixelsPerFrame(aCodec);
    if (!pemMax) {
      return Nothing();
    }
    if (!max || *pemMax > *max) {
      max = pemMax;
    }
  }
  return max;
}

EncodeSupportSet PEMFactory::SupportsCodec(CodecType aCodec) const {
  EncodeSupportSet supports{};
  for (const auto& m : mCurrentPEMs) {
    EncodeSupportSet pemSupports = m->SupportsCodec(aCodec);
    // TODO name
    LOG("Checking if {} supports codec {}: {}", m->GetName(),
        static_cast<int>(aCodec), pemSupports.isEmpty() ? "no" : "yes");
    supports += pemSupports;
  }
  if (supports.isEmpty()) {
    LOG("No PEM support {}", static_cast<int>(aCodec));
  }
  return supports;
}

already_AddRefed<PlatformEncoderModule> PEMFactory::FindPEM(
    const EncoderConfig& aConfig) const {
  RefPtr<PlatformEncoderModule> found;
  for (const auto& m : mCurrentPEMs) {
    if (!m->Supports(aConfig).isEmpty()) {
      found = m;
      break;
    }
  }
  return found.forget();
}

StaticMutex PEMFactory::sSupportedMutex;

/* static */
MediaCodecsSupported PEMFactory::Supported(bool aForceRefresh) {
  StaticMutexAutoLock lock(sSupportedMutex);

  static auto calculate = []() {
    auto pem = MakeRefPtr<PEMFactory>();
    MediaCodecsSupported supported;
    for (const auto& cd : MCSInfo::GetAllCodecDefinitions()) {
      auto codecType = MediaCodecToCodecType(cd.codec);
      if (codecType == CodecType::Unknown) {
        continue;
      }
      supported += MCSInfo::GetEncodeMediaCodecsSupported(
          cd.codec, pem->SupportsCodec(codecType));
    }
    return supported;
  };

  static MediaCodecsSupported supported = calculate();
  if (aForceRefresh) {
    supported = calculate();
  }

  return supported;
}

/* static */
media::EncodeSupportSet PEMFactory::SupportsCodec(
    CodecType aCodec, const MediaCodecsSupported& aSupported,
    RemoteMediaIn aLocation) {
  const TrackSupportSet supports =
      RemoteMediaManagerChild::GetTrackSupport(aLocation);

  if (supports.contains(TrackSupport::EncodeVideo)) {
    switch (aCodec) {
      case CodecType::H264:
        return media::MCSInfo::GetEncodeSupportSet(MediaCodec::H264,
                                                   aSupported);
      case CodecType::H265:
        return media::MCSInfo::GetEncodeSupportSet(MediaCodec::HEVC,
                                                   aSupported);
      case CodecType::VP8:
        return media::MCSInfo::GetEncodeSupportSet(MediaCodec::VP8, aSupported);
      case CodecType::VP9:
        return media::MCSInfo::GetEncodeSupportSet(MediaCodec::VP9, aSupported);
      case CodecType::AV1:
        return media::MCSInfo::GetEncodeSupportSet(MediaCodec::AV1, aSupported);
      default:
        break;
    }
  }

  if (supports.contains(TrackSupport::EncodeAudio)) {
    switch (aCodec) {
      case CodecType::Opus:
        return media::MCSInfo::GetEncodeSupportSet(MediaCodec::Opus,
                                                   aSupported);
      case CodecType::Vorbis:
        return media::MCSInfo::GetEncodeSupportSet(MediaCodec::Vorbis,
                                                   aSupported);
      case CodecType::Flac:
        return media::MCSInfo::GetEncodeSupportSet(MediaCodec::FLAC,
                                                   aSupported);
      case CodecType::AAC:
        return media::MCSInfo::GetEncodeSupportSet(MediaCodec::AAC, aSupported);
      case CodecType::PCM:
      case CodecType::G722:
      default:
        break;
    }
  }

  return media::EncodeSupportSet{};
}

}  // namespace mozilla

#undef LOGE
#undef LOG
