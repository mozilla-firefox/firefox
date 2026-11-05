use super::{hb::*, HbBlob};

use std::collections::HashMap;
use std::ffi::c_void;
use std::marker::PhantomData;
use std::ptr::null_mut;
use std::sync::atomic::{AtomicPtr, AtomicU32, Ordering};
use std::sync::{Arc, Mutex};

use read_fonts::model::{
    glyph::{
        bitmap::{BitmapGlyph, Origin},
        GlyphSource, GlyphSourceOrder,
    },
    metrics::{GlyphExtents, LineExtents, Scale, ScaledGlyphMetrics},
    Blob, Font, NormalizedCoord,
};
use read_fonts::types::{BoundingBox as FontBounds, F48Dot16, GlyphId};
use read_fonts::TableProvider;

#[cfg(feature = "draw")]
use skrifa::{DrawMemory, PathSink, ScalerFont};

#[cfg(feature = "paint")]
use read_fonts::model::glyph::{
    bitmap::Pixels,
    color::{Brush, Color, ColorStop, CompositeMode, Extend, PaintSink},
};
#[cfg(feature = "paint")]
use read_fonts::types::Matrix;

// A struct for storing your “fontations” data
#[repr(C)]
struct FontationsData {
    #[cfg(feature = "paint")]
    face_blob: Arc<HbBlob>,
    font: *mut hb_font_t,

    // Mutex for the below
    mutex: Mutex<()>,
    serial: AtomicU32,
    x_mult: f32,
    y_mult: f32,
    instance: Font,
    // Draws the instance's outlines in font units.
    #[cfg(feature = "draw")]
    scaler: ScalerFont,
}

impl FontationsData {
    unsafe fn from_hb_font(font: *mut hb_font_t) -> Option<Self> {
        let face_index = hb_face_get_index(hb_font_get_face(font));
        let face_blob = Arc::new(HbBlob(hb_face_reference_blob(hb_font_get_face(font))));
        let instance = Font::new(Blob::Shared(face_blob.clone()), face_index)?;

        let mut data = FontationsData {
            #[cfg(feature = "paint")]
            face_blob,
            font,
            mutex: Mutex::new(()),
            x_mult: 1.0,
            y_mult: 1.0,
            serial: AtomicU32::new(u32::MAX),
            #[cfg(feature = "draw")]
            scaler: outline_scaler(&instance),
            instance,
        };

        data.check_for_updates();

        Some(data)
    }

    unsafe fn _check_for_updates(&mut self) {
        let font_serial = hb_font_get_serial(self.font);
        let serial = self.serial.load(Ordering::Relaxed);
        if serial == font_serial {
            return;
        }

        let _lock = self.mutex.lock().unwrap();

        let mut x_scale: i32 = 0;
        let mut y_scale: i32 = 0;
        hb_font_get_scale(self.font, &mut x_scale, &mut y_scale);
        let upem = hb_face_get_upem(hb_font_get_face(self.font));
        self.x_mult = x_scale as f32 / upem as f32;
        self.y_mult = y_scale as f32 / upem as f32;

        let mut num_coords: u32 = 0;
        let coords = hb_font_get_var_coords_normalized(self.font, &mut num_coords);
        let coords = if coords.is_null() {
            &[]
        } else {
            std::slice::from_raw_parts(coords, num_coords as usize)
        };
        let current_coords = self.instance.normalized_coords();
        let axis_count = self
            .instance
            .tables()
            .fvar()
            .map(|fvar| fvar.axis_count() as usize)
            .unwrap_or(0);
        // Match the builder's padding and truncation, including its empty
        // representation of an all-default location.
        if (0..axis_count).any(|i| {
            current_coords
                .get(i)
                .copied()
                .unwrap_or(NormalizedCoord::ZERO)
                != NormalizedCoord::from_bits(coords.get(i).copied().unwrap_or(0) as i16)
        }) {
            self.instance = self
                .instance
                .instance_builder()
                .normalized_coords(coords.iter().map(|v| NormalizedCoord::from_bits(*v as i16)))
                .build();
            #[cfg(feature = "draw")]
            {
                self.scaler = outline_scaler(&self.instance);
            }
        }

        self.serial.store(font_serial, Ordering::Release);
    }
    fn check_for_updates(&mut self) {
        unsafe { self._check_for_updates() }
    }

    fn scale(&self) -> FontationsScale {
        FontationsScale {
            x_mult: self.x_mult,
            y_mult: self.y_mult,
        }
    }

    fn glyph_metrics(&self) -> ScaledGlyphMetrics<'_, 'static, FontationsScale> {
        self.instance.glyph_metrics().scaled(self.scale())
    }
}

/// A scaler that draws `instance` unhinted at one pixel per font unit.
///
/// A size of the units per em keeps the fractions that variations and
/// component scaling give the points, which drawing in font units rounds
/// away. Fonts that need hinting to draw correct contours are still drawn
/// unhinted.
#[cfg(feature = "draw")]
fn outline_scaler(instance: &Font) -> ScalerFont {
    ScalerFont::force_unhinted(instance.clone(), Some(f32::from(instance.units_per_em())))
}

extern "C" fn _hb_fontations_data_destroy(font_data: *mut c_void) {
    let _data = unsafe { Box::from_raw(font_data as *mut FontationsData) };
}

#[derive(Clone, Copy)]
struct FontationsScale {
    x_mult: f32,
    y_mult: f32,
}

impl Scale for FontationsScale {
    // Keep intermediate sums wide when combining HarfBuzz callback results.
    type Value = i64;

    fn add(a: i64, b: i64) -> i64 {
        a.saturating_add(b)
    }

    fn sub(a: i64, b: i64) -> i64 {
        a.saturating_sub(b)
    }

    fn half(value: i64) -> i64 {
        value >> 1
    }

    fn scale_x(&self, value: F48Dot16) -> i64 {
        round_to_position(value.to_f32() * self.x_mult) as i64
    }

    fn scale_y(&self, value: F48Dot16) -> i64 {
        round_to_position(value.to_f32() * self.y_mult) as i64
    }

