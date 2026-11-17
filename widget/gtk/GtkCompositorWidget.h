/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef widget_gtk_GtkCompositorWidget_h
#define widget_gtk_GtkCompositorWidget_h

#include "GLDefs.h"
#include "WaylandSurfaceLock.h"
#include "WindowSurfaceProvider.h"
#include "mozilla/DataMutex.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/NativeLayerRemoteChild.h"
#include "mozilla/widget/CompositorWidget.h"

class nsIWidget;
class nsWindow;

namespace mozilla {

namespace layers {
class NativeLayerRootWayland;
}  // namespace layers

namespace widget {

class PlatformCompositorWidgetDelegate : public CompositorWidgetDelegate {
 public:
  virtual void NotifyFullscreenChanged(bool aIsFullscreen) = 0;

  virtual void CleanupResources() = 0;

  // CompositorWidgetDelegate Overrides

  PlatformCompositorWidgetDelegate* AsPlatformSpecificDelegate() override {
    return this;
  }
};

class GtkCompositorWidgetInitData;

class GtkCompositorWidget : public CompositorWidget {
 public:
  GtkCompositorWidget(const GtkCompositorWidgetInitData& aInitData,
                      const layers::CompositorOptions& aOptions,
                      RefPtr<nsWindow> aWindow /* = nullptr*/);
  ~GtkCompositorWidget();

  // CompositorWidget Overrides

  already_AddRefed<gfx::DrawTarget> StartRemoteDrawing() override;
  void EndRemoteDrawing() override;

  already_AddRefed<gfx::DrawTarget> StartRemoteDrawingInRegion(
      const LayoutDeviceIntRegion& aInvalidRegion) override;
  void EndRemoteDrawingInRegion(
      gfx::DrawTarget* aDrawTarget,
      const LayoutDeviceIntRegion& aInvalidRegion) override;

  LayoutDeviceIntSize GetClientSize() override;

  nsIWidget* RealWidget() override;
  GtkCompositorWidget* AsGTK() override { return this; }

  EGLNativeWindowType GetEGLNativeWindow();

  LayoutDeviceIntRegion GetTransparentRegion() override;

  // Suspend rendering of this remote widget and clear all resources.
  // Can be used when underlying window is hidden/unmapped.
  void CleanupResources();

  // Set EGLWindow size to avoid rendering artifacts
  void SetEGLNativeWindowSize(const LayoutDeviceIntSize& aEGLWindowSize);

#if defined(MOZ_X11)
  Window XWindow() const { return mProvider.GetXWindow(); }
#endif
#if defined(MOZ_WAYLAND)
  void Init(CompositorWidgetInitData&& aInitData);
  mozilla::layers::NativeLayerRoot* GetNativeLayerRoot() override;
#endif

  void NotifyClientSizeChanged(const LayoutDeviceIntSize& aClientSize);
  void NotifyFullscreenChanged(bool aIsFullscreen);

  UniquePtr<WaylandSurfaceLock> LockSurface();

 private:
#if defined(MOZ_WAYLAND)
  void ConfigureWaylandBackend();
#endif
#if defined(MOZ_X11)
  void ConfigureX11Backend(Window aXWindow);
#endif
#ifdef MOZ_LOGGING
  bool IsPopup();
#endif

 protected:
  RefPtr<nsWindow> mWidget;

 private:
#ifdef MOZ_WAYLAND
  void CreateNativeLayerRoot();
#endif

  // This field is written to on the main thread and read from on the compositor
  // or renderer thread. During window resizing, this is subject to a (largely
  // benign) read/write race, see bug 1665726. The DataMutex doesn't prevent the
  // read/write race, but it does make it Not Undefined Behaviour, and also
  // ensures we only ever use the old or new size, and not some weird synthesis
  // of the two.
  DataMutex<LayoutDeviceIntSize> mClientSize;

  ipc::Endpoint<mozilla::layers::PNativeLayerRemoteChild> mChildEndpoint;

  // Holds rendering resources
  WindowSurfaceProvider mProvider;

#ifdef MOZ_WAYLAND
  // A NativeLayerRootWayland in the parent process, or a
  // NativeLayerRootRemoteWaylandChild in the GPU process.
  RefPtr<mozilla::layers::NativeLayerRoot> mNativeLayerRoot;
#endif
};

}  // namespace widget
}  // namespace mozilla

#endif  // widget_gtk_GtkCompositorWidget_h
