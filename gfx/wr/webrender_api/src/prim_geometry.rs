/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Primitive geometry simplification / gradient optimization helpers.
//!
//! These are pure, behaviour-neutral geometry functions that simplify a
//! repeated/tiled primitive and pre-clip/optimize gradients before they are
//! handed to the GPU. They operate only on api-resident types so that they can
//! be shared between `webrender` (frame/scene building) and content-process
//! interning in the `DisplayListBuilder`; `webrender` re-exports them from their
//! former homes. Not part of the public API surface.

use crate::units::{LayoutRect, LayoutSize, LayoutPoint, LayoutVector2D, RectExt};
use crate::{ColorU, ExtendMode};
use crate::interned_prims::{ConicGradient, LinearGradient, RadialGradient};
use crate::key_types::{
    ConicGradientParams, EdgeMask, GradientStopKey, NinePatchDescriptor,
    RadialGradientParams,
};
use euclid::{vec2, size2};
use euclid::approxeq::ApproxEq;

/// Collapse a tiled primitive whose tile (plus spacing) already covers the
/// primitive rect on an axis down to a single, non-tiled extent on that axis.
pub fn simplify_repeated_primitive(
    stretch_size: &LayoutSize,
    tile_spacing: &mut LayoutSize,
    prim_rect: &mut LayoutRect,
) {
    let stride = *stretch_size + *tile_spacing;

    if stride.width >= prim_rect.width() {
        tile_spacing.width = 0.0;
        prim_rect.max.x = f32::min(prim_rect.min.x + stretch_size.width, prim_rect.max.x);
    }
    if stride.height >= prim_rect.height() {
        tile_spacing.height = 0.0;
        prim_rect.max.y = f32::min(prim_rect.min.y + stretch_size.height, prim_rect.max.y);
    }
}

/// Snap a repeated primitive's tile size to the snapped prim-rect extent when
/// it (fuzzily) matches the unsnapped extent, so the tile lands on the snapped
/// pixel grid.
pub fn process_repeat_size(
    snapped_rect: &LayoutRect,
    unsnapped_rect: &LayoutRect,
    repeat_size: LayoutSize,
) -> LayoutSize {
    // FIXME(aosmond): The tile size is calculated based on several parameters
    // during display list building. It may produce a slightly different result
    // than the bounds due to floating point error accumulation, even though in
    // theory they should be the same. We do a fuzzy check here to paper over
    // that. It may make more sense to push the original parameters into scene
    // building and let it do a saner calculation with more information (e.g.
    // the snapped values).
    const EPSILON: f32 = 0.001;
    LayoutSize::new(
        if repeat_size.width.approx_eq_eps(&unsnapped_rect.width(), &EPSILON) {
            snapped_rect.width()
        } else {
            repeat_size.width
        },
        if repeat_size.height.approx_eq_eps(&unsnapped_rect.height(), &EPSILON) {
            snapped_rect.height()
        } else {
            repeat_size.height
        },
    )
}

/// Per-axis fraction of the primitive size that one tile of the stretched
/// pattern covers (clamped to 1.0). Returns `1.0` on each axis for a degenerate
/// prim size.
///
/// The ratio is resolved against the *snapped* prim rect at prepare time, which
/// is not the rect it is computed against here, so an axis that covers the whole
/// primitive must be reported as exactly `1.0`: anything less would resurrect a
/// non-repeating primitive as a repeating one with a sub-pixel stride.
pub fn compute_stretch_ratio(stretch_size: LayoutSize, prim_size: LayoutSize) -> LayoutSize {
    let prim_ok = prim_size.width.is_finite() &&
        prim_size.width > 0.0 &&
        prim_size.height.is_finite() &&
        prim_size.height > 0.0;
    if !prim_ok {
        return LayoutSize::new(1.0, 1.0);
    }
    const EPSILON: f32 = 0.001;
    let w = if stretch_size.width.approx_eq_eps(&prim_size.width, &EPSILON) {
        1.0
    } else {
        (stretch_size.width / prim_size.width).min(1.0)
    };
    let h = if stretch_size.height.approx_eq_eps(&prim_size.height, &EPSILON) {
        1.0
    } else {
        (stretch_size.height / prim_size.height).min(1.0)
    };
    LayoutSize::new(w, h)
}