    fn scale_glyph_extents(&self, extents: GlyphExtents<F48Dot16>) -> GlyphExtents<i64> {
        let x_bearing = self.scale_x(extents.x_bearing);
        let y_bearing = self.scale_y(extents.y_bearing);
        GlyphExtents {
            x_bearing,
            y_bearing,
            width: self.scale_x(extents.x_bearing.saturating_add(extents.width)) - x_bearing,
            height: y_bearing - self.scale_y(extents.y_bearing.saturating_sub(extents.height)),
        }
    }

    fn scale_rect(&self, bounds: FontBounds<F48Dot16>) -> FontBounds<i64> {
        FontBounds {
            x_min: self.scale_x(bounds.x_min),
            y_min: self.scale_y(bounds.y_min),
            x_max: self.scale_x(bounds.x_max),
            y_max: self.scale_y(bounds.y_max),
        }
    }
}

fn hb_position(value: i64) -> hb_position_t {
    value.clamp(hb_position_t::MIN as i64, hb_position_t::MAX as i64) as hb_position_t
}

#[inline]
fn round_to_position(value: f32) -> hb_position_t {
    // Bias in f64 so values just below a half-integer do not round up.
    let value = f64::from(value);
    (value + 0.5f64.copysign(value)) as hb_position_t
}

fn font_line_extents(font: *mut hb_font_t) -> LineExtents<i64> {
    let mut extents: hb_font_extents_t = unsafe { std::mem::zeroed() };
    unsafe {
        hb_font_get_extents_for_direction(font, hb_direction_t_HB_DIRECTION_LTR, &mut extents);
    }
    LineExtents {
        ascender: extents.ascender as i64,
        descender: extents.descender as i64,
    }
}

// SAFETY: The selected slot must be readable for a T. Alignment is not required.
unsafe fn struct_at_offset<T: Copy>(first: *const T, index: u32, stride: u32) -> T {
    first
        .cast::<u8>()
        .add(index as usize * stride as usize)
        .cast::<T>()
        .read_unaligned()
}

// SAFETY: The selected slot must be writable for a T. Alignment is not required.
unsafe fn write_struct_at_offset<T>(first: *mut T, index: u32, stride: u32, value: T) {
    first
        .cast::<u8>()
        .add(index as usize * stride as usize)
        .cast::<T>()
        .write_unaligned(value);
}

// Constructed only by with_strided_glyphs: outputs are aligned, disjoint,
// and exclusively borrowed for 'a, and cannot alias the input slots.
struct StridedGlyphs<'a> {
    index: u32,
    count: u32,
    first_glyph: *const hb_codepoint_t,
    glyph_stride: u32,
    first_value: *mut hb_position_t,
    value_stride: u32,
    lifetime: PhantomData<&'a mut hb_position_t>,
}

impl<'a> Iterator for StridedGlyphs<'a> {
    type Item = (GlyphId, &'a mut hb_position_t);

    fn next(&mut self) -> Option<Self::Item> {
        if self.index == self.count {
            return None;
        }
        let i = self.index;
        self.index += 1;
        unsafe {
            Some((
                GlyphId::new(struct_at_offset(self.first_glyph, i, self.glyph_stride)),
                &mut *self
                    .first_value
                    .cast::<u8>()
                    .add(i as usize * self.value_stride as usize)
                    .cast::<hb_position_t>(),
            ))
        }
    }
}

// Conservatively check the used slots, allowing disjoint interleaved fields.
fn strided_slots_overlap<T, U>(
    count: u32,
    first_a: *const T,
    stride_a: u32,
    first_b: *const U,
    stride_b: u32,
) -> bool {
    if count == 0 {
        return false;
    }
    let end_a =
        first_a as usize + (count - 1) as usize * stride_a as usize + std::mem::size_of::<T>();
    let end_b =
        first_b as usize + (count - 1) as usize * stride_b as usize + std::mem::size_of::<U>();
    if end_a <= first_b as usize || end_b <= first_a as usize {
        return false;
    }
    if stride_a == stride_b && stride_a != 0 {
        let gap = (first_a as usize).abs_diff(first_b as usize) % stride_a as usize;
        let (size_low, size_high) = if first_a as usize <= first_b as usize {
            (std::mem::size_of::<T>(), std::mem::size_of::<U>())
        } else {
            (std::mem::size_of::<U>(), std::mem::size_of::<T>())
        };
        // Both gaps must fit the fields, including across record boundaries.
        if gap >= size_low && stride_a as usize - gap >= size_high {
            return false;
        }
    }
    true
}

// SAFETY: All input/output slots must be readable/writable for this call.
// The callback must access these buffers only through the supplied iterator.
unsafe fn with_strided_glyphs(
    count: u32,
    first_glyph: *const hb_codepoint_t,
    glyph_stride: u32,
    first_value: *mut hb_position_t,
    value_stride: u32,
    mut batch: impl for<'a> FnMut(StridedGlyphs<'a>),
) {
    if count == 0 {
        return;
    }
    let disjoint =
        !strided_slots_overlap(count, first_glyph, glyph_stride, first_value, value_stride);
    if disjoint
        && first_value.is_aligned()
        && (count == 1
            || (value_stride as usize >= std::mem::size_of::<hb_position_t>()
                && (value_stride as usize).is_multiple_of(std::mem::align_of::<hb_position_t>())))
    {
        batch(StridedGlyphs {
            index: 0,
            count,
            first_glyph,
            glyph_stride,
            first_value,
            value_stride,
            lifetime: PhantomData,
        });
    } else {
        // Unaligned or overlapping slots cannot yield simultaneous &mut
        // references. Use one stack output at a time and scatter it back.
        for i in 0..count {
            let mut value = 0;
            batch(StridedGlyphs {
                index: 0,
                count: 1,
                first_glyph: first_glyph
                    .cast::<u8>()
                    .add(i as usize * glyph_stride as usize)
                    .cast(),
                glyph_stride: 0,
                first_value: &mut value,
                value_stride: 0,
                lifetime: PhantomData,
            });
            write_struct_at_offset(first_value, i, value_stride, value);
        }
    }
}

/// The ppem that selects a bitmap strike: the larger of the font's, or
/// `None`, which selects the largest strike, when it has none.
fn bitmap_ppem(font: *mut hb_font_t) -> Option<f32> {
    let mut x_ppem = 0;
    let mut y_ppem = 0;
    unsafe { hb_font_get_ppem(font, &mut x_ppem, &mut y_ppem) };
    let ppem = x_ppem.max(y_ppem);
    (ppem != 0).then_some(ppem as f32)
}

