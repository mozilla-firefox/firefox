/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// for strtod()
#include "nsLookAndFeel.h"

#include <cairo-gobject.h>
#include <dlfcn.h>
#include <fontconfig/fontconfig.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <pango/pango-fontmap.h>
#include <pango/pango.h>
#include <stdlib.h>

#include "GRefPtr.h"
#include "GSettings.h"
#include "GUniquePtr.h"
#include "GtkWidgets.h"
#include "ScreenHelperGTK.h"
#include "ScrollbarDrawing.h"
#include "WidgetUtils.h"
#include "gfxFontConstants.h"
#include "gfxPlatformGtk.h"
#include "gtk/gtk.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/FontPropertyTypes.h"
#include "mozilla/Preferences.h"
#include "mozilla/RelativeLuminanceUtils.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/WidgetUtilsGtk.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/glean/WidgetGtkMetrics.h"
#include "nsAppShell.h"
#include "nsCSSColorUtils.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsGtkUtils.h"
#include "nsString.h"
#include "nsStyleConsts.h"
#include "nsWindow.h"
#include "prenv.h"

#ifdef MOZ_X11
#  include <X11/XKBlib.h>
#endif

#ifdef MOZ_WAYLAND
#  include <xkbcommon/xkbcommon.h>
#endif

using namespace mozilla;
using namespace mozilla::widget;

#ifdef MOZ_LOGGING
#  include "Units.h"
#  include "mozilla/Logging.h"
#  include "nsTArray.h"
static LazyLogModule gLnfLog("LookAndFeel");
#  define LOGLNF(...) MOZ_LOG(gLnfLog, LogLevel::Debug, (__VA_ARGS__))
#  define LOGLNF_ENABLED() MOZ_LOG_TEST(gLnfLog, LogLevel::Debug)
#else
#  define LOGLNF(args)
#  define LOGLNF_ENABLED() false
#endif /* MOZ_LOGGING */

#define GDK_COLOR_TO_NS_RGB(c) \
  ((nscolor)NS_RGB(c.red >> 8, c.green >> 8, c.blue >> 8))
#define GDK_RGBA_TO_NS_RGBA(c)                                    \
  ((nscolor)NS_RGBA((int)((c).red * 255), (int)((c).green * 255), \
                    (int)((c).blue * 255), (int)((c).alpha * 255)))

static bool sIgnoreChangedSettings = false;

static void OnSettingsChange(nsLookAndFeel* aLnf, NativeChangeKind aKind) {
  // TODO: We could be more granular here, but for now assume everything
  // changed.
  if (sIgnoreChangedSettings) {
    return;
  }
  aLnf->RecordChange(aKind);
  LookAndFeel::NotifyChangedAllWindows(widget::ThemeChangeKind::StyleAndLayout);
  widget::IMContextWrapper::OnThemeChanged();
}

static void settings_changed_cb(GtkSettings*, GParamSpec* aSpec, void*) {
  const char* name = g_param_spec_get_name(aSpec);
  LOGLNF("settings_changed_cb(%s)", name);

  const bool isThemeDependent =
      !strcmp(name, "gtk-theme-name") || !strcmp(name, "gtk-font-name") ||
      !strcmp(name, "gtk-application-prefer-dark-theme");
  auto* lnf = static_cast<nsLookAndFeel*>(nsLookAndFeel::GetInstance());
  auto changeKind = isThemeDependent ? NativeChangeKind::GtkTheme
                                     : NativeChangeKind::OtherSettings;
  OnSettingsChange(lnf, changeKind);
}

// https://docs.gtk.org/gio/signal.FileMonitor.changed.html
static void kde_colors_changed(GFileMonitor* self, void*, void*,
                               GFileMonitorEvent, gpointer) {
  auto* lnf = static_cast<nsLookAndFeel*>(nsLookAndFeel::GetInstance());
  OnSettingsChange(lnf, NativeChangeKind::GtkTheme);
}

static float GetGtkTextScaleFactor() {
  GdkScreen* s = gdk_screen_get_default();
  if (!s) {
    return 1.0f;
  }
  // A non-positive resolution means "unset", not a real DPI.
  const gdouble resolution = gdk_screen_get_resolution(s);
  if (resolution <= 0.0) {
    return 1.0f;
  }
  return float(resolution / 96.0);
}

static bool sCSDAvailable;

#ifdef MOZ_ENABLE_DBUS
static nsCString GVariantToString(GVariant* aVariant) {
  nsCString ret;
  gchar* s = g_variant_print(aVariant, TRUE);
  if (s) {
    ret.Assign(s);
    g_free(s);
  }
  return ret;
}

static nsDependentCString GVariantGetString(GVariant* aVariant) {
  gsize len = 0;
  const gchar* v = g_variant_get_string(aVariant, &len);
  return nsDependentCString(v, len);
}

static void UnboxVariant(RefPtr<GVariant>& aVariant) {
  while (aVariant && g_variant_is_of_type(aVariant, G_VARIANT_TYPE_VARIANT)) {
    // Unbox the return value.
    aVariant = dont_AddRef(g_variant_get_variant(aVariant));
  }
}

static void settings_changed_signal_cb(GDBusProxy* proxy, gchar* sender_name,
                                       gchar* signal_name, GVariant* parameters,
                                       gpointer user_data) {
  LOGLNF("Settings Change sender=%s signal=%s params=%s\n", sender_name,
         signal_name, GVariantToString(parameters).get());
  if (strcmp(signal_name, "SettingChanged")) {
    NS_WARNING(
        nsPrintfCString("Unknown change signal for settings: %s", signal_name)
            .get());
    return;
  }
  RefPtr<GVariant> ns = dont_AddRef(g_variant_get_child_value(parameters, 0));
  RefPtr<GVariant> key = dont_AddRef(g_variant_get_child_value(parameters, 1));
  RefPtr<GVariant> value =
      dont_AddRef(g_variant_get_child_value(parameters, 2));
  // Third parameter is the value, but we don't care about it.
  if (!ns || !key || !value ||
      !g_variant_is_of_type(ns, G_VARIANT_TYPE_STRING) ||
      !g_variant_is_of_type(key, G_VARIANT_TYPE_STRING)) {
    MOZ_ASSERT(false, "Unexpected setting change signal parameters");
    return;
  }

  auto* lnf = static_cast<nsLookAndFeel*>(user_data);
  auto nsStr = GVariantGetString(ns);
  if (nsStr.Equals("org.freedesktop.appearance"_ns)) {
    UnboxVariant(value);
    auto keyStr = GVariantGetString(key);
    if (lnf->RecomputeDBusAppearanceSetting(keyStr, value)) {
      OnSettingsChange(lnf, NativeChangeKind::OtherSettings);
    }
  }

  if (nsStr.Equals("org.gnome.desktop.interface")) {
    UnboxVariant(value);
    auto keyStr = GVariantGetString(key);
    if (keyStr.Equals("gtk-theme")) {
      auto v = GVariantGetString(value);
      g_object_set(gtk_settings_get_default(), "gtk-theme-name", v.get(),
                   nullptr);
    }
  }
}

bool nsLookAndFeel::RecomputeDBusAppearanceSetting(const nsACString& aKey,
                                                   GVariant* aValue) {
  LOGLNF("RecomputeDBusAppearanceSetting(%s, %s)",
         PromiseFlatCString(aKey).get(), GVariantToString(aValue).get());
  if (aKey.EqualsLiteral("contrast")) {
    const bool old = mDBusSettings.mPrefersContrast;
    mDBusSettings.mPrefersContrast = g_variant_get_uint32(aValue) == 1;
    return mDBusSettings.mPrefersContrast != old;
  }
  if (aKey.EqualsLiteral("color-scheme")) {
    const auto old = mDBusSettings.mColorScheme;
    mDBusSettings.mColorScheme = [&] {
      switch (g_variant_get_uint32(aValue)) {
        default:
          MOZ_ASSERT_UNREACHABLE("Unexpected color-scheme query return value");
          break;
        case 1:
          return Some(ColorScheme::Dark);
        case 0:
        case 2:
          return Some(ColorScheme::Light);
      }
      return Maybe<ColorScheme>{};
    }();
    return mDBusSettings.mColorScheme != old;
  }
  if (aKey.EqualsLiteral("accent-color")) {
    auto old = mDBusSettings.mAccentColor;
    mDBusSettings.mAccentColor.mBg = mDBusSettings.mAccentColor.mFg =
        NS_TRANSPARENT;
    gdouble r = -1.0, g = -1.0, b = -1.0;
    g_variant_get(aValue, "(ddd)", &r, &g, &b);
    if (r >= 0.0f && g >= 0.0f && b >= 0.0f) {
      mDBusSettings.mAccentColor.mBg = gfx::sRGBColor(r, g, b, 1.0).ToABGR();
      mDBusSettings.mAccentColor.mFg =
          ThemeColors::ComputeCustomAccentForeground(
              mDBusSettings.mAccentColor.mBg);
    }
    return mDBusSettings.mAccentColor != old;
  }
  return false;
}

bool nsLookAndFeel::RecomputeDBusSettings() {
  if (!mDBusSettingsProxy) {
    return false;
  }

  GVariantBuilder namespacesBuilder;
  g_variant_builder_init(&namespacesBuilder, G_VARIANT_TYPE("as"));
  g_variant_builder_add(&namespacesBuilder, "s", "org.freedesktop.appearance");

  GUniquePtr<GError> error;
  nsAppShell::DBusConnectionCheck();
  RefPtr<GVariant> variant = dont_AddRef(g_dbus_proxy_call_sync(
      mDBusSettingsProxy, "ReadAll", g_variant_new("(as)", &namespacesBuilder),
      G_DBUS_CALL_FLAGS_NONE,
      StaticPrefs::widget_gtk_settings_portal_timeout_ms(), nullptr,
      getter_Transfers(error)));
  if (!variant) {
    LOGLNF("dbus settings query error: %s\n", error->message);
    return false;
  }

  LOGLNF("dbus settings query result: %s\n", GVariantToString(variant).get());
  variant = dont_AddRef(g_variant_get_child_value(variant, 0));
  UnboxVariant(variant);
  LOGLNF("dbus settings query result after unbox: %s\n",
         GVariantToString(variant).get());
  if (!variant || !g_variant_is_of_type(variant, G_VARIANT_TYPE_DICTIONARY)) {
    MOZ_ASSERT(false, "Unexpected dbus settings query return value");
    return false;
  }

  bool changed = false;
  // We expect one dictionary with (right now) one namespace for appearance,
  // with another dictionary inside for the actual values.
  {
    gchar* ns;
    GVariantIter outerIter;
    GVariantIter* innerIter;
    g_variant_iter_init(&outerIter, variant);
    while (g_variant_iter_loop(&outerIter, "{sa{sv}}", &ns, &innerIter)) {
      LOGLNF("Got namespace %s", ns);
      if (!strcmp(ns, "org.freedesktop.appearance")) {
        gchar* appearanceKey;
        GVariant* innerValue;
        while (g_variant_iter_loop(innerIter, "{sv}", &appearanceKey,
                                   &innerValue)) {
          LOGLNF(" > %s: %s", appearanceKey,
                 GVariantToString(innerValue).get());
          changed |= RecomputeDBusAppearanceSetting(
              nsDependentCString(appearanceKey), innerValue);
        }
      }
    }
  }
  return changed;
}

void nsLookAndFeel::WatchDBus() {
  LOGLNF("nsLookAndFeel::WatchDBus");
  GUniquePtr<GError> error;
  nsAppShell::DBusConnectionCheck();
  mDBusSettingsProxy = dont_AddRef(g_dbus_proxy_new_for_bus_sync(
      G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, nullptr,
      "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
      "org.freedesktop.portal.Settings", nullptr, getter_Transfers(error)));
  if (!mDBusSettingsProxy) {
    LOGLNF("Can't create DBus proxy for settings: %s\n", error->message);
    return;
  }

  g_signal_connect(mDBusSettingsProxy, "g-signal",
                   G_CALLBACK(settings_changed_signal_cb), this);

  // DBus interface was started after L&F init so we need to load our settings
  // from DBus explicitly.
  if (RecomputeDBusSettings()) {
    OnSettingsChange(this, NativeChangeKind::OtherSettings);
  }
}

void nsLookAndFeel::UnwatchDBus() {
  if (!mDBusSettingsProxy) {
    return;
  }
  LOGLNF("nsLookAndFeel::UnwatchDBus");
  g_signal_handlers_disconnect_by_func(
      mDBusSettingsProxy, FuncToGpointer(settings_changed_signal_cb), this);
  mDBusSettingsProxy = nullptr;
  nsAppShell::DBusConnectionCheck();
}
#endif

