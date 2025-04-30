/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderCompositorANGLE.h"

#include "GLContext.h"
#include "GLContextEGL.h"
#include "GLContextProvider.h"
#include "mozilla/gfx/DeviceManagerDx.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/StackArray.h"
#include "mozilla/layers/FenceD3D11.h"
#include "mozilla/layers/TextureD3D11.h"
#include "mozilla/layers/HelpersD3D11.h"
#include "mozilla/layers/SyncObject.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/webrender/DCLayerTree.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/widget/CompositorWidget.h"
#include "mozilla/widget/WinCompositorWidget.h"
#include "mozilla/WindowsVersion.h"
#include "mozilla/glean/GfxMetrics.h"
#include "nsPrintfCString.h"
#include "FxROutputHandler.h"

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>

// Flag for PrintWindow() that is defined in Winuser.h. It is defined since
// Windows 8.1. This allows PrintWindow to capture window content that is
// rendered with DirectComposition.
#undef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002

namespace mozilla::wr {

extern LazyLogModule gRenderThreadLog;
#define LOG(...) MOZ_LOG(gRenderThreadLog, LogLevel::Debug, (__VA_ARGS__))

/* static */
UniquePtr<RenderCompositor> RenderCompositorANGLE::Create(
    const RefPtr<widget::CompositorWidget>& aWidget, nsACString& aError) {
  RefPtr<gl::GLContext> gl = RenderThread::Get()->SingletonGL(aError);
  if (!gl) {
    if (aError.IsEmpty()) {
      aError.Assign("RcANGLE(no shared GL)"_ns);
    } else {
      aError.Append("(Create)"_ns);
    }
    return nullptr;
  }

  UniquePtr<RenderCompositorANGLE> compositor =
      MakeUnique<RenderCompositorANGLE>(aWidget, std::move(gl));
  if (!compositor->Initialize(aError)) {
    return nullptr;
  }
  return compositor;
}

RenderCompositorANGLE::RenderCompositorANGLE(
    const RefPtr<widget::CompositorWidget>& aWidget,
    RefPtr<gl::GLContext>&& aGL)
    : RenderCompositor(aWidget), mGL(aGL) {
  MOZ_ASSERT(mGL);
  LOG("RenderCompositorANGLE::RenderCompositorANGLE()");
}

RenderCompositorANGLE::~RenderCompositorANGLE() {
  LOG("RenderCompositorANGLE::~RenderCompositorANGLE()");

  DestroyEGLSurface();
  MOZ_ASSERT(!mEGLSurface);
}

ID3D11Device* RenderCompositorANGLE::GetDeviceOfEGLDisplay(nsACString& aError) {
  const auto& gle = gl::GLContextEGL::Cast(mGL);
  const auto& egl = gle->mEgl;
  MOZ_ASSERT(egl);
  if (!egl ||
      !egl->mLib->IsExtensionSupported(gl::EGLLibExtension::EXT_device_query)) {
    aError.Assign("RcANGLE(no EXT_device_query support)"_ns);
    return nullptr;
  }

  // Fetch the D3D11 device.
  EGLDeviceEXT eglDevice = nullptr;
  egl->fQueryDisplayAttribEXT(LOCAL_EGL_DEVICE_EXT, (EGLAttrib*)&eglDevice);
  MOZ_ASSERT(eglDevice);
  ID3D11Device* device = nullptr;
  egl->mLib->fQueryDeviceAttribEXT(eglDevice, LOCAL_EGL_D3D11_DEVICE_ANGLE,
                                   (EGLAttrib*)&device);
  if (!device) {
    aError.Assign("RcANGLE(get D3D11Device from EGLDisplay failed)"_ns);
    return nullptr;
  }
  return device;
}

bool RenderCompositorANGLE::Initialize(nsACString& aError) {
  // TODO(aosmond): This causes us to lose WebRender because it is unable to
  // distinguish why we failed and retry once the reset is complete. This does
  // appear to happen in the wild, so we really should try to do something
  // differently here.
  if (RenderThread::Get()->IsHandlingDeviceReset()) {
    aError.Assign("RcANGLE(waiting device reset)"_ns);
    return false;
  }

  // Force enable alpha channel to make sure ANGLE use correct framebuffer
  // formart
  const auto& gle = gl::GLContextEGL::Cast(mGL);
  const auto& egl = gle->mEgl;
  if (!gl::CreateConfig(*egl, &mEGLConfig, /* bpp */ 32,
                        /* enableDepthBuffer */ false, mGL->IsGLES())) {
    aError.Assign("RcANGLE(create EGLConfig failed)"_ns);
    return false;
  }
  MOZ_ASSERT(mEGLConfig);

  mDevice = GetDeviceOfEGLDisplay(aError);
  if (!mDevice) {
    return false;
  }

  if (layers::FenceD3D11::IsSupported(mDevice)) {
    mFence = layers::FenceD3D11::Create(mDevice);
  }

  mDevice->GetImmediateContext(getter_AddRefs(mCtx));
  if (!mCtx) {
    aError.Assign("RcANGLE(get immediate context failed)"_ns);
    return false;
  }

  // Disable native compositor when fast snapshot is needed.
  // Taking snapshot of native compositor is very slow on Windows.
  if (mWidget->GetCompositorOptions().NeedFastSnaphot()) {
    mUseNativeCompositor = false;
  }

  // Create DCLayerTree when DirectComposition is used.
  if (gfx::gfxVars::UseWebRenderDCompWin()) {
    HWND compositorHwnd = GetCompositorHwnd();
    if (compositorHwnd) {
      mDCLayerTree = DCLayerTree::Create(mGL, mEGLConfig, mDevice, mCtx,
                                         compositorHwnd, aError);
      if (!mDCLayerTree) {
        return false;
      }
    } else {
      aError.Assign("RcANGLE(no compositor window)"_ns);
      return false;
    }
  }

  // Create SwapChain when compositor is not used
  if (!UseCompositor()) {
    if (!CreateSwapChain(aError)) {
      // SwapChain creation failed.
      return false;
    }
  }

  mSyncObject = layers::SyncObjectHost::CreateSyncObjectHost(mDevice);
  if (!mSyncObject->Init()) {
    // Some errors occur. Clear the mSyncObject here.
    // Then, there will be no texture synchronization.
    aError.Assign("RcANGLE(create SyncObject failed)"_ns);
    return false;
  }

  InitializeUsePartialPresent();

  return true;
}

HWND RenderCompositorANGLE::GetCompositorHwnd() {
  HWND hwnd = 0;

  if (XRE_IsGPUProcess()) {
    hwnd = mWidget->AsWindows()->GetCompositorHwnd();
  } else if (
      StaticPrefs::
          gfx_webrender_enabled_no_gpu_process_with_angle_win_AtStartup()) {
    MOZ_ASSERT(XRE_IsParentProcess());

    // When GPU process does not exist, we do not need to use compositor window.
    hwnd = mWidget->AsWindows()->GetHwnd();
  }

  return hwnd;
}

bool RenderCompositorANGLE::CreateSwapChainForHWND() {
  RefPtr<IDXGIFactory> dxgiFactory = DXGIFactory();
  RefPtr<IDXGIFactory2> dxgiFactory2;
  HRESULT hr = dxgiFactory->QueryInterface(
      (IDXGIFactory2**)getter_AddRefs(dxgiFactory2));
  if (FAILED(hr)) {
    dxgiFactory2 = nullptr;
  }

  HWND hwnd = mWidget->AsWindows()->GetHwnd();
  const bool alpha = ShouldUseAlpha();
  if (dxgiFactory2) {
    RefPtr<IDXGISwapChain1> swapChain1;
    bool useTripleBuffering = false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 0;
    desc.Height = 0;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    bool useFlipSequential = gfx::gfxVars::UseWebRenderFlipSequentialWin();
    if (useFlipSequential && !mWidget->AsWindows()->GetCompositorHwnd()) {
      useFlipSequential = false;
      gfxCriticalNoteOnce << "FLIP_SEQUENTIAL needs CompositorHwnd. Fallback";
    }

    if (useFlipSequential) {
      useTripleBuffering = gfx::gfxVars::UseWebRenderTripleBufferingWin();
      if (useTripleBuffering) {
        desc.BufferCount = 3;
      } else {
        desc.BufferCount = 2;
      }
      desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
      desc.Scaling = DXGI_SCALING_NONE;
    } else {
      desc.BufferCount = 1;
      desc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
      desc.Scaling = DXGI_SCALING_STRETCH;
    }
    desc.AlphaMode =
        alpha ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = 0;

    hr = dxgiFactory2->CreateSwapChainForHwnd(
        mDevice, hwnd, &desc, nullptr, nullptr, getter_AddRefs(swapChain1));
    if (SUCCEEDED(hr) && swapChain1) {
      DXGI_RGBA color = {1.0f, 1.0f, 1.0f, 1.0f};
      swapChain1->SetBackgroundColor(&color);
      mSwapChain = swapChain1;
      mSwapChain1 = swapChain1;
      mUseTripleBuffering = useTripleBuffering;
      mSwapChainUsingAlpha = alpha;
      return true;
    }
    if (useFlipSequential) {
      gfxCriticalNoteOnce << "FLIP_SEQUENTIAL is not supported. Fallback";
    }
  }

  if (mWidget->AsWindows()->GetCompositorHwnd()) {
    // Destroy compositor window.
    mWidget->AsWindows()->DestroyCompositorWindow();
    hwnd = mWidget->AsWindows()->GetHwnd();
  }

  DXGI_SWAP_CHAIN_DESC swapDesc{};
  swapDesc.BufferDesc.Width = 0;
  swapDesc.BufferDesc.Height = 0;
  swapDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  swapDesc.BufferDesc.RefreshRate.Numerator = 60;
  swapDesc.BufferDesc.RefreshRate.Denominator = 1;
  swapDesc.SampleDesc.Count = 1;
  swapDesc.SampleDesc.Quality = 0;
  swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapDesc.BufferCount = 1;
  swapDesc.OutputWindow = hwnd;
  swapDesc.Windowed = TRUE;
  swapDesc.Flags = 0;
  swapDesc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;

  hr = dxgiFactory->CreateSwapChain(DXGIDevice().get(), &swapDesc,
                                    getter_AddRefs(mSwapChain));
  if (FAILED(hr)) {
    return false;
  }

  RefPtr<IDXGISwapChain1> swapChain1;
  hr =
      mSwapChain->QueryInterface((IDXGISwapChain1**)getter_AddRefs(swapChain1));
  if (SUCCEEDED(hr)) {
    mSwapChain1 = std::move(swapChain1);
  } else {
    mSwapChain1 = nullptr;
  }
  mSwapChainUsingAlpha = alpha;
  return true;
}

bool RenderCompositorANGLE::CreateSwapChain(nsACString& aError) {
  MOZ_ASSERT(!UseCompositor());

  mFirstPresent = true;
  CreateSwapChainForDCompIfPossible();
  if (gfx::gfxVars::UseWebRenderDCompWin() && !mSwapChain) {
    MOZ_ASSERT(GetCompositorHwnd());
    aError.Assign("RcANGLE(create swapchain for dcomp failed)"_ns);
    return false;
  }

  if (!mSwapChain && !CreateSwapChainForHWND()) {
    aError.Assign("RcANGLE(swap chain create failed)"_ns);
    return false;
  }

  // We need this because we don't want DXGI to respond to Alt+Enter.
  HWND hwnd = mWidget->AsWindows()->GetHwnd();
  DXGIFactory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_WINDOW_CHANGES);