/// The bitmap of `glyph` for `ppem` from the first of `sources` that has one.
fn bitmap_glyph(
    instance: &Font,
    sources: [GlyphSource; 2],
    glyph: GlyphId,
    ppem: Option<f32>,
) -> Option<BitmapGlyph<'_>> {
    sources.into_iter().find_map(|source| {
        instance
            .bitmap_strikes_with(GlyphSourceOrder::new(&[source]))
            .select_bitmap(glyph, ppem)
    })
}

/// The sources outside bitmaps that measure a glyph's ink, in order.
const NON_BITMAP_EXTENTS: GlyphSourceOrder = GlyphSourceOrder::new(&[
    GlyphSource::Colr,
    GlyphSource::Varc,
    GlyphSource::Glyf,
    GlyphSource::Cff2,
    GlyphSource::Cff,
]);

fn bitmap_glyph_extents(
    data: &FontationsData,
    bitmap_glyph: &BitmapGlyph,
) -> Option<hb_glyph_extents_t> {
    let (ppem_x, ppem_y) = (bitmap_glyph.ppem_x(), bitmap_glyph.ppem_y());
    if ppem_x == 0 || ppem_y == 0 || bitmap_glyph.width() >= 65536 || bitmap_glyph.height() >= 65536
    {
        return None;
    }

    let upem = data.instance.units_per_em() as f32;
    let x_scale = upem / ppem_x as f32;
    let y_scale = upem / ppem_y as f32;

    let x_bearing =
        (bitmap_glyph.x_bearing() as f32 + bitmap_glyph.inner_x_bearing() as f32 * x_scale).round();
    let inner_y = bitmap_glyph.inner_y_bearing() as f32
        + if bitmap_glyph.origin() == Origin::BottomLeft {
            bitmap_glyph.height() as f32
        } else {
            0.0
        };
    let y_bearing = (bitmap_glyph.y_bearing() as f32 + inner_y * y_scale).round();
    let width = (bitmap_glyph.width() as f32 * x_scale).round();
    let height = -(bitmap_glyph.height() as f32 * y_scale).round();

    let scaled_x_bearing = (x_bearing * data.x_mult).floor() as hb_position_t;
    let scaled_y_bearing = (y_bearing * data.y_mult).floor() as hb_position_t;
    let scaled_x_end = ((x_bearing + width) * data.x_mult).ceil() as hb_position_t;
    let scaled_y_end = ((y_bearing + height) * data.y_mult).ceil() as hb_position_t;

    Some(hb_glyph_extents_t {
        x_bearing: scaled_x_bearing,
        y_bearing: scaled_y_bearing,
        width: scaled_x_end.saturating_sub(scaled_x_bearing),
        height: scaled_y_end.saturating_sub(scaled_y_bearing),
    })
}

extern "C" fn _hb_fontations_get_nominal_glyphs(
    _font: *mut hb_font_t,
    font_data: *mut ::std::os::raw::c_void,
    count: ::std::os::raw::c_uint,
    first_unicode: *const hb_codepoint_t,
    unicode_stride: ::std::os::raw::c_uint,
    first_glyph: *mut hb_codepoint_t,
    glyph_stride: ::std::os::raw::c_uint,
    _user_data: *mut ::std::os::raw::c_void,
) -> ::std::os::raw::c_uint {
    let data = unsafe { &*(font_data as *const FontationsData) };
    let char_map = data.instance.charmap();

    for i in 0..count {
        let unicode = unsafe { struct_at_offset(first_unicode, i, unicode_stride) };
        let Some(glyph) = char_map.map_unicode(unicode) else {
            return i;
        };
        let glyph_id = glyph.to_u32() as hb_codepoint_t;
        unsafe { write_struct_at_offset(first_glyph, i, glyph_stride, glyph_id) };
    }

    count
}
extern "C" fn _hb_fontations_get_variation_glyph(
    _font: *mut hb_font_t,
    font_data: *mut ::std::os::raw::c_void,
    unicode: hb_codepoint_t,
    variation_selector: hb_codepoint_t,
    glyph: *mut hb_codepoint_t,
    _user_data: *mut ::std::os::raw::c_void,
) -> hb_bool_t {
    let data = unsafe { &*(font_data as *const FontationsData) };

    match data
        .instance
        .charmap()
        .map_unicode_variant(unicode, variation_selector)
    {
        Some(glyph_id) => {
            unsafe { *glyph = glyph_id.to_u32() as hb_codepoint_t };
            true as hb_bool_t
        }
        None => false as hb_bool_t,
    }
}

extern "C" fn _hb_fontations_get_glyph_h_advances(
    _font: *mut hb_font_t,
    font_data: *mut ::std::os::raw::c_void,
    count: ::std::os::raw::c_uint,
    first_glyph: *const hb_codepoint_t,
    glyph_stride: ::std::os::raw::c_uint,
    first_advance: *mut hb_position_t,
    advance_stride: ::std::os::raw::c_uint,
    _user_data: *mut ::std::os::raw::c_void,
) {
    let data = unsafe { &mut *(font_data as *mut FontationsData) };
    data.check_for_updates();

    unsafe {
        with_strided_glyphs(
            count,
            first_glyph,
            glyph_stride,
            first_advance,
            advance_stride,
            |glyphs| {
                data.instance.glyph_metrics().h_advance_batched(
                    // Skrifa rounded advances to design units before applying our scale.
                    |advance| round_to_position(advance.to_i32() as f32 * data.x_mult),
                    glyphs,
                );
            },
        );
    }
}

extern "C" fn _hb_fontations_get_glyph_v_advances(
    font: *mut hb_font_t,
    font_data: *mut ::std::os::raw::c_void,
    count: ::std::os::raw::c_uint,
    first_glyph: *const hb_codepoint_t,
    glyph_stride: ::std::os::raw::c_uint,
    first_advance: *mut hb_position_t,
    advance_stride: ::std::os::raw::c_uint,
    _user_data: *mut ::std::os::raw::c_void,
) {
    let data = unsafe { &mut *(font_data as *mut FontationsData) };
    data.check_for_updates();

    let line = font_line_extents(font);
    unsafe {
        with_strided_glyphs(
            count,
            first_glyph,
            glyph_stride,
            first_advance,
            advance_stride,
            |glyphs| {
                data.glyph_metrics()
                    .with_line_extents(Some(line))
                    .v_advance_batched(|advance| hb_position(advance).saturating_neg(), glyphs);
            },
        );
    }
}

