/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::ColorF;
use api::{ImageRendering, LineOrientation, PrimitiveFlags};
use api::units::*;
use crate::clip::ClipNodeId;
use crate::render_backend::{DataStores, DlStores};
use crate::space::SnapRounding;
use crate::quad::QuadTileClassifier;
use crate::renderer::GpuBufferHandle;
use crate::segment::EdgeMask;
use crate::debug_item::{DebugItem, DebugMessage};
use crate::debug_colors;
use glyph_rasterizer::GlyphKey;
use crate::gpu_types::QuadSegment;
use crate::intern;
use crate::picture::{PictureInstance, PictureScratch};
use crate::render_task_graph::RenderTaskId;
use crate::resource_cache::ImageProperties;
use crate::util::Recycler;
use crate::internal_types::{FastHashSet, LayoutPrimitiveInfo};
use crate::visibility::{BackdropCaptures, PrimitiveDrawHeader, PrimitiveDrawIndex};
use std::ops;

pub mod backdrop;
pub mod borders;
pub mod gradient;
pub mod image;
pub mod line_dec;
pub mod picture;
pub mod rectangle;
pub mod text_run;
pub mod interned;

pub mod storage;

use backdrop::{BackdropCaptureDataHandle, BackdropRenderDataHandle};
use borders::{ImageBorderDataHandle, NormalBorderDataHandle};
use gradient::{LinearGradientDataHandle, RadialGradientDataHandle, ConicGradientDataHandle};
use image::{ImagePrimKey, YuvImagePrimKey};
use line_dec::LineDecorationKey;
use picture::PictureDataHandle;
use rectangle::RectangleKey;
use crate::dl_interner::DlHandle;
use text_run::{TextRunDataHandle, TextRunScratch};
use crate::box_shadow::BoxShadowKey;

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, Copy, Clone, MallocSizeOf)]
pub struct PrimitiveOpacity {
    pub is_opaque: bool,
}

impl PrimitiveOpacity {
    pub fn opaque() -> PrimitiveOpacity {
        PrimitiveOpacity { is_opaque: true }
    }

    pub fn translucent() -> PrimitiveOpacity {
        PrimitiveOpacity { is_opaque: false }
    }

    pub fn from_alpha(alpha: f32) -> PrimitiveOpacity {
        PrimitiveOpacity {
            is_opaque: alpha >= 1.0,
        }
    }
}

/// For external images, it's not possible to know the
/// UV coords of the image (or the image data itself)
/// until the render thread receives the frame and issues
/// callbacks to the client application. For external
/// images that are visible, a DeferredResolve is created
/// that is stored in the frame. This allows the render
/// thread to iterate this list and update any changed
/// texture data and update the UV rect. Any filtering
/// is handled externally for NativeTexture external
/// images.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct DeferredResolve {
    pub handle: GpuBufferHandle,
    pub image_properties: ImageProperties,
    pub rendering: ImageRendering,
    pub is_composited: bool,
}

#[derive(Debug, Copy, Clone, PartialEq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct ClipTaskIndex(pub u32);

impl ClipTaskIndex {
    pub const INVALID: ClipTaskIndex = ClipTaskIndex(0);
}

#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash, MallocSizeOf, Ord, PartialOrd)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PictureIndex(pub u32);

impl PictureIndex {
    pub const INVALID: PictureIndex = PictureIndex(u32::MAX);
}

// `PolygonKey` now lives in `webrender_api` so builder-side interning keys can
// reference it. Re-exported here to keep existing references working.
pub use api::key_types::PolygonKey;

// `RectKey` now lives in `webrender_api` so builder-side interning keys can
// reference it. Re-exported here to keep existing references working.
pub use api::key_types::RectKey;

// `SideOffsetsKey`, `SizeKey`, `PointKey` and `VectorKey` now live in
// `webrender_api` so builder-side interning keys can reference them. Re-exported
// here to keep existing references working.
pub use api::key_types::VectorKey;

// `PrimKeyCommonData` now lives in `webrender_api` so interned keys reference
// only api-resident types. Re-exported here to keep existing references working.
pub use api::key_types::PrimKeyCommonData;

impl From<&LayoutPrimitiveInfo> for PrimKeyCommonData {
    fn from(info: &LayoutPrimitiveInfo) -> Self {
        PrimKeyCommonData {
            flags: info.flags,
            aligned_aa_edges: info.aligned_aa_edges,
            transformed_aa_edges: info.transformed_aa_edges,
            prim_rect: info.rect.into(),
            local_clip_rect: info.clip_rect.into(),
        }
    }
}

