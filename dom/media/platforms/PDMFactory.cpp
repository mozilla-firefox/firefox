/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PDMFactory.h"

#include "AOMDecoder.h"
#include "AgnosticDecoderModule.h"
#include "AllocationPolicy.h"
#include "AudioTrimmer.h"
#include "BlankDecoderModule.h"
#include "DecoderDoctorDiagnostics.h"
#include "EMEDecoderModule.h"
#include "GMPDecoderModule.h"
#include "H264.h"
#include "MP4Decoder.h"
#include "MediaChangeMonitor.h"
#include "MediaInfo.h"
#include "PDMFactorySupport.h"
#include "VPXDecoder.h"
#include "VideoUtils.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/RemoteDecodeUtils.h"
#include "mozilla/RemoteDecoderModule.h"
#include "mozilla/RemoteMediaManagerChild.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/ipc/UtilityMediaServiceParent.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "nsIXULRuntime.h"  // for BrowserTabsRemoteAutostart
#include "nsPrintfCString.h"

#ifdef XP_WIN
#  include "WMFDecoderModule.h"
#  ifdef MOZ_WMF_MEDIA_ENGINE
#    include "MFMediaEngineDecoderModule.h"
#  endif
#  ifdef MOZ_WMF_CDM
#    include "mozilla/CDMProxy.h"
#  endif
#endif
#ifdef MOZ_FFMPEG
#  include "FFmpegRuntimeLinker.h"
#endif
#ifdef MOZ_APPLEMEDIA
#  include "AppleDecoderModule.h"
#endif
#ifdef MOZ_WIDGET_ANDROID
#  include "AndroidDecoderModule.h"
#endif
#ifdef MOZ_OMX
#  include "OmxDecoderModule.h"
#endif
#include <functional>

#include "FFVPXRuntimeLinker.h"

using DecodeSupport = mozilla::media::DecodeSupport;
using DecodeSupportSet = mozilla::media::DecodeSupportSet;
using MediaCodec = mozilla::media::MediaCodec;
using MediaCodecsSupport = mozilla::media::MediaCodecsSupport;
using MediaCodecsSupported = mozilla::media::MediaCodecsSupported;
using MCSInfo = mozilla::media::MCSInfo;

namespace mozilla {

#define PDM_INIT_LOG(msg, ...) \
  MOZ_LOG_FMT(sPDMLog, LogLevel::Debug, "PDMInitializer, " msg, ##__VA_ARGS__)

extern already_AddRefed<PlatformDecoderModule> CreateNullDecoderModule();

constinit static StaticDataMutex<StaticRefPtr<PlatformDecoderModule>>
    sForcedPDM("Forced PDM");

class PDMInitializer final {
 public:
  // This function should only be executed ONCE per process.
  static void InitPDMs();

  // Return true if we've finished PDMs initialization.
  static bool HasInitializedPDMs();

 private:
  static void InitGpuPDMs() {
#ifdef XP_WIN
    WMFDecoderModule::Init();
    if (StaticPrefs::media_ffvpx_hw_enabled()) {
      FFVPXRuntimeLinker::Init();
    }
#elif defined(MOZ_WIDGET_ANDROID)
    if (StaticPrefs::media_gpu_process_decoder()) {
      FFVPXRuntimeLinker::Init();
    }
#endif
  }

  static void InitRddPDMs() {
#ifdef XP_WIN
    WMFDecoderModule::Init();
#endif
#ifdef MOZ_APPLEMEDIA
    AppleDecoderModule::Init();
#endif
#ifdef MOZ_FFMPEG
    if (StaticPrefs::media_rdd_ffmpeg_enabled()) {
      FFmpegRuntimeLinker::Init();
    }
#endif
    FFVPXRuntimeLinker::Init();
  }

  static void InitUtilityPDMs() {
    const ipc::SandboxingKind kind = GetCurrentSandboxingKind();
#ifdef XP_WIN
    if (kind == ipc::SandboxingKind::UTILITY_AUDIO_DECODING_WMF) {
      WMFDecoderModule::Init();
    }
#  ifdef MOZ_WMF_MEDIA_ENGINE
    if (StaticPrefs::media_wmf_media_engine_enabled() &&
        kind == ipc::SandboxingKind::MF_MEDIA_ENGINE_CDM) {
      MFMediaEngineDecoderModule::Init();
    }
#  endif
#endif
#ifdef MOZ_APPLEMEDIA
    if (kind == ipc::SandboxingKind::UTILITY_AUDIO_DECODING_APPLE_MEDIA) {
      AppleDecoderModule::Init();
    }
#endif
    if (kind == ipc::SandboxingKind::GENERIC_UTILITY) {
      FFVPXRuntimeLinker::Init();
    }
#ifdef MOZ_FFMPEG
    if (kind == ipc::SandboxingKind::GENERIC_UTILITY) {
      FFmpegRuntimeLinker::Init();
    }
#endif
  }

  static void InitContentPDMs() {
#if !defined(MOZ_WIDGET_ANDROID)  // Still required for video?
    if (StaticPrefs::media_allow_audio_non_utility()) {
#endif  // !defined(MOZ_WIDGET_ANDROID)
#ifdef XP_WIN
#  ifdef MOZ_WMF
      if (!StaticPrefs::media_rdd_process_enabled() ||
          !StaticPrefs::media_rdd_wmf_enabled()) {
        WMFDecoderModule::Init();
      }
#  endif
#endif
#ifdef MOZ_APPLEMEDIA
      AppleDecoderModule::Init();
#endif
#ifdef MOZ_OMX
      OmxDecoderModule::Init();
#endif
      FFVPXRuntimeLinker::Init();
#ifdef MOZ_FFMPEG
      FFmpegRuntimeLinker::Init();
#endif
#if !defined(MOZ_WIDGET_ANDROID)  // Still required for video?
    }
#endif  // !defined(MOZ_WIDGET_ANDROID)

    RemoteMediaManagerChild::Init();
  }

  static void InitDefaultPDMs() {
#ifdef XP_WIN
    WMFDecoderModule::Init();
#endif
#ifdef MOZ_APPLEMEDIA
    AppleDecoderModule::Init();
#endif
#ifdef MOZ_OMX
    OmxDecoderModule::Init();
#endif
    FFVPXRuntimeLinker::Init();
#ifdef MOZ_FFMPEG
    FFmpegRuntimeLinker::Init();
#endif
  }

  static bool sHasInitializedPDMs;
  static StaticMutex sMonitor MOZ_UNANNOTATED;
};

bool PDMInitializer::sHasInitializedPDMs = false;
StaticMutex PDMInitializer::sMonitor;

/* static */
void PDMInitializer::InitPDMs() {
  StaticMutexAutoLock mon(sMonitor);
  if (sHasInitializedPDMs) {
    return;
  }
  if (XRE_IsGPUProcess()) {
    PDM_INIT_LOG("Init PDMs in GPU process");
    InitGpuPDMs();
  } else if (XRE_IsRDDProcess()) {
    PDM_INIT_LOG("Init PDMs in RDD process");
    InitRddPDMs();
  } else if (XRE_IsUtilityProcess()) {
    PDM_INIT_LOG("Init PDMs in Utility process");
    InitUtilityPDMs();
  } else if (XRE_IsContentProcess()) {
    PDM_INIT_LOG("Init PDMs in Content process");
    InitContentPDMs();
  } else {
    MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess(),
                          "PDMFactory is only usable in the "
                          "Parent/GPU/RDD/Utility/Content process");
    PDM_INIT_LOG("Init PDMs in Chrome process");
    InitDefaultPDMs();
  }
  sHasInitializedPDMs = true;
}

/* static */
bool PDMInitializer::HasInitializedPDMs() {
  StaticMutexAutoLock mon(sMonitor);
  return sHasInitializedPDMs;
}

class SupportChecker {
 public:
  enum class Reason : uint8_t {
    kSupported,
    kVideoFormatNotSupported,
    kAudioFormatNotSupported,
    kUnknown,
  };

