/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WindowSurfaceWaylandMultiBuffer.h"

#include <errno.h>
#include <fcntl.h>
#include <prenv.h>
#include <sys/mman.h>

#include "GtkCompositorWidget.h"
#include "MozContainer.h"
#include "gfx2DGlue.h"
#include "gfxPlatform.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/WidgetUtils.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/gfx/Tools.h"

#undef LOGWAYLAND
#ifdef MOZ_LOGGING
#  include "Units.h"
#  include "mozilla/Logging.h"
extern mozilla::LazyLogModule gWidgetWaylandLog;
#  define LOGWAYLAND(...) \
    MOZ_LOG(gWidgetWaylandLog, mozilla::LogLevel::Debug, (__VA_ARGS__))
#else
#  define LOGWAYLAND(...)
#endif /* MOZ_LOGGING */

namespace mozilla::widget {

/*
  Wayland multi-thread rendering scheme

  Every rendering thread (main thread, compositor thread) contains its own
  nsWaylandDisplay object connected to Wayland compositor (Mutter, Weston, etc.)

  WindowSurfaceWayland implements WindowSurface class and draws nsWindow by
  WindowSurface interface (Lock, Commit) to screen through nsWaylandDisplay.

  ----------------------
  | Wayland compositor |
  ----------------------
             ^
             |
  ----------------------
  |  nsWaylandDisplay  |
  ----------------------
        ^          ^
        |          |
        |          |
        |       ---------------------------------        ------------------
        |       | WindowSurfaceWayland          |<------>| nsWindow       |
        |       |                               |        ------------------
        |       |  -----------------------      |
        |       |  | WaylandBuffer       |      |
        |       |  |                     |      |
        |       |  | ------------------- |      |
        |       |  | |  SHMBufSurface  | |      |
        |       |  | ------------------- |      |
        |       |  -----------------------      |
        |       |                               |
        |       |  -----------------------      |
        |       |  | WaylandBuffer       |      |
        |       |  |                     |      |
        |       |  | ------------------- |      |
        |       |  | |  SHMBufSurface  | |      |
        |       |  | ------------------- |      |
        |       |  -----------------------      |
        |       ---------------------------------
        |
        |
  ---------------------------------        ------------------
  | WindowSurfaceWayland          |<------>| nsWindow       |
  |                               |        ------------------
  |  -----------------------      |
  |  | WaylandBuffer       |      |
  |  |                     |      |
  |  | ------------------- |      |
  |  | |  SHMBufSurface  | |      |
  |  | ------------------- |      |
  |  -----------------------      |
  |                               |
  |  -----------------------      |
  |  | WaylandBuffer       |      |
  |  |                     |      |
  |  | ------------------- |      |
  |  | |  SHMBufSurface  | |      |
  |  | ------------------- |      |
  |  -----------------------      |
  ---------------------------------


nsWaylandDisplay

Is our connection to Wayland display server,
holds our display connection (wl_display) and event queue (wl_event_queue).

nsWaylandDisplay is created for every thread which sends data to Wayland
compositor. Wayland events for main thread is served by default Gtk+ loop,
for other threads (compositor) we must create wl_event_queue and run event loop.


WindowSurfaceWayland

Is a Wayland implementation of WindowSurface class for WindowSurfaceProvider,
we implement Lock() and Commit() interfaces from WindowSurface
for actual drawing.

One WindowSurfaceWayland draws one nsWindow so those are tied 1:1.
At Wayland level it holds one wl_surface object.

To perform visualiation of nsWindow, WindowSurfaceWayland contains one
wl_surface and two wl_buffer objects (owned by WaylandBuffer)
as we use double buffering. When nsWindow drawing is finished to wl_buffer,
the wl_buffer is attached to wl_surface and it's sent to Wayland compositor.

When there's no wl_buffer available for drawing (all wl_buffers are locked in
compositor for instance) we store the drawing to WindowImageSurface object
and draw later when wl_buffer becomes available or discard the
WindowImageSurface cache when whole screen is invalidated.

WaylandBuffer

Is a class which provides a wl_buffer for drawing.
Wl_buffer is a main Wayland object with actual graphics data.
Wl_buffer basically represent one complete window screen.
When double buffering is involved every window (GdkWindow for instance)
utilises two wl_buffers which are cycled. One is filed with data by application
and one is rendered by compositor.

WaylandBuffer is implemented by shared memory (shm).
It owns wl_buffer object, owns SHMBufSurface
(which provides the shared memory) and ties them together.

SHMBufSurface

SHMBufSurface acts as a manager of shared memory for WaylandBuffer.
Allocates it, holds reference to it and releases it.

We allocate shared memory (shm) by mmap(..., MAP_SHARED,...) as an interface
between us and wayland compositor. We draw our graphics data to the shm and
handle to wayland compositor by WaylandBuffer/WindowSurfaceWayland
(wl_buffer/wl_surface).
*/

using gfx::DataSourceSurface;

#define BACK_BUFFER_NUM 3

WindowSurfaceWaylandMB::WindowSurfaceWaylandMB(
    RefPtr<nsWindow> aWindow, GtkCompositorWidget* aCompositorWidget)
    : mWindow(std::move(aWindow)),
      mWaylandSurface(mWindow->GetWaylandSurface()),
      mCompositorWidget(aCompositorWidget) {}

bool WindowSurfaceWaylandMB::MaybeUpdateWindowSize() {
  // We want to get window size from compositor widget as it matches window
  // size used by parent RenderCompositorSWGL rendrer.
  // For main thread rendering mCompositorWidget is not available so get
  // window size directly from nsWindow.
  LayoutDeviceIntSize newWindowSize = mCompositorWidget
                                          ? mCompositorWidget->GetClientSize()
                                          : mWindow->GetClientSize();
  if (mWindowSize != newWindowSize) {
    mWindowSize = newWindowSize;
    return true;
  }
  return false;
}

already_AddRefed<DrawTarget> WindowSurfaceWaylandMB::Lock(
    const LayoutDeviceIntRegion& aInvalidRegion) {
  WaylandSurfaceLock lock(mWaylandSurface);

#ifdef MOZ_LOGGING
  gfx::IntRect lockRect = aInvalidRegion.GetBounds().ToUnknownRect();
  LOGWAYLAND("WindowSurfaceWaylandMB::Lock [%p] [%d,%d] -> [%d x %d] rects %d",
             (void*)mWindow.get(), lockRect.x, lockRect.y, lockRect.width,
             lockRect.height, aInvalidRegion.GetNumRects());
#endif

  if (mWindow->GetWindowType() == WindowType::Invisible) {
    return nullptr;
  }

  CollectPendingSurfaces(lock);

  if (MaybeUpdateWindowSize()) {
    LOGWAYLAND("  new window size [%d x %d]", mWindowSize.width,
               mWindowSize.height);
    if (mInProgressBuffer) {
      ReturnBufferToPool(lock, mInProgressBuffer);
      mInProgressBuffer = nullptr;
    }
    if (mFrontBuffer) {
      ReturnBufferToPool(lock, mFrontBuffer);
      mFrontBuffer = nullptr;
    }
    mAvailableBuffers.Clear();
  }

  if (!mInProgressBuffer) {
    if (mFrontBuffer && !mFrontBuffer->IsAttached(lock)) {
      mInProgressBuffer = mFrontBuffer;
    } else {
      mInProgressBuffer = ObtainBufferFromPool(lock, mWindowSize);
      if (!mInProgressBuffer) {
        return nullptr;
      }
      if (mFrontBuffer) {
        HandlePartialUpdate(lock, aInvalidRegion);
        ReturnBufferToPool(lock, mFrontBuffer);
      }
    }
    mFrontBuffer = nullptr;
    mFrontBufferInvalidRegion.SetEmpty();
  }

  RefPtr<DrawTarget> dt = mInProgressBuffer->Lock();
  return dt.forget();
}

void WindowSurfaceWaylandMB::HandlePartialUpdate(
    const WaylandSurfaceLock& aWaylandSurfaceLock,
    const LayoutDeviceIntRegion& aInvalidRegion) {
  LayoutDeviceIntRegion copyRegion;
  if (mInProgressBuffer->GetBufferAge() == 2) {
    copyRegion.Sub(mFrontBufferInvalidRegion, aInvalidRegion);
  } else {
    LayoutDeviceIntSize frontBufferSize = mFrontBuffer->GetSize();
    copyRegion = LayoutDeviceIntRegion(LayoutDeviceIntRect(
        0, 0, frontBufferSize.width, frontBufferSize.height));
    copyRegion.SubOut(aInvalidRegion);
  }

  if (!copyRegion.IsEmpty()) {
    RefPtr<DataSourceSurface> dataSourceSurface =
        mozilla::gfx::CreateDataSourceSurfaceFromData(
            mFrontBuffer->GetSize().ToUnknownSize(),
            mFrontBuffer->GetSurfaceFormat(),
            (const uint8_t*)mFrontBuffer->GetImageData(),
            mFrontBuffer->GetStride());
    RefPtr<DrawTarget> dt = mInProgressBuffer->Lock();

    for (auto iter = copyRegion.RectIter(); !iter.Done(); iter.Next()) {
      LayoutDeviceIntRect r = iter.Get();
      dt->CopySurface(dataSourceSurface, r.ToUnknownRect(),
                      gfx::IntPoint(r.x, r.y));
    }
  }
}

void WindowSurfaceWaylandMB::Commit(
    const LayoutDeviceIntRegion& aInvalidRegion) {
  WaylandSurfaceLock lock(mWaylandSurface);
  Commit(lock, aInvalidRegion);
}

void WindowSurfaceWaylandMB::Commit(
    const WaylandSurfaceLock& aWaylandSurfaceLock,
    const LayoutDeviceIntRegion& aInvalidRegion) {
#ifdef MOZ_LOGGING
  gfx::IntRect invalidRect = aInvalidRegion.GetBounds().ToUnknownRect();
  LOGWAYLAND(
      "WindowSurfaceWaylandMB::Commit [%p] damage rect [%d, %d] -> [%d x %d] "
      "Window [%d x %d]\n",
      (void*)mWindow.get(), invalidRect.x, invalidRect.y, invalidRect.width,
      invalidRect.height, mWindowSize.width, mWindowSize.height);
#endif

  if (!mInProgressBuffer || !mWaylandSurface->IsMapped()) {
    // invisible window
    return;
  }

  auto waylandSurface = aWaylandSurfaceLock.GetWaylandSurface();
  waylandSurface->InvalidateRegionLocked(aWaylandSurfaceLock,
                                         aInvalidRegion.ToUnknownRegion());
  waylandSurface->AttachLocked(aWaylandSurfaceLock, mInProgressBuffer);
  waylandSurface->CommitLocked(aWaylandSurfaceLock, /* force commit */ true,
                               /* force flush */ true);

  mInProgressBuffer->ResetBufferAge();
  mFrontBuffer = mInProgressBuffer;
  mFrontBufferInvalidRegion = aInvalidRegion;
  mInProgressBuffer = nullptr;

  EnforcePoolSizeLimit(aWaylandSurfaceLock);
  IncrementBufferAge(aWaylandSurfaceLock);
}

RefPtr<WaylandBuffer> WindowSurfaceWaylandMB::ObtainBufferFromPool(
    const WaylandSurfaceLock& aWaylandSurfaceLock,
    const LayoutDeviceIntSize& aSize) {
  if (!mAvailableBuffers.IsEmpty()) {
    RefPtr<WaylandBuffer> buffer = mAvailableBuffers.PopLastElement();
    mInUseBuffers.AppendElement(buffer);
    return buffer;
  }

  RefPtr<WaylandBuffer> buffer = WaylandBuffer::CreateSHM(aSize);
  if (buffer) {
    mInUseBuffers.AppendElement(buffer);
  }

  return buffer;
}

void WindowSurfaceWaylandMB::ReturnBufferToPool(
    const WaylandSurfaceLock& aWaylandSurfaceLock,
    const RefPtr<WaylandBuffer>& aBuffer) {
  if (aBuffer->IsAttached(aWaylandSurfaceLock)) {
    mPendingBuffers.AppendElement(aBuffer);
  } else if (aBuffer->IsMatchingSize(mWindowSize)) {
    mAvailableBuffers.AppendElement(aBuffer);
  }
  mInUseBuffers.RemoveElement(aBuffer);
}

void WindowSurfaceWaylandMB::EnforcePoolSizeLimit(
    const WaylandSurfaceLock& aWaylandSurfaceLock) {
  // Enforce the pool size limit, removing least-recently-used entries as
  // necessary.
  while (mAvailableBuffers.Length() > BACK_BUFFER_NUM) {
    mAvailableBuffers.RemoveElementAt(0);
  }

  NS_WARNING_ASSERTION(mPendingBuffers.Length() < BACK_BUFFER_NUM,
                       "Are we leaking pending buffers?");
  NS_WARNING_ASSERTION(mInUseBuffers.Length() < BACK_BUFFER_NUM,
                       "Are we leaking in-use buffers?");
}

void WindowSurfaceWaylandMB::CollectPendingSurfaces(
    const WaylandSurfaceLock& aWaylandSurfaceLock) {
  mPendingBuffers.RemoveElementsBy([&](auto& buffer) {
    if (!buffer->IsAttached(aWaylandSurfaceLock)) {
      if (buffer->IsMatchingSize(mWindowSize)) {
        mAvailableBuffers.AppendElement(std::move(buffer));
      }
      return true;
    }
    return false;
  });
}

void WindowSurfaceWaylandMB::IncrementBufferAge(
    const WaylandSurfaceLock& aWaylandSurfaceLock) {
  for (const RefPtr<WaylandBuffer>& buffer : mInUseBuffers) {
    buffer->IncrementBufferAge();
  }
  for (const RefPtr<WaylandBuffer>& buffer : mPendingBuffers) {
    buffer->IncrementBufferAge();
  }
  for (const RefPtr<WaylandBuffer>& buffer : mAvailableBuffers) {
    buffer->IncrementBufferAge();
  }
}

}  // namespace mozilla::widget