// `PrimKey<T>` now lives in `webrender_api::interned_prims` so builder-side
// interning can construct the alias-based keys. Re-exported here to keep
// existing references working.
pub use api::interned_prims::PrimKey;

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(MallocSizeOf)]
#[derive(Debug, Clone)]
pub struct PrimTemplateCommonData {
    pub flags: PrimitiveFlags,
    pub aligned_aa_edges: EdgeMask,
    pub transformed_aa_edges: EdgeMask,
    /// Local-space rect of the primitive, as authored by the display list (not
    /// snapped to the device pixel grid). See `PrimKeyCommonData::prim_rect`.
    pub prim_rect: LayoutRect,
    /// The primitive's own local clip rect, unsnapped. See
    /// `PrimKeyCommonData::local_clip_rect`.
    pub local_clip_rect: LayoutRect,
}

impl PrimTemplateCommonData {
    pub fn with_key_common(common: PrimKeyCommonData) -> Self {
        PrimTemplateCommonData {
            flags: common.flags,
            aligned_aa_edges: common.aligned_aa_edges,
            transformed_aa_edges: common.transformed_aa_edges,
            prim_rect: common.prim_rect.into(),
            local_clip_rect: common.local_clip_rect.into(),
        }
    }
}

/// What scene building reads of an interned primitive: where its instance goes.
/// The display item carries only the spatial id, the clip chain and the handle,
/// so this is resolved from the key's common data for every primitive type.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, MallocSizeOf)]
pub struct PrimPlacement {
    pub rect: LayoutRect,
    pub clip_rect: LayoutRect,
    pub flags: PrimitiveFlags,
}

impl<T: malloc_size_of::MallocSizeOf> crate::dl_interner::DlResolve<PrimKey<T>> for PrimPlacement {
    fn resolve(key: &PrimKey<T>, _ctx: &crate::dl_interner::DlResolveContext) -> Self {
        PrimPlacement {
            rect: key.common.prim_rect.into(),
            clip_rect: key.common.local_clip_rect.into(),
            flags: key.common.flags,
        }
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(MallocSizeOf, Clone)]
pub struct PrimTemplate<T> {
    pub common: PrimTemplateCommonData,
    pub kind: T,
}

#[derive(Debug, MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct VisibleMaskImageTile {
    pub tile_offset: TileOffset,
    pub tile_rect: LayoutRect,
    pub task_id: RenderTaskId,
}

/// Represents the visibility state of a segment (wrt clip masks).
#[cfg_attr(feature = "capture", derive(Serialize))]
#[derive(Debug, Clone)]
pub enum ClipMaskKind {
    /// The segment has a clip mask, specified by the render task.
    Mask(RenderTaskId),
    /// The segment has no clip mask.
    None,
    /// The segment is made invisible / clipped completely.
    Clipped,
}

// `NinePatchDescriptor` now lives in `webrender_api` so builder-side interning
// keys can reference it. Re-exported here to keep existing references working.
pub use api::key_types::NinePatchDescriptor;

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub enum PrimitiveKind {
    /// Direct reference to a Picture
    Picture {
        /// Handle to the common interned data for this primitive.
        data_handle: PictureDataHandle,
        pic_index: PictureIndex,
    },
    /// A run of glyphs, with associated font parameters.
    TextRun {
        /// Handle to the common interned data for this primitive.
        data_handle: TextRunDataHandle,
    },
    /// A line decoration. cache_handle refers to a cached render
    /// task handle, if this line decoration is not a simple solid.
    LineDecoration {
        /// Handle into the follower store fed by the content display list
        /// builder.
        handle: DlHandle<LineDecorationKey>,
    },
    NormalBorder {
        /// Handle to the common interned data for this primitive.
        data_handle: NormalBorderDataHandle,
    },
    ImageBorder {
        /// Handle to the common interned data for this primitive.
        data_handle: ImageBorderDataHandle,
    },
    Rectangle {
        /// Handle into the follower store fed by the content display list
        /// builder.
        handle: DlHandle<RectangleKey>,
    },
    YuvImage {
        /// Handle into the follower store fed by the content display list
        /// builder.
        handle: DlHandle<YuvImagePrimKey>,
    },
    Image {
        /// Handle into the follower store fed by the content display list
        /// builder.
        handle: DlHandle<ImagePrimKey>,
    },
    LinearGradient {
        /// Handle to the common interned data for this primitive.
        data_handle: LinearGradientDataHandle,
    },
    RadialGradient {
        /// Handle to the common interned data for this primitive.
        data_handle: RadialGradientDataHandle,
    },
    ConicGradient {
        /// Handle to the common interned data for this primitive.
        data_handle: ConicGradientDataHandle,
    },
    /// Render a portion of a specified backdrop.
    BackdropCapture {
        data_handle: BackdropCaptureDataHandle,
    },
    BackdropRender {
        data_handle: BackdropRenderDataHandle,
        pic_index: PictureIndex,
    },
    BoxShadow {
        /// Handle into the follower store fed by the content display list
        /// builder.
        handle: DlHandle<BoxShadowKey>,
    },
}

