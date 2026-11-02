/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! A self-contained description of the clips applied to a quad primitive.
//!
//! The types here hold the clipping information already pulled out of the clip
//! store and the clip interner, so that the quad path never has to resolve a
//! handle or an index range. They are the clip-side analogue of
//! `QuadTransformState`, which hides the details of incoming transforms behind
//! a small owned type, and a step towards splitting the low-level 2D rendering
//! code away from the data structures that represent WebRender's scene.
//!
//! `ClipStore::fill_quad_clips` is the bridge that builds a `QuadClipStack` out
//! of a `ClipChainInstance`. It deliberately lives in `clip.rs`, on the side of
//! the split that knows about both.

use api::{BorderRadius, ClipMode, Path, FillRule, units::*};

use crate::clip::BorderClipKey;
use crate::quad::QuadTransformState;
use crate::render_task_graph::RenderTaskId;
use crate::segment::EdgeMask;
use crate::spatial_tree::SpatialNodeIndex;
use crate::util::MaxRect;

/// The shape of a clip applied to a quad primitive.
#[derive(Clone, Debug)]
pub enum QuadClipShape {
    Border { key: BorderClipKey },
    Rectangle {
        mode: ClipMode,
    },
    RoundedRectangle {
        radius: BorderRadius,
        inset: LayoutSideOffsets,
        mode: ClipMode,
    },
    /// A mask image, already rasterized into `tile_count` tiles starting at
    /// `first_tile` in `QuadClipStack::mask_tiles`.
    Mask {
        first_tile: u32,
        tile_count: u32,
    },
    Path {
        path: Path,
        fill_rule: FillRule,
        /// Positions the path in the clip's space. Unlike `QuadClip::rect`, it
        /// is not snapped to the device pixel grid.
        origin: LayoutPoint,
    },
}

/// One clip applied to a quad primitive.
#[derive(Clone, Debug)]
pub struct QuadClip {
    pub shape: QuadClipShape,
    /// The clip's rect, in `spatial_node`'s local space.
    pub rect: LayoutRect,
    pub spatial_node: SpatialNodeIndex,
    /// Identifies the clip's shape for `QuadCacheKey`. Clips with the same shape
    /// but different positioning share a uid, matching the interning key.
    pub uid: u64,
}

/// One rasterized tile of a mask-image clip.
#[derive(Copy, Clone, Debug)]
pub struct QuadMaskTile {
    pub rect: LayoutRect,
    pub task_id: RenderTaskId,
}

/// The clips applied to a quad primitive.
///
/// Holds no handles into the clip store, the clip interner or the clip tree, so
/// consumers never have to resolve one.
///
/// The buffers are meant to be reused across a whole primitive list rather than
/// allocated per primitive: construct one alongside the `QuadTransformState` and
/// refill it with `clear` plus the push methods, or with
/// `ClipStore::fill_quad_clips`.
pub struct QuadClipStack {
    clips: Vec<QuadClip>,
    mask_tiles: Vec<QuadMaskTile>,
    local_clip_rect: LayoutRect,
    coverage_rect: DeviceRect,
    surface_clip_rect: DeviceRect,
    needs_mask: bool,
}

impl QuadClipStack {
    pub fn new() -> Self {
        QuadClipStack {
            clips: Vec::new(),
            mask_tiles: Vec::new(),
            local_clip_rect: LayoutRect::max_rect(),
            coverage_rect: DeviceRect::zero(),
            surface_clip_rect: DeviceRect::max_rect(),
            needs_mask: false,
        }
    }

    pub fn clips(&self) -> &[QuadClip] {
        &self.clips
    }

    pub fn len(&self) -> u32 {
        self.clips.len() as u32
    }

    pub fn is_empty(&self) -> bool {
        self.clips.is_empty()
    }

    /// The tiles of a mask-image clip, or an empty slice for any other shape.
    pub fn mask_tiles(&self, clip: &QuadClip) -> &[QuadMaskTile] {
        match clip.shape {
            QuadClipShape::Mask { first_tile, tile_count } => {
                let start = first_tile as usize;
                &self.mask_tiles[start .. start + tile_count as usize]
            }
            _ => &[],
        }
    }

    /// Whether any of these clips needs a mask rendered for it.
    ///
    /// Not derivable from the clips: a `Rectangle` clip in the primitive's
    /// coordinate system is folded into `local_clip_rect` and handled by the
    /// vertex shader, so a non-empty stack can still need no mask.
    pub fn needs_mask(&self) -> bool {
        self.needs_mask
    }

    /// The bounding rects of the `Clip`-mode clips that are in the primitive's
    /// coordinate system, intersected together in the primitive's local space.
    /// Does not include the primitive's own extent.
    pub fn local_clip_rect(&self) -> LayoutRect {
        self.local_clip_rect
    }

