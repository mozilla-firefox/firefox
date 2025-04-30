/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define INITGUID  // set before devguid.h

#include "gfxWindowsPlatform.h"

#include "cairo.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/layers/CompositorBridgeChild.h"

#include "gfxBlur.h"
#include "gfxImageSurface.h"
#include "gfxWindowsSurface.h"

#include "nsUnicharUtils.h"
#include "nsUnicodeProperties.h"

#include "mozilla/Preferences.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerThreadSleep.h"
#include "mozilla/Components.h"
#include "mozilla/Sprintf.h"
#include "mozilla/WindowsVersion.h"
#include "nsIGfxInfo.h"
#include "nsServiceManagerUtils.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "mozilla/glean/GfxMetrics.h"

#include "plbase64.h"
#include "nsIXULRuntime.h"
#include "imgLoader.h"

#include "nsIGfxInfo.h"

#include "gfxCrashReporterUtils.h"

#include "gfxGDIFontList.h"
#include "gfxGDIFont.h"

#include "mozilla/layers/CanvasChild.h"
#include "mozilla/layers/CompositorThread.h"

#include "gfxDWriteFontList.h"
#include "gfxDWriteFonts.h"
#include "gfxDWriteCommon.h"
#include <dwrite.h>

#include "gfxTextRun.h"
#include "gfxUserFontSet.h"
#include "nsWindowsHelpers.h"
#include "gfx2DGlue.h"

#include <string>

#include <d3d10_1.h>

#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/gfxVars.h"

#include <dwmapi.h>
#include <d3d11.h>
#include <d2d1_1.h>

#include "nsIMemoryReporter.h"
#include <winternl.h>
#include "d3dkmtQueryStatistics.h"

#include "base/thread.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_layers.h"
#include "gfxConfig.h"
#include "VsyncSource.h"
#include "DriverCrashGuard.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/gfx/DeviceManagerDx.h"
#include "mozilla/gfx/DisplayConfigWindows.h"
#include "mozilla/layers/DeviceAttachmentsD3D11.h"
#include "mozilla/WindowsProcessMitigations.h"
#include "D3D11Checks.h"
#include "mozilla/ScreenHelperWin.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::widget;
using namespace mozilla::image;
using namespace mozilla::unicode;

DCForMetrics::DCForMetrics() {
  // Get the whole screen DC:
  mDC = GetDC(nullptr);
  SetGraphicsMode(mDC, GM_ADVANCED);
}

class GfxD2DVramReporter final : public nsIMemoryReporter {
  ~GfxD2DVramReporter() {}

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    MOZ_COLLECT_REPORT("gfx-d2d-vram-draw-target", KIND_OTHER, UNITS_BYTES,
                       Factory::GetD2DVRAMUsageDrawTarget(),
                       "Video memory used by D2D DrawTargets.");

    MOZ_COLLECT_REPORT("gfx-d2d-vram-source-surface", KIND_OTHER, UNITS_BYTES,
                       Factory::GetD2DVRAMUsageSourceSurface(),
                       "Video memory used by D2D SourceSurfaces.");

    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(GfxD2DVramReporter, nsIMemoryReporter)

class GPUAdapterReporter final : public nsIMemoryReporter {
  // Callers must Release the DXGIAdapter after use or risk mem-leak
  static bool GetDXGIAdapter(IDXGIAdapter** aDXGIAdapter) {
    RefPtr<ID3D11Device> d3d11Device;
    RefPtr<IDXGIDevice> dxgiDevice;
    bool result = false;

    if ((d3d11Device = mozilla::gfx::Factory::GetDirect3D11Device())) {
      if (d3d11Device->QueryInterface(__uuidof(IDXGIDevice),
                                      getter_AddRefs(dxgiDevice)) == S_OK) {
        result = (dxgiDevice->GetAdapter(aDXGIAdapter) == S_OK);
      }
    }

    return result;
  }