impl PrimitiveKind {
    /// Whether this primitive snaps its geometry and clips to the device pixel
    /// grid.
    ///
    /// False only for device-space content: a text run is rasterized at an
    /// exact sub-pixel position, so rounding its clips would shave the edge
    /// glyph (bug 2050692). Everything else - including pictures, whose
    /// image-mask clips must stay aligned with the mask they rasterize to -
    /// snaps.
    ///
    /// Derived rather than stored: it is a property of the primitive type, so
    /// storing it per instance or per interned template would just repeat the
    /// same bit across every entry.
    pub fn snaps(&self) -> bool {
        !matches!(self, PrimitiveKind::TextRun { .. })
    }
}

impl PrimitiveKind {
    pub fn as_pic(&self) -> PictureIndex {
        match self {
            PrimitiveKind::Picture { pic_index, .. } => *pic_index,
            _ => panic!("bug: as_pic called on a prim that is not a picture"),
        }
    }
}

#[derive(Debug, Copy, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PrimitiveInstanceIndex(pub u32);

impl PrimitiveInstanceIndex {
    pub const INVALID: PrimitiveInstanceIndex = PrimitiveInstanceIndex(!0);
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct PrimitiveInstance {
    /// Identifies the kind of primitive this
    /// instance is, and references to where
    /// the relevant information for the primitive
    /// can be found.
    pub kind: PrimitiveKind,

    /// Where this primitive's clip chain starts in the clip tree. Walking from
    /// here up to the current clip root gives the clips that apply to it.
    pub clip_node_id: ClipNodeId,
}

/// How a primitive's clips round to the device pixel grid. Distinct from how
/// the prim's own rect rounds (see `SnapPolicy::rect`): a device-space prim
/// rounds its rect out but leaves its clips exact.
#[derive(Debug, Copy, Clone, PartialEq)]
pub enum ClipSnap {
    /// Snap every clip edge to the nearest device pixel, except for the clips
    /// marked as anti-aliased (see `ClipTreeNode::anti_aliased`). Used by
    /// prims that snap their geometry to the grid (`snaps`), and by
    /// anti-aliased prims.
    Nearest,
    /// Leave clip edges exact. Used by device-space prims (text runs and
    /// surfaces), whose clips must stay at the sub-pixel position matching their
    /// contents (bug 2050692).
    Exact,
}

/// The device-grid snapping policy for one primitive: how its own bounding rect
/// rounds, and how its clips round. These are separate axes - e.g. a line
/// decoration is `{ rect: Line, clip: Nearest }`, while a text run or a surface
/// is `{ rect: RoundOut, clip: Exact }`.
#[derive(Debug, Copy, Clone, PartialEq)]
pub struct SnapPolicy {
    pub rect: SnapRounding,
    pub clip: ClipSnap,
}

impl SnapPolicy {
    /// How the prim's own local clip rect rounds. It is part of the prim's
    /// geometry, so it is left exact when the prim's rect is.
    pub fn local_clip(&self) -> ClipSnap {
        match self.rect {
            SnapRounding::Exact => ClipSnap::Exact,
            _ => self.clip,
        }
    }
}

impl PrimitiveInstance {
    pub fn new(
        kind: PrimitiveKind,
        clip_node_id: ClipNodeId,
    ) -> Self {
        PrimitiveInstance {
            kind,
            clip_node_id,
        }
    }

