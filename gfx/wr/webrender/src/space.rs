/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


//! Utilities to deal with coordinate spaces.

use std::fmt;

use euclid::{Transform3D, Box2D, Point2D, Vector2D};

use api::units::{DevicePoint, DeviceRect, LayoutSideOffsets, LayoutVector2D};
use crate::spatial_tree::{CoordinateSystemId, SpatialTree, CoordinateSpaceMapping, SpatialNodeIndex, VisibleFace};
use crate::surface::SurfaceInfo;
use crate::util::project_rect;
use crate::util::{MatrixHelpers, RectHelpers, ScaleOffset};


#[derive(Debug, Clone)]
pub struct SpaceMapper<F, T> {
    kind: CoordinateSpaceMapping<F, T>,
    pub ref_spatial_node_index: SpatialNodeIndex,
    pub current_target_spatial_node_index: SpatialNodeIndex,
    pub bounds: Box2D<f32, T>,
    visible_face: VisibleFace,
}

impl<F, T> SpaceMapper<F, T> where F: fmt::Debug {
    pub fn new(
        ref_spatial_node_index: SpatialNodeIndex,
        bounds: Box2D<f32, T>,
    ) -> Self {
        SpaceMapper {
            kind: CoordinateSpaceMapping::Local,
            ref_spatial_node_index,
            current_target_spatial_node_index: ref_spatial_node_index,
            bounds,
            visible_face: VisibleFace::Front,
        }
    }

    pub fn new_with_target(
        ref_spatial_node_index: SpatialNodeIndex,
        target_node_index: SpatialNodeIndex,
        bounds: Box2D<f32, T>,
        spatial_tree: &SpatialTree,
    ) -> Self {
        let mut mapper = Self::new(ref_spatial_node_index, bounds);
        mapper.set_target_spatial_node(target_node_index, spatial_tree);
        mapper
    }

    pub fn set_target_spatial_node(
        &mut self,
        target_node_index: SpatialNodeIndex,
        spatial_tree: &SpatialTree,
    ) {
        if target_node_index == self.current_target_spatial_node_index {
            return
        }

        let ref_spatial_node = spatial_tree.get_spatial_node(self.ref_spatial_node_index);
        let target_spatial_node = spatial_tree.get_spatial_node(target_node_index);
        self.visible_face = VisibleFace::Front;

        self.kind = if self.ref_spatial_node_index == target_node_index {
            CoordinateSpaceMapping::Local
        } else if ref_spatial_node.coordinate_system_id == target_spatial_node.coordinate_system_id {
            let scale_offset = target_spatial_node.content_transform
                .then(&ref_spatial_node.content_transform.inverse());
            CoordinateSpaceMapping::ScaleOffset(scale_offset)
        } else {
            let transform = spatial_tree
                .get_relative_transform_with_face(
                    target_node_index,
                    self.ref_spatial_node_index,
                    Some(&mut self.visible_face),
                )
                .into_transform()
                .with_source::<F>()
                .with_destination::<T>();
            CoordinateSpaceMapping::Transform(transform)
        };

        self.current_target_spatial_node_index = target_node_index;
    }

    pub fn get_transform(&self) -> Transform3D<f32, F, T> {
        match self.kind {
            CoordinateSpaceMapping::Local => {
                Transform3D::identity()
            }
            CoordinateSpaceMapping::ScaleOffset(ref scale_offset) => {
                scale_offset.to_transform()
            }
            CoordinateSpaceMapping::Transform(transform) => {
                transform
            }
        }
    }

    pub fn unmap(&self, rect: &Box2D<f32, T>) -> Option<Box2D<f32, F>> {
        match self.kind {
            CoordinateSpaceMapping::Local => {
                Some(rect.cast_unit())
            }
            CoordinateSpaceMapping::ScaleOffset(ref scale_offset) => {
                Some(scale_offset.unmap_rect(rect))
            }
            CoordinateSpaceMapping::Transform(ref transform) => {
                transform.inverse_rect_footprint(rect)
            }
        }
    }

    pub fn map(&self, rect: &Box2D<f32, F>) -> Option<Box2D<f32, T>> {
        match self.kind {
            CoordinateSpaceMapping::Local => {
                Some(rect.cast_unit())
            }
            CoordinateSpaceMapping::ScaleOffset(ref scale_offset) => {
                Some(scale_offset.map_rect(rect))
            }
            CoordinateSpaceMapping::Transform(ref transform) => {
                match project_rect(transform, rect, &self.bounds) {
                    Some(bounds) => {
                        Some(bounds)
                    }
                    None => {
                        warn!("parent relative transform can't transform the primitive rect for {:?}", rect);
                        None
                    }
                }
            }
        }
    }

    // Attempt to return a rect that is contained in the mapped rect.
    pub fn map_inner_bounds(&self, rect: &Box2D<f32, F>) -> Option<Box2D<f32, T>> {
        match self.kind {
            CoordinateSpaceMapping::Local => {
                Some(rect.cast_unit())
            }
            CoordinateSpaceMapping::ScaleOffset(ref scale_offset) => {
                Some(scale_offset.map_rect(rect))
            }
            CoordinateSpaceMapping::Transform(..) => {
                // We could figure out a rect that is contained in the transformed rect but
                // for now we do the simple thing here and bail out.
                return None;
            }
        }
    }