  struct CheckResult {
    explicit CheckResult(Reason aReason,
                         MediaResult aResult = MediaResult(NS_OK))
        : mReason(aReason), mMediaResult(std::move(aResult)) {}
    CheckResult(const CheckResult& aOther) = default;
    CheckResult(CheckResult&& aOther) = default;
    CheckResult& operator=(const CheckResult& aOther) = default;
    CheckResult& operator=(CheckResult&& aOther) = default;

    Reason mReason;
    MediaResult mMediaResult;
  };

  template <class Func>
  void AddToCheckList(Func&& aChecker) {
    mCheckerList.AppendElement(std::forward<Func>(aChecker));
  }

  void AddMediaFormatChecker(const TrackInfo& aTrackConfig) {
    if (aTrackConfig.IsVideo()) {
      auto mimeType = aTrackConfig.GetAsVideoInfo()->mMimeType;
      RefPtr<MediaByteBuffer> extraData =
          aTrackConfig.GetAsVideoInfo()->mExtraData;
      AddToCheckList(
          [mimeType = std::move(mimeType), extraData = std::move(extraData)]() {
#if defined(XP_WIN) || defined(XP_DARWIN)
            if (MP4Decoder::IsH264(mimeType)) {
              SPSData spsdata;
              // WMF H.264 Video Decoder and Apple ATDecoder
              // do not support YUV444 format.
              if (H264::DecodeSPSFromExtraData(extraData, spsdata) &&
                  (spsdata.profile_idc == 244 /* Hi444PP */ ||
                   spsdata.chroma_format_idc == PDMFactory::kYUV444)) {
                return CheckResult(
                    SupportChecker::Reason::kVideoFormatNotSupported,
                    MediaResult(
                        NS_ERROR_DOM_MEDIA_FATAL_ERR,
                        RESULT_DETAIL("Decoder may not have the capability "
                                      "to handle the requested video format "
                                      "with YUV444 chroma subsampling.")));
              }
            }
#endif
            return CheckResult(SupportChecker::Reason::kSupported);
          });
    }
  }

  SupportChecker::CheckResult Check() {
    for (auto& checker : mCheckerList) {
      auto result = checker();
      if (result.mReason != SupportChecker::Reason::kSupported) {
        return result;
      }
    }
    return CheckResult(SupportChecker::Reason::kSupported);
  }

  void Clear() { mCheckerList.Clear(); }

