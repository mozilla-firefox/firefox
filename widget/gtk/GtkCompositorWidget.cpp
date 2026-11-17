/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GtkCompositorWidget.h"

#include "mozilla/WidgetUtilsGtk.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/widget/PlatformWidgetTypes.h"
#include "nsWindow.h"

#ifdef MOZ_X11
#  include "mozilla/X11Util.h"
#endif

#ifdef MOZ_WAYLAND
#  include "mozilla/layers/NativeLayerRootRemoteWaylandChild.h"
#  include "mozilla/layers/NativeLayerWayland.h"
#endif

#ifdef MOZ_LOGGING
#  undef LOG
#  define LOG(str, ...)                               \
    MOZ_LOG(IsPopup() ? gWidgetPopupLog : gWidgetLog, \
            mozilla::LogLevel::Debug,                 \
            ("[%p]: " str, mWidget.get(), ##__VA_ARGS__))
#endif /* MOZ_LOGGING */

namespace mozilla {
namespace widget {

GtkCompositorWidget::GtkCompositorWidget(
    const GtkCompositorWidgetInitData& aInitData,
    const layers::CompositorOptions& aOptions, RefPtr<nsWindow> aWindow)
    : CompositorWidget(aOptions),
      mWidget(std::move(aWindow)),
      mClientSize(LayoutDeviceIntSize(aInitData.clientSize()),
                  "GtkCompositorWidget::mClientSize") {
#if defined(MOZ_X11)
  if (GdkIsX11Display()) {
    ConfigureX11Backend((Window)aInitData.XWindow());
    LOG("GtkCompositorWidget::GtkCompositorWidget() [%p] mXWindow %p\n",
        (void*)mWidget.get(), (void*)aInitData.XWindow());
  }
#endif
#if defined(MOZ_WAYLAND)
  if (GdkIsWaylandDisplay()) {
    ConfigureWaylandBackend();
    LOG("GtkCompositorWidget::GtkCompositorWidget() [%p] mWidget %p\n",
        (void*)mWidget.get(), (void*)mWidget);
  }
#endif
  auto size = LayoutDeviceIntSize(aInitData.clientSize());
  LOG("  client size %d x %d", size.width, size.height);
}

GtkCompositorWidget::~GtkCompositorWidget() {
  LOG("GtkCompositorWidget::~GtkCompositorWidget [%p]\n", (void*)mWidget.get());
  CleanupResources();
#ifdef MOZ_WAYLAND
  if (RefPtr<layers::NativeLayerRootWayland> root =
          mNativeLayerRoot ? mNativeLayerRoot->AsNativeLayerRootWayland()
                           : nullptr) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "~GtkCompositorWidget::NativeLayerRootWayland::Shutdown()",
        [root = std::move(root)]() -> void { root->Shutdown(); }));
  }
#endif
  RefPtr<nsIWidget> widget = mWidget.forget();
  NS_ReleaseOnMainThread("GtkCompositorWidget::mWidget", widget.forget());
}

already_AddRefed<gfx::DrawTarget> GtkCompositorWidget::StartRemoteDrawing() {
  return nullptr;
}
void GtkCompositorWidget::EndRemoteDrawing() {}

already_AddRefed<gfx::DrawTarget>
GtkCompositorWidget::StartRemoteDrawingInRegion(
    const LayoutDeviceIntRegion& aInvalidRegion) {
  return mProvider.StartRemoteDrawingInRegion(aInvalidRegion);
}

void GtkCompositorWidget::EndRemoteDrawingInRegion(
    gfx::DrawTarget* aDrawTarget, const LayoutDeviceIntRegion& aInvalidRegion) {
  mProvider.EndRemoteDrawingInRegion(aDrawTarget, aInvalidRegion);
}

nsIWidget* GtkCompositorWidget::RealWidget() { return mWidget; }

void GtkCompositorWidget::NotifyClientSizeChanged(
    const LayoutDeviceIntSize& aClientSize) {
  LOG("GtkCompositorWidget::NotifyClientSizeChanged() to %d x %d",
      aClientSize.width, aClientSize.height);

  auto size = mClientSize.Lock();
  *size = aClientSize;
}

void GtkCompositorWidget::NotifyFullscreenChanged(bool aIsFullscreen) {
#ifdef MOZ_WAYLAND
  if (auto* root = mNativeLayerRoot
                       ? mNativeLayerRoot->AsNativeLayerRootWayland()
                       : nullptr) {
    LOG("GtkCompositorWidget::NotifyFullscreenChanged() [%d]", aIsFullscreen);
    root->NotifyFullscreenChanged(aIsFullscreen);
  }
#endif
}

LayoutDeviceIntSize GtkCompositorWidget::GetClientSize() {
  auto size = mClientSize.Lock();
  return *size;
}

EGLNativeWindowType GtkCompositorWidget::GetEGLNativeWindow() {
  EGLNativeWindowType window = nullptr;
  if (mWidget) {
    window = (EGLNativeWindowType)mWidget->GetNativeData(NS_NATIVE_EGL_WINDOW);
  }
#if defined(MOZ_X11)
  else {
    window = (EGLNativeWindowType)mProvider.GetXWindow();
  }
#endif
  LOG("GtkCompositorWidget::GetEGLNativeWindow [%p] window %p\n", mWidget.get(),
      window);
  return window;
}

void GtkCompositorWidget::SetEGLNativeWindowSize(
    const LayoutDeviceIntSize& aEGLWindowSize) {
#if defined(MOZ_WAYLAND)
  // We explicitly need to set EGL window size on Wayland only.
  if (mWidget && mWidget->GetWaylandSurface()) {
    mWidget->GetWaylandSurface()->ApplyEGLWindowSize(aEGLWindowSize);
  }
#endif
}

LayoutDeviceIntRegion GtkCompositorWidget::GetTransparentRegion() {
  LayoutDeviceIntRegion fullRegion(
      LayoutDeviceIntRect(LayoutDeviceIntPoint(), GetClientSize()));
  if (mWidget) {
    fullRegion.SubOut(mWidget->GetOpaqueRegion());
  }
  return fullRegion;
}

#ifdef MOZ_WAYLAND
void GtkCompositorWidget::Init(CompositorWidgetInitData&& aInitData) {
  MOZ_ASSERT(XRE_IsGPUProcess());
  // This method is only intended to be called from the GPU process,
  // which is why it uses move semantics for aInitData.
  GtkCompositorWidgetInitData gtkInitData(
      std::move((aInitData).get_GtkCompositorWidgetInitData()));
  auto size = mClientSize.Lock();
  *size = gtkInitData.clientSize();
  mChildEndpoint = std::move(gtkInitData.childEndpoint());
}

mozilla::layers::NativeLayerRoot* GtkCompositorWidget::GetNativeLayerRoot() {
  if (!mNativeLayerRoot) {
    CreateNativeLayerRoot();
    MOZ_ASSERT(mNativeLayerRoot);
  }
  return mNativeLayerRoot;
}

void GtkCompositorWidget::CreateNativeLayerRoot() {
  LOG("GtkCompositorWidget::CreateNativeLayerRoot");
  if (XRE_IsGPUProcess()) {
    auto root = MakeRefPtr<layers::NativeLayerRootRemoteWaylandChild>();
    RefPtr<layers::NativeLayerRemoteChild> remoteChild = root->GetRemoteChild();
    MOZ_ALWAYS_TRUE(mChildEndpoint.Bind(remoteChild));
    mNativeLayerRoot = std::move(root);
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(mWidget && mWidget->GetWaylandSurface());
  RefPtr surface = mWidget->GetWaylandSurface();
  surface->MarkAsRootSurface();
  RefPtr<layers::NativeLayerRootWayland> root =
      layers::NativeLayerRootWayland::Create(surface);
  root->Init();
  mNativeLayerRoot = std::move(root);
}
#endif

void GtkCompositorWidget::CleanupResources() {
  LOG("GtkCompositorWidget::CleanupResources [%p]\n", (void*)mWidget.get());
  mProvider.CleanupResources();
}

#if defined(MOZ_WAYLAND)
void GtkCompositorWidget::ConfigureWaylandBackend() {
  mProvider.Initialize(this);
}
#endif

#if defined(MOZ_X11)
void GtkCompositorWidget::ConfigureX11Backend(Window aXWindow) {
  // We don't have X window yet.
  if (!aXWindow) {
    mProvider.CleanupResources();
    return;
  }
  // Initialize the window surface provider
  mProvider.Initialize(aXWindow);
}
#endif

#ifdef MOZ_LOGGING
bool GtkCompositorWidget::IsPopup() {
  return mWidget ? mWidget->IsPopup() : false;
}
#endif

UniquePtr<WaylandSurfaceLock> GtkCompositorWidget::LockSurface() {
  return mWidget ? mWidget->LockSurface() : nullptr;
}

}  // namespace widget
}  // namespace mozilla
