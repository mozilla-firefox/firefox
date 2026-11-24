/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNativeThemeGTK.h"

#include <dlfcn.h>

#include "GtkWidgets.h"
#include "cairo.h"
#include "gfxContext.h"
#include "gtk/gtk.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/WidgetUtilsGtk.h"
#include "nsDeviceContext.h"
#include "nsIFrame.h"
#include "nsPresContext.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::widget;

nsNativeThemeGTK::nsNativeThemeGTK() : Theme(ScrollbarStyle()) {}

nsNativeThemeGTK::~nsNativeThemeGTK() { GtkWidgets::Shutdown(); }

// This is easy to extend to 9-patch if we ever paint native widgets
// again, but we are very unlikely to do that.
static RefPtr<DataSourceSurface> GetWidgetFourPatch(
    nsIFrame* aFrame, GtkWidgets::Type aWidget, CSSIntCoord aSectionSize,
    CSSToLayoutDeviceScale aScale) {
  static auto sCairoSurfaceSetDeviceScalePtr =
      (void (*)(cairo_surface_t*, double, double))dlsym(
          RTLD_DEFAULT, "cairo_surface_set_device_scale");

  CSSIntRect rect(0, 0, aSectionSize * 2, aSectionSize * 2);
  // Save actual widget scale to GtkWidgetState as we don't provide
  // the frame to gtk3drawing routines.
  GtkWidgets::DrawingParams params{
      .widget = aWidget,
      .rect = {rect.x, rect.y, rect.width, rect.height},
      .state = GTK_STATE_FLAG_NORMAL,
      .image_scale = gint(std::ceil(aScale.scale)),
  };

  if (aFrame->PresContext()->Document()->State().HasState(
          dom::DocumentState::WINDOW_INACTIVE)) {
    params.state = GtkStateFlags(gint(params.state) | GTK_STATE_FLAG_BACKDROP);
  }

  auto surfaceRect = RoundedOut(rect * aScale);
  RefPtr<DataSourceSurface> dataSurface = Factory::CreateDataSourceSurface(
      surfaceRect.Size().ToUnknownSize(), SurfaceFormat::B8G8R8A8,
      /* aZero = */ true);
  if (NS_WARN_IF(!dataSurface)) {
    return nullptr;
  }
  DataSourceSurface::ScopedMap map(dataSurface,
                                   DataSourceSurface::MapType::WRITE);
  if (NS_WARN_IF(!map.IsMapped())) {
    return nullptr;
  }
  // Create a Cairo image surface wrapping the data surface.
  cairo_surface_t* surf = cairo_image_surface_create_for_data(
      map.GetData(), CAIRO_FORMAT_ARGB32, surfaceRect.width, surfaceRect.height,
      map.GetStride());
  if (NS_WARN_IF(!surf)) {
    return nullptr;
  }
  if (cairo_t* cr = cairo_create(surf)) {
    if (aScale.scale != 1.0) {
      if (sCairoSurfaceSetDeviceScalePtr) {
        sCairoSurfaceSetDeviceScalePtr(surf, aScale.scale, aScale.scale);
      } else {
        cairo_scale(cr, aScale.scale, aScale.scale);
      }
    }
    GtkWidgets::Draw(cr, &params);
    cairo_destroy(cr);
  }
  cairo_surface_destroy(surf);
  return dataSurface;
}

static void DrawWindowDecorationsWithCairo(nsIFrame* aFrame,
                                           gfxContext* aContext, bool aSnapped,
                                           const Point& aDrawOrigin,
                                           const nsIntSize& aDrawSize) {
  DrawTarget* dt = aContext->GetDrawTarget();
  // If we are not snapped, we depend on the DT for translation.
  // Otherwise, we only need to take the device offset into account.
  const Point drawOffset = aSnapped ? aDrawOrigin -
                                          dt->GetTransform().GetTranslation() -
                                          aContext->GetDeviceOffset()
                                    : aDrawOrigin;

  const CSSIntCoord sectionSize =
      LookAndFeel::GetInt(LookAndFeel::IntID::TitlebarRadius);
  if (!sectionSize) {
    return;
  }

  const CSSToLayoutDeviceScale scaleFactor{
      float(AppUnitsPerCSSPixel()) /
      float(aFrame->PresContext()
                ->DeviceContext()
                ->AppUnitsPerDevPixelAtUnitFullZoom())};
  RefPtr dataSurface = GetWidgetFourPatch(
      aFrame, GtkWidgets::Type::WindowDecoration, sectionSize, scaleFactor);
  if (NS_WARN_IF(!dataSurface)) {
    return;
  }

  LayoutDeviceSize scaledSize(CSSCoord(sectionSize) * scaleFactor,
                              CSSCoord(sectionSize) * scaleFactor);

  // Top left.
  dt->DrawSurface(dataSurface, Rect(drawOffset, scaledSize.ToUnknownSize()),
                  Rect(Point(), scaledSize.ToUnknownSize()));
  // Top right.
  dt->DrawSurface(dataSurface,
                  Rect(Point(drawOffset.x + aDrawSize.width - scaledSize.width,
                             drawOffset.y),
                       scaledSize.ToUnknownSize()),
                  Rect(Point(scaledSize.width, 0), scaledSize.ToUnknownSize()));
  if (StaticPrefs::widget_gtk_rounded_bottom_corners_enabled()) {
    // Bottom left.
    dt->DrawSurface(
        dataSurface,
        Rect(Point(drawOffset.x,
                   drawOffset.y + aDrawSize.height - scaledSize.height),
             scaledSize.ToUnknownSize()),
        Rect(Point(0, scaledSize.height), scaledSize.ToUnknownSize()));

    // Bottom right
    dt->DrawSurface(
        dataSurface,
        Rect(Point(drawOffset.x + aDrawSize.width - scaledSize.width,
                   drawOffset.y + aDrawSize.height - scaledSize.height),
             scaledSize.ToUnknownSize()),
        Rect(Point(scaledSize.width, scaledSize.height),
             scaledSize.ToUnknownSize()));
  }
}