 private:
  nsTArray<std::function<CheckResult()>> mCheckerList;
};  // SupportChecker

PDMFactory::PDMFactory() {
  EnsureInit();
  CreatePDMs();
  CreateNullPDM();
}

PDMFactory::~PDMFactory() = default;

/* static */
void PDMFactory::EnsureInit() {
  if (PDMInitializer::HasInitializedPDMs()) {
    return;
  }
  auto initalizationGfxVarsAndPreferences = []() {
    MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
    // Ensure that all system variables are initialized.
    gfx::gfxVars::Initialize();
    // Prime the preferences system from the main thread.
    (void)BrowserTabsRemoteAutostart();
  };
  // There are some initialization needed to be done on the main thread.
  if (!gfx::gfxVars::IsInitialized()) {
    if (NS_IsMainThread()) {
      initalizationGfxVarsAndPreferences();
    } else {
      nsCOMPtr<nsIEventTarget> mainTarget = GetMainThreadSerialEventTarget();
      nsCOMPtr<nsIRunnable> runnable =
          NS_NewRunnableFunction("PDMFactory::EnsureInit",
                                 std::move(initalizationGfxVarsAndPreferences));
      SyncRunnable::DispatchToThread(mainTarget, runnable);
    }
  }
  PDMInitializer::InitPDMs();
}

RefPtr<PlatformDecoderModule::CreateDecoderPromise> PDMFactory::CreateDecoder(
    const CreateDecoderParams& aParams) {
  if (aParams.mUseNullDecoder.mUse) {
    MOZ_ASSERT(mNullPDM);
    return CreateDecoderWithPDM(mNullPDM, aParams);
  }
  bool isEncrypted =
      mEMEPDM && (aParams.mConfig.mCrypto.IsEncrypted() ||
                  aParams.mEncryptedCustomIdent ==
                      CreateDecoderParams::EncryptedCustomIdent::True);

  if (isEncrypted) {
    return CreateDecoderWithPDM(mEMEPDM, aParams);
  }

  return CheckAndMaybeCreateDecoder(CreateDecoderParamsForAsync(aParams), 0);
}

RefPtr<PlatformDecoderModule::CreateDecoderPromise>
PDMFactory::CheckAndMaybeCreateDecoder(CreateDecoderParamsForAsync&& aParams,
                                       uint32_t aIndex,
                                       Maybe<MediaResult> aEarlierError) {
  uint32_t i = aIndex;
  auto params = SupportDecoderParams(aParams);
  for (; i < mCurrentPDMs.Length(); i++) {
    if (mCurrentPDMs[i]->Supports(params, nullptr /* diagnostic */).isEmpty()) {
      continue;
    }
    RefPtr<PlatformDecoderModule::CreateDecoderPromise> p =
        CreateDecoderWithPDM(mCurrentPDMs[i], aParams)
            ->Then(
                GetCurrentSerialEventTarget(), __func__,
                [](RefPtr<MediaDataDecoder>&& aDecoder) {
                  return PlatformDecoderModule::CreateDecoderPromise::
                      CreateAndResolve(std::move(aDecoder), __func__);
                },
                [self = RefPtr{this}, i, params = std::move(aParams)](
                    const MediaResult& aError) mutable {
                  // Try the next PDM.
                  return self->CheckAndMaybeCreateDecoder(std::move(params),
                                                          i + 1, Some(aError));
                });
    return p;
  }
  if (aEarlierError) {
    return PlatformDecoderModule::CreateDecoderPromise::CreateAndReject(
        std::move(*aEarlierError), __func__);
  }
  return PlatformDecoderModule::CreateDecoderPromise::CreateAndReject(
      MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                  nsPrintfCString("Error no decoder found for %s",
                                  aParams.mConfig->mMimeType.get())
                      .get()),
      __func__);
}

