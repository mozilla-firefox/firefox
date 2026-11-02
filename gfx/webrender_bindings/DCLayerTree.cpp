/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DCLayerTree.h"

// -

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dcomp.h>
#include <dxgi1_2.h>

// -

#include "GLContext.h"
#include "GLContextEGL.h"
#include "WinUtils.h"
#include "gfxWindowsPlatform.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/WindowsVersion.h"
#include "mozilla/gfx/DeviceManagerDx.h"
#include "mozilla/gfx/GPUParent.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/Matrix.h"
#include "mozilla/gfx/StackArray.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/layers/CompositeProcessFencesHolderMap.h"
#include "mozilla/layers/FenceD3D11.h"
#include "mozilla/webrender/RenderD3D11TextureHost.h"
#include "mozilla/webrender/RenderDcompSurfaceTextureHost.h"
#include "mozilla/webrender/RenderTextureHost.h"
#include "mozilla/webrender/RenderThread.h"
#include "nsPrintfCString.h"

// -

namespace mozilla {
namespace wr {

extern LazyLogModule gRenderThreadLog;
#define LOG(...) MOZ_LOG(gRenderThreadLog, LogLevel::Debug, (__VA_ARGS__))

#define LOG_H(msg, ...)                   \
  MOZ_LOG(gDcompSurface, LogLevel::Debug, \
          ("DCSurfaceHandle=%p, " msg, this, ##__VA_ARGS__))

static UINT GetVendorId(ID3D11VideoDevice* const aVideoDevice) {
  RefPtr<IDXGIDevice> dxgiDevice;
  RefPtr<IDXGIAdapter> adapter;
  aVideoDevice->QueryInterface((IDXGIDevice**)getter_AddRefs(dxgiDevice));
  dxgiDevice->GetAdapter(getter_AddRefs(adapter));

  DXGI_ADAPTER_DESC adapterDesc;
  adapter->GetDesc(&adapterDesc);

  return adapterDesc.VendorId;
}

// Undocumented NVIDIA VSR data
struct NvidiaVSRGetData_v1 {
  UINT vsrGPUisVSRCapable : 1;   // 01/32, 1: GPU is VSR capable
  UINT vsrOtherFieldsValid : 1;  // 02/32, 1: Other status fields are valid
  // remaining fields are valid if vsrOtherFieldsValid is set - requires
  // previous execution of VPBlt with SetStreamExtension for VSR enabled.
  UINT vsrEnabled : 1;           // 03/32, 1: VSR is enabled
  UINT vsrIsInUseForThisVP : 1;  // 04/32, 1: VSR is in use by this Video
                                 // Processor
  UINT vsrLevel : 3;             // 05-07/32, 0-4 current level
  UINT vsrReserved : 21;         // 32-07
};

static Result<NvidiaVSRGetData_v1, HRESULT> GetNvidiaVpSuperResolutionInfo(
    ID3D11VideoContext* aVideoContext, ID3D11VideoProcessor* aVideoProcessor) {
  MOZ_ASSERT(aVideoContext);
  MOZ_ASSERT(aVideoProcessor);

  // Undocumented NVIDIA driver constants
  constexpr GUID nvGUID = {0xD43CE1B3,
                           0x1F4B,
                           0x48AC,
                           {0xBA, 0xEE, 0xC3, 0xC2, 0x53, 0x75, 0xE6, 0xF7}};

  NvidiaVSRGetData_v1 data{};
  HRESULT hr = aVideoContext->VideoProcessorGetStreamExtension(
      aVideoProcessor, 0, &nvGUID, sizeof(data), &data);

  if (FAILED(hr)) {
    return Err(hr);
  }
  return data;
}

static void AddProfileMarkerForNvidiaVpSuperResolutionInfo(
    ID3D11VideoContext* aVideoContext, ID3D11VideoProcessor* aVideoProcessor) {
  MOZ_ASSERT(profiler_thread_is_being_profiled_for_markers());

  auto res = GetNvidiaVpSuperResolutionInfo(aVideoContext, aVideoProcessor);
  if (res.isErr()) {
    return;
  }

  auto data = res.unwrap();

  nsPrintfCString str(
      "SuperResolution VP Capable %u OtherFieldsValid %u Enabled %u InUse %u "
      "Level %u",
      data.vsrGPUisVSRCapable, data.vsrOtherFieldsValid, data.vsrEnabled,
      data.vsrIsInUseForThisVP, data.vsrLevel);
  PROFILER_MARKER_TEXT("DCSurfaceVideo", GRAPHICS, {}, str);
}

static HRESULT SetNvidiaVpSuperResolution(ID3D11VideoContext* aVideoContext,
                                          ID3D11VideoProcessor* aVideoProcessor,
                                          bool aEnable) {
  LOG("SetNvidiaVpSuperResolution() aEnable=%d", aEnable);

  // Undocumented NVIDIA driver constants
  constexpr GUID nvGUID = {0xD43CE1B3,
                           0x1F4B,
                           0x48AC,
                           {0xBA, 0xEE, 0xC3, 0xC2, 0x53, 0x75, 0xE6, 0xF7}};

  constexpr UINT nvExtensionVersion = 0x1;
  constexpr UINT nvExtensionMethodSuperResolution = 0x2;
  struct {
    UINT version;
    UINT method;
    UINT enable;
  } streamExtensionInfo = {nvExtensionVersion, nvExtensionMethodSuperResolution,
                           aEnable ? 1u : 0};

  HRESULT hr;
  hr = aVideoContext->VideoProcessorSetStreamExtension(
      aVideoProcessor, 0, &nvGUID, sizeof(streamExtensionInfo),
      &streamExtensionInfo);
  return hr;
}

static HRESULT SetVpSuperResolution(UINT aGpuVendorId,
                                    ID3D11VideoContext* aVideoContext,
                                    ID3D11VideoProcessor* aVideoProcessor,
                                    bool aEnable) {
  MOZ_ASSERT(aVideoContext);
  MOZ_ASSERT(aVideoProcessor);

  if (aGpuVendorId == 0x10DE) {
    return SetNvidiaVpSuperResolution(aVideoContext, aVideoProcessor, aEnable);
  }
  return E_NOTIMPL;
}

static bool GetNvidiaRTXVideoTrueHDRSupported(
    ID3D11VideoContext* aVideoContext, ID3D11VideoProcessor* aVideoProcessor) {
  const GUID kNvidiaTrueHDRInterfaceGUID = {
      0xfdd62bb4,
      0x620b,
      0x4fd7,
      {0x9a, 0xb3, 0x1e, 0x59, 0xd0, 0xd5, 0x44, 0xb3}};
  UINT available = 0;
  HRESULT hr = aVideoContext->VideoProcessorGetStreamExtension(
      aVideoProcessor, 0, &kNvidiaTrueHDRInterfaceGUID, sizeof(available),
      &available);
  if (FAILED(hr)) {
    return false;
  }

  bool driverSupportsTrueHdr = (available == 1);
  return driverSupportsTrueHdr;
}

static HRESULT SetNvidiaRTXVideoTrueHDR(ID3D11VideoContext* aVideoContext,
                                        ID3D11VideoProcessor* aVideoProcessor,
                                        bool aEnable) {
  constexpr GUID kNvidiaTrueHDRInterfaceGUID = {
      0xfdd62bb4,
      0x620b,
      0x4fd7,
      {0x9a, 0xb3, 0x1e, 0x59, 0xd0, 0xd5, 0x44, 0xb3}};
  constexpr UINT kStreamExtensionMethodTrueHDR = 0x3;
  const UINT TrueHDRVersion4 = 4;
  struct {
    UINT version;
    UINT method;
    UINT enable : 1;
    UINT reserved : 31;
  } streamExtensionInfo = {TrueHDRVersion4, kStreamExtensionMethodTrueHDR,
                           aEnable ? 1u : 0u, 0u};
  HRESULT hr = aVideoContext->VideoProcessorSetStreamExtension(
      aVideoProcessor, 0, &kNvidiaTrueHDRInterfaceGUID,
      sizeof(streamExtensionInfo), &streamExtensionInfo);
  return hr;
}

static bool GetVpAutoHDRSupported(UINT aGpuVendorId,
                                  ID3D11VideoContext* aVideoContext,
                                  ID3D11VideoProcessor* aVideoProcessor) {
  MOZ_ASSERT(aVideoContext);
  MOZ_ASSERT(aVideoProcessor);

  if (aGpuVendorId == 0x10DE) {
    return GetNvidiaRTXVideoTrueHDRSupported(aVideoContext, aVideoProcessor);
  }
  return false;
}

static HRESULT SetVpAutoHDR(UINT aGpuVendorId,
                            ID3D11VideoContext* aVideoContext,
                            ID3D11VideoProcessor* aVideoProcessor,
                            bool aEnable) {
  MOZ_ASSERT(aVideoContext);
  MOZ_ASSERT(aVideoProcessor);

  if (aGpuVendorId == 0x10DE) {
    return SetNvidiaRTXVideoTrueHDR(aVideoContext, aVideoProcessor, aEnable);
  }
  MOZ_ASSERT_UNREACHABLE("Unexpected to be called");
  return E_NOTIMPL;
}

StaticAutoPtr<GpuOverlayInfo> DCLayerTree::sGpuOverlayInfo;

/* static */
UniquePtr<DCLayerTree> DCLayerTree::Create(gl::GLContext* aGL,
                                           EGLConfig aEGLConfig,
                                           ID3D11Device* aDevice,
                                           ID3D11DeviceContext* aCtx,
                                           HWND aHwnd, nsACString& aError) {
  RefPtr<IDCompositionDevice2> dCompDevice =
      gfx::DeviceManagerDx::Get()->GetDirectCompositionDevice();
  if (!dCompDevice) {
    aError.Assign("DCLayerTree(no device)"_ns);
    return nullptr;
  }

  auto layerTree = std::make_unique<DCLayerTree>(aGL, aEGLConfig, aDevice, aCtx,
                                                 aHwnd, dCompDevice);
  if (!layerTree->Initialize(aHwnd, aError)) {
    return nullptr;
  }

  return layerTree;
}

void DCLayerTree::Shutdown() { DCLayerTree::sGpuOverlayInfo = nullptr; }

DCLayerTree::DCLayerTree(gl::GLContext* aGL, EGLConfig aEGLConfig,
                         ID3D11Device* aDevice, ID3D11DeviceContext* aCtx,
                         HWND aHwnd, IDCompositionDevice2* aCompositionDevice)
    : mGL(aGL),
      mEGLConfig(aEGLConfig),
      mDevice(aDevice),
      mCtx(aCtx),
      mHwnd(aHwnd),
      mCompositionDevice(aCompositionDevice),
      mDebugCounter(false),
      mDebugVisualRedrawRegions(false),
      mEGLImage(EGL_NO_IMAGE),
      mColorRBO(0),
      mPendingCommit(false) {
  LOG("DCLayerTree::DCLayerTree()");

  if (gfx::gfxVars::UseWebRenderCompositor()) {
    MOZ_ASSERT(StaticPrefs::gfx_webrender_layer_compositor());
    mUseLayerCompositor = true;
  }
}

DCLayerTree::~DCLayerTree() {
  LOG("DCLayerTree::~DCLayerTree()");

  ReleaseNativeCompositorResources();
}

void DCLayerTree::ReleaseNativeCompositorResources() {
  const auto gl = GetGLContext();

  DestroyEGLSurface();

  // Delete any cached FBO objects
  for (auto it = mFrameBuffers.begin(); it != mFrameBuffers.end(); ++it) {
    gl->fDeleteRenderbuffers(1, &it->depthRboId);
    gl->fDeleteFramebuffers(1, &it->fboId);
  }
}

bool DCLayerTree::Initialize(HWND aHwnd, nsACString& aError) {
  HRESULT hr;

  RefPtr<IDCompositionDesktopDevice> desktopDevice;
  hr = mCompositionDevice->QueryInterface(
      (IDCompositionDesktopDevice**)getter_AddRefs(desktopDevice));
  if (FAILED(hr)) {
    aError.Assign(nsPrintfCString(
        "DCLayerTree(get IDCompositionDesktopDevice failed %lx)", hr));
    return false;
  }

  hr = desktopDevice->CreateTargetForHwnd(aHwnd, TRUE,
                                          getter_AddRefs(mCompositionTarget));
  if (FAILED(hr)) {
    aError.Assign(nsPrintfCString(
        "DCLayerTree(create DCompositionTarget failed %lx)", hr));
    return false;
  }

  hr = mCompositionDevice->CreateVisual(getter_AddRefs(mRootVisual));
  if (FAILED(hr)) {
    aError.Assign(nsPrintfCString(
        "DCLayerTree(create root DCompositionVisual failed %lx)", hr));
    return false;
  }

  hr =
      mCompositionDevice->CreateVisual(getter_AddRefs(mDefaultSwapChainVisual));
  if (FAILED(hr)) {
    aError.Assign(nsPrintfCString(
        "DCLayerTree(create swap chain DCompositionVisual failed %lx)", hr));
    return false;
  }

  if (gfx::gfxVars::UseWebRenderDCompVideoHwOverlayWin() ||
      gfx::gfxVars::UseWebRenderDCompVideoSwOverlayWin()) {
    if (!InitializeVideoOverlaySupport()) {
      RenderThread::Get()->NotifyWebRenderError(WebRenderError::VIDEO_OVERLAY);
    }
  }
  if (!sGpuOverlayInfo) {
    // Set default if sGpuOverlayInfo was not set.
    sGpuOverlayInfo = new GpuOverlayInfo();
  }

  // Initialize SwapChainInfo
  SupportsSwapChainTearing();

  mCompositionTarget->SetRoot(mRootVisual);
  // Set interporation mode to nearest, to ensure 1:1 sampling.
  // By default, a visual inherits the interpolation mode of the parent visual.
  // If no visuals set the interpolation mode, the default for the entire visual
  // tree is nearest neighbor interpolation.
  mRootVisual->SetBitmapInterpolationMode(
      DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
  return true;
}

bool FlagsSupportsOverlays(UINT flags) {
  return (flags & (DXGI_OVERLAY_SUPPORT_FLAG_DIRECT |
                   DXGI_OVERLAY_SUPPORT_FLAG_SCALING));
}

// A warpper of IDXGIOutput4::CheckOverlayColorSpaceSupport()
bool CheckOverlayColorSpaceSupport(DXGI_FORMAT aDxgiFormat,
                                   DXGI_COLOR_SPACE_TYPE aDxgiColorSpace,
                                   RefPtr<IDXGIOutput> aOutput,
                                   RefPtr<ID3D11Device> aD3d11Device) {
  UINT colorSpaceSupportFlags = 0;
  RefPtr<IDXGIOutput4> output4;

  if (FAILED(aOutput->QueryInterface(__uuidof(IDXGIOutput4),
                                     getter_AddRefs(output4)))) {
    return false;
  }

  if (FAILED(output4->CheckOverlayColorSpaceSupport(
          aDxgiFormat, aDxgiColorSpace, aD3d11Device,
          &colorSpaceSupportFlags))) {
    return false;
  }

  return (colorSpaceSupportFlags &
          DXGI_OVERLAY_COLOR_SPACE_SUPPORT_FLAG_PRESENT);
}

bool DCLayerTree::InitializeVideoOverlaySupport() {
  MOZ_ASSERT(IsWin10AnniversaryUpdateOrLater());

  HRESULT hr;

  hr = mDevice->QueryInterface(
      (ID3D11VideoDevice**)getter_AddRefs(mVideoDevice));
  if (FAILED(hr)) {
    gfxCriticalNote << "Failed to get D3D11VideoDevice: " << gfx::hexa(hr);
    return false;
  }

  hr =
      mCtx->QueryInterface((ID3D11VideoContext**)getter_AddRefs(mVideoContext));
  if (FAILED(hr)) {
    gfxCriticalNote << "Failed to get D3D11VideoContext: " << gfx::hexa(hr);
    return false;
  }

  if (sGpuOverlayInfo) {
    return true;
  }

  auto info = std::make_unique<GpuOverlayInfo>();

  RefPtr<IDXGIDevice> dxgiDevice;
  RefPtr<IDXGIAdapter> adapter;
  mDevice->QueryInterface((IDXGIDevice**)getter_AddRefs(dxgiDevice));
  dxgiDevice->GetAdapter(getter_AddRefs(adapter));

  unsigned int i = 0;
  while (true) {
    RefPtr<IDXGIOutput> output;
    if (FAILED(adapter->EnumOutputs(i++, getter_AddRefs(output)))) {
      break;
    }
    RefPtr<IDXGIOutput3> output3;
    if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput3),
                                      getter_AddRefs(output3)))) {
      break;
    }

    output3->CheckOverlaySupport(DXGI_FORMAT_NV12, mDevice,
                                 &info->mNv12OverlaySupportFlags);
    output3->CheckOverlaySupport(DXGI_FORMAT_YUY2, mDevice,
                                 &info->mYuy2OverlaySupportFlags);
    output3->CheckOverlaySupport(DXGI_FORMAT_B8G8R8A8_UNORM, mDevice,
                                 &info->mBgra8OverlaySupportFlags);
    output3->CheckOverlaySupport(DXGI_FORMAT_R10G10B10A2_UNORM, mDevice,
                                 &info->mRgb10a2OverlaySupportFlags);
    output3->CheckOverlaySupport(DXGI_FORMAT_R16G16B16A16_FLOAT, mDevice,
                                 &info->mRgba16fOverlaySupportFlags);

    if (FlagsSupportsOverlays(info->mRgb10a2OverlaySupportFlags)) {
      info->mSupportsHDR = true;
      info->mSupportsHardwareOverlayRGB10A2 = true;
    }

    if (FlagsSupportsOverlays(info->mRgba16fOverlaySupportFlags)) {
      info->mSupportsHDR = true;
      info->mSupportsHardwareOverlayRGBA16F = true;
    }

    if (!info->mSupportsHardwareOverlays &&
        FlagsSupportsOverlays(info->mNv12OverlaySupportFlags)) {
      // NV12 format is preferred if it's supported.
      info->mOverlayFormatUsed = DXGI_FORMAT_NV12;
      info->mSupportsHardwareOverlays = true;
    }

    if (!info->mSupportsHardwareOverlays &&
        FlagsSupportsOverlays(info->mYuy2OverlaySupportFlags)) {
      // If NV12 isn't supported, fallback to YUY2 if it's supported.
      info->mOverlayFormatUsed = DXGI_FORMAT_YUY2;
      info->mSupportsHardwareOverlays = true;
    }

    // Early out after the first output that reports overlay support. All
    // outputs are expected to report the same overlay support according to
    // Microsoft's WDDM documentation:
    // https://docs.microsoft.com/en-us/windows-hardware/drivers/display/multiplane-overlay-hardware-requirements
    if (info->mSupportsHardwareOverlays) {
      break;
    }
  }

  if (!StaticPrefs::gfx_webrender_dcomp_video_yuv_overlay_win_AtStartup()) {
    info->mOverlayFormatUsed = DXGI_FORMAT_B8G8R8A8_UNORM;
    info->mSupportsHardwareOverlays = false;
  }

  info->mSupportsOverlays = info->mSupportsHardwareOverlays;

  // Check VpSuperResolution and VpAutoHDR support.
  const auto size = gfx::IntSize(100, 100);
  if (EnsureVideoProcessor(size, size)) {
    const UINT vendorId = GetVendorId(mVideoDevice);
    if (vendorId == 0x10DE) {
      auto res = GetNvidiaVpSuperResolutionInfo(mVideoContext, mVideoProcessor);
      if (res.isOk() && res.unwrap().vsrGPUisVSRCapable) {
        info->mSupportsVpSuperResolution = true;
      }
    }

    const bool driverSupportVpAutoHDR =
        GetVpAutoHDRSupported(vendorId, mVideoContext, mVideoProcessor);
    if (driverSupportVpAutoHDR) {
      info->mSupportsVpAutoHDR = true;
    }
  }

  // Note: "UniquePtr::release" here is saying "release your ownership stake
  // on your pointer, so that our StaticAutoPtr can take over ownership".
  // (StaticAutoPtr doesn't have a move constructor that could directly steal
  // the contents of a UniquePtr via std::move().)
  sGpuOverlayInfo = info.release();

  if (auto* gpuParent = gfx::GPUParent::GetSingleton()) {
    gpuParent->NotifyOverlayInfo(GetOverlayInfo());
  }

  return true;
}

DCSurface* DCLayerTree::GetSurface(wr::NativeSurfaceId aId) const {
  auto surface_it = mDCSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surface_it != mDCSurfaces.end());
  return surface_it->second.get();
}