void nsNativeThemeGTK::DrawWidgetBackground(
    gfxContext* aContext, nsIFrame* aFrame, StyleAppearance aAppearance,
    const nsRect& aRect, const nsRect& aDirtyRect, DrawOverflow aDrawOverflow) {
  if (IsWidgetNonNative(aFrame, aAppearance) != NonNative::No) {
    return Theme::DrawWidgetBackground(aContext, aFrame, aAppearance, aRect,
                                       aDirtyRect, aDrawOverflow);
  }

  if (NS_WARN_IF(aAppearance != StyleAppearance::MozWindowDecorations)) {
    return;
  }

  if (GdkIsWaylandDisplay()) {
    // We don't need to paint window decorations on Wayland, see the comments in
    // browser.css
    return;
  }

  gfxContext* ctx = aContext;
  nsPresContext* presContext = aFrame->PresContext();

  gfxRect rect = presContext->AppUnitsToGfxUnits(aRect);
  gfxRect dirtyRect = presContext->AppUnitsToGfxUnits(aDirtyRect);

  // Align to device pixels where sensible
  // to provide crisper and faster drawing.
  // Don't snap if it's a non-unit scale factor. We're going to have to take
  // slow paths then in any case.
  // We prioritize the size when snapping in order to avoid distorting widgets
  // that should be square, which can occur if edges are snapped independently.
  bool snapped = ctx->UserToDevicePixelSnapped(
      rect, gfxContext::SnapOption::PrioritizeSize);
  if (snapped) {
    // Leave rect in device coords but make dirtyRect consistent.
    dirtyRect = ctx->UserToDevice(dirtyRect);
  }

  // Translate the dirty rect so that it is wrt the widget top-left.
  dirtyRect.MoveBy(-rect.TopLeft());
  // Round out the dirty rect to gdk pixels to ensure that gtk draws
  // enough pixels for interpolation to device pixels.
  dirtyRect.RoundOut();

  // GTK themes can only draw an integer number of pixels
  // (even when not snapped).
  LayoutDeviceIntRect widgetRect(0, 0, NS_lround(rect.Width()),
                                 NS_lround(rect.Height()));

  // This is the rectangle that will actually be drawn, in gdk pixels
  LayoutDeviceIntRect drawingRect(
      int32_t(dirtyRect.X()), int32_t(dirtyRect.Y()),
      int32_t(dirtyRect.Width()), int32_t(dirtyRect.Height()));
  if (widgetRect.IsEmpty() ||
      !drawingRect.IntersectRect(widgetRect, drawingRect)) {
    return;
  }

  // translate everything so (0,0) is the top left of the drawingRect
  gfxPoint origin = rect.TopLeft() + drawingRect.TopLeft().ToUnknownPoint();
  DrawWindowDecorationsWithCairo(aFrame, ctx, snapped, ToPoint(origin),
                                 drawingRect.Size().ToUnknownSize());
}

bool nsNativeThemeGTK::CreateWebRenderCommandsForWidget(
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager, nsIFrame* aFrame,
    StyleAppearance aAppearance, const nsRect& aRect) {
  if (IsWidgetNonNative(aFrame, aAppearance) != NonNative::No) {
    return Theme::CreateWebRenderCommandsForWidget(
        aBuilder, aResources, aSc, aManager, aFrame, aAppearance, aRect);
  }
  if (aAppearance == StyleAppearance::MozWindowDecorations &&
      GdkIsWaylandDisplay()) {
    // On wayland we don't need to draw window decorations.
    return true;
  }
  return false;
}