RefPtr<PlatformDecoderModule::CreateDecoderPromise>
PDMFactory::CreateDecoderWithPDM(PlatformDecoderModule* aPDM,
                                 const CreateDecoderParams& aParams) {
  MOZ_ASSERT(aPDM);
  MediaResult result = NS_OK;

  SupportChecker supportChecker;
  const TrackInfo& config = aParams.mConfig;
  supportChecker.AddMediaFormatChecker(config);

  auto checkResult = supportChecker.Check();
  if (checkResult.mReason != SupportChecker::Reason::kSupported) {
    if (checkResult.mReason ==
        SupportChecker::Reason::kVideoFormatNotSupported) {
      result = checkResult.mMediaResult;
    } else if (checkResult.mReason ==
               SupportChecker::Reason::kAudioFormatNotSupported) {
      result = checkResult.mMediaResult;
    }
    return PlatformDecoderModule::CreateDecoderPromise::CreateAndReject(
        result, __func__);
  }

  if (config.IsAudio()) {
    if (MP4Decoder::IsAAC(config.mMimeType) && !aParams.mUseNullDecoder.mUse &&
        aParams.mWrappers.contains(media::Wrapper::MediaChangeMonitor)) {
      // If AudioTrimmer is needed, MediaChangeMonitor will request it.
      return MediaChangeMonitor::Create(this, aParams);
    }
    RefPtr<PlatformDecoderModule::CreateDecoderPromise> p;
    p = aPDM->AsyncCreateDecoder(aParams)->Then(
        GetCurrentSerialEventTarget(), __func__,
        [params = CreateDecoderParamsForAsync(aParams)](
            RefPtr<MediaDataDecoder>&& aDecoder) {
          RefPtr<MediaDataDecoder> decoder = std::move(aDecoder);
          if (params.mWrappers.contains(media::Wrapper::AudioTrimmer)) {
            decoder = new AudioTrimmer(decoder.forget());
          }
          return PlatformDecoderModule::CreateDecoderPromise::CreateAndResolve(
              decoder, __func__);
        },
        [](const MediaResult& aError) {
          return PlatformDecoderModule::CreateDecoderPromise::CreateAndReject(
              aError, __func__);
        });
    return p;
  }

  if (!config.IsVideo()) {
    return PlatformDecoderModule::CreateDecoderPromise::CreateAndReject(
        MediaResult(
            NS_ERROR_DOM_MEDIA_FATAL_ERR,
            RESULT_DETAIL(
                "Decoder configuration error, expected audio or video.")),
        __func__);
  }

  if ((MP4Decoder::IsH264(config.mMimeType) ||
       AOMDecoder::IsAV1(config.mMimeType) ||
       VPXDecoder::IsVPX(config.mMimeType) ||
       MP4Decoder::IsHEVC(config.mMimeType)) &&
      !aParams.mUseNullDecoder.mUse &&
      aParams.mWrappers.contains(media::Wrapper::MediaChangeMonitor)) {
    return MediaChangeMonitor::Create(this, aParams);
  }

  // If we were not given a compositor, share the decoded frames with the
  // compositor via the image bridge. It is replaced when the compositor is
  // reinitialized, so we get the current one each time we create a decoder,
  // rather than storing it in the parameters kept by our callers.
  if (!aParams.mKnowsCompositor &&
      (XRE_IsContentProcess() || XRE_IsParentProcess())) {
    RefPtr<layers::KnowsCompositor> knowsCompositor =
        layers::ImageBridgeChild::GetSingleton();
    // The image bridge only knows about the compositor once one has been
    // created for a window, e.g. not in gtests.
    if (knowsCompositor && knowsCompositor->GetCompositorBackendType() !=
                               layers::LayersBackend::LAYERS_NONE) {
      CreateDecoderParams params(aParams);
      params.mKnowsCompositor = knowsCompositor;
      return aPDM->AsyncCreateDecoder(params);
    }
  }
  return aPDM->AsyncCreateDecoder(aParams);
}

DecodeSupportSet PDMFactory::SupportsMimeType(
    const nsACString& aMimeType) const {
  UniquePtr<TrackInfo> trackInfo = CreateTrackInfoWithMIMEType(aMimeType);
  if (!trackInfo) {
    return DecodeSupportSet{};
  }
  return Supports(SupportDecoderParams(*trackInfo), nullptr);
}

DecodeSupportSet PDMFactory::Supports(
    const SupportDecoderParams& aParams,
    DecoderDoctorDiagnostics* aDiagnostics) const {
  if (mEMEPDM) {
    return mEMEPDM->Supports(aParams, aDiagnostics);
  }

  RefPtr<PlatformDecoderModule> current =
      GetDecoderModule(aParams, aDiagnostics);

  if (!current) {
    return DecodeSupportSet{};
  }

  // We have a PDM - check for + return SW/HW support info
  return current->Supports(aParams, aDiagnostics);
}

RefPtr<PDMSupportsDecoderPromise> PDMFactory::SupportsAsync(
    const SupportDecoderParams& aParams) const {
  if (mEMEPDM) {
    return mEMEPDM->SupportsAsync(aParams);
  }

  nsTArray<RefPtr<PDMSupportsDecoderPromise>> promises(mCurrentPDMs.Length());
  for (const auto& m : mCurrentPDMs) {
    if (!m->Supports(aParams, nullptr).isEmpty()) {
      promises.AppendElement(m->SupportsAsync(aParams));
    }
  }
  return PDMSupportsDecoderPromise::AllSettled(GetCurrentSerialEventTarget(),
                                               promises)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [mimeType = aParams.mConfig.mMimeType](
              CopyableTArray<PDMSupportsDecoderPromise::ResolveOrRejectValue>&&
                  aValues) -> RefPtr<PDMSupportsDecoderPromise> {
            DecodeSupportSet support{};
            for (const auto& value : aValues) {
              if (value.IsResolve()) {
                support += value.ResolveValue();
              }
            }
            return PDMSupportsDecoderPromise::CreateAndResolve(support,
                                                               __func__);
          },
          [] {
            MOZ_CRASH("AllSettled does not reject");
            return RefPtr<PDMSupportsDecoderPromise>(nullptr);
          });
}