    /// How this prim rounds to the device pixel grid: its own rect and its
    /// clips (see `SnapPolicy`).
    ///
    /// An anti-aliased prim does not snap its rect or its own local clip rect,
    /// but snaps the clips of its clip chain like other prims, except for the
    /// anti-aliased ones. This takes precedence over everything below.
    ///
    /// A device-space prim (see `PrimitiveKind::snaps`) stays at exact
    /// sub-pixel positions and only needs a conservative, grid-aligned
    /// footprint. A decoration line snaps its thickness specially so it can't
    /// vanish or double with scale (bug 1783779); everything else snaps to the
    /// nearest pixel.
    ///
    /// The two rounding axes differ for a device-space prim: its bounding rect
    /// rounds out (a conservative, grid-aligned footprint for surface / cluster
    /// allocation) while its clips stay exact, at the sub-pixel position
    /// matching its contents (bug 2050692).
    pub fn snap_policy(&self, data_stores: &DataStores, dl_stores: &DlStores) -> SnapPolicy {
        if data_stores.prim_has_anti_aliasing(self, dl_stores) {
            return SnapPolicy { rect: SnapRounding::Exact, clip: ClipSnap::Nearest };
        }
        if !self.kind.snaps() {
            return SnapPolicy { rect: SnapRounding::RoundOut, clip: ClipSnap::Exact };
        }
        let rect = match self.kind {
            PrimitiveKind::LineDecoration { handle } => SnapRounding::Line {
                horizontal: dl_stores.line_decorations[handle].kind.orientation
                    == LineOrientation::Horizontal,
            },
            PrimitiveKind::NormalBorder { data_handle, .. } => SnapRounding::BorderInner {
                widths: data_stores.normal_border[data_handle].kind.widths,
            },
            _ => SnapRounding::Nearest,
        };
        SnapPolicy { rect, clip: ClipSnap::Nearest }
    }

