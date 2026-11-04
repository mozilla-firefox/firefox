//! This Source Code Form is subject to the terms of the Mozilla Public
//! License, v. 2.0. If a copy of the MPL was not distributed with this
//! file, You can obtain one at http://mozilla.org/MPL/2.0/.
//!
//! Glue for using Fontations in Gecko.

extern crate nsstring;
extern crate skrifa;
extern crate thin_vec;
use nsstring::nsCString;
use skrifa::charmap::MapVariant;
use skrifa::metrics::GlyphMetrics;
use skrifa::prelude::*;
use skrifa::raw::TableProvider;
use skrifa::raw::collections::int_set::Domain; // for Tag::to_u32()
use skrifa::raw::tables::os2::SelectionFlags;
use skrifa::string::StringId;
use std::slice;
use thin_vec::ThinVec;

/// Type used to represent a Skrifa FontRef in C++ as an opaque struct.
#[derive(Clone)]
pub struct SkrifaFontRef<'a>(skrifa::FontRef<'a>);

/// Create a SkrifaFontRef object for the given font data; returns null on failure.
#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_new<'a>(data: *const u8, length: usize) -> *mut SkrifaFontRef<'a> {
    let data = unsafe { slice::from_raw_parts(data, length) };
    if let Ok(font) = skrifa::FontRef::<'a>::new(data) {
        Box::into_raw(Box::new(SkrifaFontRef(font)))
    } else {
        std::ptr::null_mut() as *mut _
    }
}

/// Create a SkrifaFontRef object for the face with the given index in a collection; returns null on failure.
#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_new_from_index<'a>(
    data: *const u8,
    length: usize,
    index: u32,
) -> *mut SkrifaFontRef<'a> {
    let data = unsafe { slice::from_raw_parts(data, length) };
    if let Ok(font) = skrifa::FontRef::<'a>::from_index(data, index) {
        Box::into_raw(Box::new(SkrifaFontRef(font)))
    } else {
        std::ptr::null_mut() as *mut _
    }
}

/// Delete a SkrifaFontRef object.
#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_delete<'a>(font: *mut SkrifaFontRef) {
    if !font.is_null() {
        unsafe { drop(Box::from_raw(font)) };
    }
}

/// Map the given unicode codepoint to a glyph ID; returns 0 for unsupported codepoints.
#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_map_char_to_glyph(font: &SkrifaFontRef, unicode: u32) -> u32 {
    let charmap = font.0.charmap();
    charmap.map(unicode).unwrap_or(GlyphId::NOTDEF).into()
}

/// Get a name (identified by OpenType name ID) from a Skrifa font as a Gecko string.
/// Returns false if unable to find a name for the given name_id.
#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_get_preferred_name(
    font: &SkrifaFontRef,
    name_id: u16,
    ret_val: &mut nsCString,
) -> bool {
    if let Some(name) = font
        .0
        .localized_strings(StringId::new(name_id))
        .english_or_first()
    {
        *ret_val = name.to_string().into();
        return true;
    }
    false
}

/// Type to represent a reference to a table in a Skrifa font.
/// The data is immutable and remains owned by the Skrifa font ref.
#[repr(C)]
pub struct SkrifaFontTable {
    length: usize,
    data: *const u8,
}

/// Get a (read-only) reference to the table with the given tag.
/// Returns an empty (0-length) table if the tag is not present.
#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_get_table(font: &SkrifaFontRef, tag: u32) -> SkrifaFontTable {
    if let Some(data) = font.0.table_data(skrifa::Tag::from_u32(tag)) {
        SkrifaFontTable {
            length: data.len(),
            data: data.as_bytes().as_ptr(),
        }
    } else {
        SkrifaFontTable {
            length: 0,
            data: std::ptr::null(),
        }
    }
}

/// Check whether a given table is present.
/// Note that a table that appears in the directory, but has zero length,
/// is considered to be missing.
#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_has_table(font: &SkrifaFontRef, tag: u32) -> bool {
    font.0
        .table_data(skrifa::Tag::from_u32(tag))
        .is_some_and(|data| data.len() > 0)
}

// VARIATION SETTINGS
use style::gecko_bindings::structs::gfxFontVariation;
use style::values::generics::font::FontTag;

pub struct SkrifaLocation(skrifa::instance::Location);

