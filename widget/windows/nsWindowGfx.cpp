/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * nsWindowGfx - Painting and aceleration.
 */

/**************************************************************
 **************************************************************
 **
 ** BLOCK: Includes
 **
 ** Include headers.
 **
 **************************************************************
 **************************************************************/

#include "nsWindowGfx.h"

#include <shellapi.h>
#include <windows.h>

#include "ImageRegion.h"
#include "InProcessWinCompositorWidget.h"
#include "WinUtils.h"
#include "WinWindowOcclusionTracker.h"
#include "WindowRenderer.h"
#include "gfxConfig.h"
#include "gfxContext.h"
#include "gfxDWriteFonts.h"
#include "gfxEnv.h"
#include "gfxImageSurface.h"
#include "gfxUtils.h"
#include "gfxWindowsPlatform.h"
#include "gfxWindowsSurface.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SVGImageContext.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/gfx/Tools.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/webrender/RenderThread.h"
#include "nsAppRunner.h"
#include "nsDebug.h"
#include "nsGfxCIID.h"
#include "nsIWidgetListener.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::widget;
using namespace mozilla::plugins;
extern mozilla::LazyLogModule gWindowsLog;

/**************************************************************
 **************************************************************
 **
 ** BLOCK: Variables
 **
 ** nsWindow Class static initializations and global variables.
 **
 **************************************************************
 **************************************************************/

/**************************************************************
 *
 * SECTION: nsWindow statics
 *
 **************************************************************/

struct IconMetrics {
  int32_t xMetric;
  int32_t yMetric;
  int32_t defaultSize;
};

// Corresponds 1:1 to the IconSizeType enum
static IconMetrics sIconMetrics[] = {
    {SM_CXSMICON, SM_CYSMICON, 16},  // small icon
    {SM_CXICON, SM_CYICON, 32}       // regular icon
};

/**************************************************************
 **************************************************************
 **
 ** BLOCK: nsWindow impl.
 **
 ** Paint related nsWindow methods.
 **
 **************************************************************
 **************************************************************/

// GetRegionToPaint returns the invalidated region that needs to be painted
LayoutDeviceIntRegion nsWindow::GetRegionToPaint(const PAINTSTRUCT& ps,
                                                 HDC aDC) const {
  LayoutDeviceIntRegion fullRegion(WinUtils::ToIntRect(ps.rcPaint));
  HRGN paintRgn = ::CreateRectRgn(0, 0, 0, 0);
  if (paintRgn) {
    if (GetRandomRgn(aDC, paintRgn, SYSRGN) == 1) {
      POINT pt = {0, 0};
      ::MapWindowPoints(nullptr, mWnd, &pt, 1);
      ::OffsetRgn(paintRgn, pt.x, pt.y);
      fullRegion.AndWith(WinUtils::ConvertHRGNToRegion(paintRgn));
    }
    ::DeleteObject(paintRgn);
  }
  return fullRegion;
}

void nsWindow::ForcePresent() {
  if (mResizeState != RESIZING) {
    if (CompositorBridgeChild* remoteRenderer = GetRemoteRenderer()) {
      remoteRenderer->SendForcePresent(wr::RenderReasons::WIDGET);
    }
  }
}