nsLookAndFeel::nsLookAndFeel() {
  static constexpr nsLiteralCString kObservedSettings[] = {
      // Affects system font sizes.
      "notify::gtk-xft-dpi"_ns,
      // Affects mSystemTheme and mAltTheme as expected.
      "notify::gtk-theme-name"_ns,
      // System fonts?
      "notify::gtk-font-name"_ns,
      // prefers-reduced-motion
      "notify::gtk-enable-animations"_ns,
      // CSD media queries, etc.
      "notify::gtk-decoration-layout"_ns,
      // Text resolution affects system font and widget sizes.
      "notify::resolution"_ns,
      // These three Affect mCaretBlinkTime
      "notify::gtk-cursor-blink"_ns,
      "notify::gtk-cursor-blink-time"_ns,
      "notify::gtk-cursor-blink-timeout"_ns,
      // Affects SelectTextfieldsOnKeyFocus
      "notify::gtk-entry-select-on-focus"_ns,
      // Affects ScrollToClick
      "notify::gtk-primary-button-warps-slider"_ns,
      // Affects SubmenuDelay
      "notify::gtk-menu-popup-delay"_ns,
      // Affects DragThresholdX/Y
      "notify::gtk-dnd-drag-threshold"_ns,
      // Affects titlebar actions loaded at GtkWidgets::Refresh().
      "notify::gtk-titlebar-double-click"_ns,
      "notify::gtk-titlebar-middle-click"_ns,
  };

  GtkSettings* settings = gtk_settings_get_default();
  if (MOZ_UNLIKELY(!settings)) {
    return;
  }

  for (const auto& setting : kObservedSettings) {
    g_signal_connect_after(settings, setting.get(),
                           G_CALLBACK(settings_changed_cb), nullptr);
  }

  sCSDAvailable =
      nsWindow::GetSystemGtkWindowDecoration() != nsWindow::GTK_DECORATION_NONE;

#ifdef MOZ_ENABLE_DBUS
  if (ShouldUsePortal(PortalKind::Settings)) {
    mDBusID = g_bus_watch_name(
        G_BUS_TYPE_SESSION, "org.freedesktop.portal.Desktop",
        G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
        [](GDBusConnection*, const gchar*, const gchar*,
           gpointer data) -> void {
          auto* lnf = static_cast<nsLookAndFeel*>(data);
          lnf->WatchDBus();
        },
        [](GDBusConnection*, const gchar*, gpointer data) -> void {
          auto* lnf = static_cast<nsLookAndFeel*>(data);
          lnf->UnwatchDBus();
        },
        this, nullptr);
  }
#endif
  if (IsKdeDesktopEnvironment()) {
    GUniquePtr<gchar> path(
        g_strconcat(g_get_user_config_dir(), "/gtk-3.0/colors.css", NULL));
    mKdeColors = dont_AddRef(g_file_new_for_path(path.get()));
    mKdeColorsMonitor = dont_AddRef(g_file_monitor_file(
        mKdeColors.get(), G_FILE_MONITOR_NONE, nullptr, nullptr));
    if (mKdeColorsMonitor) {
      g_signal_connect(mKdeColorsMonitor.get(), "changed",
                       G_CALLBACK(kde_colors_changed), NULL);
    }
  }

  FcInit();
}

nsLookAndFeel::~nsLookAndFeel() {
  ClearRoundedCornerProvider();
#ifdef MOZ_ENABLE_DBUS
  if (mDBusID) {
    g_bus_unwatch_name(mDBusID);
    mDBusID = 0;
  }
  UnwatchDBus();
#endif
  if (GtkSettings* settings = gtk_settings_get_default()) {
    g_signal_handlers_disconnect_by_func(
        settings, FuncToGpointer(settings_changed_cb), nullptr);
  }
}

#if 0
static void DumpStyleContext(GtkStyleContext* aStyle) {
  static auto sGtkStyleContextToString =
      reinterpret_cast<char* (*)(GtkStyleContext*, gint)>(
          dlsym(RTLD_DEFAULT, "gtk_style_context_to_string"));
  char* str = sGtkStyleContextToString(aStyle, ~0);
  printf("%s\n", str);
  g_free(str);
  str = gtk_widget_path_to_string(gtk_style_context_get_path(aStyle));
  printf("%s\n", str);
  g_free(str);
}
#endif

static gint GetBorderRadius(GtkStyleContext* aStyle) {
  GValue value = G_VALUE_INIT;
  // NOTE(emilio): In an ideal world, we'd query the two longhands
  // (border-top-left-radius and border-top-right-radius) separately. However,
  // that doesn't work (GTK rejects the query with:
  //
  //   Style property "border-top-left-radius" is not gettable
  //
  // However! Getting border-radius does work, and it does return the
  // border-top-left-radius as a gint:
  //
  //   https://docs.gtk.org/gtk3/const.STYLE_PROPERTY_BORDER_RADIUS.html
  //   https://gitlab.gnome.org/GNOME/gtk/-/blob/gtk-3-20/gtk/gtkcssshorthandpropertyimpl.c#L961-977
  //
  // So we abuse this fact, and make the assumption here that the
  // border-top-{left,right}-radius are the same, and roll with it.
  gtk_style_context_get_property(aStyle, "border-radius", GTK_STATE_FLAG_NORMAL,
                                 &value);
  gint result = 0;
  auto type = G_VALUE_TYPE(&value);
  if (type == G_TYPE_INT) {
    result = g_value_get_int(&value);
  } else {
    NS_WARNING(nsPrintfCString("Unknown value type %lu for border-radius", type)
                   .get());
  }
  g_value_unset(&value);
  return result;
}

static bool HasBackground(GtkStyleContext* aStyle) {
  GdkRGBA gdkColor;
  gtk_style_context_get_background_color(aStyle, GTK_STATE_FLAG_NORMAL,
                                         &gdkColor);
  if (gdkColor.alpha != 0.0) {
    return true;
  }

  GValue value = G_VALUE_INIT;
  gtk_style_context_get_property(aStyle, "background-image",
                                 GTK_STATE_FLAG_NORMAL, &value);
  auto cleanup = mozilla::MakeScopeExit([&] { g_value_unset(&value); });
  return g_value_get_boxed(&value);
}

// Modifies color |*aDest| as if a pattern of color |aSource| was painted with
// CAIRO_OPERATOR_OVER to a surface with color |*aDest|.
static void ApplyColorOver(const GdkRGBA& aSource, GdkRGBA* aDest) {
  gdouble sourceCoef = aSource.alpha;
  gdouble destCoef = aDest->alpha * (1.0 - sourceCoef);
  gdouble resultAlpha = sourceCoef + destCoef;
  if (resultAlpha != 0.0) {  // don't divide by zero
    destCoef /= resultAlpha;
    sourceCoef /= resultAlpha;
    aDest->red = sourceCoef * aSource.red + destCoef * aDest->red;
    aDest->green = sourceCoef * aSource.green + destCoef * aDest->green;
    aDest->blue = sourceCoef * aSource.blue + destCoef * aDest->blue;
    aDest->alpha = resultAlpha;
  }
}

static void GetLightAndDarkness(const GdkRGBA& aColor, double* aLightness,
                                double* aDarkness) {
  double sum = aColor.red + aColor.green + aColor.blue;
  *aLightness = sum * aColor.alpha;
  *aDarkness = (3.0 - sum) * aColor.alpha;
}

static bool GetGradientColors(const GValue* aValue, GdkRGBA* aLightColor,
                              GdkRGBA* aDarkColor) {
  if (!G_TYPE_CHECK_VALUE_TYPE(aValue, CAIRO_GOBJECT_TYPE_PATTERN)) {
    return false;
  }

  auto pattern = static_cast<cairo_pattern_t*>(g_value_get_boxed(aValue));
  if (!pattern) {
    return false;
  }

  // Just picking the lightest and darkest colors as simple samples rather
  // than trying to blend, which could get messy if there are many stops.
  if (CAIRO_STATUS_SUCCESS !=
      cairo_pattern_get_color_stop_rgba(pattern, 0, nullptr, &aDarkColor->red,
                                        &aDarkColor->green, &aDarkColor->blue,
                                        &aDarkColor->alpha)) {
    return false;
  }

  double maxLightness, maxDarkness;
  GetLightAndDarkness(*aDarkColor, &maxLightness, &maxDarkness);
  *aLightColor = *aDarkColor;

  GdkRGBA stop;
  for (int index = 1;
       CAIRO_STATUS_SUCCESS ==
       cairo_pattern_get_color_stop_rgba(pattern, index, nullptr, &stop.red,
                                         &stop.green, &stop.blue, &stop.alpha);
       ++index) {
    double lightness, darkness;
    GetLightAndDarkness(stop, &lightness, &darkness);
    if (lightness > maxLightness) {
      maxLightness = lightness;
      *aLightColor = stop;
    }
    if (darkness > maxDarkness) {
      maxDarkness = darkness;
      *aDarkColor = stop;
    }
  }

  return true;
}

static bool GetColorFromImagePattern(const GValue* aValue, nscolor* aColor) {
  if (!G_TYPE_CHECK_VALUE_TYPE(aValue, CAIRO_GOBJECT_TYPE_PATTERN)) {
    return false;
  }

  auto* pattern = static_cast<cairo_pattern_t*>(g_value_get_boxed(aValue));
  if (!pattern) {
    return false;
  }

  cairo_surface_t* surface;
  if (cairo_pattern_get_surface(pattern, &surface) != CAIRO_STATUS_SUCCESS) {
    return false;
  }

  cairo_format_t format = cairo_image_surface_get_format(surface);
  if (format == CAIRO_FORMAT_INVALID) {
    return false;
  }
  int width = cairo_image_surface_get_width(surface);
  int height = cairo_image_surface_get_height(surface);
  int stride = cairo_image_surface_get_stride(surface);
  if (!width || !height) {
    return false;
  }

  // Guesstimate the central pixel would have a sensible color.
  int x = width / 2;
  int y = height / 2;

  unsigned char* data = cairo_image_surface_get_data(surface);
  switch (format) {
    // Most (all?) GTK images / patterns / etc use ARGB32.
    case CAIRO_FORMAT_ARGB32: {
      size_t offset = x * 4 + y * stride;
      uint32_t* pixel = reinterpret_cast<uint32_t*>(data + offset);
      *aColor = gfx::sRGBColor::UnusualFromARGB(*pixel).ToABGR();
      return true;
    }
    default:
      break;
  }

  return false;
}

// Finds ideal cell highlight colors used for unfocused+selected cells distinct
// from both Highlight, used as focused+selected background, and the listbox
// background which is assumed to be similar to -moz-field
void nsLookAndFeel::PerThemeData::InitCellHighlightColors() {
  int32_t minLuminosityDifference = NS_SUFFICIENT_LUMINOSITY_DIFFERENCE_BG;
  int32_t backLuminosityDifference =
      NS_LUMINOSITY_DIFFERENCE(mWindow.mBg, mField.mBg);
  if (backLuminosityDifference >= minLuminosityDifference) {
    mCellHighlight = mWindow;
    return;
  }

  uint16_t hue, sat, luminance;
  uint8_t alpha;
  mCellHighlight = mField;

  NS_RGB2HSV(mCellHighlight.mBg, hue, sat, luminance, alpha);

  uint16_t step = 30;
  // Lighten the color if the color is very dark
  if (luminance <= step) {
    luminance += step;
  }
  // Darken it if it is very light
  else if (luminance >= 255 - step) {
    luminance -= step;
  }
  // Otherwise, compute what works best depending on the text luminance.
  else {
    uint16_t textHue, textSat, textLuminance;
    uint8_t textAlpha;
    NS_RGB2HSV(mCellHighlight.mFg, textHue, textSat, textLuminance, textAlpha);
    // Text is darker than background, use a lighter shade
    if (textLuminance < luminance) {
      luminance += step;
    }
    // Otherwise, use a darker shade
    else {
      luminance -= step;
    }
  }
  NS_HSV2RGB(mCellHighlight.mBg, hue, sat, luminance, alpha);
}

void nsLookAndFeel::NativeInit() { EnsureInit(); }

nsresult nsLookAndFeel::NativeGetColor(ColorID aID, ColorScheme aScheme,
                                       nscolor& aColor) {
  EnsureInit();

  const auto& theme =
      aScheme == ColorScheme::Light ? LightTheme() : DarkTheme();
  return theme.GetColor(aID, aColor);
}

static bool ShouldUseColorForActiveDarkScrollbarThumb(nscolor aColor) {
  auto IsDifferentEnough = [](int32_t aChannel, int32_t aOtherChannel) {
    return std::abs(aChannel - aOtherChannel) > 10;
  };
  return IsDifferentEnough(NS_GET_R(aColor), NS_GET_G(aColor)) ||
         IsDifferentEnough(NS_GET_R(aColor), NS_GET_B(aColor));
}

static bool ShouldUseThemedScrollbarColor(StyleSystemColor aID, nscolor aColor,
                                          bool aIsDark) {
  if (!StaticPrefs::widget_gtk_theme_scrollbar_colors_enabled()) {
    return false;
  }
  if (!aIsDark) {
    return true;
  }
  if (StaticPrefs::widget_non_native_theme_scrollbar_dark_themed()) {
    return true;
  }
  return aID == StyleSystemColor::ThemedScrollbarThumbActive &&
         StaticPrefs::widget_non_native_theme_scrollbar_active_always_themed();
}

