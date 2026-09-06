/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* the features that media queries can test */

#include "PreferenceSheet.h"
#include "mozilla/GeckoBindings.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/RelativeLuminanceUtils.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StyleSheet.h"
#include "mozilla/StyleSheetInlines.h"
#include "mozilla/dom/BrowsingContextBinding.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/ScreenBinding.h"
#include "mozilla/gfx/gfxVars.h"
#include "nsCSSProps.h"
#include "nsCSSValue.h"
#include "nsContentUtils.h"
#include "nsDeviceContext.h"
#include "nsGlobalWindowOuter.h"
#include "nsIBaseWindow.h"
#include "nsIDocShell.h"
#include "nsIPrintSettings.h"
#include "nsIWidget.h"
#include "nsPresContext.h"
#include "nsStyleConsts.h"

using namespace mozilla;
using mozilla::dom::DisplayMode;
using mozilla::dom::Document;

// A helper for four features below
static nsSize GetSize(const Document& aDocument) {
  nsPresContext* pc = aDocument.GetPresContext();

  // Per spec, return a 0x0 viewport if we're not being rendered. See:
  //
  //  * https://github.com/w3c/csswg-drafts/issues/571
  //  * https://github.com/whatwg/html/issues/1813
  //
  if (!pc) {
    return {};
  }

  if (pc->IsRootPaginatedDocument()) {
    // We want the page size, including unprintable areas and margins.
    //
    // FIXME(emilio, bug 1414600): Not quite!
    return pc->GetPageSize();
  }

  return pc->GetVisibleArea().Size();
}

// A helper for three features below.
static nsSize GetDeviceSize(const Document& aDocument) {
  if (aDocument.ShouldResistFingerprinting(RFPTarget::CSSDeviceSize)) {
    return GetSize(aDocument);
  }

  // Media queries in documents in an RDM pane should use the simulated
  // device size.
  Maybe<CSSIntSize> deviceSize =
      nsGlobalWindowOuter::GetRDMDeviceSize(aDocument);
  if (deviceSize.isSome()) {
    return CSSPixel::ToAppUnits(deviceSize.value());
  }

  // Media queries in documents should use an override set with WebDriver BiDi
  // if it exists.
  if (dom::BrowsingContext* bc = aDocument.GetBrowsingContext()) {
    Maybe<CSSIntSize> screenSize = bc->GetScreenAreaOverride();
    if (screenSize.isSome()) {
      return CSSPixel::ToAppUnits(screenSize.value());
    }
  }

  nsPresContext* pc = aDocument.GetPresContext();
  // NOTE(emilio): We should probably figure out how to return an appropriate
  // device size here, though in a multi-screen world that makes no sense
  // really.
  if (!pc) {
    return {};
  }

  if (pc->IsRootPaginatedDocument()) {
    // We want the page size, including unprintable areas and margins.
    // XXX The spec actually says we want the "page sheet size", but
    // how is that different?
    return pc->GetPageSize();
  }

  return pc->DeviceContext()->GetDeviceSurfaceDimensions();
}

bool Gecko_MediaFeatures_IsResourceDocument(const Document* aDocument) {
  return aDocument->IsResourceDoc();
}

bool Gecko_MediaFeatures_InAndroidPipMode(const Document* aDocument) {
  return aDocument->InAndroidPipMode();
}

bool Gecko_MediaFeatures_UseOverlayScrollbars(const Document* aDocument) {
  nsPresContext* pc = aDocument->GetPresContext();
  return pc && pc->UseOverlayScrollbars();
}

static nsDeviceContext* GetDeviceContextFor(const Document* aDocument) {
  nsPresContext* pc = aDocument->GetPresContext();
  if (!pc) {
    return nullptr;
  }

  // It would be nice to call nsLayoutUtils::GetDeviceContextForScreenInfo here,
  // except for two things:  (1) it can flush, and flushing is bad here, and (2)
  // it doesn't really get us consistency in multi-monitor situations *anyway*.
  return pc->DeviceContext();
}

void Gecko_MediaFeatures_GetDeviceSize(const Document* aDocument,
                                       nscoord* aWidth, nscoord* aHeight) {
  nsSize size = GetDeviceSize(*aDocument);
  *aWidth = size.width;
  *aHeight = size.height;
}

