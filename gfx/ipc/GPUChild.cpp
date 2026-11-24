/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "GPUChild.h"

#include "GPUProcessHost.h"
#include "GPUProcessManager.h"
#include "GfxInfoBase.h"
#include "ProfilerParent.h"
#include "TelemetryProbesReporter.h"
#include "VRProcessManager.h"
#include "VideoUtils.h"
#include "gfxConfig.h"
#include "gfxPlatform.h"
#include "mozilla/Components.h"
#include "mozilla/FOGIPC.h"
#include "mozilla/HangDetails.h"
#include "mozilla/RemoteMediaManagerChild.h"  // For RemoteMediaIn
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/Telemetry.h"
#include "mozilla/TelemetryIPC.h"
#include "mozilla/dom/CheckerboardReportService.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/MemoryReportRequest.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/glean/IpcMetrics.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/APZBridgeChild.h"
#include "mozilla/layers/LayerTreeOwnerTracker.h"
#include "nsHashPropertyBag.h"
#include "nsIGfxInfo.h"
#include "nsIObserverService.h"
#include "nsIPropertyBag2.h"

namespace mozilla {
namespace gfx {

using namespace layers;

GPUChild::GPUChild(GPUProcessHost* aHost) : mHost(aHost), mGPUReady(false) {}

GPUChild::~GPUChild() = default;

RefPtr<GPUChild::InitPromiseType> GPUChild::Init() {
  nsTArray<GfxVarUpdate> updates = gfxVars::FetchNonDefaultVars();

  DevicePrefs devicePrefs;
  devicePrefs.hwCompositing() = gfxConfig::GetValue(Feature::HW_COMPOSITING);
  devicePrefs.d3d11Compositing() =
      gfxConfig::GetValue(Feature::D3D11_COMPOSITING);
  devicePrefs.oglCompositing() =
      gfxConfig::GetValue(Feature::OPENGL_COMPOSITING);
  devicePrefs.d3d11HwAngle() = gfxConfig::GetValue(Feature::D3D11_HW_ANGLE);

  nsTArray<LayerTreeIdMapping> mappings;
  LayerTreeOwnerTracker::Get()->Iterate(
      [&](LayersId aLayersId, GeckoChildID aChildID) {
        mappings.AppendElement(LayerTreeIdMapping(aLayersId, aChildID));
      });

  nsCOMPtr<nsIGfxInfo> gfxInfo = components::GfxInfo::Service();
  nsTArray<GfxInfoFeatureStatus> features;
  if (gfxInfo) {
    auto* gfxInfoRaw = static_cast<widget::GfxInfoBase*>(gfxInfo.get());
    features = gfxInfoRaw->GetAllFeatures();
  }

  RefPtr<InitPromiseType> promise =
      SendInit(updates, devicePrefs, mappings, features,
               GPUProcessManager::Get()->AllocateNamespace(),
               GPUProcessManager::Get()->IsDecodingVideoOnGpuProcess())
          ->Then(
              GetCurrentSerialEventTarget(), __func__,
              [self = RefPtr{this}](GPUDeviceData&& aData) {
                self->OnInitComplete(aData);
                return InitPromiseType::CreateAndResolve(Ok{}, __func__);
              },
              [](ipc::ResponseRejectReason) {
                return InitPromiseType::CreateAndReject(Ok{}, __func__);
              });

  gfxVars::AddReceiver(this);

  (void)SendInitProfiler(ProfilerParent::CreateForProcess(OtherPid()));

  return promise;
}

void GPUChild::OnVarChanged(const nsTArray<GfxVarUpdate>& aVar) {
  SendUpdateVar(aVar);
}

bool GPUChild::EnsureGPUReady(bool aForceSync /* = false */) {
  // On our initial process launch, we want to block on the GetDeviceStatus
  // message. Additionally, we may have updated our compositor configuration
  // through the gfxVars after fallback, in which case we want to ensure the
  // GPU process has handled any updates before creating compositor sessions.
  if (mGPUReady && !aForceSync) {
    return true;
  }

  GPUDeviceData data;
  if (!SendGetDeviceStatus(&data)) {
    return false;
  }

  OnInitComplete(data);
  return true;
}

void GPUChild::OnUnexpectedShutdown() { mUnexpectedShutdown = true; }

void GPUChild::GeneratePairedMinidump() {
  // At most generate the paired minidumps twice per session in order to
  // avoid accumulating a large number of unsubmitted minidumps on disk.
  if (mCrashReporter && mNumPairedMinidumpsCreated < 2) {
    nsAutoCString additionalDumps("browser");
    mCrashReporter->AddAnnotationNSCString(
        CrashReporter::Annotation::additional_minidumps, additionalDumps);

    nsAutoCString reason("GPUProcessKill");
    mCrashReporter->AddAnnotationNSCString(
        CrashReporter::Annotation::ipc_channel_error, reason);

    if (mCrashReporter->GenerateMinidumpAndPair(mHost, "browser"_ns)) {
      mCrashReporter->FinalizeCrashReport();
      mCreatedPairedMinidumps = true;
      mNumPairedMinidumpsCreated++;
    }
  }
}

void GPUChild::DeletePairedMinidump() {
  if (mCrashReporter && mCreatedPairedMinidumps) {
    mCrashReporter->DeleteCrashReport();
    mCreatedPairedMinidumps = false;
  }
}

void GPUChild::OnInitComplete(const GPUDeviceData& aData) {
  // This function may be called multiple times, for example if we synchronously
  // requested GPU parameters before the asynchronous SendInit completed, or if
  // EnsureGPUReady is called after launch to force a synchronization after a
  // configuration change. We only want to import device data and collect
  // telemetry for the initial GPU process launch.
  if (mGPUReady) {
    return;
  }

  gfxPlatform::GetPlatform()->ImportGPUDeviceData(aData);
  glean::gpu_process::launch_time.AccumulateRawDuration(TimeStamp::Now() -
                                                        mHost->GetLaunchTime());
  mGPUReady = true;
}

mozilla::ipc::IPCResult GPUChild::RecvDeclareStable() {
  mHost->mListener->OnProcessDeclaredStable();
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvReportCheckerboard(
    const uint32_t& aSeverity, const nsCString& aLog) {
  layers::CheckerboardEventStorage::Report(aSeverity, std::string(aLog.get()));
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvGraphicsError(const nsCString& aError) {
  gfx::LogForwarder* lf = gfx::Factory::GetLogForwarder();
  if (lf) {
    std::stringstream message;
    message << "GP+" << aError.get();
    lf->UpdateStringsVector(message.str());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvCreateVRProcess() {
  // Make sure create VR process at the main process
  MOZ_ASSERT(XRE_IsParentProcess());
  if (StaticPrefs::dom_vr_process_enabled_AtStartup() &&
      StaticPrefs::dom_vr_enabled()) {
    VRProcessManager::Initialize();
    VRProcessManager* vr = VRProcessManager::Get();
    MOZ_ASSERT(vr, "VRProcessManager must be initialized first.");

    if (vr) {
      vr->LaunchVRProcess();
    }
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvShutdownVRProcess() {
  // Make sure stopping VR process at the main process
  MOZ_ASSERT(XRE_IsParentProcess());
  if (StaticPrefs::dom_vr_process_enabled_AtStartup() &&
      StaticPrefs::dom_vr_enabled()) {
    VRProcessManager::Shutdown();
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvFlushActiveCheckerboardReportsDone() {
  nsCOMPtr<nsIObserverService> obsSvc = mozilla::services::GetObserverService();
  MOZ_ASSERT(obsSvc);
  if (obsSvc) {
    obsSvc->NotifyObservers(nullptr, "APZ:FlushActiveCheckerboard:Done",
                            nullptr);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvAccumulateChildHistograms(
    nsTArray<HistogramAccumulation>&& aAccumulations) {
  TelemetryIPC::AccumulateChildHistograms(Telemetry::ProcessID::Gpu,
                                          aAccumulations);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvAccumulateChildKeyedHistograms(
    nsTArray<KeyedHistogramAccumulation>&& aAccumulations) {
  TelemetryIPC::AccumulateChildKeyedHistograms(Telemetry::ProcessID::Gpu,
                                               aAccumulations);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvUpdateChildScalars(
    nsTArray<ScalarAction>&& aScalarActions) {
  TelemetryIPC::UpdateChildScalars(Telemetry::ProcessID::Gpu, aScalarActions);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvUpdateChildKeyedScalars(
    nsTArray<KeyedScalarAction>&& aScalarActions) {
  TelemetryIPC::UpdateChildKeyedScalars(Telemetry::ProcessID::Gpu,
                                        aScalarActions);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvRecordChildEvents(
    nsTArray<mozilla::Telemetry::ChildEventData>&& aEvents) {
  TelemetryIPC::RecordChildEvents(Telemetry::ProcessID::Gpu, aEvents);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvRecordDiscardedData(
    const mozilla::Telemetry::DiscardedData& aDiscardedData) {
  TelemetryIPC::RecordDiscardedData(Telemetry::ProcessID::Gpu, aDiscardedData);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvNotifyDeviceReset(
    const GPUDeviceData& aData, const DeviceResetReason& aReason,
    const DeviceResetDetectPlace& aPlace) {
  gfxPlatform::GetPlatform()->ImportGPUDeviceData(aData);
  mHost->mListener->OnRemoteProcessDeviceReset(mHost, aReason, aPlace);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvNotifyOverlayInfo(
    const OverlayInfo aInfo) {
  gfxPlatform::GetPlatform()->SetOverlayInfo(aInfo);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvNotifySwapChainInfo(
    const SwapChainInfo aInfo) {
  gfxPlatform::GetPlatform()->SetSwapChainInfo(aInfo);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvNotifyDisableRemoteCanvas() {
  gfxPlatform::DisableRemoteCanvas();
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvFlushMemory(const nsString& aReason) {
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (os) {
    os->NotifyObservers(nullptr, "memory-pressure", aReason.get());
  }
  return IPC_OK();
}

bool GPUChild::SendRequestMemoryReport(const uint32_t& aGeneration,
                                       const bool& aAnonymize,
                                       const bool& aMinimizeMemoryUsage,
                                       const Maybe<FileDescriptor>& aDMDFile) {
  mMemoryReportRequest = std::make_unique<MemoryReportRequestHost>(aGeneration);

  PGPUChild::SendRequestMemoryReport(aGeneration, aAnonymize,
                                     aMinimizeMemoryUsage, aDMDFile)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [](uint32_t aGeneration2) {
            if (GPUProcessManager* gpm = GPUProcessManager::Get()) {
              if (GPUChild* child = gpm->GetGPUChild()) {
                if (child->mMemoryReportRequest) {
                  child->mMemoryReportRequest->Finish(aGeneration2);
                  child->mMemoryReportRequest = nullptr;
                }
              }
            }
          },
          [](mozilla::ipc::ResponseRejectReason) {
            if (GPUProcessManager* gpm = GPUProcessManager::Get()) {
              if (GPUChild* child = gpm->GetGPUChild()) {
                child->mMemoryReportRequest = nullptr;
              }
            }
          });

  return true;
}

mozilla::ipc::IPCResult GPUChild::RecvAddMemoryReport(
    const MemoryReport& aReport) {
  if (mMemoryReportRequest) {
    mMemoryReportRequest->RecvReport(aReport);
  }
  return IPC_OK();
}

void GPUChild::ActorDestroy(ActorDestroyReason aWhy) {
  if ((aWhy == AbnormalShutdown || mUnexpectedShutdown) &&
      !mHost->WasKilledForTesting()) {
    nsAutoCString processType(
        XRE_GeckoProcessTypeToString(GeckoProcessType_GPU));
    glean::subprocess::abnormal_abort.Get(processType).Add(1);

    nsAutoString dumpId;
    if (!mCreatedPairedMinidumps) {
      GenerateCrashReport(&dumpId);
    } else if (mCrashReporter) {
      dumpId = mCrashReporter->MinidumpID();
    }

    // Notify the Telemetry environment so that we can refresh and do a
    // subsession split. This also notifies the crash reporter on geckoview.
    if (nsCOMPtr<nsIObserverService> obsvc = services::GetObserverService()) {
      RefPtr<nsHashPropertyBag> props = new nsHashPropertyBag();
      props->SetPropertyAsBool(u"abnormal"_ns, true);
      props->SetPropertyAsAString(u"dumpID"_ns, dumpId);
      props->SetPropertyAsACString(u"processType"_ns, processType);
      obsvc->NotifyObservers((nsIPropertyBag2*)props,
                             "compositor:process-aborted", nullptr);
    }
  }

  gfxVars::RemoveReceiver(this);
  mHost->OnChannelClosed();
  mHost = nullptr;
}

mozilla::ipc::IPCResult GPUChild::RecvUpdateFeature(
    const Feature& aFeature, const FeatureFailure& aChange) {
  gfxConfig::SetFailed(aFeature, aChange.status(), aChange.message().get(),
                       aChange.failureId());
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvUsedFallback(const Fallback& aFallback,
                                                   const nsCString& aMessage) {
  gfxConfig::EnableFallback(aFallback, aMessage.get());
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvBHRThreadHang(
    const HangDetails& aDetails) {
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    // Copy the HangDetails recieved over the network into a nsIHangDetails, and
    // then fire our own observer notification.
    // XXX: We should be able to avoid this potentially expensive copy here by
    // moving our deserialized argument.
    nsCOMPtr<nsIHangDetails> hangDetails =
        new nsHangDetails(HangDetails(aDetails), PersistedToDisk::No);
    obs->NotifyObservers(hangDetails, "bhr-thread-hang", nullptr);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvUpdateMediaCodecsSupported(
    const media::MediaCodecsSupported& aSupported) {
  if (ContainHardwareCodecsSupported(aSupported)) {
    mozilla::TelemetryProbesReporter::ReportDeviceMediaCodecSupported(
        aSupported);
  }
  MOZ_ASSERT(GPUProcessManager::Get()->IsDecodingVideoOnGpuProcess());
#if defined(XP_WIN)
  // Do not propagate HEVC support if the pref is off
  media::MediaCodecsSupported trimedSupported = aSupported;
  if (aSupported.contains(
          mozilla::media::MediaCodecsSupport::HEVCHardwareDecode) &&
      !StaticPrefs::media_hevc_enabled()) {
    trimedSupported -= mozilla::media::MediaCodecsSupport::HEVCHardwareDecode;
  }
  dom::ContentParent::BroadcastMediaCodecsSupportedUpdate(
      RemoteMediaIn::GpuProcess, trimedSupported);
#else
  dom::ContentParent::BroadcastMediaCodecsSupportedUpdate(
      RemoteMediaIn::GpuProcess, aSupported);
#endif
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvFOGData(ByteBuf&& aBuf) {
  glean::FOGData(std::move(aBuf));
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUChild::RecvReportGLStrings(
    GfxInfoGLStrings&& aStrings) {
  nsCOMPtr<nsIGfxInfo> gfxInfo = components::GfxInfo::Service();
  if (gfxInfo) {
    static_cast<widget::GfxInfoBase*>(gfxInfo.get())
        ->ReportGLStrings(std::move(aStrings));
  }

  return IPC_OK();
}

class DeferredDeleteGPUChild : public Runnable {
 public:
  explicit DeferredDeleteGPUChild(RefPtr<GPUChild>&& aChild)
      : Runnable("gfx::DeferredDeleteGPUChild"), mChild(std::move(aChild)) {}

  NS_IMETHODIMP Run() override { return NS_OK; }

 private:
  RefPtr<GPUChild> mChild;
};

/* static */
void GPUChild::Destroy(RefPtr<GPUChild>&& aChild) {
  NS_DispatchToMainThread(new DeferredDeleteGPUChild(std::move(aChild)));
}

}  // namespace gfx
}  // namespace mozilla