/* static */
RefPtr<PDMSupportsDecoderPromise> PDMFactory::StrictSupportsAsync(
    const CreateDecoderParams& aParams, AllocPolicy* aPolicy) {
  return AllocationWrapper::CreateDecoder(aParams, aPolicy)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [](AllocationWrapper::AllocateDecoderPromise::ResolveOrRejectValue&&
                 aValue) -> RefPtr<PDMSupportsDecoderPromise> {
            if (aValue.IsReject()) {
              // The decoder could not be created: report unsupported.
              return PDMSupportsDecoderPromise::CreateAndResolve(
                  DecodeSupportSet{}, __func__);
            }
            RefPtr<MediaDataDecoder> decoder = std::move(aValue.ResolveValue());
            return decoder->Init()->Then(
                GetCurrentSerialEventTarget(), __func__,
                [decoder](
                    MediaDataDecoder::InitPromise::ResolveOrRejectValue&& aInit)
                    -> RefPtr<PDMSupportsDecoderPromise> {
                  DecodeSupportSet support{};
                  if (aInit.IsResolve()) {
                    nsAutoCString reason;
                    support += decoder->IsHardwareAccelerated(reason)
                                   ? DecodeSupport::HardwareDecode
                                   : DecodeSupport::SoftwareDecode;
                  }
                  // Keep the decoder alive until its shutdown completes.
                  decoder->Shutdown()->Then(
                      GetCurrentSerialEventTarget(), __func__,
                      [decoder](const ShutdownPromise::ResolveOrRejectValue&) {
                      });
                  return PDMSupportsDecoderPromise::CreateAndResolve(support,
                                                                     __func__);
                });
          });
}

/* static */
void PDMFactory::ForcePDM(PlatformDecoderModule* aPDM) {
  auto forced = sForcedPDM.Lock();
  *forced = aPDM;
}

void PDMFactory::CreatePDMs() {
  {
    auto forced = sForcedPDM.Lock();
    if (*forced) {
      StartupPDM(do_AddRef(*forced));
      return;
    }
  }
  if (StaticPrefs::media_use_blank_decoder()) {
    StartupPDM(BlankDecoderModule::Create());
    // The Blank PDM SupportsMimeType reports true for all codecs; the creation
    // of its decoder is infallible. As such it will be used for all media, we
    // can stop creating more PDM from this point.
    return;
  }

  if (XRE_IsGPUProcess()) {
    CreateGpuPDMs();
  } else if (XRE_IsRDDProcess()) {
    CreateRddPDMs();
  } else if (XRE_IsUtilityProcess()) {
    CreateUtilityPDMs();
  } else if (XRE_IsContentProcess()) {
    CreateContentPDMs();
  } else {
    MOZ_DIAGNOSTIC_ASSERT(
        XRE_IsParentProcess(),
        "PDMFactory is only usable in the Parent/GPU/RDD/Content process");
    CreateDefaultPDMs();
  }
}

void PDMFactory::CreateGpuPDMs() {
#ifdef XP_WIN
  if (StaticPrefs::media_ffvpx_hw_enabled()) {
    StartupPDM(FFVPXRuntimeLinker::CreateDecoder());
  }
  if (StaticPrefs::media_wmf_enabled()) {
    StartupPDM(WMFDecoderModule::Create());
  }
#elif defined(MOZ_WIDGET_ANDROID)
  if (StaticPrefs::media_gpu_process_decoder()) {
    StartupPDM(FFVPXRuntimeLinker::CreateDecoder());
  }
#endif
}

#if defined(MOZ_FFMPEG)
static DecoderDoctorDiagnostics::Flags GetFailureFlagBasedOnFFmpegStatus(
    const FFmpegRuntimeLinker::LinkStatus& aStatus) {
  switch (aStatus) {
    case FFmpegRuntimeLinker::LinkStatus_INVALID_FFMPEG_CANDIDATE:
    case FFmpegRuntimeLinker::LinkStatus_UNUSABLE_LIBAV57:
    case FFmpegRuntimeLinker::LinkStatus_INVALID_LIBAV_CANDIDATE:
    case FFmpegRuntimeLinker::LinkStatus_OBSOLETE_FFMPEG:
    case FFmpegRuntimeLinker::LinkStatus_OBSOLETE_LIBAV:
    case FFmpegRuntimeLinker::LinkStatus_INVALID_CANDIDATE:
      return DecoderDoctorDiagnostics::Flags::LibAVCodecUnsupported;
    default:
      MOZ_DIAGNOSTIC_ASSERT(
          aStatus == FFmpegRuntimeLinker::LinkStatus_NOT_FOUND,
          "Only call this method when linker fails.");
      return DecoderDoctorDiagnostics::Flags::FFmpegNotFound;
  }
}
#endif

void PDMFactory::CreateRddPDMs() {
#ifdef XP_WIN
  if (StaticPrefs::media_wmf_enabled() &&
      StaticPrefs::media_rdd_wmf_enabled()) {
    StartupPDM(WMFDecoderModule::Create());
  }
#endif
#ifdef MOZ_APPLEMEDIA
  if (StaticPrefs::media_rdd_applemedia_enabled()) {
    StartupPDM(AppleDecoderModule::Create());
  }
#endif
  StartupPDM(FFVPXRuntimeLinker::CreateDecoder());
#ifdef MOZ_FFMPEG
  if (StaticPrefs::media_ffmpeg_enabled() &&
      StaticPrefs::media_rdd_ffmpeg_enabled()) {
    // Prefer system FFmpeg first only when Vulkan is wanted and
    // media.hardware-video-decoding-vulkan.prefer-system-ffmpeg is true;
    // otherwise leave ffvpx first.
    const bool preferSystemForVulkan =
        gfx::gfxVars::CanUseVulkanHardwareVideoDecoding() &&
        FFmpegRuntimeLinker::PreferSystemFFmpegForVulkan();
    PDM_INIT_LOG("Insert system FFmpeg before ffvpx: {}",
                 preferSystemForVulkan);
    if (!StartupPDM(FFmpegRuntimeLinker::CreateDecoder(),
                    preferSystemForVulkan)) {
      mFailureFlags += GetFailureFlagBasedOnFFmpegStatus(
          FFmpegRuntimeLinker::LinkStatusCode());
    }
  }
#endif
  StartupPDM(AgnosticDecoderModule::Create(),
             StaticPrefs::media_prefer_non_ffvpx());

  PDM_INIT_LOG("RDD PDM order:");
  int i = 0;
  for (const auto& pdm : mCurrentPDMs) {
    PDM_INIT_LOG("{}: {}", i++, pdm->Name());
  }
}

