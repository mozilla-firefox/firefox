/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef XP_WIN
#  include "WMF.h"
#  include "WMFDecoderModule.h"
#  include "WMFEncoderModule.h"
#endif
#include "FFVPXRuntimeLinker.h"
#include "GLContextProvider.h"
#include "GPUParent.h"
#include "GPUProcessHost.h"
#include "GPUProcessLog.h"
#include "GPUProcessManager.h"
#include "GfxInfoBase.h"
#include "MediaCodecsSupport.h"
#include "VRGPUChild.h"
#include "VRManager.h"
#include "VRManagerParent.h"
#include "VsyncBridgeParent.h"
#include "cairo.h"
#include "gfxConfig.h"
#include "gfxCrashReporterUtils.h"
#include "gfxGradientCache.h"
#include "gfxPlatform.h"
#include "mozilla/Assertions.h"
#include "mozilla/Components.h"
#include "mozilla/FOGIPC.h"
#include "mozilla/HangDetails.h"
#include "mozilla/PerfStats.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProcessPriorityManager.h"
#include "mozilla/RemoteMediaManagerChild.h"
#include "mozilla/RemoteMediaManagerParent.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/MemoryReportRequest.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/CanvasRenderThread.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/glean/GleanTestsTestMetrics.h"
#include "mozilla/image/ImageMemoryReporter.h"
#include "mozilla/ipc/CrashReporterClient.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/ipc/ProcessUtils.h"
#include "mozilla/layers/APZBridgeParent.h"
#include "mozilla/layers/APZPublicUtils.h"  // for apz::InitializeGlobalState
#include "mozilla/layers/APZThreadUtils.h"
#include "mozilla/layers/CompositeProcessFencesHolderMap.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorManagerParent.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/ImageBridgeParent.h"
#include "mozilla/layers/LayerTreeOwnerTracker.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/layers/UiCompositorControllerParent.h"
#include "mozilla/layers/VideoBridgeParent.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsDebugImpl.h"
#include "nsIGfxInfo.h"
#include "nsIXULRuntime.h"
#include "nsThreadManager.h"
#include "nscore.h"
#include "prenv.h"
#include "skia/include/core/SkGraphics.h"
#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
#  include "mozilla/SandboxSettings.h"
#endif
#if defined(XP_WIN)
#  include <dwrite.h>
#  include <process.h>
#  include <windows.h>

#  include "WMFDecoderModule.h"
#  include "gfxDWriteFonts.h"
#  include "gfxWindowsPlatform.h"
#  include "mozilla/gfx/DeviceManagerDx.h"
#  include "mozilla/layers/GpuProcessD3D11TextureMap.h"
#  include "mozilla/layers/TextureD3D11.h"
#  include "mozilla/widget/WinCompositorWindowThread.h"
#else
#  include <unistd.h>
#endif
#ifdef MOZ_WIDGET_GTK
#  include <gtk/gtk.h>

#  include "skia/include/ports/SkTypeface_cairo.h"
#endif
#ifdef ANDROID
#  include "mozilla/layers/AndroidHardwareBuffer.h"
#  include "mozilla/layers/AndroidImageReader.h"
#  include "skia/include/ports/SkTypeface_cairo.h"
#endif
#include "ChildProfilerController.h"
#include "nsAppRunner.h"

#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
#  include "mozilla/SandboxTestingChild.h"
#endif

