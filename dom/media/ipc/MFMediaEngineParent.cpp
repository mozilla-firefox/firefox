/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MFMediaEngineParent.h"

#include <audiosessiontypes.h>
#include <dxgi.h>
#include <intsafe.h>
#include <mfapi.h>

#ifdef MOZ_WMF_CDM
#  include "MFCDMParent.h"
#  include "MFContentProtectionManager.h"
#  include "mozilla/EMEUtils.h"
#  include "mozilla/glean/DomMediaPlatformsWmfMetrics.h"
#endif

#include "MFMediaEngineExtension.h"
#include "MFMediaEngineStream.h"
#include "MFMediaEngineUtils.h"
#include "MFMediaEngineVideoStream.h"
#include "MFMediaSource.h"
#include "RemoteMediaManagerParent.h"
#include "WMF.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/RemoteDecodeUtils.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/gfx/DeviceManagerDx.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/ipc/UtilityMediaServiceParent.h"
#include "mozilla/ipc/UtilityProcessChild.h"

namespace mozilla {

#define LOG(msg, ...)                                                \
  MOZ_LOG_FMT(gMFMediaEngineLog, LogLevel::Debug,                    \
              "MFMediaEngineParent={}, Id={}, " msg, fmt::ptr(this), \
              this->Id(), ##__VA_ARGS__)

// Helper for IPC handler methods that set up the CDM: log the failure,
// notify the content process with a fatal error, and return IPC_OK() so
// the IPC channel stays alive.
#define CDM_SETUP_IPC_RETURN_IF_FAILED(rv, description)             \
  do {                                                              \
    if (MOZ_UNLIKELY(FAILED(rv))) {                                 \
      LOG(description " failed, hr={:x}", rv);                      \
      (void)SendNotifyError(                                        \
          MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,                 \
                      nsPrintfCString(description " (hr=%lx)", rv), \
                      Some(static_cast<int32_t>(rv))));             \
      return IPC_OK();                                              \
    }                                                               \
  } while (false)

using MediaEngineMap = nsTHashMap<nsUint64HashKey, MFMediaEngineParent*>;
static StaticAutoPtr<MediaEngineMap> sMediaEngines;

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::MakeAndInitialize;

StaticMutex sMediaEnginesLock;

static void RegisterMediaEngine(MFMediaEngineParent* aMediaEngine) {
  MOZ_ASSERT(aMediaEngine);
  StaticMutexAutoLock lock(sMediaEnginesLock);
  if (!sMediaEngines) {
    sMediaEngines = new MediaEngineMap();
  }
  sMediaEngines->InsertOrUpdate(aMediaEngine->Id(), aMediaEngine);
}

static void UnregisterMedieEngine(MFMediaEngineParent* aMediaEngine) {
  StaticMutexAutoLock lock(sMediaEnginesLock);
  if (sMediaEngines) {
    sMediaEngines->Remove(aMediaEngine->Id());
  }
}

/* static */
already_AddRefed<MFMediaEngineParent> MFMediaEngineParent::GetMediaEngineById(
    uint64_t aId) {
  StaticMutexAutoLock lock(sMediaEnginesLock);
  if (!sMediaEngines) {
    return nullptr;
  }
  RefPtr<MFMediaEngineParent> engine = sMediaEngines->Get(aId);
  return engine.forget();
}

/* static */
bool MFMediaEngineParent::IsMediaEngineIdAllowed(
    uint64_t aId, const RemoteMediaManagerParent* aManager) {
  MOZ_ASSERT(RemoteMediaManagerParent::OnManagerThread());
  MOZ_ASSERT(aManager);
  if (aId == 0 || aId > sMediaEngineIdx) {
    return false;
  }
  StaticMutexAutoLock lock(sMediaEnginesLock);
  MFMediaEngineParent* engine =
      sMediaEngines ? sMediaEngines->Get(aId) : nullptr;
  return !engine || engine->mManager.get() == aManager;
}

MFMediaEngineParent::MFMediaEngineParent(RemoteMediaManagerParent* aManager,
                                         nsISerialEventTarget* aManagerThread)
    : mMediaEngineId(++sMediaEngineIdx),
      mManager(aManager),
      mManagerThread(aManagerThread) {
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(aManagerThread);
  MOZ_ASSERT(mMediaEngineId != 0);
  MOZ_ASSERT(XRE_IsUtilityProcess());
  MOZ_ASSERT(GetCurrentSandboxingKind() ==
             ipc::SandboxingKind::MF_MEDIA_ENGINE_CDM);
  LOG("Created MFMediaEngineParent");
  RegisterMediaEngine(this);
  CreateMediaEngine();
}

MFMediaEngineParent::~MFMediaEngineParent() {
  LOG("Destoryed MFMediaEngineParent");
  UnregisterMedieEngine(this);
  DestroyEngineIfExists();
}

void MFMediaEngineParent::ActorDestroy(ActorDestroyReason aWhy) {
  AssertOnManagerThread();
  LOG("ActorDestroy");
  UnregisterMedieEngine(this);
  DestroyEngineIfExists();
}

void MFMediaEngineParent::DestroyEngineIfExists(
    const Maybe<MediaResult>& aError) {
  LOG("DestroyEngineIfExists, hasError={}", aError.isSome());
  ENGINE_MARKER("MFMediaEngineParent::DestroyEngineIfExists");
  mMediaEngineEventListener.DisconnectIfExists();
  mRequestSampleListener.DisconnectIfExists();
  mMediaEngineNotify = nullptr;
  mMediaEngineExtension = nullptr;
  if (mMediaSource) {
    mMediaSource->ShutdownTaskQueue();
    mMediaSource = nullptr;
  }
#ifdef MOZ_WMF_CDM
  if (mContentProtectionManager) {
    mContentProtectionManager->Shutdown();
    mContentProtectionManager = nullptr;
  }
  mProxyId.reset();
  mHDCPRequestHolder.DisconnectIfExists();
#endif
  if (mMediaEngine) {
    LOG_IF_FAILED(mMediaEngine->Shutdown());
    mMediaEngine = nullptr;
  }
  if (mDXGIDeviceManager) {
    mDXGIDeviceManager = nullptr;
    wmf::MFUnlockDXGIDeviceManager();
  }
  if (aError) {
    (void)SendNotifyError(*aError);
  }
}

void MFMediaEngineParent::CreateMediaEngine() {
  LOG("CreateMediaEngine");
  auto errorExit = MakeScopeExit([&] {
    MediaResult error(NS_ERROR_DOM_MEDIA_FATAL_ERR, "Failed to create engine");
    DestroyEngineIfExists(Some(error));
  });

  if (!wmf::MediaFoundationInitializer::HasInitialized()) {
    NS_WARNING("Failed to initialize media foundation");
    return;
  }

  InitializeDXGIDeviceManager();

  // Create an attribute and set mandatory information that are required for
  // a media engine creation.
  ComPtr<IMFAttributes> creationAttributes;
  RETURN_VOID_IF_FAILED(wmf::MFCreateAttributes(&creationAttributes, 6));
  RETURN_VOID_IF_FAILED(
      MakeAndInitialize<MFMediaEngineNotify>(&mMediaEngineNotify));
  mMediaEngineEventListener = mMediaEngineNotify->MediaEngineEvent().Connect(
      mManagerThread, this, &MFMediaEngineParent::HandleMediaEngineEvent);
  RETURN_VOID_IF_FAILED(creationAttributes->SetUnknown(
      MF_MEDIA_ENGINE_CALLBACK, mMediaEngineNotify.Get()));
  RETURN_VOID_IF_FAILED(creationAttributes->SetUINT32(
      MF_MEDIA_ENGINE_AUDIO_CATEGORY, AudioCategory_Media));
  RETURN_VOID_IF_FAILED(
      MakeAndInitialize<MFMediaEngineExtension>(&mMediaEngineExtension));
  RETURN_VOID_IF_FAILED(creationAttributes->SetUnknown(
      MF_MEDIA_ENGINE_EXTENSION, mMediaEngineExtension.Get()));
  RETURN_VOID_IF_FAILED(
      creationAttributes->SetUINT32(MF_MEDIA_ENGINE_CONTENT_PROTECTION_FLAGS,
                                    MF_MEDIA_ENGINE_ENABLE_PROTECTED_CONTENT));
  if (mDXGIDeviceManager) {
    RETURN_VOID_IF_FAILED(creationAttributes->SetUnknown(
        MF_MEDIA_ENGINE_DXGI_MANAGER, mDXGIDeviceManager.Get()));
  }

  ComPtr<IMFMediaEngineClassFactory> factory;
  RETURN_VOID_IF_FAILED(CoCreateInstance(CLSID_MFMediaEngineClassFactory,
                                         nullptr, CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&factory)));
  const bool isLowLatency = StaticPrefs::media_wmf_low_latency_enabled();
  static const DWORD MF_MEDIA_ENGINE_DEFAULT = 0;
  RETURN_VOID_IF_FAILED(factory->CreateInstance(
      isLowLatency ? MF_MEDIA_ENGINE_REAL_TIME_MODE : MF_MEDIA_ENGINE_DEFAULT,
      creationAttributes.Get(), &mMediaEngine));