extern "C" fn _hb_fontations_get_glyph_v_origins(
    font: *mut hb_font_t,
    font_data: *mut ::std::os::raw::c_void,
    count: ::std::os::raw::c_uint,
    first_glyph: *const hb_codepoint_t,
    glyph_stride: ::std::os::raw::c_uint,
    first_x: *mut hb_position_t,
    x_stride: ::std::os::raw::c_uint,
    first_y: *mut hb_position_t,
    y_stride: ::std::os::raw::c_uint,
    _user_data: *mut ::std::os::raw::c_void,
) -> hb_bool_t {
    // Preserve scalar write order for aliased buffers, while batching disjoint
    // glyph and position fields in the same interleaved records.
    if (count > 1 && (x_stride as usize) < std::mem::size_of::<hb_position_t>())
        || strided_slots_overlap(count, first_glyph, glyph_stride, first_x, x_stride)
        || strided_slots_overlap(count, first_glyph, glyph_stride, first_y, y_stride)
        || strided_slots_overlap(count, first_x, x_stride, first_y, y_stride)
    {
        for i in 0..count {
            let glyph = unsafe { struct_at_offset(first_glyph, i, glyph_stride) };
            let (mut x, mut y) = (0, 0);
            _hb_fontations_get_glyph_v_origins(
                font, font_data, 1, &glyph, 0, &mut x, 0, &mut y, 0, _user_data,
            );
            unsafe {
                write_struct_at_offset(first_x, i, x_stride, x);
                write_struct_at_offset(first_y, i, y_stride, y);
            }
        }
        return true as hb_bool_t;
    }

    let data = unsafe { &mut *(font_data as *mut FontationsData) };
    data.check_for_updates();

    unsafe {
        hb_font_get_glyph_h_advances(font, count, first_glyph, glyph_stride, first_x, x_stride);
        for i in 0..count {
            let x = struct_at_offset(first_x, i, x_stride) / 2;
            write_struct_at_offset(first_x, i, x_stride, x);
        }
    }

    let line = font_line_extents(font);
    let extents = |glyph: GlyphId| {
        let mut extents: hb_glyph_extents_t = unsafe { std::mem::zeroed() };
        if unsafe { hb_font_get_glyph_extents(font, glyph.to_u32(), &mut extents) } == 0 {
            return None;
        }
        Some(GlyphExtents {
            x_bearing: extents.x_bearing as i64,
            y_bearing: extents.y_bearing as i64,
            width: extents.width as i64,
            height: -(extents.height as i64),
        })
    };
    let metrics = data
        .glyph_metrics()
        .with_line_extents(Some(line))
        .with_glyph_extents(Some(&extents));
    unsafe {
        with_strided_glyphs(
            count,
            first_glyph,
            glyph_stride,
            first_y,
            y_stride,
            |glyphs| {
                metrics.v_origin_y_batched(hb_position, glyphs);
            },
        );
    }

    true as hb_bool_t
}

extern "C" fn _hb_fontations_get_glyph_extents(
    _font: *mut hb_font_t,
    font_data: *mut ::std::os::raw::c_void,
    glyph: hb_codepoint_t,
    extents: *mut hb_glyph_extents_t,
    _user_data: *mut ::std::os::raw::c_void,
) -> hb_bool_t {
    let data = unsafe { &mut *(font_data as *mut FontationsData) };
    data.check_for_updates();

    let glyph_id = GlyphId::new(glyph);

    let bitmap = bitmap_glyph(
        &data.instance,
        [GlyphSource::Sbix, GlyphSource::Cbdt],
        glyph_id,
        bitmap_ppem(_font),
    );
    if let Some(bitmap) = bitmap {
        let Some(bitmap_extents) = bitmap_glyph_extents(data, &bitmap) else {
            return false as hb_bool_t;
        };
        unsafe { *extents = bitmap_extents };
        return true as hb_bool_t;
    }

    let Some(glyph_extents) = data
        .instance
        .glyph_metrics()
        .with_extents_source_order(NON_BITMAP_EXTENTS)
        .scaled(data.scale())
        .extents(glyph_id)
    else {
        return false as hb_bool_t;
    };
    unsafe {
        *extents = hb_glyph_extents_t {
            x_bearing: hb_position(glyph_extents.x_bearing),
            y_bearing: hb_position(glyph_extents.y_bearing),
            width: hb_position(glyph_extents.width),
            height: hb_position(-glyph_extents.height),
        };
    }

    true as hb_bool_t
}

extern "C" fn _hb_fontations_get_font_h_extents(
    _font: *mut hb_font_t,
    font_data: *mut ::std::os::raw::c_void,
    extents: *mut hb_font_extents_t,
    _user_data: *mut ::std::os::raw::c_void,
) -> hb_bool_t {
    let data = unsafe { &mut *(font_data as *mut FontationsData) };
    data.check_for_updates();

    let line = data
        .instance
        .metrics()
        .scaled(data.scale())
        .h_line()
        .unwrap_or_default();

    unsafe {
        (*extents).ascender = hb_position(line.ascender);
        (*extents).descender = hb_position(line.descender);
        (*extents).line_gap = hb_position(line.line_gap);
    }

    true as hb_bool_t
}

#[cfg(feature = "draw")]
struct HbPen {
    x_mult: f32,
    y_mult: f32,
    draw_state: *mut hb_draw_state_t,
    draw_funcs: *mut hb_draw_funcs_t,
    draw_data: *mut c_void,
}