int32_t Gecko_MediaFeatures_GetMonochromeBitsPerPixel(
    const Document* aDocument) {
  // The default bits per pixel for a monochrome device. We could propagate this
  // further to nsIPrintSettings, but Gecko doesn't actually know this value
  // from the hardware, so it seems silly to do so.
  static constexpr int32_t kDefaultMonochromeBpp = 8;

  nsPresContext* pc = aDocument->GetPresContext();
  if (!pc) {
    return 0;
  }
  nsIPrintSettings* ps = pc->GetPrintSettings();
  if (!ps) {
    return 0;
  }
  bool color = true;
  ps->GetPrintInColor(&color);
  return color ? 0 : kDefaultMonochromeBpp;
}

StyleColorGamut Gecko_MediaFeatures_ColorGamut(const Document* aDocument) {
  auto* dx = GetDeviceContextFor(aDocument);
  if (!dx || aDocument->ShouldResistFingerprinting(RFPTarget::CSSColorInfo)) {
    return StyleColorGamut::Srgb;
  }
  switch (dx->GetColorGamut()) {
    case dom::ScreenColorGamut::Srgb:
      return StyleColorGamut::Srgb;
    case dom::ScreenColorGamut::Rec2020:
      return StyleColorGamut::Rec2020;
    case dom::ScreenColorGamut::P3:
      return StyleColorGamut::P3;
  }
  return StyleColorGamut::Srgb;
}

int32_t Gecko_MediaFeatures_GetColorDepth(const Document* aDocument) {
  if (Gecko_MediaFeatures_GetMonochromeBitsPerPixel(aDocument) != 0) {
    // If we're a monochrome device, then the color depth is zero.
    return 0;
  }

  // Use depth of 24 when resisting fingerprinting, or when we're not being
  // rendered.
  int32_t depth = 24;

  if (!aDocument->ShouldResistFingerprinting(RFPTarget::CSSColorInfo)) {
    if (nsDeviceContext* dx = GetDeviceContextFor(aDocument)) {
      depth = dx->GetDepth();
    }
  }

  // The spec says to use bits *per color component*, so divide by 3,
  // and round down, since the spec says to use the smallest when the
  // color components differ.
  return depth / 3;
}

float Gecko_MediaFeatures_GetResolution(const Document* aDocument) {
  // We're returning resolution in terms of device pixels per css pixel, since
  // that is the preferred unit for media queries of resolution. This avoids
  // introducing precision error from conversion to and from less-used
  // physical units like inches.
  nsPresContext* pc = aDocument->GetPresContext();
  if (!pc) {
    return 1.;
  }

  if (pc->GetOverrideDPPX() > 0.) {
    return pc->GetOverrideDPPX();
  }

  if (aDocument->ShouldResistFingerprinting(RFPTarget::CSSResolution)) {
    return float(nsRFPService::GetDevicePixelRatioAtZoom(pc->GetFullZoom()));
  }
  // Get the actual device pixel ratio, which also takes zoom into account.
  return float(AppUnitsPerCSSPixel()) /
         pc->DeviceContext()->AppUnitsPerDevPixel();
}

static const Document* TopDocument(const Document* aDocument) {
  const Document* current = aDocument;
  while (const Document* parent = current->GetInProcessParentDocument()) {
    current = parent;
  }
  return current;
}

StyleDisplayMode Gecko_MediaFeatures_GetDisplayMode(const Document* aDocument) {
  const Document* rootDocument = TopDocument(aDocument);

  nsCOMPtr<nsISupports> container = rootDocument->GetContainer();
  if (nsCOMPtr<nsIBaseWindow> baseWindow = do_QueryInterface(container)) {
    nsCOMPtr<nsIWidget> mainWidget = baseWindow->GetMainWidget();
    if (mainWidget && mainWidget->SizeMode() == nsSizeMode_Fullscreen) {
      return StyleDisplayMode::Fullscreen;
    }
  }

  static_assert(
      static_cast<int32_t>(DisplayMode::Browser) ==
              static_cast<int32_t>(StyleDisplayMode::Browser) &&
          static_cast<int32_t>(DisplayMode::Minimal_ui) ==
              static_cast<int32_t>(StyleDisplayMode::MinimalUi) &&
          static_cast<int32_t>(DisplayMode::Standalone) ==
              static_cast<int32_t>(StyleDisplayMode::Standalone) &&
          static_cast<int32_t>(DisplayMode::Fullscreen) ==
              static_cast<int32_t>(StyleDisplayMode::Fullscreen) &&
          static_cast<int32_t>(DisplayMode::Picture_in_picture) ==
              static_cast<int32_t>(StyleDisplayMode::PictureInPicture),
      "DisplayMode must mach nsStyleConsts.h");

  dom::BrowsingContext* browsingContext = aDocument->GetBrowsingContext();
  if (!browsingContext) {
    return StyleDisplayMode::Browser;
  }
  return static_cast<StyleDisplayMode>(browsingContext->DisplayMode());
}