    // Map a local space point to the target coordinate space
    pub fn map_point(&self, p: Point2D<f32, F>) -> Option<Point2D<f32, T>> {
        match self.kind {
            CoordinateSpaceMapping::Local => {
                Some(p.cast_unit())
            }
            CoordinateSpaceMapping::ScaleOffset(ref scale_offset) => {
                Some(scale_offset.map_point(&p))
            }
            CoordinateSpaceMapping::Transform(ref transform) => {
                transform.transform_point2d(p)
            }
        }
    }

    pub fn map_vector(&self, v: Vector2D<f32, F>) -> Vector2D<f32, T> {
        match self.kind {
            CoordinateSpaceMapping::Local => {
                v.cast_unit()
            }
            CoordinateSpaceMapping::ScaleOffset(ref scale_offset) => {
                scale_offset.map_vector(&v)
            }
            CoordinateSpaceMapping::Transform(ref transform) => {
                transform.transform_vector2d(v)
            }
        }
    }

    pub fn as_2d_scale_offset(&self) -> Option<ScaleOffset> {
        self.kind.as_2d_scale_offset()
    }
}


/// Snaps rects to the device pixel grid at frame time, in the space they are
/// actually rasterized in. A snapper is bound to a single raster node (the
/// surface the content is rasterized into) at construction and then reused for
/// many targets via `set_target_spatial_node`, which caches the snapping
/// transform for the last target so re-snapping prims/clips that share a
/// spatial node is cheap.
///
/// The snapping transform is derived from each node's resolved
/// `content_transform` (the node-local -> coordinate-system transform the
/// spatial tree already computed, with device-pixel snapping of reference-frame
/// / scroll offsets baked in), so it is consistent with how content is actually
/// placed. Re-deriving it from raw origins / source transforms would snap rects
/// against a different offset than the node transform renders them at, landing
/// content a sub-pixel off (see bug 1580534).
///
/// Snapping into a surface's raster node (rather than always the root) snaps
/// content in the space it is rasterized in — for a tile cache that excludes
/// the scroll above the raster node, matching the cache's own (scroll-stable)
/// content transform.
///
/// A snapper built for a surface that doesn't snap (`allow_snapping == false`)
/// is disabled and passes every rect through unchanged. Such a surface is a
/// non-snapping raster root (preserve-3d / perspective / huge-scale), where
/// snapping against the surface's own scaled node would use only the tiny local
/// scale and collapse content to zero.
/// Maps a target node's content into the snap node's (device) space so a rect
/// can be snapped to the device grid there.
///
/// A rotation or reflection by a multiple of 90 degrees (the only
/// cross-coordinate-system case we snap across) is fully described by a
/// `ScaleOffset` plus an optional x/y axis swap: the 0/180-degree case is a
/// `ScaleOffset` directly; the 90/270-degree case is the same after swapping x
/// and y. `swap_xy` is always false for a target in the snap node's own
/// coordinate system.
#[derive(Clone, Debug)]
struct SnapTransform {
    scale_offset: ScaleOffset,
    swap_xy: bool,
}

#[derive(Clone, Debug)]
pub struct SpaceSnapper {
    /// If false, `snap_rect` passes rects through unchanged.
    enabled: bool,
    /// Node content is snapped against (the root, or the surface's raster node).
    snap_node_index: SpatialNodeIndex,
    /// Inverse of the snap node's `content_transform`, scaled into device space,
    /// computed once.
    raster_content_inverse: ScaleOffset,
    /// The device scale folded into `raster_content_inverse`; applied separately
    /// on the cross-coordinate-system path.
    snap_scale: f32,
    /// Coordinate system of the snap node. A target in the same coordinate
    /// system snaps with a cheap scale + offset; a target in a different one is
    /// only snappable when the reference frame between them is grid-preserving.
    raster_coord_system_id: CoordinateSystemId,
    /// Last target passed to `set_target_spatial_node`, for the cache below.
    current_target_spatial_node_index: SpatialNodeIndex,
    /// Cached snapping transform for `current_target_spatial_node_index`. `None`
    /// when the target cannot be snapped (a non-axis-aligned reference frame
    /// between it and the snap node).
    snapping_transform: Option<SnapTransform>,
    /// See `set_shadow_offset`.
    shadow_offset: LayoutVector2D,
}