#[cfg(feature = "draw")]
impl PathSink for HbPen {
    fn move_to(&mut self, x: f32, y: f32) {
        unsafe {
            hb_draw_move_to(
                self.draw_funcs,
                self.draw_data,
                self.draw_state,
                x * self.x_mult,
                y * self.y_mult,
            );
        }
    }
    fn line_to(&mut self, x: f32, y: f32) {
        unsafe {
            hb_draw_line_to(
                self.draw_funcs,
                self.draw_data,
                self.draw_state,
                x * self.x_mult,
                y * self.y_mult,
            );
        }
    }
    fn quad_to(&mut self, x1: f32, y1: f32, x: f32, y: f32) {
        unsafe {
            hb_draw_quadratic_to(
                self.draw_funcs,
                self.draw_data,
                self.draw_state,
                x1 * self.x_mult,
                y1 * self.y_mult,
                x * self.x_mult,
                y * self.y_mult,
            );
        }
    }
    fn curve_to(&mut self, x1: f32, y1: f32, x2: f32, y2: f32, x: f32, y: f32) {
        unsafe {
            hb_draw_cubic_to(
                self.draw_funcs,
                self.draw_data,
                self.draw_state,
                x1 * self.x_mult,
                y1 * self.y_mult,
                x2 * self.x_mult,
                y2 * self.y_mult,
                x * self.x_mult,
                y * self.y_mult,
            );
        }
    }
    fn close(&mut self) {
        unsafe {
            hb_draw_close_path(self.draw_funcs, self.draw_data, self.draw_state);
        }
    }
}

#[cfg(feature = "draw")]
extern "C" fn _hb_fontations_draw_glyph_or_fail(
    _font: *mut hb_font_t,
    font_data: *mut ::std::os::raw::c_void,
    glyph: hb_codepoint_t,
    draw_funcs: *mut hb_draw_funcs_t,
    draw_data: *mut ::std::os::raw::c_void,
    _user_data: *mut ::std::os::raw::c_void,
) -> hb_bool_t {
    let data = unsafe { &mut *(font_data as *mut FontationsData) };
    data.check_for_updates();

    let Some(outline) = data.scaler.outline(GlyphId::new(glyph)) else {
        return false as hb_bool_t;
    };

    let mut draw_state = unsafe { std::mem::zeroed::<hb_draw_state_t>() };

    let mut pen = HbPen {
        x_mult: data.x_mult,
        y_mult: data.y_mult,
        draw_state: &mut draw_state,
        draw_funcs,
        draw_data,
    };

    let _ = outline.draw(DrawMemory::Scratch, &mut pen);
    true as hb_bool_t
}

#[cfg(feature = "paint")]
struct HbColorPainter<'a> {
    font: *mut hb_font_t,
    paint_funcs: *mut hb_paint_funcs_t,
    paint_data: *mut c_void,
    color_records: &'a [Color],
    foreground: hb_color_t,
    is_glyph_clip: u64,
    clip_depth: u32,
}

#[cfg(feature = "paint")]
impl HbColorPainter<'_> {
    fn lookup_color(&self, color_index: u16, alpha: f32) -> hb_color_t {
        if color_index == 0xFFFF {
            // Apply alpha to foreground color
            return ((self.foreground & 0xFFFFFF00)
                | (((self.foreground & 0xFF) as f32 * alpha).round() as u32))
                as hb_color_t;
        }

        let c = self.color_records.get(color_index as usize);
        if let Some(c) = c {
            (((c.blue as u32) << 24)
                | ((c.green as u32) << 16)
                | ((c.red as u32) << 8)
                | ((c.alpha as f32 * alpha).round() as u32)) as hb_color_t
        } else {
            0 as hb_color_t
        }
    }

    fn make_color_line(&self, color_line: &ColorLineData) -> hb_color_line_t {
        let mut cl = unsafe { std::mem::zeroed::<hb_color_line_t>() };
        cl.data = color_line as *const ColorLineData as *mut ::std::os::raw::c_void;
        cl.get_color_stops = Some(_hb_fontations_get_color_stops);
        cl.get_extend = Some(_hb_fontations_get_extend);
        cl
    }
}

#[cfg(feature = "paint")]
struct ColorLineData<'a> {
    painter: &'a HbColorPainter<'a>,
    color_stops: &'a [ColorStop],
    extend: Extend,
}
#[cfg(feature = "paint")]
extern "C" fn _hb_fontations_get_color_stops(
    _color_line: *mut hb_color_line_t,
    color_line_data: *mut ::std::os::raw::c_void,
    start: ::std::os::raw::c_uint,
    count_out: *mut ::std::os::raw::c_uint,
    color_stops_out: *mut hb_color_stop_t,
    _user_data: *mut ::std::os::raw::c_void,
) -> ::std::os::raw::c_uint {
    let color_line_data = unsafe { &*(color_line_data as *const ColorLineData) };
    let color_stops = &color_line_data.color_stops;
    if count_out.is_null() {
        return color_stops.len() as u32;
    }
    let count = unsafe { *count_out };
    for i in 0..count {
        let Some(stop) = color_stops.get(start as usize + i as usize) else {
            unsafe {
                *count_out = i;
            };
            break;
        };
        unsafe {
            *(color_stops_out.offset(i as isize)) = hb_color_stop_t {
                offset: stop.offset,
                color: color_line_data
                    .painter
                    .lookup_color(stop.palette_index, stop.alpha),
                is_foreground: (stop.palette_index == 0xFFFF) as hb_bool_t,
            };
        }
    }
    color_stops.len() as u32
}
#[cfg(feature = "paint")]
extern "C" fn _hb_fontations_get_extend(
    _color_line: *mut hb_color_line_t,
    color_line_data: *mut ::std::os::raw::c_void,
    _user_data: *mut ::std::os::raw::c_void,
) -> hb_paint_extend_t {
    let color_line_data = unsafe { &*(color_line_data as *const ColorLineData) };
    color_line_data.extend as hb_paint_extend_t // They are the same
}

#[cfg(feature = "paint")]
pub fn _hb_fontations_unreduce_anchors(
    x0: f32,
    y0: f32,
    x1: f32,
    y1: f32,
) -> (f32, f32, f32, f32, f32, f32) {
    /* Returns (x0, y0, x1, y1, x2, y2) such that the original
     * `_hb_cairo_reduce_anchors` would produce (xx0, yy0, xx1, yy1)
     * as outputs.
     * The OT spec has the following wording; we just need to
     * invert that operation here:
     *
     * Note: An implementation can derive a single vector, from p₀ to a point p₃, by computing the
     * orthogonal projection of the vector from p₀ to p₁ onto a line perpendicular to line p₀p₂ and
     * passing through p₀ to obtain point p₃. The linear gradient defined using p₀, p₁ and p₂ as
     * described above is functionally equivalent to a linear gradient defined by aligning stop
     * offset 0 to p₀ and aligning stop offset 1.0 to p₃, with each color projecting on either side
     * of that line in a perpendicular direction. This specification uses three points, p₀, p₁ and
     * p₂, as that provides greater flexibility in controlling the placement and rotation of the
     * gradient, as well as variations thereof.
     */

    let dx = x1 - x0;
    let dy = y1 - y0;

    (x0, y0, x1, y1, x0 + dy, y0 - dx)
}