nsresult nsLookAndFeel::PerThemeData::GetColor(ColorID aID,
                                               nscolor& aColor) const {
  nsresult res = NS_OK;

  switch (aID) {
      // These colors don't seem to be used for anything anymore in Mozilla
      // The CSS2 colors below are used.
    case ColorID::Appworkspace:  // MDI background color
    case ColorID::Background:    // desktop background
    case ColorID::Window:
    case ColorID::Windowframe:
    case ColorID::MozCombobox:
      aColor = mWindow.mBg;
      break;
    case ColorID::MozComboboxtext:
    case ColorID::Windowtext:
      aColor = mWindow.mFg;
      break;
    case ColorID::MozDialog:
      aColor = mDialog.mBg;
      break;
    case ColorID::MozDialogtext:
      aColor = mDialog.mFg;
      break;
    case ColorID::IMESelectedRawTextBackground:
    case ColorID::IMESelectedConvertedTextBackground:
    case ColorID::Highlight:  // preference selected item,
      aColor = mSelectedText.mBg;
      break;
    case ColorID::Highlighttext:
      if (NS_GET_A(mSelectedText.mBg) < 155) {
        aColor = NS_SAME_AS_FOREGROUND_COLOR;
        break;
      }
      [[fallthrough]];
    case ColorID::IMESelectedRawTextForeground:
    case ColorID::IMESelectedConvertedTextForeground:
      aColor = mSelectedText.mFg;
      break;
    case ColorID::Selecteditem:
      aColor = mSelectedItem.mBg;
      break;
    case ColorID::Selecteditemtext:
      aColor = mSelectedItem.mFg;
      break;
    case ColorID::Accentcolor:
      aColor = mAccent.mBg;
      break;
    case ColorID::Accentcolortext:
      aColor = mAccent.mFg;
      break;
    case ColorID::MozCellhighlight:
      aColor = mCellHighlight.mBg;
      break;
    case ColorID::MozCellhighlighttext:
      aColor = mCellHighlight.mFg;
      break;
    case ColorID::IMERawInputBackground:
    case ColorID::IMEConvertedTextBackground:
      aColor = NS_TRANSPARENT;
      break;
    case ColorID::IMERawInputForeground:
    case ColorID::IMEConvertedTextForeground:
      aColor = NS_SAME_AS_FOREGROUND_COLOR;
      break;
    case ColorID::IMERawInputUnderline:
    case ColorID::IMEConvertedTextUnderline:
      aColor = NS_SAME_AS_FOREGROUND_COLOR;
      break;
    case ColorID::IMESelectedRawTextUnderline:
    case ColorID::IMESelectedConvertedTextUnderline:
      aColor = NS_TRANSPARENT;
      break;
    case ColorID::Scrollbar:
      aColor = mThemedScrollbar;
      break;
    case ColorID::ThemedScrollbar:
      aColor = mThemedScrollbar;
      if (!ShouldUseThemedScrollbarColor(aID, aColor, mIsDark)) {
        return NS_ERROR_FAILURE;
      }
      break;
    case ColorID::ThemedScrollbarThumb:
      aColor = mThemedScrollbarThumb;
      if (!ShouldUseThemedScrollbarColor(aID, aColor, mIsDark)) {
        return NS_ERROR_FAILURE;
      }
      break;
    case ColorID::ThemedScrollbarThumbHover:
      aColor = mThemedScrollbarThumbHover;
      if (!ShouldUseThemedScrollbarColor(aID, aColor, mIsDark)) {
        return NS_ERROR_FAILURE;
      }
      break;
    case ColorID::ThemedScrollbarThumbActive:
      aColor = mThemedScrollbarThumbActive;
      if (!ShouldUseThemedScrollbarColor(aID, aColor, mIsDark)) {
        return NS_ERROR_FAILURE;
      }
      break;

      // css2  http://www.w3.org/TR/REC-CSS2/ui.html#system-colors
    case ColorID::Activeborder:
      // active window border
      aColor = mMozWindowActiveBorder;
      break;
    case ColorID::Inactiveborder:
      // inactive window border
      aColor = mMozWindowInactiveBorder;
      break;
    case ColorID::Graytext:  // disabled text in windows, menus, etc.
      aColor = mGrayText;
      break;
    case ColorID::Activecaption:
      aColor = mTitlebar.mBg;
      break;
    case ColorID::Captiontext:  // text in active window caption (titlebar)
      aColor = mTitlebar.mFg;
      break;
    case ColorID::Inactivecaption:
      // inactive window caption
      aColor = mTitlebarInactive.mBg;
      break;
    case ColorID::Inactivecaptiontext:
      aColor = mTitlebarInactive.mFg;
      break;
    case ColorID::Infobackground:
      aColor = mInfo.mBg;
      break;
    case ColorID::Infotext:
      aColor = mInfo.mFg;
      break;
    case ColorID::Menu:
      aColor = mMenu.mBg;
      break;
    case ColorID::Menutext:
      aColor = mMenu.mFg;
      break;
    case ColorID::MozHeaderbar:
      aColor = mHeaderBar.mBg;
      break;
    case ColorID::MozHeaderbartext:
      aColor = mHeaderBar.mFg;
      break;
    case ColorID::MozHeaderbarinactive:
      aColor = mHeaderBarInactive.mBg;
      break;
    case ColorID::MozHeaderbarinactivetext:
      aColor = mHeaderBarInactive.mFg;
      break;
    case ColorID::Threedface:
      // 3-D face color
      aColor = mWindow.mBg;
      break;

    case ColorID::Buttonhighlight:
    case ColorID::Buttonshadow:
    case ColorID::Threedhighlight:
    case ColorID::Threedshadow:
      // 3-D highlighted outer edge color
      aColor = mFrameBorder;
      break;

#define HANDLE_BUTTON(Prefix, member_) \
  case ColorID::Prefix##border: {      \
    aColor = (member_).mBorder;        \
    break;                             \
  }                                    \
  case ColorID::Prefix##face: {        \
    aColor = (member_).mBg;            \
    break;                             \
  }                                    \
  case ColorID::Prefix##text: {        \
    aColor = (member_).mFg;            \
    break;                             \
  }

      HANDLE_BUTTON(Button, mButton);
      HANDLE_BUTTON(MozButtonhover, mButtonHover);
      HANDLE_BUTTON(MozButtonactive, mButtonActive);
    case ColorID::MozButtondisabledface:
      aColor = mButtonDisabled.mBg;
      break;
    case ColorID::MozButtondisabledborder:
      aColor = mButtonDisabled.mBorder;
      break;

    case ColorID::Threedlightshadow:
    case ColorID::MozDisabledfield:
      aColor = mIsDark ? *GenericDarkColor(aID) : NS_RGB(0xE0, 0xE0, 0xE0);
      break;
    case ColorID::Threeddarkshadow:
      aColor = mIsDark ? *GenericDarkColor(aID) : NS_RGB(0xDC, 0xDC, 0xDC);
      break;

    case ColorID::Field:
      aColor = mField.mBg;
      break;
    case ColorID::Fieldtext:
      aColor = mField.mFg;
      break;
    case ColorID::MozSidebar:
      aColor = mSidebar.mBg;
      break;
    case ColorID::MozSidebartext:
      aColor = mSidebar.mFg;
      break;
    case ColorID::MozSidebarborder:
      aColor = mSidebarBorder;
      break;
    case ColorID::MozMenuhover:
      aColor = mMenuHover.mBg;
      break;
    case ColorID::MozMenuhovertext:
      aColor = mMenuHover.mFg;
      break;
    case ColorID::MozOddtreerow:
    case ColorID::MozMenuhoverdisabled:
      aColor = NS_TRANSPARENT;
      break;
    case ColorID::Linktext:
      aColor = mNativeHyperLinkText;
      break;
    case ColorID::Visitedtext:
      aColor = mNativeVisitedHyperLinkText;
      break;
    case ColorID::MozColheader:
      aColor = mMozColHeader.mBg;
      break;
    case ColorID::MozColheadertext:
      aColor = mMozColHeader.mFg;
      break;
    case ColorID::MozColheaderhover:
      aColor = mMozColHeaderHover.mBg;
      break;
    case ColorID::MozColheaderhovertext:
      aColor = mMozColHeaderHover.mFg;
      break;
    case ColorID::MozColheaderactive:
      aColor = mMozColHeaderActive.mBg;
      break;
    case ColorID::MozColheaderactivetext:
      aColor = mMozColHeaderActive.mFg;
      break;
    case ColorID::Activetext:
    case ColorID::SpellCheckerUnderline:
    case ColorID::Mark:
    case ColorID::Marktext:
    case ColorID::MozAutofillBackground:
    case ColorID::TargetTextBackground:
    case ColorID::TargetTextForeground:
      aColor = GetStandinForNativeColor(
          aID, mIsDark ? ColorScheme::Dark : ColorScheme::Light);
      break;
    default:
      /* default color is BLACK */
      aColor = 0;
      res = NS_ERROR_FAILURE;
      break;
  }

  return res;
}

static int32_t CheckWidgetStyle(GtkWidget* aWidget, const char* aStyle,
                                int32_t aResult) {
  gboolean value = FALSE;
  gtk_widget_style_get(aWidget, aStyle, &value, nullptr);
  return value ? aResult : 0;
}

static int32_t ConvertGTKStepperStyleToMozillaScrollArrowStyle(
    GtkWidget* aWidget) {
  if (!aWidget) {
    return mozilla::LookAndFeel::eScrollArrowStyle_Single;
  }
  return CheckWidgetStyle(aWidget, "has-backward-stepper",
                          mozilla::LookAndFeel::eScrollArrow_StartBackward) |
         CheckWidgetStyle(aWidget, "has-forward-stepper",
                          mozilla::LookAndFeel::eScrollArrow_EndForward) |
         CheckWidgetStyle(aWidget, "has-secondary-backward-stepper",
                          mozilla::LookAndFeel::eScrollArrow_EndBackward) |
         CheckWidgetStyle(aWidget, "has-secondary-forward-stepper",
                          mozilla::LookAndFeel::eScrollArrow_StartForward);
}

nsresult nsLookAndFeel::NativeGetInt(IntID aID, int32_t& aResult) {
  nsresult res = NS_OK;

  // We use delayed initialization by EnsureInit() here
  // to make sure mozilla::Preferences is available (Bug 115807).
  // IntID::UseAccessibilityTheme is requested before user preferences
  // are read, and so EnsureInit(), which depends on preference values,
  // is deliberately delayed until required.
  switch (aID) {
    case IntID::ScrollButtonLeftMouseButtonAction:
      aResult = 0;
      break;
    case IntID::ScrollButtonMiddleMouseButtonAction:
      aResult = 1;
      break;
    case IntID::ScrollButtonRightMouseButtonAction:
      aResult = 2;
      break;
    case IntID::CaretBlinkTime:
      EnsureInit();
      aResult = mCaretBlinkTime;
      break;
    case IntID::CaretBlinkCount:
      EnsureInit();
      aResult = mCaretBlinkCount;
      break;
    case IntID::CaretWidth:
      aResult = 1;
      break;
    case IntID::SelectTextfieldsOnKeyFocus: {
      GtkSettings* settings = gtk_settings_get_default();
      gboolean select_on_focus = FALSE;
      if (MOZ_LIKELY(settings)) {
        g_object_get(settings, "gtk-entry-select-on-focus", &select_on_focus,
                     nullptr);
      }
      aResult = select_on_focus;
    } break;
    case IntID::ScrollToClick: {
      GtkSettings* settings = gtk_settings_get_default();
      gboolean warps_slider = FALSE;
      if (MOZ_LIKELY(settings) &&
          g_object_class_find_property(G_OBJECT_GET_CLASS(settings),
                                       "gtk-primary-button-warps-slider")) {
        g_object_get(settings, "gtk-primary-button-warps-slider", &warps_slider,
                     nullptr);
      }
      aResult = warps_slider;
    } break;
    case IntID::SubmenuDelay: {
      GtkSettings* settings = gtk_settings_get_default();
      gint delay = 0;
      if (MOZ_LIKELY(settings)) {
        g_object_get(settings, "gtk-menu-popup-delay", &delay, nullptr);
      }
      aResult = int32_t(delay);
      break;
    }
    case IntID::MenusCanOverlapOSBar:
      aResult = 0;
      break;
    case IntID::SkipNavigatingDisabledMenuItem:
      aResult = 1;
      break;
    case IntID::DragThresholdX:
    case IntID::DragThresholdY: {
      gint threshold = 0;
      GtkSettings* settings = gtk_settings_get_default();
      if (MOZ_LIKELY(settings)) {
        g_object_get(settings, "gtk-dnd-drag-threshold", &threshold, nullptr);
      }
      aResult = threshold;
    } break;
    case IntID::ScrollArrowStyle: {
      aResult = eScrollArrowStyle_Single;
      if (MOZ_LIKELY(gtk_settings_get_default())) {
        GtkWidget* scrollbar = GtkWidgets::Get(GtkWidgets::Type::Scrollbar);
        aResult = ConvertGTKStepperStyleToMozillaScrollArrowStyle(scrollbar);
      }
      break;
    }
    case IntID::TreeOpenDelay:
      aResult = 1000;
      break;
    case IntID::TreeCloseDelay:
      aResult = 1000;
      break;
    case IntID::TreeLazyScrollDelay:
      aResult = 150;
      break;
    case IntID::TreeScrollDelay:
      aResult = 100;
      break;
    case IntID::TreeScrollLinesMax:
      aResult = 3;
      break;
    case IntID::AlertNotificationOrigin:
      aResult = NS_ALERT_TOP;
      break;
    case IntID::IMERawInputUnderlineStyle:
    case IntID::IMEConvertedTextUnderlineStyle:
      aResult = static_cast<int32_t>(StyleTextDecorationStyle::Solid);
      break;
    case IntID::IMESelectedRawTextUnderlineStyle:
    case IntID::IMESelectedConvertedTextUnderline:
      aResult = static_cast<int32_t>(StyleTextDecorationStyle::None);
      break;
    case IntID::SpellCheckerUnderlineStyle:
      aResult = static_cast<int32_t>(StyleTextDecorationStyle::Wavy);
      break;
    case IntID::MenuBarDrag:
      EnsureInit();
      aResult = mSystemTheme.mMenuSupportsDrag;
      break;
    case IntID::ScrollbarButtonAutoRepeatBehavior:
      aResult = 1;
      break;
    case IntID::SwipeAnimationEnabled:
      aResult = 1;
      break;
    case IntID::ContextMenuOffsetVertical:
    case IntID::ContextMenuOffsetHorizontal:
      aResult = 2;
      break;
    case IntID::GTKCSDAvailable:
      aResult = sCSDAvailable;
      break;
    case IntID::GTKCSDTransparencyAvailable: {
      auto* screen = gdk_screen_get_default();
      aResult = MOZ_LIKELY(screen) && gdk_screen_get_rgba_visual(screen) &&
                gdk_screen_is_composited(screen);
      break;
    }
    case IntID::GTKCSDMaximizeButton:
      EnsureInit();
      aResult = mCSDMaximizeButton;
      break;
    case IntID::GTKCSDMinimizeButton:
      EnsureInit();
      aResult = mCSDMinimizeButton;
      break;
    case IntID::GTKCSDCloseButton:
      EnsureInit();
      aResult = mCSDCloseButton;
      break;
    case IntID::GTKCSDReversedPlacement:
      EnsureInit();
      aResult = mCSDReversedPlacement;
      break;
    case IntID::PrefersReducedMotion: {
      EnsureInit();
      aResult = mPrefersReducedMotion;
      break;
    }
    case IntID::SystemUsesDarkTheme: {
      EnsureInit();
      if (mColorSchemePreference) {
        aResult = *mColorSchemePreference == ColorScheme::Dark;
      } else {
        aResult = mSystemTheme.mIsDark;
      }
      break;
    }
    case IntID::GTKCSDMaximizeButtonPosition:
      aResult = mCSDMaximizeButtonPosition;
      break;
    case IntID::GTKCSDMinimizeButtonPosition:
      aResult = mCSDMinimizeButtonPosition;
      break;
    case IntID::GTKCSDCloseButtonPosition:
      aResult = mCSDCloseButtonPosition;
      break;
    case IntID::GTKThemeFamily: {
      EnsureInit();
      aResult = int32_t(mSystemTheme.mFamily);
      break;
    }
    case IntID::UseAccessibilityTheme:
    // If high contrast is enabled, enable prefers-reduced-transparency media
    // query as well as there is no dedicated option.
    case IntID::PrefersReducedTransparency:
      EnsureInit();
      aResult = mDBusSettings.mPrefersContrast || mSystemTheme.mHighContrast;
      break;
    case IntID::InvertedColors:
      // No GTK API for checking if inverted colors is enabled
      aResult = 0;
      break;
    case IntID::TooltipRadius: {
      EnsureInit();
      aResult = EffectiveTheme().mTooltipRadius;
      break;
    }
    case IntID::TitlebarRadius: {
      EnsureInit();
      aResult = EffectiveTheme().mTitlebarRadius;
      break;
    }
    case IntID::AllowOverlayScrollbarsOverlap: {
      aResult = 1;
      break;
    }
    case IntID::ScrollbarFadeBeginDelay: {
      aResult = 1000;
      break;
    }
    case IntID::ScrollbarFadeDuration: {
      aResult = 400;
      break;
    }
    case IntID::ScrollbarDisplayOnMouseMove: {
      aResult = 1;
      break;
    }
    case IntID::PanelAnimations:
      aResult = [&]() -> bool {
        if (!sCSDAvailable) {
          // Disabled on systems without CSD, see bug 1385079.
          return false;
        }
        if (GdkIsWaylandDisplay()) {
          // Disabled on wayland, see bug 1800442 and bug 1800368.
          return false;
        }
        return true;
      }();
      break;
    case IntID::UseOverlayScrollbars: {
      aResult = StaticPrefs::widget_gtk_overlay_scrollbars_enabled();
      break;
    }
    case IntID::HideCursorWhileTyping: {
      aResult = StaticPrefs::widget_gtk_hide_pointer_while_typing_enabled();
      break;
    }
    case IntID::TouchDeviceSupportPresent:
      aResult = widget::WidgetUtilsGTK::IsTouchDeviceSupportPresent();
      break;
    case IntID::NativeMenubar:
      aResult = []() {
        if (!StaticPrefs::widget_gtk_global_menu_enabled()) {
          return false;
        }
#ifdef MOZ_WAYLAND
        if (GdkIsWaylandDisplay()) {
          return StaticPrefs::widget_gtk_global_menu_wayland_enabled() &&
                 !!WaylandDisplayGet()->GetAppMenuManager();
        }
#endif
        // TODO: Maybe detect whether we can register the window or something?
        // Though the X11 code just hides the native menubar without
        // communicating it to the front-end...
        return false;
      }();
      break;
    case IntID::HourCycle:
      aResult = []() {
        if (MOZ_UNLIKELY(!gtk_settings_get_default())) {
          // Avoid accessing gsettings early on startup on xpcshell, in order to
          // avoid messing with tests that try to mock our session bus.
          // FIXME(bug 1981011): Ideally we would be able to not have this
          // special case.
          return 0;
        }

        nsAutoCString result;
        widget::GSettings::GetString("org.gnome.desktop.interface"_ns,
                                     "clock-format"_ns, result);
        if (result == "12h") {
          return 12;
        }
        if (result == "24h") {
          return 24;
        }
        return 0;
      }();
      break;
    default:
      aResult = 0;
      res = NS_ERROR_FAILURE;
  }

  return res;
}