bool nsWindow::OnPaint() {
  struct FallbackPaintContext {
    RefPtr<gfxASurface> mTargetSurface;
    RefPtr<DrawTarget> mDt;
    gfxContext mGfxContext;
    AutoLayerManagerSetup mSetup;

    explicit FallbackPaintContext(nsWindow* aWindow,
                                  RefPtr<gfxASurface> aTargetSurface,
                                  RefPtr<DrawTarget> aDt)
        : mTargetSurface(std::move(aTargetSurface)),
          mDt(std::move(aDt)),
          mGfxContext(mDt),
          mSetup(aWindow, &mGfxContext) {}
  };

  gfx::DeviceResetReason resetReason = gfx::DeviceResetReason::OK;
  if (gfxWindowsPlatform::GetPlatform()->DidRenderingDeviceReset(
          &resetReason)) {
    gfxCriticalNote << "(nsWindow) Detected device reset: " << (int)resetReason;

    gfxWindowsPlatform::GetPlatform()->UpdateRenderMode();
    wr::RenderThread::PostHandleDeviceReset(gfx::DeviceResetDetectPlace::WIDGET,
                                            resetReason);

    gfxCriticalNote << "(nsWindow) Finished device reset.";
    return false;
  }

  PAINTSTRUCT ps;

  // Avoid starting the GPU process for the initial navigator:blank window.
  if (mIsEarlyBlankWindow) {
    // Call BeginPaint/EndPaint or Windows will keep sending us messages.
    ::BeginPaint(mWnd, &ps);
    ::EndPaint(mWnd, &ps);
    return true;
  }

  RefPtr renderer = GetWindowRenderer();
  KnowsCompositor* knowsCompositor = renderer->AsKnowsCompositor();
  WebRenderLayerManager* layerManager = renderer->AsWebRender();
  const bool isFallback =
      renderer->GetBackendType() == LayersBackend::LAYERS_NONE;
  const bool isTransparent = mTransparencyMode == TransparencyMode::Transparent;
  MOZ_ASSERT(
      isFallback || renderer->GetBackendType() == LayersBackend::LAYERS_WR,
      "Unknown layers backend");

  const bool didResize = mBounds.Size() != mLastPaintBounds.Size();

  if (didResize && knowsCompositor && layerManager) {
    // Do an early async composite so that we at least have something on the
    // screen in the right place, even if the content is out of date.
    layerManager->ScheduleComposite(wr::RenderReasons::WIDGET);
  }
  mLastPaintBounds = mBounds;

  RefPtr<nsWindow> strongThis(this);
  // BeginPaint/EndPaint must be called to make Windows think that invalid
  // area is painted. Otherwise it will continue sending the same message
  // endlessly. Note that we need to call it after WillPaintWindow, which
  // informs us of our transparent region, but also before clearing the
  // nc-area, since ::BeginPaint might send WM_NCPAINT messages[1].
  // [1]:
  // https://learn.microsoft.com/en-us/windows/win32/gdi/the-wm-paint-message
  HDC hDC = ::BeginPaint(mWnd, &ps);
  auto endPaint = MakeScopeExit([&] {
    ::EndPaint(mWnd, &ps);
    mLastPaintEndTime = TimeStamp::Now();
  });

  Maybe<FallbackPaintContext> fallback;
  if (isFallback) {
    uint32_t flags = isTransparent ? gfxWindowsSurface::FLAG_IS_TRANSPARENT : 0;
    auto targetSurface = MakeRefPtr<gfxWindowsSurface>(hDC, flags);
    RECT paintRect;
    ::GetClientRect(mWnd, &paintRect);
    RefPtr<DrawTarget> dt = gfxPlatform::CreateDrawTargetForSurface(
        targetSurface, IntSize(paintRect.right - paintRect.left,
                               paintRect.bottom - paintRect.top));
    if (!dt || !dt->IsValid()) {
      gfxWarning() << "nsWindow::OnPaint failed in CreateDrawTargetForSurface";
      return false;
    }

    fallback.emplace(this, std::move(targetSurface), std::move(dt));
  }

  LayoutDeviceIntRegion region = GetRegionToPaint(ps, hDC);
  if (!mClearedRegion.IsEmpty()) {
    // Don't consider regions that Windows has told us to repaint as clear, even
    // if we've cleared them before.
    mClearedRegion.SubOut(region);
  }

  auto ComputeAndClearTranslucentRegion = [&]() {
    if (!isTransparent) {
      return;
    }
    const LayoutDeviceIntRegion translucentRegion = GetTranslucentRegion();
    auto regionToClear = translucentRegion;
    if (!mClearedRegion.IsEmpty()) {
      regionToClear.SubOut(mClearedRegion);
    }
    mClearedRegion = std::move(translucentRegion);
    auto black = reinterpret_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    // We could use RegionToHRGN, but at least for simple regions (and
    // possibly for complex ones too?) FillRect is faster; see bug 1946365
    // comment 12.
    for (auto it = regionToClear.RectIter(); !it.Done(); it.Next()) {
      if (fallback) {
        // Make sure to use the fallback DT if needed, rather than calling
        // ::FillRect directly. Not doing so could cause flicker, as Windows
        // doesn't guarantee atomicity even between ::BeginPaint and
        // ::EndPaint, see bug 1958631.
        fallback->mDt->ClearRect(Rect(it.Get().ToUnknownRect()));
      } else {
        auto rect = WinUtils::ToWinRect(it.Get());
        ::FillRect(hDC, &rect, black);
      }
    }
    region.OrWith(regionToClear);
  };

  // This is a bit subtle: For fallback windows, we paint directly into the DC,
  // so we need to clear it before PaintWindow().
  //
  // For composited windows, we first need to paint (so that we know the
  // translucent area, which is computed from the display list), then we clear
  // it.
  //
  // We don't track the transparent region for popups (meaning we always rely on
  // the whole client area being cleared) so this works out.
  if (fallback) {
    ComputeAndClearTranslucentRegion();
  }

  if (auto* listener = GetPaintListener()) {
    listener->PaintWindow(this);
  }

  if (!fallback) {
    ComputeAndClearTranslucentRegion();
  }

  if (knowsCompositor && layerManager && !region.IsEmpty()) {
    layerManager->SendInvalidRegion(region.ToUnknownRegion());
    layerManager->ScheduleComposite(wr::RenderReasons::WIDGET);
  }

#ifdef WIDGET_DEBUG_OUTPUT
  debug_DumpPaintEvent(stdout, this, region.ToUnknownRegion(), "noname",
                       (int32_t)mWnd);
#endif  // WIDGET_DEBUG_OUTPUT

  if (!isFallback && !gfxEnv::MOZ_DISABLE_FORCE_PRESENT()) {
    NS_DispatchToMainThread(NewRunnableMethod("nsWindow::ForcePresent", this,
                                              &nsWindow::ForcePresent));
  }

  return true;
}