bool Gecko_MediaFeatures_MatchesPlatform(StylePlatform aPlatform) {
  switch (aPlatform) {
#if defined(XP_WIN)
    case StylePlatform::Windows:
      return true;
#elif defined(ANDROID)
    case StylePlatform::Android:
      return true;
#elif defined(MOZ_WIDGET_GTK)
    case StylePlatform::Linux:
      return true;
#elif defined(XP_MACOSX)
    case StylePlatform::Macos:
      return true;
#elif defined(XP_IOS)
    case StylePlatform::Ios:
      return true;
#else
#  error "Unknown platform?"
#endif
    default:
      return false;
  }
}

bool Gecko_MediaFeatures_PrefersReducedMotion(const Document* aDocument) {
  if (aDocument->ShouldResistFingerprinting(
          RFPTarget::CSSPrefersReducedMotion)) {
    return false;
  }

  // Check for DevTools override first
  if (dom::BrowsingContext* bc = aDocument->GetBrowsingContext()) {
    auto* top = bc->Top();
    switch (top->GetPrefersReducedMotionOverride()) {
      case dom::PrefersReducedMotionOverride::Reduce:
        return true;
      case dom::PrefersReducedMotionOverride::No_preference:
        return false;
      case dom::PrefersReducedMotionOverride::None:
        break;
    }
  }

  return LookAndFeel::GetInt(LookAndFeel::IntID::PrefersReducedMotion, 0) == 1;
}

bool Gecko_MediaFeatures_PrefersReducedTransparency(const Document* aDocument) {
  if (aDocument->ShouldResistFingerprinting(
          RFPTarget::CSSPrefersReducedTransparency)) {
    return false;
  }
  return LookAndFeel::GetInt(LookAndFeel::IntID::PrefersReducedTransparency,
                             0) == 1;
}

StylePrefersColorScheme Gecko_MediaFeatures_PrefersColorScheme(
    const Document* aDocument, bool aUseContent) {
  auto scheme = aUseContent ? PreferenceSheet::ContentPrefs().mColorScheme
                            : aDocument->PreferredColorScheme();
  return scheme == ColorScheme::Dark ? StylePrefersColorScheme::Dark
                                     : StylePrefersColorScheme::Light;
}

bool Gecko_MediaFeatures_MacRTL(const Document* aDocument) {
  auto* widget = nsContentUtils::WidgetForDocument(aDocument);
  return widget && widget->IsMacTitlebarDirectionRTL();
}

// Neither Linux, Windows, nor Mac have a way to indicate that low contrast is
// preferred so we use the presence of an accessibility theme or forced colors
// as a signal.
StylePrefersContrast Gecko_MediaFeatures_PrefersContrast(
    const Document* aDocument) {
  if (aDocument->ShouldResistFingerprinting(RFPTarget::CSSPrefersContrast)) {
    return StylePrefersContrast::NoPreference;
  }
  const auto& prefs = PreferenceSheet::PrefsFor(*aDocument);
  if (prefs.mUseDocumentColors) {
    return StylePrefersContrast::NoPreference;
  }
  const auto& colors = prefs.ColorsFor(ColorScheme::Light);
  float ratio = RelativeLuminanceUtils::ContrastRatio(colors.mDefaultBackground,
                                                      colors.mDefault);
  // https://www.w3.org/TR/WCAG21/#contrast-minimum
  if (ratio < 4.5f) {
    return StylePrefersContrast::Less;
  }
  // https://www.w3.org/TR/WCAG21/#contrast-enhanced
  if (ratio >= 7.0f) {
    return StylePrefersContrast::More;
  }
  return StylePrefersContrast::Custom;
}