nsresult nsLookAndFeel::NativeGetFloat(FloatID aID, float& aResult) {
  nsresult rv = NS_OK;
  switch (aID) {
    case FloatID::IMEUnderlineRelativeSize:
      aResult = 1.0f;
      break;
    case FloatID::SpellCheckerUnderlineRelativeSize:
      aResult = 1.0f;
      break;
    case FloatID::CaretAspectRatio:
      EnsureInit();
      aResult = mSystemTheme.mCaretRatio;
      break;
    case FloatID::TextScaleFactor:
      aResult = mTextScaleFactor;
      break;
    default:
      aResult = -1.0;
      rv = NS_ERROR_FAILURE;
  }
  return rv;
}

static void GetSystemFontInfo(GtkStyleContext* aStyle, nsString* aFontName,
                              gfxFontStyle* aFontStyle) {
  aFontStyle->style = FontSlantStyle::NORMAL;

  // As in
  // https://git.gnome.org/browse/gtk+/tree/gtk/gtkwidget.c?h=3.22.19#n10333
  PangoFontDescription* desc;
  gtk_style_context_get(aStyle, gtk_style_context_get_state(aStyle), "font",
                        &desc, nullptr);

  aFontStyle->systemFont = true;

  constexpr auto quote = u"\""_ns;
  NS_ConvertUTF8toUTF16 family(pango_font_description_get_family(desc));
  *aFontName = quote + family + quote;

  aFontStyle->weight =
      FontWeight::FromInt(pango_font_description_get_weight(desc));

  // FIXME: Set aFontStyle->width correctly!
  aFontStyle->width = FontWidth::NORMAL;

  float size = float(pango_font_description_get_size(desc)) / PANGO_SCALE;

  // |size| is now either pixels or pango-points, convert to scale-independent
  // pixels.
  if (pango_font_description_get_size_is_absolute(desc)) {
    // Undo the already-applied font scale.
    size /= GetGtkTextScaleFactor();
  } else {
    // |size| is in pango-points, so convert to pixels.
    size *= 96 / POINTS_PER_INCH_FLOAT;
  }

  // |size| is now pixels but not scaled for the hidpi displays,
  aFontStyle->size = size;

  pango_font_description_free(desc);
}

bool nsLookAndFeel::NativeGetFont(FontID aID, nsString& aFontName,
                                  gfxFontStyle& aFontStyle) {
  return mSystemTheme.GetFont(aID, aFontName, aFontStyle, mTextScaleFactor);
}

bool nsLookAndFeel::PerThemeData::GetFont(FontID aID, nsString& aFontName,
                                          gfxFontStyle& aFontStyle,
                                          float aTextScaleFactor) const {
  switch (aID) {
    case FontID::Menu:             // css2
    case FontID::MozPullDownMenu:  // css3
      aFontName = mMenuFontName;
      aFontStyle = mMenuFontStyle;
      break;

    case FontID::MozField:  // css3
    case FontID::MozList:   // css3
      aFontName = mFieldFontName;
      aFontStyle = mFieldFontStyle;
      break;

    case FontID::MozButton:  // css3
      aFontName = mButtonFontName;
      aFontStyle = mButtonFontStyle;
      break;

    case FontID::Caption:       // css2
    case FontID::Icon:          // css2
    case FontID::MessageBox:    // css2
    case FontID::SmallCaption:  // css2
    case FontID::StatusBar:     // css2
    default:
      aFontName = mDefaultFontName;
      aFontStyle = mDefaultFontStyle;
      break;
  }
  aFontStyle.systemFont = true;

  // Convert GDK pixels to CSS pixels.
  // Note that this is generally a no-op, except when text scale factor is
  // overridden by pref.
  aFontStyle.size *= aTextScaleFactor / LookAndFeel::GetTextScaleFactor();
  return true;
}

static nsCString GetGtkSettingsStringKey(const char* aKey) {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
  nsCString ret;
  GtkSettings* settings = gtk_settings_get_default();
  char* value = nullptr;
  g_object_get(settings, aKey, &value, nullptr);
  if (value) {
    ret.Assign(value);
    g_free(value);
  }
  return ret;
}

static nsCString GetGtkTheme() {
  auto theme = GetGtkSettingsStringKey("gtk-theme-name");
  if (theme.IsEmpty()) {
    theme.AssignLiteral("Adwaita");
  }
  return theme;
}

static bool GetPreferDarkTheme() {
  GtkSettings* settings = gtk_settings_get_default();
  gboolean preferDarkTheme = FALSE;
  g_object_get(settings, "gtk-application-prefer-dark-theme", &preferDarkTheme,
               nullptr);
  return preferDarkTheme == TRUE;
}

// It seems GTK doesn't have an API to query if the current theme is "light" or
// "dark", so we synthesize it from the CSS2 Window/WindowText colors instead,
// by comparing their luminosity.
static bool GetThemeIsDark() {
  GdkRGBA bg, fg;
  GtkStyleContext* style = GtkWidgets::GetStyle(GtkWidgets::Type::Window);
  gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL, &bg);
  gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &fg);
  return RelativeLuminanceUtils::Compute(GDK_RGBA_TO_NS_RGBA(bg)) <
         RelativeLuminanceUtils::Compute(GDK_RGBA_TO_NS_RGBA(fg));
}

void nsLookAndFeel::RestoreSystemTheme() {
  LOGLNF("RestoreSystemTheme(%s, %d, %d)\n", mSystemTheme.mName.get(),
         mSystemTheme.mPreferDarkTheme, mSystemThemeOverridden);

  if (!mSystemThemeOverridden) {
    return;
  }

  // Available on Gtk 3.20+.
  static auto sGtkSettingsResetProperty =
      (void (*)(GtkSettings*, const gchar*))dlsym(
          RTLD_DEFAULT, "gtk_settings_reset_property");

  GtkSettings* settings = gtk_settings_get_default();
  if (sGtkSettingsResetProperty) {
    sGtkSettingsResetProperty(settings, "gtk-theme-name");
    sGtkSettingsResetProperty(settings, "gtk-application-prefer-dark-theme");
  } else {
    g_object_set(settings, "gtk-theme-name", mSystemTheme.mName.get(),
                 "gtk-application-prefer-dark-theme",
                 mSystemTheme.mPreferDarkTheme, nullptr);
  }
  mSystemThemeOverridden = false;
  GtkWidgets::Refresh();
}

static bool AnyColorChannelIsDifferent(nscolor aColor) {
  return NS_GET_R(aColor) != NS_GET_G(aColor) ||
         NS_GET_R(aColor) != NS_GET_B(aColor);
}

bool nsLookAndFeel::ConfigureAltTheme() {
  GtkSettings* settings = gtk_settings_get_default();
  // Toggling gtk-application-prefer-dark-theme is not enough generally to
  // switch from dark to light theme.  If the theme didn't change, and we have
  // a dark theme, try to first remove -Dark{,er,est} from the theme name to
  // find the light variant.
  if (mSystemTheme.mIsDark) {
    nsCString potentialLightThemeName = mSystemTheme.mName;
    // clang-format off
    constexpr nsLiteralCString kSubstringsToRemove[] = {
        "-darkest"_ns, "-darker"_ns, "-dark"_ns,
        "-Darkest"_ns, "-Darker"_ns, "-Dark"_ns,
        "_darkest"_ns, "_darker"_ns, "_dark"_ns,
        "_Darkest"_ns, "_Darker"_ns, "_Dark"_ns,
    };
    // clang-format on
    bool found = false;
    for (const auto& s : kSubstringsToRemove) {
      potentialLightThemeName = mSystemTheme.mName;
      potentialLightThemeName.ReplaceSubstring(s, ""_ns);
      if (potentialLightThemeName.Length() != mSystemTheme.mName.Length()) {
        found = true;
        break;
      }
    }
    if (found) {
      LOGLNF("    found potential light variant of %s: %s",
             mSystemTheme.mName.get(), potentialLightThemeName.get());
      g_object_set(settings, "gtk-theme-name", potentialLightThemeName.get(),
                   "gtk-application-prefer-dark-theme", !mSystemTheme.mIsDark,
                   nullptr);
      GtkWidgets::Refresh();

      if (!GetThemeIsDark()) {
        return true;  // Success!
      }
    }
  }

  LOGLNF("    toggling gtk-application-prefer-dark-theme");
  g_object_set(settings, "gtk-application-prefer-dark-theme",
               !mSystemTheme.mIsDark, nullptr);
  GtkWidgets::Refresh();
  if (mSystemTheme.mIsDark != GetThemeIsDark()) {
    return true;  // Success!
  }

  LOGLNF("    didn't work, falling back to default theme");
  // If the theme still didn't change enough, fall back to Adwaita with the
  // appropriate color preference.
  g_object_set(settings, "gtk-theme-name", "Adwaita",
               "gtk-application-prefer-dark-theme", !mSystemTheme.mIsDark,
               nullptr);
  GtkWidgets::Refresh();

  // If it _still_ didn't change enough, and we're looking for a dark theme,
  // try to set Adwaita-dark as a theme name. This might be needed in older GTK
  // versions.
  if (!mSystemTheme.mIsDark && !GetThemeIsDark()) {
    LOGLNF("    last resort Adwaita-dark fallback");
    g_object_set(settings, "gtk-theme-name", "Adwaita-dark", nullptr);
    GtkWidgets::Refresh();
  }

  return false;
}