namespace mozilla::gfx {

using namespace ipc;
using namespace layers;

static GPUParent* sGPUParent;

GPUParent::GPUParent() : mLaunchTime(TimeStamp::Now()) { sGPUParent = this; }

GPUParent::~GPUParent() { sGPUParent = nullptr; }

/* static */
GPUParent* GPUParent::GetSingleton() {
  MOZ_DIAGNOSTIC_ASSERT(sGPUParent);
  return sGPUParent;
}

/* static */ bool GPUParent::MaybeFlushMemory() {
#if defined(XP_WIN) && !defined(HAVE_64BIT_BUILD)
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  if (!XRE_IsGPUProcess()) {
    return false;
  }

  MEMORYSTATUSEX stat;
  stat.dwLength = sizeof(stat);
  if (!GlobalMemoryStatusEx(&stat)) {
    return false;
  }

  // We only care about virtual process memory space in the GPU process because
  // the UI process is already watching total memory usage.
  static const size_t kLowVirtualMemoryThreshold = 384 * 1024 * 1024;
  bool lowMemory = stat.ullAvailVirtual < kLowVirtualMemoryThreshold;

  // We suppress more than one low memory notification until we exit the
  // condition. The UI process goes through more effort, reporting on-going
  // memory pressure, but rather than try to manage a shared state, we just
  // send one notification here to try to resolve it.
  static bool sLowMemory = false;
  if (lowMemory && !sLowMemory) {
    NS_DispatchToMainThread(
        NS_NewRunnableFunction("gfx::GPUParent::FlushMemory", []() -> void {
          (void)GPUParent::GetSingleton()->SendFlushMemory(u"low-memory"_ns);
        }));
  }
  sLowMemory = lowMemory;
  return lowMemory;
#else
  return false;
#endif
}

bool GPUParent::Init(mozilla::ipc::UntypedEndpoint&& aEndpoint,
                     const char* aParentBuildID) {
  // Initialize the thread manager before starting IPC. Otherwise, messages
  // may be posted to the main thread and we won't be able to process them.
  if (NS_WARN_IF(NS_FAILED(nsThreadManager::get().Init()))) {
    return false;
  }

  // Now it's safe to start IPC.
  if (NS_WARN_IF(!aEndpoint.Bind(this))) {
    return false;
  }

  nsDebugImpl::SetMultiprocessMode("GPU");

  // This must be checked before any IPDL message, which may hit sentinel
  // errors due to parent and content processes having different
  // versions.
  MessageChannel* channel = GetIPCChannel();
  if (channel && !channel->SendBuildIDsMatchMessage(aParentBuildID)) {
    // We need to quit this process if the buildID doesn't match the parent's.
    // This can occur when an update occurred in the background.
    ProcessChild::QuickExit();
  }

  if (NS_FAILED(NS_InitMinimalXPCOM())) {
    return false;
  }

  // Ensure the observer service exists.
  ProcessPriorityManager::Init();

  // Init crash reporter support.
  CrashReporterClient::InitSingleton(this);

  gfxConfig::Init();
  gfxVars::Initialize();
  gfxPlatform::InitNullMetadata();
  // Ensure our Factory is initialised, mainly for gfx logging to work.
  gfxPlatform::InitMoz2DLogging();
#if defined(XP_WIN)
  gfxWindowsPlatform::InitMemoryReportersForGPUProcess();
  DeviceManagerDx::Init();
  GpuProcessD3D11TextureMap::Init();
  auto rv = wmf::MediaFoundationInitializer::HasInitialized();
  if (!rv) {
    NS_WARNING("Failed to init Media Foundation in the GPU process");
  }
#endif

#if defined(XP_WIN) || defined(XP_MACOSX)
  CompositeProcessFencesHolderMap::Init();
#endif

  CompositorThreadHolder::Start();
  RemoteTextureMap::Init();
  APZThreadUtils::SetControllerThread(NS_GetCurrentThread());
  apz::InitializeGlobalState();
  LayerTreeOwnerTracker::Initialize();
  CompositorBridgeParent::InitializeStatics();

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  // On macOS, we pass the empty string for the process name because
  // the bundle name (CFBundleName) is the complete name already.
  // If the sandbox is enabled, setting the executable name will fail
  // so don't attempt it. The executable name will be shown in
  // Activity Monitor as the process name.
  if (!IsGPUSandboxEnabled()) {
    mozilla::ipc::SetThisProcessName("");
  }
#elif defined(XP_MACOSX)
  mozilla::ipc::SetThisProcessName("");
#else
  mozilla::ipc::SetThisProcessName("GPU Process");
#endif  // XP_MACOSX && MOZ_SANDBOX

  return true;
}

void GPUParent::NotifyDeviceReset(DeviceResetReason aReason,
                                  DeviceResetDetectPlace aPlace) {
  GPUPROCESS_LOG("Device reset detected, reason {} place {}",
                 fmt::enums::format_as(aReason), fmt::enums::format_as(aPlace));
  // Notify the main process that there's been a device reset
  // and that they should reset their compositors and repaint
  GPUDeviceData data;
  RecvGetDeviceStatus(&data);
  (void)SendNotifyDeviceReset(data, aReason, aPlace);
}

void GPUParent::NotifyOverlayInfo(layers::OverlayInfo aInfo) {
  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "gfx::GPUParent::NotifyOverlayInfo", [aInfo]() -> void {
          GPUParent::GetSingleton()->NotifyOverlayInfo(aInfo);
        }));
    return;
  }
  (void)SendNotifyOverlayInfo(aInfo);
}

