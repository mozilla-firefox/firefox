/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Linear gradients
//!
//! Specification: https://drafts.csswg.org/css-images-4/#linear-gradients
//!
//! Linear gradients are rendered as quads with the gradient pattern (ps_quad_gradient).

use euclid::approxeq::ApproxEq;
use euclid::vec2;
use api::{AlphaMode, ExtendMode, GradientStop};
use api::units::*;
use crate::pattern::gradient::linear_gradient_pattern;
use crate::pattern::{Pattern, PatternBuilder, PatternBuilderState};
use crate::dl_interner::{DlResolve, DlResolveContext};
use crate::prim_store::PrimitiveOpacity;
use crate::prim_store::PrimTemplateCommonData;
use crate::prim_store::NinePatchDescriptor;
use crate::segment::EdgeMask;
use super::stops_and_min_alpha;
use std::ops::{Deref, DerefMut};
use std::mem::swap;

// `LinearGradient` (the interned value) and `LinearGradientKey` live in
// `webrender_api::interned_prims`, since the content display list builder
// interns them. The frame-time `LinearGradientTemplate` stays here.
pub use api::interned_prims::LinearGradientKey;

impl crate::prim_store::PrimKeyCommon for LinearGradientKey {
    fn key_common(&self) -> &crate::prim_store::PrimKeyCommonData {
        &self.common
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, MallocSizeOf, Clone)]
pub struct LinearGradientTemplate {
    pub common: PrimTemplateCommonData,
    pub extend_mode: ExtendMode,
    pub alpha_mode: AlphaMode,
    // Relative to the primitive's pattern rect.
    pub start_point: LayoutVector2D,
    pub end_point: LayoutVector2D,
    /// Per-axis fraction of `common.prim_size` covered by one tile of the
    /// gradient pattern. Multiply by `common.prim_size` at use to recover the
    /// absolute stretch_size.
    pub stretch_ratio: LayoutSize,
    pub tile_spacing: LayoutSize,
    pub stops_opacity: PrimitiveOpacity,
    pub stops: Vec<GradientStop>,
    pub border_nine_patch: Option<Box<NinePatchDescriptor>>,
    pub reverse_stops: bool,
}

impl PatternBuilder for LinearGradientTemplate {
    fn build(
        &self,
        pattern_rect: &LayoutRect,
        state: &mut PatternBuilderState,
    ) -> Pattern {
        let (start, end) = if self.reverse_stops {
            (self.end_point, self.start_point)
        } else {
            (self.start_point, self.end_point)
        };
        // LinearGradientTemplate stores the start and end points relative to the
        // primitive origin, but the shader works with start/end points in "proper"
        // layout coordinates (relative to the primitive's spatial node).
        linear_gradient_pattern(
            pattern_rect.min + start,
            pattern_rect.min + end,
            self.extend_mode,
            self.alpha_mode,
            &self.stops,
            state.frame_gpu_data,
        )
    }
}

impl Deref for LinearGradientTemplate {
    type Target = PrimTemplateCommonData;
    fn deref(&self) -> &Self::Target {
        &self.common
    }
}

impl DerefMut for LinearGradientTemplate {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.common
    }
}

/// Whether a linear gradient is eligible for the fast-path two-stop-per-segment
/// decomposition at prepare time. Inputs are the values produced by
/// `optimize_linear_gradient` (i.e. already simplified and clip-adjusted).
pub fn linear_gradient_decomposes(
    prim_rect: &LayoutRect,
    tile_size: LayoutSize,
    tile_spacing: LayoutSize,
    start: LayoutVector2D,
    end: LayoutVector2D,
    extend_mode: ExtendMode,
    stops: &[GradientStop],
    enable_dithering: bool,
) -> bool {
    if extend_mode != ExtendMode::Clamp || stops.is_empty() {
        return false;
    }

    let vertical = start.x.approx_eq(&end.x);
    let horizontal = start.y.approx_eq(&end.y);

    if !vertical && !horizontal {
        return false;
    }

    if vertical && horizontal {
        return false;
    }

    if !tile_spacing.is_empty() {
        return false;
    }

    let horizontally_tiled = prim_rect.width() > tile_size.width;
    let vertically_tiled = prim_rect.height() > tile_size.height;
    if vertically_tiled || horizontally_tiled {
        return false;
    }

    if !enable_dithering &&
        ((horizontal && tile_size.width < 256.0)
        || (vertical && tile_size.height < 256.0)) {
        return false;
    }

    true
}

