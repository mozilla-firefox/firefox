/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_mozilla_gfx_ipc_GPUProcessManager_h_
#define _include_mozilla_gfx_ipc_GPUProcessManager_h_

#include "Units.h"
#include "base/basictypes.h"
#include "base/process.h"
#include "mozilla/Hal.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/gfx/GPUProcessHost.h"
#include "mozilla/gfx/PGPUChild.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/ipc/TaskFactory.h"
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsIObserver.h"
#include "nsThreadUtils.h"
class nsIWidget;
enum class DeviceResetReason;

namespace mozilla {
class MemoryReportingProcess;
class PRemoteMediaManagerChild;
class RDDProcessManager;
class RDDChild;
namespace layers {
class IAPZCTreeManager;
class CompositorOptions;
class CompositorSession;
class CompositorUpdateObserver;
class PCompositorBridgeChild;
class PCompositorManagerChild;
class PImageBridgeChild;
class PVideoBridgeParent;
class RemoteCompositorSession;
class InProcessCompositorSession;
class UiCompositorControllerChild;
}  // namespace layers
namespace widget {
class CompositorWidget;
}  // namespace widget
namespace dom {
class ContentParent;
class BrowserParent;
}  // namespace dom
namespace ipc {
class GeckoChildProcessHost;
#ifdef MOZ_WMF_MEDIA_ENGINE
class UtilityMediaServiceChild;
#endif
}  // namespace ipc
namespace gfx {

class GPUChild;
class GPUProcessListener;
class PVRManagerChild;
class VsyncBridgeChild;
class VsyncIOThreadHolder;

// The GPUProcessManager is a singleton responsible for creating GPU-bound
// objects that may live in another process. Currently, it provides access
// to the compositor via CompositorBridgeParent.
class GPUProcessManager final : public GPUProcessHost::Listener {
  friend class layers::RemoteCompositorSession;
  friend class layers::InProcessCompositorSession;

  typedef layers::CompositorOptions CompositorOptions;
  typedef layers::CompositorSession CompositorSession;
  typedef layers::CompositorUpdateObserver CompositorUpdateObserver;
  typedef layers::IAPZCTreeManager IAPZCTreeManager;
  typedef layers::LayersId LayersId;
  typedef layers::PCompositorBridgeChild PCompositorBridgeChild;
  typedef layers::PCompositorManagerChild PCompositorManagerChild;
  typedef layers::PImageBridgeChild PImageBridgeChild;
  typedef layers::PVideoBridgeParent PVideoBridgeParent;
  typedef layers::RemoteCompositorSession RemoteCompositorSession;
  typedef layers::InProcessCompositorSession InProcessCompositorSession;
  typedef layers::UiCompositorControllerChild UiCompositorControllerChild;

 public:
  static void Initialize();
  static void Shutdown();
  static GPUProcessManager* Get();

  ~GPUProcessManager();

  // If not using a GPU process, launch a new GPU process asynchronously.
  nsresult LaunchGPUProcess();
  bool IsGPUProcessLaunching();

  // Ensure that GPU-bound methods can be used. If no GPU process is being
  // used, or one is launched and ready, this function returns immediately.
  // Otherwise it blocks until the GPU process has finished launching.
  // If the GPU process is enabled but has not yet been launched then this will
  // launch the process. If that is not desired then check that return value of
  // Process() is non-null before calling.
  //
  // Returns:
  // - NS_OK if compositing is ready, in either the GPU process or the parent
  // process, even if in shutdown.
  // - NS_ERROR_ILLEGAL_DURING_SHUTDOWN if compositing is not ready, and we are
  // in shutdown.
  // - NS_ERROR_ABORT if on Android and we are in the background. This is a
  // temporary error that we should recover from when in the foreground.
  nsresult EnsureGPUReady();

  bool IsGPUReady() const;

  bool IsDecodingVideoOnGpuProcess() const { return mDecodeVideoOnGpuProcess; }

  already_AddRefed<CompositorSession> CreateTopLevelCompositor(
      nsIWidget* aWidget, CSSToLayoutDeviceScale aScale,
      const CompositorOptions& aOptions, bool aUseExternalSurfaceSize,
      const gfx::IntSize& aSurfaceSize, uint64_t aInnerWindowId, bool* aRetry);