  ~GPUAdapterReporter() {}

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD
  CollectReports(nsIHandleReportCallback* aHandleReport, nsISupports* aData,
                 bool aAnonymize) override {
    HANDLE ProcessHandle = GetCurrentProcess();

    int64_t dedicatedBytesUsed = 0;
    int64_t sharedBytesUsed = 0;
    int64_t committedBytesUsed = 0;
    IDXGIAdapter* DXGIAdapter;

    HMODULE gdi32Handle;
    PFND3DKMTQS queryD3DKMTStatistics = nullptr;

    if ((gdi32Handle = LoadLibrary(TEXT("gdi32.dll"))))
      queryD3DKMTStatistics =
          (PFND3DKMTQS)GetProcAddress(gdi32Handle, "D3DKMTQueryStatistics");

    if (queryD3DKMTStatistics && GetDXGIAdapter(&DXGIAdapter)) {
      // Most of this block is understood thanks to wj32's work on Process
      // Hacker

      DXGI_ADAPTER_DESC adapterDesc;
      D3DKMTQS queryStatistics;

      DXGIAdapter->GetDesc(&adapterDesc);
      DXGIAdapter->Release();

      memset(&queryStatistics, 0, sizeof(D3DKMTQS));
      queryStatistics.Type = D3DKMTQS_PROCESS;
      queryStatistics.AdapterLuid = adapterDesc.AdapterLuid;
      queryStatistics.hProcess = ProcessHandle;
      if (NT_SUCCESS(queryD3DKMTStatistics(&queryStatistics))) {
        committedBytesUsed =
            queryStatistics.QueryResult.ProcessInfo.SystemMemory.BytesAllocated;
      }

      memset(&queryStatistics, 0, sizeof(D3DKMTQS));
      queryStatistics.Type = D3DKMTQS_ADAPTER;
      queryStatistics.AdapterLuid = adapterDesc.AdapterLuid;
      if (NT_SUCCESS(queryD3DKMTStatistics(&queryStatistics))) {
        ULONG i;
        ULONG segmentCount = queryStatistics.QueryResult.AdapterInfo.NbSegments;

        for (i = 0; i < segmentCount; i++) {
          memset(&queryStatistics, 0, sizeof(D3DKMTQS));
          queryStatistics.Type = D3DKMTQS_SEGMENT;
          queryStatistics.AdapterLuid = adapterDesc.AdapterLuid;
          queryStatistics.QuerySegment.SegmentId = i;

          if (NT_SUCCESS(queryD3DKMTStatistics(&queryStatistics))) {
            bool aperture = queryStatistics.QueryResult.SegmentInfo.Aperture;
            memset(&queryStatistics, 0, sizeof(D3DKMTQS));
            queryStatistics.Type = D3DKMTQS_PROCESS_SEGMENT;
            queryStatistics.AdapterLuid = adapterDesc.AdapterLuid;
            queryStatistics.hProcess = ProcessHandle;
            queryStatistics.QueryProcessSegment.SegmentId = i;
            if (NT_SUCCESS(queryD3DKMTStatistics(&queryStatistics))) {
              ULONGLONG bytesCommitted =
                  queryStatistics.QueryResult.ProcessSegmentInfo.BytesCommitted;
              if (aperture)
                sharedBytesUsed += bytesCommitted;
              else
                dedicatedBytesUsed += bytesCommitted;
            }
          }
        }
      }
    }

    FreeLibrary(gdi32Handle);

    MOZ_COLLECT_REPORT("gpu-committed", KIND_OTHER, UNITS_BYTES,
                       committedBytesUsed,
                       "Memory committed by the Windows graphics system.");

    MOZ_COLLECT_REPORT(
        "gpu-dedicated", KIND_OTHER, UNITS_BYTES, dedicatedBytesUsed,
        "Out-of-process memory allocated for this process in a physical "
        "GPU adapter's memory.");

    MOZ_COLLECT_REPORT("gpu-shared", KIND_OTHER, UNITS_BYTES, sharedBytesUsed,
                       "In-process memory that is shared with the GPU.");

    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(GPUAdapterReporter, nsIMemoryReporter)

Atomic<size_t> gfxWindowsPlatform::sD3D11SharedTextures;
Atomic<size_t> gfxWindowsPlatform::sD3D9SharedTextures;

class D3DSharedTexturesReporter final : public nsIMemoryReporter {
  ~D3DSharedTexturesReporter() {}

 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override {
    if (gfxWindowsPlatform::sD3D11SharedTextures > 0) {
      MOZ_COLLECT_REPORT("d3d11-shared-textures", KIND_OTHER, UNITS_BYTES,
                         gfxWindowsPlatform::sD3D11SharedTextures,
                         "D3D11 shared textures.");
    }

    if (gfxWindowsPlatform::sD3D9SharedTextures > 0) {
      MOZ_COLLECT_REPORT("d3d9-shared-textures", KIND_OTHER, UNITS_BYTES,
                         gfxWindowsPlatform::sD3D9SharedTextures,
                         "D3D9 shared textures.");
    }

    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(D3DSharedTexturesReporter, nsIMemoryReporter)

gfxWindowsPlatform::gfxWindowsPlatform() {
  // If win32k is locked down then we can't use COM STA and shouldn't need it.
  // Also, we won't be using any GPU memory in this process.
  if (!IsWin32kLockedDown()) {
    /*
     * Initialize COM
     */
    CoInitialize(nullptr);

    RegisterStrongMemoryReporter(new GfxD2DVramReporter());
    RegisterStrongMemoryReporter(new GPUAdapterReporter());
    RegisterStrongMemoryReporter(new D3DSharedTexturesReporter());
  }
}

gfxWindowsPlatform::~gfxWindowsPlatform() {
  mozilla::gfx::Factory::D2DCleanup();

  DeviceManagerDx::Shutdown();

  // We don't initialize COM when win32k is locked down.
  if (!IsWin32kLockedDown()) {
    /*
     * Uninitialize COM
     */
    CoUninitialize();
  }
}

/* static */
void gfxWindowsPlatform::InitMemoryReportersForGPUProcess() {
  MOZ_RELEASE_ASSERT(XRE_IsGPUProcess());

  RegisterStrongMemoryReporter(new GfxD2DVramReporter());
  RegisterStrongMemoryReporter(new GPUAdapterReporter());
  RegisterStrongMemoryReporter(new D3DSharedTexturesReporter());
}

/* static */
nsresult gfxWindowsPlatform::GetGpuTimeSinceProcessStartInMs(
    uint64_t* aResult) {
  // If win32k is locked down then we should not have any GPU processing and
  // cannot use these APIs either way.
  if (IsWin32kLockedDown()) {
    *aResult = 0;
    return NS_OK;
  }

  nsModuleHandle module(LoadLibrary(L"gdi32.dll"));
  if (!module) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  PFND3DKMTQS queryD3DKMTStatistics =
      (PFND3DKMTQS)GetProcAddress(module, "D3DKMTQueryStatistics");
  if (!queryD3DKMTStatistics) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  gfx::DeviceManagerDx* dm = DeviceManagerDx::Get();
  if (!dm) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  D3D11DeviceStatus status;
  if (!dm->ExportDeviceInfo(&status)) {
    // Assume that we used 0ms of GPU time if the device manager
    // doesn't know the device status.
    *aResult = 0;
    return NS_OK;
  }

  const DxgiAdapterDesc& adapterDesc = status.adapter();

  D3DKMTQS queryStatistics;
  memset(&queryStatistics, 0, sizeof(D3DKMTQS));
  queryStatistics.Type = D3DKMTQS_ADAPTER;
  queryStatistics.AdapterLuid = adapterDesc.AdapterLuid;
  if (!NT_SUCCESS(queryD3DKMTStatistics(&queryStatistics))) {
    return NS_ERROR_FAILURE;
  }

  uint64_t result = 0;
  ULONG nodeCount = queryStatistics.QueryResult.AdapterInfo.NodeCount;
  for (ULONG i = 0; i < nodeCount; ++i) {
    memset(&queryStatistics, 0, sizeof(D3DKMTQS));
    queryStatistics.Type = D3DKMTQS_PROCESS_NODE;
    queryStatistics.AdapterLuid = adapterDesc.AdapterLuid;
    queryStatistics.hProcess = GetCurrentProcess();
    queryStatistics.QueryProcessNode.NodeId = i;
    if (NT_SUCCESS(queryD3DKMTStatistics(&queryStatistics))) {
      result += queryStatistics.QueryResult.ProcessNodeInformation.RunningTime
                    .QuadPart *
                100 / PR_NSEC_PER_MSEC;
    }
  }

  *aResult = result;
  return NS_OK;
}

static void UpdateANGLEConfig() {
  if (!gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    gfxConfig::Disable(Feature::D3D11_HW_ANGLE, FeatureStatus::Disabled,
                       "D3D11 compositing is disabled",
                       "FEATURE_FAILURE_HW_ANGLE_D3D11_DISABLED"_ns);
  }
}

void gfxWindowsPlatform::InitAcceleration() {
  gfxPlatform::InitAcceleration();

  DeviceManagerDx::Init();

  InitializeConfig();
  InitGPUProcessSupport();
  // Ensure devices initialization. SharedSurfaceANGLE and
  // SharedSurfaceD3D11Interop use them. The devices are lazily initialized
  // with WebRender to reduce memory usage.
  // Initialize them now when running non-e10s.
  if (!BrowserTabsRemoteAutostart()) {
    EnsureDevicesInitialized();
  }
  UpdateANGLEConfig();
  UpdateRenderMode();

  // If we have Skia and we didn't init dwrite already, do it now.
  if (!DWriteEnabled() && GetDefaultContentBackend() == BackendType::SKIA) {
    InitDWriteSupport();
  }
  // We need to listen for font setting changes even if DWrite is not used.
  Factory::SetSystemTextQuality(gfxVars::SystemTextQuality());
  gfxVars::SetSystemTextQualityListener(
      gfxDWriteFont::SystemTextQualityChanged);

  // CanUseHardwareVideoDecoding depends on DeviceManagerDx state,
  // so update the cached value now.
  UpdateCanUseHardwareVideoDecoding();

  // Our ScreenHelperWin also depends on DeviceManagerDx state.
  if (XRE_IsParentProcess() && !gfxPlatform::IsHeadless()) {
    ScreenHelperWin::RefreshScreens();
  }

  RecordStartupTelemetry();
}

void gfxWindowsPlatform::InitWebRenderConfig() {
  gfxPlatform::InitWebRenderConfig();
  UpdateBackendPrefs();
}

bool gfxWindowsPlatform::CanUseHardwareVideoDecoding() {
  DeviceManagerDx* dm = DeviceManagerDx::Get();
  if (!dm) {
    return false;
  }
  if (!dm->TextureSharingWorks()) {
    return false;
  }
  return !dm->IsWARP() && gfxPlatform::CanUseHardwareVideoDecoding();
}

bool gfxWindowsPlatform::InitDWriteSupport() {
  mozilla::ScopedGfxFeatureReporter reporter("DWrite");
  if (!gfxDWriteFont::InitDWriteSupport()) {
    return false;
  }

  reporter.SetSuccessful();
  return true;
}

bool gfxWindowsPlatform::HandleDeviceReset() {
  mozilla::gfx::DeviceResetReason resetReason =
      mozilla::gfx::DeviceResetReason::OK;
  if (!DidRenderingDeviceReset(&resetReason)) {
    return false;
  }

  if (resetReason != mozilla::gfx::DeviceResetReason::FORCED_RESET) {
    glean::gfx::device_reset_reason.AccumulateSingleSample(
        uint32_t(resetReason));
  }

  // Remove devices and adapters.
  DeviceManagerDx::Get()->ResetDevices();

  imgLoader::NormalLoader()->ClearCache(Nothing());
  imgLoader::PrivateBrowsingLoader()->ClearCache(Nothing());
  gfxAlphaBoxBlur::ShutdownBlurCache();

  gfxConfig::Reset(Feature::D3D11_COMPOSITING);
  gfxConfig::Reset(Feature::D3D11_HW_ANGLE);
  gfxConfig::Reset(Feature::DIRECT2D);

  InitializeConfig();
  // XXX Add InitWebRenderConfig() calling.
  if (mInitializedDevices) {
    InitGPUProcessSupport();
    InitializeDevices();
  }
  UpdateANGLEConfig();
  return true;
}

BackendPrefsData gfxWindowsPlatform::GetBackendPrefs() const {
  BackendPrefsData data;

  data.mCanvasBitmask = BackendTypeBit(BackendType::SKIA);
  data.mContentBitmask = BackendTypeBit(BackendType::SKIA);
  data.mCanvasDefault = BackendType::SKIA;
  data.mContentDefault = BackendType::SKIA;

  if (gfxConfig::IsEnabled(Feature::DIRECT2D)) {
    data.mCanvasBitmask |= BackendTypeBit(BackendType::DIRECT2D1_1);
    data.mCanvasDefault = BackendType::DIRECT2D1_1;
  }
  return data;
}

void gfxWindowsPlatform::UpdateBackendPrefs() {
  BackendPrefsData data = GetBackendPrefs();
  // Remove DIRECT2D1 preference if D2D1Device does not exist.
  if (!Factory::HasD2D1Device()) {
    data.mContentBitmask &= ~BackendTypeBit(BackendType::DIRECT2D1_1);
    if (data.mContentDefault == BackendType::DIRECT2D1_1) {
      data.mContentDefault = BackendType::SKIA;
    }

    // Don't exclude DIRECT2D1_1 if using remote canvas, because DIRECT2D1_1 and
    // hence the device will be used in the GPU process.
    if (!gfxPlatform::UseRemoteCanvas()) {
      data.mCanvasBitmask &= ~BackendTypeBit(BackendType::DIRECT2D1_1);
      if (data.mCanvasDefault == BackendType::DIRECT2D1_1) {
        data.mCanvasDefault = BackendType::SKIA;
      }
    }
  }
  InitBackendPrefs(std::move(data));
}

bool gfxWindowsPlatform::IsDirect2DBackend() {
  return GetDefaultContentBackend() == BackendType::DIRECT2D1_1;
}

void gfxWindowsPlatform::UpdateRenderMode() {
  bool didReset = HandleDeviceReset();

  UpdateBackendPrefs();

  if (didReset) {
    mScreenReferenceDrawTarget = CreateOffscreenContentDrawTarget(
        IntSize(1, 1), SurfaceFormat::B8G8R8A8);
    if (!mScreenReferenceDrawTarget) {
      gfxCriticalNote
          << "Failed to update reference draw target after device reset"
          << ", D3D11 device:" << hexa(Factory::GetDirect3D11Device().get())
          << ", D3D11 status:"
          << FeatureStatusToString(
                 gfxConfig::GetValue(Feature::D3D11_COMPOSITING))
          << ", D2D1 device:" << hexa(Factory::GetD2D1Device().get())
          << ", D2D1 status:"
          << FeatureStatusToString(gfxConfig::GetValue(Feature::DIRECT2D))
          << ", content:" << int(GetDefaultContentBackend())
          << ", compositor:" << int(GetCompositorBackend());
      MOZ_CRASH(
          "GFX: Failed to update reference draw target after device reset");
    }
  }
}

mozilla::gfx::BackendType gfxWindowsPlatform::GetContentBackendFor(
    mozilla::layers::LayersBackend aLayers) {
  mozilla::gfx::BackendType defaultBackend =
      gfxPlatform::GetDefaultContentBackend();
  if (aLayers == LayersBackend::LAYERS_WR &&
      gfx::gfxVars::UseWebRenderANGLE()) {
    return defaultBackend;
  }

  if (defaultBackend == BackendType::DIRECT2D1_1) {
    // We can't have D2D without D3D11 layers, so fallback to Skia.
    return BackendType::SKIA;
  }

  // Otherwise we have some non-accelerated backend and that's ok.
  return defaultBackend;
}

mozilla::gfx::BackendType gfxWindowsPlatform::GetPreferredCanvasBackend() {
  mozilla::gfx::BackendType backend = gfxPlatform::GetPreferredCanvasBackend();

  if (backend == BackendType::DIRECT2D1_1) {
    if (!gfx::gfxVars::UseWebRenderANGLE()) {
      // We can't have D2D without ANGLE when WebRender is enabled, so fallback
      // to Skia.
      return BackendType::SKIA;
    }

    // Fall back to software when remote canvas has been deactivated.
    if (CanvasChild::Deactivated()) {
      return BackendType::SKIA;
    }
  }
  return backend;
}

bool gfxWindowsPlatform::CreatePlatformFontList() {
  if (DWriteEnabled()) {
    if (gfxPlatformFontList::Initialize(new gfxDWriteFontList)) {
      return true;
    }

    // DWrite font initialization failed! Don't know why this would happen,
    // but apparently it can - see bug 594865.
    // So we're going to fall back to GDI fonts & rendering.
    DisableD2D(FeatureStatus::Failed, "Failed to initialize fonts",
               "FEATURE_FAILURE_FONT_FAIL"_ns);
  }

  // Make sure the static variable is initialized...
  gfxPlatform::HasVariationFontSupport();
  // ...then force it to false, even if the Windows version was recent enough
  // to permit it, as we're using GDI fonts.
  sHasVariationFontSupport = false;

  return gfxPlatformFontList::Initialize(new gfxGDIFontList);
}

// This function will permanently disable D2D for the session. It's intended to
// be used when, after initially chosing to use Direct2D, we encounter a
// scenario we can't support.
//
// This is called during gfxPlatform::Init() so at this point there should be no
// DrawTargetD2D/1 instances.
void gfxWindowsPlatform::DisableD2D(FeatureStatus aStatus, const char* aMessage,
                                    const nsACString& aFailureId) {
  gfxConfig::SetFailed(Feature::DIRECT2D, aStatus, aMessage, aFailureId);
  Factory::SetDirect3D11Device(nullptr);
  UpdateBackendPrefs();
}

already_AddRefed<gfxASurface> gfxWindowsPlatform::CreateOffscreenSurface(
    const IntSize& aSize, gfxImageFormat aFormat) {
  if (!Factory::AllowedSurfaceSize(aSize)) {
    return nullptr;
  }

  RefPtr<gfxASurface> surf = nullptr;

#ifdef CAIRO_HAS_WIN32_SURFACE
  if (!XRE_IsContentProcess()) {
    surf = new gfxWindowsSurface(aSize, aFormat);
  }
#endif

  if (!surf || surf->CairoStatus()) {
    surf = new gfxImageSurface(aSize, aFormat);
  }

  return surf.forget();
}

void gfxWindowsPlatform::GetCommonFallbackFonts(
    uint32_t aCh, Script aRunScript, FontPresentation aPresentation,
    nsTArray<const char*>& aFontList) {
  if (PrefersColor(aPresentation)) {
    aFontList.AppendElement("Segoe UI Emoji");
    aFontList.AppendElement("Twemoji Mozilla");
  }

  switch (aRunScript) {
    case Script::INVALID:
    case Script::NUM_SCRIPT_CODES:
      // Ensure the switch covers all the Script enum values.
      MOZ_ASSERT_UNREACHABLE("bad script code");
      break;

    case Script::COMMON:
    case Script::INHERITED:
      // In most cases, COMMON and INHERITED characters will be merged into
      // their context, but if they occur without context, we'll just treat
      // them like Latin, etc.
    case Script::LATIN:
    case Script::CYRILLIC:
    case Script::GREEK:
    case Script::ARMENIAN:
    case Script::HEBREW:
      // We always append Arial below, so no need to add it here.
      // aFontList.AppendElement("Arial");
      break;

    case Script::MATHEMATICAL_NOTATION:
    case Script::SYMBOLS:
    case Script::SYMBOLS_EMOJI:
      // Not currently returned by script run resolution (but some symbols may
      // be handled below).
      break;

      // CJK-related script codes are a bit troublesome because of unification;
      // we'll probably just get HAN much of the time, so the choice of which
      // language font to try for fallback is rather arbitrary. Usually, though,
      // we hope that font prefs will have handled this earlier.
    case Script::BOPOMOFO:
    case Script::HAN_WITH_BOPOMOFO:
    case Script::SIMPLIFIED_HAN:
    case Script::HAN:
      aFontList.AppendElement("SimSun");
      if (aCh > 0xFFFF) {
        aFontList.AppendElement("SimSun-ExtB");
      }
      break;
      // Currently, we don't resolve script runs to this value, but we may do so
      // in future if we get better at handling things like `lang=zh-Hant`, not
      // just resolving based on the Unicode text.
    case Script::TRADITIONAL_HAN:
      aFontList.AppendElement("MingLiU");
      if (aCh > 0xFFFF) {
        aFontList.AppendElement("MingLiU-ExtB");
      }
      break;
    case Script::HIRAGANA:
    case Script::KATAKANA:
    case Script::KATAKANA_OR_HIRAGANA:
    case Script::JAPANESE:
      aFontList.AppendElement("Yu Gothic");
      aFontList.AppendElement("MS PGothic");
      break;
    case Script::HANGUL:
    case Script::JAMO:
    case Script::KOREAN:
      aFontList.AppendElement("Malgun Gothic");
      break;

    case Script::YI:
      aFontList.AppendElement("Microsoft Yi Baiti");
      break;
    case Script::MONGOLIAN:
      aFontList.AppendElement("Mongolian Baiti");
      break;
    case Script::TIBETAN:
      aFontList.AppendElement("Microsoft Himalaya");
      break;
    case Script::PHAGS_PA:
      aFontList.AppendElement("Microsoft PhagsPa");
      break;

    case Script::ARABIC:
      // Default to Arial (added unconditionally below) for Arabic script.
      break;
    case Script::ARABIC_NASTALIQ:
      aFontList.AppendElement("Urdu Typesetting");
      break;
    case Script::SYRIAC:
    case Script::ESTRANGELO_SYRIAC:
      aFontList.AppendElement("Estrangelo Edessa");
      break;
    case Script::THAANA:
      aFontList.AppendElement("MV Boli");
      break;

    case Script::BENGALI:
      aFontList.AppendElement("Vrinda");
      aFontList.AppendElement("Nirmala UI");
      break;
    case Script::DEVANAGARI:
      aFontList.AppendElement("Kokila");
      aFontList.AppendElement("Nirmala UI");
      break;
    case Script::GUJARATI:
      aFontList.AppendElement("Shruti");
      aFontList.AppendElement("Nirmala UI");
      break;
    case Script::GURMUKHI:
      aFontList.AppendElement("Raavi");
      aFontList.AppendElement("Nirmala UI");
      break;
    case Script::KANNADA:
      aFontList.AppendElement("Tunga");
      aFontList.AppendElement("Nirmala UI");
      break;
    case Script::MALAYALAM:
      aFontList.AppendElement("Kartika");
      aFontList.AppendElement("Nirmala UI");
      break;
    case Script::ORIYA:
      aFontList.AppendElement("Kalinga");
      aFontList.AppendElement("Nirmala UI");
      break;
    case Script::TAMIL:
      aFontList.AppendElement("Latha");
      aFontList.AppendElement("Nirmala UI");
      break;
    case Script::TELUGU:
      aFontList.AppendElement("Gautami");
      aFontList.AppendElement("Nirmala UI");
      break;
    case Script::SINHALA:
      aFontList.AppendElement("Iskoola Pota");
      aFontList.AppendElement("Nirmala UI");
      break;

    case Script::CHAKMA:
    case Script::MEETEI_MAYEK:
    case Script::OL_CHIKI:
    case Script::SORA_SOMPENG:
      aFontList.AppendElement("Nirmala UI");
      break;

    case Script::MYANMAR:
      aFontList.AppendElement("Myanmar Text");
      break;
    case Script::KHMER:
      aFontList.AppendElement("Khmer UI");
      break;
    case Script::LAO:
      aFontList.AppendElement("Lao UI");
      break;
    case Script::THAI:
      aFontList.AppendElement("Tahoma");
      aFontList.AppendElement("Leelawadee UI");
      break;
    case Script::TAI_LE:
      aFontList.AppendElement("Microsoft Tai Le");
      break;
    case Script::BUGINESE:
      aFontList.AppendElement("Leelawadee UI");
      break;
    case Script::NEW_TAI_LUE:
      aFontList.AppendElement("Microsoft New Tai Lue");
      break;
    case Script::JAVANESE:
      aFontList.AppendElement("Javanese Text");
      break;

    case Script::GEORGIAN:
    case Script::KHUTSURI:
    case Script::LISU:
      aFontList.AppendElement("Segoe UI");
      break;

    case Script::ETHIOPIC:
      aFontList.AppendElement("Nyala");
      aFontList.AppendElement("Ebrima");
      break;

    case Script::ADLAM:
    case Script::NKO:
    case Script::OSMANYA:
    case Script::TIFINAGH:
    case Script::VAI:
      aFontList.AppendElement("Ebrima");
      break;

    case Script::CANADIAN_ABORIGINAL:
      aFontList.AppendElement("Euphemia");
      break;

    case Script::CHEROKEE:
    case Script::OSAGE:
      aFontList.AppendElement("Gadugi");
      break;

    case Script::BRAILLE:
    case Script::DESERET:
      aFontList.AppendElement("Segoe UI Symbol");
      break;

    case Script::BRAHMI:
    case Script::CARIAN:
    case Script::CUNEIFORM:
    case Script::CYPRIOT:
    case Script::EGYPTIAN_HIEROGLYPHS:
    case Script::GLAGOLITIC:
    case Script::GOTHIC:
    case Script::IMPERIAL_ARAMAIC:
    case Script::INSCRIPTIONAL_PAHLAVI:
    case Script::INSCRIPTIONAL_PARTHIAN:
    case Script::KHAROSHTHI:
    case Script::LYCIAN:
    case Script::LYDIAN:
    case Script::MEROITIC_CURSIVE:
    case Script::OGHAM:
    case Script::OLD_ITALIC:
    case Script::OLD_PERSIAN:
    case Script::OLD_SOUTH_ARABIAN:
    case Script::OLD_TURKIC:
    case Script::PHOENICIAN:
    case Script::RUNIC:
    case Script::SHAVIAN:
    case Script::UGARITIC:
      aFontList.AppendElement("Segoe UI Historic");
      break;

      // For some scripts where Windows doesn't supply a font by default,
      // there are Noto fonts that users might have installed:
    case Script::AHOM:
      aFontList.AppendElement("Noto Serif Ahom");
      break;
    case Script::AVESTAN:
      aFontList.AppendElement("Noto Sans Avestan");
      break;
    case Script::BALINESE:
      aFontList.AppendElement("Noto Sans Balinese");
      break;
    case Script::BAMUM:
      aFontList.AppendElement("Noto Sans Bamum");
      break;
    case Script::BASSA_VAH:
      aFontList.AppendElement("Noto Sans Bassa Vah");
      break;
    case Script::BATAK:
      aFontList.AppendElement("Noto Sans Batak");
      break;
    case Script::BHAIKSUKI:
      aFontList.AppendElement("Noto Sans Bhaiksuki");
      break;
    case Script::BUHID:
      aFontList.AppendElement("Noto Sans Buhid");
      break;
    case Script::CAUCASIAN_ALBANIAN:
      aFontList.AppendElement("Noto Sans Caucasian Albanian");
      break;
    case Script::CHAM:
      aFontList.AppendElement("Noto Sans Cham");
      break;
    case Script::COPTIC:
      aFontList.AppendElement("Noto Sans Coptic");
      break;
    case Script::DUPLOYAN:
      aFontList.AppendElement("Noto Sans Duployan");
      break;
    case Script::ELBASAN:
      aFontList.AppendElement("Noto Sans Elbasan");
      break;
    case Script::GRANTHA:
      aFontList.AppendElement("Noto Sans Grantha");
      break;
    case Script::HANIFI_ROHINGYA:
      aFontList.AppendElement("Noto Sans Hanifi Rohingya");
      break;
    case Script::HANUNOO:
      aFontList.AppendElement("Noto Sans Hanunoo");
      break;
    case Script::HATRAN:
      aFontList.AppendElement("Noto Sans Hatran");
      break;
    case Script::KAITHI:
      aFontList.AppendElement("Noto Sans Kaithi");
      break;
    case Script::KAYAH_LI:
      aFontList.AppendElement("Noto Sans Kayah Li");
      break;
    case Script::KHOJKI:
      aFontList.AppendElement("Noto Sans Khojki");
      break;
    case Script::KHUDAWADI:
      aFontList.AppendElement("Noto Sans Khudawadi");
      break;
    case Script::LEPCHA:
      aFontList.AppendElement("Noto Sans Lepcha");
      break;
    case Script::LIMBU:
      aFontList.AppendElement("Noto Sans Limbu");
      break;
    case Script::LINEAR_A:
      aFontList.AppendElement("Noto Sans Linear A");
      break;
    case Script::LINEAR_B:
      aFontList.AppendElement("Noto Sans Linear B");
      break;
    case Script::MAHAJANI:
      aFontList.AppendElement("Noto Sans Mahajani");
      break;
    case Script::MANDAIC:
      aFontList.AppendElement("Noto Sans Mandaic");
      break;
    case Script::MANICHAEAN:
      aFontList.AppendElement("Noto Sans Manichaean");
      break;
    case Script::MARCHEN:
      aFontList.AppendElement("Noto Sans Marchen");
      break;
    case Script::MENDE_KIKAKUI:
      aFontList.AppendElement("Noto Sans Mende Kikakui");
      break;
    case Script::MEROITIC_HIEROGLYPHS:
      aFontList.AppendElement("Noto Sans Meroitic");
      break;
    case Script::MIAO:
      aFontList.AppendElement("Noto Sans Miao");
      break;
    case Script::MODI:
      aFontList.AppendElement("Noto Sans Modi");
      break;
    case Script::MRO:
      aFontList.AppendElement("Noto Sans Mro");
      break;
    case Script::MULTANI:
      aFontList.AppendElement("Noto Sans Multani");
      break;
    case Script::NABATAEAN:
      aFontList.AppendElement("Noto Sans Nabataean");
      break;
    case Script::NEWA:
      aFontList.AppendElement("Noto Sans Newa");
      break;
    case Script::OLD_HUNGARIAN:
      aFontList.AppendElement("Noto Sans Old Hungarian");
      break;
    case Script::OLD_NORTH_ARABIAN:
      aFontList.AppendElement("Noto Sans Old North Arabian");
      break;
    case Script::OLD_PERMIC:
      aFontList.AppendElement("Noto Sans Old Permic");
      break;
    case Script::PAHAWH_HMONG:
      aFontList.AppendElement("Noto Sans Pahawh Hmong");
      break;
    case Script::PALMYRENE:
      aFontList.AppendElement("Noto Sans Palmyrene");
      break;
    case Script::PAU_CIN_HAU:
      aFontList.AppendElement("Noto Sans Pau Cin Hau");
      break;
    case Script::PSALTER_PAHLAVI:
      aFontList.AppendElement("Noto Sans Psalter Pahlavi");
      break;
    case Script::REJANG:
      aFontList.AppendElement("Noto Sans Rejang");
      break;
    case Script::SAMARITAN:
      aFontList.AppendElement("Noto Sans Samaritan");
      break;
    case Script::SAURASHTRA:
      aFontList.AppendElement("Noto Sans Saurashtra");
      break;
    case Script::SHARADA:
      aFontList.AppendElement("Noto Sans Sharada");
      break;
    case Script::SIDDHAM:
      aFontList.AppendElement("Noto Sans Siddham");
      break;
    case Script::SUNDANESE:
      aFontList.AppendElement("Noto Sans Sundanese");
      break;
    case Script::SYLOTI_NAGRI:
      aFontList.AppendElement("Noto Sans Syloti Nagri");
      break;
    case Script::TAGALOG:
      aFontList.AppendElement("Noto Sans Tagalog");
      break;
    case Script::TAGBANWA:
      aFontList.AppendElement("Noto Sans Tagbanwa");
      break;
    case Script::TAI_THAM:
      aFontList.AppendElement("Noto Sans Tai Tham");
      break;
    case Script::TAI_VIET:
      aFontList.AppendElement("Noto Sans Tai Viet");
      break;
    case Script::TAKRI:
      aFontList.AppendElement("Noto Sans Takri");
      break;
    case Script::TIRHUTA:
      aFontList.AppendElement("Noto Sans Tirhuta");
      break;
    case Script::WANCHO:
      aFontList.AppendElement("Noto Sans Wancho");
      break;
    case Script::WARANG_CITI:
      aFontList.AppendElement("Noto Sans Warang Citi");
      break;

    case Script::AFAKA:
    case Script::ANATOLIAN_HIEROGLYPHS:
    case Script::BLISSYMBOLS:
    case Script::BOOK_PAHLAVI:
    case Script::CHORASMIAN:
    case Script::CIRTH:
    case Script::CYPRO_MINOAN:
    case Script::DEMOTIC_EGYPTIAN:
    case Script::DIVES_AKURU:
    case Script::DOGRA:
    case Script::EASTERN_SYRIAC:
    case Script::ELYMAIC:
    case Script::GARAY:
    case Script::GUNJALA_GONDI:
    case Script::GURUNG_KHEMA:
    case Script::HARAPPAN_INDUS:
    case Script::HIERATIC_EGYPTIAN:
    case Script::JURCHEN:
    case Script::KAWI:
    case Script::KHITAN_SMALL_SCRIPT:
    case Script::KIRAT_RAI:
    case Script::KPELLE:
    case Script::LATIN_FRAKTUR:
    case Script::LATIN_GAELIC:
    case Script::LOMA:
    case Script::MAKASAR:
    case Script::MASARAM_GONDI:
    case Script::MAYAN_HIEROGLYPHS:
    case Script::MEDEFAIDRIN:
    case Script::MOON:
    case Script::NAG_MUNDARI:
    case Script::NAKHI_GEBA:
    case Script::NANDINAGARI:
    case Script::NUSHU:
    case Script::NYIAKENG_PUACHUE_HMONG:
    case Script::OL_ONAL:
    case Script::OLD_CHURCH_SLAVONIC_CYRILLIC:
    case Script::OLD_SOGDIAN:
    case Script::OLD_UYGHUR:
    case Script::RONGORONGO:
    case Script::SARATI:
    case Script::SIGNWRITING:
    case Script::SOGDIAN:
    case Script::SOYOMBO:
    case Script::SUNUWAR:
    case Script::TANGSA:
    case Script::TANGUT:
    case Script::TENGWAR:
    case Script::TODHRI:
    case Script::TOTO:
    case Script::TULU_TIGALARI:
    case Script::UNKNOWN:
    case Script::UNWRITTEN_LANGUAGES:
    case Script::VISIBLE_SPEECH:
    case Script::VITHKUQI:
    case Script::WESTERN_SYRIAC:
    case Script::WOLEAI:
    case Script::YEZIDI:
    case Script::ZANABAZAR_SQUARE:
      break;
  }

  // Arial is used as default fallback for system fallback, so always try that.
  aFontList.AppendElement("Arial");

  // Symbols/dingbats are generally Script=COMMON but may be resolved to any
  // surrounding script run. So we'll always append a couple of likely fonts
  // for such characters.
  const uint32_t b = aCh >> 8;
  if (aRunScript == Script::COMMON ||  // Stray COMMON chars not resolved
      (b >= 0x20 && b <= 0x2b) || b == 0x2e ||  // BMP symbols/punctuation/etc
      GetGenCategory(aCh) == nsUGenCategory::kSymbol ||
      GetGenCategory(aCh) == nsUGenCategory::kPunctuation) {
    // Segoe UI handles some punctuation/symbols that are missing from many text
    // fonts.
    aFontList.AppendElement("Segoe UI");
    aFontList.AppendElement("Segoe UI Symbol");
    aFontList.AppendElement("Cambria Math");
  }

  // Arial Unicode MS also has lots of glyphs for obscure characters; try it as
  // a last resort, if available.
  aFontList.AppendElement("Arial Unicode MS");

  // If we didn't begin with the color-emoji fonts, include them here
  // so that they'll be preferred over user-installed (and possibly
  // broken) fonts in the global fallback path.
  if (!PrefersColor(aPresentation)) {
    aFontList.AppendElement("Segoe UI Emoji");
    aFontList.AppendElement("Twemoji Mozilla");
  }
}

bool gfxWindowsPlatform::DidRenderingDeviceReset(
    mozilla::gfx::DeviceResetReason* aResetReason) {
  DeviceManagerDx* dm = DeviceManagerDx::Get();
  if (!dm) {
    return false;
  }
  return dm->HasDeviceReset(aResetReason);
}

void gfxWindowsPlatform::CompositorUpdated() {
  DeviceManagerDx::Get()->ForceDeviceReset(
      mozilla::gfx::ForcedDeviceResetReason::COMPOSITOR_UPDATED);
  UpdateRenderMode();
}

BOOL CALLBACK InvalidateWindowForDeviceReset(HWND aWnd, LPARAM aMsg) {
  RedrawWindow(aWnd, nullptr, nullptr,
               RDW_INVALIDATE | RDW_INTERNALPAINT | RDW_FRAME);
  return TRUE;
}

void gfxWindowsPlatform::SchedulePaintIfDeviceReset() {
  AUTO_PROFILER_LABEL("gfxWindowsPlatform::SchedulePaintIfDeviceReset", OTHER);

  mozilla::gfx::DeviceResetReason resetReason =
      mozilla::gfx::DeviceResetReason::OK;
  if (!DidRenderingDeviceReset(&resetReason)) {
    return;
  }

  gfxCriticalNote << "(gfxWindowsPlatform) Detected device reset: "
                  << (int)resetReason;

  if (XRE_IsParentProcess()) {
    // Trigger an ::OnPaint for each window.
    ::EnumThreadWindows(GetCurrentThreadId(), InvalidateWindowForDeviceReset,
                        0);
  } else {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "gfx::gfxWindowsPlatform::SchedulePaintIfDeviceReset", []() -> void {
          gfxWindowsPlatform::GetPlatform()->CheckForContentOnlyDeviceReset();
        }));
  }

  gfxCriticalNote << "(gfxWindowsPlatform) scheduled device update.";
}

void gfxWindowsPlatform::CheckForContentOnlyDeviceReset() {
  if (!DidRenderingDeviceReset()) {
    return;
  }

  bool isContentOnlyTDR;
  D3D11DeviceStatus status;

  DeviceManagerDx::Get()->ExportDeviceInfo(&status);
  CompositorBridgeChild::Get()->SendCheckContentOnlyTDR(status.sequenceNumber(),
                                                        &isContentOnlyTDR);

  // The parent process doesn't know about the reset yet, or the reset is
  // local to our device.
  if (isContentOnlyTDR) {
    gfxCriticalNote << "A content-only TDR is detected.";
    dom::ContentChild* cc = dom::ContentChild::GetSingleton();
    cc->RecvReinitRenderingForDeviceReset();
  }
}

nsTArray<uint8_t> gfxWindowsPlatform::GetPlatformCMSOutputProfileData() {
  if (XRE_IsContentProcess()) {
    auto& cmsOutputProfileData = GetCMSOutputProfileData();
    // We should have set our profile data when we received our initial
    // ContentDeviceData.
    MOZ_ASSERT(cmsOutputProfileData.isSome(),
               "Should have created output profile data when we received "
               "initial content device data.");

    // If we have data, it should not be empty.
    MOZ_ASSERT_IF(cmsOutputProfileData.isSome(),
                  !cmsOutputProfileData->IsEmpty());

    if (cmsOutputProfileData.isSome()) {
      return cmsOutputProfileData.ref().Clone();
    }
    return nsTArray<uint8_t>();
  }

  return GetPlatformCMSOutputProfileData_Impl();
}

nsTArray<uint8_t> gfxWindowsPlatform::GetPlatformCMSOutputProfileData_Impl() {
  static nsTArray<uint8_t> sCached = [&] {
    // Check override pref first:
    nsTArray<uint8_t> prefProfileData =
        gfxPlatform::GetPrefCMSOutputProfileData();
    if (!prefProfileData.IsEmpty()) {
      return prefProfileData;
    }

    // -
    // Otherwise, create a dummy DC and pull from that.

    HDC dc = ::GetDC(nullptr);
    if (!dc) {
      return nsTArray<uint8_t>();
    }

    WCHAR profilePath[MAX_PATH];
    DWORD profilePathLen = MAX_PATH;

    bool getProfileResult = ::GetICMProfileW(dc, &profilePathLen, profilePath);

    ::ReleaseDC(nullptr, dc);

    if (!getProfileResult) {
      return nsTArray<uint8_t>();
    }

    void* mem = nullptr;
    size_t size = 0;

    qcms_data_from_unicode_path(profilePath, &mem, &size);
    if (!mem) {
      return nsTArray<uint8_t>();
    }

    nsTArray<uint8_t> result;
    result.AppendElements(static_cast<uint8_t*>(mem), size);

    free(mem);

    return result;
  }();

  return sCached.Clone();
}

void gfxWindowsPlatform::GetDLLVersion(char16ptr_t aDLLPath,
                                       nsAString& aVersion) {
  DWORD versInfoSize, vers[4] = {0};
  // version info not available case
  aVersion.AssignLiteral(u"0.0.0.0");
  versInfoSize = GetFileVersionInfoSizeW(aDLLPath, nullptr);
  AutoTArray<BYTE, 512> versionInfo;

  if (versInfoSize == 0) {
    return;
  }

  // XXX(Bug 1631371) Check if this should use a fallible operation as it
  // pretended earlier.
  versionInfo.AppendElements(uint32_t(versInfoSize));

  if (!GetFileVersionInfoW(aDLLPath, 0, versInfoSize,
                           LPBYTE(versionInfo.Elements()))) {
    return;
  }

  UINT len = 0;
  VS_FIXEDFILEINFO* fileInfo = nullptr;
  if (!VerQueryValue(LPBYTE(versionInfo.Elements()), TEXT("\\"),
                     (LPVOID*)&fileInfo, &len) ||
      len == 0 || fileInfo == nullptr) {
    return;
  }

  DWORD fileVersMS = fileInfo->dwFileVersionMS;
  DWORD fileVersLS = fileInfo->dwFileVersionLS;

  vers[0] = HIWORD(fileVersMS);
  vers[1] = LOWORD(fileVersMS);
  vers[2] = HIWORD(fileVersLS);
  vers[3] = LOWORD(fileVersLS);

  char buf[256];
  SprintfLiteral(buf, "%lu.%lu.%lu.%lu", vers[0], vers[1], vers[2], vers[3]);
  aVersion.Assign(NS_ConvertUTF8toUTF16(buf));
}

static BOOL CALLBACK AppendClearTypeParams(HMONITOR aMonitor, HDC, LPRECT,
                                           LPARAM aContext) {
  MONITORINFOEXW monitorInfo;
  monitorInfo.cbSize = sizeof(MONITORINFOEXW);
  if (!GetMonitorInfoW(aMonitor, &monitorInfo)) {
    return TRUE;
  }

  ClearTypeParameterInfo ctinfo;
  ctinfo.displayName.Assign(monitorInfo.szDevice);

  RefPtr<IDWriteRenderingParams> renderingParams;
  HRESULT hr = Factory::GetDWriteFactory()->CreateMonitorRenderingParams(
      aMonitor, getter_AddRefs(renderingParams));
  if (FAILED(hr)) {
    return TRUE;
  }

  ctinfo.gamma = renderingParams->GetGamma() * 1000;
  ctinfo.pixelStructure = renderingParams->GetPixelGeometry();
  ctinfo.clearTypeLevel = renderingParams->GetClearTypeLevel() * 100;
  ctinfo.enhancedContrast = renderingParams->GetEnhancedContrast() * 100;

  auto* params = reinterpret_cast<nsTArray<ClearTypeParameterInfo>*>(aContext);
  params->AppendElement(ctinfo);
  return TRUE;
}

void gfxWindowsPlatform::GetCleartypeParams(
    nsTArray<ClearTypeParameterInfo>& aParams) {
  aParams.Clear();
  if (!DWriteEnabled()) {
    return;
  }
  EnumDisplayMonitors(nullptr, nullptr, AppendClearTypeParams,
                      reinterpret_cast<LPARAM>(&aParams));
}

void gfxWindowsPlatform::FontsPrefsChanged(const char* aPref) {
  bool clearTextFontCaches = true;

  gfxPlatform::FontsPrefsChanged(aPref);

  if (aPref &&
      !strncmp(GFX_CLEARTYPE_PARAMS, aPref, strlen(GFX_CLEARTYPE_PARAMS))) {
    gfxDWriteFont::UpdateClearTypeVars();
  } else {
    clearTextFontCaches = false;
  }

  if (clearTextFontCaches) {
    gfxFontCache* fc = gfxFontCache::GetCache();
    if (fc) {
      fc->Flush();
    }
  }
}

bool gfxWindowsPlatform::IsOptimus() {
  static int knowIsOptimus = -1;
  if (knowIsOptimus == -1) {
    // other potential optimus -- nvd3d9wrapx.dll & nvdxgiwrap.dll
    if (GetModuleHandleA("nvumdshim.dll") ||
        GetModuleHandleA("nvumdshimx.dll")) {
      knowIsOptimus = 1;
    } else {
      knowIsOptimus = 0;
    }
  }
  return knowIsOptimus;
}

static void InitializeANGLEConfig() {
  FeatureState& d3d11ANGLE = gfxConfig::GetFeature(Feature::D3D11_HW_ANGLE);

  if (!gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    d3d11ANGLE.DisableByDefault(FeatureStatus::Unavailable,
                                "D3D11 compositing is disabled",
                                "FEATURE_FAILURE_HW_ANGLE_D3D11_DISABLED"_ns);
    return;
  }

  d3d11ANGLE.EnableByDefault();

  nsCString message;
  nsCString failureId;
  if (!gfxPlatform::IsGfxInfoStatusOkay(nsIGfxInfo::FEATURE_DIRECT3D_11_ANGLE,
                                        &message, failureId)) {
    d3d11ANGLE.Disable(FeatureStatus::Blocklisted, message.get(), failureId);
  }
}

void gfxWindowsPlatform::InitializeDirectDrawConfig() {
  MOZ_ASSERT(XRE_IsParentProcess());

  FeatureState& ddraw = gfxConfig::GetFeature(Feature::DIRECT_DRAW);
  ddraw.EnableByDefault();
}

void gfxWindowsPlatform::InitializeConfig() {
  if (XRE_IsParentProcess()) {
    // The parent process first determines which features can be attempted.
    // This information is relayed to content processes and the GPU process.
    InitializeD3D11Config();
    InitializeANGLEConfig();
    InitializeD2DConfig();
  } else {
    ImportCachedContentDeviceData();
    InitializeANGLEConfig();
  }
}

void gfxWindowsPlatform::InitializeD3D11Config() {
  MOZ_ASSERT(XRE_IsParentProcess());

  FeatureState& d3d11 = gfxConfig::GetFeature(Feature::D3D11_COMPOSITING);

  if (!gfxConfig::IsEnabled(Feature::HW_COMPOSITING)) {
    d3d11.DisableByDefault(FeatureStatus::Unavailable,
                           "Hardware compositing is disabled",
                           "FEATURE_FAILURE_D3D11_NEED_HWCOMP"_ns);
    return;
  }

  d3d11.EnableByDefault();

  // Check if the user really, really wants WARP.
  if (StaticPrefs::layers_d3d11_force_warp_AtStartup()) {
    // Force D3D11 on even if we disabled it.
    d3d11.UserForceEnable("User force-enabled WARP");
  }

  nsCString message;
  nsCString failureId;
  if (StaticPrefs::layers_d3d11_enable_blacklist_AtStartup() &&
      !gfxPlatform::IsGfxInfoStatusOkay(nsIGfxInfo::FEATURE_DIRECT3D_11_LAYERS,
                                        &message, failureId)) {
    d3d11.Disable(FeatureStatus::Blocklisted, message.get(), failureId);
  }
}

/* static */
void gfxWindowsPlatform::RecordContentDeviceFailure(
    TelemetryDeviceCode aDevice) {
  // If the parent process fails to acquire a device, we record this
  // normally as part of the environment. The exceptional case we're
  // looking for here is when the parent process successfully acquires
  // a device, but the content process fails to acquire the same device.
  // This would not normally be displayed in about:support.
  if (!XRE_IsContentProcess()) {
    return;
  }
  glean::gfx::content_failed_to_acquire_device.AccumulateSingleSample(
      uint32_t(aDevice));
}

void gfxWindowsPlatform::RecordStartupTelemetry() {
  if (!XRE_IsParentProcess()) {
    return;
  }

  DeviceManagerDx* dx = DeviceManagerDx::Get();
  nsTArray<DXGI_OUTPUT_DESC1> outputs = dx->EnumerateOutputs();

  uint32_t allSupportedColorSpaces = 0;
  for (auto& output : outputs) {
    uint32_t colorSpace = 1 << output.ColorSpace;
    allSupportedColorSpaces |= colorSpace;
  }

  glean::gfx_hdr::windows_display_colorspace_bitfield.Set(
      allSupportedColorSpaces);
}

// Supports lazy device initialization on Windows, so that WebRender can avoid
// initializing GPU state and allocating swap chains for most non-GPU processes.
void gfxWindowsPlatform::EnsureDevicesInitialized() {
  MOZ_DIAGNOSTIC_ASSERT(!IsWin32kLockedDown());

  if (!mInitializedDevices) {
    mInitializedDevices = true;
    InitializeDevices();
    UpdateBackendPrefs();
  }
}

bool gfxWindowsPlatform::DevicesInitialized() { return mInitializedDevices; }

void gfxWindowsPlatform::InitializeDevices() {
  MOZ_ASSERT(NS_IsMainThread());

  if (XRE_IsParentProcess()) {
    // If we're the UI process, and the GPU process is enabled, then we don't
    // initialize any DirectX devices. We do leave them enabled in gfxConfig
    // though. If the GPU process fails to create these devices it will send
    // a message back and we'll update their status.
    if (gfxConfig::IsEnabled(Feature::GPU_PROCESS)) {
      return;
    }

    // No GPU process, continue initializing devices as normal.
  }

  // If acceleration is disabled, we refuse to initialize anything.
  if (!gfxConfig::IsEnabled(Feature::HW_COMPOSITING)) {
    return;
  }

  // If we previously crashed initializing devices, bail out now.
  D3D11LayersCrashGuard detectCrashes;
  if (detectCrashes.Crashed()) {
    gfxConfig::SetFailed(Feature::HW_COMPOSITING,
                         FeatureStatus::CrashedOnStartup,
                         "Crashed during startup in a previous session");
    gfxConfig::SetFailed(
        Feature::D3D11_COMPOSITING, FeatureStatus::CrashedOnStartup,
        "Harware acceleration crashed during startup in a previous session");
    gfxConfig::SetFailed(
        Feature::DIRECT2D, FeatureStatus::CrashedOnStartup,
        "Harware acceleration crashed during startup in a previous session");
    return;
  }

  bool shouldUseD2D = gfxConfig::IsEnabled(Feature::DIRECT2D);

  // First, initialize D3D11. If this succeeds we attempt to use Direct2D.
  InitializeD3D11();
  InitializeD2D();

  if (!gfxConfig::IsEnabled(Feature::DIRECT2D) && XRE_IsContentProcess() &&
      shouldUseD2D) {
    RecordContentDeviceFailure(TelemetryDeviceCode::D2D1);
  }
}

void gfxWindowsPlatform::InitializeD3D11() {
  // This function attempts to initialize our D3D11 devices, if the hardware
  // is not blocklisted for D3D11 layers. This first attempt will try to create
  // a hardware accelerated device. If this creation fails or the hardware is
  // blocklisted, then this function will abort if WARP is disabled, causing us
  // to fallback to Basic layers. If WARP is not disabled it will use a WARP
  // device which should always be available on Windows 7 and higher.
  if (!gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    return;
  }

  DeviceManagerDx* dm = DeviceManagerDx::Get();
  if (XRE_IsParentProcess()) {
    if (!dm->CreateCompositorDevices()) {
      return;
    }
  }

  dm->CreateContentDevices();

  // Content process failed to create the d3d11 device while parent process
  // succeed.
  if (XRE_IsContentProcess() &&
      !gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    gfxCriticalError()
        << "[D3D11] Failed to create the D3D11 device in content \
                           process.";
  }
}

void gfxWindowsPlatform::InitializeD2DConfig() {
  FeatureState& d2d1 = gfxConfig::GetFeature(Feature::DIRECT2D);

  if (!gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    d2d1.DisableByDefault(FeatureStatus::Unavailable,
                          "Direct2D requires Direct3D 11 compositing",
                          "FEATURE_FAILURE_D2D_D3D11_COMP"_ns);
    return;
  }

  d2d1.SetDefaultFromPref(StaticPrefs::GetPrefName_gfx_direct2d_disabled(),
                          false,
                          StaticPrefs::GetPrefDefault_gfx_direct2d_disabled());

  nsCString message;
  nsCString failureId;
  if (!gfxPlatform::IsGfxInfoStatusOkay(nsIGfxInfo::FEATURE_DIRECT2D, &message,
                                        failureId)) {
    d2d1.Disable(FeatureStatus::Blocklisted, message.get(), failureId);
  }

  if (!d2d1.IsEnabled() &&
      StaticPrefs::gfx_direct2d_force_enabled_AtStartup()) {
    d2d1.UserForceEnable("Force-enabled via user-preference");
  }
}

void gfxWindowsPlatform::InitializeD2D() {
  ScopedGfxFeatureReporter d2d1_1("D2D1.1");

  FeatureState& d2d1 = gfxConfig::GetFeature(Feature::DIRECT2D);

  DeviceManagerDx* dm = DeviceManagerDx::Get();

  // We don't know this value ahead of time, but the user can force-override
  // it, so we use Disable instead of SetFailed.
  if (dm->IsWARP()) {
    d2d1.Disable(FeatureStatus::Blocked,
                 "Direct2D is not compatible with Direct3D11 WARP",
                 "FEATURE_FAILURE_D2D_WARP_BLOCK"_ns);
  }

  // If we pass all the initial checks, we can proceed to runtime decisions.
  if (!d2d1.IsEnabled()) {
    return;
  }

  if (!Factory::SupportsD2D1()) {
    d2d1.SetFailed(FeatureStatus::Unavailable,
                   "Failed to acquire a Direct2D 1.1 factory",
                   "FEATURE_FAILURE_D2D_FACTORY"_ns);
    return;
  }

  if (!dm->GetContentDevice()) {
    d2d1.SetFailed(FeatureStatus::Failed,
                   "Failed to acquire a Direct3D 11 content device",
                   "FEATURE_FAILURE_D2D_DEVICE"_ns);
    return;
  }

  if (!dm->TextureSharingWorks()) {
    d2d1.SetFailed(FeatureStatus::Failed,
                   "Direct3D11 device does not support texture sharing",
                   "FEATURE_FAILURE_D2D_TXT_SHARING"_ns);
    return;
  }

  // Using Direct2D depends on DWrite support.
  if (!DWriteEnabled() && !InitDWriteSupport()) {
    d2d1.SetFailed(FeatureStatus::Failed,
                   "Failed to initialize DirectWrite support",
                   "FEATURE_FAILURE_D2D_DWRITE"_ns);
    return;
  }

  // Verify that Direct2D device creation succeeded.
  RefPtr<ID3D11Device> contentDevice = dm->GetContentDevice();
  if (!Factory::SetDirect3D11Device(contentDevice)) {
    d2d1.SetFailed(FeatureStatus::Failed, "Failed to create a Direct2D device",
                   "FEATURE_FAILURE_D2D_CREATE_FAILED"_ns);
    return;
  }

  MOZ_ASSERT(d2d1.IsEnabled());
  d2d1_1.SetSuccessful();
}

void gfxWindowsPlatform::InitGPUProcessSupport() {
  FeatureState& gpuProc = gfxConfig::GetFeature(Feature::GPU_PROCESS);

  if (!gpuProc.IsEnabled()) {
    return;
  }

  if (!gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    // Don't use the GPU process if not using D3D11, unless software
    // compositor is allowed
    if (StaticPrefs::layers_gpu_process_allow_software_AtStartup()) {
      return;
    }
    gpuProc.Disable(FeatureStatus::Unavailable,
                    "Not using GPU Process since D3D11 is unavailable",
                    "FEATURE_FAILURE_NO_D3D11"_ns);
  }
  // If we're still enabled at this point, the user set the force-enabled pref.
}

class D3DVsyncSource final : public VsyncSource {
 public:
  D3DVsyncSource()
      : mPrevVsync(TimeStamp::Now()),
        mVsyncEnabled(false),
        mWaitVBlankMonitor(NULL) {
    mVsyncThread = new base::Thread("WindowsVsyncThread");
    MOZ_RELEASE_ASSERT(mVsyncThread->Start(),
                       "GFX: Could not start Windows vsync thread");
    SetVsyncRate();
  }

  void SetVsyncRate() {
    DWM_TIMING_INFO vblankTime;
    // Make sure to init the cbSize, otherwise GetCompositionTiming will fail
    vblankTime.cbSize = sizeof(DWM_TIMING_INFO);
    HRESULT hr = DwmGetCompositionTimingInfo(0, &vblankTime);
    if (SUCCEEDED(hr)) {
      UNSIGNED_RATIO refreshRate = vblankTime.rateRefresh;
      // We get the rate in hertz / time, but we want the rate in ms.
      float rate =
          ((float)refreshRate.uiDenominator / (float)refreshRate.uiNumerator) *
          1000;
      mVsyncRate = TimeDuration::FromMilliseconds(rate);
    } else {
      mVsyncRate = TimeDuration::FromMilliseconds(1000.0 / 60.0);
    }
  }

  virtual void Shutdown() override {
    MOZ_ASSERT(NS_IsMainThread());
    DisableVsync();
    mVsyncThread->Stop();
    delete mVsyncThread;
  }

  virtual void EnableVsync() override {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mVsyncThread->IsRunning());
    {  // scope lock
      if (mVsyncEnabled) {
        return;
      }
      mVsyncEnabled = true;
    }

    mVsyncThread->message_loop()->PostTask(NewRunnableMethod(
        "D3DVsyncSource::VBlankLoop", this, &D3DVsyncSource::VBlankLoop));
  }

  virtual void DisableVsync() override {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mVsyncThread->IsRunning());
    if (!mVsyncEnabled) {
      return;
    }
    mVsyncEnabled = false;
  }