void DCLayerTree::SetDefaultSwapChain(IDXGISwapChain1* aSwapChain) {
  LOG("DCLayerTree::SetDefaultSwapChain()");

  mRootVisual->AddVisual(mDefaultSwapChainVisual, TRUE, nullptr);
  mDefaultSwapChainVisual->SetContent(aSwapChain);
  // Default SwapChain's visual does not need linear interporation.
  mDefaultSwapChainVisual->SetBitmapInterpolationMode(
      DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
  mPendingCommit = true;
}

void DCLayerTree::MaybeUpdateDebug() {
  bool updated = false;
  updated |= MaybeUpdateDebugCounter();
  updated |= MaybeUpdateDebugVisualRedrawRegions();
  if (updated) {
    mPendingCommit = true;
  }
}

void DCLayerTree::MaybeCommit() {
  if (!mPendingCommit) {
    return;
  }
  mCompositionDevice->Commit();
  mPendingCommit = false;
}

void DCLayerTree::WaitForCommitCompletion() {
  // To ensure that swapchain layers have presented to the screen
  // for capture, call present twice. This is less than ideal, but
  // I'm not sure if there is a better way to ensure this syncs
  // correctly that works on both Win10/11. Even though this can
  // be slower than necessary, it's only used by the reftest
  // screenshotting code, so isn't particularly perf sensitive.
  bool needsWait = false;
  for (auto it = mDCSurfaces.begin(); it != mDCSurfaces.end(); it++) {
    auto* surface = it->second->AsDCSwapChain();
    if (surface) {
      needsWait = true;
    }
  }

  if (needsWait) {
    RefPtr<IDXGIDevice2> dxgiDevice2;
    mDevice->QueryInterface((IDXGIDevice2**)getter_AddRefs(dxgiDevice2));
    MOZ_ASSERT(dxgiDevice2);

    HANDLE event = ::CreateEvent(nullptr, false, false, nullptr);
    HRESULT hr = dxgiDevice2->EnqueueSetEvent(event);
    if (SUCCEEDED(hr)) {
      DebugOnly<DWORD> result = ::WaitForSingleObject(event, INFINITE);
      MOZ_ASSERT(result == WAIT_OBJECT_0);
    } else {
      gfxCriticalNoteOnce << "EnqueueSetEvent failed: " << gfx::hexa(hr);
    }
    ::CloseHandle(event);
  }

  mCompositionDevice->WaitForCommitCompletion();
}

bool DCLayerTree::UseLayerCompositor() const { return mUseLayerCompositor; }

void DCLayerTree::DisableNativeCompositor() {
  MOZ_ASSERT(mCurrentLayers.empty());

  mUseLayerCompositor = false;
  ReleaseNativeCompositorResources();
  mPrevLayers.clear();
  mRootVisual->RemoveAllVisuals();
}

bool DCLayerTree::EnableAsyncScreenshot() {
  MOZ_ASSERT(UseLayerCompositor());
  if (!UseLayerCompositor()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return false;
  }

  mAsyncScreenshotLastFrameUsed = mCurrentFrame;

  if (!mEnableAsyncScreenshot) {
    mEnableAsyncScreenshotInNextFrame = true;
    return false;
  }

  return true;
}

bool DCLayerTree::MaybeUpdateDebugCounter() {
  bool debugCounter = StaticPrefs::gfx_webrender_debug_dcomp_counter();
  if (mDebugCounter == debugCounter) {
    return false;
  }

  RefPtr<IDCompositionDeviceDebug> debugDevice;
  HRESULT hr = mCompositionDevice->QueryInterface(
      (IDCompositionDeviceDebug**)getter_AddRefs(debugDevice));
  if (FAILED(hr)) {
    return false;
  }

  if (debugCounter) {
    debugDevice->EnableDebugCounters();
  } else {
    debugDevice->DisableDebugCounters();
  }

  mDebugCounter = debugCounter;
  return true;
}

bool DCLayerTree::MaybeUpdateDebugVisualRedrawRegions() {
  bool debugVisualRedrawRegions =
      StaticPrefs::gfx_webrender_debug_dcomp_redraw_regions();
  if (mDebugVisualRedrawRegions == debugVisualRedrawRegions) {
    return false;
  }

  RefPtr<IDCompositionVisualDebug> visualDebug;
  HRESULT hr = mRootVisual->QueryInterface(
      (IDCompositionVisualDebug**)getter_AddRefs(visualDebug));
  if (FAILED(hr)) {
    return false;
  }

  if (debugVisualRedrawRegions) {
    visualDebug->EnableRedrawRegions();
  } else {
    visualDebug->DisableRedrawRegions();
  }

  mDebugVisualRedrawRegions = debugVisualRedrawRegions;
  return true;
}

void DCLayerTree::CompositorBeginFrame() {
  mCurrentFrame++;
  mUsedOverlayTypesInFrame = DCompOverlayTypes::NO_OVERLAY;
  if (mEnableAsyncScreenshotInNextFrame) {
    mEnableAsyncScreenshot = true;
    mEnableAsyncScreenshotInNextFrame = false;
  }
}

void DCLayerTree::CompositorEndFrame() {
  auto start = TimeStamp::Now();
  // Check if the visual tree of surfaces is the same as last frame.
  const bool same = mPrevLayers == mCurrentLayers;

  if (!same) {
    // If not, we need to rebuild the visual tree.
    mRootVisual->RemoveAllVisuals();
  }

  for (auto it = mCurrentLayers.begin(); it != mCurrentLayers.end(); ++it) {
    auto surface_it = mDCSurfaces.find(*it);
    MOZ_RELEASE_ASSERT(surface_it != mDCSurfaces.end());
    const auto surface = surface_it->second.get();
    if (!same) {
      // WebRender adds surfaces back to front.
      mRootVisual->AddVisual(surface->GetRootVisual(), false, nullptr);
    }
  }

  mPrevLayers.swap(mCurrentLayers);
  mCurrentLayers.clear();

  if (!same) {
    mPendingCommit = true;
  }

  MaybeCommit();

  auto end = TimeStamp::Now();
  mozilla::glean::gfx::composite_swap_time.AccumulateSingleSample(
      (end - start).ToMilliseconds() * 10.);

  // Remove any framebuffers that haven't been
  // used in the last 60 frames.
  //
  // This should use nsTArray::RemoveElementsBy once
  // CachedFrameBuffer is able to properly destroy
  // itself in the destructor.
  const auto gl = GetGLContext();
  for (uint32_t i = 0, len = mFrameBuffers.Length(); i < len; ++i) {
    auto& fb = mFrameBuffers[i];
    if ((mCurrentFrame - fb.lastFrameUsed) > 60) {
      gl->fDeleteRenderbuffers(1, &fb.depthRboId);
      gl->fDeleteFramebuffers(1, &fb.fboId);
      mFrameBuffers.UnorderedRemoveElementAt(i);
      --i;  // Examine the element again, if necessary.
      --len;
    }
  }

  if (mEnableAsyncScreenshot &&
      (mCurrentFrame - mAsyncScreenshotLastFrameUsed) > 1) {
    mEnableAsyncScreenshot = false;
  }

  if (!StaticPrefs::gfx_webrender_dcomp_video_check_slow_present()) {
    return;
  }

  // Disable video overlay if mCompositionDevice->Commit() with video overlay is
  // too slow. It drops fps.

  const auto commitDurationMs =
      static_cast<uint32_t>((end - start).ToMilliseconds());

  nsPrintfCString marker("CommitWait overlay %u %ums ",
                         (uint8_t)mUsedOverlayTypesInFrame, commitDurationMs);
  PROFILER_MARKER_TEXT("CommitWait", GRAPHICS, {}, marker);

  for (auto it = mDCSurfaces.begin(); it != mDCSurfaces.end(); it++) {
    auto* surfaceVideo = it->second->AsDCSurfaceVideo();
    if (surfaceVideo) {
      surfaceVideo->OnCompositorEndFrame(mCurrentFrame, commitDurationMs);
    }
  }
}

void DCLayerTree::BindSwapChain(wr::NativeSurfaceId aId,
                                const wr::DeviceIntRect* aDirtyRects,
                                size_t aNumDirtyRects) {
  auto surface = GetSurface(aId);
  surface->AsDCLayerSurface()->Bind(aDirtyRects, aNumDirtyRects);
}

void DCLayerTree::PresentSwapChain(wr::NativeSurfaceId aId,
                                   const wr::DeviceIntRect* aDirtyRects,
                                   size_t aNumDirtyRects) {
  auto surface = GetSurface(aId);
  surface->AsDCLayerSurface()->Present(aDirtyRects, aNumDirtyRects);
  if (surface->AsDCLayerDCompositionTexture()) {
    mPendingCommit = true;
  }
}

void DCLayerTree::CreateSwapChainSurface(wr::NativeSurfaceId aId,
                                         wr::DeviceIntSize aSize,
                                         bool aIsOpaque,
                                         bool aNeedsSyncDcompCommit) {
  MOZ_ASSERT_IF(mEnableAsyncScreenshot, !aNeedsSyncDcompCommit);

  auto it = mDCSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(it == mDCSurfaces.end());

  UniquePtr<DCSurface> surface;
  if (UseDCLayerDCompositionTexture()) {
    surface =
        std::make_unique<DCLayerDCompositionTexture>(aSize, aIsOpaque, this);
    if (!surface->Initialize()) {
      gfxCriticalNote << "Failed to initialize DCLayerDCompositionTexture: "
                      << wr::AsUint64(aId);
      RenderThread::Get()->HandleWebRenderError(WebRenderError::NEW_SURFACE);
    }
  } else if (
      !mEnableAsyncScreenshot &&
      (aNeedsSyncDcompCommit ||
       StaticPrefs::
           gfx_webrender_layer_compositor_force_composition_surface_AtStartup())) {
    surface =
        std::make_unique<DCLayerCompositionSurface>(aSize, aIsOpaque, this);
    if (!surface->Initialize()) {
      gfxCriticalNote << "Failed to initialize DCLayerSurface: "
                      << wr::AsUint64(aId);
      RenderThread::Get()->HandleWebRenderError(WebRenderError::NEW_SURFACE);
    }
  } else {
    surface = std::make_unique<DCSwapChain>(aSize, aIsOpaque, this);
    if (!surface->Initialize()) {
      gfxCriticalNote << "Failed to initialize DCSwapChain: "
                      << wr::AsUint64(aId);
      RenderThread::Get()->HandleWebRenderError(WebRenderError::NEW_SURFACE);
    }
  }

  MOZ_ASSERT_IF(mEnableAsyncScreenshot, mDCSurfaces.empty());

  mDCSurfaces[aId] = std::move(surface);
}

void DCLayerTree::ResizeSwapChainSurface(wr::NativeSurfaceId aId,
                                         wr::DeviceIntSize aSize) {
  auto it = mDCSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(it != mDCSurfaces.end());
  auto surface = it->second.get();

  mPendingCommit = true;

  if (!surface->AsDCLayerSurface()->Resize(aSize)) {
    RenderThread::Get()->HandleWebRenderError(WebRenderError::NEW_SURFACE);
  }
}

void DCLayerTree::CreateExternalSurface(wr::NativeSurfaceId aId,
                                        bool aIsOpaque) {
  auto it = mDCSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(it == mDCSurfaces.end());

  auto surface = std::make_unique<DCExternalSurfaceWrapper>(aIsOpaque, this);
  if (!surface->Initialize()) {
    gfxCriticalNote << "Failed to initialize DCExternalSurfaceWrapper: "
                    << wr::AsUint64(aId);
    return;
  }

  mDCSurfaces[aId] = std::move(surface);
}

void DCLayerTree::DestroySurface(NativeSurfaceId aId) {
  auto surface_it = mDCSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surface_it != mDCSurfaces.end());
  auto surface = surface_it->second.get();

  mRootVisual->RemoveVisual(surface->GetRootVisual());
  mDCSurfaces.erase(surface_it);
}

void DCLayerTree::AttachExternalImage(wr::NativeSurfaceId aId,
                                      wr::ExternalImageId aExternalImage) {
  auto surface_it = mDCSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surface_it != mDCSurfaces.end());
  surface_it->second->AttachExternalImage(aExternalImage);
}

void DCExternalSurfaceWrapper::AttachExternalImage(
    wr::ExternalImageId aExternalImage) {
  if (auto* surface = EnsureSurfaceForExternalImage(aExternalImage)) {
    surface->AttachExternalImage(aExternalImage);
  }
}

template <class ToT>
struct QI {
  template <class FromT>
  [[nodiscard]] static inline RefPtr<ToT> From(FromT* const from) {
    RefPtr<ToT> to;
    (void)from->QueryInterface(static_cast<ToT**>(getter_AddRefs(to)));
    return to;
  }
};

DCSurface* DCExternalSurfaceWrapper::EnsureSurfaceForExternalImage(
    wr::ExternalImageId aExternalImage) {
  if (mSurface) {
    return mSurface.get();
  }

  // Create a new surface based on the texture type.
  RenderTextureHost* texture =
      RenderThread::Get()->GetRenderTexture(aExternalImage);
  if (texture && texture->AsRenderDXGITextureHost()) {
    auto format = texture->GetFormat();
    if (format == gfx::SurfaceFormat::B8G8R8A8 ||
        format == gfx::SurfaceFormat::B8G8R8X8) {
      MOZ_ASSERT(RenderDXGITextureHost::UseDCompositionTextureOverlay(format));
      mSurface.reset(
          new DCSurfaceDCompositionTextureOverlay(mIsOpaque, mDCLayerTree));
      if (!mSurface->Initialize()) {
        gfxCriticalNote
            << "Failed to initialize DCSurfaceDCompositionTextureOverlay: "
            << wr::AsUint64(aExternalImage);
        mSurface = nullptr;
      }
    } else {
      mSurface.reset(new DCSurfaceVideo(mIsOpaque, mDCLayerTree));
      if (!mSurface->Initialize()) {
        gfxCriticalNote << "Failed to initialize DCSurfaceVideo: "
                        << wr::AsUint64(aExternalImage);
        mSurface = nullptr;
      }
    }
  } else if (texture && texture->AsRenderDcompSurfaceTextureHost()) {
    mSurface.reset(new DCSurfaceHandle(mIsOpaque, mDCLayerTree));
    if (!mSurface->Initialize()) {
      gfxCriticalNote << "Failed to initialize DCSurfaceHandle: "
                      << wr::AsUint64(aExternalImage);
      mSurface = nullptr;
    }
  }
  if (!mSurface) {
    gfxCriticalNote << "Failed to create a surface for external image: "
                    << gfx::hexa(texture);
    return nullptr;
  }

  // Add surface's visual which will contain video data to our root visual.
  const auto surfaceVisual = mSurface->GetRootVisual();
  mContentVisual->AddVisual(surfaceVisual, true, nullptr);

  // -
  // Apply color management.

  [&]() {
    if (!StaticPrefs::gfx_webrender_dcomp_color_manage_with_filters()) return;

    const auto cmsMode = GfxColorManagementMode();
    if (cmsMode == CMSMode::Off) return;

    const auto dcomp = mDCLayerTree->GetCompositionDevice();
    const auto dcomp3 = QI<IDCompositionDevice3>::From(dcomp);
    if (!dcomp3) {
      NS_WARNING(
          "No IDCompositionDevice3, cannot use dcomp for color management.");
      return;
    }

    // -

    const auto cspace = [&]() {
      const auto rangedCspace = texture->GetYUVColorSpace();
      const auto info = FromYUVRangedColorSpace(rangedCspace);
      auto ret = ToColorSpace2(info.space);
      if (ret == gfx::ColorSpace2::Display && cmsMode == CMSMode::All) {
        ret = gfx::ColorSpace2::SRGB;
      }
      return ret;
    }();

    const bool rec709GammaAsSrgb =
        StaticPrefs::gfx_color_management_rec709_gamma_as_srgb();
    const bool rec2020GammaAsRec709 =
        StaticPrefs::gfx_color_management_rec2020_gamma_as_rec709();

    auto cspaceDesc = color::ColorspaceDesc{};
    const auto rangedCspace = texture->GetYUVColorSpace();
    const auto info = FromYUVRangedColorSpace(rangedCspace);
    const auto tf = info.transferFunction;
    switch (cspace) {
      case gfx::ColorSpace2::Display:
        return;  // No color management needed!
      case gfx::ColorSpace2::SRGB:
        cspaceDesc.chrom = color::Chromaticities::Srgb();
        cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
        switch (tf) {
          case gfx::TransferFunction::SRGB:
            cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
            break;
          case gfx::TransferFunction::BT709:
            if (rec709GammaAsSrgb) {
              cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
            } else {
              cspaceDesc.tf = color::TransferFunctionDesc::Rec709();
            }
            break;
          case gfx::TransferFunction::HLG:
            cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_HLG();
            break;
          case gfx::TransferFunction::PQ:
            cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_PQ();
            break;
          case gfx::TransferFunction::LINEAR:
            cspaceDesc.tf = color::TransferFunctionDesc::Linear();
            break;
        }
        break;
      case gfx::ColorSpace2::DISPLAY_P3:
        cspaceDesc.chrom = color::Chromaticities::DisplayP3();
        cspaceDesc.tf = color::TransferFunctionDesc::DisplayP3();
        switch (tf) {
          case gfx::TransferFunction::SRGB:
            cspaceDesc.tf = color::TransferFunctionDesc::DisplayP3();
            break;
          case gfx::TransferFunction::BT709:
            if (rec709GammaAsSrgb) {
              cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
            } else {
              cspaceDesc.tf = color::TransferFunctionDesc::Rec709();
            }
            break;
          case gfx::TransferFunction::HLG:
            cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_HLG();
            break;
          case gfx::TransferFunction::PQ:
            cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_PQ();
            break;
          case gfx::TransferFunction::LINEAR:
            cspaceDesc.tf = color::TransferFunctionDesc::Linear();
            break;
        }
        break;
      case gfx::ColorSpace2::BT601_525:  // aka smpte170m NTSC
        cspaceDesc.chrom = color::Chromaticities::Rec601_525_Ntsc();
        cspaceDesc.tf = color::TransferFunctionDesc::Rec709();
        switch (tf) {
          case gfx::TransferFunction::SRGB:
            cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
            break;
          case gfx::TransferFunction::BT709:
            if (rec709GammaAsSrgb) {
              cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
            } else {
              cspaceDesc.tf = color::TransferFunctionDesc::Rec709();
            }
            break;
          case gfx::TransferFunction::HLG:
            cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_HLG();
            break;
          case gfx::TransferFunction::PQ:
            cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_PQ();
            break;
          case gfx::TransferFunction::LINEAR:
            cspaceDesc.tf = color::TransferFunctionDesc::Linear();
            break;
        }
        break;
      case gfx::ColorSpace2::BT709:  // Same gamut as SRGB, but different gamma.
        cspaceDesc.chrom = color::Chromaticities::Rec709();
        cspaceDesc.tf = color::TransferFunctionDesc::Rec709();
        switch (tf) {
          case gfx::TransferFunction::SRGB:
            cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
            break;
          case gfx::TransferFunction::BT709:
            if (rec709GammaAsSrgb) {
              cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
            } else {
              cspaceDesc.tf = color::TransferFunctionDesc::Rec709();
            }
            break;
          case gfx::TransferFunction::HLG:
            cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_HLG();
            break;
          case gfx::TransferFunction::PQ:
            cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_PQ();
            break;
          case gfx::TransferFunction::LINEAR:
            cspaceDesc.tf = color::TransferFunctionDesc::Linear();
            break;
        }
        break;
      case gfx::ColorSpace2::BT2020:
        cspaceDesc.chrom = color::Chromaticities::Rec2020();
        cspaceDesc.tf = color::TransferFunctionDesc::Rec2020_12bit();
        switch (tf) {
          case gfx::TransferFunction::SRGB:
            cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
            break;
          case gfx::TransferFunction::BT709:
            // BT2020 uses a higher precision version of BT709/BT1886 values.
            if (rec2020GammaAsRec709 && rec709GammaAsSrgb) {
              cspaceDesc.tf = color::TransferFunctionDesc::Srgb();
            } else if (rec2020GammaAsRec709) {
              cspaceDesc.tf = color::TransferFunctionDesc::Rec709();
            } else {
              cspaceDesc.tf = color::TransferFunctionDesc::Rec2020_12bit();
            }
            break;
          case gfx::TransferFunction::HLG:
            cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_HLG();
            break;
          case gfx::TransferFunction::PQ:
            cspaceDesc.tf = color::TransferFunctionDesc::Rec2100_PQ();
            break;
          case gfx::TransferFunction::LINEAR:
            cspaceDesc.tf = color::TransferFunctionDesc::Linear();
            break;
        }
    }

    const auto cprofileIn = color::ColorProfileDesc::From(cspaceDesc);
    auto cprofileOut = mDCLayerTree->OutputColorProfile();
    bool pretendSrgb = true;
    if (pretendSrgb) {
      cprofileOut = color::ColorProfileDesc::From(color::ColorspaceDesc{
          .chrom = color::Chromaticities::Srgb(),
          .tf = color::TransferFunctionDesc::Srgb(),
      });
    }
    const auto conversion = color::ColorProfileConversionDesc::From({
        .src = cprofileIn,
        .dst = cprofileOut,
    });

    // -

    auto chain = ColorManagementChain::From(*dcomp3, conversion);
    mCManageChain = Some(chain);

    surfaceVisual->SetEffect(mCManageChain->last.get());
  }();

  return mSurface.get();
}

void DCExternalSurfaceWrapper::PresentExternalSurface(gfx::Matrix& aTransform) {
  MOZ_ASSERT(mSurface);
  if (auto* surface = mSurface->AsDCSurfaceVideo()) {
    if (surface->CalculateSwapChainSize(aTransform)) {
      surface->PresentVideo();
    }
  } else if (auto* surface = mSurface->AsDCSurfaceHandle()) {
    surface->PresentSurfaceHandle();
  } else if (auto* surface =
                 mSurface->AsDCSurfaceDCompositionTextureOverlay()) {
    surface->Present();
  }
}

template <typename T>
static inline D2D1_RECT_F D2DRect(const T& aRect) {
  return D2D1::RectF(aRect.X(), aRect.Y(), aRect.XMost(), aRect.YMost());
}

static inline D2D1_MATRIX_3X2_F D2DMatrix(const gfx::Matrix& aTransform) {
  return D2D1::Matrix3x2F(aTransform._11, aTransform._12, aTransform._21,
                          aTransform._22, aTransform._31, aTransform._32);
}