impl SpaceSnapper {
    /// Create a snapper that snaps into `surface`'s raster space (the space the
    /// surface's content is rasterized in).
    ///
    /// A snapping surface snaps against its own raster node, which is the root
    /// for every surface except a picture cache slice. Everything that actually
    /// rasterizes - quad coverage rounding, glyph pen snapping, render task rects,
    /// tile placement - already works in that space, so snapping there is what
    /// keeps a prim's rect on the grid it is drawn on. Snapping against the root
    /// instead leaves a slice whose node sits at a fractional device offset
    /// snapped on a grid half a pixel from the one it rasterizes on, cropping
    /// cached border corners and shifting glyphs relative to their clips (bug
    /// 2062742).
    ///
    /// A surface whose raster node is in the root coordinate system but which
    /// does not itself snap keeps snapping against the root, as before.
    ///
    /// A genuine non-snapping raster root (`allow_snapping == false`:
    /// preserve-3d / perspective, raster node not in the root coordinate system)
    /// stays disabled, since snapping against its own scaled node would collapse
    /// content to zero.
    pub fn new(
        surface: &SurfaceInfo,
        spatial_tree: &SpatialTree,
    ) -> Self {
        let raster_spatial_node_index = surface.raster_spatial_node_index;
        debug_assert!(raster_spatial_node_index != SpatialNodeIndex::INVALID);
        let raster_node = spatial_tree.get_spatial_node(raster_spatial_node_index);
        let raster_in_root = raster_node.coordinate_system_id == CoordinateSystemId::root();

        // `content_transform` is relative to the coordinate system root and
        // carries no device scale, so snapping against the raster node needs the
        // surface's own scale applied on top of it to land on device pixels. The
        // root carries no scale of its own, so that path stays at 1.0.
        let (enabled, snap_node_index, snap_scale) = if raster_in_root && !surface.allow_snapping {
            (true, spatial_tree.root_reference_frame_index(), 1.0)
        } else if surface.allow_snapping {
            // A snapping surface's raster node is expected to always be in the root
            // coordinate system: `assign_surface` only clears `allow_snapping` for a
            // non-snapping raster root, and a tile cache's own node cannot leave the
            // root system (`find_scroll_root` resets across any reference frame that
            // is not a 2d scale/translation). This is the path that newly applies
            // `device_pixel_scale`, so verify rather than assume.
            debug_assert!(
                raster_in_root,
                "snapping surface with a raster node outside the root coordinate system",
            );
            (true, raster_spatial_node_index, surface.device_pixel_scale.0)
        } else {
            (false, raster_spatial_node_index, 1.0)
        };

        let snap_node = spatial_tree.get_spatial_node(snap_node_index);

        SpaceSnapper {
            enabled,
            snap_node_index,
            raster_content_inverse: snap_node.content_transform.inverse().then_scale(snap_scale),
            snap_scale,
            raster_coord_system_id: snap_node.coordinate_system_id,
            current_target_spatial_node_index: SpatialNodeIndex::INVALID,
            snapping_transform: None,
            shadow_offset: LayoutVector2D::zero(),
        }
    }

    /// Snap subsequent rects as shadow copies translated by `offset` (in the
    /// target's local space) from the content they shadow: the rect is
    /// snapped where the shadowed content is, then moved by `offset` rounded to
    /// whole device pixels on the axes that snap, with ties rounded up as the
    /// glyph pens and positive rect edges are (bug 2092300). Snapping the copy on its own
    /// would let the visible offset flip between whole pixels with the
    /// content's sub-pixel phase (bug 2076646).
    pub fn set_shadow_offset(&mut self, offset: LayoutVector2D) {
        self.shadow_offset = offset;
    }

    /// How far to move a shadow copy's rect, in the target's local space, so
    /// that it sits a whole number of device pixels from the shadowed content
    /// on the requested device axes. `None` if there is nothing to adjust.
    pub fn shadow_snap_adjustment<F>(&self, snap_x: bool, snap_y: bool) -> Option<Vector2D<f32, F>> {
        if self.shadow_offset == LayoutVector2D::zero() || (!snap_x && !snap_y) {
            return None;
        }
        let SnapTransform { ref scale_offset, swap_xy } = *self.snapping_transform.as_ref()?;
        let o = self.shadow_offset;
        let (x, y) = if swap_xy { (o.y, o.x) } else { (o.x, o.y) };
        let adjust = |v: f32, scale: f32, snap: bool| {
            if snap && scale != 0.0 { (v * scale + 0.5).floor() / scale - v } else { 0.0 }
        };
        let (ax, ay) = (adjust(x, scale_offset.scale.x, snap_x), adjust(y, scale_offset.scale.y, snap_y));
        Some(if swap_xy { Vector2D::new(ay, ax) } else { Vector2D::new(ax, ay) })
    }

    /// Set the spatial node whose content subsequent `snap_rect` calls snap.
    /// Cheap to re-set with the same target: the snapping transform is cached.
    pub fn set_target_spatial_node(
        &mut self,
        target_node_index: SpatialNodeIndex,
        spatial_tree: &SpatialTree,
    ) {
        if !self.enabled || target_node_index == self.current_target_spatial_node_index {
            return;
        }

        let target_node = spatial_tree.get_spatial_node(target_node_index);

        self.current_target_spatial_node_index = target_node_index;
        self.snapping_transform = if target_node.coordinate_system_id == self.raster_coord_system_id {
            // Same coordinate system: a cheap scale + offset.
            // target-local -> coordinate-system root -> snap-local (or root).
            Some(SnapTransform {
                scale_offset: target_node.content_transform.then(&self.raster_content_inverse),
                swap_xy: false,
            })
        } else {
            // A reference frame between the target and snap node crosses a
            // coordinate system. We can still snap across it if it is a rotation
            // or reflection by a multiple of 90 degrees (with no scaling), because
            // that keeps content on the same pixel grid: snapping in the target's
            // space is identical to snapping in the snap node's, and the content
            // is still rasterized at the same device scale.
            //
            // Anything else can't be grid-snapped: a frame that rescales (e.g. a
            // `rotate-x(45)` that flattens to a y-scale) rasterizes its content in
            // its own local raster space, not the device grid, so snapping against
            // the snap node would shift it; and skew / arbitrary rotation /
            // perspective don't keep the axes aligned at all.
            //
            // TODO: this cross-coordinate-system handling is only needed because
            // a 90/180/270-degree rotation currently establishes a new coordinate
            // system. Such a rotation keeps content on the pixel grid, so it
            // should stay in the parent's coordinate system (as a plain
            // scale/translation frame does); once it does, the target and snap
            // node share a coordinate system and this branch can go away.
            let fwd = spatial_tree
                .get_relative_transform(target_node_index, self.snap_node_index)
                .into_transform();
            fwd.as_grid_aligned_rotation()
                .map(|(scale_offset, swap_xy)| SnapTransform {
                    scale_offset: scale_offset.then_scale(self.snap_scale),
                    swap_xy,
                })
        };
    }