    pub fn uid(&self, dl_stores: &DlStores) -> intern::ItemUid {
        match &self.kind {
            PrimitiveKind::Rectangle { handle } => {
                dl_stores.rectangles.uid(*handle)
            }
            PrimitiveKind::Image { handle } => {
                dl_stores.images.uid(*handle)
            }
            PrimitiveKind::ImageBorder { data_handle, .. } => {
                data_handle.uid()
            }
            PrimitiveKind::LineDecoration { handle } => {
                dl_stores.line_decorations.uid(*handle)
            }
            PrimitiveKind::LinearGradient { data_handle, .. } => {
                data_handle.uid()
            }
            PrimitiveKind::NormalBorder { data_handle, .. } => {
                data_handle.uid()
            }
            PrimitiveKind::Picture { data_handle, .. } => {
                data_handle.uid()
            }
            PrimitiveKind::RadialGradient { data_handle, .. } => {
                data_handle.uid()
            }
            PrimitiveKind::ConicGradient { data_handle, .. } => {
                data_handle.uid()
            }
            PrimitiveKind::TextRun { data_handle, .. } => {
                data_handle.uid()
            }
            PrimitiveKind::YuvImage { handle } => {
                dl_stores.yuv_images.uid(*handle)
            }
            PrimitiveKind::BackdropCapture { data_handle, .. } => {
                data_handle.uid()
            }
            PrimitiveKind::BackdropRender { data_handle, .. } => {
                data_handle.uid()
            }
            PrimitiveKind::BoxShadow { handle } => {
                dl_stores.box_shadows.uid(*handle)
            }

        }
    }
}

pub type GlyphKeyStorage = storage::Storage<GlyphKey>;

/// Per-frame scratch storage. All fields are cleared every frame in
/// `begin_frame`. Anything written here lives only for the current frame.
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct PrimitiveFrameScratch {
    /// Per-frame draw headers. Holds visibility state, clip chain and
    /// clip-task index for each visible primitive.
    ///
    /// Densely populated: the visibility pass pushes one entry per primitive it
    /// finds is drawn, so the length tracks drawn primitives rather than scene
    /// size, and an entry existing at all means it was written this frame.
    ///
    /// Deliberately private: reach entries through `draw`/`draw_mut`, keyed by
    /// `PrimitiveDrawIndex`, or through a picture's draws with
    /// `picture_draw_range`. `PrimitiveDrawHeader`'s `prim_instance_index` goes
    /// from a draw back to its instance.
    draws: Vec<PrimitiveDrawHeader>,

    /// The draws of every visited picture's own primitives, in cluster order.
    /// Each picture's draws are contiguous; `picture_draw_ranges` locates them.
    picture_draws: Vec<PrimitiveDrawIndex>,

    /// For each picture, the range of `picture_draws` holding its draws. Empty
    /// for pictures the visibility pass did not visit.
    picture_draw_ranges: Vec<ops::Range<u32>>,

    /// Draws of the pictures the visibility pass is currently inside. Child
    /// pictures are visited in the middle of their parent's primitives, so each
    /// picture's draws are collected here and moved to `picture_draws` when its
    /// visit ends.
    pending_picture_draws: Vec<PrimitiveDrawIndex>,

    /// The backdrop-filter captures reachable this frame.
    pub backdrop_captures: BackdropCaptures,

    /// Per-frame scratch for Picture primitives. Holds the picture's
    /// primary/secondary render task ids and any per-composite-mode
    /// extra GPU buffer addresses. Indexed by `scratch_handle` on
    /// `PrimitiveKind::Picture`.
    pub pictures: storage::Storage<PictureScratch>,

    /// Per-frame scratch for TextRun primitives. Holds the per-frame
    /// font snapshot, glyph-key range, snapping offset, and raster
    /// scale for each visible text run.
    pub text_runs: storage::Storage<TextRunScratch>,

    /// Per-frame storage for glyph keys allocated by visible text
    /// runs. Each `TextRunScratch` holds a `Range` into this storage.
    /// Used to be on `PrimitiveSceneCache` (memoized across frames);
    /// graduated to per-frame here so the scene buffer cannot grow
    /// unbounded between scene rebuilds.
    pub glyph_keys: GlyphKeyStorage,

    /// Contains a list of clip mask instance parameters
    /// per segment generated.
    pub clip_mask_instances: Vec<ClipMaskKind>,

    /// List of debug display items for rendering. Cleared in `begin_frame`
    /// and refilled in `end_frame` (where retained `messages` are flushed
    /// into it for on-screen display).
    pub debug_items: Vec<DebugItem>,

    /// The backdrop-filter chains whose `BackdropRender` is drawn this frame,
    /// determined during the visibility pass
    pub required_backdrop_chains: FastHashSet<PictureIndex>,

    /// Temporary buffers for building segments in to during prepare pass
    pub quad_direct_segments: Vec<QuadSegment>,
    pub quad_indirect_segments: Vec<QuadSegment>,
}

impl Default for PrimitiveFrameScratch {
    fn default() -> Self {
        PrimitiveFrameScratch {
            draws: Vec::new(),
            picture_draws: Vec::new(),
            picture_draw_ranges: Vec::new(),
            pending_picture_draws: Vec::new(),
            backdrop_captures: BackdropCaptures::default(),
            pictures: storage::Storage::new(0),
            text_runs: storage::Storage::new(0),
            glyph_keys: GlyphKeyStorage::new(0),
            clip_mask_instances: Vec::new(),
            debug_items: Vec::new(),
            required_backdrop_chains: FastHashSet::default(),
            quad_direct_segments: Vec::new(),
            quad_indirect_segments: Vec::new(),
        }
    }
}

impl PrimitiveFrameScratch {
    /// Prepare the draw storage for a new frame over a scene with
    /// `picture_count` pictures.
    pub fn reset_draws(&mut self, picture_count: usize) {
        self.draws.clear();
        self.picture_draws.clear();
        self.picture_draw_ranges.clear();
        self.picture_draw_ranges.resize(picture_count, 0 .. 0);
        debug_assert!(self.pending_picture_draws.is_empty());
    }

    /// Start collecting the draws of a picture's own primitives. Returns the
    /// token to hand back to `end_picture_draws` when the picture's visit ends.
    pub fn begin_picture_draws(&self) -> usize {
        self.pending_picture_draws.len()
    }

    /// Finish collecting a picture's draws, begun by the `begin_picture_draws`
    /// call that returned `start`.
    pub fn end_picture_draws(&mut self, pic_index: PictureIndex, start: usize) {
        let first = self.picture_draws.len() as u32;
        self.picture_draws.extend(self.pending_picture_draws.drain(start ..));
        let end = self.picture_draws.len() as u32;
        self.picture_draw_ranges[pic_index.0 as usize] = first .. end;
    }