bool Gecko_MediaFeatures_InvertedColors(const Document* aDocument) {
  if (aDocument->ShouldResistFingerprinting(RFPTarget::CSSInvertedColors)) {
    return false;
  }
  return LookAndFeel::GetInt(LookAndFeel::IntID::InvertedColors, 0) == 1;
}

StyleScripting Gecko_MediaFeatures_Scripting(const Document* aDocument) {
  const auto* doc = aDocument;
  if (aDocument->IsStaticDocument()) {
    doc = aDocument->GetOriginalDocument();
  }

  return doc->IsScriptEnabled() ? StyleScripting::Enabled
                                : StyleScripting::None;
}

StyleDynamicRange Gecko_MediaFeatures_DynamicRange(const Document* aDocument) {
  // Bug 1759772: Once HDR color is available, update each platform
  // LookAndFeel implementation to return StyleDynamicRange::High when
  // appropriate.
  return StyleDynamicRange::Standard;
}

StyleDynamicRange Gecko_MediaFeatures_VideoDynamicRange(
    const Document* aDocument) {
  if (aDocument->ShouldResistFingerprinting(RFPTarget::CSSVideoDynamicRange)) {
    return StyleDynamicRange::Standard;
  }
  // Usually compositors can process HDR content even without HDR displays.
  //
  // This parallels logic in gfxPlatform::UseHDR().
  if (StaticPrefs::gfx_color_management_hdr_force_enabled()) {
    return StyleDynamicRange::High;
  }
  if (!StaticPrefs::gfx_color_management_hdr() || !gfx::gfxVars::VideoHDR()) {
    return StyleDynamicRange::Standard;
  }
  // video-dynamic-range: high has 3 requirements:
  // 1) high peak brightness
  // 2) high contrast ratio
  // 3) color depth > 24

  // As a proxy for those requirements, return 'High' if the screen associated
  // with the device context claims to be HDR capable.
  if (nsDeviceContext* dx = GetDeviceContextFor(aDocument)) {
    if (dx->GetScreenIsHDR()) {
      return StyleDynamicRange::High;
    }
  }

  return StyleDynamicRange::Standard;
}

static PointerCapabilities GetPointerCapabilities(const Document* aDocument,
                                                  LookAndFeel::IntID aID) {
  MOZ_ASSERT(aID == LookAndFeel::IntID::PrimaryPointerCapabilities ||
             aID == LookAndFeel::IntID::AllPointerCapabilities);
  MOZ_ASSERT(aDocument);

  if (dom::BrowsingContext* bc = aDocument->GetBrowsingContext()) {
    // The touch-events-override happens only for the Responsive Design Mode so
    // that we don't need to care about ResistFingerprinting.
    if (bc->TouchEventsOverride() == dom::TouchEventsOverride::Enabled) {
      return PointerCapabilities::Coarse;
    }
  }

  // The default value for Desktop is mouse-type pointer, and for Android
  // a coarse pointer.
  const PointerCapabilities kDefaultCapabilities =
#ifdef ANDROID
      PointerCapabilities::Coarse;
#else
      PointerCapabilities::Fine | PointerCapabilities::Hover;
#endif
  if (aDocument->ShouldResistFingerprinting(
          RFPTarget::CSSPointerCapabilities)) {
    return kDefaultCapabilities;
  }

  int32_t intValue;
  nsresult rv = LookAndFeel::GetInt(aID, &intValue);
  if (NS_FAILED(rv)) {
    return kDefaultCapabilities;
  }

  return static_cast<PointerCapabilities>(intValue);
}

PointerCapabilities Gecko_MediaFeatures_PrimaryPointerCapabilities(
    const Document* aDocument) {
  return GetPointerCapabilities(aDocument,
                                LookAndFeel::IntID::PrimaryPointerCapabilities);
}

PointerCapabilities Gecko_MediaFeatures_AllPointerCapabilities(
    const Document* aDocument) {
  return GetPointerCapabilities(aDocument,
                                LookAndFeel::IntID::AllPointerCapabilities);
}

StyleGtkThemeFamily Gecko_MediaFeatures_GtkThemeFamily() {
  static_assert(int32_t(StyleGtkThemeFamily::Unknown) == 0);
  return StyleGtkThemeFamily(
      LookAndFeel::GetInt(LookAndFeel::IntID::GTKThemeFamily));
}
