/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "UtilityMediaServiceChild.h"

#include "base/basictypes.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/gfx/gfxVars.h"

#ifdef MOZ_WMF_MEDIA_ENGINE
#  include "mozilla/StaticPrefs_media.h"
#  include "mozilla/gfx/GPUProcessManager.h"
#  include "mozilla/ipc/UtilityProcessManager.h"
#  include "mozilla/layers/PVideoBridge.h"
#  include "mozilla/layers/VideoBridgeParent.h"
#  include "mozilla/layers/VideoBridgeUtils.h"
#endif

#ifdef MOZ_WMF_CDM
#  include "mozilla/dom/Promise.h"
#  include "mozilla/EMEUtils.h"
#  include "mozilla/PMFCDM.h"
#endif

namespace mozilla::ipc {

NS_IMETHODIMP UtilityMediaServiceChildShutdownObserver::Observe(
    nsISupports* /*aSubject*/, const char* aTopic, const char16_t* /*aData*/) {
  MOZ_ASSERT(strcmp(aTopic, "ipc:utility-shutdown") == 0);

  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (observerService) {
    observerService->RemoveObserver(this, "ipc:utility-shutdown");
  }

  UtilityMediaServiceChild::Shutdown(mSandbox);
  return NS_OK;
}

NS_IMPL_ISUPPORTS(UtilityMediaServiceChildShutdownObserver, nsIObserver);

static EnumeratedArray<SandboxingKind, StaticRefPtr<UtilityMediaServiceChild>,
                       size_t(SandboxingKind::COUNT)>
    sAudioDecoderChilds;

UtilityMediaServiceChild::UtilityMediaServiceChild(SandboxingKind aKind)
    : mSandbox(aKind), mAudioDecoderChildStart(TimeStamp::Now()) {
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (observerService) {
    auto* obs = new UtilityMediaServiceChildShutdownObserver(aKind);
    observerService->AddObserver(obs, "ipc:utility-shutdown", false);
  }
}

nsresult UtilityMediaServiceChild::BindToUtilityProcess(
    RefPtr<UtilityProcessParent> aUtilityParent) {
  if (!aUtilityParent) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  Endpoint<PUtilityMediaServiceChild> utilityMediaServiceChildEnd;
  Endpoint<PUtilityMediaServiceParent> utilityMediaServiceParentEnd;
  nsresult rv = PUtilityMediaService::CreateEndpoints(
      aUtilityParent->OtherEndpointProcInfo(), EndpointProcInfo::Current(),
      &utilityMediaServiceParentEnd, &utilityMediaServiceChildEnd);

  if (NS_FAILED(rv)) {
    MOZ_ASSERT(false, "Protocol endpoints failure");
    return NS_ERROR_FAILURE;
  }

  nsTArray<gfx::GfxVarUpdate> updates = gfx::gfxVars::FetchNonDefaultVars();
  if (!aUtilityParent->SendStartUtilityMediaService(
          std::move(utilityMediaServiceParentEnd), std::move(updates))) {
    MOZ_ASSERT(false, "StartUtilityMediaService service failure");
    return NS_ERROR_FAILURE;
  }

  Bind(std::move(utilityMediaServiceChildEnd));

  PROFILER_MARKER_UNTYPED("UtilityMediaServiceChild::BindToUtilityProcess", IPC,
                          MarkerOptions(MarkerTiming::IntervalUntilNowFrom(
                              mAudioDecoderChildStart)));
  return NS_OK;
}

void UtilityMediaServiceChild::ActorDestroy(ActorDestroyReason /*aReason*/) {
  MOZ_ASSERT(NS_IsMainThread());
  gfx::gfxVars::RemoveReceiver(this);
#ifdef MOZ_WMF_MEDIA_ENGINE
  mHasCreatedVideoBridge = State::None;
  if (auto* gpm = gfx::GPUProcessManager::Get()) {
    // Note: the manager could have shutdown already.
    gpm->RemoveListener(this);
  }
#endif
  Shutdown(mSandbox);
}

void UtilityMediaServiceChild::Bind(
    Endpoint<PUtilityMediaServiceChild>&& aEndpoint) {
  MOZ_ASSERT(NS_IsMainThread());
  if (NS_WARN_IF(!aEndpoint.Bind(this))) {
    MOZ_ASSERT_UNREACHABLE("Failed to bind UtilityMediaServiceChild!");
    return;
  }
  gfx::gfxVars::AddReceiver(this);
}

/* static */
void UtilityMediaServiceChild::Shutdown(SandboxingKind aKind) {
  sAudioDecoderChilds[aKind] = nullptr;
}

/* static */
RefPtr<UtilityMediaServiceChild> UtilityMediaServiceChild::GetSingleton(
    SandboxingKind aKind) {
  MOZ_ASSERT(NS_IsMainThread());
  bool shutdown = AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMWillShutdown);
  if (!sAudioDecoderChilds[aKind] && !shutdown) {
    sAudioDecoderChilds[aKind] = new UtilityMediaServiceChild(aKind);
  }
  return sAudioDecoderChilds[aKind];
}