  LOG("Created media engine successfully");
  mIsCreatedMediaEngine = true;
  ENGINE_MARKER("MFMediaEngineParent::CreatedMediaEngine");
  errorExit.release();
}

void MFMediaEngineParent::InitializeDXGIDeviceManager() {
  auto* deviceManager = gfx::DeviceManagerDx::Get();
  if (!deviceManager) {
    return;
  }
  RefPtr<ID3D11Device> d3d11Device = deviceManager->CreateMediaEngineDevice();
  if (!d3d11Device) {
    return;
  }

  auto errorExit = MakeScopeExit([&] {
    mDXGIDeviceManager = nullptr;
    wmf::MFUnlockDXGIDeviceManager();
  });
  UINT deviceResetToken;
  RETURN_VOID_IF_FAILED(
      wmf::MFLockDXGIDeviceManager(&deviceResetToken, &mDXGIDeviceManager));
  if (!mDXGIDeviceManager) {
    return;
  }
  RETURN_VOID_IF_FAILED(
      mDXGIDeviceManager->ResetDevice(d3d11Device.get(), deviceResetToken));
  LOG("Initialized DXGI manager");
  errorExit.release();
}

#ifndef ENSURE_EVENT_DISPATCH_DURING_PLAYING
#  define ENSURE_EVENT_DISPATCH_DURING_PLAYING(event)        \
    do {                                                     \
      if (mMediaEngine->IsPaused()) {                        \
        LOG("Ignore incorrect '{}' during pausing!", event); \
        return;                                              \
      }                                                      \
    } while (false)
#endif

static MF_MEDIA_ENGINE_ERR ToError(DWORD aError) {
  MOZ_ASSERT(aError == MF_MEDIA_ENGINE_ERR_NOERROR ||
             aError == MF_MEDIA_ENGINE_ERR_ABORTED ||
             aError == MF_MEDIA_ENGINE_ERR_NETWORK ||
             aError == MF_MEDIA_ENGINE_ERR_DECODE ||
             aError == MF_MEDIA_ENGINE_ERR_SRC_NOT_SUPPORTED ||
             aError == MF_MEDIA_ENGINE_ERR_ENCRYPTED);
  return static_cast<MF_MEDIA_ENGINE_ERR>(aError);
}

