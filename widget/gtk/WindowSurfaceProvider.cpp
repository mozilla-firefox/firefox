/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WindowSurfaceProvider.h"

#include "GtkCompositorWidget.h"
#include "WidgetUtilsGtk.h"
#include "gfxPlatformGtk.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/LayersTypes.h"
#include "nsWindow.h"

#ifdef MOZ_WAYLAND
#  include "WindowSurfaceCairo.h"
#  include "WindowSurfaceWaylandMultiBuffer.h"
#  include "mozilla/StaticPrefs_widget.h"
#endif
#ifdef MOZ_X11
#  include "WindowSurfaceX11Image.h"
#  include "WindowSurfaceX11SHM.h"
#  include "mozilla/X11Util.h"
#endif

#undef LOG
#ifdef MOZ_LOGGING
#  include "Units.h"
#  include "mozilla/Logging.h"
#  include "nsTArray.h"
extern mozilla::LazyLogModule gWidgetLog;
#  define LOG(args) MOZ_LOG(gWidgetLog, mozilla::LogLevel::Debug, args)
#else
#  define LOG(args)
#endif /* MOZ_LOGGING */

namespace mozilla {
namespace widget {

using namespace mozilla::layers;

WindowSurfaceProvider::WindowSurfaceProvider()
    : mWindowSurface(nullptr),
      mMutex("WindowSurfaceProvider"),
      mWindowSurfaceValid(false)
#ifdef MOZ_X11
      ,
      mXDepth(0),
      mXWindow(0),
      mXVisual(nullptr)
#endif
{
}

WindowSurfaceProvider::~WindowSurfaceProvider() {
#ifdef MOZ_WAYLAND
  MOZ_DIAGNOSTIC_ASSERT(!mWidget,
                        "nsWindow reference is still live, we're leaking it!");
#endif
#ifdef MOZ_X11
  MOZ_DIAGNOSTIC_ASSERT(!mXWindow, "mXWindow should be released on quit!");
#endif
}

#ifdef MOZ_WAYLAND
bool WindowSurfaceProvider::Initialize(RefPtr<nsWindow> aWidget) {
  mWindowSurfaceValid = false;
  mWidget = std::move(aWidget);
  return true;
}
bool WindowSurfaceProvider::Initialize(GtkCompositorWidget* aCompositorWidget) {
  mWindowSurfaceValid = false;
  mCompositorWidget = aCompositorWidget;
  mWidget = static_cast<nsWindow*>(aCompositorWidget->RealWidget());
  return true;
}
#endif
#ifdef MOZ_X11
bool WindowSurfaceProvider::Initialize(Window aWindow) {
  mWindowSurfaceValid = false;

  // Grab the window's visual and depth
  XWindowAttributes windowAttrs;
  if (!XGetWindowAttributes(DefaultXDisplay(), aWindow, &windowAttrs)) {
    NS_WARNING("GtkCompositorWidget(): XGetWindowAttributes() failed!");
    return false;
  }

  mXWindow = aWindow;
  mXVisual = windowAttrs.visual;
  mXDepth = windowAttrs.depth;
  return true;
}
#endif

void WindowSurfaceProvider::CleanupResources() {
  MutexAutoLock lock(mMutex);
  mWindowSurfaceValid = false;
#ifdef MOZ_WAYLAND
  mWidget = nullptr;
#endif
#ifdef MOZ_X11
  mXWindow = 0;
  mXVisual = nullptr;
  mXDepth = 0;
#endif
}

RefPtr<WindowSurface> WindowSurfaceProvider::CreateWindowSurface() {
#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay()) {
    // We're called too early or we're unmapped.
    if (!mWidget) {
      return nullptr;
    }
    if (mWidget->IsDragPopup() &&
        !StaticPrefs::widget_wayland_native_dnd_popups_AtStartup()) {
      return MakeRefPtr<WindowSurfaceCairo>(mWidget);
    }
    return MakeRefPtr<WindowSurfaceWaylandMB>(mWidget, mCompositorWidget);
  }
#endif
#ifdef MOZ_X11
  if (GdkIsX11Display()) {
    // We're called too early or we're unmapped.
    if (!mXWindow) {
      return nullptr;
    }
    // Blit to the window with the following priority:
    // 1. MIT-SHM
    // 2. XPutImage
#  ifdef MOZ_HAVE_SHMIMAGE
    if (nsShmImage::UseShm()) {
      LOG(("Drawing to Window 0x%lx will use MIT-SHM\n", mXWindow.load()));
      return MakeRefPtr<WindowSurfaceX11SHM>(DefaultXDisplay(), mXWindow,
                                             mXVisual, mXDepth);
    }
#  endif  // MOZ_HAVE_SHMIMAGE

    LOG(("Drawing to Window 0x%lx will use XPutImage\n", mXWindow.load()));
    return MakeRefPtr<WindowSurfaceX11Image>(DefaultXDisplay(), mXWindow,
                                             mXVisual, mXDepth);
  }
#endif
  MOZ_RELEASE_ASSERT(false);
}

// We need to ignore thread safety checks here. We need to hold mMutex
// between StartRemoteDrawingInRegion()/EndRemoteDrawingInRegion() calls
// which confuses it.
MOZ_PUSH_IGNORE_THREAD_SAFETY

already_AddRefed<gfx::DrawTarget>
WindowSurfaceProvider::StartRemoteDrawingInRegion(
    const LayoutDeviceIntRegion& aInvalidRegion) {
  if (aInvalidRegion.IsEmpty()) {
    return nullptr;
  }

  // We return a reference to mWindowSurface inside draw target so we need to
  // hold the mutex untill EndRemoteDrawingInRegion() call where draw target
  // is returned.
  // If we return null dt, EndRemoteDrawingInRegion() won't be called to
  // release mutex.
  mMutex.Lock();
  auto unlockMutex = MakeScopeExit([&] { mMutex.Unlock(); });

  if (!mWindowSurfaceValid) {
    mWindowSurface = nullptr;
    mWindowSurfaceValid = true;
  }

  if (!mWindowSurface) {
    mWindowSurface = CreateWindowSurface();
    if (!mWindowSurface) {
      return nullptr;
    }
  }

  RefPtr<gfx::DrawTarget> dt = mWindowSurface->Lock(aInvalidRegion);
#ifdef MOZ_X11
  if (!dt && GdkIsX11Display() && !mWindowSurface->IsFallback()) {
    // We can't use WindowSurfaceX11Image fallback on Wayland but
    // Lock() call on WindowSurfaceWayland should never fail.
    gfxWarningOnce()
        << "Failed to lock WindowSurface, falling back to XPutImage backend.";
    mWindowSurface = MakeRefPtr<WindowSurfaceX11Image>(
        DefaultXDisplay(), mXWindow, mXVisual, mXDepth);
    dt = mWindowSurface->Lock(aInvalidRegion);
  }
#endif
  if (dt) {
    // We have valid dt, mutex will be released in EndRemoteDrawingInRegion().
    unlockMutex.release();
  }

  return dt.forget();
}

void WindowSurfaceProvider::EndRemoteDrawingInRegion(
    gfx::DrawTarget* aDrawTarget, const LayoutDeviceIntRegion& aInvalidRegion) {
  // Unlock mutex from StartRemoteDrawingInRegion().
  mMutex.AssertCurrentThreadOwns();
  auto unlockMutex = MakeScopeExit([&] { mMutex.Unlock(); });

  // Commit to mWindowSurface only if we have a valid one.
  if (!mWindowSurface || !mWindowSurfaceValid) {
    return;
  }
  mWindowSurface->Commit(aInvalidRegion);
}

MOZ_POP_THREAD_SAFETY

}  // namespace widget
}  // namespace mozilla