mozilla::ipc::IPCResult
UtilityMediaServiceChild::RecvUpdateMediaCodecsSupported(
    const RemoteMediaIn& aLocation,
    const media::MediaCodecsSupported& aSupported) {
  dom::ContentParent::BroadcastMediaCodecsSupportedUpdate(aLocation,
                                                          aSupported);
  return IPC_OK();
}

void UtilityMediaServiceChild::OnVarChanged(
    const nsTArray<gfx::GfxVarUpdate>& aVar) {
  SendUpdateVar(aVar);
}

#ifdef MOZ_WMF_MEDIA_ENGINE
mozilla::ipc::IPCResult
UtilityMediaServiceChild::RecvCompleteCreatedVideoBridge() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mSandbox == SandboxingKind::MF_MEDIA_ENGINE_CDM);
  if (mHasCreatedVideoBridge == State::Creating) {
    mHasCreatedVideoBridge = State::Created;
  } else {
    MOZ_ASSERT_UNREACHABLE("Video bridge created but was not creating?");
  }
  return IPC_OK();
}

void UtilityMediaServiceChild::OnCompositorUnexpectedShutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mSandbox == SandboxingKind::MF_MEDIA_ENGINE_CDM);
  mHasCreatedVideoBridge = State::None;

  if (auto* gpm = gfx::GPUProcessManager::Get()) {
    if (auto utilpm = UtilityProcessManager::GetSingleton()) {
      if (auto keepAlive = utilpm->GetSharedKeepAlive(mSandbox)) {
        RefPtr parent = keepAlive->GetProcessParent();
        if (!parent) {
          return;
        }
        if (NS_SUCCEEDED(gpm->CreateUtilityMFCDMVideoBridge(
                this, parent->OtherEndpointProcInfo()))) {
          mHasCreatedVideoBridge = State::Creating;
        }
      }
    }
  }
}

bool UtilityMediaServiceChild::CreateVideoBridge(
    mozilla::ipc::EndpointProcInfo aOtherProcess) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mSandbox == SandboxingKind::MF_MEDIA_ENGINE_CDM);

  // Creating or already created, avoiding reinit a bridge.
  if (mHasCreatedVideoBridge != State::None) {
    return true;
  }

  auto* gpm = gfx::GPUProcessManager::Get();
  if (NS_WARN_IF(!gpm) || NS_WARN_IF(NS_FAILED(gpm->EnsureGPUReady())) ||
      NS_WARN_IF(
          NS_FAILED(gpm->CreateUtilityMFCDMVideoBridge(this, aOtherProcess)))) {
    return false;
  }

  gpm->AddListener(this);
  mHasCreatedVideoBridge = State::Creating;
  return true;
}
#endif

#ifdef MOZ_WMF_CDM
void UtilityMediaServiceChild::GetKeySystemCapabilities(
    dom::Promise* aPromise) {
  EME_LOG("Ask capabilities for all supported CDMs");
  SendGetKeySystemCapabilities()->Then(
      NS_GetCurrentThread(), __func__,
      [promise = RefPtr<dom::Promise>(aPromise)](
          CopyableTArray<MFCDMCapabilitiesIPDL>&& result) {
        FallibleTArray<dom::CDMInformation> cdmInfo;
        for (const auto& capabilities : result) {
          EME_LOG("Received capabilities for %s",
                  NS_ConvertUTF16toUTF8(capabilities.keySystem()).get());
          for (const auto& v : capabilities.videoCapabilities()) {
            for (const auto& scheme : v.encryptionSchemes()) {
              EME_LOG("  capabilities: video=%s, scheme=%s",
                      NS_ConvertUTF16toUTF8(v.contentType()).get(),
                      EnumValueToString(scheme));
            }
          }
          for (const auto& a : capabilities.audioCapabilities()) {
            for (const auto& scheme : a.encryptionSchemes()) {
              EME_LOG("  capabilities: audio=%s, scheme=%s",
                      NS_ConvertUTF16toUTF8(a.contentType()).get(),
                      EnumValueToString(scheme));
            }
          }
          auto* info = cdmInfo.AppendElement(fallible);
          if (!info) {
            promise->MaybeReject(NS_ERROR_OUT_OF_MEMORY);
            return;
          }
          info->mKeySystemName = capabilities.keySystem();

          KeySystemConfig config;
          MFCDMCapabilitiesIPDLToKeySystemConfig(capabilities, config);
          info->mCapabilities = config.GetDebugInfo();
          info->mClearlead =
              DoesKeySystemSupportClearLead(info->mKeySystemName);
          info->mIsHardwareDecryption = capabilities.isHardwareDecryption();
        }
        promise->MaybeResolve(cdmInfo);
      },
      [promise = RefPtr<dom::Promise>(aPromise)](
          const mozilla::ipc::ResponseRejectReason& aReason) {
        EME_LOG("IPC failure for GetKeySystemCapabilities!");
        promise->MaybeReject(NS_ERROR_DOM_MEDIA_CDM_ERR);
      });
}

mozilla::ipc::IPCResult UtilityMediaServiceChild::RecvDisableHardwareDRM() {
  MOZ_ASSERT(NS_IsMainThread());
  static constexpr const char* kHardDRMPref = "media.eme.hwdrm.failed";
  Preferences::SetBool(kHardDRMPref, true);
  return IPC_OK();
}
#endif

}  // namespace mozilla::ipc