void GPUParent::NotifySwapChainInfo(layers::SwapChainInfo aInfo) {
  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "gfx::GPUParent::NotifySwapChainInfo", [aInfo]() -> void {
          GPUParent::GetSingleton()->NotifySwapChainInfo(aInfo);
        }));
    return;
  }
  (void)SendNotifySwapChainInfo(aInfo);
}

void GPUParent::NotifyDisableRemoteCanvas() {
  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "gfx::GPUParent::NotifyDisableRemoteCanvas", []() -> void {
          GPUParent::GetSingleton()->NotifyDisableRemoteCanvas();
        }));
    return;
  }
  (void)SendNotifyDisableRemoteCanvas();
}

void GPUParent::ReportGLStrings(GfxInfoGLStrings&& aStrings) {
  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "gfx::GPUParent::ReportGLStrings",
        [strings = std::move(aStrings)]() mutable -> void {
          GPUParent::GetSingleton()->ReportGLStrings(std::move(strings));
        }));
    return;
  }
  (void)SendReportGLStrings(std::move(aStrings));
}

mozilla::ipc::IPCResult GPUParent::RecvInit(
    nsTArray<GfxVarUpdate>&& vars, const DevicePrefs& devicePrefs,
    nsTArray<LayerTreeIdMapping>&& aMappings,
    nsTArray<GfxInfoFeatureStatus>&& aFeatures, uint32_t aWrNamespace,
    InitResolver&& aInitResolver) {
  gfxVars::ApplyUpdate(vars);

  // Inherit device preferences.
  gfxConfig::Inherit(Feature::HW_COMPOSITING, devicePrefs.hwCompositing());
  gfxConfig::Inherit(Feature::D3D11_COMPOSITING,
                     devicePrefs.d3d11Compositing());
  gfxConfig::Inherit(Feature::OPENGL_COMPOSITING, devicePrefs.oglCompositing());
  gfxConfig::Inherit(Feature::D3D11_HW_ANGLE, devicePrefs.d3d11HwAngle());

  {  // Let the crash reporter know if we've got WR enabled or not. For other
    // processes this happens in gfxPlatform::InitWebRenderConfig.
    ScopedGfxFeatureReporter reporter("WR",
                                      gfxPlatform::WebRenderPrefEnabled());
    reporter.SetSuccessful();
  }

  for (const LayerTreeIdMapping& map : aMappings) {
    LayerTreeOwnerTracker::Get()->Map(map.layersId(), map.ownerId());
  }

  widget::GfxInfoBase::SetFeatureStatus(std::move(aFeatures));

  // We bypass gfxPlatform::Init, so we must initialize any relevant libraries
  // here that would normally be initialized there.
  SkGraphics::Init();

  bool useRemoteCanvas = gfxVars::UseAcceleratedCanvas2D();
  if (useRemoteCanvas) {
    gfxGradientCache::Init();
  }

#if defined(XP_WIN)
  if (gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    if (DeviceManagerDx::Get()->CreateCompositorDevices() && useRemoteCanvas) {
      if (DeviceManagerDx::Get()->CreateCanvasDevice()) {
        gfxDWriteFont::InitDWriteSupport();
      } else {
        gfxWarning() << "Failed to create canvas device.";
      }
    }
  }
  DeviceManagerDx::Get()->CreateDirectCompositionDevice();
  // Ensure to initialize GfxInfo
  nsCOMPtr<nsIGfxInfo> gfxInfo = components::GfxInfo::Service();
  (void)gfxInfo;

  Factory::EnsureDWriteFactory();
#endif

#if defined(MOZ_WIDGET_GTK)
  char* display_name = PR_GetEnv("MOZ_GDK_DISPLAY");
  bool waylandEnabled = false;
  if (!display_name) {
#  ifdef MOZ_WAYLAND
    waylandEnabled = IsWaylandEnabled();
#  endif
    if (!waylandEnabled) {
      display_name = PR_GetEnv("DISPLAY");
    }
  }
  if (display_name) {
    int argc = 3;
    char option_name[] = "--display";
    char* argv[] = {// argv0 is unused because g_set_prgname() was called in
                    // XRE_InitChildProcess().
                    nullptr, option_name, display_name, nullptr};
    char** argvp = argv;
    gtk_init(&argc, &argvp);
  } else if (!waylandEnabled) {
    gtk_init(nullptr, nullptr);
  }
  // On Wayland we don't connect to the display. Rendering goes into DMABuf
  // surfaces which the parent process presents, and EGL uses the device or
  // surfaceless platform.

  // Ensure we have an FT library for font instantiation.
  // This would normally be set by gfxPlatform::Init().
  // Since we bypass that, we must do it here instead.
  FT_Library library = Factory::NewFTLibrary();
  MOZ_ASSERT(library);
  Factory::SetFTLibrary(library);

  // true to match gfxPlatform::FontHintingEnabled(). We must hardcode
  // this value because we do not have a gfxPlatform instance.
  SkInitCairoFT(true);

  // Ensure that GfxInfo::Init is called on the main thread.
  nsCOMPtr<nsIGfxInfo> gfxInfo = components::GfxInfo::Service();
  (void)gfxInfo;
#endif

#ifdef ANDROID
  // Ensure we have an FT library for font instantiation.
  // This would normally be set by gfxPlatform::Init().
  // Since we bypass that, we must do it here instead.
  FT_Library library = Factory::NewFTLibrary();
  MOZ_ASSERT(library);
  Factory::SetFTLibrary(library);

  // false to match gfxAndroidPlatform::FontHintingEnabled(). We must
  // hardcode this value because we do not have a gfxPlatform instance.
  SkInitCairoFT(false);

  if (gfxVars::UseAHardwareBufferSharedSurfaceWebglOop()) {
    layers::AndroidHardwareBufferManager::Init();
  }

  if (gfxVars::ForceAImageReaderAndroid()) {
    layers::GpuProcessAndroidImageReaderMap::Init();
  }

#endif

  // Make sure to do this *after* we update gfxVars above.
  wr::RenderThread::Start(aWrNamespace);
  gfx::CanvasRenderThread::Start();
  image::ImageMemoryReporter::InitForWebRender();

  // Since gfxPlatform::Init is never called for the GPU process, ensure that
  // common memory reporters get registered here instead.
  gfxPlatform::InitMemoryReportersForGPUProcess();

  VRManager::ManagerInit();
  // Send a message to the UI process that we're done.
  GPUDeviceData data;
  RecvGetDeviceStatus(&data);
  aInitResolver(data);

  // Dispatch a task to background thread to determine the media codec supported
  // result, and propagate it back to the chrome process on the main thread.
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          "GPUParent::Supported",
          []() {
            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "GPUParent::UpdateMediaCodecsSupported",
                [supported = media::MCSInfo::GetSupportFromFactory()]() {
                  (void)GPUParent::GetSingleton()
                      ->SendUpdateMediaCodecsSupported(supported);
                }));
          }),
      nsIEventTarget::DISPATCH_NORMAL));

  glean::gpu_process::initialization_time.AccumulateRawDuration(
      TimeStamp::Now() - mLaunchTime);
  return IPC_OK();
}