    /// Snap a rect to the device pixel grid to the nearest pixel. Shorthand for
    /// `snap_rect_rounded(rect, SnapRounding::Nearest)`.
    pub fn snap_rect<F>(&self, rect: &Box2D<f32, F>) -> Box2D<f32, F> where F: fmt::Debug {
        self.snap_rect_rounded(rect, SnapRounding::Nearest)
    }

    /// Snap only the requested device axes of a rect to the nearest pixel,
    /// leaving the other axis exact. A text run's glyphs are grid-snapped on one
    /// axis and sub-pixel positioned on the other, and its clip wants to agree
    /// with whichever quantisation the glyphs actually got. `snap_x` / `snap_y`
    /// are DEVICE axes, so `swap_xy` needs no special case.
    pub fn snap_rect_axes<F>(
        &self,
        rect: &Box2D<f32, F>,
        snap_x: bool,
        snap_y: bool,
    ) -> Box2D<f32, F> where F: fmt::Debug {
        match self.shadow_snap_adjustment(snap_x, snap_y) {
            Some(adjustment) => {
                let offset = self.shadow_offset.cast_unit();
                self.snap_rect_axes_impl(&rect.translate(-offset), snap_x, snap_y)
                    .translate(offset + adjustment)
            }
            None => self.snap_rect_axes_impl(rect, snap_x, snap_y),
        }
    }

    fn snap_rect_axes_impl<F>(
        &self,
        rect: &Box2D<f32, F>,
        snap_x: bool,
        snap_y: bool,
    ) -> Box2D<f32, F> where F: fmt::Debug {
        debug_assert!(!self.enabled || self.current_target_spatial_node_index != SpatialNodeIndex::INVALID);
        if !snap_x && !snap_y {
            return *rect;
        }
        match self.snapping_transform {
            Some(SnapTransform { ref scale_offset, swap_xy }) => {
                let r = if swap_xy { swap_box_xy(rect) } else { *rect };
                let device_rect: DeviceRect = scale_offset.map_rect(&r);
                let snapped = DeviceRect::new(
                    DevicePoint::new(
                        if snap_x { device_rect.min.x.round() } else { device_rect.min.x },
                        if snap_y { device_rect.min.y.round() } else { device_rect.min.y },
                    ),
                    DevicePoint::new(
                        if snap_x { device_rect.max.x.round() } else { device_rect.max.x },
                        if snap_y { device_rect.max.y.round() } else { device_rect.max.y },
                    ),
                );
                let unmapped: Box2D<f32, F> = scale_offset.unmap_rect(&snapped);
                if swap_xy { swap_box_xy(&unmapped) } else { unmapped }
            }
            None => *rect,
        }
    }

    /// Snap a rect to the device pixel grid using the current target's snapping
    /// transform: map the rect into device space, round it to the grid per
    /// `rounding`, then map it back. A target that can't be snapped (or a
    /// disabled snapper) leaves the rect unchanged. See `SnapRounding` for what
    /// each mode is for.
    pub fn snap_rect_rounded<F>(&self, rect: &Box2D<f32, F>, rounding: SnapRounding) -> Box2D<f32, F> where F: fmt::Debug {
        let snaps = rounding != SnapRounding::Exact;
        match self.shadow_snap_adjustment(snaps, snaps) {
            Some(adjustment) => {
                let offset = self.shadow_offset.cast_unit();
                self.snap_rect_rounded_impl(&rect.translate(-offset), rounding)
                    .translate(offset + adjustment)
            }
            None => self.snap_rect_rounded_impl(rect, rounding),
        }
    }

    fn snap_rect_rounded_impl<F>(&self, rect: &Box2D<f32, F>, rounding: SnapRounding) -> Box2D<f32, F> where F: fmt::Debug {
        debug_assert!(!self.enabled || self.current_target_spatial_node_index != SpatialNodeIndex::INVALID);
        // Short-circuit rather than round-tripping through the snap transform,
        // which wouldn't be bit-exact.
        if rounding == SnapRounding::Exact {
            return *rect;
        }
        match self.snapping_transform {
            Some(SnapTransform { ref scale_offset, swap_xy }) => {
                let rect = if swap_xy { swap_box_xy(rect) } else { *rect };
                let device_rect: DeviceRect = scale_offset.map_rect(&rect);
                let snapped: DeviceRect = match rounding {
                    SnapRounding::Exact => unreachable!("returned above"),
                    SnapRounding::Nearest => device_rect.snap(),
                    SnapRounding::RoundOut => device_rect.round_out(),
                    SnapRounding::Line { horizontal } =>
                        snap_line_device_rect(&device_rect, horizontal ^ swap_xy),
                    SnapRounding::BorderInner { .. } if swap_xy => device_rect.snap(),
                    SnapRounding::BorderInner { ref widths } => {
                        // Map the inner rect from layout space the way content
                        // meeting it maps its own rect, so both round a tie the
                        // same way.
                        let inner: Box2D<f32, F> = Box2D::new(
                            Point2D::new(rect.min.x + widths.left, rect.min.y + widths.top),
                            Point2D::new(rect.max.x - widths.right, rect.max.y - widths.bottom),
                        );
                        let inner_device: DeviceRect = scale_offset.map_rect(&inner);
                        snap_border_device_rect(
                            &device_rect,
                            &inner_device,
                            widths,
                            scale_offset.scale.x,
                            scale_offset.scale.y,
                        )
                    }
                };
                let unmapped: Box2D<f32, F> = scale_offset.unmap_rect(&snapped);
                if swap_xy { swap_box_xy(&unmapped) } else { unmapped }
            }
            None => *rect,
        }
    }
}