  virtual bool IsVsyncEnabled() override {
    MOZ_ASSERT(NS_IsMainThread());
    return mVsyncEnabled;
  }

  virtual TimeDuration GetVsyncRate() override { return mVsyncRate; }

  void ScheduleSoftwareVsync(TimeStamp aVsyncTimestamp) {
    MOZ_ASSERT(IsInVsyncThread());
    NS_WARNING(
        "DwmComposition dynamically disabled, falling back to software "
        "timers");

    TimeStamp nextVsync = aVsyncTimestamp + mVsyncRate;
    TimeDuration delay = nextVsync - TimeStamp::Now();
    if (delay.ToMilliseconds() < 0) {
      delay = mozilla::TimeDuration::FromMilliseconds(0);
    }

    mVsyncThread->message_loop()->PostDelayedTask(
        NewRunnableMethod("D3DVsyncSource::VBlankLoop", this,
                          &D3DVsyncSource::VBlankLoop),
        delay.ToMilliseconds());
  }

  // Returns the timestamp for the just happened vsync
  TimeStamp GetVBlankTime() {
    TimeStamp vsync = TimeStamp::Now();
    TimeStamp now = vsync;

    DWM_TIMING_INFO vblankTime;
    // Make sure to init the cbSize, otherwise
    // GetCompositionTiming will fail
    vblankTime.cbSize = sizeof(DWM_TIMING_INFO);
    HRESULT hr = DwmGetCompositionTimingInfo(0, &vblankTime);
    if (!SUCCEEDED(hr)) {
      return vsync;
    }

    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);