  // It is asserted that IsGPUReady() is true for this method. If not on a path
  // which guarantees that, then the caller must call EnsureGPUReady() and check
  // its return code first.
  bool CreateContentBridges(
      mozilla::ipc::EndpointProcInfo aOtherProcess,
      mozilla::ipc::Endpoint<PCompositorManagerChild>* aOutCompositor,
      mozilla::ipc::Endpoint<PImageBridgeChild>* aOutImageBridge,
      mozilla::ipc::Endpoint<PVRManagerChild>* aOutVRBridge,
      mozilla::ipc::Endpoint<PRemoteMediaManagerChild>* aOutVideoManager,
      dom::ContentParentId aChildId, nsTArray<uint32_t>* aNamespaces);

  nsresult CreateRddVideoBridge(RDDProcessManager* aRDD, RDDChild* aChild);
#ifdef MOZ_WMF_MEDIA_ENGINE
  nsresult CreateUtilityMFCDMVideoBridge(
      mozilla::ipc::UtilityMediaServiceChild* aChild,
      mozilla::ipc::EndpointProcInfo aOtherProcess);
#endif

  // Release compositor-thread resources referred to by |aID|.
  //
  // Must run on the content main thread.
  void UnmapLayerTreeId(LayersId aLayersId, GeckoChildID aOwningId);

  // Checks to see if aLayersId and aRequestingPID have been mapped by
  // MapLayerTreeId
  bool IsLayerTreeIdMapped(LayersId aLayersId, GeckoChildID aRequestingId);

  // Allocate an ID that can be used to refer to a layer tree and
  // associated resources that live only on the compositor thread.
  //
  // Must run on the browser main thread.
  LayersId AllocateLayerTreeId();

  // Allocate an ID that can be used as Namespace and
  // Must run on the browser main thread.
  uint32_t AllocateNamespace();

  // Allocate a layers ID and connect it to a compositor. If the compositor is
  // null, the connect operation will not be performed, but an ID will still be
  // allocated. This must be called from the browser main thread.
  //
  // It also maps the layer tree and process together so that aOwningPID is
  // allowed to access aLayersId across process.
  //
  // Note that a layer tree id is always allocated, even if this returns false.
  bool AllocateAndConnectLayerTreeId(PCompositorBridgeChild* aCompositorBridge,
                                     GeckoChildID aOtherChildID,
                                     LayersId aEmbedderLayersId,
                                     LayersId* aOutLayersId,
                                     CompositorOptions* aOutCompositorOptions);

  // Destroy and recreate all of the compositors
  void ResetCompositors();

  // Record the device reset in telemetry / annotate the crash report.
  static void RecordDeviceReset(DeviceResetReason aReason);

  static void NotifyDeviceReset(DeviceResetReason aReason,
                                DeviceResetDetectPlace aPlace);

  void OnProcessLaunchComplete(GPUProcessHost* aHost) override;
  void OnProcessUnexpectedShutdown(GPUProcessHost* aHost) override;
  void SimulateDeviceReset();
  void DisableWebRender(wr::WebRenderError aError, const nsCString& aMsg);
  void NotifyWebRenderError(wr::WebRenderError aError);
  void OnInProcessDeviceReset(DeviceResetReason aReason,
                              DeviceResetDetectPlace aPlace);
  void OnRemoteProcessDeviceReset(
      GPUProcessHost* aHost, const DeviceResetReason& aReason,
      const DeviceResetDetectPlace& aPlace) override;
  void OnProcessDeclaredStable() override;
  void NotifyListenersOnCompositeDeviceReset();

  // Notify the GPUProcessManager that a top-level PGPU protocol has been
  // terminated. This may be called from any thread.
  void NotifyRemoteActorDestroyed(const uint64_t& aProcessToken);

  void AddListener(GPUProcessListener* aListener);
  void RemoveListener(GPUProcessListener* aListener);

  // Send a message to the GPU process to flush any active checkerboard reports.
  // Returns true if the message was sent, false if not.
  bool FlushActiveCheckerboardReports();

  // Kills the GPU process. Used in normal operation to recover from an error,
  // as well as for tests and diagnostics.
  void KillProcess(bool aGenerateMinidump = false);

  // Kills the GPU process for tests. This exercises the same recovery path as
  // an unexpected shutdown, but does not count towards disabling the GPU
  // process or any of its features.
  void KillProcessForTesting();

