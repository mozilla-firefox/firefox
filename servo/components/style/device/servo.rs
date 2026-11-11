/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Servo-specific logic for [`Device`].

use crate::color::AbsoluteColor;
use crate::context::QuirksMode;
use crate::custom_properties::CssEnvironment;
use crate::font_metrics::FontMetrics;
use crate::logical_geometry::WritingMode;
use crate::media_queries::MediaType;
use crate::properties::ComputedValues;
use crate::properties::style_structs::Font;
use crate::queries::values::PrefersColorScheme;
use crate::servo::media_features::PointerCapabilities;
use crate::values::KeyframesName;
use crate::values::computed::font::GenericFontFamily;
use crate::values::computed::{
    CSSPixelLength, Length, LineHeight, LinkParameters, NonNegativeLength,
};
use crate::values::specified::ViewportVariant;
use crate::values::specified::color::{ColorSchemeFlags, ForcedColors, SystemColor};
use crate::values::specified::font::{
    FONT_MEDIUM_CAP_PX, FONT_MEDIUM_CH_PX, FONT_MEDIUM_EX_PX, FONT_MEDIUM_IC_PX,
    FONT_MEDIUM_LINE_HEIGHT_PX, FONT_MEDIUM_PX, QueryFontMetricsFlags,
};
use app_units::{AU_PER_PX, Au};
use euclid::default::Size2D as UntypedSize2D;
use euclid::{Scale, SideOffsets2D, Size2D};
use malloc_size_of_derive::MallocSizeOf;
use mime::Mime;
use parking_lot::RwLock;
use servo_arc::Arc;
use std::fmt::Debug;
use std::hash::{DefaultHasher, Hasher};
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use style_traits::{CSSPixel, DevicePixel};

use crate::device::Device;

/// A trait used to query font metrics in clients of Stylo. This is used by Device to
/// query font metrics in a way that is specific to the client using Stylo.
pub trait FontMetricsProvider: Debug + Sync {
    /// Query the font metrics for the given font and the given base font size.
    fn query_font_metrics(
        &self,
        vertical: bool,
        font: &Font,
        base_size: CSSPixelLength,
        flags: QueryFontMetricsFlags,
    ) -> FontMetrics;
    /// Gets the base size given a generic font family.
    fn base_size_for_generic(&self, generic: GenericFontFamily) -> Length;
}

#[derive(Debug, MallocSizeOf)]
pub(super) struct ExtraDeviceData {
    /// Data which Stylo uses to evaluate media queries
    media_data: MediaData,
    /// An implementation of a trait which implements support for querying font metrics.
    #[ignore_malloc_size_of = "Owned by embedder"]
    font_metrics_provider: Box<dyn FontMetricsProvider>,
}

#[derive(Debug, MallocSizeOf, Clone, PartialEq)]
/// Data which Stylo uses to evaluate media queries
pub struct MediaData {
    /// The current media type used by de device.
    pub media_type: MediaType,
    /// The current viewport size, in CSS pixels.
    pub viewport_size: Size2D<f32, CSSPixel>,
    /// The current screen size, in device pixels.
    pub device_size: Size2D<f32, DevicePixel>,
    /// The current device pixel ratio, from CSS pixels to device pixels.
    pub device_pixel_ratio: Scale<f32, CSSPixel, DevicePixel>,
    /// The current quirks mode.
    #[ignore_malloc_size_of = "Pure stack type"]
    pub quirks_mode: QuirksMode,
    /// Whether the user prefers light mode or dark mode
    #[ignore_malloc_size_of = "Pure stack type"]
    pub prefers_color_scheme: PrefersColorScheme,
    /// The capabilities of the primary pointer input
    #[ignore_malloc_size_of = "Pure stack type"]
    pub primary_pointer_capabilities: PointerCapabilities,
    /// The union of the capabilities of all pointer inputs
    #[ignore_malloc_size_of = "Pure stack type"]
    pub all_pointer_capabilities: PointerCapabilities,
}