void DCLayerTree::AddSurface(wr::NativeSurfaceId aId,
                             const wr::CompositorSurfaceTransform& aTransform,
                             wr::DeviceIntRect aClipRect,
                             wr::ImageRendering aImageRendering,
                             wr::DeviceIntRect aRoundedClipRect,
                             wr::ClipRadius aClipRadius) {
  auto it = mDCSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(it != mDCSurfaces.end());
  const auto surface = it->second.get();
  const auto visual = surface->GetContentVisual();

  float sx = aTransform.scale.x;
  float sy = aTransform.scale.y;
  float tx = aTransform.offset.x;
  float ty = aTransform.offset.y;
  gfx::Matrix transform(sx, 0.0, 0.0, sy, tx, ty);

  surface->PresentExternalSurface(transform);

  if (UseLayerCompositor() &&
      !surface->IsUpdated(aTransform, aClipRect, aImageRendering,
                          aRoundedClipRect, aClipRadius)) {
    mCurrentLayers.push_back(aId);
    return;
  }

  mPendingCommit = true;

  // The DirectComposition API applies clipping *before* any
  // transforms/offset, whereas we want the clip applied after. Right now, we
  // only support rectilinear transforms, and then we transform our clip into
  // pre-transform coordinate space for it to be applied there.
  // DirectComposition does have an option for pre-transform clipping, if you
  // create an explicit IDCompositionEffectGroup object and set a 3D transform
  // on that. I suspect that will perform worse though, so we should only do
  // that for complex transforms (which are never provided right now).
  MOZ_ASSERT(transform.IsRectilinear());
  gfx::Rect clip = transform.Inverse().TransformBounds(gfx::Rect(
      aClipRect.min.x, aClipRect.min.y, aClipRect.width(), aClipRect.height()));
  // Set the clip rect - converting from world space to the pre-offset space
  // that DC requires for rectangle clips.
  visual->SetClip(D2DRect(clip));

  // TODO: The input matrix is a 4x4, but we only support a 3x2 at
  // the D3D API level (unless we QI to IDCompositionVisual3, which might
  // not be available?).
  // Should we assert here, or restrict at the WR API level.
  visual->SetTransform(D2DMatrix(transform));

  if (aImageRendering == wr::ImageRendering::Auto) {
    visual->SetBitmapInterpolationMode(
        DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR);
  } else {
    visual->SetBitmapInterpolationMode(
        DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
  }

  surface->SetClip(aRoundedClipRect, aClipRadius);

  mCurrentLayers.push_back(aId);
}

GLuint DCLayerTree::GetOrCreateFbo(int aWidth, int aHeight) {
  const auto gl = GetGLContext();
  GLuint fboId = 0;

  // Check if we have a cached FBO with matching dimensions
  for (auto it = mFrameBuffers.begin(); it != mFrameBuffers.end(); ++it) {
    if (it->width == aWidth && it->height == aHeight) {
      fboId = it->fboId;
      it->lastFrameUsed = mCurrentFrame;
      break;
    }
  }

  // If not, create a new FBO with attached depth buffer
  if (fboId == 0) {
    // Create the depth buffer
    GLuint depthRboId;
    gl->fGenRenderbuffers(1, &depthRboId);
    gl->fBindRenderbuffer(LOCAL_GL_RENDERBUFFER, depthRboId);
    gl->fRenderbufferStorage(LOCAL_GL_RENDERBUFFER, LOCAL_GL_DEPTH_COMPONENT24,
                             aWidth, aHeight);

    // Create the framebuffer and attach the depth buffer to it
    gl->fGenFramebuffers(1, &fboId);
    gl->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, fboId);
    gl->fFramebufferRenderbuffer(LOCAL_GL_DRAW_FRAMEBUFFER,
                                 LOCAL_GL_DEPTH_ATTACHMENT,
                                 LOCAL_GL_RENDERBUFFER, depthRboId);

    // Store this in the cache for future calls.
    // TODO(gw): Maybe we should periodically scan this list and remove old
    // entries that
    //           haven't been used for some time?
    DCLayerTree::CachedFrameBuffer frame_buffer_info;
    frame_buffer_info.width = aWidth;
    frame_buffer_info.height = aHeight;
    frame_buffer_info.fboId = fboId;
    frame_buffer_info.depthRboId = depthRboId;
    frame_buffer_info.lastFrameUsed = mCurrentFrame;
    mFrameBuffers.AppendElement(frame_buffer_info);
  }

  return fboId;
}

bool DCLayerTree::EnsureVideoProcessor(const gfx::IntSize& aInputSize,
                                       const gfx::IntSize& aOutputSize) {
  HRESULT hr;

  if (!mVideoDevice || !mVideoContext) {
    return false;
  }

  if (mVideoProcessor && (aInputSize <= mVideoInputSize) &&
      (aOutputSize <= mVideoOutputSize)) {
    return true;
  }

  mVideoProcessor = nullptr;
  mVideoProcessorEnumerator = nullptr;

  D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc = {};
  desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  desc.InputFrameRate.Numerator = 60;
  desc.InputFrameRate.Denominator = 1;
  desc.InputWidth = aInputSize.width;
  desc.InputHeight = aInputSize.height;
  desc.OutputFrameRate.Numerator = 60;
  desc.OutputFrameRate.Denominator = 1;
  desc.OutputWidth = aOutputSize.width;
  desc.OutputHeight = aOutputSize.height;
  desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

  hr = mVideoDevice->CreateVideoProcessorEnumerator(
      &desc, getter_AddRefs(mVideoProcessorEnumerator));
  if (FAILED(hr)) {
    gfxCriticalNote << "Failed to create VideoProcessorEnumerator: "
                    << gfx::hexa(hr);
    return false;
  }

  hr = mVideoDevice->CreateVideoProcessor(mVideoProcessorEnumerator, 0,
                                          getter_AddRefs(mVideoProcessor));
  if (FAILED(hr)) {
    mVideoProcessor = nullptr;
    mVideoProcessorEnumerator = nullptr;
    gfxCriticalNote << "Failed to create VideoProcessor: " << gfx::hexa(hr);
    return false;
  }

  // Reduce power cosumption
  // By default, the driver might perform certain processing tasks
  // automatically
  mVideoContext->VideoProcessorSetStreamAutoProcessingMode(mVideoProcessor, 0,
                                                           FALSE);

  mVideoInputSize = aInputSize;
  mVideoOutputSize = aOutputSize;

  return true;
}

bool DCLayerTree::SupportsHardwareOverlays() {
  return sGpuOverlayInfo->mSupportsHardwareOverlays;
}

bool DCLayerTree::SupportsHardwareOverlayRGB10A2() {
  return sGpuOverlayInfo->mSupportsHardwareOverlayRGB10A2;
}

bool DCLayerTree::SupportsHardwareOverlayRGBA16F() {
  return sGpuOverlayInfo->mSupportsHardwareOverlayRGBA16F;
}

bool DCLayerTree::SupportsSwapChainTearing() {
  RefPtr<ID3D11Device> device = mDevice;
  static const bool supported = [device] {
    RefPtr<IDXGIDevice> dxgiDevice;
    RefPtr<IDXGIAdapter> adapter;
    device->QueryInterface((IDXGIDevice**)getter_AddRefs(dxgiDevice));
    dxgiDevice->GetAdapter(getter_AddRefs(adapter));

    RefPtr<IDXGIFactory5> dxgiFactory;
    HRESULT hr = adapter->GetParent(
        IID_PPV_ARGS((IDXGIFactory5**)getter_AddRefs(dxgiFactory)));
    if (FAILED(hr)) {
      return false;
    }

    BOOL presentAllowTearing = FALSE;
    hr = dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                          &presentAllowTearing,
                                          sizeof(presentAllowTearing));
    if (FAILED(hr)) {
      return false;
    }

    if (auto* gpuParent = gfx::GPUParent::GetSingleton()) {
      gpuParent->NotifySwapChainInfo(
          layers::SwapChainInfo(!!presentAllowTearing));
    } else if (XRE_IsParentProcess()) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
    return !!presentAllowTearing;
  }();

  if (!StaticPrefs::gfx_webrender_swap_chain_allow_tearing_AtStartup()) {
    return false;
  }

  return supported;
}

bool DCLayerTree::UseDCLayerDCompositionTexture() {
  if (!gfx::gfxVars::WebRenderLayerCompositorDCompTexture()) {
    return false;
  }
  return gfx::DeviceManagerDx::Get()->CanUseDCompositionTexture();
}

DXGI_FORMAT DCLayerTree::GetOverlayFormatForSDR() {
  return sGpuOverlayInfo->mOverlayFormatUsed;
}

static layers::OverlaySupportType FlagsToOverlaySupportType(
    UINT aFlags, bool aSoftwareOverlaySupported) {
  if (aFlags & DXGI_OVERLAY_SUPPORT_FLAG_SCALING) {
    return layers::OverlaySupportType::Scaling;
  }
  if (aFlags & DXGI_OVERLAY_SUPPORT_FLAG_DIRECT) {
    return layers::OverlaySupportType::Direct;
  }
  if (aSoftwareOverlaySupported) {
    return layers::OverlaySupportType::Software;
  }
  return layers::OverlaySupportType::None;
}

layers::OverlayInfo DCLayerTree::GetOverlayInfo() {
  layers::OverlayInfo info;

  info.mSupportsOverlays = sGpuOverlayInfo->mSupportsHardwareOverlays;
  info.mNv12Overlay =
      FlagsToOverlaySupportType(sGpuOverlayInfo->mNv12OverlaySupportFlags,
                                /* aSoftwareOverlaySupported */ false);
  info.mYuy2Overlay =
      FlagsToOverlaySupportType(sGpuOverlayInfo->mYuy2OverlaySupportFlags,
                                /* aSoftwareOverlaySupported */ false);
  info.mBgra8Overlay =
      FlagsToOverlaySupportType(sGpuOverlayInfo->mBgra8OverlaySupportFlags,
                                /* aSoftwareOverlaySupported */ true);
  info.mRgb10a2Overlay =
      FlagsToOverlaySupportType(sGpuOverlayInfo->mRgb10a2OverlaySupportFlags,
                                /* aSoftwareOverlaySupported */ false);
  info.mRgba16fOverlay =
      FlagsToOverlaySupportType(sGpuOverlayInfo->mRgba16fOverlaySupportFlags,
                                /* aSoftwareOverlaySupported */ false);

  info.mSupportsVpSuperResolution = sGpuOverlayInfo->mSupportsVpSuperResolution;
  info.mSupportsVpAutoHDR = sGpuOverlayInfo->mSupportsVpAutoHDR;
  info.mSupportsHDR = sGpuOverlayInfo->mSupportsHDR;

  return info;
}

void DCLayerTree::SetUsedOverlayTypeInFrame(DCompOverlayTypes aTypes) {
  mUsedOverlayTypesInFrame |= aTypes;
}

DCSurface::DCSurface(bool aIsOpaque, DCLayerTree* aDCLayerTree)
    : mDCLayerTree(aDCLayerTree), mIsOpaque(aIsOpaque) {}

DCSurface::~DCSurface() = default;

bool DCSurface::IsUpdated(const wr::CompositorSurfaceTransform& aTransform,
                          const wr::DeviceIntRect& aClipRect,
                          const wr::ImageRendering aImageRendering,
                          const wr::DeviceIntRect& aRoundedClipRect,
                          const wr::ClipRadius& aClipRadius) {
  if (mDCSurfaceData.isSome() &&
      mDCSurfaceData.ref().mTransform == aTransform &&
      mDCSurfaceData.ref().mClipRect == aClipRect &&
      mDCSurfaceData.ref().mImageRendering == aImageRendering &&
      mDCSurfaceData.ref().mRoundedClipRect == aRoundedClipRect &&
      mDCSurfaceData.ref().mClipRadius == aClipRadius) {
    return false;
  }
  mDCSurfaceData = Some(DCSurfaceData(aTransform, aClipRect, aImageRendering,
                                      aRoundedClipRect, aClipRadius));
  return true;
}

bool DCSurface::Initialize() {
  HRESULT hr;
  const auto dCompDevice = mDCLayerTree->GetCompositionDevice();
  hr = dCompDevice->CreateVisual(getter_AddRefs(mRootVisual));
  if (FAILED(hr)) {
    gfxCriticalNote << "Failed to create DCompositionVisual: " << gfx::hexa(hr);
    return false;
  }
  hr = dCompDevice->CreateVisual(getter_AddRefs(mContentVisual));
  if (FAILED(hr)) {
    gfxCriticalNote << "Failed to create DCompositionVisual: " << gfx::hexa(hr);
    return false;
  }
  mRootVisual->AddVisual(mContentVisual, false, nullptr);
  hr = dCompDevice->CreateRectangleClip(getter_AddRefs(mClip));
  if (FAILED(hr)) {
    gfxCriticalNote << "Failed to create RectangleClip: " << gfx::hexa(hr);
    return false;
  }

  return true;
}

void DCSurface::SetClip(wr::DeviceIntRect aClipRect,
                        wr::ClipRadius aClipRadius) {
  bool needsClip =
      aClipRadius.top_left > 0.0f || aClipRadius.top_right > 0.0f ||
      aClipRadius.bottom_left > 0.0f || aClipRadius.bottom_right > 0.0f;

  if (needsClip) {
    mClip->SetLeft(aClipRect.min.x);
    mClip->SetRight(aClipRect.max.x);
    mClip->SetTop(aClipRect.min.y);
    mClip->SetBottom(aClipRect.max.y);

    mClip->SetTopLeftRadiusX(aClipRadius.top_left);
    mClip->SetTopLeftRadiusY(aClipRadius.top_left);

    mClip->SetTopRightRadiusX(aClipRadius.top_right);
    mClip->SetTopRightRadiusY(aClipRadius.top_right);

    mClip->SetBottomLeftRadiusX(aClipRadius.bottom_left);
    mClip->SetBottomLeftRadiusY(aClipRadius.bottom_left);

    mClip->SetBottomRightRadiusX(aClipRadius.bottom_right);
    mClip->SetBottomRightRadiusY(aClipRadius.bottom_right);

    mRootVisual->SetBorderMode(DCOMPOSITION_BORDER_MODE_SOFT);
    mRootVisual->SetClip(mClip);
  } else {
    mRootVisual->SetBorderMode(DCOMPOSITION_BORDER_MODE_INHERIT);
    mRootVisual->SetClip(nullptr);
  }
}

DCLayerDCompositionTexture::TextureHolder::TextureHolder(
    ID3D11Texture2D* aTexture, IDCompositionTexture* aDCompositionTexture,
    EGLSurface aEGLSurface)
    : mTexture(aTexture),
      mDCompositionTexture(aDCompositionTexture),
      mEGLSurface(aEGLSurface) {}

DCLayerDCompositionTexture::DCLayerDCompositionTexture(
    wr::DeviceIntSize aSize, bool aIsOpaque, DCLayerTree* aDCLayerTree)
    : DCLayerSurface(aIsOpaque, aDCLayerTree),
      mSwapChainBufferCount(gfx::gfxVars::UseWebRenderTripleBufferingWin() ? 3
                                                                           : 2),
      mSize(aSize) {}

DCLayerDCompositionTexture::~DCLayerDCompositionTexture() { DestroyTextures(); }

bool DCLayerDCompositionTexture::Initialize() {
  DCSurface::Initialize();

  if (!AllocateTextures()) {
    return false;
  }

  // Prefer a stable visual content object whose backing composition texture
  // can be changed on each present. Keep the existing path on systems that
  // do not implement IDCompositionDevice5.
  const auto dcomp = mDCLayerTree->GetCompositionDevice();
  const auto dcomp5 = QI<IDCompositionDevice5>::From(dcomp);
  if (dcomp5) {
    RefPtr<IDCompositionDynamicTexture> dynamicTexture;
    HRESULT hr = dcomp5->CreateDynamicTexture(getter_AddRefs(dynamicTexture));
    if (SUCCEEDED(hr) && dynamicTexture) {
      hr = mContentVisual->SetContent(dynamicTexture);
      if (SUCCEEDED(hr)) {
        mDCompositionDynamicTexture = dynamicTexture;
      } else {
        gfxCriticalNoteOnce << "SetContent(dynamic texture) failed: "
                            << gfx::hexa(hr);
      }
    } else {
      gfxCriticalNoteOnce << "CreateDynamicTexture failed: " << gfx::hexa(hr);
    }
  }

  return true;
}

bool DCLayerDCompositionTexture::AllocateTextures() {
  MOZ_ASSERT(mAvailableTextureHolders.empty());
  for (size_t i = 0; i < mSwapChainBufferCount; ++i) {
    auto holder = AllocateTexture();
    if (!holder) {
      return false;
    }
    mAvailableTextureHolders.push_back(std::move(holder));
  }
  return true;
}

UniquePtr<DCLayerDCompositionTexture::TextureHolder>
DCLayerDCompositionTexture::AllocateTexture() {
  HRESULT hr;
  const auto device = mDCLayerTree->GetDevice();
  const auto dcomp = mDCLayerTree->GetCompositionDevice();
  const auto dcomp4 = QI<IDCompositionDevice4>::From(dcomp);
  if (!dcomp4) {
    return nullptr;
  }

  const auto gl = mDCLayerTree->GetGLContext();
  const auto& gle = gl::GLContextEGL::Cast(gl);
  const auto& egl = gle->mEgl;
  const EGLConfig eglConfig = mDCLayerTree->GetEGLConfig();

  CD3D11_TEXTURE2D_DESC desc(
      DXGI_FORMAT_B8G8R8A8_UNORM, mSize.width, mSize.height, 1, 1,
      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);

  desc.MiscFlags =
      D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;

  // Allocate ID3D11Texture2D
  RefPtr<ID3D11Texture2D> texture;
  hr = device->CreateTexture2D(&desc, nullptr, getter_AddRefs(texture));
  if (FAILED(hr)) {
    gfxCriticalNoteOnce << "CreateTexture2D failed:  " << gfx::hexa(hr);
    return nullptr;
  }

  // Allocate IDCompositionTexture
  RefPtr<IDCompositionTexture> dcompTexture;
  hr = dcomp4->CreateCompositionTexture(texture, getter_AddRefs(dcompTexture));
  if (FAILED(hr)) {
    gfxCriticalNoteOnce << "CreateCompositionTexture failed:  "
                        << gfx::hexa(hr);
    return nullptr;
  }

  const auto alphaMode =
      mIsOpaque ? DXGI_ALPHA_MODE_IGNORE : DXGI_ALPHA_MODE_PREMULTIPLIED;
  dcompTexture->SetAlphaMode(alphaMode);
  // XXX
  // dcompTexture->SetColorSpace();

  // Allocate mEGLSurface
  EGLSurface surface = EGL_NO_SURFACE;
  const EGLint pbuffer_attribs[]{LOCAL_EGL_WIDTH, mSize.width, LOCAL_EGL_HEIGHT,
                                 mSize.height, LOCAL_EGL_NONE};
  const auto buffer = reinterpret_cast<EGLClientBuffer>(texture.get());

  surface = egl->fCreatePbufferFromClientBuffer(
      LOCAL_EGL_D3D_TEXTURE_ANGLE, buffer, eglConfig, pbuffer_attribs);
  if (!surface) {
    EGLint err = egl->mLib->fGetError();
    gfxCriticalNote << "Failed to create Pbuffer error: " << gfx::hexa(err)
                    << " Size : "
                    << LayoutDeviceIntSize(mSize.width, mSize.height);
    return nullptr;
  }

  return std::make_unique<TextureHolder>(texture, dcompTexture, surface);
}

void DCLayerDCompositionTexture::DestroyTexture(
    UniquePtr<TextureHolder> aHolder) {
  const auto gl = mDCLayerTree->GetGLContext();
  const auto& gle = gl::GLContextEGL::Cast(gl);
  if (aHolder->mEGLSurface != EGL_NO_SURFACE) {
    if (gle->GetEGLSurfaceOverride() == aHolder->mEGLSurface) {
      gle->SetEGLSurfaceOverride(EGL_NO_SURFACE);
    }
    gle->mEgl->fDestroySurface(aHolder->mEGLSurface);
  }
}

void DCLayerDCompositionTexture::DestroyTextures() {
  if (mCurrentTextureHolder) {
    DestroyTexture(std::move(mCurrentTextureHolder));
  }
  if (mPresentingTextureHolder) {
    DestroyTexture(std::move(mPresentingTextureHolder));
  }
  while (!mAvailableTextureHolders.empty()) {
    auto holder = std::move(mAvailableTextureHolders.front());
    mAvailableTextureHolders.pop_front();
    DestroyTexture(std::move(holder));
  }
}

UniquePtr<DCLayerDCompositionTexture::TextureHolder>
DCLayerDCompositionTexture::GetNextTexture() {
  // Rotation alone does not guarantee DWM has finished reading a buffer.
  const auto ctx = mDCLayerTree->GetDeviceContext();
  const auto ctx4 = QI<ID3D11DeviceContext4>::From(ctx);
  const size_t count = mAvailableTextureHolders.size();
  for (size_t i = 0; i < count; ++i) {
    auto holder = std::move(mAvailableTextureHolders.front());
    mAvailableTextureHolders.pop_front();
    if (!holder->mHasBeenPresented) {
      return holder;
    }

    UINT64 value = 0;
    RefPtr<ID3D11Fence> fence;
    HRESULT hr = holder->mDCompositionTexture->GetAvailableFence(
        &value, __uuidof(ID3D11Fence), getter_AddRefs(fence));
    if (SUCCEEDED(hr) && fence) {
      const UINT64 completed = fence->GetCompletedValue();
      // UINT64_MAX indicates device removal, not a completed wait.
      if (completed != UINT64_MAX &&
          (completed >= value ||
           (ctx4 && SUCCEEDED(ctx4->Wait(fence, value))))) {
        return holder;
      }
    }
    mAvailableTextureHolders.push_back(std::move(holder));
  }

  // A null available fence means the texture is still displayed or queued.
  // Replace one such buffer rather than writing to it or growing the pool.
  // DWM retains the composition texture for as long as it needs it.
  if (!mAvailableTextureHolders.empty()) {
    auto holder = std::move(mAvailableTextureHolders.front());
    mAvailableTextureHolders.pop_front();
    DestroyTexture(std::move(holder));
  }
  return AllocateTexture();
}