  // Invoked when we know we will shutdown (but before shutdown begins), to
  // avoid races with other shutdown observers.
  void StopBatteryObserving();

  // Causes the GPU process to crash. Used for tests and diagnostics
  void CrashProcess();

  // Returns -1 if there is no GPU process, or the platform pid for it.
  base::ProcessId GPUProcessPid();

  // Returns Invalid() if there is no GPU process, or the proc info for it.
  mozilla::ipc::EndpointProcInfo GPUEndpointProcInfo();

  // If a GPU process is present, create a MemoryReportingProcess object.
  // Otherwise, return null.
  RefPtr<MemoryReportingProcess> GetProcessMemoryReporter();

  // Returns access to the PGPU protocol if a GPU process is present.
  GPUChild* GetGPUChild() { return mGPUChild; }

  // Returns whether or not a GPU process was ever launched.
  bool AttemptedGPUProcess() const { return mTotalProcessAttempts > 0; }

  // Returns the process host
  GPUProcessHost* Process() { return mProcess; }

  // Sets the value of mAppInForeground, and (on Windows) adjusts the priority
  // of the GPU process accordingly.
  void SetAppInForeground(bool aInForeground);

  /*
   * ** Test-only Method **
   *
   * Trigger GPU-process test metric instrumentation.
   */
  RefPtr<PGPUChild::TestTriggerMetricsPromise> TestTriggerMetrics();

 private:
  void OnPreferenceChange(const char16_t* aData);
  void ScreenInformationChanged();

  bool CreateContentCompositorManager(
      mozilla::ipc::EndpointProcInfo aOtherProcess,
      dom::ContentParentId aChildId, uint32_t aNamespace,
      uint32_t aContentBridgeNamespace,
      mozilla::ipc::Endpoint<PCompositorManagerChild>* aOutEndpoint);
  bool CreateContentImageBridge(
      mozilla::ipc::EndpointProcInfo aOtherProcess,
      dom::ContentParentId aChildId, uint32_t aNamespace,
      mozilla::ipc::Endpoint<PImageBridgeChild>* aOutEndpoint);
  bool CreateContentVRManager(
      mozilla::ipc::EndpointProcInfo aOtherProcess,
      dom::ContentParentId aChildId, uint32_t aNamespace,
      mozilla::ipc::Endpoint<PVRManagerChild>* aOutEndpoint);
  void CreateContentRemoteMediaManager(
      mozilla::ipc::EndpointProcInfo aOtherProcess,
      dom::ContentParentId aChildId,
      mozilla::ipc::Endpoint<PRemoteMediaManagerChild>* aOutEndPoint);

  nsresult EnsureVideoBridge(
      layers::VideoBridgeSource aSource,
      mozilla::ipc::EndpointProcInfo aOtherProcess,
      mozilla::ipc::Endpoint<layers::PVideoBridgeChild>* aOutChildPipe);

  // Called from RemoteCompositorSession. We track remote sessions so we can
  // notify their owning widgets that the session must be restarted.
  void RegisterRemoteProcessSession(RemoteCompositorSession* aSession);
  void UnregisterRemoteProcessSession(RemoteCompositorSession* aSession);

  // Called from InProcessCompositorSession. We track in process sessino so we
  // can notify their owning widgets that the session must be restarted
  void RegisterInProcessSession(InProcessCompositorSession* aSession);
  void UnregisterInProcessSession(InProcessCompositorSession* aSession);

  void DestroyRemoteCompositorSessions();
  void DestroyInProcessCompositorSessions();

  // Returns true if we crossed the threshold such that we should disable
  // acceleration.
  bool OnDeviceReset(bool aTrackThreshold);

  void DisableWebRenderConfig(wr::WebRenderError aError, const nsCString& aMsg);

  void FallbackToSoftware(const char* aMessage);

 private:
  GPUProcessManager();

  // Permanently disable the GPU process and record a message why.
  void DisableGPUProcess(const char* aMessage);

  // May permanently disable the GPU process and record a message why. May
  // return false if the fallback process decided we should retry the GPU
  // process, but only if aAllowRestart is also true.
  bool MaybeDisableGPUProcess(const char* aMessage, bool aAllowRestart);

  bool FallbackFromAcceleration(wr::WebRenderError aError,
                                const nsCString& aMsg);