/// How a rect is rounded to the device pixel grid by `snap_rect_rounded`.
#[derive(Debug, Copy, Clone, PartialEq)]
pub enum SnapRounding {
    /// Don't round: leave the rect at its exact sub-pixel position. For a prim
    /// that resolves its own edges in the shader (anti-aliasing), where moving an
    /// edge to the grid is what the anti-aliasing is there to avoid.
    Exact,
    /// Round each edge to the nearest device pixel. Crisp fills / borders that
    /// tile seamlessly with neighbours (`round(max_of_A) == round(min_of_B)`).
    Nearest,
    /// Round the device rect *outward* (floor min, ceil max). The result still
    /// lands on the grid but fully contains the mapped rect and never shifts an
    /// edge inward. Grid-aligns the footprint of a prim that is not itself
    /// snapped (device-space text): the content stays at its exact sub-pixel
    /// position while its bounding rect stays conservative and pixel-aligned for
    /// surface / cluster allocation (bug 2050692).
    RoundOut,
    /// Round the long (length) axis to nearest, but snap the thin (thickness)
    /// axis to a single phase-independent whole-pixel extent (floored at 1 for a
    /// non-empty line). Rounding both thin-axis edges independently makes a
    /// sub-device-pixel-thick decoration line vanish (extent rounds to 0) or
    /// double (rounds to 2) depending on the sub-pixel position it lands at for
    /// the current scale (bug 1783779); snapping the extent once removes that
    /// phase dependence. `horizontal` is the line orientation in the target's
    /// own space (a horizontal line is thin in Y).
    Line { horizontal: bool },
    /// Round a border's edges to the nearest pixel, then pull an outer edge in
    /// where the whole-pixel width measured from it (each side's width rounded
    /// as `NormalBorderData` rounds it) would stop short of the inner edge's
    /// own nearest pixel. Content that meets the border in layout snaps to that
    /// pixel, so the shortfall shows as a gap between them (bug 2074176). The
    /// border never grows past its `Nearest` rect. Falls back to `Nearest` when
    /// a width is not snapped to whole pixels, or across a 90-degree rotation.
    BorderInner { widths: LayoutSideOffsets },
}

/// Snap a device-space border rect so its inner edges never fall short of
/// the content they meet. See `SnapRounding::BorderInner`.
fn snap_border_device_rect(
    r: &DeviceRect,
    inner: &DeviceRect,
    widths: &LayoutSideOffsets,
    scale_x: f32,
    scale_y: f32,
) -> DeviceRect {
    // Mirrors the width snapping in `NormalBorderData::update`: widths of at
    // least one layout pixel round to whole device pixels, zero stays zero.
    let snapped_width = |w: f32, s: f32| {
        if w == 0.0 {
            Some(0.0)
        } else if w >= 1.0 && s > 0.0 {
            Some((w * s).round().max(1.0))
        } else {
            None
        }
    };
    let (Some(left), Some(top), Some(right), Some(bottom)) = (
        snapped_width(widths.left, scale_x),
        snapped_width(widths.top, scale_y),
        snapped_width(widths.right, scale_x),
        snapped_width(widths.bottom, scale_y),
    ) else {
        return r.snap();
    };

    if inner.is_empty() {
        return r.snap();
    }
    let outer = r.snap();
    let inner = inner.snap();

    // Content meeting the inner edge snaps it to `inner`. Where the whole-pixel
    // width measured from the snapped outer edge stops short of that, the gap
    // would show, so pull the outer edge in to close it. Where it reaches past
    // `inner` instead, the outer edge stays put, so the border never grows past
    // its own snapped rect.
    let snapped = DeviceRect::new(
        DevicePoint::new(outer.min.x.max(inner.min.x - left), outer.min.y.max(inner.min.y - top)),
        DevicePoint::new(outer.max.x.min(inner.max.x + right), outer.max.y.min(inner.max.y + bottom)),
    );
    if snapped.width() < left + right || snapped.height() < top + bottom {
        return outer;
    }
    snapped
}

/// Snap a device-space decoration-line rect: round both edges of the long axis
/// to the grid, and snap the thin axis to a phase-independent whole-pixel
/// extent (floored at 1 for a non-empty line). See `SnapRounding::Line`.
fn snap_line_device_rect(r: &DeviceRect, thin_is_y: bool) -> DeviceRect {
    let snap_extent = |e: f32| if e > 0.0 { e.round().max(1.0) } else { 0.0 };
    if thin_is_y {
        let min_y = r.min.y.round();
        DeviceRect::from_floats(
            r.min.x.round(),
            min_y,
            r.max.x.round(),
            min_y + snap_extent(r.max.y - r.min.y),
        )
    } else {
        let min_x = r.min.x.round();
        DeviceRect::from_floats(
            min_x,
            r.min.y.round(),
            min_x + snap_extent(r.max.x - r.min.x),
            r.max.y.round(),
        )
    }
}