void DCLayerDCompositionTexture::UpdateCurrentTexture() {
  if (mCurrentTextureHolder) {
    mAvailableTextureHolders.push_back(std::move(mCurrentTextureHolder));
  }

  MOZ_ASSERT(!mCurrentTextureHolder);

  mCurrentTextureHolder = GetNextTexture();
}

void DCLayerDCompositionTexture::Bind(const wr::DeviceIntRect* aDirtyRects,
                                      size_t aNumDirtyRects) {
  const auto gl = mDCLayerTree->GetGLContext();
  const auto& gle = gl::GLContextEGL::Cast(gl);
  // Submit pending ANGLE work before inserting a D3D wait and copy on the
  // same immediate context. Neither operation changes ANGLE's cached state.
  gl->fFlush();
  UpdateCurrentTexture();
  if (!mCurrentTextureHolder) {
    gfxCriticalNoteOnce << "Failed to acquire a composition texture buffer";
    RenderThread::Get()->HandleWebRenderError(WebRenderError::NEW_SURFACE);
    return;
  }

  // A new or resized layer is fully rendered by WrLayerCompositor::begin_frame.
  MOZ_ASSERT_IF(aNumDirtyRects > 0, mPresentingTextureHolder);
  if (aNumDirtyRects > 0 && mPresentingTextureHolder) {
    // Every backing buffer must contain the whole latest frame: DWM may read
    // outside the damage hint. Copy before drawing the new dirty region.
    mDCLayerTree->GetDeviceContext()->CopyResource(
        mCurrentTextureHolder->mTexture, mPresentingTextureHolder->mTexture);
  }
  gle->SetEGLSurfaceOverride(mCurrentTextureHolder->mEGLSurface);
}

bool DCLayerDCompositionTexture::Resize(wr::DeviceIntSize aSize) {
  DestroyTextures();
  mSize = aSize;
  bool ret = AllocateTextures();
  return ret;
}

void DCLayerDCompositionTexture::Present(const wr::DeviceIntRect* aDirtyRects,
                                         size_t aNumDirtyRects) {
  if (!mCurrentTextureHolder) {
    return;
  }

  const bool firstPresent = !mPresentingTextureHolder;
  if (mPresentingTextureHolder) {
    mAvailableTextureHolders.push_back(std::move(mPresentingTextureHolder));
  }
  mPresentingTextureHolder = std::move(mCurrentTextureHolder);
  mPresentingTextureHolder->mHasBeenPresented = true;
  mDCLayerTree->GetGLContext()->fFlush();

  if (mDCompositionDynamicTexture) {
    HRESULT hr;
    const auto texture = mPresentingTextureHolder->mDCompositionTexture;
    if (firstPresent || aNumDirtyRects == 0) {
      // Empty dirty-rect input means full rendering in the layer compositor.
      // The three-argument API with zero rects instead means no damage.
      hr = mDCompositionDynamicTexture->SetTexture(texture);
    } else {
      StackArray<D2D_RECT_L, 1> rects(aNumDirtyRects);
      size_t count = 0;
      for (size_t i = 0; i < aNumDirtyRects; ++i) {
        const auto& rect = aDirtyRects[i];
        const int left = std::clamp(rect.min.x, 0, mSize.width);
        const int top = std::clamp(rect.min.y, 0, mSize.height);
        const int right = std::clamp(rect.max.x, 0, mSize.width);
        const int bottom = std::clamp(rect.max.y, 0, mSize.height);
        if (left < right && top < bottom) {
          rects[count++] = D2D_RECT_L{left, top, right, bottom};
        }
      }
      hr =
          mDCompositionDynamicTexture->SetTexture(texture, rects.data(), count);
    }
    if (SUCCEEDED(hr)) {
      // PresentSwapChain() schedules Commit() even if visual content is stable.
      return;
    }
    gfxCriticalNoteOnce << "Dynamic texture SetTexture failed: "
                        << gfx::hexa(hr);
    mDCompositionDynamicTexture = nullptr;
  }

  // The copied buffer is complete, so fallback remains correct after partial
  // drawing; only DWM's incremental damage optimization is lost.
  mContentVisual->SetContent(mPresentingTextureHolder->mDCompositionTexture);
}

DCSwapChain::DCSwapChain(wr::DeviceIntSize aSize, bool aIsOpaque,
                         DCLayerTree* aDCLayerTree)
    : DCLayerSurface(aIsOpaque, aDCLayerTree),
      mSwapChainBufferCount(gfx::gfxVars::UseWebRenderTripleBufferingWin() ? 3
                                                                           : 2),
      mSize(aSize),
      mEGLSurface(EGL_NO_SURFACE) {
  MOZ_ASSERT(mSwapChainBufferCount == 2 || mSwapChainBufferCount == 3);
}

DCSwapChain::~DCSwapChain() {
  if (mEGLSurface) {
    const auto gl = mDCLayerTree->GetGLContext();

    const auto& gle = gl::GLContextEGL::Cast(gl);
    const auto& egl = gle->mEgl;

    if (gle->GetEGLSurfaceOverride() == mEGLSurface) {
      gle->SetEGLSurfaceOverride(EGL_NO_SURFACE);
    }
    egl->fDestroySurface(mEGLSurface);
    mEGLSurface = EGL_NO_SURFACE;
  }
}

bool DCSwapChain::Initialize() {
  DCSurface::Initialize();

  const auto gl = mDCLayerTree->GetGLContext();
  const auto& gle = gl::GLContextEGL::Cast(gl);
  const auto& egl = gle->mEgl;

  HRESULT hr;
  auto device = mDCLayerTree->GetDevice();

  RefPtr<IDXGIDevice> dxgiDevice;
  device->QueryInterface((IDXGIDevice**)getter_AddRefs(dxgiDevice));

  RefPtr<IDXGIFactory2> dxgiFactory;
  {
    RefPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(getter_AddRefs(adapter));
    adapter->GetParent(
        IID_PPV_ARGS((IDXGIFactory2**)getter_AddRefs(dxgiFactory)));
  }

  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width = mSize.width;
  desc.Height = mSize.height;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = mSwapChainBufferCount;
  // DXGI_SCALING_NONE caused swap chain creation failure.
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.AlphaMode =
      mIsOpaque ? DXGI_ALPHA_MODE_IGNORE : DXGI_ALPHA_MODE_PREMULTIPLIED;
  desc.Flags = 0;
  if (mDCLayerTree->SupportsSwapChainTearing()) {
    desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
  }

  hr = dxgiFactory->CreateSwapChainForComposition(device, &desc, nullptr,
                                                  getter_AddRefs(mSwapChain));
  if (FAILED(hr)) {
    gfxCriticalNote << "CreateSwapChainForComposition() failed: "
                    << gfx::hexa(hr) << " Size : "
                    << LayoutDeviceIntSize(mSize.width, mSize.height);
    return false;
  }
  mContentVisual->SetContent(mSwapChain);

  RefPtr<ID3D11Texture2D> backBuffer;
  hr = mSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                             (void**)getter_AddRefs(backBuffer));
  if (hr == DXGI_ERROR_INVALID_CALL) {
    // This happens on some GPUs/drivers when there's a TDR.
    if (device->GetDeviceRemovedReason() != S_OK) {
      gfxCriticalNote << "GetBuffer returned invalid call: " << gfx::hexa(hr)
                      << " Size : "
                      << LayoutDeviceIntSize(mSize.width, mSize.height);
      return false;
    }
  }

  const EGLint pbuffer_attribs[]{LOCAL_EGL_WIDTH, mSize.width, LOCAL_EGL_HEIGHT,
                                 mSize.height, LOCAL_EGL_NONE};
  const auto buffer = reinterpret_cast<EGLClientBuffer>(backBuffer.get());
  EGLConfig eglConfig = mDCLayerTree->GetEGLConfig();

  mEGLSurface = egl->fCreatePbufferFromClientBuffer(
      LOCAL_EGL_D3D_TEXTURE_ANGLE, buffer, eglConfig, pbuffer_attribs);
  if (!mEGLSurface) {
    EGLint err = egl->mLib->fGetError();
    gfxCriticalNote << "Failed to create Pbuffer error: " << gfx::hexa(err)
                    << " Size : "
                    << LayoutDeviceIntSize(mSize.width, mSize.height);
    return false;
  }

  return true;
}

void DCSwapChain::Bind(const wr::DeviceIntRect* aDirtyRects,
                       size_t aNumDirtyRects) {
  const auto gl = mDCLayerTree->GetGLContext();
  const auto& gle = gl::GLContextEGL::Cast(gl);

  gle->SetEGLSurfaceOverride(mEGLSurface);
}

bool DCSwapChain::Resize(wr::DeviceIntSize aSize) {
  MOZ_ASSERT(mSwapChain);

  if (!mSwapChain) {
    return false;
  }

  const auto gl = mDCLayerTree->GetGLContext();

  const auto& gle = gl::GLContextEGL::Cast(gl);
  const auto& egl = gle->mEgl;

  if (mEGLSurface) {
    egl->fDestroySurface(mEGLSurface);
    mEGLSurface = EGL_NO_SURFACE;
  }

  DXGI_SWAP_CHAIN_DESC desc;
  HRESULT hr;

  mSwapChain->GetDesc(&desc);

  UINT flags = mDCLayerTree->SupportsSwapChainTearing()
                   ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
                   : 0;
  hr = mSwapChain->ResizeBuffers(desc.BufferCount, aSize.width, aSize.height,
                                 DXGI_FORMAT_B8G8R8A8_UNORM, flags);
  if (FAILED(hr)) {
    gfxCriticalNote << "Failed to resize swap chain buffers: " << gfx::hexa(hr)
                    << " Size : "
                    << LayoutDeviceIntSize(aSize.width, aSize.height);
    return false;
  }

  RefPtr<ID3D11Texture2D> backBuffer;
  hr = mSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                             (void**)getter_AddRefs(backBuffer));
  if (hr == DXGI_ERROR_INVALID_CALL) {
    auto device = mDCLayerTree->GetDevice();
    // This happens on some GPUs/drivers when there's a TDR.
    if (device->GetDeviceRemovedReason() != S_OK) {
      gfxCriticalNote << "GetBuffer returned invalid call: " << gfx::hexa(hr)
                      << " Size : "
                      << LayoutDeviceIntSize(aSize.width, aSize.height);
      return false;
    }
  }

  const EGLint pbuffer_attribs[]{LOCAL_EGL_WIDTH, aSize.width, LOCAL_EGL_HEIGHT,
                                 aSize.height, LOCAL_EGL_NONE};
  const auto buffer = reinterpret_cast<EGLClientBuffer>(backBuffer.get());
  EGLConfig eglConfig = mDCLayerTree->GetEGLConfig();

  mEGLSurface = egl->fCreatePbufferFromClientBuffer(
      LOCAL_EGL_D3D_TEXTURE_ANGLE, buffer, eglConfig, pbuffer_attribs);
  if (!mEGLSurface) {
    EGLint err = egl->mLib->fGetError();
    gfxCriticalNote << "Failed to create Pbuffer error: " << gfx::hexa(err)
                    << " Size : "
                    << LayoutDeviceIntSize(aSize.width, aSize.height);
    return false;
  }

  mSize = aSize;
  return true;
}

void DCSwapChain::Present(const wr::DeviceIntRect* aDirtyRects,
                          size_t aNumDirtyRects) {
  MOZ_ASSERT_IF(aNumDirtyRects > 0, !mFirstPresent);

  MOZ_ASSERT(mSwapChain);

  if (!mSwapChain) {
    return;
  }

  HRESULT hr = S_OK;
  int rectsCount = 0;
  StackArray<RECT, 1> rects(aNumDirtyRects);
  const UINT flags =
      mDCLayerTree->SupportsSwapChainTearing() ? DXGI_PRESENT_ALLOW_TEARING : 0;

  if (aNumDirtyRects > 0) {
    for (size_t i = 0; i < aNumDirtyRects; ++i) {
      const auto& rect = aDirtyRects[i];
      // Clip rect to bufferSize
      int left = std::clamp((int)rect.min.x, 0, mSize.width);
      int top = std::clamp((int)rect.min.y, 0, mSize.height);
      int right = std::clamp((int)rect.max.x, 0, mSize.width);
      int bottom = std::clamp((int)rect.max.y, 0, mSize.height);

      // When rect is not empty, the rect could be passed to Present1().
      if (left < right && top < bottom) {
        rects[rectsCount].left = left;
        rects[rectsCount].top = top;
        rects[rectsCount].right = right;
        rects[rectsCount].bottom = bottom;
        rectsCount++;
      }
    }

    if (rectsCount > 0) {
      DXGI_PRESENT_PARAMETERS params;
      PodZero(&params);
      params.DirtyRectsCount = rectsCount;
      params.pDirtyRects = rects.data();

      hr = mSwapChain->Present1(0, flags, &params);
      if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) {
        auto* device = mDCLayerTree->GetDevice();
        auto reason = device->GetDeviceRemovedReason();
        gfxCriticalNote << "Present1 failed: " << gfx::hexa(hr) << " reason "
                        << gfx::hexa(reason);
      }
    }
  } else {
    mSwapChain->Present(0, flags);
  }

  if (mFirstPresent) {
    mFirstPresent = false;

    // Wait for the GPU to finish executing its commands before
    // committing the DirectComposition tree, or else the swapchain
    // may flicker black when it's first presented.
    auto* device = mDCLayerTree->GetDevice();
    RefPtr<IDXGIDevice2> dxgiDevice2;
    device->QueryInterface((IDXGIDevice2**)getter_AddRefs(dxgiDevice2));
    MOZ_ASSERT(dxgiDevice2);

    HANDLE event = ::CreateEvent(nullptr, false, false, nullptr);
    hr = dxgiDevice2->EnqueueSetEvent(event);
    if (SUCCEEDED(hr)) {
      DebugOnly<DWORD> result = ::WaitForSingleObject(event, INFINITE);
      MOZ_ASSERT(result == WAIT_OBJECT_0);
    } else {
      gfxCriticalNoteOnce << "EnqueueSetEvent failed: " << gfx::hexa(hr);
    }
    ::CloseHandle(event);
  }
}

DCLayerCompositionSurface::DCLayerCompositionSurface(wr::DeviceIntSize aSize,
                                                     bool aIsOpaque,
                                                     DCLayerTree* aDCLayerTree)
    : DCLayerSurface(aIsOpaque, aDCLayerTree), mSize(aSize) {}

DCLayerCompositionSurface::~DCLayerCompositionSurface() {
  if (mEGLSurface) {
    const auto gl = mDCLayerTree->GetGLContext();
    const auto& gle = gl::GLContextEGL::Cast(gl);
    const auto& egl = gle->mEgl;

    egl->fDestroySurface(mEGLSurface);
    mEGLSurface = EGL_NO_SURFACE;
  }
}

bool DCLayerCompositionSurface::Initialize() {
  DCSurface::Initialize();

  if (!Resize(mSize)) {
    return false;
  }
  return true;
}

void DCLayerCompositionSurface::Bind(const wr::DeviceIntRect* aDirtyRects,
                                     size_t aNumDirtyRects) {
  MOZ_ASSERT(mCompositionSurface);

  if (!mCompositionSurface) {
    return;
  }

  RefPtr<ID3D11Texture2D> backBuffer;
  POINT offset;
  HRESULT hr;

  RECT updateRect;
  gfx::IntPoint updatePos;
  if (aNumDirtyRects > 0) {
    MOZ_ASSERT(!mFirstDraw);
    MOZ_ASSERT(aNumDirtyRects == 1);

    updateRect.left = std::clamp(aDirtyRects[0].min.x, 0, mSize.width);
    updateRect.top = std::clamp(aDirtyRects[0].min.y, 0, mSize.height);
    updateRect.right = std::clamp(aDirtyRects[0].max.x, 0, mSize.width);
    updateRect.bottom = std::clamp(aDirtyRects[0].max.y, 0, mSize.height);

    updatePos = {updateRect.left, updateRect.top};
  } else {
    updateRect.left = 0;
    updateRect.top = 0;
    updateRect.right = mSize.width;
    updateRect.bottom = mSize.height;

    updatePos = {0, 0};
  }

  mFirstDraw = false;
  LayoutDeviceIntRect rect = widget::WinUtils::ToIntRect(updateRect);
  MOZ_ASSERT(!rect.IsEmpty());

  hr = mCompositionSurface->BeginDraw(&updateRect, __uuidof(ID3D11Texture2D),
                                      (void**)getter_AddRefs(backBuffer),
                                      &offset);

  if (FAILED(hr)) {
    gfxCriticalNote << "DCLayerCompositionSurface::Bind failed: "
                    << gfx::hexa(hr) << " " << rect;
    RenderThread::Get()->HandleWebRenderError(WebRenderError::BEGIN_DRAW);
    return;
  }

  const auto gl = mDCLayerTree->GetGLContext();
  const auto& gle = gl::GLContextEGL::Cast(gl);
  const auto& egl = gle->mEgl;

  gfx::IntPoint originOffset = {(int)offset.x - updatePos.x,
                                (int)offset.y - updatePos.y};
  const EGLint pbuffer_attribs[]{LOCAL_EGL_WIDTH,
                                 mSize.width,
                                 LOCAL_EGL_HEIGHT,
                                 mSize.height,
                                 LOCAL_EGL_TEXTURE_OFFSET_X_ANGLE,
                                 originOffset.x,
                                 LOCAL_EGL_TEXTURE_OFFSET_Y_ANGLE,
                                 originOffset.y,
                                 LOCAL_EGL_NONE};
  const auto buffer = reinterpret_cast<EGLClientBuffer>(backBuffer.get());
  EGLConfig eglConfig = mDCLayerTree->GetEGLConfig();

  mEGLSurface = egl->fCreatePbufferFromClientBuffer(
      LOCAL_EGL_D3D_TEXTURE_ANGLE, buffer, eglConfig, pbuffer_attribs);
  if (!mEGLSurface) {
    EGLint err = egl->mLib->fGetError();
    gfxCriticalNote << "Failed to create Pbuffer error: " << gfx::hexa(err)
                    << " Size : "
                    << LayoutDeviceIntSize(mSize.width, mSize.height);
    return;
  }

  gle->SetEGLSurfaceOverride(mEGLSurface);
}

bool DCLayerCompositionSurface::Resize(wr::DeviceIntSize aSize) {
  MOZ_ASSERT(mEGLSurface == EGL_NO_SURFACE);

  if (mSize.width == 0 || mSize.height == 0) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return false;
  }

  HRESULT hr;
  auto* dcompDevice = mDCLayerTree->GetCompositionDevice();
  const auto alphaMode =
      mIsOpaque ? DXGI_ALPHA_MODE_IGNORE : DXGI_ALPHA_MODE_PREMULTIPLIED;

  RefPtr<IDCompositionSurface> surface;
  hr = dcompDevice->CreateSurface(aSize.width, aSize.height,
                                  DXGI_FORMAT_R8G8B8A8_UNORM, alphaMode,
                                  getter_AddRefs(surface));
  if (FAILED(hr)) {
    gfxCriticalNote << "Failed to create DCompositionSurface: "
                    << gfx::hexa(hr);
    return false;
  }

  hr = mContentVisual->SetContent(surface);
  if (FAILED(hr)) {
    gfxCriticalNote << "Failed to SetContent: " << gfx::hexa(hr);
    return false;
  }

  mCompositionSurface = surface;
  mSize = aSize;
  mFirstDraw = true;
  return true;
}

void DCLayerCompositionSurface::Present(const wr::DeviceIntRect* aDirtyRects,
                                        size_t aNumDirtyRects) {
  MOZ_ASSERT(mEGLSurface);
  MOZ_ASSERT(mCompositionSurface);

  mDCSurfaceData = Nothing();

  if (!mCompositionSurface) {
    return;
  }

  mCompositionSurface->EndDraw();

  if (!mEGLSurface) {
    return;
  }

  const auto gl = mDCLayerTree->GetGLContext();
  const auto& gle = gl::GLContextEGL::Cast(gl);
  const auto& egl = gle->mEgl;

  gle->SetEGLSurfaceOverride(EGL_NO_SURFACE);

  egl->fDestroySurface(mEGLSurface);
  mEGLSurface = EGL_NO_SURFACE;
}

DCSurfaceDCompositionTextureOverlay::DCSurfaceDCompositionTextureOverlay(
    bool aIsOpaque, DCLayerTree* aDCLayerTree)
    : DCSurface(aIsOpaque, aDCLayerTree) {}

DCSurfaceDCompositionTextureOverlay::~DCSurfaceDCompositionTextureOverlay() =
    default;

void DCSurfaceDCompositionTextureOverlay::AttachExternalImage(
    wr::ExternalImageId aExternalImage) {
  auto* texture = RenderThread::Get()->GetRenderTexture(aExternalImage);
  if (!texture) {
    return;
  }
  mRenderTextureHost = texture;
}