    /// The range of `picture_draw` positions holding a picture's draws.
    pub fn picture_draw_range(&self, pic_index: PictureIndex) -> ops::Range<u32> {
        self.picture_draw_ranges[pic_index.0 as usize].clone()
    }

    /// The draw at a position returned by `picture_draw_range`.
    pub fn picture_draw(&self, position: u32) -> PrimitiveDrawIndex {
        self.picture_draws[position as usize]
    }

    /// Record a draw for the primitive instance named by the header, and return
    /// its index. An instance may be drawn more than once in a frame.
    pub fn push_draw(&mut self, header: PrimitiveDrawHeader) -> PrimitiveDrawIndex {
        let prim_instance_index = header.prim_instance_index;
        debug_assert!(prim_instance_index.0 != PrimitiveInstanceIndex::INVALID.0);

        let draw_index = PrimitiveDrawIndex::from_u32(self.draws.len() as u32);
        self.draws.push(header);
        self.pending_picture_draws.push(draw_index);

        draw_index
    }

    /// Check that the visibility pass resolved a state for every draw it
    /// pushed. A draw is pushed before its state is known in the common case
    /// (the tile-cache dependency update decides it), so a path that pushes and
    /// then fails to resolve would leave `DrawState::Unset` for prepare and
    /// batching to trip over.
    pub fn assert_draws_resolved(&self) {
        #[cfg(debug_assertions)]
        {
            for draw in &self.draws {
                assert!(
                    !matches!(draw.state, crate::visibility::DrawState::Unset),
                    "bug: draw for {:?} left Unset by the visibility pass",
                    draw.prim_instance_index,
                );
            }
        }
    }

    /// Every draw pushed this frame.
    pub fn draws(&self) -> &[PrimitiveDrawHeader] {
        &self.draws
    }

    /// The draw header for a draw index, as carried by the command stream and
    /// by consumers such as `PlaneSplitAnchor` and `ExternalSurfaceDescriptor`.
    pub fn draw(&self, draw_index: PrimitiveDrawIndex) -> &PrimitiveDrawHeader {
        &self.draws[draw_index.0 as usize]
    }

    pub fn draw_mut(&mut self, draw_index: PrimitiveDrawIndex) -> &mut PrimitiveDrawHeader {
        &mut self.draws[draw_index.0 as usize]
    }


    pub fn recycle(&mut self, recycler: &mut Recycler) {
        recycler.recycle_vec(&mut self.draws);
        recycler.recycle_vec(&mut self.picture_draws);
        recycler.recycle_vec(&mut self.picture_draw_ranges);
        self.backdrop_captures.recycle(recycler);
        self.pictures.recycle(recycler);
        self.text_runs.recycle(recycler);
        self.glyph_keys.recycle(recycler);
        recycler.recycle_vec(&mut self.clip_mask_instances);
        recycler.recycle_vec(&mut self.debug_items);
        recycler.recycle_vec(&mut self.quad_direct_segments);
        recycler.recycle_vec(&mut self.quad_indirect_segments);
    }

    pub fn begin_frame(&mut self) {
        self.pictures.clear();
        self.text_runs.clear();
        self.glyph_keys.clear();

        // Clear the clip mask tasks for the beginning of the frame. Append
        // a single kind representing no clip mask, at the ClipTaskIndex::INVALID
        // location.
        self.clip_mask_instances.clear();
        self.clip_mask_instances.push(ClipMaskKind::None);
        self.quad_direct_segments.clear();
        self.quad_indirect_segments.clear();

        self.required_backdrop_chains.clear();

        self.debug_items.clear();
    }
}

/// Per-scene cache. Now empty — the originally memoized fields have
/// migrated to per-frame storage. Kept as a placeholder for any future
/// scene-stable state and so the lifetime invariant on
/// PrimitiveScratchBuffer (frame / scene / retained) remains visible
/// at the type level; a follow-up may drop it entirely.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[derive(Default)]
pub struct PrimitiveSceneCache {}

impl PrimitiveSceneCache {
    pub fn recycle(&mut self, _recycler: &mut Recycler) {}
}

/// State that lives strictly longer than a single frame *and* is not tied
/// to scene lifetime. These fields manage their own trim/eviction policy
/// rather than being cleared by `begin_frame` or `recycle`.
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct PrimitiveRetained {
    /// Debug log of recent messages. Trimmed by time/count in
    /// `PrimitiveScratchBuffer::end_frame` and flushed into
    /// `PrimitiveFrameScratch::debug_items` for display.
    messages: Vec<DebugMessage>,

    /// A retained classifier for checking which segments of a tiled
    /// primitive need a mask / are clipped / can be rendered directly.
    pub quad_tile_classifier: QuadTileClassifier,
}

impl Default for PrimitiveRetained {
    fn default() -> Self {
        PrimitiveRetained {
            messages: Vec::new(),
            quad_tile_classifier: QuadTileClassifier::new(),
        }
    }
}

/// Contains various vecs of data that is used only during frame building,
/// where we want to recycle the memory each new display list, to avoid
/// constantly re-allocating and moving memory around. Written during
/// primitive preparation, and read during batching.
///
/// Storage is partitioned by lifetime: `frame` is per-frame (cleared in
/// `begin_frame`), `scene` is per-scene (recycled on scene rebuild), and
/// `retained` lives across both with its own trim policy.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[derive(Default)]
pub struct PrimitiveScratchBuffer {
    pub frame: PrimitiveFrameScratch,
    pub scene: PrimitiveSceneCache,
    pub retained: PrimitiveRetained,
}

impl PrimitiveScratchBuffer {
    pub fn recycle(&mut self, recycler: &mut Recycler) {
        self.frame.recycle(recycler);
        self.scene.recycle(recycler);
    }