    LARGE_INTEGER qpcNow;
    QueryPerformanceCounter(&qpcNow);

    const int microseconds = 1000000;
    int64_t adjust = qpcNow.QuadPart - vblankTime.qpcVBlank;
    int64_t usAdjust = (adjust * microseconds) / frequency.QuadPart;
    vsync -= TimeDuration::FromMicroseconds((double)usAdjust);

    // On Windows 10 and on, DWMGetCompositionTimingInfo, mostly
    // reports the upcoming vsync time, which is in the future.
    // It can also sometimes report a vblank time in the past.
    // Since large parts of Gecko assume TimeStamps can't be in future,
    // use the previous vsync.

    // Windows 10 and Intel HD vsync timestamps are messy and
    // all over the place once in a while. Most of the time,
    // it reports the upcoming vsync. Sometimes, that upcoming
    // vsync is in the past. Sometimes that upcoming vsync is before
    // the previously seen vsync.
    // In these error cases, normalize to Now();
    if (vsync >= now) {
      vsync = vsync - mVsyncRate;
    }

    // On Windows 7 and 8, DwmFlush wakes up AFTER qpcVBlankTime
    // from DWMGetCompositionTimingInfo. We can return the adjusted vsync.
    if (vsync >= now) {
      vsync = now;
    }