  // Crashes the parent process if we are disabling the GPU process and we
  // ever once had a stable GPU process. This is to avoid fallback into the
  // parent when we know the configuration allows for the GPU process.
  void MaybeCrashIfGpuProcessOnceStable();

  void ResetProcessStable();

  // Returns true if the composting pocess is currently considered to be stable.
  bool IsProcessStable(const TimeStamp& aNow);

  // Shutdown the GPU process.
  void ShutdownInternal();
  // Destroy the process and clean up resources.
  // Setting aUnexpectedShutdown = true indicates that this is being called to
  // clean up resources in response to an unexpected shutdown having been
  // detected.
  void DestroyProcess(bool aUnexpectedShutdown = false);

  void HandleProcessLost();
  // Reinitialize rendering following a GPU process loss.
  void ReinitializeRendering();

  void EnsureVsyncIOThread();
  void ShutdownVsyncIOThread();

  bool EnsureProtocolsReady();
  bool EnsureCompositorManagerChild();
  bool EnsureImageBridgeChild();
  bool EnsureVRManager();

#if defined(XP_WIN)
  void SetProcessIsForeground();
#endif

#if defined(MOZ_WIDGET_ANDROID)
  RefPtr<UiCompositorControllerChild> CreateUiCompositorController(
      nsIWidget* aWidget, const LayersId aId);
#endif  // defined(MOZ_WIDGET_ANDROID)

  RefPtr<CompositorSession> CreateRemoteSession(
      nsIWidget* aWidget, const LayersId& aRootLayerTreeId,
      CSSToLayoutDeviceScale aScale, const CompositorOptions& aOptions,
      bool aUseExternalSurfaceSize, const gfx::IntSize& aSurfaceSize,
      uint64_t aInnerWindowId);

  DISALLOW_COPY_AND_ASSIGN(GPUProcessManager);

  void NotifyObserve(const char* aTopic, const char16_t* aData);
  void NotifyBatteryInfo(const hal::BatteryInformation& aBatteryInfo);

  class Observer final : public nsIObserver {
   public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOBSERVER

    Observer();
    void Shutdown();

   protected:
    virtual ~Observer() = default;
  };
  friend class Observer;

  class BatteryObserver final : public hal::BatteryObserver {
   public:
    NS_INLINE_DECL_REFCOUNTING(BatteryObserver)

    BatteryObserver();
    void Notify(const hal::BatteryInformation& aBatteryInfo) override;
    void Shutdown();

   protected:
    ~BatteryObserver() override = default;
  };

 private:
  bool mDecodeVideoOnGpuProcess = true;

  RefPtr<Observer> mObserver;
  RefPtr<BatteryObserver> mBatteryObserver;
  mozilla::ipc::TaskFactory<GPUProcessManager> mTaskFactory;
  RefPtr<VsyncIOThreadHolder> mVsyncIOThread;
  uint32_t mNextNamespace;

  uint32_t mUnstableProcessAttempts;
  uint32_t mTotalProcessAttempts;
  uint32_t mLaunchProcessAttempts = 0;
  TimeStamp mProcessAttemptLastTime;

  nsTArray<RefPtr<RemoteCompositorSession>> mRemoteSessions;
  nsTArray<RefPtr<InProcessCompositorSession>> mInProcessSessions;
  nsTArray<RefPtr<GPUProcessListener>> mListeners;

  uint32_t mDeviceResetCount;
  TimeStamp mDeviceResetLastTime;

  // Keeps track of whether not the application is in the foreground on android.
  bool mAppInForeground;

  // Fields that are associated with the current GPU process.
  RefPtr<GPUProcessHost> mProcess;
  uint64_t mProcessToken;
  bool mProcessStable = false;
  bool mProcessStableOnce = false;
  Maybe<wr::WebRenderError> mLastError;
  Maybe<nsCString> mLastErrorMsg;
  GPUChild* mGPUChild;
  RefPtr<VsyncBridgeChild> mVsyncBridge;
  // Collects any pref changes that occur during process launch (after
  // the initial map is passed in command-line arguments) to be sent
  // when the process can receive IPC messages.
  nsTArray<mozilla::dom::Pref> mQueuedPrefs;
};

}  // namespace gfx
}  // namespace mozilla

#endif  // _include_mozilla_gfx_ipc_GPUProcessManager_h_