void PDMFactory::CreateUtilityPDMs() {
  const ipc::SandboxingKind aKind = GetCurrentSandboxingKind();
#ifdef XP_WIN
  if (StaticPrefs::media_wmf_enabled() &&
      aKind == ipc::SandboxingKind::UTILITY_AUDIO_DECODING_WMF) {
    StartupPDM(WMFDecoderModule::Create());
  }
#endif
#ifdef MOZ_APPLEMEDIA
  if (aKind == ipc::SandboxingKind::UTILITY_AUDIO_DECODING_APPLE_MEDIA) {
    StartupPDM(AppleDecoderModule::Create());
  }
#endif
  if (aKind == ipc::SandboxingKind::GENERIC_UTILITY) {
    StartupPDM(FFVPXRuntimeLinker::CreateDecoder());
#ifdef MOZ_FFMPEG
    if (StaticPrefs::media_ffmpeg_enabled() &&
        !StartupPDM(FFmpegRuntimeLinker::CreateDecoder())) {
      mFailureFlags += GetFailureFlagBasedOnFFmpegStatus(
          FFmpegRuntimeLinker::LinkStatusCode());
    }
#endif
#ifdef MOZ_WIDGET_ANDROID
    if (StaticPrefs::media_utility_android_media_codec_enabled()) {
      StartupPDM(AndroidDecoderModule::Create(),
                 StaticPrefs::media_android_media_codec_preferred());
    }
#endif
    StartupPDM(AgnosticDecoderModule::Create(),
               StaticPrefs::media_prefer_non_ffvpx());
  }
#ifdef MOZ_WMF_MEDIA_ENGINE
  if (aKind == ipc::SandboxingKind::MF_MEDIA_ENGINE_CDM) {
    if (StaticPrefs::media_wmf_media_engine_enabled()) {
      StartupPDM(MFMediaEngineDecoderModule::Create());
    }
  }
#endif

  PDM_INIT_LOG("Utility PDM order:");
  int i = 0;
  for (const auto& pdm : mCurrentPDMs) {
    PDM_INIT_LOG("{}: {}", i++, pdm->Name());
  }
}

void PDMFactory::CreateContentPDMs() {
  if (StaticPrefs::media_gpu_process_decoder()) {
    StartupPDM(RemoteDecoderModule::Create(RemoteMediaIn::GpuProcess));
  }

  if (StaticPrefs::media_rdd_process_enabled()) {
    StartupPDM(RemoteDecoderModule::Create(RemoteMediaIn::RddProcess));
  }

  if (StaticPrefs::media_utility_process_enabled()) {
#ifdef MOZ_APPLEMEDIA
    StartupPDM(
        RemoteDecoderModule::Create(RemoteMediaIn::UtilityProcess_AppleMedia));
#endif
#ifdef XP_WIN
    StartupPDM(RemoteDecoderModule::Create(RemoteMediaIn::UtilityProcess_WMF));
#endif
    // WMF and AppleMedia should be created before Generic because the order
    // affects what decoder module would be chose first.
    StartupPDM(
        RemoteDecoderModule::Create(RemoteMediaIn::UtilityProcess_Generic));
  }
#ifdef MOZ_WMF_MEDIA_ENGINE
  if (StaticPrefs::media_wmf_media_engine_enabled()) {
    StartupPDM(RemoteDecoderModule::Create(
        RemoteMediaIn::UtilityProcess_MFMediaEngineCDM));
  }
#endif

#if !defined(MOZ_WIDGET_ANDROID)  // Still required for video?
  if (StaticPrefs::media_allow_audio_non_utility()) {
#endif  // !defined(MOZ_WIDGET_ANDROID)
#ifdef XP_WIN
    if (StaticPrefs::media_wmf_enabled()) {
#  ifdef MOZ_WMF
      if (!StaticPrefs::media_rdd_process_enabled() ||
          !StaticPrefs::media_rdd_wmf_enabled()) {
        if (!StartupPDM(WMFDecoderModule::Create())) {
          mFailureFlags += DecoderDoctorDiagnostics::Flags::WMFFailedToLoad;
        }
      }
#  endif
    } else if (StaticPrefs::media_decoder_doctor_wmf_disabled_is_failure()) {
      mFailureFlags += DecoderDoctorDiagnostics::Flags::WMFFailedToLoad;
    }
#endif

#ifdef MOZ_APPLEMEDIA
    StartupPDM(AppleDecoderModule::Create());
#endif
#ifdef MOZ_OMX
    if (StaticPrefs::media_omx_enabled()) {
      StartupPDM(OmxDecoderModule::Create());
    }
#endif
    StartupPDM(FFVPXRuntimeLinker::CreateDecoder());
#ifdef MOZ_FFMPEG
    if (StaticPrefs::media_ffmpeg_enabled() &&
        !StartupPDM(FFmpegRuntimeLinker::CreateDecoder())) {
      mFailureFlags += GetFailureFlagBasedOnFFmpegStatus(
          FFmpegRuntimeLinker::LinkStatusCode());
    }
#endif

    StartupPDM(AgnosticDecoderModule::Create(),
               StaticPrefs::media_prefer_non_ffvpx());
#if !defined(MOZ_WIDGET_ANDROID)  // Still required for video?
  }
#endif  // !defined(MOZ_WIDGET_ANDROID)

  // Android still needs this, the actual decoder is remoted on java side
#ifdef MOZ_WIDGET_ANDROID
  if (AndroidDecoderModule::IsJavaDecoderModuleAllowed()) {
    StartupPDM(AndroidDecoderModule::Create(),
               StaticPrefs::media_android_media_codec_preferred());
  }
#endif

  if (StaticPrefs::media_gmp_decoder_enabled() &&
      !StartupPDM(GMPDecoderModule::Create(),
                  StaticPrefs::media_gmp_decoder_preferred())) {
    mFailureFlags += DecoderDoctorDiagnostics::Flags::GMPPDMFailedToStartup;
  }
  PDM_INIT_LOG("Content PDM order:");
  int i = 0;
  for (const auto& pdm : mCurrentPDMs) {
    PDM_INIT_LOG("{}: {}", i++, pdm->Name());
  }
}

