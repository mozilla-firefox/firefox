/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
  TODO:
  - Better layers update mechanism - update only in changed layes and updated
    properties.
  - Create cache of mapped layers?
  - Fix messages from SurfacePoolWayland() mPendingEntries num xxx
    mPoolSizeLimit 25 Are we leaking pending entries?
  - Implemented screenshotter
  - Presentation feedback
  - Fullscreen - handle differently
  - Attach dmabuf feedback to dmabuf surfaces to get formats for direct scanout

  Testing:
    Mochitest test speeds
*/

#include "mozilla/layers/NativeLayerWayland.h"

#include <dlfcn.h>
#include <utility>
#include <algorithm>

#include "gfxUtils.h"
#include "nsGtkUtils.h"
#include "GLContextProvider.h"
#include "GLBlitHelper.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/SurfacePoolWayland.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/webrender/RenderDMABUFTextureHost.h"
#include "mozilla/widget/WaylandSurface.h"
#include "mozilla/StaticPrefs_widget.h"
#include "ScopedGLHelpers.h"

#ifdef MOZ_LOGGING
#  undef LOG
#  undef LOGVERBOSE
#  undef LOG_VSYNC
#  include "mozilla/Logging.h"
#  include "nsTArray.h"
#  include "Units.h"
extern mozilla::LazyLogModule gWidgetCompositorLog;
extern mozilla::LazyLogModule gWidgetVsync;
#  define LOG(str, ...)                                     \
    MOZ_LOG(gWidgetCompositorLog, mozilla::LogLevel::Debug, \
            ("%s: " str, GetDebugTag().get(), ##__VA_ARGS__))
#  define LOGVERBOSE(str, ...)                                \
    MOZ_LOG(gWidgetCompositorLog, mozilla::LogLevel::Verbose, \
            ("%s: " str, GetDebugTag().get(), ##__VA_ARGS__))
#  define LOGS(str, ...)                                    \
    MOZ_LOG(gWidgetCompositorLog, mozilla::LogLevel::Debug, \
            (str, ##__VA_ARGS__))
#  define LOG_VSYNC(str, ...)                       \
    MOZ_LOG(gWidgetVsync, mozilla::LogLevel::Debug, \
            ("[%p]: " str, GetDebugTag().get(), ##__VA_ARGS__))
#else
#  define LOG(args)
#  define LOG_VSYNC(args)
#endif /* MOZ_LOGGING */

using namespace mozilla;
using namespace mozilla::widget;

namespace mozilla::layers {

using gfx::BackendType;
using gfx::DrawTarget;
using gfx::IntPoint;
using gfx::IntRect;
using gfx::IntRegion;
using gfx::IntSize;
using gfx::Matrix4x4;
using gfx::Point;
using gfx::Rect;
using gfx::SamplingFilter;
using gfx::Size;

#ifdef MOZ_LOGGING
nsAutoCString NativeLayerRootWayland::GetDebugTag() const {
  nsAutoCString tag;
  tag.AppendPrintf("W[%p]R[%p]", mLoggingWidget, this);
  return tag;
}

nsAutoCString NativeLayerWayland::GetDebugTag() const {
  nsAutoCString tag;
  tag.AppendPrintf("W[%p]R[%p]L[%p]", mRootLayer->GetLoggingWidget(),
                   mRootLayer.get(), this);
  return tag;
}
nsAutoCString NativeLayerRootSnapshotterWayland::GetDebugTag() const {
  return mRootLayer->GetDebugTag();
}
#endif

/* static */
already_AddRefed<NativeLayerRootWayland> NativeLayerRootWayland::Create(
    RefPtr<WaylandSurface> aWaylandSurface) {
  return MakeAndAddRef<NativeLayerRootWayland>(std::move(aWaylandSurface));
}

void NativeLayerRootWayland::SetDRMFormat(DRMFormat* aFormat) {
  if (aFormat) {
    aFormat->AddRef();
  }
  if (DRMFormat* oldFormat = mDRMFormat.exchange(aFormat)) {
    oldFormat->Release();
  }
}

void NativeLayerRootWayland::ConfigureScaleLocked(
    WaylandSurfaceLock& aProofOfLock) {
  LOGVERBOSE("NativeLayerRootWayland::ConfigureScaleLocked()");

  // coordinates scale setup is not changing
  static bool coordinatesScale =
      StaticPrefs::widget_wayland_coordinates_scale_enabled() &&
      WaylandDisplayGet()->GetFractionalScaleManagerV2();

  if (coordinatesScale) {
    mRootSurface->SetScaleCallbackLocked(
        aProofOfLock, WaylandSurface::ScaleCallbackType::Layers,
        [this, self = RefPtr{this}]() {
          WaylandSurfaceLock lock(mRootSurface);
          if (!mRootSurface->IsCoordinatesScaleLocked(lock)) {
            return;
          }
          LOGVERBOSE("NativeLayerRootWayland::CoordinatesScaleCallback()");
          uint32_t scale = mRootSurface->GetCoordinatesScale();
          for (RefPtr<NativeLayerWayland>& layer : mSublayers) {
            layer->SetCoordinatesScale(scale);
          }
        });
  }

  mRootSurface->SetScaleTypeLocked(aProofOfLock,
                                   coordinatesScale
                                       ? WaylandSurface::ScaleType::Coordinates
                                       : WaylandSurface::ScaleType::Fractional,
                                   /* aSetHandler */ coordinatesScale);
}

void NativeLayerRootWayland::Init() {
  LOG("NativeLayerRootWayland::Init");
  mTmpBuffer = widget::WaylandBufferSHM::Create(LayoutDeviceIntSize(1, 1));

  // Get DRM format for surfaces created by GBM.
  if (!gfx::gfxVars::UseDMABufSurfaceExport()) {
    RefPtr<DMABufFormats> formats = WaylandDisplayGet()->GetDMABufFormats();
    DRMFormat* format = nullptr;
    if (formats) {
      if (!(format = formats->GetFormat(GBM_FORMAT_ARGB8888,
                                        /* aScanoutFormat */ true))) {
        LOGVERBOSE(
            "NativeLayerRootWayland::Init() missing scanout format, use global "
            "one");
        format = formats->GetFormat(GBM_FORMAT_ARGB8888,
                                    /* aScanoutFormat */ false);
      }
    }
    if (!format) {
      LOGVERBOSE(
          "NativeLayerRootWayland::Init() fallback to format without "
          "modifiers");
      format = new DRMFormat(GBM_FORMAT_ARGB8888);
    }
    SetDRMFormat(format);
  }

  WaylandSurfaceLock lock(mRootSurface);

  // Repaint layers if we're mapped.
  mRootSurface->SetMapCallbackLocked(
      lock,
      [this, self = RefPtr{this}](WaylandSurfaceLock& aProofOfLock) -> void {
        LOG("NativeLayerRootWayland map callback, missing root commit [%d]",
            mMissingRootCommit);
        if (mMissingRootCommit) {
          CommitToScreenLocked(aProofOfLock);
        }
        ConfigureScaleLocked(aProofOfLock);
      });

  if (mRootSurface->IsMapped()) {
    ConfigureScaleLocked(lock);
  }

  // Unmap all layers if nsWindow is unmapped
  mRootSurface->SetUnmapCallbackLocked(
      lock, [this, self = RefPtr{this}]() -> void {
        LOG("NativeLayerRootWayland Unmap callback");
        WaylandSurfaceLock lock(mRootSurface);
        for (RefPtr<NativeLayerWayland>& layer : mSublayers) {
          if (layer->IsMapped()) {
            layer->Unmap();
            layer->MainThreadUnmap();
          }
        }
      });

  mRootSurface->SetGdkCommitCallbackLocked(
      lock, [this, self = RefPtr{this}]() -> void {
        LOGVERBOSE("GdkCommitCallback()");
        // Try to update on main thread if we
        // need it
        UpdateLayersOnMainThread();
      });

  // Propagate frame callback state (enabled/disabled) to all layers
  // to save resources.
  mRootSurface->SetVSyncCallbackStateHandlerLocked(
      lock, [this, self = RefPtr{this}](bool aState) -> void {
        LOG_VSYNC("VSyncCallbackStateHandler()");
        // It's run on locked surface
        mRootSurface->AssertCurrentThreadOwnsMutex();
        for (RefPtr<NativeLayerWayland>& layer : mSublayers) {
          layer->SetFrameCallbackState(aState);
        }
      });

  // Run emulated VSync if there isn't any mapped/painted layer
  mRootSurface->SetVSyncEmulateCheckLocked(
      lock, [this, self = RefPtr{this}]() -> bool {
        // It's run on locked surface
        mRootSurface->AssertCurrentThreadOwnsMutex();
        bool isVisible = false;
        for (RefPtr<NativeLayerWayland>& layer : mSublayers) {
          if ((isVisible = layer->IsVisible())) {
            break;
          }
        }
        LOG_VSYNC("Emulate VSync [%d]", !isVisible);
        return !isVisible;
      });

  // Get the best DMABuf format for root wl_surface. We use the same
  // for child surfaces as we expect them to share the same window/monitor.
  //
  // Using suboptimal format doesn't cause any functional/visual issue
  // but may lead to worse performance as Wayland compositor may need
  // to convert it for direct scanout.
  //
  // TODO: Recreate (Unmap/Map and Dispose buffers) child surfaces
  // if there's format table refresh.
  //
  // Use on nightly only as it's not implemented yet by compositors
  // to get scanout formats for non-fullscreen surfaces.
#ifdef NIGHTLY_BUILD
  if (!gfx::gfxVars::UseDMABufSurfaceExport() &&
      StaticPrefs::widget_dmabuf_feedback_enabled_AtStartup()) {
    mRootSurface->EnableDMABufFormatsLocked(lock, [this, self = RefPtr{this}](
                                                      DMABufFormats* aFormats) {
      if (DRMFormat* format = aFormats->GetFormat(GBM_FORMAT_ARGB8888,
                                                  /* aScanoutFormat */ true)) {
        LOG("NativeLayerRootWayland DMABuf format refresh: we have scanout "
            "format.");
        SetDRMFormat(format);
        return;
      }
      if (DRMFormat* format = aFormats->GetFormat(GBM_FORMAT_ARGB8888,
                                                  /* aScanoutFormat */ false)) {
        LOG("NativeLayerRootWayland DMABuf format refresh: missing scanout "
            "format, use generic one.");
        SetDRMFormat(format);
        return;
      }
      LOG("NativeLayerRootWayland DMABuf format refresh: missing DRM "
          "format!");
    });
  }
#endif
}

void NativeLayerRootWayland::Shutdown() {
  LOG("NativeLayerRootWayland::Shutdown()");
  AssertIsOnMainThread();

  UpdateLayersOnMainThread();

  {
    WaylandSurfaceLock lock(mRootSurface);
    if (mRootSurface->IsMapped()) {
      mRootSurface->RemoveAttachedBufferLocked(lock);
    }
    mRootSurface->ClearMapCallbackLocked(lock);
    mRootSurface->ClearUnmapCallbackLocked(lock);
    mRootSurface->ClearGdkCommitCallbackLocked(lock);
    mRootSurface->DisableDMABufFormatsLocked(lock);
  }

  mRootSurface = nullptr;
  mTmpBuffer = nullptr;
  mDRMFormat = nullptr;
}

NativeLayerRootWayland::NativeLayerRootWayland(
    RefPtr<WaylandSurface> aWaylandSurface)
    : mRootSurface(aWaylandSurface) {
#ifdef MOZ_LOGGING
  mLoggingWidget = mRootSurface->GetLoggingWidget();
  mRootSurface->SetLoggingWidget(this);
  LOG("NativeLayerRootWayland::NativeLayerRootWayland() nsWindow [%p] mapped "
      "%d",
      mLoggingWidget, mRootSurface->IsMapped());
#endif
  if (!WaylandSurface::IsOpaqueRegionEnabled()) {
    NS_WARNING(
        "Wayland opaque region disabled, expect poor rendering performance!");
  }
}

NativeLayerRootWayland::~NativeLayerRootWayland() {
  LOG("NativeLayerRootWayland::~NativeLayerRootWayland()");
  MOZ_DIAGNOSTIC_ASSERT(
      !mRootSurface,
      "NativeLayerRootWayland destroyed without Shutdown() call!");
  SetDRMFormat(nullptr);
}

#ifdef MOZ_LOGGING
void* NativeLayerRootWayland::GetLoggingWidget() const {
  return mLoggingWidget;
}
#endif

// Create layer for rendering to layer/surface so get blank one from
// surface pool.
already_AddRefed<NativeLayer> NativeLayerRootWayland::CreateLayer(
    const IntSize& aSize, bool aIsOpaque,
    SurfacePoolHandle* aSurfacePoolHandle) {
  LOG("NativeLayerRootWayland::CreateLayer() [%d x %d] nsWindow [%p] opaque %d",
      aSize.width, aSize.height, GetLoggingWidget(), aIsOpaque);
  return MakeAndAddRef<NativeLayerWaylandRender>(
      this, aSize, aIsOpaque, aSurfacePoolHandle->AsSurfacePoolHandleWayland());
}

already_AddRefed<NativeLayer>
NativeLayerRootWayland::CreateLayerForExternalTexture(bool aIsOpaque) {
  LOG("NativeLayerRootWayland::CreateLayerForExternalTexture() nsWindow [%p] "
      "opaque %d",
      GetLoggingWidget(), aIsOpaque);
  return MakeAndAddRef<NativeLayerWaylandExternal>(this, aIsOpaque);
}

UniquePtr<NativeLayerRootSnapshotter>
NativeLayerRootWayland::CreateSnapshotter() {
  if (!mGL) {
    MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
    return nullptr;
  }

  if (mSublayers.IsEmpty()) {
    return nullptr;
  }

  auto snapshotter = NativeLayerRootSnapshotterWayland::Create(this, mGL);
  if (!snapshotter) {
    MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
    return nullptr;
  }

  return snapshotter;
}

NativeLayerWaylandRender* NativeLayerRootWayland::GetLayerForSnapshot() {
  // For snapshotting, WebRender manages layers to either of the followings.
  // - One content layer
  // - One content layer and one debug overlay layer
  MOZ_ASSERT(mSublayers.Length() <= 2);

  auto& layer = mSublayers[0];

  auto* layerRender = layer->AsNativeLayerWaylandRender();
  if (!layerRender) {
    MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
    return nullptr;
  }

  auto* gl = layerRender->gl();
  if (!gl || gl != mGL) {
    MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
    return nullptr;
  }

  return layerRender;
}

void NativeLayerRootWayland::AppendLayer(NativeLayer* aLayer) {
  MOZ_CRASH("NativeLayerRootWayland::AppendLayer() not implemented.");
}

void NativeLayerRootWayland::RemoveLayer(NativeLayer* aLayer) {
  MOZ_CRASH("NativeLayerRootWayland::RemoveLayer() not implemented.");
}

bool NativeLayerRootWayland::IsEmptyLocked(
    const WaylandSurfaceLock& aProofOfLock) {
  return mSublayers.IsEmpty();
}

void NativeLayerRootWayland::ClearLayersLocked(
    const widget::WaylandSurfaceLock& aProofOfLock) {
  LOG("NativeLayerRootWayland::ClearLayersLocked() layers num [%d]",
      (int)mRemovedSublayers.Length());
  for (const RefPtr<NativeLayerWayland>& layer : mRemovedSublayers) {
    LOG("  Unmap removed child layer [%p]", layer.get());
    layer->Unmap();
  }
  mMainThreadUpdateSublayers.AppendElements(std::move(mRemovedSublayers));
  RequestUpdateOnMainThreadLocked(aProofOfLock);
}

void NativeLayerRootWayland::SetLayers(
    const nsTArray<RefPtr<NativeLayer>>& aLayers) {
  // Removing all layers can destroy us so hold ref
  RefPtr<NativeLayerRoot> kungfuDeathGrip = this;

  WaylandSurfaceLock lock(mRootSurface);

  // Take shortcut if all layers are removed
  if (aLayers.IsEmpty()) {
    mRemovedSublayers.AppendElements(std::move(mSublayers));
    ClearLayersLocked(lock);
    return;
  }

  nsTArray<RefPtr<NativeLayerWayland>> newLayers(aLayers.Length());
  for (const RefPtr<NativeLayer>& sublayer : aLayers) {
    RefPtr<NativeLayerWayland> layer = sublayer->AsNativeLayerWayland();
    layer->MarkClear();
    newLayers.AppendElement(std::move(layer));
  }

  if (newLayers == mSublayers) {
    return;
  }

  LOG("NativeLayerRootWayland::SetLayers(), old layers num %d new layers num "
      "%d",
      (int)mSublayers.Length(), (int)aLayers.Length());

  // newLayers (aLayers) is a mix of old (already used) and new layers.
  // We need to go through recent layers and remove the ones missing
  // in new layers.
  for (const RefPtr<NativeLayerWayland>& layer : mSublayers) {
    layer->MarkRemoved();
  }
  for (const RefPtr<NativeLayerWayland>& layer : newLayers) {
    layer->MarkAdded();
  }

  for (const RefPtr<NativeLayerWayland>& layer : mSublayers) {
    if (layer->IsRemoved()) {
      LOG("  Unmap removed child layer [%p]", layer.get());
      mRemovedSublayers.AppendElement(layer);
    }
  }

  // Map newly added layers only if root surface itself is mapped.
  // We lock it to make sure root surface stays mapped.
  lock.RequestForceCommit();

  if (mRootSurface->IsMapped()) {
    for (const RefPtr<NativeLayerWayland>& layer : newLayers) {
      if (layer->IsNew()) {
        LOG("  Map new child layer [%p]", layer.get());
        if (!layer->Map(lock)) {
          continue;
        }
        if (layer->IsOpaque() && WaylandSurface::IsOpaqueRegionEnabled()) {
          LOG("  adding new opaque layer [%p]", layer.get());
          mMainThreadUpdateSublayers.AppendElement(layer);
        }
      }
    }
  }

  mSublayers = std::move(newLayers);
  mRootMutatedStackingOrder = true;

  mRootAllLayersRendered = false;
  mRootSurface->SetCommitStateLocked(lock, mRootAllLayersRendered);

  // We need to process a part of map event on main thread as we use Gdk
  // code there. Ask for the processing now.
  RequestUpdateOnMainThreadLocked(lock);
}

// Update layers on main thread. Missing the main thread update is not critical
// but may lead to worse performance as we tell Gdk to skip compositing opaque
// surfaces.
void NativeLayerRootWayland::UpdateLayersOnMainThread() {
  AssertIsOnMainThread();

  // We're called after Shutdown so do nothing.
  if (!mRootSurface) {
    return;
  }

  LOG("NativeLayerRootWayland::UpdateLayersOnMainThread()");
  WaylandSurfaceLock lock(mRootSurface);
  for (const RefPtr<NativeLayerWayland>& layer : mMainThreadUpdateSublayers) {
    LOGVERBOSE("NativeLayerRootWayland::UpdateLayersOnMainThread() [%p]",
               layer.get());
    layer->UpdateOnMainThread();
  }
  mMainThreadUpdateSublayers.Clear();
  mMainThreadUpdateQueued = false;
}

void NativeLayerRootWayland::RequestUpdateOnMainThreadLocked(
    const WaylandSurfaceLock& aProofOfLock) {
  if (!mMainThreadUpdateSublayers.Length() || mMainThreadUpdateQueued) {
    return;
  }
  mMainThreadUpdateQueued = true;

  LOG("NativeLayerRootWayland::RequestUpdateOnMainThreadLocked()");
  nsCOMPtr<nsIRunnable> updateLayersRunnable = NewRunnableMethod<>(
      "layers::NativeLayerRootWayland::UpdateLayersOnMainThread", this,
      &NativeLayerRootWayland::UpdateLayersOnMainThread);
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThreadQueue(
      updateLayersRunnable.forget(), EventQueuePriority::Normal));
}

#ifdef MOZ_LOGGING
void NativeLayerRootWayland::LogStatsLocked(
    const WaylandSurfaceLock& aProofOfLock) {
  if (!MOZ_LOG_TEST(gWidgetCompositorLog, mozilla::LogLevel::Verbose)) {
    return;
  }

  int layersNum = 0;
  int layersMapped = 0;
  int layersMappedOpaque = 0;
  int layersMappedOpaqueSet = 0;
  int layersBufferAttached = 0;
  int layersVisible = 0;
  int layersRendered = 0;
  int layersRenderedLastCycle = 0;

  for (RefPtr<NativeLayerWayland>& layer : mSublayers) {
    layersNum++;
    if (layer->IsMapped()) {
      layersMapped++;
    }
    if (layer->GetWaylandSurface()->HasBufferAttached()) {
      layersBufferAttached++;
    }
    if (layer->IsMapped() && layer->IsOpaque()) {
      layersMappedOpaque++;
      if (layer->GetWaylandSurface()->IsOpaqueSurfaceHandlerSet()) {
        layersMappedOpaqueSet++;
      }
    }
    if (layer->State()->mIsVisible) {
      layersVisible++;
    }
    if (layer->State()->mIsRendered) {
      layersRendered++;
    }
    if (layer->State()->mRenderedLastCycle) {
      layersRenderedLastCycle++;
    }
  }
  LOGVERBOSE(
      "Rendering stats: all rendered [%d] layers [%d] mapped [%d] attached "
      "[%d] visible [%d] "
      "rendered [%d] last [%d] opaque [%d] opaque set [%d] fullscreen [%d]",
      mRootAllLayersRendered, layersNum, layersMapped, layersBufferAttached,
      layersVisible, layersRendered, layersRenderedLastCycle,
      layersMappedOpaque, layersMappedOpaqueSet, mIsFullscreen);
}
#endif

bool NativeLayerRootWayland::CommitToScreen() {
  WaylandSurfaceLock lock(mRootSurface);

  // Attach empty tmp buffer to root layer (nsWindow).
  // We need to have any content to attach child layers to it.
  if (!mRootSurface->HasBufferAttached()) {
    mRootSurface->AttachLocked(lock, mTmpBuffer);
    mRootSurface->ClearOpaqueRegionLocked(lock);
  }

  if (!mRootSurface->IsMapped()) {
    LOG("NativeLayerRootWayland::CommitToScreen() root surface is not mapped");
    mMissingRootCommit = true;
    return false;
  }

  return CommitToScreenLocked(lock);
}

bool NativeLayerRootWayland::CommitToScreenLocked(WaylandSurfaceLock& aLock) {
  mFrameInProcess = false;

  LOG("NativeLayerRootWayland::CommitToScreen()");

  // Try to map all missing surfaces
  for (RefPtr<NativeLayerWayland>& layer : mSublayers) {
    if (!layer->IsMapped()) {
      if (!layer->Map(aLock)) {
        LOGVERBOSE(
            "NativeLayerRootWayland::CommitToScreen() failed to map layer [%p]",
            layer.get());
        continue;
      }
      if (layer->IsOpaque() && WaylandSurface::IsOpaqueRegionEnabled()) {
        mMainThreadUpdateSublayers.AppendElement(layer);
      }
      mRootMutatedStackingOrder = true;
    }
  }

  if (mRootMutatedStackingOrder) {
    RequestUpdateOnMainThreadLocked(aLock);
  }

  const double scale = mRootSurface->GetScale();
  mRootAllLayersRendered = true;
  for (RefPtr<NativeLayerWayland>& layer : mSublayers) {
    layer->RenderLayer(scale);
    if (layer->State()->mMutatedStackingOrder) {
      mRootMutatedStackingOrder = true;
    }
    if (layer->State()->mIsVisible && !layer->State()->mIsRendered) {
      LOG("NativeLayerRootWayland::CommitToScreen() layer [%p] is not rendered",
          layer.get());
      mRootAllLayersRendered = false;
    }
  }

  if (mRootMutatedStackingOrder) {
    LOGVERBOSE(
        "NativeLayerRootWayland::CommitToScreen(): changed stacking order");
    NativeLayerWayland* previousWaylandSurface = nullptr;
    for (RefPtr<NativeLayerWayland>& layer : mSublayers) {
      if (layer->State()->mIsVisible) {
        MOZ_DIAGNOSTIC_ASSERT(layer->IsMapped());
        if (previousWaylandSurface) {
          layer->PlaceAbove(previousWaylandSurface);
        }
        previousWaylandSurface = layer;
      }
      layer->State()->mMutatedStackingOrder = false;
    }
    mRootMutatedStackingOrder = false;
  }

  LOGVERBOSE("NativeLayerRootWayland::CommitToScreen(): %s root commit",
             mRootAllLayersRendered ? "enabled" : "disabled");
  mRootSurface->SetCommitStateLocked(aLock, mRootAllLayersRendered);

#ifdef MOZ_LOGGING
  LogStatsLocked(aLock);
#endif

  // Commit all layers changes now so we can unmap removed layers without
  // flickering.
  aLock.Commit();

  if (mRootAllLayersRendered && !mRemovedSublayers.IsEmpty()) {
    ClearLayersLocked(aLock);
  }

  mMissingRootCommit = false;
  return true;
}

// Ready-to-paint signal (VSync) from child surfaces. Route it to
// root WaylandSurface (owned by nsWindow) where it's used to fire VSync.
void NativeLayerRootWayland::VSyncCallbackHandler(uint32_t aTime,
                                                  bool aEmulated) {
  MOZ_DIAGNOSTIC_ASSERT(!aEmulated,
                        "VSyncCallbackHandler() is supposed to be HW only");
  {
    // Child layer wl_subsurface already requested next frame callback
    // and we need to commit to root surface too as we're in
    // wl_subsurface synced mode.
    WaylandSurfaceLock lock(mRootSurface);
  }

  if (aTime <= mLastFrameCallbackTime) {
    LOG_VSYNC(
        "NativeLayerRootWayland::VSyncCallbackHandler() ignoring redundant "
        "callback %d",
        aTime);
    return;
  }
  mLastFrameCallbackTime = aTime;

  LOG_VSYNC(
      "NativeLayerRootWayland::VSyncCallbackHandler() time %d emulated [%d]",
      aTime, aEmulated);
  mRootSurface->VSyncCallbackHandler(nullptr, aTime,
                                     /* aEmulated */ aEmulated,
                                     /* aRoutedFromChildSurface */ true);
}

// We don't need to lock access to GdkWindow() as we process all Gdk/Gtk
// events on main thread only.
GdkWindow* NativeLayerRootWayland::GetGdkWindow() const {
  AssertIsOnMainThread();
  return mRootSurface->GetGdkWindow();
}

// Try to match stored wl_buffer with provided DMABufSurface or create
// a new one.
RefPtr<WaylandBuffer> NativeLayerRootWayland::BorrowExternalBuffer(
    RefPtr<DMABufSurface> aDMABufSurface) {
  LOG("NativeLayerRootWayland::BorrowExternalBuffer() WaylandSurface [%p] UID "
      "%d PID %d mExternalBuffers num %d",
      aDMABufSurface.get(), aDMABufSurface->GetUID(), aDMABufSurface->GetPID(),
      (int)mExternalBuffers.Length());

  RefPtr waylandBuffer =
      widget::WaylandBufferDMABUF::CreateExternal(aDMABufSurface);
  for (auto& b : mExternalBuffers) {
    if (b.Matches(aDMABufSurface)) {
      LOG("NativeLayerRootWayland::BorrowExternalBuffer() wl_buffer matches, "
          "recycling");
      waylandBuffer->SetExternalWLBuffer(b.GetWLBuffer());
      return waylandBuffer.forget();
    }
  }

  wl_buffer* wlbuffer = waylandBuffer->CreateWlBuffer();
  if (!wlbuffer) {
    return nullptr;
  }

  LOG("NativeLayerRootWayland::BorrowExternalBuffer() adding new wl_buffer");
  waylandBuffer->SetExternalWLBuffer(wlbuffer);
  mExternalBuffers.EmplaceBack(aDMABufSurface, wlbuffer);
  return waylandBuffer.forget();
}

NativeLayerWayland::NativeLayerWayland(NativeLayerRootWayland* aRootLayer,
                                       const IntSize& aSize, bool aIsOpaque)
    : mRootLayer(aRootLayer), mIsOpaque(aIsOpaque), mSize(aSize) {
  mSurface = new WaylandSurface();
#ifdef MOZ_LOGGING
  mSurface->SetLoggingWidget(this);
#endif
  mSurface->Init(mRootLayer->GetRootWaylandSurface());
  LOG("NativeLayerWayland::NativeLayerWayland() WaylandSurface [%p] size [%d, "
      "%d] opaque %d",
      mSurface.get(), mSize.width, mSize.height, aIsOpaque);

  mState.mMutatedStackingOrder = true;
  mState.mMutatedPlacement = true;
}

NativeLayerWayland::~NativeLayerWayland() {
  LOG("NativeLayerWayland::~NativeLayerWayland() IsMapped %d",
      mSurface->IsMapped());
  MOZ_RELEASE_ASSERT(!mSurface->IsMapped(), "Releasing mapped surface!");
}

bool NativeLayerWayland::IsMapped() { return mSurface->IsMapped(); }

bool NativeLayerWayland::IsVisible() {
  return mSurface->IsMapped() && mSurface->HasBufferAttached();
}

void NativeLayerWayland::SetSurfaceIsFlipped(bool aIsFlipped) {
  WaylandSurfaceLock lock(mSurface);
  if (aIsFlipped != mSurfaceIsFlipped) {
    mSurfaceIsFlipped = aIsFlipped;
    mState.mMutatedPlacement = true;
  }
}

bool NativeLayerWayland::SurfaceIsFlipped() {
  WaylandSurfaceLock lock(mSurface);
  return mSurfaceIsFlipped;
}

IntSize NativeLayerWayland::GetSize() {
  WaylandSurfaceLock lock(mSurface);
  return mSize;
}

void NativeLayerWayland::SetPosition(const IntPoint& aPosition) {
  WaylandSurfaceLock lock(mSurface);
  if (aPosition != mPosition) {
    LOG("NativeLayerWayland::SetPosition() [%d, %d]", (int)aPosition.x,
        (int)aPosition.y);
    mPosition = aPosition;
    mState.mMutatedPlacement = true;
  }
}

IntPoint NativeLayerWayland::GetPosition() {
  WaylandSurfaceLock lock(mSurface);
  return mPosition;
}

void NativeLayerWayland::PlaceAbove(NativeLayerWayland* aLowerLayer) {
  WaylandSurfaceLock lock(mSurface);
  WaylandSurfaceLock lowerSurfacelock(aLowerLayer->mSurface);

  MOZ_DIAGNOSTIC_ASSERT(IsMapped());
  MOZ_DIAGNOSTIC_ASSERT(aLowerLayer->IsMapped());
  MOZ_DIAGNOSTIC_ASSERT(this != aLowerLayer);

  mSurface->PlaceAboveLocked(lock, lowerSurfacelock);
  mState.mMutatedStackingOrder = true;
}

void NativeLayerWayland::SetCoordinatesScale(uint32_t aCoordinatesScale) {
  WaylandSurfaceLock lock(mSurface);
  if (mSurface->SetCoordinatesScaleLocked(lock, aCoordinatesScale)) {
    mState.mMutatedPlacement = true;
  }
}

void NativeLayerWayland::SetTransform(const Matrix4x4& aTransform) {
  WaylandSurfaceLock lock(mSurface);
  MOZ_DIAGNOSTIC_ASSERT(aTransform.IsRectilinear());
  if (aTransform != mTransform) {
    mTransform = aTransform;
    mState.mMutatedPlacement = true;
  }
}

void NativeLayerWayland::SetSamplingFilter(
    gfx::SamplingFilter aSamplingFilter) {
  WaylandSurfaceLock lock(mSurface);
  if (aSamplingFilter != mSamplingFilter) {
    mSamplingFilter = aSamplingFilter;
  }
}

Matrix4x4 NativeLayerWayland::GetTransform() {
  WaylandSurfaceLock lock(mSurface);
  return mTransform;
}

IntRect NativeLayerWayland::GetRect() {
  WaylandSurfaceLock lock(mSurface);
  return IntRect(mPosition, mSize);
}

// TODO: remove lock?
bool NativeLayerWayland::IsOpaque() {
  WaylandSurfaceLock lock(mSurface);
  return mIsOpaque;
}

void NativeLayerWayland::SetClipRect(const Maybe<IntRect>& aClipRect) {
  WaylandSurfaceLock lock(mSurface);
  if (aClipRect != mClipRect) {
#if MOZ_LOGGING
    if (aClipRect) {
      gfx::IntRect rect(aClipRect.value());
      LOG("NativeLayerWaylandRender::SetClipRect() [%d,%d] -> [%d x %d]",
          rect.x, rect.y, rect.width, rect.height);
    }
#endif
    mClipRect = aClipRect;
    mState.mMutatedPlacement = true;
  }
}

Maybe<IntRect> NativeLayerWayland::ClipRect() {
  WaylandSurfaceLock lock(mSurface);
  return mClipRect;
}

void NativeLayerWayland::SetRoundedClipRect(
    const Maybe<gfx::RoundedRect>& aClip) {
  WaylandSurfaceLock lock(mSurface);
  if (aClip != mRoundedClipRect) {
    // TODO(gw): Support rounded clips on wayland
    mRoundedClipRect = aClip;
  }
}

Maybe<gfx::RoundedRect> NativeLayerWayland::RoundedClipRect() {
  WaylandSurfaceLock lock(mSurface);
  return mRoundedClipRect;
}

IntRect NativeLayerWayland::CurrentSurfaceDisplayRect() {
  WaylandSurfaceLock lock(mSurface);
  return mDisplayRect;
}

void NativeLayerWayland::SetScalelocked(
    const widget::WaylandSurfaceLock& aProofOfLock, double aScale) {
  MOZ_DIAGNOSTIC_ASSERT(aScale > 0);
  if (aScale != mScale) {
    mScale = aScale;
    mState.mMutatedPlacement = true;
  }
}

void NativeLayerWayland::UpdateLayerPlacementLocked(
    const widget::WaylandSurfaceLock& aProofOfLock) {
  // It's possible that NativeLayerWayland is unmapped/waiting to unmap.
  if (!IsMapped()) {
    return;
  }

  if (!mState.mMutatedPlacement) {
    return;
  }
  mState.mMutatedPlacement = false;

  // Placement is changed to we need to repaint
  mState.mMutatedVisibility = true;

  LOGVERBOSE("NativeLayerWayland::UpdateLayerPlacementLocked()");

  MOZ_RELEASE_ASSERT(mTransform.Is2D());
  auto transform2D = mTransform.As2D();

  Rect surfaceRectClipped = Rect(0, 0, (float)mSize.width, (float)mSize.height);
  surfaceRectClipped = surfaceRectClipped.Intersect(Rect(mDisplayRect));

  LOGVERBOSE(
      " size [%d x %d] clipped (display rect) size [%f, %f] -> [%f x %f]",
      mSize.width, mSize.height, surfaceRectClipped.x, surfaceRectClipped.y,
      surfaceRectClipped.width, surfaceRectClipped.height);

  transform2D.PostTranslate((float)mPosition.x, (float)mPosition.y);
  surfaceRectClipped = transform2D.TransformBounds(surfaceRectClipped);

  if (mClipRect) {
    surfaceRectClipped = surfaceRectClipped.Intersect(Rect(mClipRect.value()));
  }

  const bool visible = !surfaceRectClipped.IsEmpty();
  if (mState.mIsVisible != visible) {
    mState.mIsVisible = visible;
    mState.mMutatedStackingOrder = true;
    if (!mState.mIsVisible) {
      LOGVERBOSE("NativeLayerWayland become hidden");
      mSurface->RemoveAttachedBufferLocked(aProofOfLock);
      return;
    }
    LOGVERBOSE("NativeLayerWayland become visible");
  }

  mSurface->SetTransformFlippedLocked(aProofOfLock, transform2D._11 < 0.0,
                                      transform2D._22 < 0.0);

  bool useCoordinatesScale = mSurface->HasCoordinatesScaleLocked(aProofOfLock);
  auto unscaledRect =
      useCoordinatesScale
          ? gfx::RoundedToInt(surfaceRectClipped)
          : gfx::RoundedToInt(surfaceRectClipped / UnknownScaleFactor(mScale));
  auto rect = DesktopIntRect::FromUnknownRect(unscaledRect);
  mSurface->MoveLocked(aProofOfLock, rect.TopLeft());
  mSurface->SetViewPortDestLocked(aProofOfLock, rect.Size());

  LOGVERBOSE("  destination [%d, %d] -> [%d x %d] coordinate scale [%f]",
             rect.x, rect.y, rect.width, rect.height,
             mSurface->GetCoordinatesScaleRounded());

  auto transform2DInversed = transform2D.Inverse();
  Rect bufferClip = transform2DInversed.TransformBounds(surfaceRectClipped);
  auto unscaledViewportRect =
      bufferClip.Intersect(Rect(0, 0, mSize.width, mSize.height));
  auto viewportRect =
      useCoordinatesScale
          ? gfx::RoundedToInt(
                unscaledViewportRect *
                UnknownScaleFactor(mSurface->GetCoordinatesScaleRounded()))
          : gfx::RoundedToInt(unscaledViewportRect);

  LOGVERBOSE("  source [%d, %d] -> [%d x %d] coordinate scale [%f]",
             viewportRect.x, viewportRect.y, viewportRect.width,
             viewportRect.height, mSurface->GetCoordinatesScaleRounded());
  mSurface->SetViewPortSourceRectLocked(
      aProofOfLock, DesktopIntRect::FromUnknownRect(viewportRect));
}

void NativeLayerWayland::RenderLayer(double aScale) {
  WaylandSurfaceLock lock(mSurface);

  LOG("NativeLayerWayland::RenderLayer()");

  SetScalelocked(lock, aScale);
  UpdateLayerPlacementLocked(lock);

  mState.mRenderedLastCycle = false;

  // Don't operate over hidden layers
  if (!mState.mIsVisible) {
    LOG("NativeLayerWayland::RenderLayer() quit, not visible");
    return;
  }

  // Return if front buffer didn't changed (or changed area is empty)
  // and there isn't any visibility change.
  if (!IsFrontBufferChanged() && !mState.mMutatedVisibility) {
    LOG("NativeLayerWayland::RenderLayer() quit "
        "IsFrontBufferChanged [%d] "
        "mState.mMutatedVisibility [%d] rendered [%d]",
        IsFrontBufferChanged(), mState.mMutatedVisibility, mState.mIsRendered);
    return;
  }

  if (!mFrontBuffer) {
    LOG("NativeLayerWayland::RenderLayer() - missing front buffer!");
    return;
  }

  mState.mIsRendered = mState.mRenderedLastCycle =
      CommitFrontBufferToScreenLocked(lock);

  mState.mMutatedFrontBuffer = false;
  mState.mMutatedVisibility = false;

  if (mState.mIsVisible) {
    MOZ_DIAGNOSTIC_ASSERT(mSurface->HasBufferAttached());
  }

  LOG("NativeLayerWayland::RenderLayer(): rendered [%d]", mState.mIsRendered);
}

bool NativeLayerWayland::Map(WaylandSurfaceLock& aParentWaylandSurfaceLock) {
  WaylandSurfaceLock surfaceLock(mSurface);

  if (mNeedsMainThreadUpdate == MainThreadUpdate::Unmap) {
    LOG("NativeLayerWayland::Map() waiting to MainThreadUpdate::Unmap");
    return false;
  }

  LOG("NativeLayerWayland::Map() parent %p", mRootLayer.get());

  MOZ_DIAGNOSTIC_ASSERT(!mSurface->IsMapped());
  MOZ_DIAGNOSTIC_ASSERT(mNeedsMainThreadUpdate != MainThreadUpdate::Map);

  if (!mSurface->MapLocked(surfaceLock, &aParentWaylandSurfaceLock,
                           DesktopIntPoint())) {
    gfxCriticalError() << "NativeLayerWayland::Map() failed!";
    return false;
  }
  mSurface->DisableUserInputLocked(surfaceLock);

  auto* parentSurface = aParentWaylandSurfaceLock.GetWaylandSurface();
  if (parentSurface->IsCoordinatesScaleLocked(aParentWaylandSurfaceLock)) {
    mSurface->SetCoordinatesScaleLocked(surfaceLock,
                                        parentSurface->GetCoordinatesScale());
  }

  // Route frame-to-paint (frame callback) from child layer to root layer
  // where it's passed to Vsync.
  //
  // aTime param is used to identify duplicate events.
  //
  mSurface->SetVSyncCallbackHandlerLocked(
      surfaceLock,
      [this, self = RefPtr{this}](wl_callback* aCallback, uint32_t aTime,
                                  bool aEmulated) -> void {
        LOG_VSYNC(
            "NativeLayerWayland::VSyncCallbackHandler() time %d emulated %d",
            aTime, aEmulated);
        MOZ_DIAGNOSTIC_ASSERT(!aEmulated);
        mRootLayer->VSyncCallbackHandler(aTime, aEmulated);
      });

  if (mIsHDR) {
    gfx::YUVColorSpace yuvColorSpace = gfx::YUVColorSpace::BT709;
    gfx::TransferFunction transferFunction = gfx::TransferFunction::BT709;
    if (auto* external = AsNativeLayerWaylandExternal()) {
      if (RefPtr surface = external->GetSurface()) {
        if (auto* surfaceYUV = surface->GetAsDMABufSurfaceYUV()) {
          yuvColorSpace = surfaceYUV->GetYUVColorSpace();
          transferFunction = surfaceYUV->GetTransferFunction();
        }
      }
    }
    mSurface->EnableColorManagementLocked(surfaceLock, yuvColorSpace,
                                          transferFunction);
  }

  if (auto* external = AsNativeLayerWaylandExternal()) {
    if (RefPtr surface = external->GetSurface()) {
      if (auto* surfaceYUV = surface->GetAsDMABufSurfaceYUV()) {
        mSurface->SetColorRepresentationLocked(
            surfaceLock, surfaceYUV->GetYUVColorSpace(),
            surfaceYUV->IsFullRange(), surfaceYUV->GetWPChromaLocation());
      }
    }
  }

  mNeedsMainThreadUpdate = MainThreadUpdate::Map;
  mState.mMutatedStackingOrder = true;
  mState.mMutatedVisibility = true;
  mState.mMutatedPlacement = true;
  mState.mIsRendered = false;
  return true;
}

void NativeLayerWayland::SetFrameCallbackState(bool aState) {
  LOG_VSYNC("NativeLayerWayland::SetFrameCallbackState() %d", aState);
  WaylandSurfaceLock lock(mSurface);
  mSurface->SetVSyncCallbackStateLocked(lock, aState);
}

void NativeLayerWayland::MainThreadMap() {
  AssertIsOnMainThread();
  MOZ_DIAGNOSTIC_ASSERT(IsOpaque());
  MOZ_DIAGNOSTIC_ASSERT(mNeedsMainThreadUpdate == MainThreadUpdate::Map);

  WaylandSurfaceLock lock(mSurface);
  if (!mSurface->IsOpaqueSurfaceHandlerSet()) {
    // Don't register commit handler, we do it for all surfaces at
    // GdkCommitCallback() handler.
    mSurface->AddOpaqueSurfaceHandlerLocked(lock, mRootLayer->GetGdkWindow(),
                                            /* aRegisterCommitHandler */ false);
    mSurface->SetOpaqueLocked(lock);
    mNeedsMainThreadUpdate = MainThreadUpdate::None;
  }
}

void NativeLayerWayland::Unmap() {
  WaylandSurfaceLock surfaceLock(mSurface);

  if (!mSurface->IsMapped()) {
    // Release buffers attached while unmapped on the render thread, as the
    // last layer reference may be dropped on the main thread (Bug 2092895).
    DiscardBackbuffersLocked(surfaceLock, /* aForce */ true);
    return;
  }

  LOG("NativeLayerWayland::Unmap()");

  mSurface->UnmapLocked(surfaceLock);
  // Clear reference to this added at NativeLayerWayland::Map() by
  // callback handler.
  mSurface->ClearVSyncCallbackHandlerLocked(surfaceLock);
  mState.mMutatedStackingOrder = true;
  mState.mMutatedVisibility = true;
  mState.mIsRendered = false;
  mState.mIsVisible = false;
  DiscardBackbuffersLocked(surfaceLock);
  mNeedsMainThreadUpdate = MainThreadUpdate::Unmap;
}

void NativeLayerWayland::MainThreadUnmap() {
  WaylandSurfaceLock lock(mSurface);

  MOZ_DIAGNOSTIC_ASSERT(mNeedsMainThreadUpdate == MainThreadUpdate::Unmap);
  AssertIsOnMainThread();

  if (mSurface->IsPendingGdkCleanup()) {
    mSurface->GdkCleanUpLocked(lock);
    // TODO: Do we need to clear opaque region?
  }
  mNeedsMainThreadUpdate = MainThreadUpdate::None;
}

void NativeLayerWayland::UpdateOnMainThread() {
  AssertIsOnMainThread();
  if (mNeedsMainThreadUpdate == MainThreadUpdate::None) {
    return;
  }
  if (mNeedsMainThreadUpdate == MainThreadUpdate::Map) {
    MainThreadMap();
  } else {
    MainThreadUnmap();
  }
}

void NativeLayerWayland::DiscardBackbuffers() {
  WaylandSurfaceLock lock(mSurface);
  DiscardBackbuffersLocked(lock);
}

void NativeLayerWayland::ForceCommit() {
  WaylandSurfaceLock lock(mSurface);
  if (mSurface->IsMapped()) {
    mSurface->CommitLocked(lock, /* force commit */ true);
  }
}

NativeLayerWaylandRender::NativeLayerWaylandRender(
    NativeLayerRootWayland* aRootLayer, const IntSize& aSize, bool aIsOpaque,
    SurfacePoolHandleWayland* aSurfacePoolHandle)
    : NativeLayerWayland(aRootLayer, aSize, aIsOpaque),
      mSurfacePoolHandle(aSurfacePoolHandle) {
  MOZ_RELEASE_ASSERT(mSurfacePoolHandle,
                     "Need a non-null surface pool handle.");
}

gl::GLContext* NativeLayerWaylandRender::gl() {
  return mSurfacePoolHandle->gl();
}

void NativeLayerWaylandRender::AttachExternalImage(
    wr::RenderTextureHost* aExternalImage) {
  MOZ_CRASH("NativeLayerWaylandRender::AttachExternalImage() not implemented.");
}

bool NativeLayerWaylandRender::IsFrontBufferChanged() {
  return mState.mMutatedFrontBuffer && !mDirtyRegion.IsEmpty();
}

RefPtr<DrawTarget> NativeLayerWaylandRender::NextSurfaceAsDrawTarget(
    const IntRect& aDisplayRect, const IntRegion& aUpdateRegion,
    BackendType aBackendType) {
  LOG("NativeLayerWaylandRender::NextSurfaceAsDrawTarget()");

  WaylandSurfaceLock lock(mSurface);

  if (!mDisplayRect.IsEqualEdges(aDisplayRect)) {
    mDisplayRect = aDisplayRect;
    mState.mMutatedPlacement = true;
  }
  mDirtyRegion = aUpdateRegion;

  MOZ_DIAGNOSTIC_ASSERT(!mInProgressBuffer);
  if (mFrontBuffer && !mFrontBuffer->IsAttached(lock)) {
    LOGVERBOSE(
        "NativeLayerWaylandRender::NextSurfaceAsDrawTarget(): use front buffer "
        "for rendering");
    // the Wayland compositor released the buffer early, we can reuse it
    mInProgressBuffer = std::move(mFrontBuffer);
  } else {
    LOGVERBOSE(
        "NativeLayerWaylandRender::NextSurfaceAsDrawTarget(): use progress "
        "buffer for rendering");
    mInProgressBuffer = mSurfacePoolHandle->ObtainBufferFromPool(
        lock, mSize, mRootLayer->GetDRMFormat());
    if (mFrontBuffer) {
      LOGVERBOSE(
          "NativeLayerWaylandRender::NextSurfaceAsDrawTarget(): read-back from "
          "front buffer");
      ReadBackFrontBuffer(lock);
      mSurfacePoolHandle->ReturnBufferToPool(lock, mFrontBuffer);
      mFrontBuffer = nullptr;
    }
  }
  MOZ_DIAGNOSTIC_ASSERT(!mFrontBuffer);

  if (!mInProgressBuffer) {
    gfxCriticalError() << "Failed to obtain buffer";
    wr::RenderThread::Get()->HandleWebRenderError(
        wr::WebRenderError::NEW_SURFACE);
    return nullptr;
  }

  MOZ_DIAGNOSTIC_ASSERT(!mInProgressBuffer->IsAttached(lock),
                        "Reusing attached buffer!");

  return mInProgressBuffer->Lock();
}

Maybe<GLuint> NativeLayerWaylandRender::NextSurfaceAsFramebuffer(
    const IntRect& aDisplayRect, const IntRegion& aUpdateRegion,
    bool aNeedsDepth) {
  LOG("NativeLayerWaylandRender::NextSurfaceAsFramebuffer()");

  WaylandSurfaceLock lock(mSurface);

  if (!mDisplayRect.IsEqualEdges(aDisplayRect)) {
    mDisplayRect = aDisplayRect;
    mState.mMutatedPlacement = true;
  }
  mDirtyRegion = IntRegion(aUpdateRegion);

  MOZ_DIAGNOSTIC_ASSERT(!mInProgressBuffer);
  if (mFrontBuffer && !mFrontBuffer->IsAttached(lock)) {
    LOGVERBOSE(
        "NativeLayerWaylandRender::NextSurfaceAsFramebuffer(): use front "
        "buffer for rendering");
    // the Wayland compositor released the buffer early, we can reuse it
    mInProgressBuffer = std::move(mFrontBuffer);
  } else {
    LOGVERBOSE(
        "NativeLayerWaylandRender::NextSurfaceAsFramebuffer(): use progress "
        "buffer for rendering");
    mInProgressBuffer = mSurfacePoolHandle->ObtainBufferFromPool(
        lock, mSize, mRootLayer->GetDRMFormat());
  }

  MOZ_DIAGNOSTIC_ASSERT(mInProgressBuffer,
                        "NativeLayerWaylandRender: Failed to obtain buffer");
  if (!mInProgressBuffer) {
    return Nothing();
  }

  MOZ_DIAGNOSTIC_ASSERT(!mInProgressBuffer->IsAttached(lock),
                        "Reusing attached buffer!");

  // get the framebuffer before handling partial damage so we don't accidently
  // create one without depth buffer
  Maybe<GLuint> fbo = mSurfacePoolHandle->GetFramebufferForBuffer(
      mInProgressBuffer, aNeedsDepth);
  MOZ_DIAGNOSTIC_ASSERT(
      fbo, "NativeLayerWaylandRender: Failed to create framebuffer!");
  if (!fbo) {
    return Nothing();
  }

  if (mFrontBuffer) {
    LOGVERBOSE(
        "NativeLayerWaylandRender::NextSurfaceAsFramebuffer(): read-back from "
        "front buffer");
    ReadBackFrontBuffer(lock);
    mSurfacePoolHandle->ReturnBufferToPool(lock, mFrontBuffer);
    mFrontBuffer = nullptr;
  }

  return fbo;
}

// Front buffer is still used by compositor so we can't paint into it.
// Read it back to progress buffer and paint next frame to progress buffer.
void NativeLayerWaylandRender::ReadBackFrontBuffer(
    const WaylandSurfaceLock& aProofOfLock) {
  IntRegion copyRegion = IntRegion(mDisplayRect);
  copyRegion.SubOut(mDirtyRegion);

  LOG("NativeLayerWaylandRender::ReadBackFrontBuffer()");

  if (!copyRegion.IsEmpty()) {
    if (mSurfacePoolHandle->gl()) {
      mSurfacePoolHandle->gl()->MakeCurrent();
      for (auto iter = copyRegion.RectIter(); !iter.Done(); iter.Next()) {
        gfx::IntRect r = iter.Get();
        Maybe<GLuint> sourceFB =
            mSurfacePoolHandle->GetFramebufferForBuffer(mFrontBuffer, false);
        MOZ_DIAGNOSTIC_ASSERT(sourceFB,
                              "NativeLayerWaylandRender: Failed to get "
                              "mFrontBuffer framebuffer!");
        if (!sourceFB) {
          return;
        }
        Maybe<GLuint> destFB = mSurfacePoolHandle->GetFramebufferForBuffer(
            mInProgressBuffer, false);
        MOZ_DIAGNOSTIC_ASSERT(destFB,
                              "NativeLayerWaylandRender: Failed to get "
                              "mInProgressBuffer framebuffer!");
        if (!destFB) {
          return;
        }
        mSurfacePoolHandle->gl()->BlitHelper()->BlitFramebufferToFramebuffer(
            sourceFB.value(), destFB.value(), r, r, LOCAL_GL_NEAREST);
      }
    } else {
      RefPtr<gfx::DataSourceSurface> dataSourceSurface =
          gfx::CreateDataSourceSurfaceFromData(
              mSize, mFrontBuffer->GetSurfaceFormat(),
              (const uint8_t*)mFrontBuffer->GetImageData(),
              mSize.width * BytesPerPixel(mFrontBuffer->GetSurfaceFormat()));
      RefPtr<DrawTarget> dt = mInProgressBuffer->Lock();

      for (auto iter = copyRegion.RectIter(); !iter.Done(); iter.Next()) {
        IntRect r = iter.Get();
        dt->CopySurface(dataSourceSurface, r, IntPoint(r.x, r.y));
      }
    }
  }
}

bool NativeLayerWaylandRender::CommitFrontBufferToScreenLocked(
    const WaylandSurfaceLock& aProofOfLock) {
  // Don't operate over hidden layers
  LOG("NativeLayerWaylandRender::CommitFrontBufferToScreenLocked()");

  if (mState.mMutatedVisibility) {
    mSurface->InvalidateLocked(aProofOfLock);
  } else {
    mSurface->InvalidateRegionLocked(aProofOfLock, mDirtyRegion);
  }
  mDirtyRegion.SetEmpty();

  auto* buffer = mFrontBuffer->AsWaylandBufferDMABUF();
  if (buffer) {
    buffer->GetSurface()->FenceWait();
  }

  mSurface->AttachLocked(aProofOfLock, mFrontBuffer);
  return true;
}

void NativeLayerWaylandRender::NotifySurfaceReady() {
  LOG("NativeLayerWaylandRender::NotifySurfaceReady()");

  WaylandSurfaceLock lock(mSurface);

  // NotifySurfaceReady() may be called even if NextSurfaceAsDrawTarget() fails.
  if (!mInProgressBuffer) {
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(!mFrontBuffer);
  mFrontBuffer = std::move(mInProgressBuffer);
  if (mSurfacePoolHandle->gl()) {
    auto* buffer = mFrontBuffer->AsWaylandBufferDMABUF();
    if (buffer) {
      buffer->GetSurface()->FenceSet();
    }
    mSurfacePoolHandle->gl()->FlushIfHeavyGLCallsSinceLastFlush();
  }

  mState.mMutatedFrontBuffer = true;
}

void NativeLayerWaylandRender::CopyFrontBufferToFrameBuffer(GLuint aFB) {
  if (!mFrontBuffer) {
    return;
  }

  WaylandSurfaceLock lock(mSurface);

  mSurfacePoolHandle->gl()->MakeCurrent();

  Maybe<GLuint> sourceFB =
      mSurfacePoolHandle->GetFramebufferForBuffer(mFrontBuffer, true);
  MOZ_DIAGNOSTIC_ASSERT(
      sourceFB,
      "NativeLayerWaylandRender: Failed to get mFrontBuffer framebuffer!");
  if (!sourceFB) {
    return;
  }

  mSurfacePoolHandle->gl()->BlitHelper()->BlitFramebufferToFramebuffer(
      sourceFB.value(), aFB, IntRect(IntPoint(), mSize),
      IntRect(IntPoint(), mSize), LOCAL_GL_NEAREST);
}

void NativeLayerWaylandRender::DiscardBackbuffersLocked(
    const WaylandSurfaceLock& aProofOfLock, bool aForce) {
  LOGVERBOSE(
      "NativeLayerWaylandRender::DiscardBackbuffersLocked() force %d progress "
      "%p front %p",
      aForce, mInProgressBuffer.get(), mFrontBuffer.get());
  if (mInProgressBuffer &&
      (!mInProgressBuffer->IsAttached(aProofOfLock) || aForce)) {
    mSurfacePoolHandle->ReturnBufferToPool(aProofOfLock, mInProgressBuffer);
    mInProgressBuffer = nullptr;
  }
  if (mFrontBuffer && (!mFrontBuffer->IsAttached(aProofOfLock) || aForce)) {
    mSurfacePoolHandle->ReturnBufferToPool(aProofOfLock, mFrontBuffer);
    mFrontBuffer = nullptr;
  }
}

NativeLayerWaylandRender::~NativeLayerWaylandRender() {
  LOG("NativeLayerWaylandRender::~NativeLayerWaylandRender()");
  WaylandSurfaceLock lock(mSurface);
  DiscardBackbuffersLocked(lock, /* aForce */ true);
}

RefPtr<DMABufSurface> NativeLayerWaylandExternal::GetSurface() {
  return mTextureHost ? mTextureHost->GetSurface() : nullptr;
}

NativeLayerWaylandExternal::NativeLayerWaylandExternal(
    NativeLayerRootWayland* aRootLayer, bool aIsOpaque)
    : NativeLayerWayland(aRootLayer, IntSize(), aIsOpaque) {}

void NativeLayerWaylandExternal::AttachExternalImage(
    wr::RenderTextureHost* aExternalImage) {
  WaylandSurfaceLock lock(mSurface);

  wr::RenderDMABUFTextureHost* texture =
      aExternalImage->AsRenderDMABUFTextureHost();
  MOZ_DIAGNOSTIC_ASSERT(texture);
  if (!texture) {
    LOG("NativeLayerWayland::AttachExternalImage() failed.");
    gfxCriticalNoteOnce << "ExternalImage is not RenderDMABUFTextureHost";
    return;
  }

  if (mSize != texture->GetSize(0)) {
    mSize = texture->GetSize(0);
    mDisplayRect = IntRect(IntPoint{}, mSize);
    mState.mMutatedPlacement = true;
  }

  mState.mMutatedFrontBuffer =
      (!mTextureHost || mTextureHost->GetSurface() != texture->GetSurface());
  if (!mState.mMutatedFrontBuffer) {
    return;
  }
  mTextureHost = texture;

  auto surface = mTextureHost->GetSurface();
  mIsHDR = surface->IsHDRSurface();

  LOG("NativeLayerWaylandExternal::AttachExternalImage() host [%p] "
      "DMABufSurface [%p] DMABuf UID %d [%d x %d] HDR %d Opaque %d recycle "
      "%d",
      mTextureHost.get(), mTextureHost->GetSurface().get(),
      mTextureHost->GetSurface()->GetUID(), mSize.width, mSize.height, mIsHDR,
      mIsOpaque, surface->CanRecycle());

  mFrontBuffer = surface->CanRecycle()
                     ? mRootLayer->BorrowExternalBuffer(surface)
                     : widget::WaylandBufferDMABUF::CreateExternal(surface);
}

void NativeLayerWaylandExternal::DiscardBackbuffersLocked(
    const WaylandSurfaceLock& aProofOfLock, bool aForce) {
  LOG("NativeLayerWaylandRender::DiscardBackbuffersLocked()");

  // Buffers attached to compositor are still tracked by WaylandSurface
  // so we can release reference here.
  mTextureHost = nullptr;
  mFrontBuffer = nullptr;
}

RefPtr<DrawTarget> NativeLayerWaylandExternal::NextSurfaceAsDrawTarget(
    const IntRect& aDisplayRect, const IntRegion& aUpdateRegion,
    BackendType aBackendType) {
  MOZ_CRASH(
      "NativeLayerWaylandExternal::NextSurfaceAsDrawTarget() not implemented!");
  return nullptr;
}

Maybe<GLuint> NativeLayerWaylandExternal::NextSurfaceAsFramebuffer(
    const IntRect& aDisplayRect, const IntRegion& aUpdateRegion,
    bool aNeedsDepth) {
  MOZ_CRASH(
      "NativeLayerWaylandExternal::NextSurfaceAsFramebuffer() "
      "not implemented!");
  return Nothing();
}

bool NativeLayerWaylandExternal::IsFrontBufferChanged() {
  return mState.mMutatedFrontBuffer;
}

bool NativeLayerWaylandExternal::CommitFrontBufferToScreenLocked(
    const WaylandSurfaceLock& aProofOfLock) {
  LOG("NativeLayerWaylandExternal::CommitFrontBufferToScreenLocked()");
  mSurface->InvalidateLocked(aProofOfLock);
  mSurface->AttachLocked(aProofOfLock, mFrontBuffer);
  return true;
}

NativeLayerWaylandExternal::~NativeLayerWaylandExternal() {
  LOG("NativeLayerWaylandExternal::~NativeLayerWaylandExternal()");
}

/* static */ UniquePtr<NativeLayerRootSnapshotterWayland>
NativeLayerRootSnapshotterWayland::Create(NativeLayerRootWayland* aRootLayer,
                                          gl::GLContext* aGL) {
  MOZ_ASSERT(aRootLayer);
  MOZ_ASSERT(aGL);

  return UniquePtr<NativeLayerRootSnapshotterWayland>(
      new NativeLayerRootSnapshotterWayland(aRootLayer, aGL));
}

NativeLayerRootSnapshotterWayland::NativeLayerRootSnapshotterWayland(
    NativeLayerRootWayland* aRootLayer, gl::GLContext* aGL)
    : mRootLayer(aRootLayer), mGL(aGL) {
  LOG("NativeLayerRootSnapshotterWayland::NativeLayerRootSnapshotterWayland()");
}

NativeLayerRootSnapshotterWayland::~NativeLayerRootSnapshotterWayland() {
  LOG("NativeLayerRootSnapshotterWayland::~NativeLayerRootSnapshotterWayland("
      ")");
}

already_AddRefed<profiler_screenshots::RenderSource>
NativeLayerRootSnapshotterWayland::GetWindowContents(
    const gfx::IntSize& aWindowSize) {
  LOG("NativeLayerRootSnapshotterWayland::GetWindowContents()");
  UpdateSnapshot(aWindowSize);
  return do_AddRef(mSnapshot);
}

void NativeLayerRootSnapshotterWayland::UpdateSnapshot(
    const gfx::IntSize& aSize) {
  LOG("NativeLayerRootSnapshotterWayland::UpdateSnapshot()");
  auto* layer = mRootLayer->GetLayerForSnapshot();
  if (!layer) {
    return;
  }

  mGL->MakeCurrent();

  if (mLayerForSnapshot != layer) {
    mSnapshot = nullptr;
    mLayerForSnapshot = layer;
  }

  if (!mSnapshot || mSnapshot->Size() != aSize) {
    mSnapshot = nullptr;
    auto fb = gl::MozFramebuffer::Create(mGL, aSize, 0, false);
    if (!fb) {
      return;
    }
    mSnapshot = MakeRefPtr<RenderSourceNLRS>(std::move(fb));
  }

  mLayerForSnapshot->CopyFrontBufferToFrameBuffer(mSnapshot->FB().mFB);
}

bool NativeLayerRootSnapshotterWayland::ReadbackPixels(
    const gfx::IntSize& aReadbackSize, gfx::SurfaceFormat aReadbackFormat,
    const Range<uint8_t>& aReadbackBuffer) {
  LOG("NativeLayerRootSnapshotterWayland::ReadbackPixels()");
  if (aReadbackFormat != gfx::SurfaceFormat::B8G8R8A8) {
    return false;
  }

  UpdateSnapshot(aReadbackSize);
  if (!mSnapshot) {
    return false;
  }

  const gl::ScopedBindFramebuffer bindFB(mGL, mSnapshot->FB().mFB);
  gl::ScopedPackState safePackState(mGL);
  mGL->fReadPixels(0.0f, 0.0f, aReadbackSize.width, aReadbackSize.height,
                   LOCAL_GL_BGRA, LOCAL_GL_UNSIGNED_BYTE, &aReadbackBuffer[0]);

  return true;
}

already_AddRefed<profiler_screenshots::DownscaleTarget>
NativeLayerRootSnapshotterWayland::CreateDownscaleTarget(
    const gfx::IntSize& aSize) {
  LOG("NativeLayerRootSnapshotterWayland::CreateDownscaleTarget()");
  auto fb = gl::MozFramebuffer::Create(mGL, aSize, 0, false);
  if (!fb) {
    return nullptr;
  }
  RefPtr dt = MakeRefPtr<DownscaleTargetNLRS>(mGL, std::move(fb));
  return dt.forget();
}

already_AddRefed<profiler_screenshots::AsyncReadbackBuffer>
NativeLayerRootSnapshotterWayland::CreateAsyncReadbackBuffer(
    const gfx::IntSize& aSize) {
  LOG("NativeLayerRootSnapshotterWayland::CreateAsyncReadbackBuffer()");
  size_t bufferByteCount = aSize.width * aSize.height * 4;
  GLuint bufferHandle = 0;
  mGL->fGenBuffers(1, &bufferHandle);

  gl::ScopedPackState scopedPackState(mGL);
  mGL->fBindBuffer(LOCAL_GL_PIXEL_PACK_BUFFER, bufferHandle);
  mGL->fPixelStorei(LOCAL_GL_PACK_ALIGNMENT, 1);
  mGL->fBufferData(LOCAL_GL_PIXEL_PACK_BUFFER, bufferByteCount, nullptr,
                   LOCAL_GL_STREAM_READ);
  return MakeAndAddRef<AsyncReadbackBufferNLRS>(mGL, aSize, bufferHandle,
                                                /*bool aYFlip*/ false);
}

}  // namespace mozilla::layers