impl Device {
    /// Trivially construct a new `Device`.
    pub fn new(
        default_values: Arc<ComputedValues>,
        font_metrics_provider: Box<dyn FontMetricsProvider>,
        media_data: MediaData,
    ) -> Device {
        let root_style = RwLock::new(Arc::clone(&default_values));
        Device {
            root_style,
            root_font_size: AtomicU32::new(FONT_MEDIUM_PX.to_bits()),
            root_line_height: AtomicU32::new(FONT_MEDIUM_LINE_HEIGHT_PX.to_bits()),
            root_font_metrics_ex: AtomicU32::new(FONT_MEDIUM_EX_PX.to_bits()),
            root_font_metrics_cap: AtomicU32::new(FONT_MEDIUM_CAP_PX.to_bits()),
            root_font_metrics_ch: AtomicU32::new(FONT_MEDIUM_CH_PX.to_bits()),
            root_font_metrics_ic: AtomicU32::new(FONT_MEDIUM_IC_PX.to_bits()),
            used_root_font_size: AtomicBool::new(false),
            used_root_line_height: AtomicBool::new(false),
            used_root_font_metrics: RwLock::new(false),
            used_font_metrics: AtomicBool::new(false),
            used_viewport_size: AtomicBool::new(false),
            used_dynamic_viewport_size: AtomicBool::new(false),
            environment: CssEnvironment,
            default_values,
            body_text_color: RwLock::new(AbsoluteColor::BLACK),
            extra: ExtraDeviceData {
                media_data,
                font_metrics_provider,
            },
        }
    }

    /// Returns the computed line-height for the font in a given computed values instance.
    ///
    /// If you pass down an element, then the used line-height is returned.
    pub fn calc_line_height(
        &self,
        font: &crate::properties::style_structs::Font,
        _writing_mode: WritingMode,
        _element: Option<()>,
    ) -> NonNegativeLength {
        (match font.line_height {
            // TODO: compute `normal` from the font metrics
            LineHeight::Normal => CSSPixelLength::new(0.),
            LineHeight::Number(number) => font.font_size.computed_size() * number.0,
            LineHeight::Length(length) => length.0,
        })
        .into()
    }

    /// Get the quirks mode of the current device.
    pub fn quirks_mode(&self) -> QuirksMode {
        self.extra.media_data.quirks_mode
    }

    /// Returns a hasher specific to this document, used to vary the random
    /// base values of `random()` functions between document instances.
    #[inline]
    pub fn document_random_hasher(&self) -> impl Hasher {
        // TODO: Implement a document-specific hasher (like Gecko in Bug 2081781).
        DefaultHasher::new()
    }

    /// Gets the base size given a generic font family.
    pub fn base_size_for_generic(&self, generic: GenericFontFamily) -> Length {
        self.extra
            .font_metrics_provider
            .base_size_for_generic(generic)
    }

    /// Whether a given animation name may be referenced from style.
    pub fn animation_name_may_be_referenced(&self, _: &KeyframesName) -> bool {
        // Assume it is, since we don't have any good way to prove it's not.
        true
    }

    /// Get the [`MediaData`] associated with this [`Device`].
    #[inline]
    pub fn media_data(&self) -> &MediaData {
        &self.extra.media_data
    }

    /// Mutate the [`MediaData`] on this [`Device`].
    ///
    /// Note that this does not update any associated `Stylist`. For this you must call
    /// `Stylist::media_features_change_changed_style` and
    /// `Stylist::force_stylesheet_origins_dirty`.
    pub fn media_data_mut(&mut self) -> &mut MediaData {
        &mut self.extra.media_data
    }

    /// Set the [`MediaData`] on this [`Device`].
    ///
    /// Note that this does not update any associated `Stylist`. For this you must call
    /// `Stylist::media_features_change_changed_style` and
    /// `Stylist::force_stylesheet_origins_dirty`.
    pub fn set_media_data(&mut self, media_data: MediaData) {
        self.extra.media_data = media_data;
    }

    /// Get the viewport size on this [`Device`].
    pub fn viewport_size(&self) -> Size2D<f32, CSSPixel> {
        self.extra.media_data.viewport_size
    }

    /// Set the viewport size on this [`Device`].
    ///
    /// Note that this does not update any associated `Stylist`. For this you must call
    /// `Stylist::media_features_change_changed_style` and
    /// `Stylist::force_stylesheet_origins_dirty`.
    pub fn set_viewport_size(&mut self, viewport_size: Size2D<f32, CSSPixel>) {
        self.extra.media_data.viewport_size = viewport_size;
    }

    /// Returns the viewport size of the current device in app units, needed,
    /// among other things, to resolve viewport units.
    #[inline]
    pub fn au_viewport_size(&self) -> UntypedSize2D<Au> {
        Size2D::new(
            Au::from_f32_px(self.extra.media_data.viewport_size.width),
            Au::from_f32_px(self.extra.media_data.viewport_size.height),
        )
    }