#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
mozilla::ipc::IPCResult GPUParent::RecvInitSandboxTesting(
    Endpoint<PSandboxTestingChild>&& aEndpoint) {
  if (!SandboxTestingChild::Initialize(std::move(aEndpoint))) {
    return IPC_FAIL(
        this, "InitSandboxTesting failed to initialise the child process.");
  }
  return IPC_OK();
}
#endif

mozilla::ipc::IPCResult GPUParent::RecvInitCompositorManager(
    Endpoint<PCompositorManagerParent>&& aEndpoint, uint32_t aNamespace) {
  CompositorManagerParent::Create(std::move(aEndpoint), ContentParentId(),
                                  aNamespace, /* aContentBridgeNamespace */ 0,
                                  /* aIsRoot */ true);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvInitVsyncBridge(
    Endpoint<PVsyncBridgeParent>&& aVsyncEndpoint) {
  mVsyncBridge = VsyncBridgeParent::Start(std::move(aVsyncEndpoint));
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvInitImageBridge(
    Endpoint<PImageBridgeParent>&& aEndpoint, uint32_t aNamespace) {
  ImageBridgeParent::CreateForGPUProcess(std::move(aEndpoint), aNamespace);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvInitVideoBridge(
    Endpoint<PVideoBridgeParent>&& aEndpoint,
    const layers::VideoBridgeSource& aSource) {
  // For GPU decoding, the video bridge would be opened in
  // `VideoBridgeChild::StartupForGPUProcess`.
  MOZ_ASSERT(aSource == layers::VideoBridgeSource::RddProcess ||
             aSource == layers::VideoBridgeSource::MFMediaEngineCDMProcess);
  VideoBridgeParent::Open(std::move(aEndpoint), aSource);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvInitVRManager(
    Endpoint<PVRManagerParent>&& aEndpoint, uint32_t aNamespace) {
  VRManagerParent::CreateForGPUProcess(std::move(aEndpoint), aNamespace);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvInitVR(
    Endpoint<PVRGPUChild>&& aEndpoint) {
  gfx::VRGPUChild::InitForGPUProcess(std::move(aEndpoint));
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvInitUiCompositorController(
    const LayersId& aRootLayerTreeId,
    Endpoint<PUiCompositorControllerParent>&& aEndpoint) {
  UiCompositorControllerParent::Start(aRootLayerTreeId, std::move(aEndpoint));
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvInitAPZBridge(
    const LayersId& aRootLayerTreeId, Endpoint<PAPZBridgeParent>&& aEndpoint) {
  APZBridgeParent::Create(aRootLayerTreeId, std::move(aEndpoint));
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvInitProfiler(
    Endpoint<PProfilerChild>&& aEndpoint) {
  mProfilerController = ChildProfilerController::Create(std::move(aEndpoint));
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvUpdateVar(
    const nsTArray<GfxVarUpdate>& aUpdate) {
  gfxVars::ApplyUpdate(aUpdate);
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          "GPUParent::RecvUpdateVar",
          []() {
#ifdef XP_WIN
            WMFDecoderModule::Init();
            WMFEncoderModule::ClearCache();
#endif
            if (StaticPrefs::media_ffvpx_hw_enabled()) {
              FFVPXRuntimeLinker::Init();
            }
            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "GPUParent::UpdateMediaCodecsSupported",
                [supported = media::MCSInfo::GetSupportFromFactory(
                     true /* force refresh */)]() {
                  if (auto* gpu = GPUParent::GetSingleton()) {
                    (void)gpu->SendUpdateMediaCodecsSupported(supported);
                  }
                }));
          }),
      nsIEventTarget::DISPATCH_NORMAL));
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvPreferenceUpdate(const Pref& aPref) {
  Preferences::SetPreference(aPref);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvScreenInformationChanged() {
#if defined(XP_WIN)
  DeviceManagerDx::Get()->PostUpdateMonitorInfo();
#endif
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvNotifyBatteryInfo(
    const BatteryInformation& aBatteryInfo) {
  wr::RenderThread::Get()->SetBatteryInfo(aBatteryInfo);
  return IPC_OK();
}

static void CopyFeatureChange(Feature aFeature, Maybe<FeatureFailure>* aOut) {
  FeatureState& feature = gfxConfig::GetFeature(aFeature);
  if (feature.DisabledByDefault() || feature.IsEnabled()) {
    // No change:
    //   - Disabled-by-default means the parent process told us not to use this
    //   feature.
    //   - Enabled means we were told to use this feature, and we didn't
    //   discover anything
    //     that would prevent us from doing so.
    *aOut = Nothing();
    return;
  }

  MOZ_ASSERT(!feature.IsEnabled());

  nsCString message;
  message.AssignASCII(feature.GetFailureMessage());

  *aOut =
      Some(FeatureFailure(feature.GetValue(), message, feature.GetFailureId()));
}

mozilla::ipc::IPCResult GPUParent::RecvGetDeviceStatus(GPUDeviceData* aOut) {
  CopyFeatureChange(Feature::D3D11_COMPOSITING, &aOut->d3d11Compositing());
  CopyFeatureChange(Feature::OPENGL_COMPOSITING, &aOut->oglCompositing());

#if defined(XP_WIN)
  if (DeviceManagerDx* dm = DeviceManagerDx::Get()) {
    D3D11DeviceStatus deviceStatus;
    dm->ExportDeviceInfo(&deviceStatus);
    aOut->gpuDevice() = Some(deviceStatus);
  }
#else
  aOut->gpuDevice() = Nothing();
#endif

  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvSimulateDeviceReset() {
#if defined(XP_WIN)
  DeviceManagerDx::Get()->ForceDeviceReset(
      ForcedDeviceResetReason::COMPOSITOR_UPDATED);
#endif
  wr::RenderThread::Get()->SimulateDeviceReset();
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvNewContentCompositorManager(
    Endpoint<PCompositorManagerParent>&& aEndpoint,
    const ContentParentId& aChildId, uint32_t aNamespace,
    uint32_t aContentBridgeNamespace) {
  CompositorManagerParent::Create(std::move(aEndpoint), aChildId, aNamespace,
                                  aContentBridgeNamespace,
                                  /* aIsRoot */ false);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvNewContentImageBridge(
    Endpoint<PImageBridgeParent>&& aEndpoint, const ContentParentId& aChildId,
    uint32_t aNamespace) {
  if (!ImageBridgeParent::CreateForContent(std::move(aEndpoint), aChildId,
                                           aNamespace)) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvNewContentVRManager(
    Endpoint<PVRManagerParent>&& aEndpoint, const ContentParentId& aChildId,
    uint32_t aNamespace) {
  if (!VRManagerParent::CreateForContent(std::move(aEndpoint), aChildId,
                                         aNamespace)) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvNewContentRemoteMediaManager(
    Endpoint<PRemoteMediaManagerParent>&& aEndpoint,
    const ContentParentId& aChildId) {
  if (!RemoteMediaManagerParent::CreateForContent(std::move(aEndpoint),
                                                  aChildId)) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvAddLayerTreeIdMapping(
    const LayerTreeIdMapping& aMapping) {
  LayerTreeOwnerTracker::Get()->Map(aMapping.layersId(), aMapping.ownerId());
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvRemoveLayerTreeIdMapping(
    const LayerTreeIdMapping& aMapping) {
  LayerTreeOwnerTracker::Get()->Unmap(aMapping.layersId(), aMapping.ownerId());
  CompositorBridgeParent::DeallocateLayerTreeId(aMapping.layersId());
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvFlushActiveCheckerboardReports() {
  nsCOMPtr<nsIObserverService> obsSvc = mozilla::services::GetObserverService();
  MOZ_ASSERT(obsSvc);
  if (obsSvc) {
    obsSvc->NotifyObservers(nullptr, "APZ:FlushActiveCheckerboard", nullptr);
  }
  return IPC_OK();
}

/* static */
void GPUParent::GetGPUProcessName(nsACString& aStr) {
  auto processType = XRE_GetProcessType();
  unsigned pid = 0;
  if (processType == GeckoProcessType_GPU) {
    pid = getpid();
  } else {
    MOZ_DIAGNOSTIC_ASSERT(processType == GeckoProcessType_Default);
    pid = GPUProcessManager::Get()->GPUProcessPid();
  }

  nsPrintfCString processName("GPU (pid %u)", pid);
  aStr.Assign(processName);
}

mozilla::ipc::IPCResult GPUParent::RecvRequestMemoryReport(
    const uint32_t& aGeneration, const bool& aAnonymize,
    const bool& aMinimizeMemoryUsage, const Maybe<FileDescriptor>& aDMDFile,
    const RequestMemoryReportResolver& aResolver) {
  nsAutoCString processName;
  GetGPUProcessName(processName);

  mozilla::dom::MemoryReportRequestClient::Start(
      aGeneration, aAnonymize, aMinimizeMemoryUsage, aDMDFile, processName,
      [&](const MemoryReport& aReport) {
        (void)GetSingleton()->SendAddMemoryReport(aReport);
      },
      aResolver);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvShutdownVR() {
  if (StaticPrefs::dom_vr_process_enabled_AtStartup() &&
      StaticPrefs::dom_vr_enabled()) {
    VRGPUChild::Shutdown();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvUpdatePerfStatsCollectionMask(
    const uint64_t& aMask) {
  PerfStats::SetCollectionMask(static_cast<PerfStats::MetricMask>(aMask));
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvCollectPerfStatsJSON(
    CollectPerfStatsJSONResolver&& aResolver) {
  aResolver(PerfStats::CollectLocalPerfStatsJSON());
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvFlushFOGData(
    FlushFOGDataResolver&& aResolver) {
  glean::FlushFOGData(std::move(aResolver));
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvTestTriggerMetrics(
    TestTriggerMetricsResolver&& aResolve) {
  mozilla::glean::test_only_ipc::a_counter.Add(nsIXULRuntime::PROCESS_TYPE_GPU);
  aResolve(true);
  return IPC_OK();
}

mozilla::ipc::IPCResult GPUParent::RecvCrashProcess() {
  MOZ_CRASH("Deliberate GPU process crash");
  return IPC_OK();
}

void GPUParent::ActorDestroy(ActorDestroyReason aWhy) {
  if (AbnormalShutdown == aWhy) {
    NS_WARNING("Shutting down GPU process early due to a crash!");
    ProcessChild::QuickExit();
  }

  // Send the last bits of Glean data over to the main process.
  glean::FlushFOGData(
      [](ByteBuf&& aBuf) { glean::SendFOGData(std::move(aBuf)); });

#ifndef NS_FREE_PERMANENT_DATA
  // No point in going through XPCOM shutdown because we don't keep persistent
  // state.
  ProcessChild::QuickExit();
#endif

  // Wait until all RemoteMediaManagerParent have closed.
  mShutdownBlockers.WaitUntilClear(10 * 1000 /* 10s timeout*/)
      ->Then(GetCurrentSerialEventTarget(), __func__, [self = RefPtr{this}]() {
        if (self->mProfilerController) {
          self->mProfilerController->Shutdown();
          self->mProfilerController = nullptr;
        }

        if (self->mVsyncBridge) {
          self->mVsyncBridge->Shutdown();
          self->mVsyncBridge = nullptr;
        }
        VideoBridgeParent::Shutdown();
        // This could be running on either the Compositor thread, the Renderer
        // thread, or the dedicated CanvasRender thread, so we need to shutdown
        // before the former two.
        CanvasRenderThread::Shutdown();
        CompositorThreadHolder::Shutdown();
        RemoteTextureMap::Shutdown();
#ifdef ANDROID
        layers::GpuProcessAndroidImageReaderMap::Shutdown();
#endif
        // There is a case that RenderThread exists when gfxVars::UseWebRender()
        // is false. This could happen when WebRender was fallbacked to
        // compositor.
        if (wr::RenderThread::Get()) {
          wr::RenderThread::ShutDown();
        }
#ifdef XP_WIN
        if (widget::WinCompositorWindowThread::Get()) {
          widget::WinCompositorWindowThread::ShutDown();
        }
#endif

        image::ImageMemoryReporter::ShutdownForWebRender();

        // Shut down the default GL context provider.
        gl::GLContextProvider::Shutdown();

#if defined(XP_WIN) || defined(XP_MACOSX)
        // The above shutdown call shuts down the default context provider,
        // which is the only context provider on most platforms. Windows and
        // Mac, however, may initialize EGL for ANGLE in addition to their
        // respective defaults.
        gl::GLContextProviderEGL::Shutdown();
#endif

        Factory::ShutDown();

    // We bypass gfxPlatform shutdown, so we must shutdown any libraries here
    // that would normally be handled by it.
#ifdef NS_FREE_PERMANENT_DATA
        SkGraphics::PurgeFontCache();
        cairo_debug_reset_static_data();
#endif

#if defined(XP_WIN)
        GpuProcessD3D11TextureMap::Shutdown();
        DeviceManagerDx::Shutdown();
#endif

#if defined(XP_WIN) || defined(XP_MACOSX)
        CompositeProcessFencesHolderMap::Shutdown();
#endif

        LayerTreeOwnerTracker::Shutdown();
        gfxVars::Shutdown();
        gfxConfig::Shutdown();
        CrashReporterClient::DestroySingleton();
        XRE_ShutdownChildProcess();
      });
}

}  // namespace mozilla::gfx