void MFMediaEngineParent::HandleMediaEngineEvent(
    MFMediaEngineEventWrapper aEvent) {
  AssertOnManagerThread();
  LOG("Received media engine event {}", MediaEngineEventToStr(aEvent.mEvent));
  ENGINE_MARKER_TEXT(
      "MFMediaEngineParent::HandleMediaEngineEvent",
      nsPrintfCString("%s", MediaEngineEventToStr(aEvent.mEvent)));
  switch (aEvent.mEvent) {
    case MF_MEDIA_ENGINE_EVENT_ERROR: {
      MOZ_ASSERT(aEvent.mParam1 && aEvent.mParam2);
      auto error = ToError(*aEvent.mParam1);
      auto result = static_cast<HRESULT>(*aEvent.mParam2);
      NotifyError(error, result);
      break;
    }
    case MF_MEDIA_ENGINE_EVENT_FORMATCHANGE: {
      if (mMediaEngine->HasVideo()) {
        NotifyVideoResizing();
      }
      break;
    }
    case MF_MEDIA_ENGINE_EVENT_FIRSTFRAMEREADY: {
#ifdef MOZ_WMF_CDM
      // A produced frame means protected activation succeeded; refresh the
      // readiness conditions and restore the recovery budget so a later
      // transient failure on this playback is not constrained by earlier ones.
      if (mProxyId) {
        if (RefPtr<MFCDMParent> cdmParent =
                MFCDMParent::GetCDMById(*mProxyId)) {
          if (cdmParent->IsHardwareDRM()) {
            // Record before restoring the budget so the success event reports
            // how many recoveries it took to reach the first frame.
            RecordProtectedReadiness(cdmParent,
                                     ProtectedActivationPhase::Succeeded, S_OK,
                                     Nothing());
          }
          // A produced frame proves the protected pipeline is working, so
          // refresh the conditions (e.g. an HDCP pre-warm that could not
          // confirm support and marked it failed) to ready. Skip this for
          // clearlead: its first frame is clear and does not exercise HDCP or
          // the decryptor, so it does not prove them ready.
          if (cdmParent->IsHardwareDRM() && !cdmParent->IsClearLead()) {
            cdmParent->ReadinessMonitor().MarkAllReady();
          }
          cdmParent->ReadinessMonitor().ResetRecoveryBudget();
        }
      }
#endif
      if (mMediaEngine->HasVideo() && !mIsFrameServerMode) {
        EnsureDcompSurfaceHandle();
      }
      [[fallthrough]];
    }
    case MF_MEDIA_ENGINE_EVENT_LOADEDDATA:
    case MF_MEDIA_ENGINE_EVENT_WAITING:
    case MF_MEDIA_ENGINE_EVENT_SEEKED:
    case MF_MEDIA_ENGINE_EVENT_BUFFERINGSTARTED:
    case MF_MEDIA_ENGINE_EVENT_BUFFERINGENDED:
      (void)SendNotifyEvent(aEvent.mEvent);
      break;
    case MF_MEDIA_ENGINE_EVENT_PLAYING:
      ENSURE_EVENT_DISPATCH_DURING_PLAYING(
          MediaEngineEventToStr(aEvent.mEvent));
      (void)SendNotifyEvent(aEvent.mEvent);
      break;
    case MF_MEDIA_ENGINE_EVENT_ENDED: {
      ENSURE_EVENT_DISPATCH_DURING_PLAYING(
          MediaEngineEventToStr(aEvent.mEvent));
      (void)SendNotifyEvent(aEvent.mEvent);
      UpdateStatisticsData();
      break;
    }
    case MF_MEDIA_ENGINE_EVENT_TIMEUPDATE: {
      auto currentTimeInSeconds = mMediaEngine->GetCurrentTime();
      (void)SendUpdateCurrentTime(currentTimeInSeconds);
      UpdateStatisticsData();
      if (mIsFrameServerMode && mMediaEngine->HasVideo()) {
        LONGLONG pts = 0;
        HRESULT hr = mMediaEngine->OnVideoStreamTick(&pts);
        LOG("FrameServer pump: OnVideoStreamTick hr={:x} pts={}", (long)hr,
            (int64_t)pts);
      }
      break;
    }
    default:
      LOG("Unhandled event={}", MediaEngineEventToStr(aEvent.mEvent));
      break;
  }
}

#ifdef MOZ_WMF_CDM
void MFMediaEngineParent::RecordProtectedReadiness(
    MFCDMParent* aCdmParent, ProtectedActivationPhase aPhase,
    HRESULT aPlatformError,
    const Maybe<MFProtectedPathReadinessMonitor::Reaction>& aReaction) {
  AssertOnManagerThread();
  auto& monitor = aCdmParent->ReadinessMonitor();
  const uint32_t recoveryCount = monitor.RecoveriesUsed();
  nsCString readiness = monitor.DescribeReadiness();
  // A reaction only exists for a failed activation handled by the gate; it is
  // Nothing on the other phases (and when the gate is disabled).
  const char* reactionStr = "none";
  if (aReaction) {
    reactionStr =
        MFProtectedPathReadinessMonitor::EnumValueToString(*aReaction);
  }
  glean::mfcdm::ProtectedReadinessExtra extra;
  extra.readiness = Some(readiness);
  extra.platformError =
      Some(static_cast<int64_t>(static_cast<uint32_t>(aPlatformError)));
  extra.phase = Some(nsCString(EnumValueToString(aPhase)));
  if (aReaction) {
    extra.reaction = Some(nsCString(reactionStr));
  }
  extra.gateWaitMs =
      Some(static_cast<int64_t>(mProtectedGateWait.ToMilliseconds()));
  extra.recoveryCount = Some(static_cast<int64_t>(recoveryCount));
  extra.keySystem = Some(NS_ConvertUTF16toUTF8(aCdmParent->GetKeySystem()));
  const nsCString adapterVendorID = gfx::gfxVars::AdapterVendorID();
  if (!adapterVendorID.IsEmpty()) {
    extra.adapterVendorId = Some(adapterVendorID);
    extra.adapterDeviceId = Some(gfx::gfxVars::AdapterDeviceID());
    extra.adapterDriverVersion = Some(gfx::gfxVars::AdapterDriverVersion());
  }
  LOG("protected_readiness probe: phase={}, reaction={}, error={:#x}, "
      "recoveryCount={}, gateWaitMs={}, readiness=[{}]",
      EnumValueToString(aPhase), reactionStr,
      static_cast<uint32_t>(aPlatformError), recoveryCount,
      static_cast<int64_t>(mProtectedGateWait.ToMilliseconds()),
      readiness.get());
  glean::mfcdm::protected_readiness.Record(Some(std::move(extra)));
}

void MFMediaEngineParent::RecoverProtectedPlayback(HRESULT aResult) {
  AssertOnManagerThread();
  LOG("Recovering protected playback, hr={:x}", aResult);
  // SendNotifyHardwareReset drives the content side to tear this engine down
  // and build a fresh one (new IMFMediaEngine + SetSource), which re-drives
  // Media Foundation instead of leaving it wedged in the error state.
  sPendingHDCPCheck = nullptr;
  if (RefPtr<MFCDMParent> cdmParent =
          mProxyId ? MFCDMParent::GetCDMById(*mProxyId) : nullptr) {
    cdmParent->OnHardwareContextReset();
    sPendingHDCPCheck = cdmParent->WaitForHDCPSettleAfterReset();
  }
  (void)SendNotifyHardwareReset(static_cast<uint32_t>(aResult));
}
#endif