    /// Like the above, but records that we've used viewport units.
    pub fn au_viewport_size_for_viewport_unit_resolution(
        &self,
        _: ViewportVariant,
    ) -> UntypedSize2D<Au> {
        self.used_viewport_size.store(true, Ordering::Relaxed);
        // Servo doesn't have dynamic UA interfaces that affect the viewport,
        // so we can just ignore the ViewportVariant.
        self.au_viewport_size()
    }

    /// Returns the number of app units per device pixel we're using currently.
    pub fn app_units_per_device_pixel(&self) -> i32 {
        (AU_PER_PX as f32 / self.extra.media_data.device_pixel_ratio.0) as i32
    }

    /// Returns the device pixel ratio, ignoring the full zoom factor.
    pub fn device_pixel_ratio_ignoring_full_zoom(&self) -> Scale<f32, CSSPixel, DevicePixel> {
        self.extra.media_data.device_pixel_ratio
    }

    /// Returns the device pixel ratio.
    pub fn device_pixel_ratio(&self) -> Scale<f32, CSSPixel, DevicePixel> {
        self.extra.media_data.device_pixel_ratio
    }

    /// Set a new device pixel ratio on this [`Device`].
    ///
    /// Note that this does not update any associated `Stylist`. For this you must call
    /// `Stylist::media_features_change_changed_style` and
    /// `Stylist::force_stylesheet_origins_dirty`.
    pub fn set_device_pixel_ratio(
        &mut self,
        device_pixel_ratio: Scale<f32, CSSPixel, DevicePixel>,
    ) {
        self.extra.media_data.device_pixel_ratio = device_pixel_ratio;
    }

    /// Set the device size on this [`Device`] (e.g. the available screen dimensions).
    ///
    /// Note that this does not update any associated `Stylist`. For this you must call
    /// `Stylist::media_features_change_changed_style` and
    /// `Stylist::force_stylesheet_origins_dirty`.
    pub fn set_device_size(&mut self, device_size: Size2D<f32, DevicePixel>) {
        self.extra.media_data.device_size = device_size;
    }

    /// Returns the screen size of the current device in app units.
    #[inline]
    pub fn device_size(&self) -> Size2D<f32, DevicePixel> {
        self.extra.media_data.device_size
    }

    /// Gets the size of the scrollbar in CSS pixels.
    pub fn scrollbar_inline_size(&self) -> CSSPixelLength {
        // TODO: implement this.
        CSSPixelLength::new(0.0)
    }

    /// Queries font metrics using the [`FontMetricsProvider`] interface.
    pub fn query_font_metrics(
        &self,
        vertical: bool,
        font: &Font,
        base_size: CSSPixelLength,
        flags: QueryFontMetricsFlags,
        track_usage: bool,
    ) -> FontMetrics {
        if track_usage {
            self.used_font_metrics.store(true, Ordering::Relaxed);
        }
        self.extra
            .font_metrics_provider
            .query_font_metrics(vertical, font, base_size, flags)
    }

    /// Set the media type on this [`Device`].
    ///
    /// Note that this does not update any associated `Stylist`. For this you must call
    /// `Stylist::media_features_change_changed_style` and
    /// `Stylist::force_stylesheet_origins_dirty`.
    pub fn set_media_type(&mut self, media_type: MediaType) {
        self.extra.media_data.media_type = media_type;
    }

    /// Return the media type of the current device.
    pub fn media_type(&self) -> MediaType {
        self.extra.media_data.media_type.clone()
    }

    /// Returns whether document colors are enabled.
    pub fn forced_colors(&self) -> ForcedColors {
        ForcedColors::None
    }

    /// Returns the default background color.
    pub fn default_background_color(&self) -> AbsoluteColor {
        AbsoluteColor::WHITE
    }

    /// Returns the default foreground color.
    pub fn default_color(&self) -> AbsoluteColor {
        AbsoluteColor::BLACK
    }

    /// Set the [`PrefersColorScheme`] value on this [`Device`].
    ///
    /// Note that this does not update any associated `Stylist`. For this you must call
    /// `Stylist::media_features_change_changed_style` and
    /// `Stylist::force_stylesheet_origins_dirty`.
    pub fn set_color_scheme(&mut self, new_color_scheme: PrefersColorScheme) {
        self.extra.media_data.prefers_color_scheme = new_color_scheme;
    }

    /// Returns the color scheme of this [`Device`].
    pub fn color_scheme(&self) -> PrefersColorScheme {
        self.extra.media_data.prefers_color_scheme
    }