/// Clip a (possibly tiled) gradient primitive to its local clip rect, returning
/// the offset that must be applied to the gradient's start/center so the
/// gradient stays aligned after the prim rect is shrunk.
pub fn apply_gradient_local_clip(
    prim_rect: &mut LayoutRect,
    stretch_size: &LayoutSize,
    tile_spacing: &LayoutSize,
    clip_rect: &LayoutRect,
) -> LayoutVector2D {
    let w = prim_rect.max.x.min(clip_rect.max.x) - prim_rect.min.x;
    let h = prim_rect.max.y.min(clip_rect.max.y) - prim_rect.min.y;
    let is_tiled_x = w > stretch_size.width + tile_spacing.width;
    let is_tiled_y = h > stretch_size.height + tile_spacing.height;

    let mut offset = LayoutVector2D::new(0.0, 0.0);

    if !is_tiled_x {
        let diff = (clip_rect.min.x - prim_rect.min.x).min(prim_rect.width());
        if diff > 0.0 {
            prim_rect.min.x += diff;
            offset.x = -diff;
        }

        let diff = prim_rect.max.x - clip_rect.max.x;
        if diff > 0.0 {
            prim_rect.max.x -= diff;
        }
    }

    if !is_tiled_y {
        let diff = (clip_rect.min.y - prim_rect.min.y).min(prim_rect.height());
        if diff > 0.0 {
            prim_rect.min.y += diff;
            offset.y = -diff;
        }

        let diff = prim_rect.max.y - clip_rect.max.y;
        if diff > 0.0 {
            prim_rect.max.y -= diff;
        }
    }

    offset
}

pub fn optimize_linear_gradient(
    prim_rect: &mut LayoutRect,
    tile_size: &mut LayoutSize,
    mut tile_spacing: LayoutSize,
    clip_rect: &LayoutRect,
    start: &mut LayoutPoint,
    end: &mut LayoutPoint,
) {
    simplify_repeated_primitive(&tile_size, &mut tile_spacing, prim_rect);

    let vertical = start.x.approx_eq(&end.x);
    let horizontal = start.y.approx_eq(&end.y);

    let horizontally_tiled = prim_rect.width() > tile_size.width;
    let vertically_tiled = prim_rect.height() > tile_size.height;

    // Check whether the tiling is equivalent to stretching on either axis.
    // Stretching the gradient is more efficient than repeating it.
    if vertically_tiled && horizontal && tile_spacing.height == 0.0 {
        tile_size.height = prim_rect.height();
    }

    if horizontally_tiled && vertical && tile_spacing.width == 0.0 {
        tile_size.width = prim_rect.width();
    }

    let offset = apply_gradient_local_clip(
        prim_rect,
        &tile_size,
        &tile_spacing,
        &clip_rect
    );

    // The size of gradient render tasks depends on the tile_size. No need to generate
    // large stretch sizes that will be clipped to the bounds of the primitive.
    tile_size.width = tile_size.width.min(prim_rect.width());
    tile_size.height = tile_size.height.min(prim_rect.height());

    *start += offset;
    *end += offset;
}