    // Our vsync time is some time very far in the past, adjust to Now.
    // 4 ms is arbitrary, so feel free to pick something else if this isn't
    // working. See the comment above.
    if ((now - vsync).ToMilliseconds() > 4.0) {
      vsync = now;
    }

    return vsync;
  }

  void VBlankLoop() {
    MOZ_ASSERT(IsInVsyncThread());
    MOZ_ASSERT(sizeof(int64_t) == sizeof(QPC_TIME));

    TimeStamp vsync = TimeStamp::Now();
    mPrevVsync = TimeStamp();
    TimeStamp flushTime = TimeStamp::Now();
    TimeDuration longVBlank = mVsyncRate * 2;

    for (;;) {
      {  // scope lock
        if (!mVsyncEnabled) return;
      }

      // Large parts of gecko assume that the refresh driver timestamp
      // must be <= Now() and cannot be in the future.
      MOZ_ASSERT(vsync <= TimeStamp::Now());
      NotifyVsync(vsync, vsync + mVsyncRate);

      HRESULT hr = E_FAIL;
      if (!StaticPrefs::gfx_vsync_force_disable_waitforvblank()) {
        UpdateVBlankOutput();
        if (mWaitVBlankOutput) {
          const TimeStamp vblank_begin_wait = TimeStamp::Now();
          {
            AUTO_PROFILER_THREAD_SLEEP;
            hr = mWaitVBlankOutput->WaitForVBlank();
          }
          if (SUCCEEDED(hr)) {
            // vblank might return instantly when running headless,
            // monitor powering off, etc.  Since we're on a dedicated
            // thread, instant-return should not happen in the normal
            // case, so catch any odd behavior with a time cutoff:
            TimeDuration vblank_wait = TimeStamp::Now() - vblank_begin_wait;
            if (vblank_wait.ToMilliseconds() < 1.0) {
              hr = E_FAIL;  // fall back on old behavior
            }
          }
        }
      } else {
        // To mitigate bug 1924932 we only want to use DwmFlush if WaitForVBlank
        // is disabled, WaitForVBlank is the standard since Vista so we should
        // probably remove this option entirely.
        hr = DwmFlush();
      }
      if (!SUCCEEDED(hr)) {
        // DWMFlush isn't working, fallback to software vsync.
        ScheduleSoftwareVsync(TimeStamp::Now());
        return;
      }

      TimeStamp now = TimeStamp::Now();
      TimeDuration flushDiff = now - flushTime;
      flushTime = now;
      if ((flushDiff > longVBlank) || mPrevVsync.IsNull()) {
        // Our vblank took longer than 2 intervals, readjust our timestamps
        vsync = GetVBlankTime();
        mPrevVsync = vsync;
      } else {
        // Instead of giving the actual vsync time, a constant interval
        // between vblanks instead of the noise generated via hardware
        // is actually what we want. Most apps just care about the diff
        // between vblanks to animate, so a clean constant interval is
        // smoother.
        vsync = mPrevVsync + mVsyncRate;
        if (vsync > now) {
          // DWMFlush woke up very early, so readjust our times again
          vsync = GetVBlankTime();
        }

        if (vsync <= mPrevVsync) {
          vsync = TimeStamp::Now();
        }

        if ((now - vsync).ToMilliseconds() > 2.0) {
          // Account for time drift here where vsync never quite catches up to
          // Now and we'd fall ever so slightly further behind Now().
          vsync = GetVBlankTime();
        }

        mPrevVsync = vsync;
      }
    }  // end for
  }
  virtual ~D3DVsyncSource() { MOZ_ASSERT(NS_IsMainThread()); }