void MFMediaEngineParent::NotifyError(MF_MEDIA_ENGINE_ERR aError,
                                      HRESULT aResult) {
  AssertOnManagerThread();
  if (aError == MF_MEDIA_ENGINE_ERR_NOERROR) {
    return;
  }
#ifdef MOZ_WMF_CDM
  // Hardware context reset errors require engine recovery, not a real error.
  if (IsHardwareResetHRESULT(aResult)) {
    LOG("Notifying hardware reset error, hr={:x}", aResult);
    ENGINE_MARKER("MFMediaEngineParent,HardwareContextReset");
    RecoverProtectedPlayback(aResult);
    return;
  }

  // Bounded last-resort reaction for the two protected-activation errors Media
  // Foundation gives no advance signal for. When the gate is enabled, re-drive
  // the protected pipeline within budget; once the budget is spent the error
  // falls through to the terminal handling below. SL3000 is never demoted.
  if (mProxyId) {
    if (RefPtr<MFCDMParent> cdmParent = MFCDMParent::GetCDMById(*mProxyId);
        cdmParent && cdmParent->IsHardwareDRM()) {
      // The bounded-recovery decision only runs when the gate is enabled;
      // otherwise the reaction stays Nothing and the error falls through to the
      // terminal handling below.
      Maybe<MFProtectedPathReadinessMonitor::Reaction> reaction;
      if (StaticPrefs::
              media_wmf_media_engine_protected_readiness_gate_enabled_AtStartup()) {
        reaction = Some(cdmParent->ReadinessMonitor().OnActivationError(
            aResult,
            StaticPrefs::
                media_wmf_media_engine_protected_readiness_gate_max_recoveries()));
      }

      // Record the readiness snapshot at the failure.
      RecordProtectedReadiness(cdmParent, ProtectedActivationPhase::Failed,
                               aResult, reaction);

      if (reaction ==
          Some(MFProtectedPathReadinessMonitor::Reaction::Recover)) {
        LOG("Re-driving protected playback after activation error, hr={:x}",
            aResult);
        ENGINE_MARKER("MFMediaEngineParent,RetryProtectedActivation");
        RecoverProtectedPlayback(aResult);
        return;
      }
    }
  }
#endif
  LOG("Notify error '{}', hr={:x}", MFMediaEngineErrorToStr(aError), aResult);
  ENGINE_MARKER_TEXT(
      "MFMediaEngineParent::NotifyError",
      nsPrintfCString("%s, hr=%lx", MFMediaEngineErrorToStr(aError), aResult));
  switch (aError) {
    case MF_MEDIA_ENGINE_ERR_ABORTED:
    case MF_MEDIA_ENGINE_ERR_NETWORK:
      // We ignore these because we fetch data by ourselves.
      return;
    case MF_MEDIA_ENGINE_ERR_DECODE: {
      MediaResult error(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                        nsPrintfCString("Decoder error (hr=%lx)", aResult),
                        Some(static_cast<int32_t>(aResult)));
#ifdef MOZ_WMF_CDM
      if (aResult == MSPR_E_NO_DECRYPTOR_AVAILABLE ||
          aResult == MF_E_HARDWARE_DRM_UNSUPPORTED) {
        NotifyDisableHWDRM();
      }
#endif
      (void)SendNotifyError(error);
      return;
    }
    case MF_MEDIA_ENGINE_ERR_SRC_NOT_SUPPORTED: {
#ifdef MOZ_WMF_CDM
      if (mProxyId) {
        // When a CDM is active the media format was validated before the CDM
        // was initialised, so a genuine codec/container issue is unlikely.
        // Most errors tunnelled through SRC_NOT_SUPPORTED in this context are
        // CDM pipeline or content-protection failures. Use an allowlist of the
        // few HRESULTs that genuinely mean unsupported format; classify
        // everything else as a decode error so it surfaces correctly.
        bool isSrcError =
            (aResult == S_OK || aResult == MF_E_INVALIDMEDIATYPE ||
             aResult == MF_E_UNSUPPORTED_BYTESTREAM_TYPE ||
             aResult == MF_E_UNSUPPORTED_FORMAT ||
             aResult == MF_E_TOPO_CODEC_NOT_FOUND);
        MediaResult error(
            isSrcError ? NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR
                       : NS_ERROR_DOM_MEDIA_DECODE_ERR,
            nsPrintfCString(
                "%s (hr=%lx)",
                isSrcError ? "Source not supported" : "Decode error", aResult),
            Some(static_cast<int32_t>(aResult)));
        (void)SendNotifyError(error);
        return;
      }
#endif
      MediaResult error(
          NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR,
          nsPrintfCString("Source not supported (hr=%lx)", aResult),
          Some(static_cast<int32_t>(aResult)));
      (void)SendNotifyError(error);
      return;
    }
    case MF_MEDIA_ENGINE_ERR_ENCRYPTED: {
      MediaResult error(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                        nsPrintfCString("Encrypted error (hr=%lx)", aResult),
                        Some(static_cast<int32_t>(aResult)));
      (void)SendNotifyError(error);
      return;
    }
    default:
      MOZ_ASSERT_UNREACHABLE("Unsupported error");
      return;
  }
}

/* static */
bool MFMediaEngineParent::IsHardwareResetHRESULT(HRESULT aResult) {
#ifdef MOZ_WMF_CDM
  if (aResult == DRM_E_TEE_INVALID_HWDRM_STATE ||      // TEE state invalid
      aResult == DRM_OEM_E_ASD_ACTIVE_DISPLAY_FAIL) {  // display link failure
    return true;
  }
#endif
  // https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-error
  return aResult == DXGI_ERROR_DEVICE_REMOVED ||  // GPU removed / driver crash
         aResult == DXGI_ERROR_DEVICE_HUNG ||     // GPU hung (TDR timeout)
         aResult == DXGI_ERROR_DEVICE_RESET;      // GPU reset by OS
}

MFMediaEngineStreamWrapper* MFMediaEngineParent::GetMediaEngineStream(
    TrackType aType, const CreateDecoderParams& aParam) {
  // Has been shutdowned.
  if (!mMediaSource) {
    return nullptr;
  }
  LOG("Create a media engine decoder for {}", TrackTypeToStr(aType));
  if (aType == TrackType::kAudioTrack) {
    auto* stream = mMediaSource->GetAudioStream();
    return new MFMediaEngineStreamWrapper(stream, stream->GetTaskQueue(),
                                          aParam);
  }
  MOZ_ASSERT(aType == TrackType::kVideoTrack);
  auto* stream = mMediaSource->GetVideoStream();
  stream->AsVideoStream()->SetKnowsCompositor(aParam.mKnowsCompositor);
  stream->AsVideoStream()->SetConfig(aParam.mConfig);
  return new MFMediaEngineStreamWrapper(stream, stream->GetTaskQueue(), aParam);
}