#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_resolve_variations_to_location(
    font: &SkrifaFontRef,
    settings: &ThinVec<gfxFontVariation>,
) -> *mut SkrifaLocation {
    Box::into_raw(Box::new(SkrifaLocation(font.0.axes().location(
        settings.iter().map(|s| (Tag::from_u32(s.tag.0), s.value)),
    ))))
}

#[unsafe(no_mangle)]
pub extern "C" fn skrifa_location_delete(location: *mut SkrifaLocation) {
    if !location.is_null() {
        unsafe { drop(Box::from_raw(location)) };
    }
}

// VARIATION AXES AND INSTANCES
use style::gecko_bindings::structs::gfxFontVariationAxis;

#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_axes_count(font: &SkrifaFontRef) -> usize {
    font.0.axes().len()
}

#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_copy_axes(
    font: &SkrifaFontRef,
    axes: &mut ThinVec<gfxFontVariationAxis>,
    include_hidden: bool,
) -> usize {
    axes.extend(font.0.axes().iter().filter_map(|a| {
        if include_hidden || !a.is_hidden() {
            Some(gfxFontVariationAxis {
                mTag: a.tag().to_u32(),
                mName: font
                    .0
                    .localized_strings(a.name_id())
                    .english_or_first()
                    .map_or_else(|| nsCString::new(), |name| name.to_string().into()),
                mMinValue: a.min_value(),
                mMaxValue: a.max_value(),
                mDefaultValue: a.default_value(),
            })
        } else {
            None
        }
    }));
    axes.len()
}

#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_instances_count(font: &SkrifaFontRef) -> usize {
    font.0.named_instances().len()
}

#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_instance_settings_count(font: &SkrifaFontRef, index: usize) -> usize {
    font.0
        .named_instances()
        .get(index)
        .map_or_else(|| 0, |instance| instance.user_coords().count())
}

#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_copy_instance(
    font: &SkrifaFontRef,
    index: usize,
    name: &mut nsCString,
    settings: &mut ThinVec<gfxFontVariation>,
) -> bool {
    let instance = match font.0.named_instances().get(index) {
        Some(instance) => instance,
        None => return false,
    };
    *name = font
        .0
        .localized_strings(instance.subfamily_name_id())
        .english_or_first()
        .map_or_else(|| nsCString::new(), |name| name.to_string().into());
    settings.extend(instance.user_coords().enumerate().map(|(i, value)| {
        gfxFontVariation {
            tag: FontTag(
                font.0
                    .axes()
                    .get(i)
                    .map_or_else(|| 0, |axis| axis.tag().to_u32()),
            ),
            value,
        }
    }));
    true
}

// FONT METRICS for a specific size and location in variation space.
#[repr(C)]
pub struct SkrifaMetrics {
    scale_factor: f32,
    max_advance: f32,
    ave_char_width: f32,
    max_ascent: f32,
    max_descent: f32,
    external_leading: f32,
    // position & thickness of decoration lines; f32::NAN if not available.
    underline_offset: f32,
    underline_size: f32,
    strikeout_offset: f32,
    strikeout_size: f32,
    // x- and cap-height metrics; f32::NAN if not available.
    x_height: f32,
    cap_height: f32,
    // overall font bounding box; returns f32::NAN if not available.
    x_min: f32,
    y_min: f32,
    x_max: f32,
    y_max: f32,
}