/// Avoid invoking the radial gradient shader on large areas where the color is
/// constant.
///
/// If the extend mode is set to clamp, the "interesting" part
/// of the gradient is only in the bounds of the gradient's ellipse, and the rest
/// is the color of the last gradient stop.
///
/// The `solid_parts` callback is invoked with the constant-color margin
/// rectangles that surround the shrunk gradient.
pub fn optimize_radial_gradient(
    prim_rect: &mut LayoutRect,
    stretch_size: &mut LayoutSize,
    center: &mut LayoutPoint,
    tile_spacing: &mut LayoutSize,
    aa_mask: &mut EdgeMask,
    clip_rect: &LayoutRect,
    radius: LayoutSize,
    end_offset: f32,
    extend_mode: ExtendMode,
    stops: &[GradientStopKey],
    solid_parts: &mut dyn FnMut(&LayoutRect, ColorU, EdgeMask),
) {
    let offset = apply_gradient_local_clip(
        prim_rect,
        stretch_size,
        tile_spacing,
        clip_rect
    );

    *center += offset;

    if extend_mode != ExtendMode::Clamp || stops.is_empty() {
        return;
    }

    // Bounding box of the "interesting" part of the gradient.
    let min = prim_rect.min + center.to_vector() - radius.to_vector() * end_offset;
    let max = prim_rect.min + center.to_vector() + radius.to_vector() * end_offset;

    // The (non-repeated) gradient primitive rect.
    let gradient_rect = LayoutRect::from_origin_and_size(
        prim_rect.min,
        *stretch_size,
    );

    // How much internal margin between the primitive bounds and the gradient's
    // bounding rect (areas that are a constant color).
    let mut l = (min.x - gradient_rect.min.x).max(0.0).floor();
    let mut t = (min.y - gradient_rect.min.y).max(0.0).floor();
    let mut r = (gradient_rect.max.x - max.x).max(0.0).floor();
    let mut b = (gradient_rect.max.y - max.y).max(0.0).floor();

    let is_tiled = prim_rect.width() > stretch_size.width + tile_spacing.width
        || prim_rect.height() > stretch_size.height + tile_spacing.height;

    let bg_color = stops.last().unwrap().color;

    if bg_color.a != 0 && is_tiled {
        // If the primitive has repetitions, it's not enough to insert solid rects around it,
        // so bail out.
        return;
    }

    // If the background is fully transparent, shrinking the primitive bounds as much as possible
    // is always a win. If the background is not transparent, we have to insert solid rectangles
    // around the shrunk parts.
    // If the background is transparent and the primitive is tiled, the optimization may introduce
    // tile spacing which forces the tiling to be manually decomposed.
    // Either way, don't bother optimizing unless it saves a significant amount of pixels.
    if bg_color.a != 0 || (is_tiled && tile_spacing.is_empty()) {
        let threshold = 128.0;
        if l < threshold { l = 0.0 }
        if t < threshold { t = 0.0 }
        if r < threshold { r = 0.0 }
        if b < threshold { b = 0.0 }
    }

    if l + t + r + b == 0.0 {
        // No adjustment to make;
        return;
    }

    // Insert solid rectangles around the gradient, in the places where the primitive will be
    // shrunk.
    if bg_color.a != 0 {
        if l != 0.0 && t != 0.0 {
            let solid_rect = LayoutRect::from_origin_and_size(
                gradient_rect.min,
                size2(l, t),
            );
            solid_parts(&solid_rect, bg_color, EdgeMask::LEFT | EdgeMask::TOP);
        }

        if l != 0.0 && b != 0.0 {
            let solid_rect = LayoutRect::from_origin_and_size(
                gradient_rect.bottom_left() - vec2(0.0, b),
                size2(l, b),
            );
            solid_parts(&solid_rect, bg_color, EdgeMask::LEFT | EdgeMask::BOTTOM);
        }

        if t != 0.0 && r != 0.0 {
            let solid_rect = LayoutRect::from_origin_and_size(
                gradient_rect.top_right() - vec2(r, 0.0),
                size2(r, t),
            );
            solid_parts(&solid_rect, bg_color, EdgeMask::TOP | EdgeMask::RIGHT);
        }

        if r != 0.0 && b != 0.0 {
            let solid_rect = LayoutRect::from_origin_and_size(
                gradient_rect.bottom_right() - vec2(r, b),
                size2(r, b),
            );
            solid_parts(&solid_rect, bg_color, EdgeMask::RIGHT | EdgeMask::BOTTOM);
        }

        if l != 0.0 {
            let solid_rect = LayoutRect::from_origin_and_size(
                gradient_rect.min + vec2(0.0, t),
                size2(l, gradient_rect.height() - t - b),
            );
            let mut solid_aa = EdgeMask::LEFT;
            solid_aa.set(EdgeMask::TOP, t == 0.0);
            solid_aa.set(EdgeMask::BOTTOM, b == 0.0);
            solid_parts(&solid_rect, bg_color, solid_aa);
            aa_mask.remove(EdgeMask::LEFT);
        }

        if r != 0.0 {
            let solid_rect = LayoutRect::from_origin_and_size(
                gradient_rect.top_right() + vec2(-r, t),
                size2(r, gradient_rect.height() - t - b),
            );
            let mut solid_aa = EdgeMask::RIGHT;
            solid_aa.set(EdgeMask::TOP, t == 0.0);
            solid_aa.set(EdgeMask::BOTTOM, b == 0.0);
            solid_parts(&solid_rect, bg_color, solid_aa);
            aa_mask.remove(EdgeMask::RIGHT);
        }

        if t != 0.0 {
            let solid_rect = LayoutRect::from_origin_and_size(
                gradient_rect.min + vec2(l, 0.0),
                size2(gradient_rect.width() - l - r, t),
            );
            let mut solid_aa = EdgeMask::TOP;
            solid_aa.set(EdgeMask::LEFT, l == 0.0);
            solid_aa.set(EdgeMask::RIGHT, r == 0.0);
            solid_parts(&solid_rect, bg_color, solid_aa);
            aa_mask.remove(EdgeMask::TOP);
        }

        if b != 0.0 {
            let solid_rect = LayoutRect::from_origin_and_size(
                gradient_rect.bottom_left() + vec2(l, -b),
                size2(gradient_rect.width() - l - r, b),
            );
            let mut solid_aa = EdgeMask::BOTTOM;
            solid_aa.set(EdgeMask::LEFT, l == 0.0);
            solid_aa.set(EdgeMask::RIGHT, r == 0.0);
            solid_parts(&solid_rect, bg_color, solid_aa);
            aa_mask.remove(EdgeMask::BOTTOM);
        }
    }

    // Shrink the gradient primitive.

    prim_rect.min.x += l;
    prim_rect.min.y += t;

    stretch_size.width -= l + r;
    stretch_size.height -= b + t;

    center.x -= l;
    center.y -= t;

    tile_spacing.width += l + r;
    tile_spacing.height += t + b;
}