bool nsWindow::NeedsToTrackWindowOcclusionState() {
  if (!WinWindowOcclusionTracker::Get()) {
    return false;
  }

  if (mCompositorSession && mWindowType == WindowType::TopLevel) {
    return true;
  }

  return false;
}

void nsWindow::NotifyOcclusionState(mozilla::widget::OcclusionState aState) {
  MOZ_ASSERT(NeedsToTrackWindowOcclusionState());

  bool isFullyOccluded = aState == mozilla::widget::OcclusionState::OCCLUDED;
  // When window is minimized, it is not set as fully occluded.
  if (mFrameState->GetSizeMode() == nsSizeMode_Minimized) {
    isFullyOccluded = false;
  }
  if (isFullyOccluded && (!mHasBeenShown || nsWindow::sIsRestoringSession)) {
    // Don't mark a newly-created window as occluded until
    // it is finished being restored (including sizing) and has been shown once.
    // (bug 1968297)
    isFullyOccluded = false;
  }

  // Don't dispatch if the new occlustion state is the same as the current
  // state.
  if (mIsFullyOccluded == isFullyOccluded) {
    return;
  }

  mIsFullyOccluded = isFullyOccluded;

  MOZ_LOG(gWindowsLog, LogLevel::Info,
          ("nsWindow::NotifyOcclusionState() mIsFullyOccluded %d "
           "mFrameState->GetSizeMode() %d",
           mIsFullyOccluded, mFrameState->GetSizeMode()));

  wr::DebugFlags flags{0};
  flags._0 = gfx::gfxVars::WebRenderDebugFlags();
  bool debugEnabled = bool(flags & wr::DebugFlags::WINDOW_VISIBILITY_DBG);
  if (debugEnabled && mCompositorWidgetDelegate) {
    mCompositorWidgetDelegate->NotifyVisibilityUpdated(mIsFullyOccluded);
  }

  if (mWidgetListener) {
    mWidgetListener->OcclusionStateChanged(mIsFullyOccluded);
  }
}

void nsWindow::MaybeEnableWindowOcclusion(bool aEnable) {
  // WindowOcclusion is enabled/disabled only when compositor session exists.
  // See nsWindow::NeedsToTrackWindowOcclusionState().
  if (!mCompositorSession) {
    return;
  }

  bool enabled = gfxConfig::IsEnabled(gfx::Feature::WINDOW_OCCLUSION);

  if (aEnable) {
    // Enable window occlusion.
    if (enabled && NeedsToTrackWindowOcclusionState()) {
      WinWindowOcclusionTracker::Get()->Enable(this, mWnd);

      wr::DebugFlags flags{0};
      flags._0 = gfx::gfxVars::WebRenderDebugFlags();
      bool debugEnabled = bool(flags & wr::DebugFlags::WINDOW_VISIBILITY_DBG);
      if (debugEnabled && mCompositorWidgetDelegate) {
        mCompositorWidgetDelegate->NotifyVisibilityUpdated(mIsFullyOccluded);
      }
    }
    return;
  }

  // Disable window occlusion.
  MOZ_ASSERT(!aEnable);

  if (!NeedsToTrackWindowOcclusionState()) {
    return;
  }

  WinWindowOcclusionTracker::Get()->Disable(this, mWnd);
  NotifyOcclusionState(OcclusionState::VISIBLE);

  wr::DebugFlags flags{0};
  flags._0 = gfx::gfxVars::WebRenderDebugFlags();
  bool debugEnabled = bool(flags & wr::DebugFlags::WINDOW_VISIBILITY_DBG);
  if (debugEnabled && mCompositorWidgetDelegate) {
    mCompositorWidgetDelegate->NotifyVisibilityUpdated(mIsFullyOccluded);
  }
}