  if (!ResizeBufferIfNeeded()) {
    aError.Assign("RcANGLE(resize buffer failed)"_ns);
    return false;
  }
  return true;
}

void RenderCompositorANGLE::CreateSwapChainForDCompIfPossible() {
  if (!mDCLayerTree) {
    return;
  }

  HWND hwnd = GetCompositorHwnd();
  if (!hwnd) {
    // When DirectComposition or DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL is used,
    // compositor window needs to exist.
    if (gfx::gfxVars::UseWebRenderDCompWin() ||
        gfx::gfxVars::UseWebRenderFlipSequentialWin()) {
      gfxCriticalNote << "Compositor window was not created";
    }
    return;
  }

  // When compositor is enabled, CompositionSurface is used for rendering.
  // It does not support triple buffering.
  const bool useTripleBuffering =
      gfx::gfxVars::UseWebRenderTripleBufferingWin() && !UseCompositor();
  RefPtr<IDXGISwapChain1> swapChain1 =
      CreateSwapChainForDComp(useTripleBuffering);
  if (swapChain1) {
    mSwapChain = swapChain1;
    mSwapChain1 = swapChain1;
    mUseTripleBuffering = useTripleBuffering;
    mDCLayerTree->SetDefaultSwapChain(swapChain1);
  } else {
    // Clear DCLayerTree on falire
    mDCLayerTree = nullptr;
  }
}