mozilla::ipc::IPCResult MFMediaEngineParent::RecvInitMediaEngine(
    const MediaEngineInfoIPDL& aInfo, InitMediaEngineResolver&& aResolver) {
  AssertOnManagerThread();
  if (!mIsCreatedMediaEngine) {
    aResolver(0);
    return IPC_OK();
  }
  if (MOZ_UNLIKELY(mIsMediaEngineInitialized)) {
    MOZ_ASSERT_UNREACHABLE("MFMediaEngine must not be initialized twice");
    return IPC_FAIL(this, "MFMediaEngine must not be initialized twice");
  }
  // Metadata preload is controlled by content process side before creating
  // media engine.
  if (aInfo.preload()) {
    // TODO : really need this?
    (void)mMediaEngine->SetPreload(MF_MEDIA_ENGINE_PRELOAD_AUTOMATIC);
  }
  RETURN_PARAM_IF_FAILED(
      SetMediaInfo(aInfo.mediaInfo(), aInfo.encryptedCustomIdent()), IPC_OK());
  mIsMediaEngineInitialized = true;
  aResolver(mMediaEngineId);
  return IPC_OK();
}

HRESULT MFMediaEngineParent::SetMediaInfo(const MediaInfoIPDL& aInfo,
                                          bool aIsEncryptedCustomInit) {
  AssertOnManagerThread();
  MOZ_ASSERT(mIsCreatedMediaEngine, "Hasn't created media engine?");
  MOZ_ASSERT(!mMediaSource);

  LOG("SetMediaInfo");

  auto errorExit = MakeScopeExit([&] {
    MediaResult error(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                      "Failed to create media source");
    DestroyEngineIfExists(Some(error));
  });

  // Create media source and set it to the media engine.
  NS_ENSURE_TRUE(SUCCEEDED(MakeAndInitialize<MFMediaSource>(
                     &mMediaSource, aInfo.audioInfo(), aInfo.videoInfo(),
                     mManagerThread, aIsEncryptedCustomInit)),
                 IPC_OK());

  const bool isEncrypted = mMediaSource->IsEncrypted();
  ENGINE_MARKER("MFMediaEngineParent,CreatedMediaSource");
  const gfx::IntSize videoImage =
      aInfo.videoInfo() ? aInfo.videoInfo()->mImage : gfx::IntSize{};
  const gfx::IntSize videoDisplay =
      aInfo.videoInfo() ? aInfo.videoInfo()->mDisplay : gfx::IntSize{};
  nsPrintfCString message(
      "Created the media source, audio=%s, video=%s, encrypted-audio=%s, "
      "encrypted-video=%s, aIsEncryptedCustomInit=%d, isEncrypted=%d, "
      "video-image=[%dx%d], video-display=[%dx%d]",
      aInfo.audioInfo() ? aInfo.audioInfo()->mMimeType.get() : "none",
      aInfo.videoInfo() ? aInfo.videoInfo()->mMimeType.get() : "none",
      aInfo.audioInfo() && aInfo.audioInfo()->mCrypto.IsEncrypted() ? "yes"
                                                                    : "no",
      aInfo.videoInfo() && aInfo.videoInfo()->mCrypto.IsEncrypted() ? "yes"
                                                                    : "no",
      aIsEncryptedCustomInit, isEncrypted, videoImage.width, videoImage.height,
      videoDisplay.width, videoDisplay.height);
  LOG("{}", message.get());

  if (aInfo.videoInfo()) {
    ComPtr<IMFMediaEngineEx> mediaEngineEx;
    RETURN_IF_FAILED(mMediaEngine.As(&mediaEngineEx));
    if (isEncrypted) {
      // Microsoft recommends to disable low latency with DRM.
      RETURN_IF_FAILED(mediaEngineEx->SetRealTimeMode(false));
      LOG("Turned off the real time mode for encrypted playback");
    }
  }

  mRequestSampleListener = mMediaSource->RequestSampleEvent().Connect(
      mManagerThread, this, &MFMediaEngineParent::HandleRequestSample);
  errorExit.release();

#ifdef MOZ_WMF_CDM
  if (isEncrypted && !mContentProtectionManager) {
    // We will set the source later when the CDM proxy is ready.
    return S_OK;
  }

  if (isEncrypted && mContentProtectionManager) {
    RefPtr<MFCDMProxy> proxy = mContentProtectionManager->GetCDMProxy();
    MOZ_ASSERT(proxy);
    mMediaSource->SetCDMProxy(proxy);
  }
#endif

  SetMediaSourceOnEngine();
  return S_OK;
}

void MFMediaEngineParent::SetMediaSourceOnEngine() {
  AssertOnManagerThread();
  MOZ_ASSERT(mMediaSource);

#ifdef MOZ_WMF_CDM
  // Hold the protected topology build until the readiness conditions have
  // settled, so activation happens when the protected pipeline is ready rather
  // than being attempted blind and reacted to on failure. Only hardware-DRM
  // playback is gated; software and clearkey are not. The gate releases once
  // every gating condition has settled (each is resolved to ready, or failed,
  // which also counts as settled): the adapter and content protection manager
  // settle when the engine attaches to the CDM, and HDCP settles when its
  // pre-warm query completes or fails. There is no timer.
  const bool gateEnabled =
      mProxyId && !mIsFrameServerMode &&
      StaticPrefs::
          media_wmf_media_engine_protected_readiness_gate_enabled_AtStartup();
  RefPtr<MFCDMParent> cdmParent =
      gateEnabled ? MFCDMParent::GetCDMById(*mProxyId) : nullptr;
  const bool holdForReadiness =
      cdmParent && cdmParent->IsHardwareDRM() &&
      !cdmParent->ReadinessMonitor().AllActivationConditionsSettled();
  if (holdForReadiness) {
    LOG("Holding protected topology build until readiness conditions settle");
    ENGINE_MARKER("MFMediaEngineParent, HoldForReadiness");
    // Record that the gate engaged. A "gate_held" event with no later
    // succeeded/failed event for this session indicates the gate never
    // released, which this otherwise-silent hold would not surface.
    RecordProtectedReadiness(cdmParent, ProtectedActivationPhase::GateHeld,
                             S_OK, Nothing());
    mProtectedGateHoldStart = TimeStamp::Now();
    cdmParent->ReadinessMonitor().RunWhenActivationSettled(
        [self = RefPtr{this}, this] {
          if (mMediaEngine) {
            mProtectedGateWait = TimeStamp::Now() - mProtectedGateHoldStart;
            LOG("Readiness settled; setting protected media source on engine");
            SetMediaSourceOnEngine();
          }
        },
        mManagerThread);
    return;
  }
#endif

  auto errorExit = MakeScopeExit([&] {
    MediaResult error(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                      "Failed to set media source");
    DestroyEngineIfExists(Some(error));
  });

  // Enable windowless swapchain mode before setting the source. For encrypted
  // content, this must be called after SetContentProtectionManager (which
  // resets the swapchain mode), and before SetSource.
  if (mMediaSource->GetVideoStream()) {
    if (mIsFrameServerMode) {
      LOG("Frame server mode: skipping DComp");
      ENGINE_MARKER("MFMediaEngineParent,FrameServerMode");
      mMediaSource->GetVideoStream()->AsVideoStream()->SetFrameServerMode();
      (void)SendNotifyFrameServerMode();
    } else {
      ComPtr<IMFMediaEngineEx> mediaEngineEx;
      RETURN_VOID_IF_FAILED(mMediaEngine.As(&mediaEngineEx));
      HRESULT swapChainHr = mediaEngineEx->EnableWindowlessSwapchainMode(true);
      if (SUCCEEDED(swapChainHr)) {
        mDCompModeEnabled = true;
        LOG("Enabled dcomp swap chain mode");
        ENGINE_MARKER("MFMediaEngineParent,EnabledSwapChain");
      } else {
        LOG("EnableWindowlessSwapchainMode failed: hr={:x}", (long)swapChainHr);
        // This branch is reached in non-WMFClearKey playback (e.g. unencrypted
        // or Widevine/PlayReady EME) when the underlying graphics driver does
        // not support windowless swap chains (e.g. running in a remote desktop
        // or software-rendering environment). mDCompModeEnabled stays false, so
        // EnsureDcompSurfaceHandle() will early-return on FIRSTFRAMEREADY, and
        // the engine falls back to its own internal rendering while still
        // firing LOADEDDATA/ENDED/etc. normally. Do NOT enter frame-server mode
        // here: that path is only for WMFClearKey, which is handled by the
        // mIsFrameServerMode fast path above (set in RecvSetCDMProxyId before
        // SetMediaSourceOnEngine runs).
      }
    }
  }

  mMediaEngineExtension->SetMediaSource(mMediaSource.Get());

  // We use the source scheme in order to let the media engine to load our
  // custom source.
  RETURN_VOID_IF_FAILED(
      mMediaEngine->SetSource(SysAllocString(L"MFRendererSrc")));

  LOG("Finished setup our custom media source to the media engine");
  ENGINE_MARKER("MFMediaEngineParent,FinishedSetupMediaSource");
  errorExit.release();
}