void DCSurfaceDCompositionTextureOverlay::Present() {
  if (!mRenderTextureHost) {
    return;
  }

  // Content is not updated
  if (mPrevRenderTextureHost == mRenderTextureHost) {
    return;
  }

  const auto textureHost = mRenderTextureHost->AsRenderDXGITextureHost();

  auto start = TimeStamp::Now();
  RefPtr<IDCompositionTexture> dcompTexture =
      textureHost->GetDCompositionTexture();
  auto end = TimeStamp::Now();
  if (!dcompTexture) {
    gfxCriticalNote << "Failed to get DCompTexture";
    RenderThread::Get()->NotifyWebRenderError(
        WebRenderError::DCOMP_TEXTURE_OVERLAY);
    return;
  }

  const auto maxGetWaitDurationMs = 2;
  const auto maxSlowGetCount = 5;

  const auto getDurationMs =
      static_cast<uint32_t>((end - start).ToMilliseconds());

  if (getDurationMs > maxGetWaitDurationMs) {
    mSlowGetCount++;
  } else {
    mSlowGetCount = 0;
  }

  if (mSlowGetCount > maxSlowGetCount) {
    RenderThread::Get()->NotifyWebRenderError(
        WebRenderError::DCOMP_TEXTURE_OVERLAY);
  }

  const auto alphaMode =
      mIsOpaque ? DXGI_ALPHA_MODE_IGNORE : DXGI_ALPHA_MODE_PREMULTIPLIED;
  dcompTexture->SetAlphaMode(alphaMode);
  //  XXX
  //  dcompTexture->SetColorSpace();

  mContentVisual->SetContent(dcompTexture);
  mPrevRenderTextureHost = mRenderTextureHost;
  mDCLayerTree->SetPendingCommit();
}

DCSurfaceVideo::DCSurfaceVideo(bool aIsOpaque, DCLayerTree* aDCLayerTree)
    : DCSurface(aIsOpaque, aDCLayerTree),
      mSwapChainBufferCount(
          StaticPrefs::gfx_webrender_dcomp_video_force_triple_buffering() ? 3
                                                                          : 2) {
}

DCSurfaceVideo::~DCSurfaceVideo() {
  ReleaseDecodeSwapChainResources();
  MOZ_ASSERT(!mSwapChainSurfaceHandle);
}

bool IsYUVSwapChainFormat(DXGI_FORMAT aFormat) {
  switch (aFormat) {
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_YUY2:
      return true;
    default:
      return false;
  }
}

void DCSurfaceVideo::AttachExternalImage(wr::ExternalImageId aExternalImage) {
  auto [texture, usageInfo] =
      RenderThread::Get()->GetRenderTextureAndUsageInfo(aExternalImage);
  if (!texture) {
    gfxCriticalNoteOnce << "Failed to attach ExternalImage for extId:"
                        << AsUint64(aExternalImage);
    mRenderTextureHost = nullptr;
    return;
  }

  if (usageInfo) {
    mRenderTextureHostUsageInfo = usageInfo;
  }

  if (mPrevTexture == texture) {
    return;
  }

  // If the content is HDR, we will want to use more than 8bit. A high bit-depth
  // pixel format alone is not sufficient — 10-bit SDR content (e.g. BT.2020
  // with a non-HDR transfer function) must not be treated as HDR, otherwise
  // the compositor applies HDR tone mapping to SDR content and corrupts the
  // image. Check both the pixel format and the transfer function.
  mContentIsHDR = false;
  if (texture) {
    const auto format = texture->GetFormat();
    nsPrintfCString str("AttachExternalImage: SurfaceFormat %d", (int)format);
    PROFILER_MARKER_TEXT("DCSurfaceVideo", GRAPHICS, {}, str);
    const auto* dxgiTexture = texture->AsRenderDXGITextureHost();
    mContentIsHDR = dxgiTexture && gfx::IsHDRTransferFunction(
                                       dxgiTexture->GetTransferFunction());
  }

  // XXX if software decoded video frame format is nv12, it could be used as
  // video overlay.
  if (!texture || !texture->AsRenderDXGITextureHost() ||
      ((texture->GetFormat() != gfx::SurfaceFormat::NV12) &&
       (texture->GetFormat() != gfx::SurfaceFormat::P010) &&
       (texture->GetFormat() != gfx::SurfaceFormat::P016))) {
    gfxCriticalNote << "Unsupported RenderTexture for overlay: "
                    << gfx::hexa(texture);
    return;
  }

  mRenderTextureHost = texture;
}

bool DCSurfaceVideo::CalculateSwapChainSize(gfx::Matrix& aTransform) {
  if (!mRenderTextureHost) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return false;
  }

  const auto overlayType = mRenderTextureHost->IsSoftwareDecodedVideo()
                               ? DCompOverlayTypes::SOFTWARE_DECODED_VIDEO
                               : DCompOverlayTypes::HARDWARE_DECODED_VIDEO;
  mDCLayerTree->SetUsedOverlayTypeInFrame(overlayType);

  mVideoSize = mRenderTextureHost->AsRenderDXGITextureHost()->GetSize(0);

  // When RenderTextureHost, swapChainSize or VideoSwapChain are updated,
  // DCSurfaceVideo::PresentVideo() needs to be called.
  bool needsToPresent = mPrevTexture != mRenderTextureHost;
  gfx::IntSize swapChainSize = mVideoSize;
  gfx::Matrix transform = aTransform;
  const bool isDRM = mRenderTextureHost->IsFromDRMSource();

  // When video is rendered to axis aligned integer rectangle, video scaling
  // could be done by VideoProcessor
  bool scaleVideoAtVideoProcessor = false;
  if (StaticPrefs::gfx_webrender_dcomp_video_vp_scaling_win_AtStartup() &&
      aTransform.PreservesAxisAlignedRectangles() &&
      !aTransform.HasNegativeScaling()) {
    gfx::Size scaledSize = gfx::Size(mVideoSize) * aTransform.ScaleFactors();
    gfx::IntSize size(int32_t(std::round(scaledSize.width)),
                      int32_t(std::round(scaledSize.height)));
    if (gfx::FuzzyEqual(scaledSize.width, size.width, 0.1f) &&
        gfx::FuzzyEqual(scaledSize.height, size.height, 0.1f)) {
      scaleVideoAtVideoProcessor = true;
      swapChainSize = size;
    }
  }

  if (scaleVideoAtVideoProcessor) {
    // 4:2:2 subsampled formats like YUY2 must have an even width, and 4:2:0
    // subsampled formats like NV12 must have an even width and height.
    if (swapChainSize.width % 2 == 1) {
      swapChainSize.width += 1;
    }
    if (swapChainSize.height % 2 == 1) {
      swapChainSize.height += 1;
    }
    transform = gfx::Matrix::Translation(aTransform.GetTranslation());
  }

  if (!mDCLayerTree->EnsureVideoProcessor(mVideoSize, swapChainSize)) {
    gfxCriticalNote << "EnsureVideoProcessor Failed";
    return false;
  }

  MOZ_ASSERT(mDCLayerTree->GetVideoContext());
  MOZ_ASSERT(mDCLayerTree->GetVideoProcessor());

  const UINT vendorId = GetVendorId(mDCLayerTree->GetVideoDevice());
  const bool driverSupportsAutoHDR =
      GetVpAutoHDRSupported(vendorId, mDCLayerTree->GetVideoContext(),
                            mDCLayerTree->GetVideoProcessor());
  const bool contentIsHDR = mContentIsHDR;
  const bool monitorIsHDR =
      gfx::DeviceManagerDx::Get()->WindowHDREnabled(mDCLayerTree->GetHwnd());
  const bool powerIsCharging = RenderThread::Get()->GetPowerIsCharging();

  bool useVpAutoHDR = gfx::gfxVars::WebRenderOverlayVpAutoHDR() &&
                      !contentIsHDR && monitorIsHDR && driverSupportsAutoHDR &&
                      powerIsCharging && !mVpAutoHDRFailed;

  bool useHDR = gfx::gfxVars::WebRenderOverlayHDR() && contentIsHDR;
  // We can rely on the Desktop Window Manager (DWM) to handle RGB10A2 format
  // swapchains with BT2100 PQ color space, even if hardware overlays are not
  // supported it will convert for us, it also guarantees support for scRGB in
  // RGBA16F format but that uses twice the bandwidth and VideoProcessor is not
  // required to implement conversion from YCBCR BT2100 PQ to scRGB RGBA16F, so
  // some drivers can't do that and we should stick to RGB10A2 where possible.
  bool useHDRRGB10A2 = useHDR;
  bool useHDRRGBA16F = false;

  if (profiler_thread_is_being_profiled_for_markers()) {
    nsPrintfCString str(
        "useVpAutoHDR %d gfxVars %d contentIsHDR %d monitor %d driver %d "
        "charging %d failed %d",
        useVpAutoHDR, gfx::gfxVars::WebRenderOverlayVpAutoHDR(), contentIsHDR,
        monitorIsHDR, driverSupportsAutoHDR, powerIsCharging, mVpAutoHDRFailed);
    PROFILER_MARKER_TEXT("DCSurfaceVideo", GRAPHICS, {}, str);
  }

  if (!mVideoSwapChain || mSwapChainSize != swapChainSize || mIsDRM != isDRM ||
      mUseVpAutoHDR != useVpAutoHDR) {
    needsToPresent = true;
    ReleaseDecodeSwapChainResources();
    // Update mSwapChainSize before creating SwapChain
    mSwapChainSize = swapChainSize;
    mIsDRM = isDRM;

    auto swapChainFormat =
        GetSwapChainFormat(useVpAutoHDR, useHDRRGB10A2, useHDRRGBA16F);
    bool useYUVSwapChain = IsYUVSwapChainFormat(swapChainFormat);
    if (useYUVSwapChain) {
      // Tries to create YUV SwapChain
      nsPrintfCString str("Creating video swapchain for YUV as DXGI format %d",
                          (int)swapChainFormat);
      PROFILER_MARKER_TEXT("DCSurfaceVideo", GRAPHICS, {}, str);
      CreateVideoSwapChain(swapChainFormat);
      if (!mVideoSwapChain) {
        mFailedYuvSwapChain = true;
        ReleaseDecodeSwapChainResources();

        gfxCriticalNote << "Fallback to RGB SwapChain";
      }
    }
    // Tries to create RGB SwapChain
    if (!mVideoSwapChain) {
      nsPrintfCString str("Creating video swapchain for RGB as DXGI format %d",
                          (int)swapChainFormat);
      PROFILER_MARKER_TEXT("DCSurfaceVideo", GRAPHICS, {}, str);
      CreateVideoSwapChain(swapChainFormat);
    }
    if (!mVideoSwapChain && useVpAutoHDR) {
      mVpAutoHDRFailed = true;
      gfxCriticalNoteOnce << "Failed to create video SwapChain for VpAutoHDR";

      // Disable VpAutoHDR
      useVpAutoHDR = false;
      swapChainFormat =
          GetSwapChainFormat(useVpAutoHDR, useHDRRGB10A2, useHDRRGBA16F);
      nsPrintfCString str(
          "Creating video swapchain for RGB as DXGI format %d after fallback "
          "from VpAutoHDR",
          (int)swapChainFormat);
      PROFILER_MARKER_TEXT("DCSurfaceVideo", GRAPHICS, {}, str);
      CreateVideoSwapChain(swapChainFormat);
    }
  }

  aTransform = transform;
  mUseVpAutoHDR = useVpAutoHDR;
  mUseHDR = useHDR;

  return needsToPresent;
}

void DCSurfaceVideo::PresentVideo() {
  if (!mRenderTextureHost) {
    return;
  }

  if (!mVideoSwapChain) {
    gfxCriticalNote << "Failed to create VideoSwapChain";
    RenderThread::Get()->NotifyWebRenderError(
        wr::WebRenderError::VIDEO_OVERLAY);
    return;
  }

  if (!CallVideoProcessorBlt()) {
    bool useYUVSwapChain = IsYUVSwapChainFormat(mSwapChainFormat);
    if (useYUVSwapChain) {
      mFailedYuvSwapChain = true;
      ReleaseDecodeSwapChainResources();
      return;
    }
    RenderThread::Get()->NotifyWebRenderError(
        wr::WebRenderError::VIDEO_OVERLAY);
    return;
  }

  const auto device = mDCLayerTree->GetDevice();
  HRESULT hr;

  auto start = TimeStamp::Now();
  if (mFirstPresent) {
    mFirstPresent = false;
    UINT flags = DXGI_PRESENT_USE_DURATION;
    // DirectComposition can display black for a swap chain between the first
    // and second time it's presented to - maybe the first Present can get lost
    // somehow and it shows the wrong buffer. In that case copy the buffers so
    // all have the correct contents, which seems to help. The first Present()
    // after this needs to have SyncInterval > 0, or else the workaround doesn't
    // help.
    for (size_t i = 0; i < mSwapChainBufferCount - 1; ++i) {
      hr = mVideoSwapChain->Present(0, flags);
      // Ignore DXGI_STATUS_OCCLUDED since that's not an error but only
      // indicates that the window is occluded and we can stop rendering.
      if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) {
        gfxCriticalNoteOnce << "video Present failed during first present: "
                            << gfx::hexa(hr);
        return;
      }

      RefPtr<ID3D11Texture2D> destTexture;
      mVideoSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                 (void**)getter_AddRefs(destTexture));
      MOZ_ASSERT(destTexture);
      RefPtr<ID3D11Texture2D> srcTexture;
      hr = mVideoSwapChain->GetBuffer(1, __uuidof(ID3D11Texture2D),
                                      (void**)getter_AddRefs(srcTexture));
      MOZ_ASSERT(srcTexture);
      RefPtr<ID3D11DeviceContext> context;
      device->GetImmediateContext(getter_AddRefs(context));
      MOZ_ASSERT(context);
      context->CopyResource(destTexture, srcTexture);
    }

    // Additionally wait for the GPU to finish executing its commands, or
    // there still may be a black flicker when presenting expensive content
    // (e.g. 4k video).

    RefPtr<IDXGIDevice2> dxgiDevice2;
    device->QueryInterface((IDXGIDevice2**)getter_AddRefs(dxgiDevice2));
    MOZ_ASSERT(dxgiDevice2);

    HANDLE event = ::CreateEvent(nullptr, false, false, nullptr);
    hr = dxgiDevice2->EnqueueSetEvent(event);
    if (SUCCEEDED(hr)) {
      DebugOnly<DWORD> result = ::WaitForSingleObject(event, INFINITE);
      MOZ_ASSERT(result == WAIT_OBJECT_0);
    } else {
      gfxCriticalNoteOnce << "EnqueueSetEvent failed: " << gfx::hexa(hr);
    }
    ::CloseHandle(event);
  }

  UINT flags = DXGI_PRESENT_USE_DURATION;
  UINT interval = 1;
  if (StaticPrefs::gfx_webrender_dcomp_video_swap_chain_present_interval_0()) {
    interval = 0;
  }

  hr = mVideoSwapChain->Present(interval, flags);
  auto end = TimeStamp::Now();

  if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) {
    gfxCriticalNoteOnce << "video Present failed: " << gfx::hexa(hr);
  }

  mPrevTexture = mRenderTextureHost;

  // Disable video overlay if mVideoSwapChain->Present() is too slow. It drops
  // fps.

  if (!StaticPrefs::gfx_webrender_dcomp_video_check_slow_present()) {
    return;
  }

  const auto presentDurationMs =
      static_cast<uint32_t>((end - start).ToMilliseconds());
  const auto overlayType = mRenderTextureHost->IsSoftwareDecodedVideo()
                               ? DCompOverlayTypes::SOFTWARE_DECODED_VIDEO
                               : DCompOverlayTypes::HARDWARE_DECODED_VIDEO;

  nsPrintfCString marker("PresentWait overlay %u %ums ", (uint8_t)overlayType,
                         presentDurationMs);
  PROFILER_MARKER_TEXT("PresentWait", GRAPHICS, {}, marker);

  // RenderTextureHostUsageInfo::OnVideoPresent() is called to disable video
  // overlay if present is slow. However, HDR requires video overlay to show
  // correct colors. To avoid displaying incorrect color, don't make this call
  // if the content is HDR.
  if (!mContentIsHDR && mRenderTextureHostUsageInfo) {
    mRenderTextureHostUsageInfo->OnVideoPresent(mDCLayerTree->GetFrameId(),
                                                presentDurationMs);
  }
}

void DCSurfaceVideo::OnCompositorEndFrame(int aFrameId, uint32_t aDurationMs) {
  if (!mRenderTextureHostUsageInfo) {
    return;
  }
  mRenderTextureHostUsageInfo->OnCompositorEndFrame(aFrameId, aDurationMs);
}

DXGI_FORMAT DCSurfaceVideo::GetSwapChainFormat(bool aUseVpAutoHDR,
                                               bool aUseRGB10A2,
                                               bool aUseRGBA16F) {
  if (aUseVpAutoHDR) {
    return DXGI_FORMAT_R10G10B10A2_UNORM;
  }
  if (aUseRGB10A2) {
    return DXGI_FORMAT_R10G10B10A2_UNORM;
  }
  if (aUseRGBA16F) {
    return DXGI_FORMAT_R16G16B16A16_FLOAT;
  }
  if (mFailedYuvSwapChain || !mDCLayerTree->SupportsHardwareOverlays()) {
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  }
  return mDCLayerTree->GetOverlayFormatForSDR();
}

bool DCSurfaceVideo::CreateVideoSwapChain(DXGI_FORMAT aSwapChainFormat) {
  MOZ_ASSERT(mRenderTextureHost);

  mFirstPresent = true;

  const auto device = mDCLayerTree->GetDevice();

  RefPtr<IDXGIDevice> dxgiDevice;
  device->QueryInterface((IDXGIDevice**)getter_AddRefs(dxgiDevice));

  RefPtr<IDXGIFactoryMedia> dxgiFactoryMedia;
  {
    RefPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(getter_AddRefs(adapter));
    adapter->GetParent(
        IID_PPV_ARGS((IDXGIFactoryMedia**)getter_AddRefs(dxgiFactoryMedia)));
  }

  mSwapChainSurfaceHandle = gfx::DeviceManagerDx::CreateDCompSurfaceHandle();
  if (!mSwapChainSurfaceHandle) {
    gfxCriticalNote << "Failed to create DCompSurfaceHandle";
    return false;
  }

  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Width = mSwapChainSize.width;
  desc.Height = mSwapChainSize.height;
  desc.Format = aSwapChainFormat;
  desc.Stereo = FALSE;
  desc.SampleDesc.Count = 1;
  desc.BufferCount = mSwapChainBufferCount;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.Flags = DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO;
  if (IsYUVSwapChainFormat(aSwapChainFormat)) {
    desc.Flags |= DXGI_SWAP_CHAIN_FLAG_YUV_VIDEO;
  }
  if (mIsDRM) {
    desc.Flags |= DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY;
  }
  desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

  HRESULT hr;
  hr = dxgiFactoryMedia->CreateSwapChainForCompositionSurfaceHandle(
      device, mSwapChainSurfaceHandle, &desc, nullptr,
      getter_AddRefs(mVideoSwapChain));

  if (FAILED(hr)) {
    gfxCriticalNote << "Failed to create video SwapChain: " << gfx::hexa(hr)
                    << " " << mSwapChainSize;
    return false;
  }

  mSwapChainFormat = aSwapChainFormat;
  mContentVisual->SetContent(mVideoSwapChain);
  return true;
}