RefPtr<IDXGIDevice> RenderCompositorANGLE::DXGIDevice() {
  RefPtr<IDXGIDevice> dxgiDevice;
  mDevice->QueryInterface((IDXGIDevice**)getter_AddRefs(dxgiDevice));
  return dxgiDevice;
}

RefPtr<IDXGIFactory> RenderCompositorANGLE::DXGIFactory() {
  RefPtr<IDXGIAdapter> adapter;
  DXGIDevice()->GetAdapter(getter_AddRefs(adapter));

  RefPtr<IDXGIFactory> dxgiFactory;
  adapter->GetParent(IID_PPV_ARGS((IDXGIFactory**)getter_AddRefs(dxgiFactory)));
  return dxgiFactory;
}

RefPtr<IDXGISwapChain1> RenderCompositorANGLE::CreateSwapChainForDComp(
    bool aUseTripleBuffering) {
  RefPtr<IDXGIDevice> dxgiDevice;
  mDevice->QueryInterface((IDXGIDevice**)getter_AddRefs(dxgiDevice));

  RefPtr<IDXGIFactory> dxgiFactory = DXGIFactory();

  RefPtr<IDXGIFactory2> dxgiFactory2;
  HRESULT hr = dxgiFactory->QueryInterface(
      (IDXGIFactory2**)getter_AddRefs(dxgiFactory2));
  if (FAILED(hr)) {
    return nullptr;
  }

  RefPtr<IDXGISwapChain1> swapChain1;
  DXGI_SWAP_CHAIN_DESC1 desc{};
  // DXGI does not like 0x0 swapchains. Swap chain creation failed when 0x0 was
  // set.
  desc.Width = 1;
  desc.Height = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  if (aUseTripleBuffering) {
    desc.BufferCount = 3;
  } else {
    desc.BufferCount = 2;
  }
  // DXGI_SCALING_NONE caused swap chain creation failure.
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  const bool alpha = ShouldUseAlpha();
  // See if we need to use transparency.
  desc.AlphaMode =
      alpha ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;
  desc.Flags = 0;

  hr = dxgiFactory2->CreateSwapChainForComposition(mDevice, &desc, nullptr,
                                                   getter_AddRefs(swapChain1));
  if (SUCCEEDED(hr) && swapChain1) {
    DXGI_RGBA color = {1.0f, 1.0f, 1.0f, 1.0f};
    swapChain1->SetBackgroundColor(&color);
    mSwapChainUsingAlpha = alpha;
    return swapChain1;
  }

  return nullptr;
}