    /// The primitive's clipped coverage rect, in the device space of the surface
    /// it is drawn into. Not intersected with that surface's clipping rect.
    pub fn coverage_rect(&self) -> DeviceRect {
        self.coverage_rect
    }

    /// The clipping rect of the surface the primitive is drawn into, in that
    /// surface's device space. Nothing outside of it can be rasterized.
    pub fn surface_clip_rect(&self) -> DeviceRect {
        self.surface_clip_rect
    }

    pub fn clear(&mut self) {
        self.clips.clear();
        self.mask_tiles.clear();
        self.local_clip_rect = LayoutRect::max_rect();
        self.coverage_rect = DeviceRect::zero();
        self.surface_clip_rect = DeviceRect::max_rect();
        self.needs_mask = false;
    }

    pub fn set_bounds(
        &mut self,
        local_clip_rect: LayoutRect,
        coverage_rect: DeviceRect,
        surface_clip_rect: DeviceRect,
        needs_mask: bool,
    ) {
        self.local_clip_rect = local_clip_rect;
        self.coverage_rect = coverage_rect;
        self.surface_clip_rect = surface_clip_rect;
        self.needs_mask = needs_mask;
    }

    pub fn push_rect(
        &mut self,
        rect: LayoutRect,
        mode: ClipMode,
        spatial_node: SpatialNodeIndex,
        uid: u64,
    ) {
        self.clips.push(QuadClip {
            shape: QuadClipShape::Rectangle { mode },
            rect,
            spatial_node,
            uid,
        });
    }

    pub fn push_path(
        &mut self,
        rect: LayoutRect,
        path: Path,
        fill_rule: FillRule,
        origin: LayoutPoint,
        spatial_node: SpatialNodeIndex,
        uid: u64,
    ) {
        self.clips.push(QuadClip {
            shape: QuadClipShape::Path { path, fill_rule, origin },
            rect,
            spatial_node,
            uid,
        });
    }

    pub fn push_rounded_rect(
        &mut self,
        rect: LayoutRect,
        radius: BorderRadius,
        inset: LayoutSideOffsets,
        mode: ClipMode,
        spatial_node: SpatialNodeIndex,
        uid: u64,
    ) {
        self.clips.push(QuadClip {
            shape: QuadClipShape::RoundedRectangle { radius, inset, mode },
            rect,
            spatial_node,
            uid,
        });
    }

    pub fn push_border(
        &mut self, rect: LayoutRect, key: BorderClipKey,
        spatial_node: SpatialNodeIndex, uid: u64,
    ) {
        self.clips.push(QuadClip {
            shape: QuadClipShape::Border { key }, rect, spatial_node, uid,
        });
    }

    pub fn adjust_border_clip_coverage(
        &self,
        local_bounds: LayoutRect,
        aa_edges: EdgeMask,
        transform: &QuadTransformState,
        task_rect: &DeviceRect,
    ) -> (LayoutRect, EdgeMask) {
        let Some(clip) = self.clips.iter().find(|clip| {
            matches!(clip.shape, QuadClipShape::Border { .. })
                && clip.spatial_node == transform.prim_spatial_node_index()
        }) else {
            return (local_bounds, aa_edges);
        };
        let mut shared_edges = EdgeMask::empty();
        shared_edges.set(EdgeMask::LEFT, local_bounds.min.x == clip.rect.min.x);
        shared_edges.set(EdgeMask::TOP, local_bounds.min.y == clip.rect.min.y);
        shared_edges.set(EdgeMask::RIGHT, local_bounds.max.x == clip.rect.max.x);
        shared_edges.set(EdgeMask::BOTTOM, local_bounds.max.y == clip.rect.max.y);
        if shared_edges.is_empty() {
            return (local_bounds, aa_edges);
        }
        let Some(task_bounds) = transform.unmap_rect(&task_rect.inflate(1.0, 1.0)) else {
            return (local_bounds, aa_edges);
        };
        // Extend shared edges past the task so only the border mask antialiases them.
        let bounds = shared_edges.select(local_bounds.union(&task_bounds), local_bounds);
        (bounds, aa_edges & !shared_edges)
    }

    pub fn push_mask(
        &mut self,
        rect: LayoutRect,
        spatial_node: SpatialNodeIndex,
        uid: u64,
        tiles: impl Iterator<Item = QuadMaskTile>,
    ) {
        let first_tile = self.mask_tiles.len() as u32;
        self.mask_tiles.extend(tiles);
        let tile_count = self.mask_tiles.len() as u32 - first_tile;

        self.clips.push(QuadClip {
            shape: QuadClipShape::Mask { first_tile, tile_count },
            rect,
            spatial_node,
            uid,
        });
    }
}

impl Default for QuadClipStack {
    fn default() -> Self {
        QuadClipStack::new()
    }
}
