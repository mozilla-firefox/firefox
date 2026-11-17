/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef widget_gtk_InProcessGtkCompositorWidget_h
#define widget_gtk_InProcessGtkCompositorWidget_h

#include "GtkCompositorWidget.h"

class nsWindow;

namespace mozilla {
namespace widget {

class InProcessGtkCompositorWidget final
    : public GtkCompositorWidget,
      public PlatformCompositorWidgetDelegate {
 public:
  InProcessGtkCompositorWidget(const GtkCompositorWidgetInitData& aInitData,
                               const layers::CompositorOptions& aOptions,
                               nsWindow* aWindow);

  CompositorWidgetDelegate* AsDelegate() override { return this; }

  void ObserveVsync(VsyncObserver* aObserver) override;

  // PlatformCompositorWidgetDelegate

  void NotifyClientSizeChanged(
      const LayoutDeviceIntSize& aClientSize) override {
    GtkCompositorWidget::NotifyClientSizeChanged(aClientSize);
  }
  void NotifyFullscreenChanged(bool aIsFullscreen) override {
    GtkCompositorWidget::NotifyFullscreenChanged(aIsFullscreen);
  }
  void CleanupResources() override { GtkCompositorWidget::CleanupResources(); }
};

}  // namespace widget
}  // namespace mozilla

#endif  // widget_gtk_InProcessGtkCompositorWidget_h