void nsLookAndFeel::PerThemeData::RestoreColorOverrides() {
  for (auto& override : Reversed(mOverrides)) {
    *reinterpret_cast<nscolor*>(reinterpret_cast<uint8_t*>(this) +
                                override.mByteOffset) = override.mOriginalColor;
  }
  mOverrides.Clear();
}

void nsLookAndFeel::PerThemeData::ApplyColorOverride(nscolor* aMember,
                                                     nscolor aNewColor) {
  auto offset =
      reinterpret_cast<uintptr_t>(aMember) - reinterpret_cast<uintptr_t>(this);
  MOZ_ASSERT(offset < sizeof(*this));
  mOverrides.AppendElement(ColorOverride{uint32_t(offset), *aMember});
  *aMember = aNewColor;
}

void nsLookAndFeel::PerThemeData::ApplyColorOverride(
    ColorPair* aMember, const ColorPair& aNewPair) {
  ApplyColorOverride(&aMember->mBg, aNewPair.mBg);
  ApplyColorOverride(&aMember->mFg, aNewPair.mFg);
}

// We override some adwaita colors from GTK3 to LibAdwaita, see:
// https://gnome.pages.gitlab.gnome.org/libadwaita/doc/1.7/css-variables.html
// https://gitlab.gnome.org/GNOME/libadwaita/-/blob/690c0a70315c74b95b2cb5fa29622370b3195b0d/src/stylesheet/_defaults.scss
void nsLookAndFeel::MaybeApplyColorOverrides() {
  auto& dark = mSystemTheme.mIsDark ? mSystemTheme : mAltTheme;
  auto& light = mSystemTheme.mIsDark ? mAltTheme : mSystemTheme;

  dark.RestoreColorOverrides();
  light.RestoreColorOverrides();

  auto MaybeApplyDbusOrAdwaitaAccentColor =
      [](PerThemeData& aTheme, const DBusSettings& aDBusSettings) {
        if (aTheme.mFamily != ThemeFamily::Adwaita) {
          return;
        }
        if (aDBusSettings.HasAccentColor()) {
          aTheme.ApplyColorOverride(&aTheme.mAccent,
                                    aDBusSettings.mAccentColor);
          aTheme.ApplyColorOverride(&aTheme.mSelectedItem,
                                    aDBusSettings.mAccentColor);
          aTheme.ApplyColorOverride(&aTheme.mMenuHover,
                                    aDBusSettings.mAccentColor);
          aTheme.ApplyColorOverride(&aTheme.mNativeHyperLinkText,
                                    aDBusSettings.mAccentColor.mBg);
          aTheme.ApplyColorOverride(&aTheme.mNativeVisitedHyperLinkText,
                                    aDBusSettings.mAccentColor.mBg);
        } else {
          aTheme.ApplyColorOverride(
              &aTheme.mAccent,
              {NS_RGB(0x35, 0x84, 0xe4), NS_RGB(0xff, 0xff, 0xff)});
        }
        aTheme.ApplyColorOverride(&aTheme.mSelectedText, aTheme.mAccent);
      };

  MaybeApplyDbusOrAdwaitaAccentColor(dark, mDBusSettings);
  MaybeApplyDbusOrAdwaitaAccentColor(light, mDBusSettings);

  if (StaticPrefs::widget_gtk_libadwaita_colors_enabled()) {
    // https://gitlab.gnome.org/GNOME/libadwaita/-/blob/main/src/stylesheet/widgets/_buttons.scss
    // (which is somewhat confusingly also reused for fields).
    auto ApplyLibadwaitaButtonColors = [](PerThemeData& aTheme) {
      // TODO: Technically adwaita doesn't have borders, but we apply this
      // border to checkboxes and textfields as well, so for now let it be.
      // aTheme.mButton.mBorder = aTheme.mButtonHover.mBorder =
      //     aTheme.mButtonActive.mBorder = NS_TRANSPARENT;
      aTheme.ApplyColorOverride(&aTheme.mField.mFg, aTheme.mWindow.mFg);
      aTheme.ApplyColorOverride(&aTheme.mButton.mFg, aTheme.mWindow.mFg);
      aTheme.ApplyColorOverride(&aTheme.mButtonHover.mFg, aTheme.mWindow.mFg);
      aTheme.ApplyColorOverride(&aTheme.mButtonActive.mFg, aTheme.mWindow.mFg);
      // Window background combined with 10%, 15% and 30% of the foreground
      // color, respectively.
      const nscolor buttonBg = NS_ComposeColors(
          aTheme.mWindow.mBg,
          NS_RGBA(NS_GET_R(aTheme.mWindow.mFg), NS_GET_G(aTheme.mWindow.mFg),
                  NS_GET_B(aTheme.mWindow.mFg), 26));
      aTheme.ApplyColorOverride(&aTheme.mButton.mBg, buttonBg);
      aTheme.ApplyColorOverride(&aTheme.mField.mBg, buttonBg);
      aTheme.ApplyColorOverride(
          &aTheme.mButtonHover.mBg,
          NS_ComposeColors(aTheme.mWindow.mBg,
                           NS_RGBA(NS_GET_R(aTheme.mWindow.mFg),
                                   NS_GET_G(aTheme.mWindow.mFg),
                                   NS_GET_B(aTheme.mWindow.mFg), 39)));
      aTheme.ApplyColorOverride(
          &aTheme.mButtonActive.mBg,
          NS_ComposeColors(aTheme.mWindow.mBg,
                           NS_RGBA(NS_GET_R(aTheme.mWindow.mFg),
                                   NS_GET_G(aTheme.mWindow.mFg),
                                   NS_GET_B(aTheme.mWindow.mFg), 77)));
    };

    if (light.mFamily == ThemeFamily::Adwaita) {
      // #323232 is rgba(0,0,0,.8) over #fafafa.
      light.ApplyColorOverride(&light.mWindow.mBg, NS_RGB(0xfa, 0xfa, 0xfb));
      light.ApplyColorOverride(
          &light.mWindow.mFg,
          NS_ComposeColors(light.mWindow.mBg, NS_RGBA(0, 0, 6, 204)));
      light.ApplyColorOverride(&light.mDialog, light.mWindow);

      ApplyLibadwaitaButtonColors(light);

      // FIXME(emilio): This is _technically_ not right, but the Firefox
      // front-end relies on this right now to not look really ugly. Arguably
      // Menu backgrounds or so is what should be used for the urlbar popups,
      // rather than Field...
      light.ApplyColorOverride(&light.mField.mBg, NS_RGB(0xff, 0xff, 0xff));

      // rgba(0,0,6,.8) over the background.
      light.ApplyColorOverride(&light.mSidebar.mBg, NS_RGB(0xeb, 0xeb, 0xed));
      light.ApplyColorOverride(
          &light.mSidebar.mFg,
          NS_ComposeColors(light.mSidebar.mBg, NS_RGBA(0, 0, 6, 204)));

      // We use the sidebar colors for the headerbar in light mode background
      // because it creates much better contrast. GTK headerbar colors are
      // white, and meant to "blend" with the contents otherwise, but that
      // doesn't work fine for Firefox's toolbars.
      light.ApplyColorOverride(&light.mHeaderBar, light.mSidebar);
      light.ApplyColorOverride(&light.mTitlebar, light.mSidebar);
      light.ApplyColorOverride(&light.mHeaderBarInactive, light.mSidebar);
      light.ApplyColorOverride(&light.mTitlebarInactive, light.mSidebar);

      // headerbar_backdrop_color
      light.ApplyColorOverride(&light.mHeaderBarInactive.mBg,
                               light.mWindow.mBg);
      light.ApplyColorOverride(&light.mTitlebarInactive.mBg, light.mWindow.mBg);

      light.ApplyColorOverride(&light.mFrameBorder, NS_RGB(0xe0, 0xe0, 0xe0));
      light.ApplyColorOverride(&light.mSidebarBorder, NS_RGBA(0, 0, 0, 18));

      // popover_bg_color, popover_fg_color
      light.ApplyColorOverride(&light.mMenu.mBg, NS_RGB(0xff, 0xff, 0xff));
      light.ApplyColorOverride(
          &light.mMenu.mFg,
          NS_ComposeColors(light.mMenu.mBg, NS_RGBA(0, 0, 6, 204)));
    }

    if (dark.mFamily == ThemeFamily::Adwaita) {
      dark.ApplyColorOverride(
          &dark.mWindow, {NS_RGB(0x22, 0x22, 0x26), NS_RGB(0xff, 0xff, 0xff)});
      dark.ApplyColorOverride(
          &dark.mDialog, {NS_RGB(0x36, 0x36, 0x3a), NS_RGB(0xff, 0xff, 0xff)});

      ApplyLibadwaitaButtonColors(dark);

      dark.ApplyColorOverride(
          &dark.mSidebar, {NS_RGB(0x2e, 0x2e, 0x32), NS_RGB(0xff, 0xff, 0xff)});
      dark.ApplyColorOverride(&dark.mHeaderBar, dark.mSidebar);
      dark.ApplyColorOverride(&dark.mTitlebar, dark.mSidebar);
      dark.ApplyColorOverride(&dark.mHeaderBarInactive, dark.mSidebar);
      dark.ApplyColorOverride(&dark.mTitlebarInactive, dark.mSidebar);

      // headerbar_backdrop_color
      dark.ApplyColorOverride(&dark.mHeaderBarInactive.mBg, dark.mWindow.mBg);
      dark.ApplyColorOverride(&dark.mTitlebarInactive.mBg, dark.mWindow.mBg);

      // headerbar_shade_color
      dark.ApplyColorOverride(&dark.mFrameBorder, NS_RGB(0x1f, 0x1f, 0x1f));
      dark.ApplyColorOverride(&dark.mSidebarBorder, NS_RGBA(0, 0, 0, 92));

      // popover_bg_color, popover_fg_color
      dark.ApplyColorOverride(
          &dark.mMenu, {NS_RGB(0x36, 0x36, 0x3a), NS_RGB(0xff, 0xff, 0xff)});
    }
  }

  // Some of the alt theme colors we can grab from the system theme, if we fell
  // back to the default light / dark themes.
  if (mAltTheme.mIsDefaultThemeFallback) {
    if (StaticPrefs::widget_gtk_alt_theme_selection()) {
      mAltTheme.ApplyColorOverride(&mAltTheme.mSelectedText,
                                   mSystemTheme.mSelectedText);
    }

    if (StaticPrefs::widget_gtk_alt_theme_scrollbar_active() &&
        (!mAltTheme.mIsDark || ShouldUseColorForActiveDarkScrollbarThumb(
                                   mSystemTheme.mThemedScrollbarThumbActive))) {
      mAltTheme.ApplyColorOverride(&mAltTheme.mThemedScrollbarThumbActive,
                                   mSystemTheme.mThemedScrollbarThumbActive);
    }

    if (StaticPrefs::widget_gtk_alt_theme_accent()) {
      mAltTheme.ApplyColorOverride(&mAltTheme.mAccent, mSystemTheme.mAccent);
    }
  }
}

void nsLookAndFeel::ConfigureAndInitializeAltTheme() {
  const bool fellBackToDefaultTheme = !ConfigureAltTheme();

  mAltTheme.Init();
  mAltTheme.mIsDefaultThemeFallback = fellBackToDefaultTheme;

  // Right now we're using the opposite color-scheme theme, make sure to record
  // it.
  mSystemThemeOverridden = true;
}

void nsLookAndFeel::ClearRoundedCornerProvider() {
  if (!mRoundedCornerProvider) {
    return;
  }
  gtk_style_context_remove_provider_for_screen(
      gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(mRoundedCornerProvider.get()));
  mRoundedCornerProvider = nullptr;
}