#[cfg(feature = "paint")]
impl PaintSink for HbColorPainter<'_> {
    fn push_transform(&mut self, transform: Matrix<f32>) {
        unsafe {
            hb_paint_push_transform(
                self.paint_funcs,
                self.paint_data,
                transform.xx,
                transform.yx,
                transform.xy,
                transform.yy,
                transform.dx,
                transform.dy,
            );
        }
    }
    fn pop_transform(&mut self) {
        unsafe {
            hb_paint_pop_transform(self.paint_funcs, self.paint_data);
        }
    }
    fn fill_glyph(
        &mut self,
        glyph_id: GlyphId,
        brush_transform: Option<Matrix<f32>>,
        brush: Brush<'_>,
    ) {
        unsafe {
            hb_paint_push_inverse_font_transform(self.paint_funcs, self.paint_data, self.font);
        }

        if brush_transform.is_none() {
            if let Brush::Solid {
                palette_index: color_index,
                alpha,
            } = brush
            {
                let is_foreground = color_index == 0xFFFF;
                let color = self.lookup_color(color_index, alpha);
                unsafe {
                    hb_paint_fill_glyph(
                        self.paint_funcs,
                        self.paint_data,
                        glyph_id.to_u32() as hb_codepoint_t,
                        self.font,
                        is_foreground as hb_bool_t,
                        color,
                    );
                }
                self.pop_transform();
                return;
            }
        }

        unsafe {
            hb_paint_push_clip_glyph(
                self.paint_funcs,
                self.paint_data,
                glyph_id.to_u32() as hb_codepoint_t,
                self.font,
            );
            hb_paint_push_font_transform(self.paint_funcs, self.paint_data, self.font);
        }
        if let Some(wrap_in_transform) = brush_transform {
            self.push_transform(wrap_in_transform);
            self.fill(brush);
            self.pop_transform();
        } else {
            self.fill(brush);
        }
        self.pop_transform();
        unsafe {
            hb_paint_pop_clip(self.paint_funcs, self.paint_data);
        }
        self.pop_transform();
    }
    fn push_clip_glyph(&mut self, glyph_id: GlyphId) {
        if self.clip_depth < 64 {
            self.is_glyph_clip |= 1 << self.clip_depth;
            self.clip_depth += 1;
        } else {
            return;
        }
        unsafe {
            hb_paint_push_inverse_font_transform(self.paint_funcs, self.paint_data, self.font);
            hb_paint_push_clip_glyph(
                self.paint_funcs,
                self.paint_data,
                glyph_id.to_u32() as hb_codepoint_t,
                self.font,
            );
            hb_paint_push_font_transform(self.paint_funcs, self.paint_data, self.font);
        }
    }
    fn push_clip_box(&mut self, bbox: FontBounds<f32>) {
        if self.clip_depth < 64 {
            self.is_glyph_clip &= !(1 << self.clip_depth);
            self.clip_depth += 1;
        } else {
            return;
        }
        unsafe {
            hb_paint_push_clip_rectangle(
                self.paint_funcs,
                self.paint_data,
                bbox.x_min,
                bbox.y_min,
                bbox.x_max,
                bbox.y_max,
            );
        }
    }
    fn pop_clip(&mut self) {
        if self.clip_depth > 0 {
            self.clip_depth -= 1;
        } else {
            return;
        }
        unsafe {
            if (self.is_glyph_clip & (1 << self.clip_depth)) != 0 {
                hb_paint_pop_transform(self.paint_funcs, self.paint_data);
            }
            hb_paint_pop_clip(self.paint_funcs, self.paint_data);
            if (self.is_glyph_clip & (1 << self.clip_depth)) != 0 {
                hb_paint_pop_transform(self.paint_funcs, self.paint_data);
            }
        }
    }
    fn fill(&mut self, brush: Brush) {
        match brush {
            Brush::Solid {
                palette_index: color_index,
                alpha,
            } => {
                let is_foreground = color_index == 0xFFFF;
                unsafe {
                    hb_paint_color(
                        self.paint_funcs,
                        self.paint_data,
                        is_foreground as hb_bool_t,
                        self.lookup_color(color_index, alpha),
                    );
                }
            }
            Brush::LinearGradient {
                color_stops,
                extend,
                p0,
                p1,
            } => {
                let color_stops = ColorLineData {
                    painter: self,
                    color_stops,
                    extend,
                };
                let mut color_line = self.make_color_line(&color_stops);

                let (x0, y0, x1, y1, x2, y2) =
                    _hb_fontations_unreduce_anchors(p0.x, p0.y, p1.x, p1.y);

                unsafe {
                    hb_paint_linear_gradient(
                        self.paint_funcs,
                        self.paint_data,
                        &mut color_line,
                        x0,
                        y0,
                        x1,
                        y1,
                        x2,
                        y2,
                    );
                }
            }
            Brush::RadialGradient {
                color_stops,
                extend,
                c0,
                r0,
                c1,
                r1,
            } => {
                let color_stops = ColorLineData {
                    painter: self,
                    color_stops,
                    extend,
                };
                let mut color_line = self.make_color_line(&color_stops);
                unsafe {
                    hb_paint_radial_gradient(
                        self.paint_funcs,
                        self.paint_data,
                        &mut color_line,
                        c0.x,
                        c0.y,
                        r0,
                        c1.x,
                        c1.y,
                        r1,
                    );
                }
            }
            Brush::SweepGradient {
                color_stops,
                extend,
                c0,
                start_angle,
                end_angle,
            } => {
                let color_stops = ColorLineData {
                    painter: self,
                    color_stops,
                    extend,
                };
                let mut color_line = self.make_color_line(&color_stops);
                // Skrifa has this gem, so we swap end_angle and start_angle
                // when passing to our API:
                //
                //  * Convert angles and stops from counter-clockwise to clockwise
                //  * for the shader if the gradient is not already reversed due to
                //  * start angle being larger than end angle.
                //
                //  Undo that.
                let (start_angle, end_angle) = (360. - start_angle, 360. - end_angle);
                let start_angle = start_angle.to_radians();
                let end_angle = end_angle.to_radians();
                unsafe {
                    hb_paint_sweep_gradient(
                        self.paint_funcs,
                        self.paint_data,
                        &mut color_line,
                        c0.x,
                        c0.y,
                        start_angle,
                        end_angle,
                    );
                }
            }
        }
    }
    fn push_layer(&mut self, mode: CompositeMode) {
        let mode = mode as hb_paint_composite_mode_t;
        unsafe {
            hb_paint_push_group_for(self.paint_funcs, self.paint_data, mode);
        }
    }
    fn pop_layer_with_mode(&mut self, mode: CompositeMode) {
        let mode = mode as hb_paint_composite_mode_t; // They are the same
        unsafe {
            hb_paint_pop_group(self.paint_funcs, self.paint_data, mode);
        }
    }
}