    pub fn begin_frame(&mut self) {
        self.frame.begin_frame();
    }

    pub fn end_frame(&mut self) {
        const MSGS_TO_RETAIN: usize = 32;
        const TIME_TO_RETAIN: u64 = 2000000000;
        const LINE_HEIGHT: f32 = 20.0;
        const X0: f32 = 32.0;
        const Y0: f32 = 32.0;
        let now = zeitstempel::now();

        let messages = &mut self.retained.messages;
        let msgs_to_remove = messages.len().max(MSGS_TO_RETAIN) - MSGS_TO_RETAIN;
        let mut msgs_removed = 0;

        messages.retain(|msg| {
            if msgs_removed < msgs_to_remove {
                msgs_removed += 1;
                return false;
            }

            if msg.timestamp + TIME_TO_RETAIN < now {
                return false;
            }

            true
        });

        let mut y = Y0 + messages.len() as f32 * LINE_HEIGHT;
        let shadow_offset = 1.0;
        let debug_items = &mut self.frame.debug_items;

        for msg in messages.iter() {
            debug_items.push(DebugItem::Text {
                position: DevicePoint::new(X0 + shadow_offset, y + shadow_offset),
                color: debug_colors::BLACK,
                msg: msg.msg.clone(),
            });

            debug_items.push(DebugItem::Text {
                position: DevicePoint::new(X0, y),
                color: debug_colors::RED,
                msg: msg.msg.clone(),
            });

            y -= LINE_HEIGHT;
        }
    }

    pub fn push_debug_rect_with_stroke_width(
        &mut self,
        rect: WorldRect,
        border: ColorF,
        stroke_width: f32
    ) {
        let top_edge = WorldRect::new(
            WorldPoint::new(rect.min.x + stroke_width, rect.min.y),
            WorldPoint::new(rect.max.x - stroke_width, rect.min.y + stroke_width)
        );
        self.push_debug_rect(top_edge * DevicePixelScale::new(1.0), 1, border, border);

        let bottom_edge = WorldRect::new(
            WorldPoint::new(rect.min.x + stroke_width, rect.max.y - stroke_width),
            WorldPoint::new(rect.max.x - stroke_width, rect.max.y)
        );
        self.push_debug_rect(bottom_edge * DevicePixelScale::new(1.0), 1, border, border);

        let right_edge = WorldRect::new(
            WorldPoint::new(rect.max.x - stroke_width, rect.min.y),
            rect.max
        );
        self.push_debug_rect(right_edge * DevicePixelScale::new(1.0), 1, border, border);

        let left_edge = WorldRect::new(
            rect.min,
            WorldPoint::new(rect.min.x + stroke_width, rect.max.y)
        );
        self.push_debug_rect(left_edge * DevicePixelScale::new(1.0), 1, border, border);
    }

    #[allow(dead_code)]
    pub fn push_debug_rect(
        &mut self,
        rect: DeviceRect,
        thickness: i32,
        outer_color: ColorF,
        inner_color: ColorF,
    ) {
        self.frame.debug_items.push(DebugItem::Rect {
            rect,
            outer_color,
            inner_color,
            thickness,
        });
    }