void PDMFactory::CreateDefaultPDMs() {
#ifdef XP_WIN
  if (StaticPrefs::media_wmf_enabled()) {
    if (!StartupPDM(WMFDecoderModule::Create())) {
      mFailureFlags += DecoderDoctorDiagnostics::Flags::WMFFailedToLoad;
    }
  } else if (StaticPrefs::media_decoder_doctor_wmf_disabled_is_failure()) {
    mFailureFlags += DecoderDoctorDiagnostics::Flags::WMFFailedToLoad;
  }
#endif

#ifdef MOZ_APPLEMEDIA
  StartupPDM(AppleDecoderModule::Create());
#endif
#ifdef MOZ_OMX
  if (StaticPrefs::media_omx_enabled()) {
    StartupPDM(OmxDecoderModule::Create());
  }
#endif
  StartupPDM(FFVPXRuntimeLinker::CreateDecoder());
#ifdef MOZ_FFMPEG
  if (StaticPrefs::media_ffmpeg_enabled() &&
      !StartupPDM(FFmpegRuntimeLinker::CreateDecoder())) {
    mFailureFlags += GetFailureFlagBasedOnFFmpegStatus(
        FFmpegRuntimeLinker::LinkStatusCode());
  }
#endif
#ifdef MOZ_WIDGET_ANDROID
  if (AndroidDecoderModule::IsJavaDecoderModuleAllowed()) {
    StartupPDM(AndroidDecoderModule::Create(),
               StaticPrefs::media_android_media_codec_preferred());
  }
#endif

  StartupPDM(AgnosticDecoderModule::Create(),
             StaticPrefs::media_prefer_non_ffvpx());

  if (StaticPrefs::media_gmp_decoder_enabled() &&
      !StartupPDM(GMPDecoderModule::Create(),
                  StaticPrefs::media_gmp_decoder_preferred())) {
    mFailureFlags += DecoderDoctorDiagnostics::Flags::GMPPDMFailedToStartup;
  }

  PDM_INIT_LOG("Default PDM order:");
  int i = 0;
  for (const auto& pdm : mCurrentPDMs) {
    PDM_INIT_LOG("{}: {}", i++, pdm->Name());
  }
}

void PDMFactory::CreateNullPDM() {
  mNullPDM = CreateNullDecoderModule();
  MOZ_ASSERT(mNullPDM && NS_SUCCEEDED(mNullPDM->Startup()));
}

bool PDMFactory::StartupPDM(already_AddRefed<PlatformDecoderModule> aPDM,
                            bool aInsertAtBeginning) {
  RefPtr<PlatformDecoderModule> pdm = aPDM;
  if (pdm && NS_SUCCEEDED(pdm->Startup())) {
    if (aInsertAtBeginning) {
      mCurrentPDMs.InsertElementAt(0, pdm);
    } else {
      mCurrentPDMs.AppendElement(pdm);
    }
    return true;
  }
  return false;
}

already_AddRefed<PlatformDecoderModule> PDMFactory::GetDecoderModule(
    const SupportDecoderParams& aParams,
    DecoderDoctorDiagnostics* aDiagnostics) const {
  if (aDiagnostics) {
    // If libraries failed to load, the following loop over mCurrentPDMs
    // will not even try to use them. So we record failures now.
    aDiagnostics->SetFailureFlags(mFailureFlags);
  }

  RefPtr<PlatformDecoderModule> pdm;
  for (const auto& current : mCurrentPDMs) {
    if (!current->Supports(aParams, aDiagnostics).isEmpty()) {
      pdm = current;
      break;
    }
  }
  return pdm.forget();
}