/// Build the interned description of a linear gradient.
///
/// Moved verbatim from the scene builder's `create_linear_gradient_prim` so the
/// display list builder can build the key at push time. `None` means the
/// gradient cannot contribute to the scene and no primitive should be recorded.
///
/// `prim_rect` is the rect the gradient will be recorded with, i.e. already
/// through `optimize_linear_gradient`. Note that the `simplify_repeated_primitive`
/// call below deliberately discards its rect mutation and keeps only the
/// `tile_spacing` one, and that `stretch_ratio` is resolved against `prim_rect`
/// rather than the simplified rect - both as the scene builder had it.
pub fn linear_gradient_prim(
    prim_rect: LayoutRect,
    start_point: LayoutPoint,
    end_point: LayoutPoint,
    stops: Vec<GradientStopKey>,
    extend_mode: ExtendMode,
    stretch_size: LayoutSize,
    mut tile_spacing: LayoutSize,
    nine_patch: Option<Box<NinePatchDescriptor>>,
) -> Option<LinearGradient> {
    let mut simplified_rect = prim_rect;
    simplify_repeated_primitive(&stretch_size, &mut tile_spacing, &mut simplified_rect);

    let mut is_entirely_transparent = true;
    for stop in &stops {
        if stop.color.a > 0 {
            is_entirely_transparent = false;
        }
    }

    // If all the stops have no alpha, then this
    // gradient can't contribute to the scene.
    if is_entirely_transparent {
        return None;
    }

    // Try to ensure that if the gradient is specified in reverse, then so long as the stops
    // are also supplied in reverse that the rendered result will be equivalent. To do this,
    // a reference orientation for the gradient line must be chosen, somewhat arbitrarily, so
    // just designate the reference orientation as start < end. Aligned gradient rendering
    // manages to produce the same result regardless of orientation, so don't worry about
    // reversing in that case.
    let reverse_stops = start_point.x > end_point.x ||
        (start_point.x == end_point.x && start_point.y > end_point.y);

    // To get reftests exactly matching with reverse start/end
    // points, it's necessary to reverse the gradient
    // line in some cases.
    let (sp, ep) = if reverse_stops {
        (end_point, start_point)
    } else {
        (start_point, end_point)
    };

    let stretch_ratio = compute_stretch_ratio(stretch_size, prim_rect.size());

    Some(LinearGradient {
        extend_mode,
        start_point: sp.into(),
        end_point: ep.into(),
        stretch_ratio: stretch_ratio.into(),
        tile_spacing: tile_spacing.into(),
        stops,
        reverse_stops,
        nine_patch,
    })
}

/// Build the interned description of a conic gradient. Moved verbatim from the
/// scene builder's `create_conic_gradient_prim`; see `linear_gradient_prim` for
/// the two quirks this preserves.
pub fn conic_gradient_prim(
    prim_rect: LayoutRect,
    center: LayoutPoint,
    angle: f32,
    start_offset: f32,
    end_offset: f32,
    stops: Vec<GradientStopKey>,
    extend_mode: ExtendMode,
    stretch_size: LayoutSize,
    mut tile_spacing: LayoutSize,
    nine_patch: Option<Box<NinePatchDescriptor>>,
) -> ConicGradient {
    let mut simplified_rect = prim_rect;
    simplify_repeated_primitive(&stretch_size, &mut tile_spacing, &mut simplified_rect);

    let stretch_ratio = compute_stretch_ratio(stretch_size, prim_rect.size());

    ConicGradient {
        extend_mode,
        center: center.into(),
        params: ConicGradientParams { angle, start_offset, end_offset },
        stretch_ratio: stretch_ratio.into(),
        tile_spacing: tile_spacing.into(),
        nine_patch,
        stops,
    }
}

/// Build the interned description of a radial gradient. Moved verbatim from the
/// scene builder's `create_radial_gradient_prim`; see `linear_gradient_prim` for
/// the two quirks this preserves.
pub fn radial_gradient_prim(
    prim_rect: LayoutRect,
    center: LayoutPoint,
    start_radius: f32,
    end_radius: f32,
    ratio_xy: f32,
    stops: Vec<GradientStopKey>,
    extend_mode: ExtendMode,
    stretch_size: LayoutSize,
    mut tile_spacing: LayoutSize,
    nine_patch: Option<Box<NinePatchDescriptor>>,
) -> RadialGradient {
    let mut simplified_rect = prim_rect;
    simplify_repeated_primitive(&stretch_size, &mut tile_spacing, &mut simplified_rect);

    let params = RadialGradientParams {
        start_radius,
        end_radius,
        ratio_xy,
    };

    let stretch_ratio = compute_stretch_ratio(stretch_size, prim_rect.size());

    RadialGradient {
        extend_mode,
        center: center.into(),
        params,
        stretch_ratio: stretch_ratio.into(),
        tile_spacing: tile_spacing.into(),
        nine_patch,
        stops,
    }
}