void nsLookAndFeel::UpdateRoundedBottomCornerStyles() {
  int32_t radius = StaticPrefs::widget_gtk_rounded_bottom_corners_enabled()
                       ? EffectiveTheme().mTitlebarRadius
                       : 0;
  LOGLNF("UpdateRoundedBottomCornerStyles(%dpx -> %dpx)",
         mRoundedCornerProviderRadius, radius);
  if (radius == mRoundedCornerProviderRadius) {
    return;
  }
  mRoundedCornerProviderRadius = radius;
  if (!radius) {
    return ClearRoundedCornerProvider();
  }
  mRoundedCornerProvider = dont_AddRef(gtk_css_provider_new());
  nsPrintfCString string(
      "window.csd decoration {"
      "border-bottom-right-radius: %dpx;"
      "border-bottom-left-radius: %dpx;"
      "}\n",
      radius, radius);
  GUniquePtr<GError> error;
  if (!gtk_css_provider_load_from_data(mRoundedCornerProvider.get(),
                                       string.get(), string.Length(),
                                       getter_Transfers(error))) {
    NS_WARNING(nsPrintfCString("Failed to load provider: %s - %s", string.get(),
                               error ? error->message : nullptr)
                   .get());
  }
  gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(mRoundedCornerProvider.get()),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

Maybe<ColorScheme> nsLookAndFeel::ComputeColorSchemeSetting() {
  {
    // Check the pref explicitly here. Usually this shouldn't be needed, but
    // since we can only load one GTK theme at a time, and the pref will
    // override the effective value that the rest of gecko assumes for the
    // "system" color scheme, we need to factor it in our GTK theme decisions.
    int32_t pref = 0;
    if (NS_SUCCEEDED(Preferences::GetInt("ui.systemUsesDarkTheme", &pref))) {
      return Some(pref ? ColorScheme::Dark : ColorScheme::Light);
    }
  }

  return mDBusSettings.mColorScheme;
}

void nsLookAndFeel::Initialize() {
  MOZ_DIAGNOSTIC_ASSERT(mPendingChanges != NativeChangeKind::None);
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread(),
                        "LookAndFeel init should be done on the main thread");

  auto pendingChanges = std::exchange(mPendingChanges, NativeChangeKind::None);

  GtkSettings* settings = gtk_settings_get_default();
  if (MOZ_UNLIKELY(!settings)) {
    NS_WARNING("EnsureInit: No settings");
    return;
  }

  AutoRestore<bool> restoreIgnoreSettings(sIgnoreChangedSettings);
  sIgnoreChangedSettings = true;

  // First initialize global settings.
  InitializeGlobalSettings();

  if (pendingChanges & NativeChangeKind::GtkTheme) {
    // Our current theme may be different from the system theme if we're
    // matching the Firefox theme or using the alt theme intentionally due to
    // the color-scheme preference. Make sure to restore the original system
    // theme.
    RestoreSystemTheme();

    // Record our system theme settings now.
    mSystemTheme.Init();

    // Find the alternative-scheme theme (light if the system theme is dark, or
    // vice versa), configure it and initialize it.
    ConfigureAndInitializeAltTheme();

    LOGLNF("System Theme: %s. Alt Theme: %s\n", mSystemTheme.mName.get(),
           mAltTheme.mName.get());
  }

  MaybeApplyColorOverrides();

  // Go back to the system theme or keep the alt theme configured, depending on
  // Firefox theme or user color-scheme preference.
  ConfigureFinalEffectiveTheme();

  // The current rounded corner radii depends on the effective theme.
  UpdateRoundedBottomCornerStyles();

  RecordTelemetry();
}

/* ButtonLayout represents a GTK CSD button and whether its on the left or
 * right side of the tab bar */
enum class HeaderBarButtonType { None = 0, Close, Minimize, Maximize };
struct HeaderBarButtonLayout {
  std::array<HeaderBarButtonType, 3> mButtons = {HeaderBarButtonType::None};
  bool mReversedPlacement = false;
};

HeaderBarButtonLayout GetGtkHeaderBarButtonLayout() {
  using Type = HeaderBarButtonType;

  HeaderBarButtonLayout result;

  gchar* decorationLayoutSetting = nullptr;
  GtkSettings* settings = gtk_settings_get_default();
  g_object_get(settings, "gtk-decoration-layout", &decorationLayoutSetting,
               nullptr);
  auto free = mozilla::MakeScopeExit([&] { g_free(decorationLayoutSetting); });

  // Use a default layout
  const gchar* decorationLayout = "menu:minimize,maximize,close";
  if (decorationLayoutSetting) {
    decorationLayout = decorationLayoutSetting;
  }

  // "minimize,maximize,close:" layout means buttons are on the opposite
  // titlebar side. close button is always there.
  const char* closeButton = strstr(decorationLayout, "close");
  const char* separator = strchr(decorationLayout, ':');
  result.mReversedPlacement =
      closeButton && separator && closeButton < separator;

  // We check what position a button string is stored in decorationLayout.
  //
  // decorationLayout gets its value from the GNOME preference:
  // org.gnome.desktop.vm.preferences.button-layout via the
  // gtk-decoration-layout property.
  //
  // Documentation of the gtk-decoration-layout property can be found here:
  // https://developer.gnome.org/gtk3/stable/GtkSettings.html#GtkSettings--gtk-decoration-layout
  nsDependentCSubstring layout(decorationLayout, strlen(decorationLayout));
  size_t activeButtons = 0;
  for (const auto& part : layout.Split(':')) {
    for (const auto& button : part.Split(',')) {
      if (activeButtons == result.mButtons.size()) {
        break;
      }
      if (button.EqualsLiteral("close")) {
        result.mButtons[activeButtons++] = Type::Close;
      } else if (button.EqualsLiteral("minimize")) {
        result.mButtons[activeButtons++] = Type::Minimize;
      } else if (button.EqualsLiteral("maximize")) {
        result.mButtons[activeButtons++] = Type::Maximize;
      }
    }
  }

  return result;
}

void nsLookAndFeel::InitializeGlobalSettings() {
  GtkSettings* settings = gtk_settings_get_default();

  mTextScaleFactor = GetGtkTextScaleFactor();

  mColorSchemePreference = ComputeColorSchemeSetting();

  gboolean enableAnimations = false;
  g_object_get(settings, "gtk-enable-animations", &enableAnimations, nullptr);
  mPrefersReducedMotion = !enableAnimations;

  gint blink_time = 0;     // In milliseconds
  gint blink_timeout = 0;  // in seconds
  gboolean blink;
  g_object_get(settings, "gtk-cursor-blink-time", &blink_time,
               "gtk-cursor-blink-timeout", &blink_timeout, "gtk-cursor-blink",
               &blink, nullptr);
  // From
  // https://docs.gtk.org/gtk3/property.Settings.gtk-cursor-blink-timeout.html:
  //
  //     Setting this to zero has the same effect as setting
  //     GtkSettings:gtk-cursor-blink to FALSE.
  //
  mCaretBlinkTime = blink && blink_timeout ? (int32_t)blink_time : 0;

  if (mCaretBlinkTime) {
    // blink_time * 2 because blink count is a full blink cycle.
    mCaretBlinkCount =
        std::max(1, int32_t(std::ceil(float(blink_timeout * 1000) /
                                      (float(blink_time) * 2.0f))));
  } else {
    mCaretBlinkCount = -1;
  }

  mCSDCloseButton = false;
  mCSDMinimizeButton = false;
  mCSDMaximizeButton = false;
  mCSDCloseButtonPosition = 0;
  mCSDMinimizeButtonPosition = 0;
  mCSDMaximizeButtonPosition = 0;

  // We need to initialize whole CSD config explicitly because it's queried
  // as -moz-gtk* media features.
  {
    auto layout = GetGtkHeaderBarButtonLayout();
    mCSDReversedPlacement = layout.mReversedPlacement;
    int32_t i = 0;
    for (auto buttonType : layout.mButtons) {
      // We check if a button is represented on the right side of the tabbar.
      // Then we assign it a value from 3 to 5, instead of 0 to 2 when it is on
      // the left side.
      int32_t* pos = nullptr;
      switch (buttonType) {
        case HeaderBarButtonType::Minimize:
          mCSDMinimizeButton = true;
          pos = &mCSDMinimizeButtonPosition;
          break;
        case HeaderBarButtonType::Maximize:
          mCSDMaximizeButton = true;
          pos = &mCSDMaximizeButtonPosition;
          break;
        case HeaderBarButtonType::Close:
          mCSDCloseButton = true;
          pos = &mCSDCloseButtonPosition;
          break;
        case HeaderBarButtonType::None:
          break;
      }

      if (pos) {
        *pos = i++;
      }
    }
  }

  struct actionMapping {
    TitlebarAction action;
    char name[100];
  } ActionMapping[] = {
      {TitlebarAction::None, "none"},
      {TitlebarAction::WindowLower, "lower"},
      {TitlebarAction::WindowMenu, "menu"},
      {TitlebarAction::WindowMinimize, "minimize"},
      {TitlebarAction::WindowMaximize, "maximize"},
      {TitlebarAction::WindowMaximizeToggle, "toggle-maximize"},
  };

  auto GetWindowAction = [&](const char* eventName) -> TitlebarAction {
    gchar* action = nullptr;
    g_object_get(settings, eventName, &action, nullptr);
    if (!action) {
      return TitlebarAction::None;
    }
    auto free = mozilla::MakeScopeExit([&] { g_free(action); });
    for (auto const& mapping : ActionMapping) {
      if (!strncmp(action, mapping.name, strlen(mapping.name))) {
        return mapping.action;
      }
    }
    return TitlebarAction::None;
  };

  mDoubleClickAction = GetWindowAction("gtk-titlebar-double-click");
  mMiddleClickAction = GetWindowAction("gtk-titlebar-middle-click");
}

void nsLookAndFeel::ConfigureFinalEffectiveTheme() {
  const bool shouldUseSystemTheme = [&] {
    using ChromeSetting = PreferenceSheet::ChromeColorSchemeSetting;
    // NOTE: We can't call ColorSchemeForChrome directly because this might run
    // while we're computing it.
    switch (PreferenceSheet::ColorSchemeSettingForChrome()) {
      case ChromeSetting::Light:
        return !mSystemTheme.mIsDark;
      case ChromeSetting::Dark:
        return mSystemTheme.mIsDark;
      case ChromeSetting::System:
        break;
    };
    if (!mColorSchemePreference) {
      return true;
    }
    const bool preferenceIsDark = *mColorSchemePreference == ColorScheme::Dark;
    return preferenceIsDark == mSystemTheme.mIsDark;
  }();

  const bool usingSystem = !mSystemThemeOverridden;
  LOGLNF("OverrideSystemThemeIfNeeded(matchesSystem=%d, usingSystem=%d)\n",
         shouldUseSystemTheme, usingSystem);

  if (shouldUseSystemTheme == usingSystem) {
    return;
  }

  if (shouldUseSystemTheme) {
    RestoreSystemTheme();
  } else if (usingSystem) {
    LOGLNF("Setting theme %s, %d\n", mAltTheme.mName.get(),
           mAltTheme.mPreferDarkTheme);

    GtkSettings* settings = gtk_settings_get_default();
    if (mSystemTheme.mName == mAltTheme.mName) {
      // Prefer setting only gtk-application-prefer-dark-theme, so we can still
      // get notified from notify::gtk-theme-name if the user changes the theme.
      g_object_set(settings, "gtk-application-prefer-dark-theme",
                   mAltTheme.mPreferDarkTheme, nullptr);
    } else {
      g_object_set(settings, "gtk-theme-name", mAltTheme.mName.get(),
                   "gtk-application-prefer-dark-theme",
                   mAltTheme.mPreferDarkTheme, nullptr);
    }
    mSystemThemeOverridden = true;
    GtkWidgets::Refresh();
  }
}

static bool GetColorFromBackgroundImage(GtkStyleContext* aStyle,
                                        nscolor aForForegroundColor,
                                        GtkStateFlags aState, nscolor* aColor) {
  GValue value = G_VALUE_INIT;
  gtk_style_context_get_property(aStyle, "background-image", aState, &value);
  auto cleanup = MakeScopeExit([&] { g_value_unset(&value); });
  if (GetColorFromImagePattern(&value, aColor)) {
    return true;
  }

  {
    GdkRGBA light, dark;
    if (GetGradientColors(&value, &light, &dark)) {
      nscolor l = GDK_RGBA_TO_NS_RGBA(light);
      nscolor d = GDK_RGBA_TO_NS_RGBA(dark);
      // Return the one with more contrast.
      // TODO(emilio): This could do interpolation or what not but seems
      // overkill.
      if (NS_LUMINOSITY_DIFFERENCE(l, aForForegroundColor) >
          NS_LUMINOSITY_DIFFERENCE(d, aForForegroundColor)) {
        *aColor = l;
      } else {
        *aColor = d;
      }
      return true;
    }
  }

  return false;
}

static nscolor GetBackgroundColor(
    GtkStyleContext* aStyle, nscolor aForForegroundColor,
    GtkStateFlags aState = GTK_STATE_FLAG_NORMAL,
    nscolor aOverBackgroundColor = NS_TRANSPARENT) {
  // Try to synthesize a color from a background-image.
  nscolor imageColor = NS_TRANSPARENT;
  if (GetColorFromBackgroundImage(aStyle, aForForegroundColor, aState,
                                  &imageColor)) {
    if (NS_GET_A(imageColor) == 255) {
      return imageColor;
    }
  }

  GdkRGBA gdkColor;
  gtk_style_context_get_background_color(aStyle, aState, &gdkColor);
  nscolor bgColor = GDK_RGBA_TO_NS_RGBA(gdkColor);
  // background-image paints over background-color.
  const nscolor finalColor = NS_ComposeColors(bgColor, imageColor);
  if (finalColor != aOverBackgroundColor) {
    return finalColor;
  }
  return NS_TRANSPARENT;
}

// Sets |aLightColor| and |aDarkColor| to colors from |aContext|.  Returns
// true if |aContext| uses these colors to render a visible border.
// If returning false, then the colors returned are a fallback from the
// border-color value even though |aContext| does not use these colors to
// render a border.
static Maybe<nscolor> GetBorderColor(
    GtkStyleContext* aContext, GtkStateFlags aState = GTK_STATE_FLAG_NORMAL) {
  // Determine whether the border on this style context is visible.
  GtkBorderStyle borderStyle = GTK_BORDER_STYLE_NONE;
  gtk_style_context_get(aContext, aState, GTK_STYLE_PROPERTY_BORDER_STYLE,
                        &borderStyle, nullptr);
  if (borderStyle == GTK_BORDER_STYLE_NONE ||
      borderStyle == GTK_BORDER_STYLE_HIDDEN) {
    return {};
  }
  // GTK has an initial value of zero for border-widths, and so themes
  // need to explicitly set border-widths to make borders visible.
  GtkBorder border;
  gtk_style_context_get_border(aContext, aState, &border);
  if (!border.top && !border.right && !border.bottom && !border.left) {
    return {};
  }

  // The initial value for the border-color is the foreground color, and so
  // this will usually return a color distinct from the background even if
  // there is no visible border detected.
  GdkRGBA color{};
  gtk_style_context_get_border_color(aContext, aState, &color);
  return Some(GDK_RGBA_TO_NS_RGBA(color));
}