    /// Set the [`PointerCapbabilities`] value for the primary pointer on this [`Device`]
    ///
    /// Note that this does not update any associated `Stylist`. For this you must call
    /// `Stylist::media_features_change_changed_style` and
    /// `Stylist::force_stylesheet_origins_dirty`.
    pub fn set_primary_pointer_capabilities(&mut self, capabilities: PointerCapabilities) {
        self.extra.media_data.primary_pointer_capabilities = capabilities;
    }

    /// Returns the pointer capabilities of this [`Device`].
    pub fn primary_pointer_capabilities(&self) -> PointerCapabilities {
        self.extra.media_data.primary_pointer_capabilities
    }

    /// Set the [`PointerCapbabilities`] value for all pointers on this [`Device`]
    ///
    /// Note that this does not update any associated `Stylist`. For this you must call
    /// `Stylist::media_features_change_changed_style` and
    /// `Stylist::force_stylesheet_origins_dirty`.
    pub fn set_all_pointer_capabilities(&mut self, capabilities: PointerCapabilities) {
        self.extra.media_data.all_pointer_capabilities = capabilities;
    }

    /// Returns the pointer capabilities of this [`Device`].
    pub fn all_pointer_capabilities(&self) -> PointerCapabilities {
        self.extra.media_data.all_pointer_capabilities
    }

    pub(crate) fn is_dark_color_scheme(&self, color_scheme_flags: ColorSchemeFlags) -> bool {
        // Inspired by
        // https://searchfox.org/firefox-main/rev/0a7f146ccac85b8f413264042dcd764028d419ec/widget/nsXPLookAndFeel.cpp#1296
        let supports_dark_mode = color_scheme_flags.contains(ColorSchemeFlags::DARK);
        let supports_light_mode = color_scheme_flags.contains(ColorSchemeFlags::LIGHT);

        // If only one is supported, then use dark mode if it was the supported one.
        if supports_dark_mode != supports_light_mode {
            return supports_dark_mode;
        }

        // If either both or none are supported, then use the preferred color scheme
        // to determine whether the user wants dark mode.
        return self.color_scheme() == PrefersColorScheme::Dark;
    }