void nsWindow::CreateCompositor() {
  nsIWidget::CreateCompositor();

  MaybeEnableWindowOcclusion(/* aEnable */ true);
}

void nsWindow::DestroyCompositor() {
  MaybeEnableWindowOcclusion(/* aEnable */ false);

  nsIWidget::DestroyCompositor();
}

LayoutDeviceIntSize nsWindowGfx::GetIconMetrics(IconSizeType aSizeType) {
  int32_t width = ::GetSystemMetrics(sIconMetrics[aSizeType].xMetric);
  int32_t height = ::GetSystemMetrics(sIconMetrics[aSizeType].yMetric);

  if (width == 0 || height == 0) {
    width = height = sIconMetrics[aSizeType].defaultSize;
  }

  return LayoutDeviceIntSize(width, height);
}

nsresult nsWindowGfx::CreateIcon(imgIContainer* aContainer,
                                 nsISVGPaintContext* aSVGPaintContext,
                                 bool aIsCursor, LayoutDeviceIntPoint aHotspot,
                                 LayoutDeviceIntSize aScaledSize,
                                 HICON* aIcon) {
  MOZ_ASSERT(aHotspot.x >= 0 && aHotspot.y >= 0);
  MOZ_ASSERT((aScaledSize.width > 0 && aScaledSize.height > 0) ||
             (aScaledSize.width == 0 && aScaledSize.height == 0));

  // Get the image data
  IntSize iconSize(aScaledSize.width, aScaledSize.height);

  RefPtr<DataSourceSurface> dataSurface;
  bool mappedOK;
  DataSourceSurface::MappedSurface map;

  if (aContainer->GetType() == imgIContainer::TYPE_VECTOR) {
    if (iconSize == IntSize(0, 0)) {  // use frame's intrinsic size
      int32_t width, height;
      nsresult rv = aContainer->GetWidth(&width);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = aContainer->GetHeight(&height);
      NS_ENSURE_SUCCESS(rv, rv);

      NS_ENSURE_TRUE(width > 0 && height > 0, NS_ERROR_FAILURE);

      iconSize = IntSize(width, height);
    }

    RefPtr<DrawTarget> drawTarget =
        gfxPlatform::GetPlatform()->CreateOffscreenContentDrawTarget(
            iconSize, SurfaceFormat::B8G8R8A8);
    if (!drawTarget || !drawTarget->IsValid()) {
      NS_ERROR("Failed to create valid DrawTarget");
      return NS_ERROR_FAILURE;
    }

    gfxContext context(drawTarget);

    SVGImageContext svgContext;
    svgContext.SetViewportSize(
        Some(CSSIntSize(iconSize.width, iconSize.height)));
    svgContext.SetColorScheme(Some(LookAndFeel::SystemColorScheme()));
    SVGImageContext::MaybeStoreContextPaint(svgContext, aSVGPaintContext,
                                            aContainer);

    mozilla::image::ImgDrawResult res = aContainer->Draw(
        &context, iconSize, image::ImageRegion::Create(iconSize),
        imgIContainer::FRAME_CURRENT, SamplingFilter::LINEAR, svgContext,
        imgIContainer::FLAG_SYNC_DECODE | imgIContainer::FLAG_ASYNC_NOTIFY,
        1.0);

    if (res != mozilla::image::ImgDrawResult::SUCCESS) {
      return NS_ERROR_FAILURE;
    }

    RefPtr<SourceSurface> surface = drawTarget->Snapshot();
    NS_ENSURE_TRUE(surface, NS_ERROR_NOT_AVAILABLE);

    dataSurface = surface->GetDataSurface();
    NS_ENSURE_TRUE(dataSurface, NS_ERROR_FAILURE);
    mappedOK = dataSurface->Map(DataSourceSurface::MapType::READ, &map);
  } else {
    RefPtr<SourceSurface> surface = aContainer->GetFrame(
        imgIContainer::FRAME_CURRENT,
        imgIContainer::FLAG_SYNC_DECODE | imgIContainer::FLAG_ASYNC_NOTIFY);
    NS_ENSURE_TRUE(surface, NS_ERROR_NOT_AVAILABLE);

    IntSize frameSize = surface->GetSize();
    if (frameSize.IsEmpty()) {
      return NS_ERROR_FAILURE;
    }

    if (iconSize == IntSize(0, 0)) {  // use frame's intrinsic size
      iconSize = frameSize;
    }

    if (iconSize != frameSize) {
      // Scale the surface
      dataSurface =
          Factory::CreateDataSourceSurface(iconSize, SurfaceFormat::B8G8R8A8);
      NS_ENSURE_TRUE(dataSurface, NS_ERROR_FAILURE);
      mappedOK = dataSurface->Map(DataSourceSurface::MapType::READ_WRITE, &map);
      NS_ENSURE_TRUE(mappedOK, NS_ERROR_FAILURE);

      RefPtr<DrawTarget> dt = Factory::CreateDrawTargetForData(
          BackendType::SKIA, map.mData, dataSurface->GetSize(), map.mStride,
          SurfaceFormat::B8G8R8A8);
      if (!dt) {
        gfxWarning()
            << "nsWindowGfx::CreatesIcon failed in CreateDrawTargetForData";
        return NS_ERROR_OUT_OF_MEMORY;
      }
      dt->DrawSurface(surface, Rect(0, 0, iconSize.width, iconSize.height),
                      Rect(0, 0, frameSize.width, frameSize.height),
                      DrawSurfaceOptions(),
                      DrawOptions(1.0f, CompositionOp::OP_SOURCE));
    } else if (surface->GetFormat() != SurfaceFormat::B8G8R8A8) {
      // Convert format to SurfaceFormat::B8G8R8A8
      dataSurface = gfxUtils::CopySurfaceToDataSourceSurfaceWithFormat(
          surface, SurfaceFormat::B8G8R8A8);
      NS_ENSURE_TRUE(dataSurface, NS_ERROR_FAILURE);
      mappedOK = dataSurface->Map(DataSourceSurface::MapType::READ, &map);
    } else {
      dataSurface = surface->GetDataSurface();
      NS_ENSURE_TRUE(dataSurface, NS_ERROR_FAILURE);
      mappedOK = dataSurface->Map(DataSourceSurface::MapType::READ, &map);
    }
  }
  NS_ENSURE_TRUE(dataSurface && mappedOK, NS_ERROR_FAILURE);
  MOZ_ASSERT(dataSurface->GetFormat() == SurfaceFormat::B8G8R8A8);

  uint8_t* data = nullptr;
  UniquePtr<uint8_t[]> autoDeleteArray;
  if (map.mStride == BytesPerPixel(dataSurface->GetFormat()) * iconSize.width) {
    // Mapped data is already packed
    data = map.mData;
  } else {
    // We can't use map.mData since the pixels are not packed (as required by
    // CreateDIBitmap, which is called under the DataToBitmap call below).
    //
    // We must unmap before calling SurfaceToPackedBGRA because it needs access
    // to the pixel data.
    dataSurface->Unmap();
    map.mData = nullptr;

    autoDeleteArray = SurfaceToPackedBGRA(dataSurface);
    data = autoDeleteArray.get();
    NS_ENSURE_TRUE(data, NS_ERROR_FAILURE);
  }

  HBITMAP bmp = DataToBitmap(data, iconSize.width, -iconSize.height, 32);
  uint8_t* a1data = Data32BitTo1Bit(data, iconSize.width, iconSize.height);
  if (map.mData) {
    dataSurface->Unmap();
  }
  if (!a1data) {
    return NS_ERROR_FAILURE;
  }

  HBITMAP mbmp = DataToBitmap(a1data, iconSize.width, -iconSize.height, 1);
  free(a1data);

  ICONINFO info = {0};
  info.fIcon = !aIsCursor;
  info.xHotspot = aHotspot.x;
  info.yHotspot = aHotspot.y;
  info.hbmMask = mbmp;
  info.hbmColor = bmp;

  HCURSOR icon = ::CreateIconIndirect(&info);
  ::DeleteObject(mbmp);
  ::DeleteObject(bmp);
  if (!icon) return NS_ERROR_FAILURE;
  *aIcon = icon;
  return NS_OK;
}