bool RenderCompositorANGLE::ShouldUseAlpha() const {
  return mWidget->AsWindows()->TransparencyModeIs(
      widget::TransparencyMode::Transparent);
}

bool RenderCompositorANGLE::BeginFrame() {
  mWidget->AsWindows()->UpdateCompositorWndSizeIfNecessary();

  if (!UseCompositor()) {
    if (NS_WARN_IF(!mSwapChainUsingAlpha && ShouldUseAlpha())) {
      if (NS_WARN_IF(!RecreateNonNativeCompositorSwapChain())) {
        return false;
      }
      MOZ_ASSERT(mSwapChainUsingAlpha);
    }
    if (!ResizeBufferIfNeeded()) {
      return false;
    }
  }

  if (!MakeCurrent()) {
    gfxCriticalNote << "Failed to make render context current, can't draw.";
    return false;
  }

  if (RenderThread::Get()->SyncObjectNeeded() && mSyncObject) {
    if (!mSyncObject->Synchronize(/* aFallible */ true)) {
      // It's timeout or other error. Handle the device-reset here.
      RenderThread::Get()->HandleDeviceReset(
          gfx::DeviceResetDetectPlace::WR_SYNC_OBJRCT,
          gfx::DeviceResetReason::UNKNOWN);
      return false;
    }
  }
  return true;
}

RenderedFrameId RenderCompositorANGLE::EndFrame(
    const nsTArray<DeviceIntRect>& aDirtyRects) {
  RenderedFrameId frameId = GetNextRenderFrameId();
  InsertGraphicsCommandsFinishedWaitQuery(frameId);

  if (mFence) {
    mFence->IncrementAndSignal();
  }

  if (!UseCompositor()) {
    auto start = TimeStamp::Now();
    if (auto* fxrHandler = mWidget->AsWindows()->GetFxrOutputHandler()) {
      // There is a Firefox Reality handler for this swapchain. Update this
      // window's contents to the VR window.
      if (fxrHandler->TryInitialize(mSwapChain, mDevice)) {
        fxrHandler->UpdateOutput(mCtx);
      }
    }

    const UINT interval =
        mFirstPresent ||
                StaticPrefs::
                    gfx_webrender_dcomp_video_swap_chain_present_interval_0()
            ? 0
            : 1;
    const UINT flags = 0;

    const LayoutDeviceIntSize& bufferSize = mBufferSize.ref();
    if (mUsePartialPresent && mSwapChain1) {
      // Clear full render flag.
      mFullRender = false;
      // If there is no diry rect, we skip SwapChain present.
      if (!aDirtyRects.IsEmpty()) {
        int rectsCount = 0;
        StackArray<RECT, 1> rects(aDirtyRects.Length());

        for (size_t i = 0; i < aDirtyRects.Length(); ++i) {
          const DeviceIntRect& rect = aDirtyRects[i];
          // Clip rect to bufferSize
          int left = std::clamp(rect.min.x, 0, bufferSize.width);
          int top = std::clamp(rect.min.y, 0, bufferSize.height);
          int right = std::clamp(rect.max.x, 0, bufferSize.width);
          int bottom = std::clamp(rect.max.y, 0, bufferSize.height);

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

          HRESULT hr;
          hr = mSwapChain1->Present1(interval, flags, &params);
          if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) {
            gfxCriticalNote << "Present1 failed: " << gfx::hexa(hr);
            mFullRender = true;
          }
        }
      }
    } else {
      mSwapChain->Present(interval, flags);
    }
    auto end = TimeStamp::Now();
    mozilla::glean::gfx::composite_swap_time.AccumulateSingleSample(
        (end - start).ToMilliseconds() * 10.);

    if (mFirstPresent && mDCLayerTree) {
      // Wait for the GPU to finish executing its commands before
      // committing the DirectComposition tree, or else the swapchain
      // may flicker black when it's first presented.
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
    mFirstPresent = false;
  }

  if (mDisablingNativeCompositor) {
    // During disabling native compositor, we need to wait all gpu tasks
    // complete. Otherwise, rendering window could cause white flash.
    WaitForPreviousGraphicsCommandsFinishedQuery(/* aWaitAll */ true);
    mDisablingNativeCompositor = false;
  }

  if (mDCLayerTree) {
    mDCLayerTree->MaybeUpdateDebug();
    mDCLayerTree->MaybeCommit();
  }

  return frameId;
}

