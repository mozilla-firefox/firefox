/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_gfx_ipc_GPUParent_h_
#define _include_gfx_ipc_GPUParent_h_

#include "mozilla/RefPtr.h"
#include "mozilla/gfx/PGPUParent.h"
#include "mozilla/ipc/AsyncBlockers.h"
#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
#  include "mozilla/PSandboxTestingChild.h"
#endif

namespace mozilla {

class TimeStamp;
class ChildProfilerController;

namespace gfx {

class VsyncBridgeParent;

class GPUParent final : public PGPUParent {
 public:
  NS_INLINE_DECL_REFCOUNTING(GPUParent, final)

  GPUParent();

  static GPUParent* GetSingleton();

  ipc::AsyncBlockers& AsyncShutdownService() { return mShutdownBlockers; }

  // Gets the name of the GPU process, in the format expected by about:memory.
  // There must be a GPU process active, and the caller must be either in that
  // process or the parent process.
  static void GetGPUProcessName(nsACString& aStr);

  // Check for memory pressure and notify the parent process if necessary.
  static bool MaybeFlushMemory();

  bool Init(mozilla::ipc::UntypedEndpoint&& aEndpoint,
            const char* aParentBuildID);
  void NotifyDeviceReset(DeviceResetReason aReason,
                         DeviceResetDetectPlace aPlace);
  void NotifyOverlayInfo(layers::OverlayInfo aInfo);
  void NotifySwapChainInfo(layers::SwapChainInfo aInfo);
  void NotifyDisableRemoteCanvas();

  void ReportGLStrings(GfxInfoGLStrings&& aStrings);

  mozilla::ipc::IPCResult RecvInit(nsTArray<GfxVarUpdate>&& vars,
                                   const DevicePrefs& devicePrefs,
                                   nsTArray<LayerTreeIdMapping>&& mappings,
                                   nsTArray<GfxInfoFeatureStatus>&& features,
                                   uint32_t wrNamespace, bool aDecodeVideo,
                                   InitResolver&& aInitResolver);
  mozilla::ipc::IPCResult RecvInitCompositorManager(
      Endpoint<PCompositorManagerParent>&& aEndpoint, uint32_t aNamespace);
  mozilla::ipc::IPCResult RecvInitVsyncBridge(
      Endpoint<PVsyncBridgeParent>&& aVsyncEndpoint);
  mozilla::ipc::IPCResult RecvInitImageBridge(
      Endpoint<PImageBridgeParent>&& aEndpoint, uint32_t aNamespace);
  mozilla::ipc::IPCResult RecvInitVideoBridge(
      Endpoint<PVideoBridgeParent>&& aEndpoint,
      const layers::VideoBridgeSource& aSource);
  mozilla::ipc::IPCResult RecvInitVRManager(
      Endpoint<PVRManagerParent>&& aEndpoint, uint32_t aNamespace);
  mozilla::ipc::IPCResult RecvInitVR(Endpoint<PVRGPUChild>&& aVRGPUChild);
  mozilla::ipc::IPCResult RecvInitUiCompositorController(
      const LayersId& aRootLayerTreeId,
      Endpoint<PUiCompositorControllerParent>&& aEndpoint);
  mozilla::ipc::IPCResult RecvInitAPZBridge(
      const LayersId& aRootLayerTreeId, Endpoint<PAPZBridgeParent>&& aEndpoint);
  mozilla::ipc::IPCResult RecvInitProfiler(
      Endpoint<PProfilerChild>&& aEndpoint);
  mozilla::ipc::IPCResult RecvUpdateVar(const nsTArray<GfxVarUpdate>& var);
  mozilla::ipc::IPCResult RecvPreferenceUpdate(const Pref& pref);
  mozilla::ipc::IPCResult RecvScreenInformationChanged();
  mozilla::ipc::IPCResult RecvNotifyBatteryInfo(
      const BatteryInformation& aBatteryInfo);
  mozilla::ipc::IPCResult RecvNewContentCompositorManager(
      Endpoint<PCompositorManagerParent>&& aEndpoint,
      const ContentParentId& aChildId, uint32_t aNamespace,
      uint32_t aContentBridgeNamespace);
  mozilla::ipc::IPCResult RecvNewContentImageBridge(
      Endpoint<PImageBridgeParent>&& aEndpoint, const ContentParentId& aChildId,
      uint32_t aNamespace);
  mozilla::ipc::IPCResult RecvNewContentVRManager(
      Endpoint<PVRManagerParent>&& aEndpoint, const ContentParentId& aChildId,
      uint32_t aNamespace);
  mozilla::ipc::IPCResult RecvNewContentRemoteMediaManager(
      Endpoint<PRemoteMediaManagerParent>&& aEndpoint,
      const ContentParentId& aChildId);
  mozilla::ipc::IPCResult RecvGetDeviceStatus(GPUDeviceData* aOutStatus);
  mozilla::ipc::IPCResult RecvSimulateDeviceReset();
  mozilla::ipc::IPCResult RecvAddLayerTreeIdMapping(
      const LayerTreeIdMapping& aMapping);
  mozilla::ipc::IPCResult RecvRemoveLayerTreeIdMapping(
      const LayerTreeIdMapping& aMapping);
  mozilla::ipc::IPCResult RecvFlushActiveCheckerboardReports();
  mozilla::ipc::IPCResult RecvRequestMemoryReport(
      const uint32_t& generation, const bool& anonymize,
      const bool& minimizeMemoryUsage,
      const Maybe<ipc::FileDescriptor>& DMDFile,
      const RequestMemoryReportResolver& aResolver);
  mozilla::ipc::IPCResult RecvShutdownVR();

  mozilla::ipc::IPCResult RecvUpdatePerfStatsCollectionMask(
      const uint64_t& aMask);
  mozilla::ipc::IPCResult RecvCollectPerfStatsJSON(
      CollectPerfStatsJSONResolver&& aResolver);

#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
  mozilla::ipc::IPCResult RecvInitSandboxTesting(
      Endpoint<PSandboxTestingChild>&& aEndpoint);
#endif

  mozilla::ipc::IPCResult RecvFlushFOGData(FlushFOGDataResolver&& aResolver);

  mozilla::ipc::IPCResult RecvTestTriggerMetrics(
      TestTriggerMetricsResolver&& aResolve);

  mozilla::ipc::IPCResult RecvCrashProcess();

  void ActorDestroy(ActorDestroyReason aWhy) override;

 private:
  ~GPUParent();

  const TimeStamp mLaunchTime;
  // Whether content processes may decode or encode media in this process.
  bool mDecodeVideo = true;
  RefPtr<VsyncBridgeParent> mVsyncBridge;
  RefPtr<ChildProfilerController> mProfilerController;
  ipc::AsyncBlockers mShutdownBlockers;
};

}  // namespace gfx
}  // namespace mozilla

#endif  // _include_gfx_ipc_GPUParent_h_