 private:
  bool IsInVsyncThread() {
    return mVsyncThread->thread_id() == PlatformThread::CurrentId();
  }

  void UpdateVBlankOutput() {
    HMONITOR primary_monitor =
        MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
    if (primary_monitor == mWaitVBlankMonitor && mWaitVBlankOutput) {
      return;
    }

    mWaitVBlankMonitor = primary_monitor;

    RefPtr<IDXGIOutput> output = nullptr;
    if (DeviceManagerDx* dx = DeviceManagerDx::Get()) {
      if (dx->GetOutputFromMonitor(mWaitVBlankMonitor, &output)) {
        mWaitVBlankOutput = output;
        return;
      }
    }

    // failed to convert a monitor to an output so keep trying
    mWaitVBlankOutput = nullptr;
  }

  TimeStamp mPrevVsync;
  base::Thread* mVsyncThread;
  TimeDuration mVsyncRate;
  Atomic<bool> mVsyncEnabled;

  HMONITOR mWaitVBlankMonitor;
  RefPtr<IDXGIOutput> mWaitVBlankOutput;
};  // D3DVsyncSource

already_AddRefed<mozilla::gfx::VsyncSource>
gfxWindowsPlatform::CreateGlobalHardwareVsyncSource() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread(), "GFX: Not in main thread.");

  RefPtr<VsyncSource> d3dVsyncSource = new D3DVsyncSource();
  return d3dVsyncSource.forget();
}