RefPtr<layers::Fence> RenderCompositorANGLE::GetAndResetReleaseFence() {
  RefPtr<layers::Fence> fence;
  if (mFence) {
    fence = mFence->CloneFromHandle();
  }
  return fence.forget();
}

bool RenderCompositorANGLE::WaitForGPU() {
  // Note: this waits on the query we inserted in the previous frame,
  // not the one we just inserted now. Example:
  //   Insert query #1
  //   Present #1
  //   (first frame, no wait)
  //   Insert query #2
  //   Present #2
  //   Wait for query #1.
  //   Insert query #3
  //   Present #3
  //   Wait for query #2.
  //
  // This ensures we're done reading textures before swapping buffers.
  if (!StaticPrefs::gfx_webrender_wait_gpu_finished_disabled_AtStartup()) {
    return WaitForPreviousGraphicsCommandsFinishedQuery();
  }
  return true;
}

bool RenderCompositorANGLE::ResizeBufferIfNeeded() {
  MOZ_ASSERT(mSwapChain);

  LayoutDeviceIntSize size = mWidget->GetClientSize();

  // DXGI does not like 0x0 swapchains. ResizeBuffers() failed when 0x0 was set
  // when DComp is used.
  size.width = std::max(size.width, 1);
  size.height = std::max(size.height, 1);

  if (mBufferSize.isSome() && mBufferSize.ref() == size) {
    MOZ_ASSERT(mEGLSurface);
    return true;
  }

  // Release EGLSurface of back buffer before calling ResizeBuffers().
  DestroyEGLSurface();

  mBufferSize = Some(size);

  if (!CreateEGLSurface()) {
    mBufferSize.reset();
    return false;
  }

  if (mUsePartialPresent) {
    mFullRender = true;
  }
  return true;
}

bool RenderCompositorANGLE::CreateEGLSurface() {
  MOZ_ASSERT(mBufferSize.isSome());
  MOZ_ASSERT(mEGLSurface == EGL_NO_SURFACE);

  HRESULT hr;
  RefPtr<ID3D11Texture2D> backBuf;

  if (mBufferSize.isNothing()) {
    gfxCriticalNote << "Buffer size is invalid";
    return false;
  }

  const LayoutDeviceIntSize& size = mBufferSize.ref();

  // Resize swap chain
  DXGI_SWAP_CHAIN_DESC desc;
  hr = mSwapChain->GetDesc(&desc);
  if (FAILED(hr)) {
    gfxCriticalNote << "Failed to read swap chain description: "
                    << gfx::hexa(hr) << " Size : " << size;
    return false;
  }
  hr = mSwapChain->ResizeBuffers(desc.BufferCount, size.width, size.height,
                                 DXGI_FORMAT_B8G8R8A8_UNORM, 0);
  if (FAILED(hr)) {
    gfxCriticalNote << "Failed to resize swap chain buffers: " << gfx::hexa(hr)
                    << " Size : " << size;
    return false;
  }

  hr = mSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                             (void**)getter_AddRefs(backBuf));
  if (hr == DXGI_ERROR_INVALID_CALL) {
    // This happens on some GPUs/drivers when there's a TDR.
    if (mDevice->GetDeviceRemovedReason() != S_OK) {
      gfxCriticalError() << "GetBuffer returned invalid call: " << gfx::hexa(hr)
                         << " Size : " << size;
      return false;
    }
  }

  const EGLint pbuffer_attribs[]{LOCAL_EGL_WIDTH, size.width, LOCAL_EGL_HEIGHT,
                                 size.height, LOCAL_EGL_NONE};

  const auto buffer = reinterpret_cast<EGLClientBuffer>(backBuf.get());

  const auto& gle = gl::GLContextEGL::Cast(mGL);
  const auto& egl = gle->mEgl;
  const EGLSurface surface = egl->fCreatePbufferFromClientBuffer(
      LOCAL_EGL_D3D_TEXTURE_ANGLE, buffer, mEGLConfig, pbuffer_attribs);

  if (!surface) {
    EGLint err = egl->mLib->fGetError();
    gfxCriticalError() << "Failed to create Pbuffer of back buffer error: "
                       << gfx::hexa(err) << " Size : " << size;
    return false;
  }

  mEGLSurface = surface;

  return true;
}