static nscolor GetTextColor(GtkStyleContext* aStyle,
                            GtkStateFlags aState = GTK_STATE_FLAG_NORMAL) {
  GdkRGBA color;
  gtk_style_context_get_color(aStyle, aState, &color);
  return GDK_RGBA_TO_NS_RGBA(color);
}

using ColorPair = nsLookAndFeel::ColorPair;
static ColorPair GetColorPair(GtkStyleContext* aStyle,
                              GtkStateFlags aState = GTK_STATE_FLAG_NORMAL) {
  ColorPair result;
  result.mFg = GetTextColor(aStyle, aState);
  result.mBg = GetBackgroundColor(aStyle, result.mFg, aState);
  return result;
}

using ButtonColors = nsLookAndFeel::ButtonColors;
static ButtonColors GetButtonColors(
    GtkStyleContext* aStyle, GtkStateFlags aState = GTK_STATE_FLAG_NORMAL) {
  ButtonColors result;
  result.mFg = GetTextColor(aStyle, aState);
  result.mBorder = GetBorderColor(aStyle, aState).valueOr(NS_TRANSPARENT);
  result.mBg = GetBackgroundColor(aStyle, result.mFg, aState);
  return result;
}

static bool GetNamedColorPair(GtkStyleContext* aStyle, const char* aBgName,
                              const char* aFgName, ColorPair* aPair) {
  GdkRGBA bg, fg;
  if (!gtk_style_context_lookup_color(aStyle, aBgName, &bg) ||
      !gtk_style_context_lookup_color(aStyle, aFgName, &fg)) {
    return false;
  }

  aPair->mBg = GDK_RGBA_TO_NS_RGBA(bg);
  aPair->mFg = GDK_RGBA_TO_NS_RGBA(fg);

  // If the colors are semi-transparent and the theme provides a
  // background color, blend with them to get the "final" color, see
  // bug 1717077.
  if (NS_GET_A(aPair->mBg) != 255 &&
      (gtk_style_context_lookup_color(aStyle, "bg_color", &bg) ||
       gtk_style_context_lookup_color(aStyle, "theme_bg_color", &bg))) {
    aPair->mBg = NS_ComposeColors(GDK_RGBA_TO_NS_RGBA(bg), aPair->mBg);
  }

  // A semi-transparent foreground color would be kinda silly, but is done
  // for symmetry.
  if (NS_GET_A(aPair->mFg) != 255) {
    aPair->mFg = NS_ComposeColors(aPair->mBg, aPair->mFg);
  }

  return true;
}

static void EnsureColorPairIsOpaque(ColorPair& aPair) {
  // Blend with white, ensuring the color is opaque, so that the UI doesn't have
  // to care about alpha.
  aPair.mBg = NS_ComposeColors(NS_RGB(0xff, 0xff, 0xff), aPair.mBg);
  aPair.mFg = NS_ComposeColors(aPair.mBg, aPair.mFg);
}

static void PreferDarkerBackground(ColorPair& aPair) {
  // We use the darker one unless the foreground isn't really a color (is all
  // white / black / gray) and the background is, in which case we stick to what
  // we have.
  if (RelativeLuminanceUtils::Compute(aPair.mBg) >
          RelativeLuminanceUtils::Compute(aPair.mFg) &&
      (AnyColorChannelIsDifferent(aPair.mFg) ||
       !AnyColorChannelIsDifferent(aPair.mBg))) {
    std::swap(aPair.mBg, aPair.mFg);
  }
}

void nsLookAndFeel::PerThemeData::Init() {
  mName = GetGtkTheme();

  mFamily = [&] {
    if (StringBeginsWith(mName, "adw"_ns, nsCaseInsensitiveCStringComparator)) {
      // This catches "Adwaita", "Adwaita-dark", and "{A,a}dw-gtk3" too.
      return ThemeFamily::Adwaita;
    }
    if (StringBeginsWith(mName, "Breeze"_ns)) {
      return ThemeFamily::Breeze;
    }
    if (StringBeginsWith(mName, "Yaru"_ns)) {
      return ThemeFamily::Yaru;
    }
    return ThemeFamily::Unknown;
  }();

  GtkStyleContext* style;

  mHighContrast = StaticPrefs::widget_content_gtk_high_contrast_enabled() &&
                  mName.Find("HighContrast"_ns) >= 0;

  mPreferDarkTheme = GetPreferDarkTheme();

  mIsDark = GetThemeIsDark();

  GdkRGBA color{};

  // The label is not added to a parent widget, but shared for constructing
  // different style contexts.  The node hierarchy is constructed only on
  // the label style context.
  GtkWidget* labelWidget = gtk_label_new("M");
  g_object_ref_sink(labelWidget);

  // Window colors
  style = GtkWidgets::GetStyle(GtkWidgets::Type::Window);
  mWindow = mDialog = GetColorPair(style);

  gtk_style_context_get_border_color(style, GTK_STATE_FLAG_NORMAL, &color);
  mMozWindowActiveBorder = GDK_RGBA_TO_NS_RGBA(color);

  gtk_style_context_get_border_color(style, GTK_STATE_FLAG_INSENSITIVE, &color);
  mMozWindowInactiveBorder = GDK_RGBA_TO_NS_RGBA(color);

  style = GtkWidgets::GetStyle(GtkWidgets::Type::WindowContainer);
  {
    GtkStyleContext* labelStyle =
        GtkWidgets::CreateStyleForWidget(labelWidget, style);
    GetSystemFontInfo(labelStyle, &mDefaultFontName, &mDefaultFontStyle);
    g_object_unref(labelStyle);
  }

  // tooltip foreground and background
  style = GtkWidgets::GetStyle(GtkWidgets::Type::TooltipBoxLabel);
  mInfo.mFg = GetTextColor(style);
  style = GtkWidgets::GetStyle(GtkWidgets::Type::Tooltip);
  mInfo.mBg = GetBackgroundColor(style, mInfo.mFg);
  mTooltipRadius = GetBorderRadius(style);

  // Scrollbar colors: Some themes style the <trough>, while others style the
  // <scrollbar> itself, so we look at both and compose the colors.
  {
    style = GtkWidgets::GetStyle(GtkWidgets::Type::Scrollbar);
    gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL,
                                           &color);
    mThemedScrollbar = GDK_RGBA_TO_NS_RGBA(color);

    style = GtkWidgets::GetStyle(GtkWidgets::Type::ScrollbarTrough);
    gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL,
                                           &color);
    mThemedScrollbar =
        NS_ComposeColors(mThemedScrollbar, GDK_RGBA_TO_NS_RGBA(color));

    style = GtkWidgets::GetStyle(GtkWidgets::Type::ScrollbarThumb);
    gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL,
                                           &color);
    mThemedScrollbarThumb = GDK_RGBA_TO_NS_RGBA(color);
    gtk_style_context_get_background_color(style, GTK_STATE_FLAG_PRELIGHT,
                                           &color);
    mThemedScrollbarThumbHover = GDK_RGBA_TO_NS_RGBA(color);
    gtk_style_context_get_background_color(
        style, GtkStateFlags(GTK_STATE_FLAG_PRELIGHT | GTK_STATE_FLAG_ACTIVE),
        &color);
    mThemedScrollbarThumbActive = GDK_RGBA_TO_NS_RGBA(color);

    // Make sure that the thumb is visible, at least.
    const bool fallbackToUnthemedColors = [&] {
      if (!ShouldHonorThemeScrollbarColors()) {
        return true;
      }
      // If any of the scrollbar thumb colors are fully transparent, fall back
      // to non-native ones.
      if (!NS_GET_A(mThemedScrollbarThumb) ||
          !NS_GET_A(mThemedScrollbarThumbHover) ||
          !NS_GET_A(mThemedScrollbarThumbActive)) {
        return true;
      }
      // If the thumb and track are the same color and opaque, fall back to
      // non-native colors as well.
      if (mThemedScrollbar == mThemedScrollbarThumb &&
          NS_GET_A(mThemedScrollbar) == 0xff) {
        return true;
      }
      return false;
    }();

    if (fallbackToUnthemedColors) {
      if (mIsDark) {
        // Taken from Adwaita-dark.
        mThemedScrollbar = NS_RGB(0x31, 0x31, 0x31);
        mThemedScrollbarThumb = NS_RGB(0xa3, 0xa4, 0xa4);
      } else {
        // Taken from Adwaita.
        mThemedScrollbar = NS_RGB(0xce, 0xce, 0xce);
        mThemedScrollbarThumb = NS_RGB(0x82, 0x81, 0x7e);
      }

      mThemedScrollbarThumbHover =
          ThemeColors::AdjustUnthemedScrollbarThumbColor(
              mThemedScrollbarThumb, dom::ElementState::HOVER);
      mThemedScrollbarThumbActive =
          ThemeColors::AdjustUnthemedScrollbarThumbColor(
              mThemedScrollbarThumb, dom::ElementState::ACTIVE);
    }
  }

  style = GtkWidgets::GetStyle(GtkWidgets::Type::Menuitem);
  {
    GtkStyleContext* accelStyle =
        GtkWidgets::CreateStyleForWidget(gtk_accel_label_new("M"), style);

    GetSystemFontInfo(accelStyle, &mMenuFontName, &mMenuFontStyle);

    gtk_style_context_get_color(accelStyle, GTK_STATE_FLAG_NORMAL, &color);
    mMenu.mFg = GetTextColor(accelStyle);
    mGrayText = GetTextColor(accelStyle, GTK_STATE_FLAG_INSENSITIVE);
    g_object_unref(accelStyle);
  }

  style = GtkWidgets::GetStyle(GtkWidgets::Type::HeaderBar);
  {
    const bool headerBarHasBackground = HasBackground(style);
    if (!headerBarHasBackground || !GetBorderRadius(style)) {
      // Some themes like Elementary's style the container of the headerbar
      // rather than the header bar itself.
      GtkStyleContext* fixedStyle =
          GtkWidgets::GetStyle(GtkWidgets::Type::HeaderBarFixed);
      if (HasBackground(fixedStyle) &&
          (GetBorderRadius(fixedStyle) || !headerBarHasBackground)) {
        style = fixedStyle;
      }
    }
  }
  {
    mTitlebar = GetColorPair(style, GTK_STATE_FLAG_NORMAL);
    mTitlebarInactive = GetColorPair(style, GTK_STATE_FLAG_BACKDROP);
    mTitlebarRadius = GetBorderRadius(style);
  }

  // We special-case the header bar color in Adwaita, Yaru and Breeze to be the
  // titlebar color, because it looks better and matches what apps do by
  // default, see bug 1838460.
  //
  // We only do this in the relevant desktop environments, however, since in
  // other cases we don't really know if the DE's titlebars are going to match.
  //
  // For breeze, additionally we read the KDE colors directly, if available,
  // since these are user-configurable.
  //
  // For most other themes or those in unknown DEs, we use the menubar colors.
  //
  // FIXME(emilio): Can we do something a bit less special-case-y?
  const bool shouldUseTitlebarColorsForHeaderBar = [&] {
    if (mFamily == ThemeFamily::Adwaita || mFamily == ThemeFamily::Yaru) {
      return IsGnomeDesktopEnvironment();
    }
    if (mFamily == ThemeFamily::Breeze) {
      return IsKdeDesktopEnvironment();
    }
    return false;
  }();

  if (shouldUseTitlebarColorsForHeaderBar) {
    mHeaderBar = mTitlebar;
    mHeaderBarInactive = mTitlebarInactive;
    if (mFamily == ThemeFamily::Breeze) {
      GetNamedColorPair(style, "theme_header_background_breeze",
                        "theme_header_foreground_breeze", &mHeaderBar);
      GetNamedColorPair(style, "theme_header_background_backdrop_breeze",
                        "theme_header_foreground_backdrop_breeze",
                        &mHeaderBarInactive);
    }
  } else {
    style = GtkWidgets::GetStyle(GtkWidgets::Type::MenubarItem);
    mHeaderBar.mFg = GetTextColor(style);
    mHeaderBarInactive.mFg = GetTextColor(style, GTK_STATE_FLAG_BACKDROP);

    style = GtkWidgets::GetStyle(GtkWidgets::Type::Menubar);
    mHeaderBar.mBg = GetBackgroundColor(style, mHeaderBar.mFg);
    mHeaderBarInactive.mBg = GetBackgroundColor(style, mHeaderBarInactive.mFg,
                                                GTK_STATE_FLAG_BACKDROP);
  }

  style = GtkWidgets::GetStyle(GtkWidgets::Type::Menupopup);
  mMenu.mBg = [&] {
    nscolor color = GetBackgroundColor(style, mMenu.mFg);
    if (NS_GET_A(color)) {
      return color;
    }
    // Some themes only style menupopups with the backdrop pseudo-class. Since a
    // context / popup menu always seems to match that, try that before giving
    // up.
    color = GetBackgroundColor(style, mMenu.mFg, GTK_STATE_FLAG_BACKDROP);
    if (NS_GET_A(color)) {
      return color;
    }
    // If we get here we couldn't figure out the right color to use. Rather than
    // falling back to transparent, fall back to the window background.
    NS_WARNING(
        "Couldn't find menu background color, falling back to window "
        "background");
    return mWindow.mBg;
  }();

  style = GtkWidgets::GetStyle(GtkWidgets::Type::Menuitem);
  gtk_style_context_get_color(style, GTK_STATE_FLAG_PRELIGHT, &color);
  mMenuHover.mFg = GDK_RGBA_TO_NS_RGBA(color);
  mMenuHover.mBg = NS_ComposeColors(
      mMenu.mBg,
      GetBackgroundColor(style, mMenu.mFg, GTK_STATE_FLAG_PRELIGHT, mMenu.mBg));

  GtkWidget* parent = gtk_fixed_new();
  GtkWidget* window = gtk_window_new(GTK_WINDOW_POPUP);
  GtkWidget* treeView = gtk_tree_view_new();
  GtkWidget* linkButton = gtk_link_button_new("http://example.com/");
  GtkWidget* menuBar = gtk_menu_bar_new();
  GtkWidget* menuBarItem = gtk_menu_item_new();
  GtkWidget* entry = gtk_entry_new();
  GtkWidget* textView = gtk_text_view_new();

  gtk_container_add(GTK_CONTAINER(parent), treeView);
  gtk_container_add(GTK_CONTAINER(parent), linkButton);
  gtk_container_add(GTK_CONTAINER(parent), menuBar);
  gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), menuBarItem);
  gtk_container_add(GTK_CONTAINER(window), parent);
  gtk_container_add(GTK_CONTAINER(parent), entry);
  gtk_container_add(GTK_CONTAINER(parent), textView);

  // Text colors
  GdkRGBA bgColor;
  // If the text window background is translucent, then the background of
  // the textview root node is visible.
  style = GtkWidgets::GetStyle(GtkWidgets::Type::TextView);
  gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL,
                                         &bgColor);

  style = GtkWidgets::GetStyle(GtkWidgets::Type::TextViewText);
  gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL, &color);
  ApplyColorOver(color, &bgColor);
  mField.mBg = GDK_RGBA_TO_NS_RGBA(bgColor);
  gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &color);
  mField.mFg = GDK_RGBA_TO_NS_RGBA(color);
  mSidebar = mField;

  // Selected text and background
  {
    GtkStyleContext* selectionStyle =
        GtkWidgets::GetStyle(GtkWidgets::Type::TextViewTextSelection);
    auto GrabSelectionColors = [&](GtkStyleContext* style) {
      gtk_style_context_get_background_color(
          style,
          static_cast<GtkStateFlags>(GTK_STATE_FLAG_FOCUSED |
                                     GTK_STATE_FLAG_SELECTED),
          &color);
      mSelectedText.mBg = GDK_RGBA_TO_NS_RGBA(color);
      gtk_style_context_get_color(
          style,
          static_cast<GtkStateFlags>(GTK_STATE_FLAG_FOCUSED |
                                     GTK_STATE_FLAG_SELECTED),
          &color);
      mSelectedText.mFg = GDK_RGBA_TO_NS_RGBA(color);
    };
    GrabSelectionColors(selectionStyle);
    if (mSelectedText.mBg == mSelectedText.mFg) {
      // Some old distros/themes don't properly use the .selection style, so
      // fall back to the regular text view style.
      GrabSelectionColors(style);
    }

    // Default selected item color is the selection background / foreground
    // colors, but we prefer named colors, as those are more general purpose
    // than the actual selection style, which might e.g. be too-transparent.
    //
    // NOTE(emilio): It's unclear which one of the theme_selected_* or the
    // selected_* pairs should we prefer, in all themes that define both that
    // I've found, they're always the same.
    if (!GetNamedColorPair(style, "selected_bg_color", "selected_fg_color",
                           &mSelectedItem) &&
        !GetNamedColorPair(style, "theme_selected_bg_color",
                           "theme_selected_fg_color", &mSelectedItem)) {
      mSelectedItem = mSelectedText;
    }

    EnsureColorPairIsOpaque(mSelectedItem);

    // In a similar fashion, default accent color is the selected item/text
    // pair, but we also prefer named colors, if available.
    //
    // accent_{bg,fg}_color is not _really_ a gtk3 thing (it's a gtk4 thing),
    // but if gtk 3 themes want to specify these we let them, see:
    //
    //   https://gnome.pages.gitlab.gnome.org/libadwaita/doc/main/named-colors.html#accent-colors
    if (!GetNamedColorPair(style, "accent_bg_color", "accent_fg_color",
                           &mAccent)) {
      mAccent = mSelectedItem;
    }

    EnsureColorPairIsOpaque(mAccent);
    PreferDarkerBackground(mAccent);
  }

  // Button text color
  style = GtkWidgets::GetStyle(GtkWidgets::Type::Button);
  {
    GtkStyleContext* labelStyle =
        GtkWidgets::CreateStyleForWidget(labelWidget, style);
    GetSystemFontInfo(labelStyle, &mButtonFontName, &mButtonFontStyle);
    g_object_unref(labelStyle);
  }

  mButton = GetButtonColors(style);
  mButtonHover = GetButtonColors(style, GTK_STATE_FLAG_PRELIGHT);
  mButtonActive = GetButtonColors(
      style, GtkStateFlags(GTK_STATE_FLAG_PRELIGHT | GTK_STATE_FLAG_ACTIVE));
  mButtonDisabled = GetButtonColors(style, GTK_STATE_FLAG_INSENSITIVE);
  if (!NS_GET_A(mButtonHover.mBg)) {
    mButtonHover.mBg = mWindow.mBg;
  }
  if (!NS_GET_A(mButtonActive.mBg)) {
    mButtonActive.mBg = mWindow.mBg;
  }
  // Borders in Yaru / Adwaita have relatively little contrast, and are rather
  // neutral themes, so our stand-in ones work fine.