#[cfg(feature = "paint")]
unsafe extern "C" fn destroy_bitmap_blob(user_data: *mut c_void) {
    hb_blob_destroy(user_data.cast());
}

#[cfg(feature = "paint")]
fn paint_bitmap_glyph(
    font: *mut hb_font_t,
    data: &FontationsData,
    glyph_id: GlyphId,
    paint_funcs: *mut hb_paint_funcs_t,
    paint_data: *mut ::std::os::raw::c_void,
) -> hb_bool_t {
    let bitmap_glyph = bitmap_glyph(
        &data.instance,
        [GlyphSource::Cbdt, GlyphSource::Sbix],
        glyph_id,
        bitmap_ppem(font),
    );
    let Some(bitmap_glyph) = bitmap_glyph else {
        return false as hb_bool_t;
    };
    let Some(mut extents) = bitmap_glyph_extents(data, &bitmap_glyph) else {
        return false as hb_bool_t;
    };

    let (image, format) = match bitmap_glyph.pixels() {
        Pixels::Png(image) => (image, u32::from_be_bytes(*b"png ")),
        Pixels::Bgra(image) => (image.data(), u32::from_be_bytes(*b"BGRA")),
        Pixels::Mask(_) => return false as hb_bool_t,
    };
    if image.is_empty() {
        return false as hb_bool_t;
    }
    let Ok(image_length) = image.len().try_into() else {
        return false as hb_bool_t;
    };
    let face_blob = unsafe { hb_blob_reference(data.face_blob.0) };
    let blob = unsafe {
        hb_blob_create_or_fail(
            image.as_ptr().cast(),
            image_length,
            hb_memory_mode_t_HB_MEMORY_MODE_READONLY,
            face_blob.cast(),
            Some(destroy_bitmap_blob),
        )
    };
    if blob.is_null() {
        return false as hb_bool_t;
    }

    unsafe {
        hb_paint_image(
            paint_funcs,
            paint_data,
            blob,
            bitmap_glyph.width(),
            bitmap_glyph.height(),
            format,
            0.0,
            &mut extents,
        );
        hb_blob_destroy(blob);
    }
    true as hb_bool_t
}

#[cfg(feature = "paint")]
extern "C" fn _hb_fontations_paint_glyph_or_fail(
    font: *mut hb_font_t,
    font_data: *mut ::std::os::raw::c_void,
    glyph: hb_codepoint_t,
    paint_funcs: *mut hb_paint_funcs_t,
    paint_data: *mut ::std::os::raw::c_void,
    palette_index: ::std::os::raw::c_uint,
    foreground: hb_color_t,
    _user_data: *mut ::std::os::raw::c_void,
) -> hb_bool_t {
    let data = unsafe { &mut *(font_data as *mut FontationsData) };
    data.check_for_updates();

    let glyph_id = GlyphId::new(glyph);
    let Some(color_glyph) = data.instance.color_glyphs().get(glyph_id) else {
        return paint_bitmap_glyph(font, data, glyph_id, paint_funcs, paint_data);
    };

    let palettes = data.instance.palettes();
    let color_records = palettes
        .get(palette_index)
        // https://github.com/harfbuzz/harfbuzz/issues/5116
        .or_else(|| palettes.get(0))
        .map_or(&[][..], |palette| palette.colors());

    let font = if (unsafe { hb_font_is_synthetic(font) } != false as hb_bool_t) {
        unsafe {
            let sub_font = hb_font_create_sub_font(font);
            hb_font_set_synthetic_bold(sub_font, 0.0, 0.0, true as hb_bool_t);
            hb_font_set_synthetic_slant(sub_font, 0.0);
            sub_font
        }
    } else {
        unsafe { hb_font_reference(font) }
    };

    let mut painter = HbColorPainter {
        font,
        paint_funcs,
        paint_data,
        color_records,
        foreground,
        is_glyph_clip: 0,
        clip_depth: 0,
    };
    unsafe {
        hb_paint_push_font_transform(paint_funcs, paint_data, font);
    }
    let _ = color_glyph.paint(&mut painter);
    unsafe {
        hb_paint_pop_transform(paint_funcs, paint_data);
        hb_font_destroy(font);
    }
    true as hb_bool_t
}

extern "C" fn _hb_fontations_glyph_name(
    _font: *mut hb_font_t,
    font_data: *mut ::std::os::raw::c_void,
    glyph: hb_codepoint_t,
    name: *mut ::std::os::raw::c_char,
    size: ::std::os::raw::c_uint,
    _user_data: *mut ::std::os::raw::c_void,
) -> hb_bool_t {
    let data = unsafe { &mut *(font_data as *mut FontationsData) };

    if let Some(glyph_name) = data.instance.glyph_name(GlyphId::new(glyph)) {
        if size == 0 {
            return true as hb_bool_t;
        }
        let glyph_name = glyph_name.as_str();
        // Copy the glyph name into the buffer, up to size-1 bytes
        let len = glyph_name.len().min((size as usize) - 1);
        let name: *mut u8 = name.cast();
        unsafe {
            std::slice::from_raw_parts_mut(name, len)
                .copy_from_slice(&glyph_name.as_bytes()[..len]);
            *name.add(len) = 0;
        }
        true as hb_bool_t
    } else {
        false as hb_bool_t
    }
}