mozilla::ipc::IPCResult MFMediaEngineParent::RecvPlay() {
  AssertOnManagerThread();
  if (!mMediaEngine) {
    LOG("Engine has been shutdowned!");
    return IPC_OK();
  }
  LOG("Play, expected playback rate {}, default playback rate={}",
      mPlaybackRate, mMediaEngine->GetDefaultPlaybackRate());
  ENGINE_MARKER("MFMediaEngineParent,Play");
  NS_ENSURE_TRUE(SUCCEEDED(mMediaEngine->Play()), IPC_OK());
  return IPC_OK();
}

mozilla::ipc::IPCResult MFMediaEngineParent::RecvPause() {
  AssertOnManagerThread();
  if (!mMediaEngine) {
    LOG("Engine has been shutdowned!");
    return IPC_OK();
  }
  LOG("Pause");
  ENGINE_MARKER("MFMediaEngineParent,Pause");
  NS_ENSURE_TRUE(SUCCEEDED(mMediaEngine->Pause()), IPC_OK());
  return IPC_OK();
}

mozilla::ipc::IPCResult MFMediaEngineParent::RecvSeek(
    double aTargetTimeInSecond) {
  AssertOnManagerThread();
  if (!mMediaEngine) {
    return IPC_OK();
  }

  // If the target time is already equal to the current time, the media engine
  // doesn't perform seek internally so we won't be able to receive `seeked`
  // event. Therefore, we simply return `seeked` here because we're already in
  // the target time.
  const auto currentTimeInSeconds = mMediaEngine->GetCurrentTime();
  if (currentTimeInSeconds == aTargetTimeInSecond) {
    (void)SendNotifyEvent(MF_MEDIA_ENGINE_EVENT_SEEKED);
    return IPC_OK();
  }

  LOG("Seek to {}", aTargetTimeInSecond);
  ENGINE_MARKER_TEXT("MFMediaEngineParent,Seek",
                     nsPrintfCString("%f", aTargetTimeInSecond));
  NS_ENSURE_TRUE(SUCCEEDED(mMediaEngine->SetCurrentTime(aTargetTimeInSecond)),
                 IPC_OK());

  return IPC_OK();
}

mozilla::ipc::IPCResult MFMediaEngineParent::RecvSetCDMProxyId(
    uint64_t aProxyId) {
  if (!mMediaEngine) {
    return IPC_OK();
  }
#ifdef MOZ_WMF_CDM
  LOG("SetCDMProxy, Id={}", aProxyId);
  mProxyId = Some(aProxyId);
  RefPtr<MFCDMParent> cdmParent = MFCDMParent::GetCDMById(aProxyId);
  if (!cdmParent) {
    LOG("No CDM found for Id={}", aProxyId);
    (void)SendNotifyError(MediaResult(NS_ERROR_DOM_MEDIA_CDM_NOT_FOUND_ERR,
                                      "No CDM for proxy id"));
    return IPC_OK();
  }
  if (IsWMFClearKeySystemAndSupported(cdmParent->GetKeySystem())) {
    LOG("WMFClearKey CDM detected, enabling frame server mode");
    mIsFrameServerMode = true;
  }
  if (mContentProtectionManager) {
    mContentProtectionManager->Shutdown();
  }
  HRESULT rv = MakeAndInitialize<MFContentProtectionManager>(
      &mContentProtectionManager, mManagerThread);
  CDM_SETUP_IPC_RETURN_IF_FAILED(rv,
                                 "Failed to create content protection manager");

  ComPtr<IMFMediaEngineProtectedContent> protectedMediaEngine;
  rv = mMediaEngine.As(&protectedMediaEngine);
  CDM_SETUP_IPC_RETURN_IF_FAILED(rv, "Failed to get protected media engine");

  rv = protectedMediaEngine->SetContentProtectionManager(
      mContentProtectionManager.Get());
  CDM_SETUP_IPC_RETURN_IF_FAILED(rv,
                                 "Failed to set content protection manager");

  RefPtr<MFCDMProxy> proxy = cdmParent->GetMFCDMProxy();
  if (!proxy) {
    LOG("Failed to get MFCDMProxy!");
    (void)SendNotifyError(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, "Failed to get CDM proxy"));
    return IPC_OK();
  }

  rv = mContentProtectionManager->SetCDMProxy(proxy);
  CDM_SETUP_IPC_RETURN_IF_FAILED(rv, "Failed to set CDM proxy");

  // Record the engine-side readiness here, at engine<->CDM attach, rather than
  // when the DXGI device manager is first created: this is the earliest point
  // where the CDM (and so its readiness monitor) is known. The adapter was
  // bound when the engine was created; on an adapter change the engine is
  // recreated and re-attaches, re-marking these conditions after the monitor's
  // engine-side conditions are reset.
  auto& readiness = cdmParent->ReadinessMonitor();
  readiness.MarkReady(
      MFProtectedPathReadinessMonitor::Condition::ContentProtectionManager);
  if (mDXGIDeviceManager) {
    readiness.MarkReady(
        MFProtectedPathReadinessMonitor::Condition::HwDrmAdapter);
  } else {
    readiness.MarkFailed(
        MFProtectedPathReadinessMonitor::Condition::HwDrmAdapter, E_FAIL);
  }

  mContentProtectionManager->SetNotifyWaitingForKeyCallback(
      [self = RefPtr{this}]() {
        if (self->CanSend() && self->mMediaEngine) {
          (void)self->SendNotifyWaitingForKey();
        }
      });

  // TODO : is it possible to set CDM proxy before creating media source? If so,
  // handle that as well.
  if (mMediaSource) {
    mMediaSource->SetCDMProxy(proxy);
    if (sPendingHDCPCheck) {
      LOG("Deferring SetMediaSourceOnEngine until HDCP settle check completes");
      RefPtr<GenericPromise> hdcpCheck = std::move(sPendingHDCPCheck);
      hdcpCheck
          ->Then(mManagerThread, __func__,
                 [self = RefPtr{this}](GenericPromise::ResolveOrRejectValue&&) {
                   self->mHDCPRequestHolder.Complete();
                   // Proceed regardless of whether the HDCP check resolved or
                   // rejected: the check is a timing signal, not a hard gate.
                   // The engine will enforce any HDCP policy on its own.
                   if (self->mMediaEngine) {
                     self->SetMediaSourceOnEngine();
                   }
                 })
          ->Track(mHDCPRequestHolder);
    } else {
      SetMediaSourceOnEngine();
    }
  }
  LOG("Set CDM Proxy successfully on the media engine!");