/// Swap the x and y coordinates of a rect, mapping it through the `(x, y) ->
/// (y, x)` reflection. Used to fold a 90/270-degree axis swap into a
/// `ScaleOffset` snap; it is its own inverse.
fn swap_box_xy<F>(r: &Box2D<f32, F>) -> Box2D<f32, F> {
    Box2D::new(
        Point2D::new(r.min.y, r.min.x),
        Point2D::new(r.max.y, r.max.x),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use api::{PipelineId, PropertyBinding, ReferenceFrameKind, StickyOffsetBounds, TransformStyle};
    use api::units::{
        DevicePixelScale, DevicePoint, DeviceSize, LayoutPoint, LayoutRect, LayoutSize,
        LayoutTransform, LayoutVector2D,
    };
    use crate::scene::SceneProperties;
    use crate::spatial_node::StickyFrameInfo;
    use crate::spatial_tree::{SceneSpatialTree, SpatialTree};
    use crate::surface::SurfaceInfo;

    #[test]
    fn test_as_grid_aligned_rotation() {
        let deg = |d: f32| euclid::Angle::degrees(d);

        // Snappable: 90/180/270-degree rotations, reflections, identity. The
        // 90/270-degree cases swap x and y; the others do not.
        for (d, expect_swap) in [(0.0, false), (90.0, true), (180.0, false), (270.0, true), (-90.0, true)] {
            let rot = LayoutTransform::rotation(0.0, 0.0, 1.0, deg(d)).as_grid_aligned_rotation();
            assert_eq!(
                rot.map(|(_, swap)| swap),
                Some(expect_swap),
                "rotate-z({d}) should be a grid-aligned rotation with swap_xy={expect_swap}",
            );
        }
        assert!(LayoutTransform::identity().as_grid_aligned_rotation().is_some());
        assert!(LayoutTransform::scale(-1.0, 1.0, 1.0).as_grid_aligned_rotation().is_some());
        assert!(LayoutTransform::rotation(0.0, 0.0, 1.0, deg(90.0))
            .then_translate(euclid::vec3(12.0, -7.0, 0.0))
            .as_grid_aligned_rotation()
            .is_some());

        // Not snappable: a 45-degree z-rotation doesn't keep the axes aligned.
        assert!(LayoutTransform::rotation(0.0, 0.0, 1.0, deg(45.0)).as_grid_aligned_rotation().is_none());
        // A non-unit scale rescales the grid (e.g. a flattened rotate-x).
        assert!(LayoutTransform::scale(1.0, 0.707, 1.0).as_grid_aligned_rotation().is_none());
        // Perspective has identity 2x2 and unit scale, but m34 != 0: must be
        // rejected (the bug that broke the css-transforms/perspective WPTs).
        let mut perspective = LayoutTransform::identity();
        perspective.m34 = -1.0 / 500.0;
        assert!(perspective.as_grid_aligned_rotation().is_none());
        // z-coupling (e.g. rotate-x leaving a residual) must be rejected.
        assert!(LayoutTransform::rotation(1.0, 0.0, 0.0, deg(30.0)).as_grid_aligned_rotation().is_none());
    }

    // Bug 2004666: content rasterized into a snapping surface (e.g. a sticky /
    // scrolled tile cache) whose raster node is in the root coordinate system but
    // is offset from root by a fractional, un-snapped amount must land on whole
    // device pixels once composited, or glyphs and line decorations jitter
    // sub-pixel while scrolling. A `should_snap:false` 2d-scale-translation
    // reference frame at a fractional offset reproduces that fractional cs-origin.
    //
    // This originally asserted the *mechanism* - that such a surface snaps against
    // the root rather than against its own node. That is no longer the mechanism
    // that achieves it: a slice composites its tiles at a *rounded* device offset
    // (`get_relative_scale_offset`), so snapping in the slice's own space is what
    // puts content on whole device pixels, while snapping against the root leaves
    // the fraction inside the content for the coverage rounding to quantize away
    // (bug 2062742). Assert the outcome both mechanisms were reaching for.
    fn assert_content_lands_on_device_pixels(st: &SpatialTree, raster_node: SpatialNodeIndex) {
        // Precondition: the raster node is in the root coordinate system but
        // offset from root by a fractional amount.
        let node = st.get_spatial_node(raster_node);
        assert_eq!(node.coordinate_system_id, CoordinateSystemId::root());
        assert!(
            (node.content_transform.offset.x - 0.4).abs() < 0.0001,
            "expected fractional cs-origin, got {:?}",
            node.content_transform.offset,
        );

        // A snapping surface rasterized into that node (allow_snapping = true,
        // like a tile cache).
        let surface = SurfaceInfo::new(
            raster_node,
            raster_node,
            DeviceRect::from_origin_and_size(DevicePoint::zero(), DeviceSize::new(1000.0, 1000.0)),
            st,
            DevicePixelScale::new(1.0),
            (1.0, 1.0),
            (1.0, 1.0),
            (1.0, 1.0),
            true,
            false,
        );

        let mut snapper = SpaceSnapper::new(&surface, st);
        snapper.set_target_spatial_node(raster_node, st);

        let rect = LayoutRect::from_origin_and_size(
            LayoutPoint::new(20.0, 40.0),
            LayoutSize::new(60.0, 20.0),
        );
        let snapped = snapper.snap_rect(&rect);

        // Where the content actually lands: the cache composites its tiles at this
        // offset, which `get_relative_scale_offset` rounds to a whole device pixel.
        let composite = crate::picture::get_relative_scale_offset(
            raster_node,
            st.root_reference_frame_index(),
            st,
        );
        let device_x = snapped.min.x * composite.scale.x + composite.offset.x;
        let device_y = snapped.min.y * composite.scale.y + composite.offset.y;

        assert!(
            (device_x - device_x.round()).abs() < 0.01
                && (device_y - device_y.round()).abs() < 0.01,
            "expected snapped content to composite on whole device pixels, got              ({device_x}, {device_y}) from snapped local {:?} at composite offset {:?}",
            snapped.min,
            composite.offset,
        );
    }

    fn add_fractional_ref_frame(
        cst: &mut SceneSpatialTree,
        parent: SpatialNodeIndex,
    ) -> SpatialNodeIndex {
        // A 2d-scale-translation reference frame stays in the root coordinate
        // system, and with should_snap:false its fractional offset is not
        // device-snapped — exactly the situation a sticky tile cache hits under
        // layout.disable-pixel-alignment.
        cst.add_reference_frame(
            parent,
            TransformStyle::Flat,
            PropertyBinding::Value(LayoutTransform::translation(0.4, 0.4, 0.0)),
            ReferenceFrameKind::Transform {
                is_2d_scale_translation: true,
                should_snap: false,
                paired_with_perspective: false,
            },
            LayoutVector2D::zero(),
            PipelineId::dummy(),
            false,
        )
    }

    #[test]
    fn test_root_cs_surface_content_lands_on_device_pixels() {
        let mut cst = SceneSpatialTree::new();
        let root = cst.root_reference_frame_index();
        let frac = add_fractional_ref_frame(&mut cst, root);

        let mut st = SpatialTree::new();
        st.apply_updates(cst.end_frame_and_get_pending_updates());
        st.update_tree(&SceneProperties::new());

        assert_content_lands_on_device_pixels(&st, frac);
    }

    #[test]
    fn test_sticky_cache_content_lands_on_device_pixels() {
        // The same fractional cs-origin reached through a sticky frame (which gets
        // its own tile cache): content in the sticky cache must land on whole
        // device pixels too - this is the configuration that jittered.
        let mut cst = SceneSpatialTree::new();
        let root = cst.root_reference_frame_index();
        let frac = add_fractional_ref_frame(&mut cst, root);

        let sticky = cst.add_sticky_frame(
            frac,
            StickyFrameInfo {
                frame_rect: LayoutRect::from_size(LayoutSize::new(400.0, 100.0)),
                margins: euclid::SideOffsets2D::new(None, None, None, None),
                vertical_offset_bounds: StickyOffsetBounds::new(0.0, 0.0),
                horizontal_offset_bounds: StickyOffsetBounds::new(0.0, 0.0),
                current_offset: LayoutVector2D::zero(),
                transform: None,
            },
            PipelineId::dummy(),
        );

        let mut st = SpatialTree::new();
        st.apply_updates(cst.end_frame_and_get_pending_updates());
        st.update_tree(&SceneProperties::new());

        assert_content_lands_on_device_pixels(&st, sticky);
    }

    #[test]
    fn test_grid_preserving_rotation_snaps_across_coord_system() {
        // A 90-degree rotation creates a *new* coordinate system, but it is
        // grid-preserving (maps axis-aligned rects to axis-aligned rects on the
        // device grid), so content under it must still snap to the device grid
        // against root. Earlier the cross-coordinate-system case bailed out of
        // snapping entirely, leaving line decorations under writing-mode /
        // rotation a sub-pixel off (bug 2004666 reftest regression).
        let mut cst = SceneSpatialTree::new();
        let root = cst.root_reference_frame_index();

        let rot = cst.add_reference_frame(
            root,
            TransformStyle::Flat,
            PropertyBinding::Value(LayoutTransform::rotation(
                0.0,
                0.0,
                1.0,
                euclid::Angle::degrees(-90.0),
            )),
            ReferenceFrameKind::Transform {
                is_2d_scale_translation: false,
                should_snap: false,
                paired_with_perspective: false,
            },
            LayoutVector2D::zero(),
            PipelineId::dummy(),
            false,
        );

        let mut st = SpatialTree::new();
        st.apply_updates(cst.end_frame_and_get_pending_updates());
        st.update_tree(&SceneProperties::new());

        // Precondition: the rotation really is in a different coordinate system.
        assert_ne!(
            st.get_spatial_node(rot).coordinate_system_id,
            CoordinateSystemId::root(),
            "expected rotation to establish a new coordinate system",
        );

        // Surface rasterized into root (allow_snapping = true); content's spatial
        // node is the rotation frame.
        let surface = SurfaceInfo::new(
            root,
            root,
            DeviceRect::from_origin_and_size(DevicePoint::zero(), DeviceSize::new(1000.0, 1000.0)),
            &st,
            DevicePixelScale::new(1.0),
            (1.0, 1.0),
            (1.0, 1.0),
            (1.0, 1.0),
            true,
            false,
        );

        let mut snapper = SpaceSnapper::new(&surface, &st);
        snapper.set_target_spatial_node(rot, &st);

        // A fractional rect in the rotated frame must land on the integer grid
        // once mapped to device space - i.e. it really got snapped, not passed
        // through unchanged.
        let rect = LayoutRect::from_origin_and_size(
            LayoutPoint::new(10.3, 20.7),
            LayoutSize::new(40.4, 2.6),
        );
        let snapped = snapper.snap_rect(&rect);

        let to_root = st.get_relative_transform(rot, root).into_transform();
        for corner in [snapped.min, snapped.max] {
            let device = to_root.transform_point2d(corner).unwrap();
            assert!(
                (device.x - device.x.round()).abs() < 0.01
                    && (device.y - device.y.round()).abs() < 0.01,
                "snapped corner {:?} -> device {:?} not on the integer grid",
                corner,
                device,
            );
        }
    }

    #[test]
    fn test_shadow_offset_snaps_relative_to_shadowed_content() {
        // Under a scale(1.25), a 1.25 shadow offset is 1.5625 device pixels.
        // Whatever the shadowed rect's sub-pixel phase, the snapped copy must
        // land exactly 2 device pixels from the snapped original (bug 2076646).
        let mut cst = SceneSpatialTree::new();
        let root = cst.root_reference_frame_index();
        let scaled = cst.add_reference_frame(
            root,
            TransformStyle::Flat,
            PropertyBinding::Value(LayoutTransform::scale(1.25, 1.25, 1.0)),
            ReferenceFrameKind::Transform {
                is_2d_scale_translation: true,
                should_snap: false,
                paired_with_perspective: false,
            },
            LayoutVector2D::zero(),
            PipelineId::dummy(),
            false,
        );

        let mut st = SpatialTree::new();
        st.apply_updates(cst.end_frame_and_get_pending_updates());
        st.update_tree(&SceneProperties::new());

        let surface = SurfaceInfo::new(
            root,
            root,
            DeviceRect::from_origin_and_size(DevicePoint::zero(), DeviceSize::new(1000.0, 1000.0)),
            &st,
            DevicePixelScale::new(1.0),
            (1.0, 1.0),
            (1.0, 1.0),
            (1.0, 1.0),
            true,
            false,
        );

        let offset = LayoutVector2D::new(1.25, 1.25);
        let mut snapper = SpaceSnapper::new(&surface, &st);
        snapper.set_target_spatial_node(scaled, &st);
        let mut shadow_snapper = snapper.clone();
        shadow_snapper.set_shadow_offset(offset);

        for phase in [0.0, 0.1, 0.2, 0.3, 0.5, 0.7] {
            let rect = LayoutRect::from_origin_and_size(
                LayoutPoint::new(10.0 + phase, 40.0 + phase),
                LayoutSize::new(40.0, 8.0),
            );
            let original = snapper.snap_rect(&rect);
            let copy = shadow_snapper.snap_rect(&rect.translate(offset));
            let delta = (copy.min - original.min) * 1.25;
            assert!(
                (delta.x - 2.0).abs() < 0.001 && (delta.y - 2.0).abs() < 0.001,
                "phase {}: copy is {:?} device pixels from the original",
                phase,
                delta,
            );

            // An unsnapped prim keeps its exact offset.
            let exact = shadow_snapper.snap_rect_rounded(&rect.translate(offset), SnapRounding::Exact);
            assert_eq!(exact, rect.translate(offset));
        }
    }

    #[test]
    fn test_shadow_offset_half_pixel_ties_round_up() {
        // A half device pixel shadow offset moves the copy the way pixel
        // aligned content would have snapped on its own: -0.5 to 0 and 0.5 to
        // 1 (bug 2092300).
        let mut cst = SceneSpatialTree::new();
        let root = cst.root_reference_frame_index();
        let mut st = SpatialTree::new();
        st.apply_updates(cst.end_frame_and_get_pending_updates());
        st.update_tree(&SceneProperties::new());

        let surface = SurfaceInfo::new(
            root,
            root,
            DeviceRect::from_origin_and_size(DevicePoint::zero(), DeviceSize::new(1000.0, 1000.0)),
            &st,
            DevicePixelScale::new(1.0),
            (1.0, 1.0),
            (1.0, 1.0),
            (1.0, 1.0),
            true,
            false,
        );

        let mut snapper = SpaceSnapper::new(&surface, &st);
        snapper.set_target_spatial_node(root, &st);

        for (offset, expected) in [(-0.5, 0.0), (0.5, 1.0), (-1.5, -1.0), (1.5, 2.0)] {
            let offset = LayoutVector2D::new(offset, offset);
            let mut shadow_snapper = snapper.clone();
            shadow_snapper.set_shadow_offset(offset);
            for phase in [0.0, 0.25, 0.5, 0.75] {
                let rect = LayoutRect::from_origin_and_size(
                    LayoutPoint::new(10.0 + phase, 40.0 + phase),
                    LayoutSize::new(40.0, 8.0),
                );
                let original = snapper.snap_rect(&rect);
                let copy = shadow_snapper.snap_rect(&rect.translate(offset));
                let delta = copy.min - original.min;
                assert!(
                    (delta.x - expected).abs() < 0.001 && (delta.y - expected).abs() < 0.001,
                    "offset {:?} phase {}: copy is {:?} device pixels from the original",
                    offset,
                    phase,
                    delta,
                );
            }
        }
    }
}