    pub(crate) fn system_color(
        &self,
        system_color: SystemColor,
        color_scheme_flags: ColorSchemeFlags,
    ) -> AbsoluteColor {
        fn srgb(r: u8, g: u8, b: u8) -> AbsoluteColor {
            AbsoluteColor::srgb_legacy(r, g, b, 1f32)
        }

        // Refer to spec
        // <https://www.w3.org/TR/css-color-4/#css-system-colors>
        if self.is_dark_color_scheme(color_scheme_flags) {
            match system_color {
                SystemColor::Accentcolor => srgb(10, 132, 255),
                SystemColor::Accentcolortext => srgb(255, 255, 255),
                SystemColor::Activetext => srgb(255, 0, 0),
                SystemColor::Linktext => srgb(158, 158, 255),
                SystemColor::Visitedtext => srgb(208, 173, 240),
                SystemColor::Buttonborder
                // Deprecated system colors (CSS Color 4) mapped to Buttonborder.
                | SystemColor::Activeborder
                | SystemColor::Inactiveborder
                | SystemColor::Threeddarkshadow
                | SystemColor::Threedshadow
                | SystemColor::Windowframe => srgb(255, 255, 255),
                SystemColor::Buttonface
                // Deprecated system colors (CSS Color 4) mapped to Buttonface.
                | SystemColor::Buttonhighlight
                | SystemColor::Buttonshadow
                | SystemColor::Threedface
                | SystemColor::Threedhighlight
                | SystemColor::Threedlightshadow => srgb(107, 107, 107),
                SystemColor::Buttontext => srgb(245, 245, 245),
                SystemColor::Canvas
                // Deprecated system colors (CSS Color 4) mapped to Canvas.
                | SystemColor::Activecaption
                | SystemColor::Appworkspace
                | SystemColor::Background
                | SystemColor::Inactivecaption
                | SystemColor::Infobackground
                | SystemColor::Menu
                | SystemColor::Scrollbar
                | SystemColor::Window => srgb(30, 30, 30),
                SystemColor::Canvastext
                // Deprecated system colors (CSS Color 4) mapped to Canvastext.
                | SystemColor::Captiontext
                | SystemColor::Infotext
                | SystemColor::Menutext
                | SystemColor::Windowtext => srgb(232, 232, 232),
                SystemColor::Field => srgb(45, 45, 45),
                SystemColor::Fieldtext => srgb(240, 240, 240),
                SystemColor::Graytext
                // Deprecated system colors (CSS Color 4) mapped to Graytext.
                | SystemColor::Inactivecaptiontext => srgb(155, 155, 155),
                SystemColor::Highlight => srgb(38, 79, 120),
                SystemColor::Highlighttext => srgb(255, 255, 255),
                SystemColor::Mark => srgb(102, 92, 0),
                SystemColor::Marktext => srgb(255, 255, 255),
                SystemColor::Selecteditem => srgb(153, 200, 255),
                SystemColor::Selecteditemtext => srgb(59, 59, 59),
            }
        } else {
            match system_color {
                SystemColor::Accentcolor => srgb(0, 102, 204),
                SystemColor::Accentcolortext => srgb(255, 255, 255),
                SystemColor::Activetext => srgb(238, 0, 0),
                SystemColor::Linktext => srgb(0, 0, 238),
                SystemColor::Visitedtext => srgb(85, 26, 139),
                SystemColor::Buttonborder
                // Deprecated system colors (CSS Color 4) mapped to Buttonborder.
                | SystemColor::Activeborder
                | SystemColor::Inactiveborder
                | SystemColor::Threeddarkshadow
                | SystemColor::Threedshadow
                | SystemColor::Windowframe => srgb(169, 169, 169),
                SystemColor::Buttonface
                // Deprecated system colors (CSS Color 4) mapped to Buttonface.
                | SystemColor::Buttonhighlight
                | SystemColor::Buttonshadow
                | SystemColor::Threedface
                | SystemColor::Threedhighlight
                | SystemColor::Threedlightshadow => srgb(220, 220, 220),
                SystemColor::Buttontext => srgb(0, 0, 0),
                SystemColor::Canvas
                // Deprecated system colors (CSS Color 4) mapped to Canvas.
                | SystemColor::Activecaption
                | SystemColor::Appworkspace
                | SystemColor::Background
                | SystemColor::Inactivecaption
                | SystemColor::Infobackground
                | SystemColor::Menu
                | SystemColor::Scrollbar
                | SystemColor::Window => srgb(255, 255, 255),
                SystemColor::Canvastext
                // Deprecated system colors (CSS Color 4) mapped to Canvastext.
                | SystemColor::Captiontext
                | SystemColor::Infotext
                | SystemColor::Menutext
                | SystemColor::Windowtext => srgb(0, 0, 0),
                SystemColor::Field => srgb(255, 255, 255),
                SystemColor::Fieldtext => srgb(0, 0, 0),
                SystemColor::Graytext
                // Deprecated system colors (CSS Color 4) mapped to Graytext.
                | SystemColor::Inactivecaptiontext => srgb(109, 109, 109),
                SystemColor::Highlight => srgb(0, 65, 198),
                SystemColor::Highlighttext => srgb(0, 0, 0),
                SystemColor::Mark => srgb(255, 235, 59),
                SystemColor::Marktext => srgb(0, 0, 0),
                SystemColor::Selecteditem => srgb(0, 102, 204),
                SystemColor::Selecteditemtext => srgb(255, 255, 255),
            }
        }
    }

    /// Returns the current effective text zoom.
    #[inline]
    pub(super) fn text_zoom(&self) -> f32 {
        // (Servo doesn't do text-zoom)
        1.
    }

    /// Returns safe area insets
    pub fn safe_area_insets(&self) -> SideOffsets2D<f32, CSSPixel> {
        SideOffsets2D::zero()
    }

    /// Returns true if the given MIME type is supported
    pub fn is_supported_mime_type(&self, mime_type: &str) -> bool {
        match mime_type.parse::<Mime>() {
            Ok(m) => {
                // Keep this in sync with 'image_classifer' from
                // components/net/mime_classifier.rs
                m == mime::IMAGE_BMP
                    || m == mime::IMAGE_GIF
                    || m == mime::IMAGE_PNG
                    || m == mime::IMAGE_JPEG
                    || m == "image/x-icon"
                    || m == "image/webp"
            },
            _ => false,
        }
    }

    /// Return whether the document is a chrome document.
    #[inline]
    pub fn chrome_rules_enabled_for_document(&self) -> bool {
        false
    }

    /// Returns the link-parameters that have been set for this document.
    /// <https://drafts.csswg.org/css-link-params-1/>
    #[inline]
    pub fn link_parameters(&self) -> Option<&LinkParameters> {
        None
    }
}