void gfxWindowsPlatform::ImportGPUDeviceData(
    const mozilla::gfx::GPUDeviceData& aData) {
  MOZ_ASSERT(XRE_IsParentProcess());

  gfxPlatform::ImportGPUDeviceData(aData);

  gfxConfig::ImportChange(Feature::D3D11_COMPOSITING, aData.d3d11Compositing());

  DeviceManagerDx* dm = DeviceManagerDx::Get();
  if (gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    dm->ImportDeviceInfo(aData.gpuDevice().ref());
  } else {
    // There should be no devices, so this just takes away the device status.
    dm->ResetDevices();

    // Make sure we disable D2D if content processes might use it.
    FeatureState& d2d1 = gfxConfig::GetFeature(Feature::DIRECT2D);
    if (d2d1.IsEnabled()) {
      d2d1.SetFailed(FeatureStatus::Unavailable,
                     "Direct2D requires Direct3D 11 compositing",
                     "FEATURE_FAILURE_D2D_D3D11_COMP"_ns);
    }
  }

  // CanUseHardwareVideoDecoding depends on d3d11 state, so update
  // the cached value now.
  UpdateCanUseHardwareVideoDecoding();

  // For completeness (and messaging in about:support). Content recomputes this
  // on its own, and we won't use ANGLE in the UI process if we're using a GPU
  // process.
  UpdateANGLEConfig();
}