LayoutDeviceIntMargin nsNativeThemeGTK::GetWidgetBorder(
    nsDeviceContext* aContext, nsIFrame* aFrame, StyleAppearance aAppearance) {
  if (IsWidgetAlwaysNonNative(aFrame, aAppearance)) {
    return Theme::GetWidgetBorder(aContext, aFrame, aAppearance);
  }
  return {};
}

bool nsNativeThemeGTK::GetWidgetPadding(nsDeviceContext* aContext,
                                        nsIFrame* aFrame,
                                        StyleAppearance aAppearance,
                                        LayoutDeviceIntMargin* aResult) {
  if (IsWidgetAlwaysNonNative(aFrame, aAppearance)) {
    return Theme::GetWidgetPadding(aContext, aFrame, aAppearance, aResult);
  }
  return false;
}

bool nsNativeThemeGTK::GetWidgetOverflow(nsDeviceContext* aContext,
                                         nsIFrame* aFrame,
                                         StyleAppearance aAppearance,
                                         nsRect* aOverflowRect) {
  if (IsWidgetNonNative(aFrame, aAppearance) != NonNative::No) {
    return Theme::GetWidgetOverflow(aContext, aFrame, aAppearance,
                                    aOverflowRect);
  }
  return false;
}

auto nsNativeThemeGTK::IsWidgetNonNative(nsIFrame* aFrame,
                                         StyleAppearance aAppearance)
    -> NonNative {
  if (IsWidgetAlwaysNonNative(aFrame, aAppearance)) {
    return NonNative::Always;
  }

  // If the current GTK theme color scheme matches our color-scheme, then we
  // can draw a native widget.
  if (LookAndFeel::ColorSchemeForFrame(aFrame) ==
      PreferenceSheet::ColorSchemeForChrome()) {
    return NonNative::No;
  }

  // If the non-native theme doesn't support the widget then oh well...
  if (!Theme::ThemeSupportsWidget(aFrame->PresContext(), aFrame, aAppearance)) {
    return NonNative::No;
  }

  return NonNative::BecauseColorMismatch;
}

bool nsNativeThemeGTK::IsWidgetAlwaysNonNative(nsIFrame* aFrame,
                                               StyleAppearance aAppearance) {
  return Theme::IsWidgetAlwaysNonNative(aFrame, aAppearance) ||
         aAppearance == StyleAppearance::MozMenulistArrowButton ||
         aAppearance == StyleAppearance::Textfield ||
         aAppearance == StyleAppearance::NumberInput ||
         aAppearance == StyleAppearance::PasswordInput ||
         aAppearance == StyleAppearance::Textarea ||
         aAppearance == StyleAppearance::Checkbox ||
         aAppearance == StyleAppearance::Radio ||
         aAppearance == StyleAppearance::Button ||
         aAppearance == StyleAppearance::Listbox ||
         aAppearance == StyleAppearance::Menulist;
}

LayoutDeviceIntSize nsNativeThemeGTK::GetMinimumWidgetSize(
    nsPresContext* aPresContext, nsIFrame* aFrame,
    StyleAppearance aAppearance) {
  if (IsWidgetAlwaysNonNative(aFrame, aAppearance)) {
    return Theme::GetMinimumWidgetSize(aPresContext, aFrame, aAppearance);
  }
  return {};
}

bool nsNativeThemeGTK::ThemeSupportsWidget(nsPresContext* aPresContext,
                                           nsIFrame* aFrame,
                                           StyleAppearance aAppearance) {
  if (IsWidgetAlwaysNonNative(aFrame, aAppearance)) {
    return Theme::ThemeSupportsWidget(aPresContext, aFrame, aAppearance);
  }
  return aAppearance == StyleAppearance::MozWindowDecorations;
}

bool nsNativeThemeGTK::ThemeDrawsFocusForWidget(nsIFrame* aFrame,
                                                StyleAppearance aAppearance) {
  if (IsWidgetNonNative(aFrame, aAppearance) != NonNative::No) {
    return Theme::ThemeDrawsFocusForWidget(aFrame, aAppearance);
  }
  return false;
}

nsITheme::Transparency nsNativeThemeGTK::GetWidgetTransparency(
    nsIFrame* aFrame, StyleAppearance aAppearance) {
  if (IsWidgetNonNative(aFrame, aAppearance) != NonNative::No) {
    return Theme::GetWidgetTransparency(aFrame, aAppearance);
  }

  return eUnknownTransparency;
}

already_AddRefed<Theme> do_CreateNativeThemeDoNotUseDirectly() {
  if (gfxPlatform::IsHeadless()) {
    return do_AddRef(new Theme(Theme::ScrollbarStyle()));
  }
  return do_AddRef(new nsNativeThemeGTK());
}