#endif
  return IPC_OK();
}

mozilla::ipc::IPCResult MFMediaEngineParent::RecvSetVolume(double aVolume) {
  AssertOnManagerThread();
  if (mMediaEngine) {
    LOG("SetVolume={}", aVolume);
    ENGINE_MARKER_TEXT("MFMediaEngineParent,SetVolume",
                       nsPrintfCString("%f", aVolume));
    NS_ENSURE_TRUE(SUCCEEDED(mMediaEngine->SetVolume(aVolume)), IPC_OK());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult MFMediaEngineParent::RecvSetPlaybackRate(
    double aPlaybackRate) {
  AssertOnManagerThread();
  if (aPlaybackRate <= 0) {
    LOG("Not support zero or negative playback rate");
    return IPC_OK();
  }
  LOG("SetPlaybackRate={}", aPlaybackRate);
  ENGINE_MARKER_TEXT("MFMediaEngineParent,SetPlaybackRate",
                     nsPrintfCString("%f", aPlaybackRate));
  mPlaybackRate = aPlaybackRate;
  NS_ENSURE_TRUE(SUCCEEDED(mMediaEngine->SetPlaybackRate(mPlaybackRate)),
                 IPC_OK());
  // The media Engine uses the default playback rate to determine the playback
  // rate when calling `play()`. So if we don't change default playback rate
  // together, the playback rate would fallback to 1 after pausing or
  // seeking, which would be different from our expected playback rate.
  NS_ENSURE_TRUE(SUCCEEDED(mMediaEngine->SetDefaultPlaybackRate(mPlaybackRate)),
                 IPC_OK());
  return IPC_OK();
}

mozilla::ipc::IPCResult MFMediaEngineParent::RecvSetLooping(bool aLooping) {
  AssertOnManagerThread();
  // We handle looping by seeking back to the head by ourselves, so we don't
  // rely on the media engine for looping.
  return IPC_OK();
}

mozilla::ipc::IPCResult MFMediaEngineParent::RecvNotifyEndOfStream(
    TrackInfo::TrackType aType) {
  AssertOnManagerThread();
  MOZ_ASSERT(mMediaSource);
  LOG("NotifyEndOfStream, type={}", TrackTypeToStr(aType));
  mMediaSource->NotifyEndOfStream(aType);
  return IPC_OK();
}

mozilla::ipc::IPCResult MFMediaEngineParent::RecvShutdown() {
  AssertOnManagerThread();
  LOG("Shutdown");
  ENGINE_MARKER("MFMediaEngineParent,Shutdown");
  DestroyEngineIfExists();
  return IPC_OK();
}

void MFMediaEngineParent::HandleRequestSample(const SampleRequest& aRequest) {
  AssertOnManagerThread();
  MOZ_ASSERT(aRequest.mType == TrackInfo::TrackType::kAudioTrack ||
             aRequest.mType == TrackInfo::TrackType::kVideoTrack);
  (void)SendRequestSample(aRequest.mType, aRequest.mIsEnough);
}

void MFMediaEngineParent::AssertOnManagerThread() const {
  MOZ_ASSERT(mManagerThread->IsOnCurrentThread());
}

Maybe<gfx::IntSize> MFMediaEngineParent::DetectVideoSizeChange() {
  AssertOnManagerThread();
  MOZ_ASSERT(mMediaEngine);
  MOZ_ASSERT(mMediaEngine->HasVideo());

  DWORD width, height;
  RETURN_PARAM_IF_FAILED(mMediaEngine->GetNativeVideoSize(&width, &height),
                         Nothing());
  if (width != mDisplayWidth || height != mDisplayHeight) {
    ENGINE_MARKER_TEXT("MFMediaEngineParent,VideoSizeChange",
                       nsPrintfCString("%lux%lu", width, height));
    LOG("Updated video size [{}x{}] -> [{}x{}] ", mDisplayWidth, mDisplayHeight,
        width, height);
    mDisplayWidth = width;
    mDisplayHeight = height;
    return Some(gfx::IntSize{width, height});
  }
  return Nothing();
}

void MFMediaEngineParent::EnsureDcompSurfaceHandle() {
  AssertOnManagerThread();
  MOZ_ASSERT(mMediaEngine);
  MOZ_ASSERT(mMediaEngine->HasVideo());

  if (!mDCompModeEnabled) {
    LOG("Skip EnsureDcompSurfaceHandle: DComp mode not enabled");
    return;
  }
  ComPtr<IMFMediaEngineEx> mediaEngineEx;
  RETURN_VOID_IF_FAILED(mMediaEngine.As(&mediaEngineEx));

  // Ensure that the width and height is already up-to-date.
  gfx::IntSize size{mDisplayWidth, mDisplayHeight};
  if (auto newSize = DetectVideoSizeChange()) {
    size = *newSize;
  }

  // A zero-rect poisons EnableWindowlessSwapchainMode, causing all subsequent
  // GetVideoSwapchainHandle calls to return S_FALSE. Wait for a real size.
  // See https://issues.chromium.org/issues/40218622#comment5
  if (size.IsEmpty()) {
    LOG("EnsureDcompSurfaceHandle: size is empty, deferring");
    return;
  }

  // Update stream size before asking for a handle. If we don't update the
  // size, media engine will create the dcomp surface in a wrong size.
  RECT rect = {0, 0, (LONG)size.width, (LONG)size.height};
  HRESULT rv = mediaEngineEx->UpdateVideoStream(nullptr /* pSrc */, &rect,
                                                nullptr /* pBorderClr */);
  if (MOZ_UNLIKELY(FAILED(rv))) {
    LOG("UpdateVideoStream failed, hr={:x}", rv);
    (void)SendNotifyError(
        MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                    nsPrintfCString("UpdateVideoStream (hr=%lx)", rv),
                    Some(static_cast<int32_t>(rv))));
    return;
  }

  HANDLE surfaceHandle = INVALID_HANDLE_VALUE;
  rv = mediaEngineEx->GetVideoSwapchainHandle(&surfaceHandle);
  if (FAILED(rv)) {
    if (IsHardwareResetHRESULT(rv)) {
      LOG("GetVideoSwapchainHandle failed with hardware reset hr={:x}", rv);
      (void)SendNotifyHardwareReset(static_cast<uint32_t>(rv));
    } else {
      LOG("GetVideoSwapchainHandle failed, hr={:x}", rv);
      MediaResult error(
          NS_ERROR_DOM_MEDIA_DECODE_ERR,
          nsPrintfCString("GetVideoSwapchainHandle failed (hr=%lx)", rv),
          Some(static_cast<int32_t>(rv)));
      (void)SendNotifyError(error);
    }
    return;
  }
  if (surfaceHandle && surfaceHandle != INVALID_HANDLE_VALUE) {
    LOG("EnsureDcompSurfaceHandle, handle={}, size=[{}x{}]",
        fmt::ptr(surfaceHandle), size.width, size.height);
    mMediaSource->SetDCompSurfaceHandle(surfaceHandle, size);
  } else {
    // The surface isn't ready yet (e.g. the first frame hasn't been decoded).
    // EnsureDcompSurfaceHandle will be called again on the next engine event.
    LOG("SurfaceHandle not ready yet, will retry later");
  }
}

void MFMediaEngineParent::NotifyVideoResizing() {
  AssertOnManagerThread();
  if (auto newSize = DetectVideoSizeChange()) {
    (void)SendNotifyResizing(newSize->width, newSize->height);
  }
}

void MFMediaEngineParent::UpdateStatisticsData() {
  AssertOnManagerThread();

  // Statistic data is only for video.
  if (!mMediaEngine->HasVideo()) {
    return;
  }

  ComPtr<IMFMediaEngineEx> mediaEngineEx;
  RETURN_VOID_IF_FAILED(mMediaEngine.As(&mediaEngineEx));

  struct scopePropVariant : public PROPVARIANT {
    scopePropVariant() { PropVariantInit(this); }
    ~scopePropVariant() { PropVariantClear(this); }
    scopePropVariant(scopePropVariant const&) = delete;
    scopePropVariant& operator=(scopePropVariant const&) = delete;
  };

  // https://docs.microsoft.com/en-us/windows/win32/api/mfmediaengine/ne-mfmediaengine-mf_media_engine_statistic
  scopePropVariant renderedFramesProp, droppedFramesProp;
  RETURN_VOID_IF_FAILED(mediaEngineEx->GetStatistics(
      MF_MEDIA_ENGINE_STATISTIC_FRAMES_RENDERED, &renderedFramesProp));
  RETURN_VOID_IF_FAILED(mediaEngineEx->GetStatistics(
      MF_MEDIA_ENGINE_STATISTIC_FRAMES_DROPPED, &droppedFramesProp));

  const unsigned long renderedFrames = renderedFramesProp.ulVal;
  const unsigned long droppedFrames = droppedFramesProp.ulVal;

  // As the amount of rendered frame MUST increase monotonically. If the new
  // statistic data show the decreasing, which means the media engine has reset
  // the statistic data and started a new one. (That will happens after calling
  // flush internally)
  if (renderedFrames < mCurrentPlaybackStatisticData.renderedFrames()) {
    mPrevPlaybackStatisticData =
        StatisticData{mPrevPlaybackStatisticData.renderedFrames() +
                          mCurrentPlaybackStatisticData.renderedFrames(),
                      mPrevPlaybackStatisticData.droppedFrames() +
                          mCurrentPlaybackStatisticData.droppedFrames()};
    mCurrentPlaybackStatisticData = StatisticData{};
  }

  if (mCurrentPlaybackStatisticData.renderedFrames() != renderedFrames ||
      mCurrentPlaybackStatisticData.droppedFrames() != droppedFrames) {
    mCurrentPlaybackStatisticData =
        StatisticData{renderedFrames, droppedFrames};
    const uint64_t totalRenderedFrames =
        mPrevPlaybackStatisticData.renderedFrames() +
        mCurrentPlaybackStatisticData.renderedFrames();
    const uint64_t totalDroppedFrames =
        mPrevPlaybackStatisticData.droppedFrames() +
        mCurrentPlaybackStatisticData.droppedFrames();
    LOG("Update statistic data, rendered={}, dropped={}", totalRenderedFrames,
        totalDroppedFrames);
    (void)SendUpdateStatisticData(
        StatisticData{totalRenderedFrames, totalDroppedFrames});
  }
}

#ifdef MOZ_WMF_CDM
void MFMediaEngineParent::NotifyDisableHWDRM() {
  AssertOnManagerThread();
  RefPtr<ipc::UtilityProcessChild> upc =
      ipc::UtilityProcessChild::GetSingleton();
  if (!upc) {
    return;
  }

  RefPtr<ipc::UtilityMediaServiceParent> umsp = upc->GetMediaService();
  if (!umsp) {
    return;
  }

  ENGINE_MARKER("MFMediaEngineParent::NotifyDisableHWDRM");
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "MFMediaEngineParent::NotifyDisableHWDRM",
      [umsp]() { (void)umsp->SendDisableHardwareDRM(); }));
}
#endif

#undef LOG
#undef RETURN_IF_FAILED
#undef ENSURE_EVENT_DISPATCH_DURING_PLAYING

}  // namespace mozilla