static mut GLYPH_FROM_NAMES_KEY: hb_user_data_key_t = hb_user_data_key_t { unused: 0 };

extern "C" fn _hb_glyph_from_names_destroy(data: *mut c_void) {
    let _ = unsafe { Box::from_raw(data as *mut HashMap<String, u32>) };
}

extern "C" fn _hb_fontations_glyph_from_name(
    font: *mut hb_font_t,
    font_data: *mut ::std::os::raw::c_void,
    name: *const ::std::os::raw::c_char,
    len: ::std::os::raw::c_int,
    glyph: *mut hb_codepoint_t,
    _user_data: *mut ::std::os::raw::c_void,
) -> hb_bool_t {
    let data = unsafe { &*(font_data as *const FontationsData) };

    let name: *const u8 = name.cast();
    // SAFETY: HarfBuzz guarantees the string is valid memory for `len` bytes.
    let name_bytes = unsafe { std::slice::from_raw_parts(name, len as usize) };
    let name_str = match std::str::from_utf8(name_bytes) {
        Ok(s) => s,
        Err(_) => return false as hb_bool_t,
    };

    let face = unsafe { hb_font_get_face(font) };
    let mut user_data_ptr =
        unsafe { hb_face_get_user_data(face, std::ptr::addr_of_mut!(GLYPH_FROM_NAMES_KEY)) };

    if user_data_ptr.is_null() {
        // Build the HashMap from glyph names to IDs
        let mut map = HashMap::new();
        for (glyph_id, glyph_name) in data.instance.glyph_names() {
            map.insert(glyph_name.to_string(), glyph_id.to_u32());
        }

        let boxed_map = Box::new(map);
        let ptr = Box::into_raw(boxed_map) as *mut c_void;

        let success = unsafe {
            hb_face_set_user_data(
                face,
                std::ptr::addr_of_mut!(GLYPH_FROM_NAMES_KEY),
                ptr,
                Some(_hb_glyph_from_names_destroy),
                false as hb_bool_t,
            )
        };

        if success != false as hb_bool_t {
            user_data_ptr = ptr;
        } else {
            // Another reader may have published first. Never replace its map:
            // readers keep using it until the face is destroyed.
            _hb_glyph_from_names_destroy(ptr);
            user_data_ptr = unsafe {
                hb_face_get_user_data(face, std::ptr::addr_of_mut!(GLYPH_FROM_NAMES_KEY))
            };
            if user_data_ptr.is_null() {
                // Publication failed without a winner (for example, on OOM).
                return false as hb_bool_t;
            }
        }
    }

    let glyph_from_names = unsafe { &*(user_data_ptr as *const HashMap<String, u32>) };

    match glyph_from_names.get(name_str) {
        Some(gid) => {
            unsafe { *glyph = *gid };
            true as hb_bool_t
        }
        None => false as hb_bool_t,
    }
}

fn _hb_fontations_font_funcs_get() -> *mut hb_font_funcs_t {
    static STATIC_FFUNCS: AtomicPtr<hb_font_funcs_t> = AtomicPtr::new(null_mut());

    loop {
        let mut ffuncs = STATIC_FFUNCS.load(Ordering::Acquire);

        if !ffuncs.is_null() {
            return ffuncs;
        }

        ffuncs = unsafe { hb_font_funcs_create() };

        unsafe {
            hb_font_funcs_set_nominal_glyphs_func(
                ffuncs,
                Some(_hb_fontations_get_nominal_glyphs),
                null_mut(),
                None,
            );
            hb_font_funcs_set_variation_glyph_func(
                ffuncs,
                Some(_hb_fontations_get_variation_glyph),
                null_mut(),
                None,
            );
            hb_font_funcs_set_glyph_h_advances_func(
                ffuncs,
                Some(_hb_fontations_get_glyph_h_advances),
                null_mut(),
                None,
            );
            hb_font_funcs_set_glyph_v_advances_func(
                ffuncs,
                Some(_hb_fontations_get_glyph_v_advances),
                null_mut(),
                None,
            );
            hb_font_funcs_set_glyph_v_origins_func(
                ffuncs,
                Some(_hb_fontations_get_glyph_v_origins),
                null_mut(),
                None,
            );
            hb_font_funcs_set_glyph_extents_func(
                ffuncs,
                Some(_hb_fontations_get_glyph_extents),
                null_mut(),
                None,
            );
            hb_font_funcs_set_font_h_extents_func(
                ffuncs,
                Some(_hb_fontations_get_font_h_extents),
                null_mut(),
                None,
            );
            #[cfg(feature = "draw")]
            hb_font_funcs_set_draw_glyph_or_fail_func(
                ffuncs,
                Some(_hb_fontations_draw_glyph_or_fail),
                null_mut(),
                None,
            );
            #[cfg(feature = "paint")]
            hb_font_funcs_set_paint_glyph_or_fail_func(
                ffuncs,
                Some(_hb_fontations_paint_glyph_or_fail),
                null_mut(),
                None,
            );
            hb_font_funcs_set_glyph_name_func(
                ffuncs,
                Some(_hb_fontations_glyph_name),
                null_mut(),
                None,
            );
            hb_font_funcs_set_glyph_from_name_func(
                ffuncs,
                Some(_hb_fontations_glyph_from_name),
                null_mut(),
                None,
            );
        }

        if (STATIC_FFUNCS.compare_exchange(null_mut(), ffuncs, Ordering::SeqCst, Ordering::Relaxed))
            == Ok(null_mut())
        {
            return ffuncs;
        } else {
            unsafe {
                hb_font_funcs_destroy(ffuncs);
            }
        }
    }
}

/// # Safety
///
/// This function is unsafe because it connects with the HarfBuzz API.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn hb_fontations_font_set_funcs(font: *mut hb_font_t) {
    let ffuncs = _hb_fontations_font_funcs_get();

    let data = FontationsData::from_hb_font(font);
    let data = match data {
        Some(d) => d,
        None => return,
    };
    let data_ptr = Box::into_raw(Box::new(data)) as *mut c_void;

    hb_font_set_funcs(font, ffuncs, data_ptr, Some(_hb_fontations_data_destroy));
}