// Adjust cursor image data
uint8_t* nsWindowGfx::Data32BitTo1Bit(uint8_t* aImageData, uint32_t aWidth,
                                      uint32_t aHeight) {
  // We need (aWidth + 7) / 8 bytes plus zero-padding up to a multiple of
  // 4 bytes for each row (HBITMAP requirement). Bug 353553.
  uint32_t outBpr = ((aWidth + 31) / 8) & ~3;

  // Allocate and clear mask buffer
  uint8_t* outData = (uint8_t*)calloc(outBpr, aHeight);
  if (!outData) return nullptr;

  int32_t* imageRow = (int32_t*)aImageData;
  for (uint32_t curRow = 0; curRow < aHeight; curRow++) {
    uint8_t* outRow = outData + curRow * outBpr;
    uint8_t mask = 0x80;
    for (uint32_t curCol = 0; curCol < aWidth; curCol++) {
      // Use sign bit to test for transparency, as alpha byte is highest byte
      if (*imageRow++ < 0) *outRow |= mask;

      mask >>= 1;
      if (!mask) {
        outRow++;
        mask = 0x80;
      }
    }
  }

  return outData;
}

/**
 * Convert the given image data to a HBITMAP. If the requested depth is
 * 32 bit, a bitmap with an alpha channel will be returned.
 *
 * @param aImageData The image data to convert. Must use the format accepted
 *                   by CreateDIBitmap.
 * @param aWidth     With of the bitmap, in pixels.
 * @param aHeight    Height of the image, in pixels.
 * @param aDepth     Image depth, in bits. Should be one of 1, 24 and 32.
 *
 * @return The HBITMAP representing the image. Caller should call
 *         DeleteObject when done with the bitmap.
 *         On failure, nullptr will be returned.
 */