    #[allow(dead_code)]
    pub fn push_debug_string(
        &mut self,
        position: DevicePoint,
        color: ColorF,
        msg: String,
    ) {
        self.frame.debug_items.push(DebugItem::Text {
            position,
            color,
            msg,
        });
    }

    #[allow(dead_code)]
    pub fn log(
        &mut self,
        msg: String,
    ) {
        self.retained.messages.push(DebugMessage {
            msg,
            timestamp: zeitstempel::now(),
        })
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Clone, Debug)]
pub struct PrimitiveStoreStats {
    picture_count: usize,
}

impl PrimitiveStoreStats {
    pub fn empty() -> Self {
        PrimitiveStoreStats {
            picture_count: 0,
        }
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct PrimitiveStore {
    pub pictures: Vec<PictureInstance>,
}

impl PrimitiveStore {
    pub fn new(stats: &PrimitiveStoreStats) -> PrimitiveStore {
        PrimitiveStore {
            pictures: Vec::with_capacity(stats.picture_count),
        }
    }

    pub fn reset(&mut self) {
        self.pictures.clear();
    }

    pub fn get_stats(&self) -> PrimitiveStoreStats {
        PrimitiveStoreStats {
            picture_count: self.pictures.len(),
        }
    }

    #[allow(unused)]
    pub fn print_picture_tree(&self, root: PictureIndex) {
        use crate::print_tree::PrintTree;
        let mut pt = PrintTree::new("picture tree");
        self.pictures[root.0 as usize].print(&self.pictures, root, &mut pt);
    }
}

impl Default for PrimitiveStore {
    fn default() -> Self {
        PrimitiveStore::new(&PrimitiveStoreStats::empty())
    }
}

/// Trait for primitives that are directly internable.
/// see SceneBuilder::add_primitive<P>
pub trait InternablePrimitive: intern::Internable<InternData = ()> + Sized {
    /// Build a new key from self with `info`.
    fn into_key(
        self,
        info: &LayoutPrimitiveInfo,
    ) -> Self::Key;

    fn make_instance_kind(
        key: Self::Key,
        data_handle: intern::Handle<Self>,
        prim_store: &mut PrimitiveStore,
    ) -> PrimitiveKind;
}


#[test]
fn device_text_runs_do_not_snap_their_clips() {
    // Regression test for bug 2050692 (Slack channel-name last character cut
    // off). A device-space text run must resolve its clips UNSNAPPED, or a
    // fractional clip edge rounds inward onto the device grid and shaves the
    // last glyph.
    //
    // The rendered difference is a sub-pixel clip shift that headless software
    // rasterization collapses (it only bites once the compositor anti-aliases
    // the clip edge, e.g. Windows at a fractional device scale), so it cannot
    // be guarded by a reftest - hence this unit test on the policy itself.
    use crate::intern::Handle;

    assert!(
        !PrimitiveKind::TextRun { data_handle: Handle::INVALID }.snaps(),
        "device-space text must not snap its clips (bug 2050692)",
    );

    // Everything else snaps, including pictures - an image-mask clip has to
    // stay aligned with the mask it rasterizes to.
    assert!(
        PrimitiveKind::Rectangle {
            handle: DlHandle::new(crate::dl_interner::DlNamespace(0), 0, 0),
        }.snaps(),
        "a snapping primitive must snap its clips",
    );
    assert!(
        PrimitiveKind::Picture {
            data_handle: Handle::INVALID,
            pic_index: PictureIndex::INVALID,
        }.snaps(),
        "a picture must snap its clips so image-mask clips stay aligned",
    );
}

#[test]
#[cfg(target_pointer_width = "64")]
fn test_struct_sizes() {
    use std::mem;
    // The sizes of these structures are critical for performance on a number of
    // talos stress tests. If you get a failure here on CI, there's two possibilities:
    // (a) You made a structure smaller than it currently is. Great work! Update the
    //     test expectations and move on.
    // (b) You made a structure larger. This is not necessarily a problem, but should only
    //     be done with care, and after checking if talos performance regresses badly.
    assert_eq!(mem::size_of::<PrimitiveInstance>(), 20, "PrimitiveInstance size changed");
    assert_eq!(mem::size_of::<PrimitiveKind>(), 16, "PrimitiveKind size changed");
}