void RenderCompositorANGLE::DestroyEGLSurface() {
  // Release EGLSurface of back buffer before calling ResizeBuffers().
  if (mEGLSurface) {
    const auto& gle = gl::GLContextEGL::Cast(gl());
    const auto& egl = gle->mEgl;
    gle->SetEGLSurfaceOverride(EGL_NO_SURFACE);
    egl->fDestroySurface(mEGLSurface);
    mEGLSurface = nullptr;
  }
}

void RenderCompositorANGLE::Pause() {}

bool RenderCompositorANGLE::Resume() { return true; }

void RenderCompositorANGLE::Update() {
  // Update compositor window's size if it exists.
  // It needs to be called here, since OS might update compositor
  // window's size at unexpected timing.
  mWidget->AsWindows()->UpdateCompositorWndSizeIfNecessary();
}

bool RenderCompositorANGLE::MakeCurrent() {
  gl::GLContextEGL::Cast(gl())->SetEGLSurfaceOverride(mEGLSurface);
  return gl()->MakeCurrent();
}

LayoutDeviceIntSize RenderCompositorANGLE::GetBufferSize() {
  if (!UseCompositor()) {
    MOZ_ASSERT(mBufferSize.isSome());
    if (mBufferSize.isNothing()) {
      return LayoutDeviceIntSize();
    }
    return mBufferSize.ref();
  } else {
    auto size = mWidget->GetClientSize();
    // This size is used for WR DEBUG_OVERLAY. Its DCTile does not like 0.
    size.width = std::max(size.width, 1);
    size.height = std::max(size.height, 1);
    return size;
  }
}

RefPtr<ID3D11Query> RenderCompositorANGLE::GetD3D11Query() {
  RefPtr<ID3D11Query> query;

  if (mRecycledQuery) {
    query = mRecycledQuery.forget();
    return query;
  }

  CD3D11_QUERY_DESC desc(D3D11_QUERY_EVENT);
  HRESULT hr = mDevice->CreateQuery(&desc, getter_AddRefs(query));
  if (FAILED(hr) || !query) {
    gfxWarning() << "Could not create D3D11_QUERY_EVENT: " << gfx::hexa(hr);
    return nullptr;
  }
  return query;
}

void RenderCompositorANGLE::InsertGraphicsCommandsFinishedWaitQuery(
    RenderedFrameId aFrameId) {
  RefPtr<ID3D11Query> query;
  query = GetD3D11Query();
  if (!query) {
    return;
  }

  mCtx->End(query);
  mCtx->Flush();
  mWaitForPresentQueries.emplace(aFrameId, query);
}

bool RenderCompositorANGLE::WaitForPreviousGraphicsCommandsFinishedQuery(
    bool aWaitAll) {
  size_t waitLatency = mUseTripleBuffering ? 3 : 2;
  if (aWaitAll) {
    waitLatency = 1;
  }

  while (mWaitForPresentQueries.size() >= waitLatency) {
    auto queryPair = mWaitForPresentQueries.front();
    BOOL result;
    bool ret =
        layers::WaitForFrameGPUQuery(mDevice, mCtx, queryPair.second, &result);

    if (!ret) {
      mWaitForPresentQueries.pop();
      return false;
    }

    // Recycle query for later use.
    mRecycledQuery = queryPair.second;
    mLastCompletedFrameId = queryPair.first;
    mWaitForPresentQueries.pop();
  }
  return true;
}