void PDMFactory::SetCDMProxy(CDMProxy* aProxy) {
  MOZ_ASSERT(aProxy);

#ifdef MOZ_WIDGET_ANDROID
  if (IsWidevineKeySystem(aProxy->KeySystem()) &&
      AndroidDecoderModule::IsJavaDecoderModuleAllowed()) {
    mEMEPDM = AndroidDecoderModule::Create(aProxy);
    return;
  }
#endif
#ifdef MOZ_WMF_CDM
  if (IsPlayReadyKeySystemAndSupported(aProxy->KeySystem()) ||
      IsWidevineExperimentKeySystemAndSupported(aProxy->KeySystem()) ||
      (IsWidevineKeySystem(aProxy->KeySystem()) &&
       aProxy->IsHardwareDecryptionSupported()) ||
      IsWMFClearKeySystemAndSupported(aProxy->KeySystem())) {
    mEMEPDM = RemoteDecoderModule::Create(
        RemoteMediaIn::UtilityProcess_MFMediaEngineCDM);
    return;
  }
#endif
  auto m = MakeRefPtr<PDMFactory>();
  mEMEPDM = MakeRefPtr<EMEDecoderModule>(aProxy, m);
}

StaticMutex PDMFactory::sSupportedMutex;

/* static */
media::MediaCodecsSupported PDMFactory::Supported(bool aForceRefresh) {
  StaticMutexAutoLock lock(sSupportedMutex);

  if (aForceRefresh) {
    PDMFactorySupport::Invalidate();
  }

  auto calculate = []() {
    MediaCodecsSupported supported;
    // H264 and AAC depends on external framework that must be dynamically
    // loaded.
    // We currently only ship a single PDM per platform able to decode AAC or
    // H264. As such we can assert that being able to create a H264 or AAC
    // decoder indicates that with WMF on Windows or FFmpeg on Unixes is
    // available.
    // This logic will have to be revisited if a PDM supporting either codec
    // will be added in addition to the WMF and FFmpeg PDM (such as OpenH264)
    for (const auto& cd : MCSInfo::GetAllCodecDefinitions()) {
      supported += MCSInfo::GetDecodeMediaCodecsSupported(
          cd.codec,
          PDMFactorySupport::IsTypeSupported(nsCString(cd.mimeTypeString)));
    }
#ifdef MOZ_WIDGET_ANDROID
    if (AndroidDecoderModule::IsJavaDecoderModuleAllowed()) {
      supported += AndroidDecoderModule::GetSupportedCodecs();
    }
#endif
    return supported;
  };

  static MediaCodecsSupported supported = calculate();
  if (aForceRefresh) {
    supported = calculate();
  }

  return supported;
}

/* static */
DecodeSupportSet PDMFactory::SupportsMimeType(
    const nsACString& aMimeType, const MediaCodecsSupported& aSupported,
    RemoteMediaIn aLocation) {
  const TrackSupportSet supports =
      RemoteMediaManagerChild::GetTrackSupport(aLocation);

  if (supports.contains(TrackSupport::DecodeVideo)) {
    if (MP4Decoder::IsH264(aMimeType)) {
      return MCSInfo::GetDecodeSupportSet(MediaCodec::H264, aSupported);
    }
    if (VPXDecoder::IsVP9(aMimeType)) {
      return MCSInfo::GetDecodeSupportSet(MediaCodec::VP9, aSupported);
    }
    if (VPXDecoder::IsVP8(aMimeType)) {
      return MCSInfo::GetDecodeSupportSet(MediaCodec::VP8, aSupported);
    }
    if (AOMDecoder::IsAV1(aMimeType)) {
      return MCSInfo::GetDecodeSupportSet(MediaCodec::AV1, aSupported);
    }
    if (MP4Decoder::IsHEVC(aMimeType)) {
      return MCSInfo::GetDecodeSupportSet(MediaCodec::HEVC, aSupported);
    }
  }

  if (supports.contains(TrackSupport::DecodeAudio)) {
    if (MP4Decoder::IsAAC(aMimeType)) {
      return MCSInfo::GetDecodeSupportSet(MediaCodec::AAC, aSupported);
    }
    if (aMimeType.EqualsLiteral("audio/mpeg")) {
      return MCSInfo::GetDecodeSupportSet(MediaCodec::MP3, aSupported);
    }
    if (aMimeType.EqualsLiteral("audio/opus")) {
      return MCSInfo::GetDecodeSupportSet(MediaCodec::Opus, aSupported);
    }
    if (aMimeType.EqualsLiteral("audio/vorbis")) {
      return MCSInfo::GetDecodeSupportSet(MediaCodec::Vorbis, aSupported);
    }
    if (aMimeType.EqualsLiteral("audio/flac")) {
      return MCSInfo::GetDecodeSupportSet(MediaCodec::FLAC, aSupported);
    }
    if (IsWaveMimetype(aMimeType)) {
      return MCSInfo::GetDecodeSupportSet(MediaCodec::Wave, aSupported);
    }
  }
  return DecodeSupportSet{};
}

/* static */
bool PDMFactory::AllDecodersAreRemote() {
  return StaticPrefs::media_rdd_process_enabled() &&
         StaticPrefs::media_rdd_opus_enabled() &&
         StaticPrefs::media_rdd_vorbis_enabled() &&
         StaticPrefs::media_rdd_vpx_enabled() &&
#if defined(MOZ_WMF)
         StaticPrefs::media_rdd_wmf_enabled() &&
#endif
         StaticPrefs::media_rdd_wav_enabled();
}

#undef PDM_INIT_LOG
}  // namespace mozilla