HBITMAP nsWindowGfx::DataToBitmap(uint8_t* aImageData, uint32_t aWidth,
                                  uint32_t aHeight, uint32_t aDepth) {
  HDC dc = ::GetDC(nullptr);

  if (aDepth == 32) {
    // Alpha channel. We need the new header.
    BITMAPV4HEADER head = {0};
    head.bV4Size = sizeof(head);
    head.bV4Width = aWidth;
    head.bV4Height = aHeight;
    head.bV4Planes = 1;
    head.bV4BitCount = aDepth;
    head.bV4V4Compression = BI_BITFIELDS;
    head.bV4SizeImage = 0;  // Uncompressed
    head.bV4XPelsPerMeter = 0;
    head.bV4YPelsPerMeter = 0;
    head.bV4ClrUsed = 0;
    head.bV4ClrImportant = 0;

    head.bV4RedMask = 0x00FF0000;
    head.bV4GreenMask = 0x0000FF00;
    head.bV4BlueMask = 0x000000FF;
    head.bV4AlphaMask = 0xFF000000;

    HBITMAP bmp = ::CreateDIBitmap(
        dc, reinterpret_cast<CONST BITMAPINFOHEADER*>(&head), CBM_INIT,
        aImageData, reinterpret_cast<CONST BITMAPINFO*>(&head), DIB_RGB_COLORS);
    ::ReleaseDC(nullptr, dc);
    return bmp;
  }

  char reserved_space[sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * 2];
  BITMAPINFOHEADER& head = *(BITMAPINFOHEADER*)reserved_space;

  head.biSize = sizeof(BITMAPINFOHEADER);
  head.biWidth = aWidth;
  head.biHeight = aHeight;
  head.biPlanes = 1;
  head.biBitCount = (WORD)aDepth;
  head.biCompression = BI_RGB;
  head.biSizeImage = 0;  // Uncompressed
  head.biXPelsPerMeter = 0;
  head.biYPelsPerMeter = 0;
  head.biClrUsed = 0;
  head.biClrImportant = 0;

  BITMAPINFO& bi = *(BITMAPINFO*)reserved_space;

  if (aDepth == 1) {
    RGBQUAD black = {0, 0, 0, 0};
    RGBQUAD white = {255, 255, 255, 0};

    bi.bmiColors[0] = white;
    bi.bmiColors[1] = black;
  }

  HBITMAP bmp =
      ::CreateDIBitmap(dc, &head, CBM_INIT, aImageData, &bi, DIB_RGB_COLORS);
  ::ReleaseDC(nullptr, dc);
  return bmp;
}