/// Decompose an axis-aligned linear gradient into a sequence of two-stop
/// segments that tile end-to-end across `prim_rect`. Each callback invocation
/// is one segment, ready to be rendered as its own quad via the fast-path
/// gradient shader. Run at frame-build (against the snapped prim_rect) so
/// adjacent segments share a snapped boundary and tile without phase drift.
///
/// Caller must have verified eligibility via [`linear_gradient_decomposes`].
pub fn decompose_axis_aligned_gradient(
    prim_rect: &LayoutRect,
    tile_size: LayoutSize,
    start: LayoutVector2D,
    end: LayoutVector2D,
    stops: &[GradientStop],
    clip_rect: &LayoutRect,
    mut callback: impl FnMut(&LayoutRect, LayoutVector2D, LayoutVector2D, [GradientStop; 2], EdgeMask),
) {
    debug_assert!(!stops.is_empty());

    let vertical = start.x.approx_eq(&end.x);

    // Flip x/y when the gradient is vertical so the remaining math treats it
    // as horizontal; un-flip per-segment outputs at the end.
    let adjust_rect = &mut |rect: &mut LayoutRect| {
        if vertical {
            swap(&mut rect.min.x, &mut rect.min.y);
            swap(&mut rect.max.x, &mut rect.max.y);
        }
    };
    let adjust_size = &mut |size: &mut LayoutSize| {
        if vertical { swap(&mut size.width, &mut size.height); }
    };
    let adjust_vector = &mut |p: &mut LayoutVector2D| {
        if vertical { swap(&mut p.x, &mut p.y); }
    };

    let clip_rect = match clip_rect.intersection(prim_rect) {
        Some(clip) => clip,
        None => return,
    };

    let mut prim_rect = *prim_rect;
    let mut start = start;
    let mut end = end;
    let mut tile_size = tile_size;

    adjust_rect(&mut prim_rect);
    adjust_vector(&mut start);
    adjust_vector(&mut end);
    adjust_size(&mut tile_size);

    // `clip_rect` stays in the original (un-swapped) space — segment_rect
    // gets `adjust_rect` applied twice (once implicitly via the prim_rect
    // copy, once explicitly after computing per-segment extent) and lands
    // back in original space before this intersection.

    let length = (end.x - start.x).abs();

    // Match the pre-refactor optimiser: when the gradient line points in
    // decreasing-x (post-axis-swap), swap start/end and walk the stop list
    // in reverse, so the loop always processes stops in increasing-x
    // order. The pre-refactor code did this via `stops.reverse()` in
    // place; we can't mutate the template's stops here, so use a reversed
    // iterator and swap which end of the slice supplies the fake-stop
    // colour accordingly.
    let reverse_stops = start.x > end.x;
    if reverse_stops {
        swap(&mut start, &mut end);
    }

    let (first_stop, last_stop) = if reverse_stops {
        (*stops.last().unwrap(), *stops.first().unwrap())
    } else {
        (*stops.first().unwrap(), *stops.last().unwrap())
    };

    let mut prev = first_stop;
    let mut last = last_stop;
    prev.offset = -start.x / length;
    last.offset = (tile_size.width - start.x) / length;
    if reverse_stops {
        prev.offset = 1.0 - prev.offset;
        last.offset = 1.0 - last.offset;
    }

    let (side_edges, first_edge, last_edge) = if vertical {
        (
            EdgeMask::LEFT | EdgeMask::RIGHT,
            EdgeMask::TOP,
            EdgeMask::BOTTOM,
        )
    } else {
        (
            EdgeMask::TOP | EdgeMask::BOTTOM,
            EdgeMask::LEFT,
            EdgeMask::RIGHT,
        )
    };

    let mut is_first = true;
    let last_offset = last.offset;

    // Iterate stops in increasing-x order. When reverse_stops is set, walk the
    // backing slice in reverse instead of mutating it.
    let stops_iter: Box<dyn Iterator<Item = &GradientStop>> = if reverse_stops {
        Box::new(stops.iter().rev())
    } else {
        Box::new(stops.iter())
    };

    for stop in stops_iter.chain(std::iter::once(&last)) {
        let prev_stop = prev;
        prev = *stop;

        if prev_stop.color.a == 0.0 && stop.color.a == 0.0 {
            continue;
        }

        let prev_offset = if reverse_stops { 1.0 - prev_stop.offset } else { prev_stop.offset };
        let offset = if reverse_stops { 1.0 - stop.offset } else { stop.offset };

        // Segment_start and segment_end are in the gradient's pre-flip space
        // (relative to the prim's origin); the adjust_* helpers below restore
        // axis orientation when emitting.
        let segment_start = prev_offset * length;
        let segment_end = offset * length;
        let segment_length = segment_end - segment_start;

        if segment_length <= 0.0 {
            continue;
        }

        let rect_start = prim_rect.min.x + start.x + segment_start;
        let rect_end = prim_rect.min.x + start.x + segment_end;
        if rect_end <= rect_start {
            continue;
        }

        let mut segment_rect = prim_rect;
        segment_rect.min.x = rect_start;
        segment_rect.max.x = rect_end;

        let mut seg_start = vec2(0.0, 0.0);
        let mut seg_end = vec2(segment_length, 0.0);

        adjust_vector(&mut seg_start);
        adjust_vector(&mut seg_end);
        adjust_rect(&mut segment_rect);

        let origin_before_clip = segment_rect.min;
        segment_rect = match segment_rect.intersection(&clip_rect) {
            Some(rect) => rect,
            None => continue,
        };
        let clip_offset = segment_rect.min - origin_before_clip;
        seg_start -= clip_offset;
        seg_end -= clip_offset;

        let mut edge_flags = side_edges;
        if is_first {
            edge_flags |= first_edge;
            is_first = false;
        }
        if stop.offset == last_offset {
            edge_flags |= last_edge;
        }

        callback(
            &segment_rect,
            seg_start,
            seg_end,
            [
                GradientStop { offset: 0.0, color: prev_stop.color },
                GradientStop { offset: 1.0, color: stop.color },
            ],
            edge_flags,
        );
    }
}

/// Nothing to validate: a gradient names no resource.
impl DlResolve<LinearGradientKey> for LinearGradientTemplate {
    fn resolve(item: &LinearGradientKey, _ctx: &DlResolveContext) -> Self {
        let common = PrimTemplateCommonData::with_key_common(item.common);

        let (stops, min_alpha) = stops_and_min_alpha(&item.stops);

        // Save opacity of the stops for use in
        // selecting which pass this gradient
        // should be drawn in.
        let stops_opacity = PrimitiveOpacity::from_alpha(min_alpha);

        let start_point = LayoutVector2D::new(item.start_point.x, item.start_point.y);
        let end_point = LayoutVector2D::new(item.end_point.x, item.end_point.y);
        let tile_spacing: LayoutSize = item.tile_spacing.into();
        let stretch_ratio: LayoutSize = item.stretch_ratio.into();

        LinearGradientTemplate {
            common,
            extend_mode: item.extend_mode,
            alpha_mode: item.alpha_mode,
            start_point,
            end_point,
            stretch_ratio,
            tile_spacing,
            stops_opacity,
            stops,
            border_nine_patch: item.nine_patch.clone(),
            reverse_stops: item.reverse_stops,
        }
    }
}