static Maybe<DXGI_COLOR_SPACE_TYPE> GetSourceDXGIColorSpace(
    const gfx::YUVColorSpace aYUVColorSpace, const gfx::ColorRange aColorRange,
    const gfx::TransferFunction aTransferFunction) {
  switch (aYUVColorSpace) {
    case gfx::YUVColorSpace::BT601:
      // https://en.wikipedia.org/wiki/Rec._601 - this is the NTSC and SECAM/PAL
      // color spaces
      if (aTransferFunction != gfx::TransferFunction::BT709) {
        gfxCriticalNoteOnce
            << "GetSourceDXGIColorSpace: Unhandled transfer function "
            << static_cast<int>(aTransferFunction)
            << " for BT601, treating as BT709 transfer function";
      }
      switch (aColorRange) {
        case gfx::ColorRange::FULL:
          return Some(DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601);
        case gfx::ColorRange::LIMITED:
          return Some(DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601);
      }
      gfxCriticalNoteOnce << "GetSourceDXGIColorSpace: Unhandled color range "
                          << static_cast<int>(aColorRange) << " for BT601";
      return Nothing();
    case gfx::YUVColorSpace::Identity:
      gfxCriticalNoteOnce
          << "GetSourceDXGIColorSpace: Unhandled YUV color space "
          << static_cast<int>(aYUVColorSpace)
          << ", treating as BT709 color space";
      FMT_FALLTHROUGH;
    case gfx::YUVColorSpace::BT709:
      // https://en.wikipedia.org/wiki/Rec._709 - this is the HDTV color space
      if (aTransferFunction != gfx::TransferFunction::BT709) {
        gfxCriticalNoteOnce
            << "GetSourceDXGIColorSpace: Unhandled transfer function "
            << static_cast<int>(aTransferFunction)
            << " for BT709, treating as BT709 transfer function";
      }
      switch (aColorRange) {
        case gfx::ColorRange::FULL:
          return Some(DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709);
        case gfx::ColorRange::LIMITED:
          return Some(DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
      }
      gfxCriticalNoteOnce << "GetSourceDXGIColorSpace: Unhandled color range "
                          << static_cast<int>(aColorRange) << " for BT709";
      return Nothing();
    case gfx::YUVColorSpace::BT2020:
      // https://en.wikipedia.org/wiki/Rec._2020 - this is the UHDTV color space
      if (!gfxPlatform::UseHDR()) {
        // This pref being off mimics legacy behavior, it's wrong but it's
        // precisely what we did before, looks washed out if it's PQ.
        switch (aColorRange) {
          case gfx::ColorRange::FULL:
            return Some(DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020);
          case gfx::ColorRange::LIMITED:
            return Some(DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020);
        }
        gfxCriticalNoteOnce << "GetSourceDXGIColorSpace: Unhandled color range "
                            << static_cast<int>(aColorRange) << " for BT2020";
        return Nothing();
      }
      switch (aTransferFunction) {
        case gfx::TransferFunction::SRGB:
        case gfx::TransferFunction::LINEAR:
          // Almost certainly never used, but cover all switch cases to support
          // the compiler warning if any are added later.
          gfxCriticalNoteOnce
              << "GetSourceDXGIColorSpace: DXGI has no support for "
              << static_cast<int>(aTransferFunction)
              << " transfer function for YCBCR content, treating as BT2020 "
                 "transfer function";
          FMT_FALLTHROUGH;
        case gfx::TransferFunction::BT709:
          // BT2020 defines a transfer function that is almost identical to
          // BT709 + BT1886, so this refers to BT2020 transfer function.
          // https://en.wikipedia.org/wiki/Rec._2020
          switch (aColorRange) {
            case gfx::ColorRange::FULL:
              return Some(DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020);
            case gfx::ColorRange::LIMITED:
              return Some(DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020);
          }
          gfxCriticalNoteOnce
              << "GetSourceDXGIColorSpace: Unhandled color range "
              << static_cast<int>(aColorRange) << " for BT2020";
          return Nothing();
        case gfx::TransferFunction::PQ:
          // This is an HDR video transfer function, needs 10bit (HDR10) to
          // avoid being lower quality than BT709 over the SDR range.
          // https://en.wikipedia.org/wiki/Perceptual_quantizer
          switch (aColorRange) {
            case gfx::ColorRange::FULL:
              gfxCriticalNoteOnce
                  << "GetSourceDXGIColorSpace: DXGI has no support for PQ "
                     "transfer function with full color range for BT2020 "
                     "content, treating as studio range";
              return Some(DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020);
            case gfx::ColorRange::LIMITED:
              return Some(DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020);
          }
          gfxCriticalNoteOnce
              << "GetSourceDXGIColorSpace: Unhandled color range "
              << static_cast<int>(aColorRange) << " for BT2020";
          return Nothing();
        case gfx::TransferFunction::HLG:
          // This is an HDR video transfer function, does not strictly require
          // 10bit but certainly benefits from it.
          // https://en.wikipedia.org/wiki/Hybrid_log%E2%80%93gamma
          switch (aColorRange) {
            case gfx::ColorRange::FULL:
              return Some(DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020);
            case gfx::ColorRange::LIMITED:
              return Some(DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020);
          }
          gfxCriticalNoteOnce
              << "GetSourceDXGIColorSpace: Unhandled color range "
              << static_cast<int>(aColorRange) << " for BT2020";
          return Nothing();
      }
      gfxCriticalNoteOnce
          << "GetSourceDXGIColorSpace: Unhandled transfer function "
          << static_cast<int>(aTransferFunction) << " for BT2020";
      return Nothing();
  }

  return Nothing();
}

static Maybe<DXGI_COLOR_SPACE_TYPE> GetSourceDXGIColorSpace(
    const gfx::YUVRangedColorSpace aYUVColorSpace) {
  const auto info = FromYUVRangedColorSpace(aYUVColorSpace);
  return GetSourceDXGIColorSpace(info.space, info.range, info.transferFunction);
}

static Maybe<DXGI_COLOR_SPACE_TYPE> GetOutputDXGIColorSpace(
    DXGI_FORMAT aSwapChainFormat, DXGI_COLOR_SPACE_TYPE aInputColorSpace) {
  switch (aSwapChainFormat) {
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_YUY2:
      return Some(aInputColorSpace);
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
      return Some(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
      return Some(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
    case DXGI_FORMAT_R10G10B10A2_UNORM:
      switch (aInputColorSpace) {
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020:
          return Some(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
        case DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020:
          return Some(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
        case DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:
          return Some(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
        default:
          return Some(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020);
      }
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
      return Some(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    default:
      return Nothing();
  }
}

static DXGI_HDR_METADATA_HDR10 ToStreamHDR10Metadata(
    const gfx::HDRMetadata& aMetadata) {
  constexpr float kChromaticityScale = 50000.0f;
  constexpr float kMinLuminanceScale = 10000.0f;
  DXGI_HDR_METADATA_HDR10 hdr10 = {};
  if (const auto& smpte = aMetadata.mSmpte2086) {
    hdr10.RedPrimary[0] =
        static_cast<UINT16>(smpte->displayPrimaryRed.x * kChromaticityScale);
    hdr10.RedPrimary[1] =
        static_cast<UINT16>(smpte->displayPrimaryRed.y * kChromaticityScale);
    hdr10.GreenPrimary[0] =
        static_cast<UINT16>(smpte->displayPrimaryGreen.x * kChromaticityScale);
    hdr10.GreenPrimary[1] =
        static_cast<UINT16>(smpte->displayPrimaryGreen.y * kChromaticityScale);
    hdr10.BluePrimary[0] =
        static_cast<UINT16>(smpte->displayPrimaryBlue.x * kChromaticityScale);
    hdr10.BluePrimary[1] =
        static_cast<UINT16>(smpte->displayPrimaryBlue.y * kChromaticityScale);
    hdr10.WhitePoint[0] =
        static_cast<UINT16>(smpte->whitePoint.x * kChromaticityScale);
    hdr10.WhitePoint[1] =
        static_cast<UINT16>(smpte->whitePoint.y * kChromaticityScale);
    hdr10.MaxMasteringLuminance = static_cast<UINT>(smpte->maxLuminance);
    hdr10.MinMasteringLuminance =
        static_cast<UINT>(smpte->minLuminance * kMinLuminanceScale);
  }
  if (const auto& cll = aMetadata.mContentLightLevel) {
    hdr10.MaxContentLightLevel = cll->maxContentLightLevel;
    hdr10.MaxFrameAverageLightLevel = cll->maxFrameAverageLightLevel;
  }
  return hdr10;
}

static const char kShaderBltYUVHLGToRGBPQ_VS[] = R"(
  struct VS_INPUT {
    float4 position : POSITION;
    float2 texCoord : TEXCOORD0;
  };

  struct PS_INPUT {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
  };

  PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;
    output.position = input.position;
    output.texCoord = input.texCoord;
    return output;
  }
)";

static constexpr uint32_t MODE_BT709 = 0x00;
static constexpr uint32_t MODE_PQ = 0x01;
static constexpr uint32_t MODE_HLG = 0x02;

struct psConstants {
  // Converts from encoded YUV to encoded RGB (same transfer function).
  color::mat4 yuvToRgbMatrix;
  // Converts from one color space to another (e.g. BT.2020 to BT.709).
  color::mat4 colorSpaceMatrix;
  // Tonemapping parameters for Reinhard tonemapping curve.
  // Based on equation 4 in
  // "Photographic Tone Reproduction for Digital Images" by Reinhard et al
  // https://www-old.cs.utah.edu/docs/techreports/2002/pdf/UUCS-02-001.pdf
  float tonemapping[2];
  // Conversion factor between display-referred HLG and scene-referred PQ.
  float pqMultiplier;
  // Which conversion mode to use, see MODE_* enum values above.
  uint32_t inputMode;
  uint32_t outputMode;
  // align the size of this struct to 16 bytes for HLSL constant buffer packing
  // rules.
  uint32_t padding[3];
};

static const char kShaderBltYUVHLGToRGBPQ_PS[] = R"(
  Texture2D texY : register(t0);
  Texture2D texUV : register(t1);
  SamplerState samplerLinear : register(s0);

  static const uint MODE_BT709 = 0;
  static const uint MODE_PQ = 1;
  static const uint MODE_HLG = 2;

  cbuffer Constants : register(b0) {
    float4x4 yuvToRgbMatrix;
    float4x4 colorSpaceMatrix;
    float2 tonemapping;
    float pqMultiplier;
    uint inputMode;
    uint outputMode;
  };

  struct PS_INPUT {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
  };

  float3 BT709toLinear(float3 rgb) {
    // EOTF for BT.709
    return lerp((rgb / 4.5f), pow((rgb + 0.099f) / 1.099f, 1.0f / 0.45f), step(0.081f, rgb));
  }

  float3 HLGtoLinear(float3 rgb) {
    // EOTF for HLG (ITU-R BT.2100)
    const float a = 0.17883277f;
    const float b = 0.28466892f;
    const float c = 0.55991073f;

    return lerp((rgb * rgb) * 4.0f, exp((rgb - c) / a) + b, step(0.5f, rgb));
  }

  float3 PQToLinear(float3 rgb) {
    // EOTF for PQ (ST.2084)
    const float m1 = 0.1593017578f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;

    float3 y = pow(max(rgb, 0.0f), 1.0f / m2);
    float3 l = pow(max((y - c1) / (c2 - c3 * y), 0.0f), 1.0f / m1) * (1.0f / pqMultiplier);
    return l;
  }

  float3 LinearToPQ(float3 rgb) {
    // OETF / Inverse EOTF for PQ (ST.2084)
    const float m1 = 0.1593017578f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;

    float3 y = pow(max(rgb, 0.0f) * pqMultiplier, m1);
    float3 pq = pow((c1 + c2 * y) / (1.0f + c3 * y), m2);
    return pq;
  }

  float3 LinearToBT709(float3 rgb) {
    // EOTF for BT.709
    return lerp((rgb * 4.5f), 1.099f * pow(rgb, 0.45f) - 0.099f, step(0.018f, rgb));
  }

  float3 tonemap(float3 color, float a, float b) {
    float m = max(color.x, max(color.y, color.z));
    return color * (1.0f + a * m) / (1.0f + b * m);
  }

  float4 main(PS_INPUT input) : SV_TARGET {
    float2 uv = input.texCoord;

    float y = texY.Sample(samplerLinear, uv).x;
    float2 chroma = texUV.Sample(samplerLinear, uv).xy;

    float3 srcYUV = float3(y, chroma.x, chroma.y);

    float3 srcRGB = mul(float4(srcYUV, 1.0), yuvToRgbMatrix).xyz;
    // Clamp to avoid negative values for narrow range YUV to RGB conversion.
    srcRGB = max(srcRGB, 0.0f);

    float3 srcLinearRGB;
    switch(inputMode) {
      default:
      case MODE_BT709:
        srcLinearRGB = BT709toLinear(srcRGB);
        break;
      case MODE_HLG:
        srcLinearRGB = HLGtoLinear(srcRGB);
        break;
      case MODE_PQ:
        srcLinearRGB = PQToLinear(srcRGB);
        break;
    }

    float3 dstLinearRGB = mul(colorSpaceMatrix, float4(srcLinearRGB, 1.0)).xyz;

    float3 dstTonemappedRGB = tonemap(dstLinearRGB, tonemapping.x, tonemapping.y);

    float3 dstRGB;
    switch(outputMode) {
      default:
      case MODE_PQ:
        dstRGB = LinearToPQ(dstTonemappedRGB);
        break;
      case MODE_BT709:
        dstRGB = LinearToBT709(dstTonemappedRGB);
        break;
    }

    return float4(dstRGB, 1.0f);
  }
)";

bool DCSurfaceVideo::ShaderBltSetup() {
  // Compile the pixel shader
  HRESULT hr;
  const auto device = mDCLayerTree->GetDevice();
  if (!mShaderBltVSBlob) {
    RefPtr<ID3DBlob> errorBlob;
    hr = D3DCompile(
        kShaderBltYUVHLGToRGBPQ_VS, strlen(kShaderBltYUVHLGToRGBPQ_VS), nullptr,
        nullptr, nullptr, "main", "vs_5_0", 0, 0,
        getter_AddRefs(mShaderBltVSBlob), getter_AddRefs(errorBlob));
    if (FAILED(hr)) {
      if (errorBlob) {
        gfxCriticalNoteOnce
            << "Vertex shader compilation error: "
            << static_cast<const char*>(errorBlob->GetBufferPointer());
      } else {
        gfxCriticalNoteOnce << "Failed to compile vertex shader: hresult="
                            << gfx::hexa(hr);
      }
      return false;
    }
    hr = device->CreateVertexShader(mShaderBltVSBlob->GetBufferPointer(),
                                    mShaderBltVSBlob->GetBufferSize(), nullptr,
                                    getter_AddRefs(mShaderBltVertexShader));
    if (FAILED(hr)) {
      gfxCriticalNoteOnce << "Failed to create vertex shader: hresult="
                          << gfx::hexa(hr);
      return false;
    }
  }

  if (!mShaderBltPSBlob) {
    RefPtr<ID3DBlob> errorBlob;
    hr = D3DCompile(
        kShaderBltYUVHLGToRGBPQ_PS, strlen(kShaderBltYUVHLGToRGBPQ_PS), nullptr,
        nullptr, nullptr, "main", "ps_5_0", 0, 0,
        getter_AddRefs(mShaderBltPSBlob), getter_AddRefs(errorBlob));
    if (FAILED(hr)) {
      if (errorBlob) {
        gfxCriticalNoteOnce
            << "Pixel shader compilation error: "
            << static_cast<const char*>(errorBlob->GetBufferPointer());
      } else {
        gfxCriticalNoteOnce << "Failed to compile pixel shader: hresult="
                            << gfx::hexa(hr);
      }
      return false;
    }
    hr = device->CreatePixelShader(mShaderBltPSBlob->GetBufferPointer(),
                                   mShaderBltPSBlob->GetBufferSize(), nullptr,
                                   getter_AddRefs(mShaderBltPixelShader));
    if (FAILED(hr)) {
      gfxCriticalNoteOnce << "Failed to create pixel shader: hresult="
                          << gfx::hexa(hr);
      return false;
    }
  }

  if (!mShaderBltIndexBuffer) {
    uint16_t vsIndices[] = {0, 1, 2, 0, 2, 3};
    D3D11_BUFFER_DESC indexBufferDesc = {};
    indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    indexBufferDesc.ByteWidth = sizeof(vsIndices);
    indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA indexInitData = {};
    indexInitData.pSysMem = vsIndices;
    hr = device->CreateBuffer(&indexBufferDesc, &indexInitData,
                              getter_AddRefs(mShaderBltIndexBuffer));
    if (FAILED(hr)) {
      gfxCriticalNoteOnce << "Failed to create index buffer";
      return false;
    }
  }

  if (!mShaderBltVertexBuffer) {
    D3D11_BUFFER_DESC vsBufferDesc = {};
    vsBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    vsBufferDesc.ByteWidth = sizeof(float[4][6]);
    vsBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    hr = device->CreateBuffer(&vsBufferDesc, nullptr,
                              getter_AddRefs(mShaderBltVertexBuffer));
    if (FAILED(hr)) {
      gfxCriticalNoteOnce << "Failed to create vertex buffer";
      return false;
    }
  }

  if (!mShaderBltInputLayout) {
    // Set input layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16,
         D3D11_INPUT_PER_VERTEX_DATA, 0}};
    hr = device->CreateInputLayout(layout, 2,
                                   mShaderBltVSBlob->GetBufferPointer(),
                                   mShaderBltVSBlob->GetBufferSize(),
                                   getter_AddRefs(mShaderBltInputLayout));
    if (FAILED(hr)) {
      gfxCriticalNoteOnce << "Failed to create input layout";
      return false;
    }
  }

  if (!mShaderBltRasterizerState) {
    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    hr = device->CreateRasterizerState(
        &rasterizerDesc, getter_AddRefs(mShaderBltRasterizerState));
    if (FAILED(hr)) {
      gfxCriticalNoteOnce << "Failed to create rasterizer state";
      return false;
    }
  }

  if (!mShaderBltBlendState) {
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device->CreateBlendState(&blendDesc,
                                  getter_AddRefs(mShaderBltBlendState));
    if (FAILED(hr)) {
      gfxCriticalNoteOnce << "Failed to create blend state";
      return false;
    }
  }

  if (!mShaderBltConstantBuffer) {
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.ByteWidth = sizeof(psConstants);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = device->CreateBuffer(&cbDesc, nullptr,
                              getter_AddRefs(mShaderBltConstantBuffer));
    if (FAILED(hr)) {
      gfxCriticalNoteOnce << "Failed to create constant buffer";
      return false;
    }
  }

  if (!mShaderBltSamplerState) {
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    hr = device->CreateSamplerState(&samplerDesc,
                                    getter_AddRefs(mShaderBltSamplerState));
    if (FAILED(hr)) {
      gfxCriticalNoteOnce << "Failed to create sampler state";
      return false;
    }
  }

  return true;
}

class SavedD3D11State {
  RefPtr<ID3D11DeviceContext> context;
  RefPtr<ID3D11Buffer> vertexBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
  UINT vertexBufferStrides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
  UINT vertexBufferOffsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
  RefPtr<ID3D11InputLayout> inputLayout;
  RefPtr<ID3D11Buffer> indexBuffer;
  DXGI_FORMAT indexBufferFormat;
  UINT indexBufferOffset;
  D3D11_PRIMITIVE_TOPOLOGY primitiveTopology;
  RefPtr<ID3D11VertexShader> vertexShader;
  RefPtr<ID3D11ClassInstance>
      vertexShaderClassInstances[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
  UINT vertexShaderClassInstanceCount;
  RefPtr<ID3D11ShaderResourceView>
      vertexShaderResources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
  RefPtr<ID3D11Buffer>
      vertexConstantBuffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
  RefPtr<ID3D11PixelShader> pixelShader;
  RefPtr<ID3D11ClassInstance>
      pixelShaderClassInstances[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
  UINT pixelShaderClassInstanceCount;
  RefPtr<ID3D11ShaderResourceView>
      pixelShaderResources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
  RefPtr<ID3D11Buffer>
      pixelConstantBuffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
  RefPtr<ID3D11SamplerState>
      pixelSamplerStates[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
  UINT viewportsCount = 0;
  D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX + 1];
  UINT scissorRectsCount = 0;
  D3D11_RECT scissorRects[D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX + 1];
  RefPtr<ID3D11RasterizerState> rasterizerState;
  RefPtr<ID3D11RenderTargetView>
      renderTargetViews[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
  RefPtr<ID3D11DepthStencilView> depthStencilView;
  RefPtr<ID3D11DepthStencilState> depthStencilState;
  UINT stencilRef;
  RefPtr<ID3D11BlendState> blendState;
  FLOAT blendFactors[4];
  UINT sampleMask;

 public:
  explicit SavedD3D11State(RefPtr<ID3D11DeviceContext> _context) {
    AUTO_PROFILER_MARKER("ShaderBlt::SavedD3D11State begin", GRAPHICS);
    context = _context;
    ID3D11Buffer*
        savedVertexBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11Buffer* savedVertexConstantBuffers
        [D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ID3D11ClassInstance* savedVertexClassInstances
        [D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11ShaderResourceView* savedVertexShaderResources
        [D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11Buffer* savedPixelConstantBuffers
        [D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ID3D11ClassInstance* savedPixelShaderClassInstances
        [D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11ShaderResourceView* savedPixelShaderResources
        [D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11SamplerState*
        savedPixelSamplerStates[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    ID3D11RenderTargetView*
        savedRenderTargetViews[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};

    context->IAGetInputLayout(getter_AddRefs(inputLayout));
    context->IAGetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,
                                savedVertexBuffers, vertexBufferStrides,
                                vertexBufferOffsets);
    context->IAGetIndexBuffer(getter_AddRefs(indexBuffer), &indexBufferFormat,
                              &indexBufferOffset);
    context->IAGetPrimitiveTopology(&primitiveTopology);
    context->VSGetShader(getter_AddRefs(vertexShader),
                         savedVertexClassInstances,
                         &vertexShaderClassInstanceCount);
    context->VSGetConstantBuffers(
        0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
        savedVertexConstantBuffers);
    context->VSGetShaderResources(0,
                                  D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,
                                  savedVertexShaderResources);
    context->PSGetShader(getter_AddRefs(pixelShader),
                         savedPixelShaderClassInstances,
                         &pixelShaderClassInstanceCount);
    context->PSGetConstantBuffers(
        0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
        savedPixelConstantBuffers);
    context->PSGetShaderResources(0,
                                  D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,
                                  savedPixelShaderResources);
    context->PSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT,
                           savedPixelSamplerStates);
    context->RSGetViewports(&viewportsCount, viewports);
    context->RSGetScissorRects(&scissorRectsCount, scissorRects);
    context->RSGetState(getter_AddRefs(rasterizerState));
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                savedRenderTargetViews,
                                getter_AddRefs(depthStencilView));
    context->OMGetBlendState(getter_AddRefs(blendState), blendFactors,
                             &sampleMask);
    context->OMGetDepthStencilState(getter_AddRefs(depthStencilState),
                                    &stencilRef);

    for (UINT i = 0; i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i) {
      vertexBuffers[i] = dont_AddRef(savedVertexBuffers[i]);
    }
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i) {
      vertexShaderClassInstances[i] = dont_AddRef(savedVertexClassInstances[i]);
    }
    for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
         ++i) {
      vertexConstantBuffers[i] = dont_AddRef(savedVertexConstantBuffers[i]);
    }
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i) {
      vertexShaderResources[i] = dont_AddRef(savedVertexShaderResources[i]);
    }
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i) {
      pixelShaderClassInstances[i] =
          dont_AddRef(savedPixelShaderClassInstances[i]);
    }
    for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
         ++i) {
      pixelConstantBuffers[i] = dont_AddRef(savedPixelConstantBuffers[i]);
    }
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i) {
      pixelShaderResources[i] = dont_AddRef(savedPixelShaderResources[i]);
    }
    for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i) {
      pixelSamplerStates[i] = dont_AddRef(savedPixelSamplerStates[i]);
    }
    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
      renderTargetViews[i] = dont_AddRef(savedRenderTargetViews[i]);
    }
  }

  ~SavedD3D11State() {
    AUTO_PROFILER_MARKER("ShaderBlt::SavedD3D11State end", GRAPHICS);
    ID3D11Buffer*
        savedVertexBuffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11Buffer* savedVertexConstantBuffers
        [D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ID3D11ClassInstance* savedVertexShaderClassInstances
        [D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11ShaderResourceView* savedVertexShaderResources
        [D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11Buffer* savedPixelConstantBuffers
        [D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ID3D11ClassInstance* savedPixelShaderClassInstances
        [D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11ShaderResourceView* savedPixelShaderResources
        [D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11SamplerState*
        savedPixelSamplerStates[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    ID3D11RenderTargetView*
        savedRenderTargetViews[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};

    for (UINT i = 0; i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i) {
      savedVertexBuffers[i] = vertexBuffers[i];
    }
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i) {
      savedVertexShaderClassInstances[i] = vertexShaderClassInstances[i];
    }
    for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
         ++i) {
      savedVertexConstantBuffers[i] = vertexConstantBuffers[i];
    }
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i) {
      savedVertexShaderResources[i] = vertexShaderResources[i];
    }
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i) {
      savedPixelShaderClassInstances[i] = pixelShaderClassInstances[i];
    }
    for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
         ++i) {
      savedPixelConstantBuffers[i] = pixelConstantBuffers[i];
    }
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i) {
      savedPixelShaderResources[i] = pixelShaderResources[i];
    }
    for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i) {
      savedPixelSamplerStates[i] = pixelSamplerStates[i];
    }
    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
      savedRenderTargetViews[i] = renderTargetViews[i];
    }

    context->IASetVertexBuffers(0, 1, savedVertexBuffers, vertexBufferStrides,
                                vertexBufferOffsets);
    context->IASetInputLayout(inputLayout);
    context->IASetIndexBuffer(indexBuffer, indexBufferFormat,
                              indexBufferOffset);
    context->IASetPrimitiveTopology(primitiveTopology);
    context->VSSetShader(vertexShader, savedVertexShaderClassInstances,
                         vertexShaderClassInstanceCount);
    context->VSSetShaderResources(0, 2, savedVertexShaderResources);
    context->VSSetConstantBuffers(0, 1, savedVertexConstantBuffers);
    context->PSSetShader(pixelShader, savedPixelShaderClassInstances,
                         pixelShaderClassInstanceCount);
    context->PSSetShaderResources(0, 2, savedPixelShaderResources);
    context->PSSetConstantBuffers(0, 1, savedPixelConstantBuffers);
    context->PSSetSamplers(0, 2, savedPixelSamplerStates);
    context->RSSetViewports(viewportsCount, viewports);
    context->RSSetScissorRects(scissorRectsCount, scissorRects);
    context->RSSetState(rasterizerState);
    context->OMSetRenderTargets(1, savedRenderTargetViews, depthStencilView);
    context->OMSetDepthStencilState(depthStencilState, stencilRef);
    context->OMSetBlendState(blendState, blendFactors, sampleMask);
  }
};