#define MAYBE_OVERRIDE_BUTTON_BORDER(field_, color_)                        \
  if (mFamily == ThemeFamily::Adwaita || mFamily == ThemeFamily::Yaru ||    \
      !NS_GET_A((field_).mBorder)) {                                        \
    (field_).mBorder = nsXPLookAndFeel::GetStandinForNativeColor(           \
        ColorID::color_, mIsDark ? ColorScheme::Dark : ColorScheme::Light); \
  }
  MAYBE_OVERRIDE_BUTTON_BORDER(mButton, Buttonborder)
  MAYBE_OVERRIDE_BUTTON_BORDER(mButtonHover, MozButtonhoverborder)
  MAYBE_OVERRIDE_BUTTON_BORDER(mButtonActive, MozButtonactiveborder)
  MAYBE_OVERRIDE_BUTTON_BORDER(mButtonDisabled, MozButtondisabledborder)
#undef MAYBE_OVERRIDE_BUTTON_BORDER

  // Column header colors
  style = GtkWidgets::GetStyle(GtkWidgets::Type::TreeHeaderCell);
  mMozColHeader = GetColorPair(style, GTK_STATE_FLAG_NORMAL);
  mMozColHeaderHover = GetColorPair(style, GTK_STATE_FLAG_NORMAL);
  mMozColHeaderActive = GetColorPair(style, GTK_STATE_FLAG_ACTIVE);

  // Compute cell highlight colors
  InitCellHighlightColors();

  // GtkFrame has a "border" subnode on which Adwaita draws the border.
  // Some themes do not draw on this node but draw a border on the widget
  // root node, so check the root node if no border is found on the border
  // node.
  style = GtkWidgets::GetStyle(GtkWidgets::Type::FrameBorder);
  if (auto color = GetBorderColor(style)) {
    mFrameBorder = *color;
  } else if (auto color = GetBorderColor(
                 GtkWidgets::GetStyle(GtkWidgets::Type::Frame))) {
    mFrameBorder = *color;
  } else {
    mFrameBorder = kBlack;
  }
  mSidebarBorder = mFrameBorder;

  // Some themes have a unified menu bar, and support window dragging on it
  gboolean supports_menubar_drag = FALSE;
  GParamSpec* param_spec = gtk_widget_class_find_style_property(
      GTK_WIDGET_GET_CLASS(menuBar), "window-dragging");
  if (param_spec) {
    if (g_type_is_a(G_PARAM_SPEC_VALUE_TYPE(param_spec), G_TYPE_BOOLEAN)) {
      gtk_widget_style_get(menuBar, "window-dragging", &supports_menubar_drag,
                           nullptr);
    }
  }
  mMenuSupportsDrag = supports_menubar_drag;

  // TODO: It returns wrong color for themes which
  // sets link color for GtkLabel only as we query
  // GtkLinkButton style here.
  style = gtk_widget_get_style_context(linkButton);
  gtk_style_context_get_color(style, GTK_STATE_FLAG_LINK, &color);
  mNativeHyperLinkText = GDK_RGBA_TO_NS_RGBA(color);

  gtk_style_context_get_color(style, GTK_STATE_FLAG_VISITED, &color);
  mNativeVisitedHyperLinkText = GDK_RGBA_TO_NS_RGBA(color);

  // invisible character styles
  guint value;
  g_object_get(entry, "invisible-char", &value, nullptr);
  mInvisibleCharacter = char16_t(value);

  // caret styles
  gtk_widget_style_get(entry, "cursor-aspect-ratio", &mCaretRatio, nullptr);

  GetSystemFontInfo(gtk_widget_get_style_context(entry), &mFieldFontName,
                    &mFieldFontStyle);

  gtk_widget_destroy(window);
  g_object_unref(labelWidget);

  if (LOGLNF_ENABLED()) {
    LOGLNF("Initialized theme %s (%d)\n", mName.get(), mPreferDarkTheme);
    for (auto id : MakeEnumeratedRange(ColorID::End)) {
      nscolor color;
      nsresult rv = GetColor(id, color);
      LOGLNF(" * color %d: pref=%s success=%d value=%x\n", int(id),
             GetColorPrefName(id), NS_SUCCEEDED(rv),
             NS_SUCCEEDED(rv) ? color : 0);
    }
    LOGLNF(" * titlebar-radius: %d\n", mTitlebarRadius);
  }
}

// virtual
char16_t nsLookAndFeel::GetPasswordCharacterImpl() {
  EnsureInit();
  return mSystemTheme.mInvisibleCharacter;
}

bool nsLookAndFeel::GetEchoPasswordImpl() { return false; }

bool nsLookAndFeel::GetDefaultDrawInTitlebar() { return sCSDAvailable; }

nsXPLookAndFeel::TitlebarAction nsLookAndFeel::GetTitlebarAction(
    TitlebarEvent aEvent) {
  return aEvent == TitlebarEvent::Double_Click ? mDoubleClickAction
                                               : mMiddleClickAction;
}

void nsLookAndFeel::GetThemeInfo(nsACString& aInfo) {
  aInfo.Append(mSystemTheme.mName);
  aInfo.Append(" / ");
  aInfo.Append(mAltTheme.mName);
}

static bool WidgetUsesImage(GtkWidgets::Type aNodeType) {
  static constexpr GtkStateFlags sFlagsToCheck[]{
      GTK_STATE_FLAG_NORMAL, GTK_STATE_FLAG_PRELIGHT,
      GtkStateFlags(GTK_STATE_FLAG_PRELIGHT | GTK_STATE_FLAG_ACTIVE),
      GTK_STATE_FLAG_BACKDROP, GTK_STATE_FLAG_INSENSITIVE};

  GtkStyleContext* style = GtkWidgets::GetStyle(aNodeType);
  GValue value = G_VALUE_INIT;
  for (GtkStateFlags state : sFlagsToCheck) {
    gtk_style_context_get_property(style, "background-image", state, &value);
    bool hasPattern = G_VALUE_TYPE(&value) == CAIRO_GOBJECT_TYPE_PATTERN &&
                      g_value_get_boxed(&value);
    g_value_unset(&value);
    if (hasPattern) {
      return true;
    }
  }
  return false;
}

nsresult nsLookAndFeel::GetKeyboardLayoutImpl(nsACString& aLayout) {
  if (mozilla::widget::GdkIsX11Display()) {
#if defined(MOZ_X11)
    Display* display = gdk_x11_get_default_xdisplay();
    if (!display) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    XkbDescRec* kbdDesc = XkbAllocKeyboard();
    if (!kbdDesc) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    auto cleanup = MakeScopeExit([&] { XkbFreeKeyboard(kbdDesc, 0, true); });

    XkbStateRec state;
    XkbGetState(display, XkbUseCoreKbd, &state);
    uint32_t group = state.group;

    XkbGetNames(display, XkbGroupNamesMask, kbdDesc);

    if (!kbdDesc->names || !kbdDesc->names->groups[group]) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    char* layout = XGetAtomName(display, kbdDesc->names->groups[group]);

    aLayout.Assign(layout);
#endif
  } else {
#if defined(MOZ_WAYLAND)
    struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    auto cleanupContext = MakeScopeExit([&] { xkb_context_unref(context); });

    struct xkb_keymap* keymap = xkb_keymap_new_from_names(
        context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    auto cleanupKeymap = MakeScopeExit([&] { xkb_keymap_unref(keymap); });

    const char* layout = xkb_keymap_layout_get_name(keymap, 0);

    if (layout) {
      aLayout.Assign(layout);
    }
#endif
  }

  return NS_OK;
}

void nsLookAndFeel::RecordLookAndFeelSpecificTelemetry() {
  // Gtk version we're on.
  nsCString version;
  version.AppendPrintf("%d.%d", gtk_major_version, gtk_minor_version);
  glean::widget::gtk_version.Set(version);

  glean::widget::desktop_environment.Set(GetDesktopEnvironmentIdentifier());
}

bool nsLookAndFeel::ShouldHonorThemeScrollbarColors() {
  // If the Gtk theme uses anything other than solid color backgrounds for Gtk
  // scrollbar parts, this is a good indication that painting XUL scrollbar part
  // elements using colors extracted from the theme won't provide good results.
  return !WidgetUsesImage(GtkWidgets::Type::Scrollbar) &&
         !WidgetUsesImage(GtkWidgets::Type::ScrollbarContents) &&
         !WidgetUsesImage(GtkWidgets::Type::ScrollbarTrough) &&
         !WidgetUsesImage(GtkWidgets::Type::ScrollbarThumb);
}

#undef LOGLNF
#undef LOGLNF_ENABLED