void gfxWindowsPlatform::ImportContentDeviceData(
    const mozilla::gfx::ContentDeviceData& aData) {
  MOZ_ASSERT(XRE_IsContentProcess());

  gfxPlatform::ImportContentDeviceData(aData);

  const DevicePrefs& prefs = aData.prefs();
  gfxConfig::Inherit(Feature::D3D11_COMPOSITING, prefs.d3d11Compositing());
  gfxConfig::Inherit(Feature::DIRECT2D, prefs.useD2D1());

  if (gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    DeviceManagerDx* dm = DeviceManagerDx::Get();
    dm->ImportDeviceInfo(aData.d3d11());
  }
}

void gfxWindowsPlatform::BuildContentDeviceData(ContentDeviceData* aOut) {
  // Check for device resets before giving back new graphics information.
  UpdateRenderMode();

  gfxPlatform::BuildContentDeviceData(aOut);

  const FeatureState& d3d11 = gfxConfig::GetFeature(Feature::D3D11_COMPOSITING);
  aOut->prefs().d3d11Compositing() = d3d11.GetValue();
  aOut->prefs().useD2D1() = gfxConfig::GetValue(Feature::DIRECT2D);

  if (d3d11.IsEnabled()) {
    DeviceManagerDx* dm = DeviceManagerDx::Get();
    dm->ExportDeviceInfo(&aOut->d3d11());
  }

  aOut->cmsOutputProfileData() =
      gfxPlatform::GetPlatform()->GetPlatformCMSOutputProfileData();
}

bool gfxWindowsPlatform::CheckVariationFontSupport() {
  // Variation font support is only available on Fall Creators Update or later.
  return IsWin10FallCreatorsUpdateOrLater();
}

void gfxWindowsPlatform::GetPlatformDisplayInfo(
    mozilla::widget::InfoObject& aObj) {
  HwStretchingSupport stretch;
  DeviceManagerDx::Get()->CheckHardwareStretchingSupport(stretch);

  nsPrintfCString stretchValue(
      "both=%u window-only=%u full-screen-only=%u none=%u error=%u",
      stretch.mBoth, stretch.mWindowOnly, stretch.mFullScreenOnly,
      stretch.mNone, stretch.mError);
  aObj.DefineProperty("HardwareStretching", stretchValue.get());

  ScaledResolutionSet scaled;
  GetScaledResolutions(scaled);
  if (scaled.IsEmpty()) {
    return;
  }

  aObj.DefineProperty("ScaledResolutionCount", scaled.Length());
  for (size_t i = 0; i < scaled.Length(); ++i) {
    auto& s = scaled[i];
    nsPrintfCString name("ScaledResolution%zu", i);
    nsPrintfCString value("source %dx%d, target %dx%d", s.first.width,
                          s.first.height, s.second.width, s.second.height);
    aObj.DefineProperty(name.get(), value.get());
  }
}