// Shader-based YUV->RGB blit, this is a fallback for VideoProcessorBlt when it
// doesn't support certain format combinations (notably HLG to PQ).
bool DCSurfaceVideo::ShaderBlt(const RECT& sourceRect,
                               DXGI_COLOR_SPACE_TYPE outputColorSpace,
                               const RECT& destRect) {
  HRESULT hr;
  const auto device = mDCLayerTree->GetDevice();
  const auto texture = mRenderTextureHost->AsRenderDXGITextureHost();
  auto yuvColorSpace = texture->GetYUVColorSpace();
  auto ranged = gfx::FromYUVRangedColorSpace(yuvColorSpace);
  // Update constant buffer with the color transforms we need,
  // currently the P601 and P709 cases are unused because VideoProcessorBlt
  // always succeeds for those cases, but are included for completeness.
  uint32_t inputMode = 0;
  uint32_t outputMode = 0;
  float const* m;
  switch (ranged.transferFunction) {
    default:
      gfxCriticalNoteOnce << "ShaderBlt: Unhandled input transfer function "
                          << static_cast<int>(ranged.transferFunction)
                          << ", treating as BT709 transfer function";
      FMT_FALLTHROUGH;
    case gfx::TransferFunction::BT709:
      inputMode = MODE_BT709;
      break;
    case gfx::TransferFunction::HLG:
      inputMode = MODE_HLG;
      break;
    case gfx::TransferFunction::PQ:
      inputMode = MODE_PQ;
      break;
  }

  static const float yuvToRgbMatrixP709Studio[16] = {
      1.164384f,  -0.000000f, 1.792741f, -0.972945f, 1.164384f,  -0.213249f,
      -0.532909f, 0.301483f,  1.164384f, 2.112402f,  -0.000000f, -1.133402f,
      0.000000f,  0.000000f,  0.000000f, 1.000000f};

  static const float yuvToRgbMatrixP709Full[16] = {
      1.000000f,  -0.000000f, 1.574800f, -0.790488f, 1.000000f,  -0.187324f,
      -0.468124f, 0.329010f,  1.000000f, 1.855600f,  -0.000000f, -0.931439f,
      0.000000f,  0.000000f,  0.000000f, 1.000000f};

  static const float yuvToRgbMatrixP601Studio[16] = {
      1.164384f,  -0.000000f, 1.596027f, -0.874202f, 1.164384f, -0.391762f,
      -0.812968f, 0.531668f,  1.164384f, 2.017232f,  0.000000f, -1.085631f,
      0.000000f,  0.000000f,  0.000000f, 1.000000f};

  static const float yuvToRgbMatrixP601Full[16] = {
      1.0000f,    0.000000f,  1.402000f, -0.701000f, 1.0000f,    -0.344136f,
      -0.714136f, 0.5271972f, 1.0000f,   1.772000f,  -0.000000f, -0.861777f,
      0.000000f,  0.000000f,  0.000000f, 1.000000f};

  static const float yuvToRgbMatrixP2020Studio[16] = {
      1.168932f,  0.000000f, 1.685231f, -0.915688f, 1.168932f,  -0.188058f,
      -0.652965f, 0.347458f, 1.168932f, 2.150139f,  -0.000000f, -1.148145f,
      0.000000f,  0.000000f, 0.000000f, 1.000000f};

  static const float yuvToRgbMatrixP2020Full[16] = {
      1.000000f,  -0.000000f, 1.474600f, -0.737311f, 1.000000f,  -0.164553f,
      -0.571353f, 0.367959f,  1.000000f, 1.881400f,  -0.000000f, -0.940714f,
      0.000000f,  0.000000f,  0.000000f, 1.000000f};

  auto markerInputColorSpaceName = "unknown";
  switch (ranged.space) {
    default:
      gfxCriticalNoteOnce << "ShaderBlt: Unhandled input color space "
                          << static_cast<int>(ranged.space)
                          << ", treating as BT709 limited color space";
      FMT_FALLTHROUGH;
    case gfx::YUVColorSpace::BT709:
      markerInputColorSpaceName = "BT709";
      if (ranged.range == gfx::ColorRange::FULL) {
        m = yuvToRgbMatrixP709Full;
      } else {
        m = yuvToRgbMatrixP709Studio;
      }
      break;
    case gfx::YUVColorSpace::BT601:
      markerInputColorSpaceName = "BT601";
      if (ranged.range == gfx::ColorRange::FULL) {
        m = yuvToRgbMatrixP601Full;
      } else {
        m = yuvToRgbMatrixP601Studio;
      }
      break;
    case gfx::YUVColorSpace::BT2020:
      if (inputMode == MODE_BT709) {
        markerInputColorSpaceName = "BT2020";
      } else if (inputMode == MODE_HLG) {
        markerInputColorSpaceName = "BT2100HLG";
      } else if (inputMode == MODE_PQ) {
        markerInputColorSpaceName = "BT2100PQ";
      }
      if (ranged.range == gfx::ColorRange::FULL) {
        m = yuvToRgbMatrixP2020Full;
      } else {
        m = yuvToRgbMatrixP2020Studio;
      }
      break;
  }

  auto markerOutputColorSpaceName = "unknown";
  switch (outputColorSpace) {
    default:
      gfxCriticalNoteOnce << "ShaderBlt: Unhandled output color space "
                          << static_cast<int>(outputColorSpace)
                          << ", treating as BT709 output color space";
      FMT_FALLTHROUGH;
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
      markerOutputColorSpaceName = "BT709";
      outputMode = MODE_BT709;
      break;
    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
      markerOutputColorSpaceName = "BT2100PQ";
      outputMode = MODE_PQ;
      break;
  }

  if (profiler_thread_is_being_profiled_for_markers()) {
    nsPrintfCString str("%s -> %s", markerInputColorSpaceName,
                        markerOutputColorSpaceName);
    PROFILER_MARKER_TEXT("DCSurfaceVideo::ShaderBlt", GRAPHICS, {}, str);
  }

  RefPtr<ID3D11Texture2D> inputTexture = texture->GetD3D11Texture2DWithGL();
  RefPtr<ID3D11Texture2D> outputTexture;
  mVideoSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                             (void**)getter_AddRefs(outputTexture));
  if (!inputTexture || !outputTexture) {
    gfxCriticalNoteOnce << "Failed to get D3D11Texture2D for ShaderBlt";
    return false;
  }
  RefPtr<ID3D11DeviceContext> context;
  device->GetImmediateContext(getter_AddRefs(context));
  if (!context) {
    gfxCriticalNoteOnce << "Failed to get D3D11DeviceContext for ShaderBlt";
    return false;
  }

  if (!ShaderBltSetup()) {
    return false;
  }

  D3D11_TEXTURE2D_DESC inputTextureDesc = {};
  inputTexture->GetDesc(&inputTextureDesc);
  RefPtr<ID3D11ShaderResourceView> yResourceView;
  RefPtr<ID3D11ShaderResourceView> uvResourceView;
  D3D11_SHADER_RESOURCE_VIEW_DESC ySRVDesc = {};
  D3D11_SHADER_RESOURCE_VIEW_DESC uvSRVDesc = {};
  ySRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  ySRVDesc.Texture2D.MostDetailedMip = 0;
  ySRVDesc.Texture2D.MipLevels = 1;
  uvSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  uvSRVDesc.Texture2D.MostDetailedMip = 0;
  uvSRVDesc.Texture2D.MipLevels = 1;
  switch (inputTextureDesc.Format) {
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
      ySRVDesc.Format = DXGI_FORMAT_R16_UNORM;
      uvSRVDesc.Format = DXGI_FORMAT_R16G16_UNORM;
      break;
    case DXGI_FORMAT_NV12:
      ySRVDesc.Format = DXGI_FORMAT_R8_UNORM;
      uvSRVDesc.Format = DXGI_FORMAT_R8G8_UNORM;
      break;
    default:
      ySRVDesc.Format = DXGI_FORMAT_R8_UNORM;
      uvSRVDesc.Format = DXGI_FORMAT_R8G8_UNORM;
      gfxCriticalNoteOnce << "Unhandled inputTextureDesc.Format "
                          << static_cast<int>(inputTextureDesc.Format);
      break;
  }
  hr = device->CreateShaderResourceView(inputTexture, &ySRVDesc,
                                        getter_AddRefs(yResourceView));
  if (FAILED(hr)) {
    gfxCriticalNoteOnce << "Failed to create Y shader resource view (format "
                        << static_cast<int>(ySRVDesc.Format)
                        << ") for input texture (format "
                        << static_cast<int>(inputTextureDesc.Format) << ")";
    return false;
  }
  hr = device->CreateShaderResourceView(inputTexture, &uvSRVDesc,
                                        getter_AddRefs(uvResourceView));
  if (FAILED(hr)) {
    gfxCriticalNoteOnce << "Failed to create UV shader resource view (format "
                        << static_cast<int>(uvSRVDesc.Format)
                        << ") for input texture (format "
                        << static_cast<int>(inputTextureDesc.Format) << ")";
    return false;
  }

  // Create and set render target view for output texture
  RefPtr<ID3D11RenderTargetView> rtView;
  D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
  D3D11_TEXTURE2D_DESC outputDesc;
  outputTexture->GetDesc(&outputDesc);
  rtvDesc.Format = outputDesc.Format;
  rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
  rtvDesc.Texture2D.MipSlice = 0;

  hr = device->CreateRenderTargetView(outputTexture, &rtvDesc,
                                      getter_AddRefs(rtView));
  if (FAILED(hr)) {
    gfxCriticalNoteOnce << "Failed to create render target view";
    return false;
  }

  // Set up shader resources
  float scale[2] = {
      1.0f / static_cast<float>(inputTextureDesc.Width),
      1.0f / static_cast<float>(inputTextureDesc.Height),
  };
  float offset[2] = {0.0f * scale[0], 0.0f * scale[1]};
  float vertexData[4][6] = {
      // position[4]    texCoord[2]
      {-1.0f, -1.0f, 0.1f, 1.0f, sourceRect.left * scale[0] + offset[0],
       sourceRect.bottom * scale[1] + offset[1]},
      {1.0f, -1.0f, 0.1f, 1.0f, sourceRect.right * scale[0] + offset[0],
       sourceRect.bottom * scale[1] + offset[1]},
      {-1.0f, 1.0f, 0.1f, 1.0f, sourceRect.left * scale[0] + offset[0],
       sourceRect.top * scale[1] + offset[1]},
      {1.0f, 1.0f, 0.1f, 1.0f, sourceRect.right * scale[0] + offset[0],
       sourceRect.top * scale[1] + offset[1]}};
  context->UpdateSubresource(mShaderBltVertexBuffer, 0, nullptr, vertexData[0],
                             0, 0);

  color::mat4 yuvToRgb = color::mat4{{color::vec4{{m[0], m[1], m[2], m[3]}},
                                      {{m[4], m[5], m[6], m[7]}},
                                      {{m[8], m[9], m[10], m[11]}},
                                      {{m[12], m[13], m[14], m[15]}}}};

  // This is identity for now, if converting between BT2020 and BT709 we would
  // need to set it.
  color::mat4 linearColor = color::mat4::Identity();

  // Set Reinhard tonemapping based on content and display luminance.
  //
  // Currently this is unused functionality - to configure this properly we will
  // need to get the content luminance from the video stream and the display
  // luminance from the screen info. For now we just set it to 1.0f for both
  // which is a no-op.
  const float luminanceContent = 1.0f;
  const float luminanceDisplay = 1.0f;
  float a = 0.0f;
  float b = 0.0f;
  if (luminanceContent <= luminanceDisplay) {
    // No op - the content fits within display limits
    a = 0.0f;
    b = 0.0f;
  } else {
    // Use tonemapping to make content fit in display limits.
    a = luminanceDisplay / (luminanceContent * luminanceContent);
    b = 1.0f / luminanceDisplay;
  }

  // This is the ratio of reference white in PQ (scene-referred) to HLG
  // (display-referred)
  const float pqMultiplier = 80.0f / 10000.0f;

  psConstants constants = {.yuvToRgbMatrix = yuvToRgb,
                           .colorSpaceMatrix = linearColor,
                           .tonemapping = {a, b},
                           .pqMultiplier = pqMultiplier,
                           .inputMode = inputMode,
                           .outputMode = outputMode};
  context->UpdateSubresource(mShaderBltConstantBuffer, 0, nullptr, &constants,
                             0, 0);

  UINT vertexBufferStride = sizeof(vertexData[0]);
  UINT vertexBufferOffset = 0;
  // Set viewport for destination rectangle
  D3D11_VIEWPORT vp = {};
  vp.TopLeftX = static_cast<FLOAT>(destRect.left);
  vp.TopLeftY = static_cast<FLOAT>(destRect.top);
  vp.Width = static_cast<FLOAT>(destRect.right - destRect.left);
  vp.Height = static_cast<FLOAT>(destRect.bottom - destRect.top);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  ID3D11ShaderResourceView* srvs[] = {yResourceView, uvResourceView};
  ID3D11SamplerState* samplers[] = {mShaderBltSamplerState,
                                    mShaderBltSamplerState};
  ID3D11Buffer* psConstantBuffers[1] = {mShaderBltConstantBuffer};
  ID3D11Buffer* vertexBuffers[1] = {mShaderBltVertexBuffer};
  ID3D11RenderTargetView* rtViews[1] = {rtView};

  {
    // Save D3D11 state
    SavedD3D11State savedState(context);

    // Set state for the shader
    context->IASetVertexBuffers(0, 1, vertexBuffers, &vertexBufferStride,
                                &vertexBufferOffset);
    context->IASetInputLayout(mShaderBltInputLayout);
    context->IASetIndexBuffer(nullptr, DXGI_FORMAT_R16_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->VSSetShader(mShaderBltVertexShader, nullptr, 0);
    context->VSSetSamplers(0, 0, nullptr);
    context->VSSetShaderResources(0, 0, nullptr);
    context->VSSetConstantBuffers(0, 0, nullptr);
    context->PSSetShader(mShaderBltPixelShader, nullptr, 0);
    context->PSSetSamplers(0, 2, samplers);
    context->PSSetShaderResources(0, 2, srvs);
    context->PSSetConstantBuffers(0, 1, psConstantBuffers);
    context->RSSetViewports(1, &vp);
    context->RSSetScissorRects(0, nullptr);
    context->RSSetState(mShaderBltRasterizerState);
    context->OMSetRenderTargets(1, rtViews, nullptr);
    context->OMSetDepthStencilState(nullptr, 0);
    context->OMSetBlendState(mShaderBltBlendState, nullptr, 0xffffffff);

    // Draw the quad
    context->Draw(4, 0);

    // D3D11 state will be restored when savedState goes out of scope
  }

  return true;
}