RenderedFrameId RenderCompositorANGLE::GetLastCompletedFrameId() {
  while (!mWaitForPresentQueries.empty()) {
    auto queryPair = mWaitForPresentQueries.front();
    if (mCtx->GetData(queryPair.second, nullptr, 0,
                      D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
      break;
    }

    mRecycledQuery = queryPair.second;
    mLastCompletedFrameId = queryPair.first;
    mWaitForPresentQueries.pop();
  }

  nsPrintfCString marker("Pending frames %u",
                         (uint32_t)mWaitForPresentQueries.size());
  PROFILER_MARKER_TEXT("GetLastCompletedFrameId", GRAPHICS, {}, marker);

  return mLastCompletedFrameId;
}

RenderedFrameId RenderCompositorANGLE::UpdateFrameId() {
  RenderedFrameId frameId = GetNextRenderFrameId();
  InsertGraphicsCommandsFinishedWaitQuery(frameId);
  return frameId;
}

gfx::DeviceResetReason RenderCompositorANGLE::IsContextLost(bool aForce) {
  // glGetGraphicsResetStatus does not always work to detect timeout detection
  // and recovery (TDR). On Windows, ANGLE itself is just relying upon the same
  // API, so we should not need to check it separately.
  auto reason = mDevice->GetDeviceRemovedReason();
  return layers::DXGIErrorToDeviceResetReason(reason);
}

bool RenderCompositorANGLE::UseCompositor() const {
  return mUseNativeCompositor && mDCLayerTree &&
         gfx::gfxVars::UseWebRenderCompositor();
}

bool RenderCompositorANGLE::SupportAsyncScreenshot() {
#ifdef NIGHTLY_BUILD
  if (StaticPrefs::gfx_webrender_layer_compositor_AtStartup()) {
    return true;
  }
#endif
  return !UseCompositor() && !mDisablingNativeCompositor;
}

bool RenderCompositorANGLE::ShouldUseNativeCompositor() {
  return UseCompositor();
}

void RenderCompositorANGLE::CompositorBeginFrame() {
  mDCLayerTree->CompositorBeginFrame();
}

void RenderCompositorANGLE::CompositorEndFrame() {
  mDCLayerTree->CompositorEndFrame();
}

void RenderCompositorANGLE::Bind(wr::NativeTileId aId,
                                 wr::DeviceIntPoint* aOffset, uint32_t* aFboId,
                                 wr::DeviceIntRect aDirtyRect,
                                 wr::DeviceIntRect aValidRect) {
  mDCLayerTree->Bind(aId, aOffset, aFboId, aDirtyRect, aValidRect);
}

void RenderCompositorANGLE::Unbind() { mDCLayerTree->Unbind(); }

void RenderCompositorANGLE::BindSwapChain(wr::NativeSurfaceId aId) {
  mDCLayerTree->BindSwapChain(aId);
}
void RenderCompositorANGLE::PresentSwapChain(wr::NativeSurfaceId aId) {
  mDCLayerTree->PresentSwapChain(aId);
}

void RenderCompositorANGLE::CreateSurface(wr::NativeSurfaceId aId,
                                          wr::DeviceIntPoint aVirtualOffset,
                                          wr::DeviceIntSize aTileSize,
                                          bool aIsOpaque) {
  mDCLayerTree->CreateSurface(aId, aVirtualOffset, aTileSize, aIsOpaque);
}

void RenderCompositorANGLE::CreateSwapChainSurface(wr::NativeSurfaceId aId,
                                                   wr::DeviceIntSize aSize,
                                                   bool aIsOpaque) {
  mDCLayerTree->CreateSwapChainSurface(aId, aSize, aIsOpaque);
}

void RenderCompositorANGLE::ResizeSwapChainSurface(wr::NativeSurfaceId aId,
                                                   wr::DeviceIntSize aSize) {
  mDCLayerTree->ResizeSwapChainSurface(aId, aSize);
}

void RenderCompositorANGLE::CreateExternalSurface(wr::NativeSurfaceId aId,
                                                  bool aIsOpaque) {
  mDCLayerTree->CreateExternalSurface(aId, aIsOpaque);
}

void RenderCompositorANGLE::DestroySurface(NativeSurfaceId aId) {
  mDCLayerTree->DestroySurface(aId);
}

void RenderCompositorANGLE::CreateTile(wr::NativeSurfaceId aId, int aX,
                                       int aY) {
  mDCLayerTree->CreateTile(aId, aX, aY);
}

void RenderCompositorANGLE::DestroyTile(wr::NativeSurfaceId aId, int aX,
                                        int aY) {
  mDCLayerTree->DestroyTile(aId, aX, aY);
}

void RenderCompositorANGLE::AttachExternalImage(
    wr::NativeSurfaceId aId, wr::ExternalImageId aExternalImage) {
  mDCLayerTree->AttachExternalImage(aId, aExternalImage);
}

void RenderCompositorANGLE::AddSurface(
    wr::NativeSurfaceId aId, const wr::CompositorSurfaceTransform& aTransform,
    wr::DeviceIntRect aClipRect, wr::ImageRendering aImageRendering,
    wr::DeviceIntRect aRoundedClipRect, wr::ClipRadius aClipRadius) {
  mDCLayerTree->AddSurface(aId, aTransform, aClipRect, aImageRendering,
                           aRoundedClipRect, aClipRadius);
}

void RenderCompositorANGLE::GetCompositorCapabilities(
    CompositorCapabilities* aCaps) {
  RenderCompositor::GetCompositorCapabilities(aCaps);

  if (StaticPrefs::gfx_webrender_dcomp_use_virtual_surfaces_AtStartup()) {
    aCaps->virtual_surface_size = VIRTUAL_SURFACE_SIZE;
  } else {
    aCaps->virtual_surface_size = 0;
  }
  // DComp video overlay does not support negative scaling. See Bug 1831820
  aCaps->supports_external_compositor_surface_negative_scaling = false;
}

void RenderCompositorANGLE::GetWindowProperties(WindowProperties* aProperties) {
  aProperties->is_opaque = !ShouldUseAlpha();
}

void RenderCompositorANGLE::EnableNativeCompositor(bool aEnable) {
  // XXX Re-enable native compositor is not handled yet.
  MOZ_RELEASE_ASSERT(!mDisablingNativeCompositor);
  MOZ_RELEASE_ASSERT(!aEnable);
  LOG("RenderCompositorANGLE::EnableNativeCompositor() aEnable %d", aEnable);

  if (!UseCompositor()) {
    return;
  }

  mUseNativeCompositor = false;
  mDCLayerTree->DisableNativeCompositor();

  if (!RecreateNonNativeCompositorSwapChain()) {
    gfxCriticalNote << "Failed to re-create SwapChain";
    RenderThread::Get()->HandleWebRenderError(WebRenderError::NEW_SURFACE);
    return;
  }

  mDisablingNativeCompositor = true;
}

bool RenderCompositorANGLE::RecreateNonNativeCompositorSwapChain() {
  DestroyEGLSurface();
  mBufferSize.reset();

  if (mDCLayerTree) {
    RefPtr<IDXGISwapChain1> swapChain1 =
        CreateSwapChainForDComp(mUseTripleBuffering);
    if (!swapChain1) {
      return false;
    }
    mSwapChain = swapChain1;
    mSwapChain1 = swapChain1;
    mDCLayerTree->SetDefaultSwapChain(swapChain1);
  } else {
    if (NS_WARN_IF(!CreateSwapChainForHWND())) {
      return false;
    }
  }
  return ResizeBufferIfNeeded();
}

void RenderCompositorANGLE::InitializeUsePartialPresent() {
  // Even when mSwapChain1 is null, we could enable WR partial present, since
  // when mSwapChain1 is null, SwapChain is blit model swap chain with one
  // buffer.
  mUsePartialPresent = !UseCompositor() &&
                       !mWidget->AsWindows()->HasFxrOutputHandler() &&
                       gfx::gfxVars::WebRenderMaxPartialPresentRects() > 0;
}

bool RenderCompositorANGLE::UsePartialPresent() { return mUsePartialPresent; }

bool RenderCompositorANGLE::RequestFullRender() { return mFullRender; }

uint32_t RenderCompositorANGLE::GetMaxPartialPresentRects() {
  if (!mUsePartialPresent) {
    return 0;
  }
  return gfx::gfxVars::WebRenderMaxPartialPresentRects();
}

bool RenderCompositorANGLE::MaybeReadback(
    const gfx::IntSize& aReadbackSize, const wr::ImageFormat& aReadbackFormat,
    const Range<uint8_t>& aReadbackBuffer, bool* aNeedsYFlip) {
  MOZ_ASSERT(aReadbackFormat == wr::ImageFormat::BGRA8);

  if (!UseCompositor()) {
    return false;
  }

  auto start = TimeStamp::Now();

  HDC nulldc = ::GetDC(NULL);
  HDC dc = ::CreateCompatibleDC(nulldc);
  ::ReleaseDC(nullptr, nulldc);
  if (!dc) {
    gfxCriticalError() << "CreateCompatibleDC failed";
    return false;
  }

  BITMAPV4HEADER header;
  memset(&header, 0, sizeof(BITMAPV4HEADER));
  header.bV4Size = sizeof(BITMAPV4HEADER);
  header.bV4Width = aReadbackSize.width;
  header.bV4Height = -LONG(aReadbackSize.height);  // top-to-buttom DIB
  header.bV4Planes = 1;
  header.bV4BitCount = 32;
  header.bV4V4Compression = BI_BITFIELDS;
  header.bV4RedMask = 0x00FF0000;
  header.bV4GreenMask = 0x0000FF00;
  header.bV4BlueMask = 0x000000FF;
  header.bV4AlphaMask = 0xFF000000;

  void* readbackBits = nullptr;
  HBITMAP bitmap =
      ::CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&header),
                         DIB_RGB_COLORS, &readbackBits, nullptr, 0);
  if (!bitmap) {
    ::DeleteDC(dc);
    gfxCriticalError() << "CreateDIBSection failed";
    return false;
  }

  ::SelectObject(dc, bitmap);

  UINT flags = PW_CLIENTONLY | PW_RENDERFULLCONTENT;
  HWND hwnd = mWidget->AsWindows()->GetHwnd();

  mDCLayerTree->WaitForCommitCompletion();

  BOOL result = ::PrintWindow(hwnd, dc, flags);
  if (!result) {
    ::DeleteObject(bitmap);
    ::DeleteDC(dc);
    gfxCriticalError() << "PrintWindow failed";
    return false;
  }

  ::GdiFlush();

  memcpy(&aReadbackBuffer[0], readbackBits, aReadbackBuffer.length());

  ::DeleteObject(bitmap);
  ::DeleteDC(dc);

  uint32_t latencyMs = round((TimeStamp::Now() - start).ToMilliseconds());
  if (latencyMs > 500) {
    gfxCriticalNote << "Readback took too long: " << latencyMs << " ms";
  }

  if (aNeedsYFlip) {
    *aNeedsYFlip = false;
  }

  return true;
}

}  // namespace mozilla::wr