#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_get_metrics(
    font: &SkrifaFontRef,
    size: f32,
    location: &SkrifaLocation,
    result: &mut SkrifaMetrics,
) {
    let instance_size = Size::new(size);
    let metrics = font.0.metrics(instance_size, &location.0);
    result.scale_factor = instance_size.linear_scale(metrics.units_per_em);
    result.max_advance = metrics.max_width.unwrap_or(size);
    result.ave_char_width = metrics.average_width.unwrap_or(size * 0.5);
    result.max_ascent = metrics.ascent;
    result.max_descent = metrics.descent;
    result.external_leading = metrics.leading;

    // Hack around broken Nimbus font metrics, see bug 2082557
    if font
        .0
        .localized_strings(StringId::FAMILY_NAME)
        .english_or_first()
        .is_some_and(|name| name.to_string().starts_with("Nimbus "))
        && let Ok(os2) = font.0.os2()
        && !os2
            .fs_selection()
            .contains(SelectionFlags::USE_TYPO_METRICS)
        && os2.s_typo_line_gap() > 0
        && os2.us_win_ascent() != 0
    {
        result.max_ascent = os2.us_win_ascent() as f32 * result.scale_factor;
        result.max_descent = -(os2.us_win_descent() as f32 * result.scale_factor);
        result.external_leading = 0.0;
    }

    // We return 0.0 for these metrics if unavailable; gfxFont::SanitizeMetrics
    // will fix them up to reasonable defaults.
    if let Some(underline) = metrics.underline {
        result.underline_offset = underline.offset;
        result.underline_size = underline.thickness;
    } else {
        result.underline_offset = 0.0;
        result.underline_size = 0.0;
    }
    if let Some(strikeout) = metrics.strikeout {
        result.strikeout_offset = strikeout.offset;
        result.strikeout_size = strikeout.thickness;
    } else {
        result.strikeout_offset = 0.0;
        result.strikeout_size = 0.0;
    }
    result.x_height = metrics.x_height.unwrap_or(0.0);
    result.cap_height = metrics.cap_height.unwrap_or(0.0);

    // Bounding box, or f32::NAN if unknown.
    if let Some(bounds) = metrics.bounds {
        result.x_min = bounds.x_min;
        result.y_min = bounds.y_min;
        result.x_max = bounds.x_max;
        result.y_max = bounds.y_max;
    } else {
        result.x_min = f32::NAN;
        result.y_min = f32::NAN;
        result.x_max = f32::NAN;
        result.y_max = f32::NAN;
    }
}

// GLYPH METRICS
pub struct SkrifaGlyphMetrics<'a>(skrifa::metrics::GlyphMetrics<'a>);

#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_create_glyph_metrics<'a>(
    font: &'a SkrifaFontRef<'a>,
    size: f32,
    location: &'a SkrifaLocation,
) -> *mut SkrifaGlyphMetrics<'a> {
    Box::into_raw(Box::new(SkrifaGlyphMetrics(GlyphMetrics::new(
        &font.0,
        Size::new(size),
        &location.0,
    ))))
}

#[unsafe(no_mangle)]
pub extern "C" fn skrifa_glyph_metrics_delete(metrics: *mut SkrifaGlyphMetrics) {
    if !metrics.is_null() {
        unsafe { drop(Box::from_raw(metrics)) };
    }
}

// Get glyph advance width; returns zero for out-of-range glyph ids.
#[unsafe(no_mangle)]
pub extern "C" fn skrifa_metrics_get_glyph_advance(
    glyph_metrics: &SkrifaGlyphMetrics,
    gid: u32,
) -> f32 {
    glyph_metrics
        .0
        .advance_width(GlyphId::new(gid))
        .unwrap_or(0.0)
}

// Expose glyph bounding box in gfx::Rect form for Gecko C++ code to use.
use style::gecko_bindings::structs::gfx;

// Wrapper for gfx::Rect so that we can implement the From trait here.
#[repr(transparent)]
pub struct BoundingBox(gfx::Rect);

impl From<skrifa::metrics::BoundingBox> for BoundingBox {
    fn from(bounds: skrifa::metrics::BoundingBox) -> Self {
        // Convert from Skrifa's BoundingBox to gfx::Rect's x/y/width/height fields.
        Self(gfx::Rect {
            x: bounds.x_min,
            y: bounds.y_min,
            width: bounds.x_max - bounds.x_min,
            height: bounds.y_max - bounds.y_min,
        })
    }
}

// Get glyph bounding box. Returns true if successful, false if bbox not available.
#[unsafe(no_mangle)]
pub extern "C" fn skrifa_metrics_get_glyph_bounds(
    glyph_metrics: &SkrifaGlyphMetrics,
    gid: u32,
    bbox: &mut BoundingBox,
) -> bool {
    if let Some(bounds) = glyph_metrics.0.bounds(GlyphId::new(gid)) {
        *bbox = bounds.into();
        true
    } else {
        false
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn skrifa_font_get_glyph_variant(
    font: &SkrifaFontRef,
    unicode: u32,
    variation: u32,
) -> u32 {
    let charmap = font.0.charmap();
    let glyph = match charmap.map_variant(unicode, variation) {
        None => GlyphId::NOTDEF,
        Some(MapVariant::UseDefault) => charmap.map(unicode).unwrap_or(GlyphId::NOTDEF),
        Some(MapVariant::Variant(id)) => id,
    };
    glyph.into()
}