bool DCSurfaceVideo::CallVideoProcessorBlt() {
  MOZ_ASSERT(mRenderTextureHost);

  HRESULT hr;
  const auto device = mDCLayerTree->GetDevice();
  const auto videoDevice = mDCLayerTree->GetVideoDevice();
  const auto videoContext = mDCLayerTree->GetVideoContext();
  const auto texture = mRenderTextureHost->AsRenderDXGITextureHost();

  Maybe<DXGI_COLOR_SPACE_TYPE> sourceColorSpace =
      GetSourceDXGIColorSpace(texture->GetYUVColorSpace());
  if (sourceColorSpace.isNothing()) {
    gfxCriticalNote << "Unsupported color space";
    return false;
  }

  RefPtr<ID3D11Texture2D> texture2D = texture->GetD3D11Texture2DWithGL();
  if (!texture2D) {
    gfxCriticalNote << "Failed to get D3D11Texture2D";
    return false;
  }

  if (!mVideoSwapChain) {
    return false;
  }

  if (texture->mFencesHolderId.isSome()) {
    auto* fencesHolderMap = layers::CompositeProcessFencesHolderMap::Get();
    MOZ_ASSERT(fencesHolderMap);
    auto fence = fencesHolderMap->GetWriteFence(texture->mFencesHolderId.ref());
    layers::FenceD3D11::WaitD3D11Fence(fence, device);
  }

  RefPtr<IDXGISwapChain3> swapChain3;
  mVideoSwapChain->QueryInterface(
      (IDXGISwapChain3**)getter_AddRefs(swapChain3));
  if (!swapChain3) {
    gfxCriticalNote << "Failed to get IDXGISwapChain3";
    return false;
  }

  RefPtr<ID3D11VideoContext1> videoContext1;
  videoContext->QueryInterface(
      (ID3D11VideoContext1**)getter_AddRefs(videoContext1));
  if (!videoContext1) {
    gfxCriticalNote << "Failed to get ID3D11VideoContext1";
    return false;
  }

  const auto videoProcessor = mDCLayerTree->GetVideoProcessor();
  const auto videoProcessorEnumerator =
      mDCLayerTree->GetVideoProcessorEnumerator();

  DXGI_COLOR_SPACE_TYPE inputColorSpace = sourceColorSpace.ref();
  videoContext1->VideoProcessorSetStreamColorSpace1(videoProcessor, 0,
                                                    inputColorSpace);

  Maybe<DXGI_COLOR_SPACE_TYPE> outputColorSpaceRef =
      GetOutputDXGIColorSpace(mSwapChainFormat, inputColorSpace);
  if (outputColorSpaceRef.isNothing()) {
    gfxCriticalNoteOnce << "Unrecognized DXGI mSwapChainFormat, unsure of "
                           "correct DXGI colorspace: "
                        << gfx::hexa(mSwapChainFormat);
    return false;
  }
  DXGI_COLOR_SPACE_TYPE outputColorSpace = outputColorSpaceRef.ref();
  DXGI_COLOR_SPACE_TYPE swapChainColorSpace = outputColorSpace;
  if (mUseVpAutoHDR) {
    swapChainColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
  }

  hr = swapChain3->SetColorSpace1(swapChainColorSpace);
  if (FAILED(hr)) {
    gfxCriticalNoteOnce << "SetColorSpace1 failed: " << gfx::hexa(hr);
    RenderThread::Get()->NotifyWebRenderError(
        wr::WebRenderError::VIDEO_OVERLAY);
    return false;
  }
  videoContext1->VideoProcessorSetOutputColorSpace1(videoProcessor,
                                                    outputColorSpace);

  auto hdrMetadata =
      gfx::DeviceManagerDx::Get()->WindowHDRMetadata(mDCLayerTree->GetHwnd());
  RefPtr<ID3D11VideoContext2> videoContext2;
  videoContext->QueryInterface(
      (ID3D11VideoContext2**)getter_AddRefs(videoContext2));
  if (hdrMetadata.isSome() && videoContext2) {
    // If we had to fall back to non-HDR color spaces in VideoProcessorBlt we
    // should remove the HDR meta data to avoid confusing the
    // VideoProcessorBlt implementation.  We'll still display as HDR if it was
    // PQ content but without the metadata being fed to VideoProcessorBlt, so it
    // may be too bright but at least it should work.
    if (mFailedVideoProcessorBltYUVHLGToRGBPQ ||
        mFailedVideoProcessorBltYUVPQtoRGBPQ) {
      videoContext2->VideoProcessorSetOutputHDRMetaData(
          videoProcessor, DXGI_HDR_METADATA_TYPE_NONE, 0, NULL);
    } else {
      videoContext2->VideoProcessorSetOutputHDRMetaData(
          videoProcessor, DXGI_HDR_METADATA_TYPE_HDR10,
          sizeof(DXGI_HDR_METADATA_HDR10), &(hdrMetadata.ref()));
    }
  }

  if (videoContext2) {
    const auto& streamHdrMetadata = texture->GetHDRMetadata();
    if (streamHdrMetadata.isSome()) {
      DXGI_HDR_METADATA_HDR10 hdr10 = ToStreamHDR10Metadata(*streamHdrMetadata);
      videoContext2->VideoProcessorSetStreamHDRMetaData(
          videoProcessor, 0, DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdr10),
          &hdr10);
    }
  }

  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc = {};
  inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
  inputDesc.Texture2D.ArraySlice = texture->ArrayIndex();

  RefPtr<ID3D11VideoProcessorInputView> inputView;
  hr = videoDevice->CreateVideoProcessorInputView(
      texture2D, videoProcessorEnumerator, &inputDesc,
      getter_AddRefs(inputView));
  if (FAILED(hr)) {
    gfxCriticalNote << "ID3D11VideoProcessorInputView creation failed: "
                    << gfx::hexa(hr);
    return false;
  }

  D3D11_VIDEO_PROCESSOR_STREAM stream = {};
  stream.Enable = true;
  stream.OutputIndex = 0;
  stream.InputFrameOrField = 0;
  stream.PastFrames = 0;
  stream.FutureFrames = 0;
  stream.pInputSurface = inputView.get();

  RECT destRect;
  destRect.left = 0;
  destRect.top = 0;
  destRect.right = mSwapChainSize.width;
  destRect.bottom = mSwapChainSize.height;

  videoContext->VideoProcessorSetOutputTargetRect(videoProcessor, TRUE,
                                                  &destRect);
  videoContext->VideoProcessorSetStreamDestRect(videoProcessor, 0, TRUE,
                                                &destRect);
  RECT sourceRect;
  sourceRect.left = 0;
  sourceRect.top = 0;
  sourceRect.right = mVideoSize.width;
  sourceRect.bottom = mVideoSize.height;
  videoContext->VideoProcessorSetStreamSourceRect(videoProcessor, 0, TRUE,
                                                  &sourceRect);

  if (outputColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 &&
      StaticPrefs::gfx_color_management_hdr_yuv_to_rgb_video_shader_always()) {
    return ShaderBlt(sourceRect, outputColorSpace, destRect);
  }

  if (!mOutputView) {
    RefPtr<ID3D11Texture2D> backBuf;
    mVideoSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                               (void**)getter_AddRefs(backBuf));

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc = {};
    outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outputDesc.Texture2D.MipSlice = 0;

    hr = videoDevice->CreateVideoProcessorOutputView(
        backBuf, videoProcessorEnumerator, &outputDesc,
        getter_AddRefs(mOutputView));
    if (FAILED(hr)) {
      gfxCriticalNote << "ID3D11VideoProcessorOutputView creation failed: "
                      << gfx::hexa(hr);
      return false;
    }
  }

  const UINT vendorId = GetVendorId(videoDevice);
  const auto powerIsCharging = RenderThread::Get()->GetPowerIsCharging();
  const bool useSuperResolution =
      gfx::gfxVars::WebRenderOverlayVpSuperResolution() && powerIsCharging &&
      !mVpSuperResolutionFailed;

  if (profiler_thread_is_being_profiled_for_markers()) {
    nsPrintfCString str(
        "useSuperResolution %d gfxVars %d charging %d failed %d",
        useSuperResolution, gfx::gfxVars::WebRenderOverlayVpSuperResolution(),
        powerIsCharging, mVpSuperResolutionFailed);
    PROFILER_MARKER_TEXT("DCSurfaceVideo", GRAPHICS, {}, str);
  }

  if (useSuperResolution) {
    PROFILER_MARKER_TEXT("DCSurfaceVideo", GRAPHICS, {},
                         "SetVpSuperResolution"_ns);

    hr = SetVpSuperResolution(vendorId, videoContext, videoProcessor, true);
    if (FAILED(hr)) {
      if (hr != E_NOTIMPL) {
        gfxCriticalNoteOnce << "SetVpSuperResolution failed: " << gfx::hexa(hr);
      }
      mVpSuperResolutionFailed = true;
    }
  } else if (gfx::gfxVars::WebRenderOverlayVpSuperResolution() &&
             !useSuperResolution) {
    SetVpSuperResolution(vendorId, videoContext, videoProcessor, false);
  }

  if (profiler_thread_is_being_profiled_for_markers() && vendorId == 0x10DE) {
    AddProfileMarkerForNvidiaVpSuperResolutionInfo(videoContext,
                                                   videoProcessor);
  }

  if (mUseVpAutoHDR) {
    PROFILER_MARKER_TEXT("DCSurfaceVideo", GRAPHICS, {}, "SetVpAutoHDR"_ns);

    hr = SetVpAutoHDR(vendorId, videoContext, videoProcessor, true);
    if (FAILED(hr)) {
      gfxCriticalNoteOnce << "SetVpAutoHDR failed: " << gfx::hexa(hr);
      mVpAutoHDRFailed = true;
    }
  }

  hr = videoContext->VideoProcessorBlt(videoProcessor, mOutputView, 0, 1,
                                       &stream);
  if (hr == E_NOTIMPL &&
      outputColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
    if (StaticPrefs::
            gfx_color_management_hdr_yuv_to_rgb_video_shader_fallback()) {
      return ShaderBlt(sourceRect, outputColorSpace, destRect);
    }
    if (inputColorSpace == DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020 ||
        inputColorSpace == DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020) {
      // If YUV BT2100 HLG conversion to RGB BT2100 PQ is not working, we have
      // to fall back to displaying it as regular RGB BT2020, which is not HDR
      // but still has the gamut of BT2020.  Once we implement HLG conversion
      // ourselves (probably using a D3D11 shader instead of VideoProcessorBlt)
      // we can handle this more gracefully.
      gfxCriticalNoteOnce
          << "VideoProcessorBlt failed with BT2100 HLG content with "
             "error E_NOTIMPL, BT2100 HLG content will be displayed as BT2020 "
             "(not HDR).  Input color space: "
          << static_cast<int>(inputColorSpace)
          << ", output color space: " << static_cast<int>(outputColorSpace)
          << ", swap chain format: " << static_cast<int>(mSwapChainFormat);
      mFailedVideoProcessorBltYUVHLGToRGBPQ = true;
      hr = swapChain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020);
      if (FAILED(hr)) {
        gfxCriticalNoteOnce << "SetColorSpace1 failed: " << gfx::hexa(hr);
        RenderThread::Get()->NotifyWebRenderError(
            wr::WebRenderError::VIDEO_OVERLAY);
        return false;
      }
      // Now retry the VideoProcessorBlt with BT709 which should just work.
      if (inputColorSpace == DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020) {
        videoContext1->VideoProcessorSetStreamColorSpace1(
            videoProcessor, 0, DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
      } else {
        videoContext1->VideoProcessorSetStreamColorSpace1(
            videoProcessor, 0, DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709);
      }
      videoContext1->VideoProcessorSetOutputColorSpace1(
          videoProcessor, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020);
      hr = videoContext->VideoProcessorBlt(videoProcessor, mOutputView, 0, 1,
                                           &stream);
    } else {
      // If YUV BT2100 PQ conversion to RGB BT2100 PQ is not working, we can
      // just pretend it is BT2020 and the VideoProcessorBlt should succeed,
      // it's not clear if this causes any color distortion.
      gfxCriticalNoteOnce
          << "VideoProcessorBlt failed with BT2100 PQ content with "
             "error E_NOTIMPL, will retry as BT709 for processing and display "
             "as BT2100 PQ.  Input color space: "
          << static_cast<int>(inputColorSpace)
          << ", output color space: " << static_cast<int>(outputColorSpace)
          << ", swap chain format: " << static_cast<int>(mSwapChainFormat);
      mFailedVideoProcessorBltYUVPQtoRGBPQ = true;
      // Now retry the VideoProcessorBlt with BT709 which should just work.
      videoContext1->VideoProcessorSetStreamColorSpace1(
          videoProcessor, 0, DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
      videoContext1->VideoProcessorSetOutputColorSpace1(
          videoProcessor, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
      hr = videoContext->VideoProcessorBlt(videoProcessor, mOutputView, 0, 1,
                                           &stream);
    }
  }
  if (FAILED(hr)) {
    gfxCriticalNote << "VideoProcessorBlt failed: " << gfx::hexa(hr);
    return false;
  }

  return true;
}

void DCSurfaceVideo::ReleaseDecodeSwapChainResources() {
  mOutputView = nullptr;
  mVideoSwapChain = nullptr;
  mDecodeSwapChain = nullptr;
  mDecodeResource = nullptr;
  if (mSwapChainSurfaceHandle) {
    ::CloseHandle(mSwapChainSurfaceHandle);
    mSwapChainSurfaceHandle = 0;
  }
  mUseVpAutoHDR = false;
  mUseHDR = false;
  mShaderBltVertexShader = nullptr;
  mShaderBltVSBlob = nullptr;
  mShaderBltPixelShader = nullptr;
  mShaderBltPSBlob = nullptr;
  mShaderBltIndexBuffer = nullptr;
  mShaderBltVertexBuffer = nullptr;
  mShaderBltInputLayout = nullptr;
  mShaderBltRasterizerState = nullptr;
  mShaderBltBlendState = nullptr;
  mShaderBltConstantBuffer = nullptr;
  mShaderBltSamplerState = nullptr;
}

DCSurfaceHandle::DCSurfaceHandle(bool aIsOpaque, DCLayerTree* aDCLayerTree)
    : DCSurface(aIsOpaque, aDCLayerTree) {}

void DCSurfaceHandle::AttachExternalImage(wr::ExternalImageId aExternalImage) {
  RenderTextureHost* texture =
      RenderThread::Get()->GetRenderTexture(aExternalImage);
  RenderDcompSurfaceTextureHost* renderTexture =
      texture ? texture->AsRenderDcompSurfaceTextureHost() : nullptr;
  if (!renderTexture) {
    gfxCriticalNote << "Unsupported RenderTexture for DCSurfaceHandle: "
                    << gfx::hexa(texture);
    return;
  }

  const auto handle = renderTexture->GetDcompSurfaceHandle();
  if (GetSurfaceHandle() == handle) {
    return;
  }

  LOG_H("AttachExternalImage, ext-image=%" PRIu64 ", texture=%p, handle=%p",
        wr::AsUint64(aExternalImage), renderTexture, handle);
  mDcompTextureHost = renderTexture;
}

HANDLE DCSurfaceHandle::GetSurfaceHandle() const {
  if (mDcompTextureHost) {
    return mDcompTextureHost->GetDcompSurfaceHandle();
  }
  return nullptr;
}

IDCompositionSurface* DCSurfaceHandle::EnsureSurface() {
  if (auto* surface = mDcompTextureHost->GetSurface()) {
    return surface;
  }

  // Texture host hasn't created the surface yet, ask it to create a new one.
  RefPtr<IDCompositionDevice> device;
  HRESULT hr = mDCLayerTree->GetCompositionDevice()->QueryInterface(
      (IDCompositionDevice**)getter_AddRefs(device));
  if (FAILED(hr)) {
    gfxCriticalNote
        << "Failed to convert IDCompositionDevice2 to IDCompositionDevice: "
        << gfx::hexa(hr);
    return nullptr;
  }

  return mDcompTextureHost->CreateSurfaceFromDevice(device);
}

void DCSurfaceHandle::PresentSurfaceHandle() {
  LOG_H("PresentSurfaceHandle");
  if (IDCompositionSurface* surface = EnsureSurface()) {
    LOG_H("Set surface %p to visual", surface);
    mContentVisual->SetContent(surface);
  } else {
    mContentVisual->SetContent(nullptr);
  }
}

GLuint DCLayerTree::CreateEGLSurfaceForCompositionSurface(
    wr::DeviceIntRect aDirtyRect, wr::DeviceIntPoint* aOffset,
    RefPtr<IDCompositionSurface> aCompositionSurface,
    wr::DeviceIntPoint aSurfaceOffset) {
  MOZ_ASSERT(aCompositionSurface.get());

  HRESULT hr;
  const auto gl = GetGLContext();
  RefPtr<ID3D11Texture2D> backBuf;
  POINT offset;

  RECT update_rect;
  update_rect.left = aSurfaceOffset.x + aDirtyRect.min.x;
  update_rect.top = aSurfaceOffset.y + aDirtyRect.min.y;
  update_rect.right = aSurfaceOffset.x + aDirtyRect.max.x;
  update_rect.bottom = aSurfaceOffset.y + aDirtyRect.max.y;
  hr = aCompositionSurface->BeginDraw(&update_rect, __uuidof(ID3D11Texture2D),
                                      (void**)getter_AddRefs(backBuf), &offset);

  if (FAILED(hr)) {
    LayoutDeviceIntRect rect = widget::WinUtils::ToIntRect(update_rect);

    gfxCriticalNote << "DCompositionSurface::BeginDraw failed: "
                    << gfx::hexa(hr) << " " << rect;
    RenderThread::Get()->HandleWebRenderError(WebRenderError::BEGIN_DRAW);
    return false;
  }

  // DC includes the origin of the dirty / update rect in the draw offset,
  // undo that here since WR expects it to be an absolute offset.
  offset.x -= aDirtyRect.min.x;
  offset.y -= aDirtyRect.min.y;

  D3D11_TEXTURE2D_DESC desc;
  backBuf->GetDesc(&desc);

  const auto& gle = gl::GLContextEGL::Cast(gl);
  const auto& egl = gle->mEgl;

  const auto buffer = reinterpret_cast<EGLClientBuffer>(backBuf.get());

  // Construct an EGLImage wrapper around the D3D texture for ANGLE.
  const EGLint attribs[] = {LOCAL_EGL_NONE};
  mEGLImage = egl->fCreateImage(EGL_NO_CONTEXT, LOCAL_EGL_D3D11_TEXTURE_ANGLE,
                                buffer, attribs);

  // Get the current FBO and RBO id, so we can restore them later
  GLint currentFboId, currentRboId;
  gl->fGetIntegerv(LOCAL_GL_DRAW_FRAMEBUFFER_BINDING, &currentFboId);
  gl->fGetIntegerv(LOCAL_GL_RENDERBUFFER_BINDING, &currentRboId);

  // Create a render buffer object that is backed by the EGL image.
  gl->fGenRenderbuffers(1, &mColorRBO);
  gl->fBindRenderbuffer(LOCAL_GL_RENDERBUFFER, mColorRBO);
  gl->fEGLImageTargetRenderbufferStorage(LOCAL_GL_RENDERBUFFER, mEGLImage);

  // Get or create an FBO for the specified dimensions
  GLuint fboId = GetOrCreateFbo(desc.Width, desc.Height);

  // Attach the new renderbuffer to the FBO
  gl->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, fboId);
  gl->fFramebufferRenderbuffer(LOCAL_GL_DRAW_FRAMEBUFFER,
                               LOCAL_GL_COLOR_ATTACHMENT0,
                               LOCAL_GL_RENDERBUFFER, mColorRBO);

  // Restore previous FBO and RBO bindings
  gl->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER, currentFboId);
  gl->fBindRenderbuffer(LOCAL_GL_RENDERBUFFER, currentRboId);

  aOffset->x = offset.x;
  aOffset->y = offset.y;

  return fboId;
}

void DCLayerTree::DestroyEGLSurface() {
  const auto gl = GetGLContext();

  if (mColorRBO) {
    gl->fDeleteRenderbuffers(1, &mColorRBO);
    mColorRBO = 0;
  }

  if (mEGLImage) {
    const auto& gle = gl::GLContextEGL::Cast(gl);
    const auto& egl = gle->mEgl;
    egl->fDestroyImage(mEGLImage);
    mEGLImage = EGL_NO_IMAGE;
  }
}

// -

}  // namespace wr
namespace gfx {

color::ColorProfileDesc QueryOutputColorProfile() {
  // GPU process can't simply init gfxPlatform, (and we don't need most of it)
  // but we do need gfxPlatform::GetCMSOutputProfile().
  // So we steal what we need through the window:
  const auto outputProfileData =
      gfxWindowsPlatform::GetPlatformCMSOutputProfileData_Impl();

  const auto qcmsProfile = qcms_profile_from_memory(
      outputProfileData.Elements(), outputProfileData.Length());
  const auto release = MakeScopeExit([&]() {
    if (qcmsProfile) {
      qcms_profile_release(qcmsProfile);
    }
  });

  const bool print = gfxEnv::MOZ_GL_SPEW();

  const auto ret = [&]() {
    if (qcmsProfile) {
      return color::ColorProfileDesc::From(*qcmsProfile);
    }
    if (print) {
      printf_stderr(
          "Missing or failed to load display color profile, defaulting to "
          "sRGB.\n");
    }
    const auto MISSING_PROFILE_DEFAULT_SPACE = color::ColorspaceDesc{
        color::Chromaticities::Srgb(),
        color::TransferFunctionDesc::Srgb(),
    };
    return color::ColorProfileDesc::From(MISSING_PROFILE_DEFAULT_SPACE);
  }();

  if (print) {
    const auto gammaGuess = color::GuessGamma(ret.linearFromTf.r);
    printf_stderr(
        "Display profile:\n"
        "  Approx Gamma: %f\n"
        "  XYZ-D65 Red  : %f, %f, %f\n"
        "  XYZ-D65 Green: %f, %f, %f\n"
        "  XYZ-D65 Blue : %f, %f, %f\n",
        gammaGuess, ret.xyzd65FromLinearRgb.at(0, 0),
        ret.xyzd65FromLinearRgb.at(0, 1), ret.xyzd65FromLinearRgb.at(0, 2),

        ret.xyzd65FromLinearRgb.at(1, 0), ret.xyzd65FromLinearRgb.at(1, 1),
        ret.xyzd65FromLinearRgb.at(1, 2),

        ret.xyzd65FromLinearRgb.at(2, 0), ret.xyzd65FromLinearRgb.at(2, 1),
        ret.xyzd65FromLinearRgb.at(2, 2));
  }

  return ret;
}

}  // namespace gfx
namespace wr {

inline D2D1_MATRIX_5X4_F to_D2D1_MATRIX_5X4_F(const color::mat4& m) {
  return D2D1_MATRIX_5X4_F{{{
      m.rows[0][0],
      m.rows[1][0],
      m.rows[2][0],
      m.rows[3][0],
      m.rows[0][1],
      m.rows[1][1],
      m.rows[2][1],
      m.rows[3][1],
      m.rows[0][2],
      m.rows[1][2],
      m.rows[2][2],
      m.rows[3][2],
      m.rows[0][3],
      m.rows[1][3],
      m.rows[2][3],
      m.rows[3][3],
      0,
      0,
      0,
      0,
  }}};
}

ColorManagementChain ColorManagementChain::From(
    IDCompositionDevice3& dcomp,
    const color::ColorProfileConversionDesc& conv) {
  auto ret = ColorManagementChain{};

  const auto Append = [&](const RefPtr<IDCompositionFilterEffect>& afterLast) {
    if (ret.last) {
      afterLast->SetInput(0, ret.last, 0);
    }
    ret.last = afterLast;
  };

  const auto MaybeAppendColorMatrix = [&](const color::mat4& m) {
    RefPtr<IDCompositionColorMatrixEffect> e;
    if (approx(m, color::mat4::Identity())) return e;
    dcomp.CreateColorMatrixEffect(getter_AddRefs(e));
    MOZ_ASSERT(e);
    if (!e) return e;
    e->SetMatrix(to_D2D1_MATRIX_5X4_F(m));
    Append(e);
    return e;
  };
  const auto MaybeAppendTableTransfer = [&](const color::RgbTransferTables& t) {
    RefPtr<IDCompositionTableTransferEffect> e;
    if (!t.r.size() && !t.g.size() && !t.b.size()) return e;
    dcomp.CreateTableTransferEffect(getter_AddRefs(e));
    MOZ_ASSERT(e);
    if (!e) return e;
    e->SetRedTable(t.r.data(), t.r.size());
    e->SetGreenTable(t.g.data(), t.g.size());
    e->SetBlueTable(t.b.data(), t.b.size());
    Append(e);
    return e;
  };

  ret.srcRgbFromSrcYuv = MaybeAppendColorMatrix(conv.srcRgbFromSrcYuv);
  ret.srcLinearFromSrcTf = MaybeAppendTableTransfer(conv.srcLinearFromSrcTf);
  ret.dstLinearFromSrcLinear =
      MaybeAppendColorMatrix(color::mat4(conv.dstLinearFromSrcLinear));
  ret.dstTfFromDstLinear = MaybeAppendTableTransfer(conv.dstTfFromDstLinear);

  return ret;
}

ColorManagementChain::~ColorManagementChain() = default;

}  // namespace wr
}  // namespace mozilla

#undef LOG_H
