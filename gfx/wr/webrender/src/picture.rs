/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! A picture represents a dynamically rendered image.
//!
//! # Overview
//!
//! Pictures consists of:
//!
//! - A number of primitives that are drawn onto the picture.
//! - A composite operation describing how to composite this
//!   picture into its parent.
//! - A configuration describing how to draw the primitives on
//!   this picture (e.g. in screen space or local space).
//!
//! The tree of pictures are generated during scene building.
//!
//! Depending on their composite operations pictures can be rendered into
//! intermediate targets or folded into their parent picture.
//!
//! ## Picture caching
//!
//! Pictures can be cached to reduce the amount of rasterization happening per
//! frame.
//!
//! When picture caching is enabled, the scene is cut into a small number of slices,
//! typically:
//!
//! - content slice
//! - UI slice
//! - background UI slice which is hidden by the other two slices most of the time.
//!
//! Each of these slice is made up of fixed-size large tiles of 2048x512 pixels
//! (or 128x128 for the UI slice).
//!
//! Tiles can be either cached rasterized content into a texture or "clear tiles"
//! that contain only a solid color rectangle rendered directly during the composite
//! pass.
//!
//! ## Invalidation
//!
//! Each tile keeps track of the elements that affect it, which can be:
//!
//! - primitives
//! - clips
//! - image keys
//! - opacity bindings
//! - transforms
//!
//! These dependency lists are built each frame and compared to the previous frame to
//! see if the tile changed.
//!
//! The tile's primitive dependency information is organized in a quadtree, each node
//! storing an index buffer of tile primitive dependencies.
//!
//! The union of the invalidated leaves of each quadtree produces a per-tile dirty rect
//! which defines the scissor rect used when replaying the tile's drawing commands and
//! can be used for partial present.
//!
//! ## Display List shape
//!
//! WR will first look for an iframe item in the root stacking context to apply
//! picture caching to. If that's not found, it will apply to the entire root
//! stacking context of the display list. Apart from that, the format of the
//! display list is not important to picture caching. Each time a new scroll root
//! is encountered, a new picture cache slice will be created. If the display
//! list contains more than some arbitrary number of slices (currently 8), the
//! content will all be squashed into a single slice, in order to save GPU memory
//! and compositing performance.
//!
//! ## Compositor Surfaces
//!
//! Sometimes, a primitive would prefer to exist as a native compositor surface.
//! This allows a large and/or regularly changing primitive (such as a video, or
//! webgl canvas) to be updated each frame without invalidating the content of
//! tiles, and can provide a significant performance win and battery saving.
//!
//! Since drawing a primitive as a compositor surface alters the ordering of
//! primitives in a tile, we use 'overlay tiles' to ensure correctness. If a
//! tile has a compositor surface, _and_ that tile has primitives that overlap
//! the compositor surface rect, the tile switches to be drawn in alpha mode.
//!
//! We rely on only promoting compositor surfaces that are opaque primitives.
//! With this assumption, the tile(s) that intersect the compositor surface get
//! a 'cutout' in the rectangle where the compositor surface exists (not the
//! entire tile), allowing that tile to be drawn as an alpha tile after the
//! compositor surface.
//!
//! Tiles are only drawn in overlay mode if there is content that exists on top
//! of the compositor surface. Otherwise, we can draw the tiles in the normal fast
//! path before the compositor surface is drawn. Use of the per-tile valid and
//! dirty rects ensure that we do a minimal amount of per-pixel work here to
//! blend the overlay tile (this is not always optimal right now, but will be
//! improved as a follow up).

use api::RasterSpace;
use api::{DebugFlags, ColorF, PrimitiveFlags, SnapshotInfo};
use api::units::*;
use crate::command_buffer::{CommandBufferIndex, PrimitiveCommand};
use crate::renderer::GpuBufferBuilderF;
use crate::box_shadow::BLUR_SAMPLE_SCALE;
use crate::pattern::PatternBuilder;
use crate::pattern::image::{ImagePattern, ShadowPattern};
use crate::pattern::mix_blend::{MixBlendPattern, FixedFunctionMixBlendPattern};
use crate::pattern::filter::BlendFilterPattern;
use crate::segment::EdgeMask;
use api::ImageBufferKind;
use crate::clip::{ClipChainInstance, ClipNodeId, ClipNodeFlags, ClipNodeRange, ClipTreeBuilder};
use crate::spatial_tree::{SpatialTree, CoordinateSpaceMapping, SpatialNodeIndex, VisibleFace};
use crate::composite::{tile_kind, CompositeTileSurface, CompositorKind, NativeTileId};
use crate::composite::{CompositeTileDescriptor, CompositeTile};
use crate::debug_colors;
use euclid::{vec3, Scale, Vector2D, Box2D};
use crate::internal_types::{FastHashMap, PlaneSplitter, Filter};
use crate::internal_types::{PlaneSplitterIndex, PlaneSplitAnchor, TextureSource};
use crate::frame_builder::{FrameBuildingContext, FrameBuildingState, PictureState, PictureContext};
use plane_split::{Clipper, Polygon};
use crate::prim_store::{PictureIndex, PrimitiveInstance, PrimitiveKind};
use crate::prim_store::storage::Index as StorageIndex;
use crate::visibility::{PrimitiveDrawHeader, PrimitiveDrawIndex};
use crate::prim_store::{PrimitiveScratchBuffer, ClipTaskIndex, ClipMaskKind};
use crate::prim_store::storage;
use crate::print_tree::PrintTreePrinter;
use crate::render_backend::DataStores;
use crate::render_task_graph::RenderTaskId;
use crate::render_task::{RenderTask, RenderTaskLocation};
use crate::render_task::{StaticRenderTaskSurface, RenderTaskKind, EmptyTask};
use crate::renderer::{BlendMode, GpuBufferAddress};
use crate::resource_cache::ResourceCache;
use crate::space::{SpaceMapper, SpaceSnapper};
use crate::quad::{self, QuadDescriptor, QuadTransformState};
use crate::scene::SceneProperties;
use crate::spatial_tree::CoordinateSystemId;
use crate::surface::{SurfaceDescriptor, SurfaceTileDescriptor, get_surface_rects};
use crate::surface::{SurfaceIndex, SurfaceInfo, SubpixelMode};
use smallvec::SmallVec;
use std::{mem, u8, u32};
use std::ops::Range;
use crate::picture_textures::PictureCacheTextureHandle;
use crate::util::{MaxRect, Recycler, ScaleOffset};
use crate::tile_cache::{SliceDebugInfo, TileDebugInfo, DirtyTileDebugInfo, CompositorClipDebugInfo};
use crate::tile_cache::{SliceId, TileCacheInstance, TileSurface, NativeSurface};
use crate::tile_cache::{BackdropKind, BackdropSurface};
use crate::tile_cache::{TileKey, SubSliceIndex};
use crate::invalidation::InvalidationReason;
use crate::tile_cache::MAX_SURFACE_SIZE;

use crate::picture_composite_mode::{PictureCompositeMode, prepare_composite_mode};

// Maximum blur radius for blur filter (different than box-shadow blur).
// Taken from FilterNodeSoftware.cpp in Gecko.
pub(crate) const MAX_BLUR_RADIUS: f32 = 100.;

/// Maximum size of a compositor surface.
pub const MAX_COMPOSITOR_SURFACES_SIZE: f32 = 8192.0;

pub fn clamp(value: i32, low: i32, high: i32) -> i32 {
    value.max(low).min(high)
}

pub fn clampf(value: f32, low: f32, high: f32) -> f32 {
    value.max(low).min(high)
}

/// A descriptor for the kind of texture that a picture cache tile will
/// be drawn into.
#[derive(Debug)]
pub enum SurfaceTextureDescriptor {
    /// When using the WR compositor, the tile is drawn into an entry
    /// in the WR texture cache.
    TextureCache {
        handle: Option<PictureCacheTextureHandle>,
    },
    /// When using an OS compositor, the tile is drawn into a native
    /// surface identified by arbitrary id.
    Native {
        /// The arbitrary id of this tile.
        id: Option<NativeTileId>,
    },
}

/// This is the same as a `SurfaceTextureDescriptor` but has been resolved
/// into a texture cache handle (if appropriate) that can be used by the
/// batching and compositing code in the renderer.
#[derive(Clone, Debug, Eq, PartialEq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum ResolvedSurfaceTexture {
    TextureCache {
        /// The texture ID to draw to.
        texture: TextureSource,
    },
    Native {
        /// The arbitrary id of this tile.
        id: NativeTileId,
        /// The size of the tile in device pixels.
        size: DeviceIntSize,
    }
}

impl SurfaceTextureDescriptor {
    /// Create a resolved surface texture for this descriptor
    pub fn resolve(
        &self,
        resource_cache: &ResourceCache,
        size: DeviceIntSize,
    ) -> ResolvedSurfaceTexture {
        match self {
            SurfaceTextureDescriptor::TextureCache { handle } => {
                let texture = resource_cache
                    .picture_textures
                    .get_texture_source(handle.as_ref().unwrap());

                ResolvedSurfaceTexture::TextureCache { texture }
            }
            SurfaceTextureDescriptor::Native { id } => {
                ResolvedSurfaceTexture::Native {
                    id: id.expect("bug: native surface not allocated"),
                    size,
                }
            }
        }
    }
}

pub struct PictureScratchBuffer {
    surface_stack: Vec<SurfaceIndex>,
}

impl Default for PictureScratchBuffer {
    fn default() -> Self {
        PictureScratchBuffer {
            surface_stack: Vec::new(),
        }
    }
}

impl PictureScratchBuffer {
    pub fn begin_frame(&mut self) {
        self.surface_stack.clear();
    }

    pub fn recycle(&mut self, recycler: &mut Recycler) {
        recycler.recycle_vec(&mut self.surface_stack);
    }
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct RasterConfig {
    /// How this picture should be composited into
    /// the parent surface.
    // TODO(gw): We should remove this and just use what is in PictureInstance
    pub composite_mode: PictureCompositeMode,
    /// Index to the surface descriptor for this
    /// picture.
    pub surface_index: SurfaceIndex,
}

bitflags! {
    /// A set of flags describing why a picture may need a backing surface.
    #[cfg_attr(feature = "capture", derive(Serialize))]
    #[derive(Debug, Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash)]
    pub struct BlitReason: u32 {
        /// Mix-blend-mode on a child that requires isolation.
        const BLEND_MODE = 1 << 0;
        /// Clip node that _might_ require a surface.
        const CLIP = 1 << 1;
        /// Preserve-3D requires a surface for plane-splitting.
        const PRESERVE3D = 1 << 2;
        /// A forced isolation request from gecko.
        const FORCED_ISOLATION = 1 << 3;
        /// We may need to render the picture into an image and cache it.
        const SNAPSHOT = 1 << 4;
    }
}

/// Enum value describing the place of a picture in a 3D context.
#[derive(Clone, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub enum Picture3DContext<C> {
    /// The picture is not a part of 3D context sub-hierarchy.
    Out,
    /// The picture is a part of 3D context.
    In {
        /// Additional data per child for the case of this a root of 3D hierarchy.
        root_data: Option<Vec<C>>,
        /// The spatial node index of an "ancestor" element, i.e. one
        /// that establishes the transformed element's containing block.
        ///
        /// See CSS spec draft for more details:
        /// https://drafts.csswg.org/css-transforms-2/#accumulated-3d-transformation-matrix-computation
        ancestor_index: SpatialNodeIndex,
        /// Index in the built scene's array of plane splitters.
        plane_splitter_index: PlaneSplitterIndex,
    },
}

/// Information about a preserve-3D hierarchy child that has been plane-split
/// and ordered according to the view direction.
#[derive(Clone, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct OrderedPictureChild {
    pub anchor: PlaneSplitAnchor,
    pub gpu_address: GpuBufferAddress,
}

bitflags! {
    /// A set of flags describing why a picture may need a backing surface.
    #[cfg_attr(feature = "capture", derive(Serialize))]
    #[derive(Debug, Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash)]
    pub struct ClusterFlags: u32 {
        /// Whether this cluster is visible when the position node is a backface.
        const IS_BACKFACE_VISIBLE = 1;
        /// This flag is set during the first pass picture traversal, depending on whether
        /// the cluster is visible or not. It's read during the second pass when primitives
        /// consult their owning clusters to see if the primitive itself is visible.
        const IS_VISIBLE = 2;
    }
}

/// Descriptor for a cluster of primitives. For now, this is quite basic but will be
/// extended to handle more spatial clustering of primitives.
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct PrimitiveCluster {
    /// The positioning node for this cluster.
    pub spatial_node_index: SpatialNodeIndex,
    /// The bounding rect of the cluster, in the local space of the spatial node,
    /// using display-list-authored prim culling rects (not snapped to the device
    /// pixel grid). This is used to quickly determine the overall bounding rect
    /// for a picture during the first picture traversal, which is needed for
    /// local scale determination, and render task size calculations.
    pub unsnapped_bounding_rect: LayoutRect,
    /// The range of primitive instance indices associated with this cluster.
    pub prim_range: Range<usize>,
    /// Various flags / state for this cluster.
    pub flags: ClusterFlags,
}

impl PrimitiveCluster {
    /// Construct a new primitive cluster for a given positioning node.
    fn new(
        spatial_node_index: SpatialNodeIndex,
        flags: ClusterFlags,
        first_instance_index: usize,
    ) -> Self {
        PrimitiveCluster {
            unsnapped_bounding_rect: LayoutRect::zero(),
            spatial_node_index,
            flags,
            prim_range: first_instance_index..first_instance_index
        }
    }

    /// Return true if this cluster is compatible with the given params
    pub fn is_compatible(
        &self,
        spatial_node_index: SpatialNodeIndex,
        flags: ClusterFlags,
        instance_index: usize,
    ) -> bool {
        self.flags == flags &&
        self.spatial_node_index == spatial_node_index &&
        instance_index == self.prim_range.end
    }

    pub fn prim_range(&self) -> Range<usize> {
        self.prim_range.clone()
    }

    /// Add a primitive instance to this cluster, at the start or end
    fn add_instance(
        &mut self,
        culling_rect: &LayoutRect,
        instance_index: usize,
    ) {
        debug_assert_eq!(instance_index, self.prim_range.end);
        self.unsnapped_bounding_rect = self.unsnapped_bounding_rect.union(culling_rect);
        self.prim_range.end += 1;
    }
}

/// A list of primitive instances that are added to a picture
/// This ensures we can keep a list of primitives that
/// are pictures, for a fast initial traversal of the picture
/// tree without walking the instance list.
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct PrimitiveList {
    /// List of primitives grouped into clusters.
    pub clusters: Vec<PrimitiveCluster>,
    pub child_pictures: Vec<PictureIndex>,
    /// The number of Image compositor surfaces that were found when
    /// adding prims to this list, which might be rendered as overlays.
    pub image_surface_count: usize,
    /// The number of YuvImage compositor surfaces that were found when
    /// adding prims to this list, which might be rendered as overlays.
    pub yuv_image_surface_count: usize,
    pub needs_scissor_rect: bool,
}

impl PrimitiveList {
    /// Construct an empty primitive list. This is
    /// just used during the take_context / restore_context
    /// borrow check dance, which will be removed as the
    /// picture traversal pass is completed.
    pub fn empty() -> Self {
        PrimitiveList {
            clusters: Vec::new(),
            child_pictures: Vec::new(),
            image_surface_count: 0,
            yuv_image_surface_count: 0,
            needs_scissor_rect: false,
        }
    }

    pub fn merge(&mut self, other: PrimitiveList) {
        self.clusters.extend(other.clusters);
        self.child_pictures.extend(other.child_pictures);
        self.image_surface_count += other.image_surface_count;
        self.yuv_image_surface_count += other.yuv_image_surface_count;
        self.needs_scissor_rect |= other.needs_scissor_rect;
    }

    /// Add a primitive instance to the end of the list
    pub fn add_prim(
        &mut self,
        prim_instance: PrimitiveInstance,
        prim_rect: LayoutRect,
        spatial_node_index: SpatialNodeIndex,
        prim_flags: PrimitiveFlags,
        prim_instances: &mut Vec<PrimitiveInstance>,
        clip_tree_builder: &ClipTreeBuilder,
    ) {
        let mut flags = ClusterFlags::empty();

        // Pictures are always put into a new cluster, to make it faster to
        // iterate all pictures in a given primitive list.
        match prim_instance.kind {
            PrimitiveKind::Picture { pic_index, .. } => {
                self.child_pictures.push(pic_index);
            }
            PrimitiveKind::TextRun { .. } => {
                self.needs_scissor_rect = true;
            }
            PrimitiveKind::YuvImage { .. } => {
                // Any YUV image that requests a compositor surface is implicitly
                // opaque. Though we might treat this prim as an underlay, which
                // doesn't require an overlay surface, we add to the count anyway
                // in case we opt to present it as an overlay. This means we may
                // be allocating more subslices than we actually need, but it
                // gives us maximum flexibility.
                if prim_flags.contains(PrimitiveFlags::PREFER_COMPOSITOR_SURFACE) {
                    self.yuv_image_surface_count += 1;
                }
            }
            PrimitiveKind::Image { .. } => {
                // For now, we assume that any image that wants a compositor surface
                // is transparent, and uses the existing overlay compositor surface
                // infrastructure. In future, we could detect opaque images, however
                // it's a little bit of work, as scene building doesn't have access
                // to the opacity state of an image key at this point.
                if prim_flags.contains(PrimitiveFlags::PREFER_COMPOSITOR_SURFACE) {
                    self.image_surface_count += 1;
                }
            }
            _ => {}
        }

        if prim_flags.contains(PrimitiveFlags::IS_BACKFACE_VISIBLE) {
            flags.insert(ClusterFlags::IS_BACKFACE_VISIBLE);
        }

        let clip_leaf = clip_tree_builder.get_leaf(prim_instance.clip_leaf_id);
        // Scene-build feeds the cluster's `unsnapped_bounding_rect` from this
        // culling rect (clip-leaf rect ∩ prim_rect). Both inputs are pre-snap;
        // the cluster bounding rect is re-snapped each frame in
        // `PictureInstance::propagate_bounding_rect`.
        let culling_rect = clip_leaf.unsnapped_local_clip_rect
            .intersection(&prim_rect)
            .unwrap_or_else(LayoutRect::zero);

        let instance_index = prim_instances.len();
        prim_instances.push(prim_instance);

        if let Some(cluster) = self.clusters.last_mut() {
            if cluster.is_compatible(spatial_node_index, flags, instance_index) {
                cluster.add_instance(&culling_rect, instance_index);
                return;
            }
        }

        // Same idea with clusters, using a different distribution.
        let clusters_len = self.clusters.len();
        if clusters_len == self.clusters.capacity() {
            let next_alloc = match clusters_len {
                1 ..= 15 => 16 - clusters_len,
                16 ..= 127 => 128 - clusters_len,
                _ => clusters_len * 2,
            };

            self.clusters.reserve(next_alloc);
        }

        let mut cluster = PrimitiveCluster::new(
            spatial_node_index,
            flags,
            instance_index,
        );

        cluster.add_instance(&culling_rect, instance_index);
        self.clusters.push(cluster);
    }

    /// Returns true if there are no clusters (and thus primitives)
    pub fn is_empty(&self) -> bool {
        self.clusters.is_empty()
    }
}

bitflags! {
    #[cfg_attr(feature = "capture", derive(Serialize))]
    /// Flags describing properties for a given PictureInstance
    #[derive(Debug, Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash)]
    pub struct PictureFlags : u8 {
        /// This picture is a resolve target (doesn't actually render content itself,
        /// will have content copied in to it)
        const IS_RESOLVE_TARGET = 1 << 0;
        /// This picture establishes a sub-graph, which affects how SurfaceBuilder will
        /// set up dependencies in the render task graph
        const IS_SUB_GRAPH = 1 << 1;
    }
}

/// Per-frame scratch data for a Picture primitive. Pushed in `take_context`
/// and read by both prepare and batch through the `scratch_handle` carried
/// on `PrimitiveKind::Picture`.
#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct PictureScratch {
    /// All render tasks have 0-2 input tasks.
    pub primary_render_task_id: Option<RenderTaskId>,
    /// If a mix-blend-mode, contains the render task for
    /// the readback of the framebuffer that we use to sample
    /// from in the mix-blend-mode shader.
    /// For drop-shadow filter, this will store the original
    /// picture task which would be rendered on screen after
    /// blur pass.
    /// This is also used by SVGFEBlend, SVGFEComposite and
    /// SVGFEDisplacementMap filters.
    pub secondary_render_task_id: Option<RenderTaskId>,
    /// Optional cache handles for storing extra data in the
    /// GPU cache, depending on the type of picture.
    pub extra_gpu_data: SmallVec<[GpuBufferAddress; 1]>,
}

impl PictureScratch {
    pub fn empty() -> Self {
        PictureScratch {
            primary_render_task_id: None,
            secondary_render_task_id: None,
            extra_gpu_data: SmallVec::new(),
        }
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct PictureInstance {
    /// List of primitives, and associated info for this picture.
    pub prim_list: PrimitiveList,

    /// If false and transform ends up showing the back of the picture,
    /// it will be considered invisible.
    pub is_backface_visible: bool,

    /// How this picture should be composited.
    /// If None, don't composite - just draw directly on parent surface.
    pub composite_mode: Option<PictureCompositeMode>,

    pub raster_config: Option<RasterConfig>,
    pub context_3d: Picture3DContext<OrderedPictureChild>,

    /// The spatial node index of this picture when it is
    /// composited into the parent picture.
    pub spatial_node_index: SpatialNodeIndex,

    /// Store the state of the previous local rect
    /// for this picture. We need this in order to know when
    /// to invalidate segments / drop-shadow gpu cache handles.
    pub prev_local_rect: LayoutRect,

    /// If false, this picture needs to (re)build segments
    /// if it supports segment rendering. This can occur
    /// if the local rect of the picture changes due to
    /// transform animation and/or scrolling.
    pub segments_are_valid: bool,

    /// Requested raster space for this picture
    pub raster_space: RasterSpace,

    /// Flags for this picture primitive
    pub flags: PictureFlags,

    /// The lowest common ancestor clip of all of the primitives in this
    /// picture, to be ignored when clipping those primitives and applied
    /// later when compositing the picture.
    pub clip_root: Option<ClipNodeId>,

    /// If provided, cache the content of this picture into an image
    /// associated with the image key.
    pub snapshot: Option<SnapshotInfo>,
}

impl PictureInstance {
    pub fn print<T: PrintTreePrinter>(
        &self,
        pictures: &[Self],
        self_index: PictureIndex,
        pt: &mut T,
    ) {
        pt.new_level(format!("{:?}", self_index));
        pt.add_item(format!("cluster_count: {:?}", self.prim_list.clusters.len()));
        pt.add_item(format!("spatial_node_index: {:?}", self.spatial_node_index));
        pt.add_item(format!("raster_config: {:?}", self.raster_config));
        pt.add_item(format!("composite_mode: {:?}", self.composite_mode));
        pt.add_item(format!("flags: {:?}", self.flags));

        for child_pic_index in &self.prim_list.child_pictures {
            pictures[child_pic_index.0].print(pictures, *child_pic_index, pt);
        }

        pt.end_level();
    }

    pub fn resolve_scene_properties(&mut self, properties: &SceneProperties) {
        match self.composite_mode {
            Some(PictureCompositeMode::Filter(ref mut filter)) => {
                match *filter {
                    Filter::Opacity(ref binding, ref mut value) => {
                        *value = properties.resolve_float(binding);
                    }
                    _ => {}
                }
            }
            _ => {}
        }
    }

    pub fn is_visible(
        &self,
        spatial_tree: &SpatialTree,
    ) -> bool {
        if let Some(PictureCompositeMode::Filter(ref filter)) = self.composite_mode {
            if !filter.is_visible() {
                return false;
            }
        }

        // For out-of-preserve-3d pictures, the backface visibility is determined by
        // the local transform only.
        // Note: we aren't taking the transform relative to the parent picture,
        // since picture tree can be more dense than the corresponding spatial tree.
        if !self.is_backface_visible {
            if let Picture3DContext::Out = self.context_3d {
                match spatial_tree.get_local_visible_face(self.spatial_node_index) {
                    VisibleFace::Front => {}
                    VisibleFace::Back => return false,
                }
            }
        }

        true
    }

    pub fn new_image(
        composite_mode: Option<PictureCompositeMode>,
        context_3d: Picture3DContext<OrderedPictureChild>,
        prim_flags: PrimitiveFlags,
        prim_list: PrimitiveList,
        spatial_node_index: SpatialNodeIndex,
        raster_space: RasterSpace,
        flags: PictureFlags,
        snapshot: Option<SnapshotInfo>,
    ) -> Self {
        PictureInstance {
            prim_list,
            composite_mode,
            raster_config: None,
            context_3d,
            is_backface_visible: prim_flags.contains(PrimitiveFlags::IS_BACKFACE_VISIBLE),
            spatial_node_index,
            prev_local_rect: LayoutRect::zero(),
            segments_are_valid: false,
            raster_space,
            flags,
            clip_root: None,
            snapshot,
        }
    }

    pub fn take_context(
        &mut self,
        pic_index: PictureIndex,
        parent_surface_index: Option<SurfaceIndex>,
        parent_subpixel_mode: SubpixelMode,
        frame_state: &mut FrameBuildingState,
        frame_context: &FrameBuildingContext,
        data_stores: &DataStores,
        scratch: &mut PrimitiveScratchBuffer,
        tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
    ) -> Option<(PictureContext, PictureState, PrimitiveList, storage::Index<PictureScratch>)> {
        let mut picture_scratch = PictureScratch::empty();

        let dbg_flags = DebugFlags::PICTURE_CACHING_DBG | DebugFlags::PICTURE_BORDERS;
        if frame_context.debug_flags.intersects(dbg_flags) {
            self.draw_debug_overlay(
                parent_surface_index,
                frame_state,
                frame_context,
                tile_caches,
                scratch,
            );
        }

        if !self.is_visible(frame_context.spatial_tree) {
            return None;
        }

        tracy_rs::profile_scope!("take_context");

        let surface_index = match self.raster_config {
            Some(ref raster_config) => raster_config.surface_index,
            None => parent_surface_index.expect("bug: no parent"),
        };
        let surface = &frame_state.surfaces[surface_index.0];
        let surface_spatial_node_index = surface.surface_spatial_node_index;

        let map_pic_to_device = SpaceMapper::new_with_target(
            frame_context.root_spatial_node_index,
            surface_spatial_node_index,
            frame_context.global_screen_device_rect,
            frame_context.spatial_tree,
        );

        let map_pic_to_vis = SpaceMapper::new_with_target(
            // TODO: switch from root to raster space.
            frame_context.root_spatial_node_index,
            surface_spatial_node_index,
            surface.culling_rect,
            frame_context.spatial_tree,
        );

        // TODO: When moving VisRect to raster space, compute the picture
        // bounds by projecting the parent surface's culling rect into the
        // current surface's raster space.
        let pic_bounds = map_pic_to_device
            .unmap(&map_pic_to_device.bounds)
            .unwrap_or_else(PictureRect::max_rect);

        let map_local_to_pic = SpaceMapper::new(
            surface_spatial_node_index,
            pic_bounds,
        );

        match self.raster_config {
            Some(RasterConfig { surface_index, composite_mode: PictureCompositeMode::TileCache { slice_id }, .. }) => {
                prepare_tiled_picture_surface(
                    surface_index,
                    slice_id,
                    surface_spatial_node_index,
                    &map_pic_to_device,
                    frame_context,
                    frame_state,
                    tile_caches,
                );
            }
            Some(ref mut raster_config) => {
                let (pic_rect, force_scissor_rect) = {
                    let surface = &frame_state.surfaces[raster_config.surface_index.0];
                    (surface.clipped_local_rect, surface.force_scissor_rect)
                };

                let parent_surface_index = parent_surface_index.expect("bug: no parent for child surface");

                // Layout space for the picture is picture space from the
                // perspective of its child primitives.
                let local_rect = pic_rect * Scale::new(1.0);

                // If the precise rect changed since last frame, we need to invalidate
                // any segments and gpu cache handles for drop-shadows.
                // TODO(gw): Requiring storage of the `prev_precise_local_rect` here
                //           is a total hack. It's required because `prev_precise_local_rect`
                //           gets written to twice (during initial vis pass and also during
                //           prepare pass). The proper longer term fix for this is to make
                //           use of the conservative picture rect for segmenting (which should
                //           be done during scene building).
                if local_rect != self.prev_local_rect {
                    // Invalidate any segments built for this picture, since the local
                    // rect has changed.
                    self.segments_are_valid = false;
                    self.prev_local_rect = local_rect;
                }

                let max_surface_size = frame_context
                    .fb_config
                    .max_surface_override
                    .unwrap_or(MAX_SURFACE_SIZE) as f32;

                let surface_rects = match get_surface_rects(
                    raster_config.surface_index,
                    &raster_config.composite_mode,
                    parent_surface_index,
                    &mut frame_state.surfaces,
                    frame_context.spatial_tree,
                    max_surface_size,
                    force_scissor_rect,
                ) {
                    Some(rects) => rects,
                    None => return None,
                };

                if let PictureCompositeMode::IntermediateSurface = raster_config.composite_mode {
                    if !scratch.frame.required_sub_graphs.contains(&pic_index) {
                        return None;
                    }
                }

                let can_use_shared_surface = !self.flags.contains(PictureFlags::IS_RESOLVE_TARGET);
                let (surface_descriptor, render_tasks) = prepare_composite_mode(
                    &raster_config.composite_mode,
                    surface_index,
                    parent_surface_index,
                    &surface_rects,
                    &self.snapshot,
                    can_use_shared_surface,
                    frame_context,
                    frame_state,
                    data_stores,
                    &mut picture_scratch.extra_gpu_data,
                );

                picture_scratch.primary_render_task_id = render_tasks[0];
                picture_scratch.secondary_render_task_id = render_tasks[1];

                let is_sub_graph = self.flags.contains(PictureFlags::IS_SUB_GRAPH);

                frame_state.surface_builder.push_surface(
                    raster_config.surface_index,
                    is_sub_graph,
                    surface_rects.clipped_local,
                    Some(surface_descriptor),
                    frame_state.surfaces,
                    frame_state.rg_builder,
                );
            }
            None => {}
        };

        let state = PictureState {
            map_local_to_pic,
            map_pic_to_vis,
        };

        let mut dirty_region_count = 0;

        // If this is a picture cache, push the dirty region to ensure any
        // child primitives are culled and clipped to the dirty rect(s).
        if let Some(RasterConfig { composite_mode: PictureCompositeMode::TileCache { slice_id }, .. }) = self.raster_config {
            let dirty_region = tile_caches[&slice_id].dirty_region.clone();
            frame_state.push_dirty_region(dirty_region);

            dirty_region_count += 1;
        }

        let subpixel_mode = compute_subpixel_mode(
            &self.raster_config,
            tile_caches,
            parent_subpixel_mode
        );

        let context = PictureContext {
            pic_index,
            raster_spatial_node_index: frame_state.surfaces[surface_index.0].raster_spatial_node_index,
            // TODO: switch the visibility spatial node from the root to raster space.
            visibility_spatial_node_index: frame_context.root_spatial_node_index,
            surface_spatial_node_index,
            surface_index,
            dirty_region_count,
            subpixel_mode,
        };

        let prim_list = mem::replace(&mut self.prim_list, PrimitiveList::empty());

        let scratch_handle = scratch.frame.pictures.push(picture_scratch);
        Some((context, state, prim_list, scratch_handle))
    }

    pub fn restore_context(
        &mut self,
        pic_index: PictureIndex,
        prim_list: PrimitiveList,
        context: PictureContext,
        frame_context: &FrameBuildingContext,
        frame_state: &mut FrameBuildingState,
        scratch: &PrimitiveScratchBuffer,
    ) {
        // Pop any dirty regions this picture set
        for _ in 0 .. context.dirty_region_count {
            frame_state.pop_dirty_region();
        }

        if self.raster_config.is_some() {
            frame_state.surface_builder.pop_surface(
                pic_index,
                frame_state.rg_builder,
                frame_state.cmd_buffers,
                frame_context.spatial_tree,
            );
        }

        if let Picture3DContext::In { root_data: Some(ref mut list), plane_splitter_index, ancestor_index, .. } = self.context_3d {
            let splitter = &mut frame_state.plane_splitters[plane_splitter_index.0];

            // Resolve split planes via BSP
            PictureInstance::resolve_split_planes(
                splitter,
                list,
                &mut frame_state.frame_gpu_data.f32,
                ancestor_index,
                &frame_context.spatial_tree,
            );

            // Add the child prims to the relevant command buffers
            let mut cmd_buffer_targets = Vec::new();
            for child in list {
                let draw = scratch.frame.draw(child.anchor.draw_index);
                if frame_state.surface_builder.get_cmd_buffer_targets_for_prim(
                    draw,
                    &mut cmd_buffer_targets,
                ) {
                    // The picture content this plane samples. Missing when the
                    // child has no content task (for example a detached snapshot),
                    // in which case there is nothing to composite.
                    let pic_scratch_handle = draw.kind_scratch.unwrap_picture();
                    let src_task_id = match scratch.frame.pictures[pic_scratch_handle].primary_render_task_id {
                        Some(task_id) => task_id,
                        None => continue,
                    };

                    // Maps the plane's local space to the destination raster space.
                    let transform_id = frame_state.transforms.gpu.get_id(
                        child.anchor.spatial_node_index,
                        context.raster_spatial_node_index,
                        frame_context.spatial_tree,
                    );

                    let prim_cmd = PrimitiveCommand::split_composite(
                        child.anchor.draw_index,
                        child.gpu_address,
                        transform_id,
                        src_task_id,
                        child.anchor.pattern_rect,
                    );

                    frame_state.push_prim(
                        &prim_cmd,
                        child.anchor.spatial_node_index,
                        &cmd_buffer_targets,
                    );
                }
            }
        }

        self.prim_list = prim_list;
    }

    /// Add a primitive instance to the plane splitter. The function would generate
    /// an appropriate polygon, clip it against the frustum, and register with the
    /// given plane splitter.
    pub fn add_split_plane(
        splitter: &mut PlaneSplitter,
        spatial_tree: &SpatialTree,
        prim_spatial_node_index: SpatialNodeIndex,
        // Coordinate space of the 3D rendering context's containing block
        // (`ancestor_index` from Picture3DContext::In). Polygons are projected
        // into this pre-perspective space so the BSP works on real 3D planes
        // rather than post-perspective ones.
        ancestor_spatial_node_index: SpatialNodeIndex,
        visibility_spatial_node_index: SpatialNodeIndex,
        original_local_rect: LayoutRect,
        combined_local_clip_rect: &LayoutRect,
        dirty_rect: VisRect,
        plane_split_anchor: PlaneSplitAnchor,
    ) -> bool {
        let plane_split_anchor = PlaneSplitAnchor {
            pattern_rect: original_local_rect,
            ..plane_split_anchor
        };

        let prim_to_ancestor = spatial_tree.get_relative_transform(
            prim_spatial_node_index,
            ancestor_spatial_node_index
        );

        let ancestor_matrix = prim_to_ancestor.clone().into_transform().cast().to_untyped();

        // Apply the local clip rect here, before splitting. This is
        // because the local clip rect can't be applied in the vertex
        // shader for split composites, since we are drawing polygons
        // rather that rectangles. The interpolation still works correctly
        // since we determine the UVs by doing a bilerp with a factor
        // from the original local rect.
        let local_rect = match original_local_rect
            .intersection(combined_local_clip_rect)
        {
            Some(rect) => rect.cast(),
            None => return false,
        };

        match prim_to_ancestor {
            CoordinateSpaceMapping::Local => {
                let polygon = Polygon::from_rect(
                    local_rect.to_rect() * Scale::new(1.0),
                    plane_split_anchor,
                );
                splitter.add(polygon);
            }
            CoordinateSpaceMapping::ScaleOffset(scale_offset) if scale_offset.scale == Vector2D::new(1.0, 1.0) => {
                let inv_matrix = scale_offset.inverse().to_transform().cast();
                let polygon = Polygon::from_transformed_rect_with_inverse(
                    local_rect.to_rect().to_untyped(),
                    &ancestor_matrix,
                    &inv_matrix,
                    plane_split_anchor,
                ).unwrap();
                splitter.add(polygon);
            }
            CoordinateSpaceMapping::ScaleOffset(_) |
            CoordinateSpaceMapping::Transform(_) => {
                // Project the visible region back into the 3D context's containing-block
                // (ancestor) space.
                // This may fail if the dirty rect doesn't have a valid pre-image (e.g. it
                // sits behind the projection plane in ancestor space), in which case we
                // fall back to no lateral bounds.
                // TODO: Instead of trying to map the vis (world) space dirty rect into the
                // right space here, we should be able to find the dirty rect in this space
                // that was built during the dirty rect propagation at the beginning of the
                // frame.
                let map_ancestor_to_vis = SpaceMapper::<LayoutPixel, VisPixel>::new_with_target(
                    visibility_spatial_node_index,
                    ancestor_spatial_node_index,
                    VisRect::max_rect(),
                    spatial_tree,
                );
                let ancestor_dirty_rect = map_ancestor_to_vis.unmap(&dirty_rect);

                let ancestor_bounds = ancestor_dirty_rect.map(|r| r.cast().to_rect().to_untyped());

                let mut clipper = Clipper::<PlaneSplitAnchor>::new();
                let planes = match Clipper::<PlaneSplitAnchor>::frustum_planes(&ancestor_matrix, ancestor_bounds) {
                    Ok(p) => p,
                    Err(_) => return false,
                };
                for plane in planes {
                    clipper.add(plane);
                }

                let polygon = Polygon::from_rect(
                    local_rect.to_rect().to_untyped(),
                    plane_split_anchor,
                );
                let clipped: Vec<_> = clipper.clip(polygon).to_vec();

                for poly in clipped {
                    if let Some(transformed) = poly.transform(&ancestor_matrix) {
                        splitter.add(transformed);
                    }
                }
            }
        }

        true
    }

    fn resolve_split_planes(
        splitter: &mut PlaneSplitter,
        ordered: &mut Vec<OrderedPictureChild>,
        gpu_buffer: &mut GpuBufferBuilderF,
        ancestor_index: SpatialNodeIndex,
        spatial_tree: &SpatialTree,
    ) {
        ordered.clear();

        // Process the accumulated split planes and order them for rendering.
        // Z axis is directed at the screen, `sort` is ascending, and we need back-to-front order.
        let sorted = splitter.sort(vec3(0.0, 0.0, 1.0));
        ordered.reserve(sorted.len());
        for poly in sorted {
            let transform = match spatial_tree
                .get_relative_transform(poly.anchor.spatial_node_index, ancestor_index)
                .inverse()
            {
                Some(transform) => transform.into_transform(),
                // logging this would be a bit too verbose
                None => continue,
            };

            let local_points = [
                transform.transform_point3d(poly.points[0].cast_unit().to_f32()),
                transform.transform_point3d(poly.points[1].cast_unit().to_f32()),
                transform.transform_point3d(poly.points[2].cast_unit().to_f32()),
                transform.transform_point3d(poly.points[3].cast_unit().to_f32()),
            ];

            // If any of the points are un-transformable, just drop this
            // plane from drawing.
            if local_points.iter().any(|p| p.is_none()) {
                continue;
            }

            let p0 = local_points[0].unwrap();
            let p1 = local_points[1].unwrap();
            let p2 = local_points[2].unwrap();
            let p3 = local_points[3].unwrap();

            let mut writer = gpu_buffer.write_blocks(2);
            writer.push_one([p0.x, p0.y, p1.x, p1.y]);
            writer.push_one([p2.x, p2.y, p3.x, p3.y]);
            let gpu_address = writer.finish();

            ordered.push(OrderedPictureChild {
                anchor: poly.anchor,
                gpu_address,
            });
        }
    }

    /// Called during initial picture traversal, before we know the
    /// bounding rect of children. It is possible to determine the
    /// surface / raster config now though.
    pub fn assign_surface(
        &mut self,
        frame_context: &FrameBuildingContext,
        parent_surface_index: Option<SurfaceIndex>,
        tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
        surfaces: &mut Vec<SurfaceInfo>,
    ) -> Option<SurfaceIndex> {
        // Reset raster config in case we early out below.
        self.raster_config = None;

        match self.composite_mode {
            Some(ref composite_mode) => {
                let surface_spatial_node_index = self.spatial_node_index;

                // Currently, we ensure that the scaling factor is >= 1.0 as a smaller scale factor can result in blurry output.
                let mut min_scale;
                let mut max_scale = 1.0e32;

                // If a raster root is established, this surface should be scaled based on the scale factors of the surface raster to parent raster transform.
                // This scaling helps ensure that the content in this surface does not become blurry or pixelated when composited in the parent surface.

                let world_scale_factors = match parent_surface_index {
                    Some(parent_surface_index) => {
                        let parent_surface = &surfaces[parent_surface_index.0];

                        let local_to_surface = frame_context
                            .spatial_tree
                            .get_relative_transform(
                                surface_spatial_node_index,
                                parent_surface.surface_spatial_node_index,
                            );

                        // A perspective surface's contents are coplanar, so a
                        // transform whose only perspective terms are m34/m44 still maps
                        // them with a constant w, and exact scale factors exist. Only a
                        // true keystone (m14/m24 non-zero) has no single reasonable
                        // scale; there, use (1,1), which is what Gecko does when it
                        // chooses to supply a scale factor anyway. In future, we might be
                        // able to improve the quality for those by taking into account
                        // the screen rect after clipping, but for now this gives better
                        // results than just taking the matrix scale factors.
                        //
                        // Only ever scale up: a perspective surface that is minified
                        // gains nothing from rasterizing smaller, and the foreshortening
                        // at the perspective origin makes the exact scale marginally less
                        // than one for content that is essentially flat, which would
                        // resample it for no reason.
                        //
                        // Both of those only apply to a perspective mapping, and
                        // `coplanar_scale_factors` also succeeds without one. A surface
                        // minified by a plain 2d transform must still rasterize small,
                        // so it keeps its exact, unclamped scale factors.
                        let scale_factors = if local_to_surface.is_perspective() {
                            local_to_surface
                                .coplanar_scale_factors()
                                .map_or((1.0, 1.0), |(x, y)| (x.max(1.0), y.max(1.0)))
                        } else {
                            local_to_surface.scale_factors()
                        };

                        let scale_factors = (
                            scale_factors.0 * parent_surface.world_scale_factors.0,
                            scale_factors.1 * parent_surface.world_scale_factors.1,
                        );

                        scale_factors
                    }
                    None => {
                        let local_to_surface_scale_factors = frame_context
                            .spatial_tree
                            .get_relative_transform(
                                surface_spatial_node_index,
                                frame_context.spatial_tree.root_reference_frame_index(),
                            )
                            .scale_factors();

                        let scale_factors = (
                            local_to_surface_scale_factors.0,
                            local_to_surface_scale_factors.1,
                        );

                        scale_factors
                    }
                };

                // For some primitives (e.g. text runs) we can't rely on the bounding rect being
                // exactly correct. For these cases, ensure we set a scissor rect when drawing
                // this picture to a surface.
                // TODO(gw) In future, we may be able to improve how the text run bounding rect is
                // calculated so that we don't need to do this. We could either fix Gecko up to
                // provide an exact bounds, or we could calculate the bounding rect internally in
                // WR, which would be easier to do efficiently once we have retained text runs
                // as part of the planned frame-tree interface changes.
                let force_scissor_rect = self.prim_list.needs_scissor_rect;

                // Check if there is perspective or if an SVG filter is applied, and thus whether a new
                // rasterization root should be established. `surface_snaps` records
                // whether content rasterized into this surface should be snapped:
                // false for a non-snapping raster root, where snapping against its
                // own scaled node would collapse content (see `raster-root-huge-scale`).
                let (device_pixel_scale, raster_spatial_node_index, surface_snaps, local_scale, world_scale_factors) = match composite_mode {
                    PictureCompositeMode::TileCache { slice_id } => {
                        let tile_cache = tile_caches.get_mut(&slice_id).unwrap();

                        // Get the complete scale-offset from local space to device space
                        let local_to_device = get_relative_scale_offset(
                            tile_cache.spatial_node_index,
                            frame_context.root_spatial_node_index,
                            frame_context.spatial_tree,
                        );
                        let local_to_cur_raster_scale = local_to_device.scale.x / tile_cache.current_raster_scale;

                        // We only update the raster scale if we're in high quality zoom mode, or there is no
                        // pinch-zoom active, or the zoom has doubled or halved since the raster scale was
                        // last updated. During a low-quality zoom we therefore typically retain the previous
                        // scale factor, which avoids expensive re-rasterizations, except for when the zoom
                        // has become too large or too small when we re-rasterize to avoid bluriness or a
                        // proliferation of picture cache tiles. When the zoom ends we select a high quality
                        // scale factor for the next frame to be drawn.
                        if !frame_context.fb_config.low_quality_pinch_zoom
                            || !frame_context
                                .spatial_tree.get_spatial_node(tile_cache.spatial_node_index)
                                .is_ancestor_or_self_zooming
                            || local_to_cur_raster_scale <= 0.5
                            || local_to_cur_raster_scale >= 2.0
                        {
                            tile_cache.current_raster_scale = local_to_device.scale.x;
                        }

                        // We may need to minify when zooming out picture cache tiles
                        min_scale = 0.0;

                        if frame_context.fb_config.low_quality_pinch_zoom {
                            // Force the scale for this tile cache to be the currently selected
                            // local raster scale, so we don't need to rasterize tiles during
                            // the pinch-zoom.
                            min_scale = tile_cache.current_raster_scale;
                            max_scale = tile_cache.current_raster_scale;
                        }

                        // Pick the largest scale factor of the transform for the scaling factor.
                        let scaling_factor = world_scale_factors.0.max(world_scale_factors.1).max(min_scale).min(max_scale);

                        let device_pixel_scale = Scale::new(scaling_factor);

                        // Tile caches snap against their own (scroll-stable) raster node.
                        (device_pixel_scale, surface_spatial_node_index, true, (1.0, 1.0), world_scale_factors)
                    }
                    _ => {
                        let surface_spatial_node = frame_context.spatial_tree.get_spatial_node(surface_spatial_node_index);

                        let enable_snapping =
                            surface_spatial_node.coordinate_system_id == CoordinateSystemId::root();

                        if enable_snapping {
                            let raster_spatial_node_index = frame_context.spatial_tree.root_reference_frame_index();

                            let local_to_raster_transform = frame_context
                                .spatial_tree
                                .get_relative_transform(
                                    self.spatial_node_index,
                                    raster_spatial_node_index,
                                );

                            let local_scale = local_to_raster_transform.scale_factors();

                            // Root-snapping surface: raster node is root, content snaps.
                            (Scale::new(1.0), raster_spatial_node_index, true, local_scale, (1.0, 1.0))
                        } else {
                            // If client supplied a specific local scale, use that instead of
                            // estimating from parent transform
                            let world_scale_factors = match self.raster_space {
                                RasterSpace::Screen => world_scale_factors,
                                RasterSpace::Local(scale) => (scale, scale),
                            };

                            let device_pixel_scale = Scale::new(
                                world_scale_factors.0.max(world_scale_factors.1).min(max_scale)
                            );

                            // Non-snapping raster root: its raster node is its own
                            // (scaled) node, so content is left unsnapped — snapping
                            // through the surface's local scale would collapse it.
                            (device_pixel_scale, surface_spatial_node_index, false, (1.0, 1.0), world_scale_factors)
                        }
                    }
                };

                let mut surface = SurfaceInfo::new(
                    surface_spatial_node_index,
                    raster_spatial_node_index,
                    frame_context.global_screen_device_rect,
                    &frame_context.spatial_tree,
                    device_pixel_scale,
                    world_scale_factors,
                    local_scale,
                    surface_snaps,
                    force_scissor_rect,
                );

                // For a backdrop filter the SVGFE graph composites in this
                // surface's (backdrop-root) space, but its subregions are
                // authored against the filtered element's spatial node. Record
                // the mapping between the two so every coverage path can map the
                // subregions consistently. Recomputed per frame, so an async
                // (APZ) scroll of either node is accounted for.
                //
                // Only a 2D scale+offset relationship is handled here; if the
                // element->backdrop-root transform is not a 2D scale/translation
                // (e.g. rotation or a 3D transform) `as_2d_scale_offset` returns
                // None and the mapping stays identity, leaving the subregions
                // as authored for that rare case.
                if let PictureCompositeMode::SVGFEGraph(_, source_spatial_node_index) = &composite_mode {
                    if *source_spatial_node_index != surface_spatial_node_index {
                        if let Some(scale_offset) = frame_context.spatial_tree
                            .get_relative_transform(*source_spatial_node_index, surface_spatial_node_index)
                            .as_2d_scale_offset()
                        {
                            surface.svgfe_source_map = scale_offset;
                        }
                    }
                }

                let surface_index = SurfaceIndex(surfaces.len());

                surfaces.push(surface);

                self.raster_config = Some(RasterConfig {
                    composite_mode: composite_mode.clone(),
                    surface_index,
                });

                Some(surface_index)
            }
            None => {
                None
            }
        }
    }

    /// Called after updating child pictures during the initial
    /// picture traversal. Bounding rects are propagated from
    /// child pictures up to parent picture surfaces, so that the
    /// parent bounding rect includes any dynamic picture bounds.
    pub fn propagate_bounding_rect(
        &mut self,
        surface_index: SurfaceIndex,
        parent_surface_index: Option<SurfaceIndex>,
        surfaces: &mut [SurfaceInfo],
        frame_context: &FrameBuildingContext,
    ) {
        let surface = &mut surfaces[surface_index.0];

        // Snapper into this surface's raster space, reused across all clusters
        // (a no-op for surfaces that don't snap).
        let mut snapper = SpaceSnapper::new(surface, frame_context.spatial_tree);

        for cluster in &mut self.prim_list.clusters {
            cluster.flags.remove(ClusterFlags::IS_VISIBLE);

            // Skip the cluster if backface culled.
            if !cluster.flags.contains(ClusterFlags::IS_BACKFACE_VISIBLE) {
                // For in-preserve-3d primitives and pictures, the backface visibility is
                // evaluated relative to the containing block.
                if let Picture3DContext::In { ancestor_index, .. } = self.context_3d {
                    let mut face = VisibleFace::Front;
                    frame_context.spatial_tree.get_relative_transform_with_face(
                        cluster.spatial_node_index,
                        ancestor_index,
                        Some(&mut face),
                    );
                    if face == VisibleFace::Back {
                        continue
                    }
                }
            }

            // No point including this cluster if it can't be transformed
            let spatial_node = &frame_context
                .spatial_tree
                .get_spatial_node(cluster.spatial_node_index);
            if !spatial_node.invertible {
                continue;
            }

            // Map the cluster bounding rect into the space of the surface, and
            // include it in the surface bounding rect.
            surface.map_local_to_picture.set_target_spatial_node(
                cluster.spatial_node_index,
                frame_context.spatial_tree,
            );

            // Mark the cluster visible, since it passed the invertible and
            // backface checks.
            cluster.flags.insert(ClusterFlags::IS_VISIBLE);

            // Snap the cluster bounding rect into the surface's raster space
            // (the space this picture's content is rasterized in), mirroring
            // the per-prim snap done in the visibility pass. Note that this
            // alone is not enough to make `surface.unclipped_local_rect`
            // snap-correct — see the SNAPTODO on that field.
            snapper.set_target_spatial_node(cluster.spatial_node_index, frame_context.spatial_tree);
            let snapped_bounding_rect = snapper.snap_rect(&cluster.unsnapped_bounding_rect);
            if let Some(cluster_rect) = surface.map_local_to_picture.map(&snapped_bounding_rect) {
                surface.unclipped_local_rect = surface.unclipped_local_rect.union(&cluster_rect);
            }
        }

        // If this picture establishes a surface, then map the surface bounding
        // rect into the parent surface coordinate space, and propagate that up
        // to the parent.
        if let Some(ref mut raster_config) = self.raster_config {
            // Propagate up to parent surface, now that we know this surface's static rect
            if let Some(parent_surface_index) = parent_surface_index {
                let surface_rect = raster_config.composite_mode.get_coverage(
                    surface,
                    Some(surface.unclipped_local_rect.cast_unit()),
                );

                let parent_surface = &mut surfaces[parent_surface_index.0];
                parent_surface.map_local_to_picture.set_target_spatial_node(
                    self.spatial_node_index,
                    frame_context.spatial_tree,
                );

                // Drop shadows draw both a content and shadow rect, so need to expand the local
                // rect of any surfaces to be composited in parent surfaces correctly.

                if let Some(parent_surface_rect) = parent_surface
                    .map_local_to_picture
                    .map(&surface_rect)
                {
                    parent_surface.unclipped_local_rect =
                        parent_surface.unclipped_local_rect.union(&parent_surface_rect);
                }
            }
        }
    }

    #[cold]
    fn draw_debug_overlay(
        &self,
        parent_surface_index: Option<SurfaceIndex>,
        frame_state: &FrameBuildingState,
        frame_context: &FrameBuildingContext,
        tile_caches: &FastHashMap<SliceId, Box<TileCacheInstance>>,
        scratch: &mut PrimitiveScratchBuffer,
    ) {
        fn draw_debug_border(
            local_rect: &PictureRect,
            thickness: i32,
            pic_to_root_mapper: &SpaceMapper<PicturePixel, DevicePixel>,
            color: ColorF,
            scratch: &mut PrimitiveScratchBuffer,
        ) {
            if let Some(device_rect) = pic_to_root_mapper.map(&local_rect) {
                scratch.push_debug_rect(
                    device_rect,
                    thickness,
                    color,
                    ColorF::TRANSPARENT,
                );
            }
        }

        let flags = frame_context.debug_flags;
        let draw_borders = flags.contains(DebugFlags::PICTURE_BORDERS);
        let draw_tile_dbg = flags.contains(DebugFlags::PICTURE_CACHING_DBG);

        let surface_index = match &self.raster_config {
            Some(raster_config) => raster_config.surface_index,
            None => parent_surface_index.expect("bug: no parent"),
        };
        let surface_spatial_node_index = frame_state
            .surfaces[surface_index.0]
            .surface_spatial_node_index;

        let map_pic_to_root = SpaceMapper::new_with_target(
            frame_context.root_spatial_node_index,
            surface_spatial_node_index,
            frame_context.global_screen_device_rect,
            frame_context.spatial_tree,
        );

        let Some(raster_config) = &self.raster_config else {
            return;
        };

        if draw_borders {
            let layer_color;
            if let PictureCompositeMode::TileCache { slice_id } = &raster_config.composite_mode {
                // Tiled picture;
                layer_color = ColorF::new(0.0, 1.0, 0.0, 0.8);

                let Some(tile_cache) = tile_caches.get(&slice_id) else {
                    return;
                };

                // Draw a rectangle for each tile.
                for (_, sub_slice) in tile_cache.sub_slices.iter().enumerate() {
                    for tile in sub_slice.tiles.values() {
                        if !tile.is_visible {
                            continue;
                        }
                        let rect = tile.cached_surface.local_rect.intersection(&tile_cache.local_rect);
                        if let Some(rect) = rect {
                            draw_debug_border(
                                &rect,
                                1,
                                &map_pic_to_root,
                                ColorF::new(0.0, 1.0, 0.0, 0.2),
                                scratch,
                            );
                        }
                    }
                }
            } else {
                // Non-tiled picture
                layer_color = ColorF::new(1.0, 0.0, 0.0, 0.5);
            }

            // Draw a rectangle for the whole picture.
            let pic_rect = frame_state
                .surfaces[raster_config.surface_index.0]
                .unclipped_local_rect;

            draw_debug_border(
                &pic_rect,
                3,
                &map_pic_to_root,
                layer_color,
                scratch,
            );
        }

        if draw_tile_dbg && self.is_visible(frame_context.spatial_tree) {
            if let PictureCompositeMode::TileCache { slice_id } = &raster_config.composite_mode {
                let Some(tile_cache) = tile_caches.get(&slice_id) else {
                    return;
                };
                for (sub_slice_index, sub_slice) in tile_cache.sub_slices.iter().enumerate() {
                    for tile in sub_slice.tiles.values() {
                        if !tile.is_visible {
                            continue;
                        }
                        tile.cached_surface.root.draw_debug_rects(
                            &map_pic_to_root,
                            tile.is_opaque,
                            tile.cached_surface.current_descriptor.local_valid_rect,
                            scratch,
                        );

                        let label_offset = DeviceVector2D::new(
                            20.0 + sub_slice_index as f32 * 20.0,
                            30.0 + sub_slice_index as f32 * 20.0,
                        );

                        if tile.device_tile_rect.height() >= label_offset.y {
                            let surface = tile.surface.as_ref().expect("no tile surface set!");

                            scratch.push_debug_string(
                                tile.device_tile_rect.min + label_offset,
                                debug_colors::RED,
                                format!("{:?}: s={} is_opaque={} surface={} sub={}",
                                        tile.id,
                                        tile_cache.slice,
                                        tile.is_opaque,
                                        surface.kind(),
                                        sub_slice_index,
                                ),
                            );
                        }
                    }
                }
            }
        }
    }
}

pub fn get_relative_scale_offset(
    child_spatial_node_index: SpatialNodeIndex,
    parent_spatial_node_index: SpatialNodeIndex,
    spatial_tree: &SpatialTree,
) -> ScaleOffset {
    let transform = spatial_tree.get_relative_transform(
        child_spatial_node_index,
        parent_spatial_node_index,
    );
    let mut scale_offset = match transform {
        CoordinateSpaceMapping::Local => ScaleOffset::identity(),
        CoordinateSpaceMapping::ScaleOffset(scale_offset) => scale_offset,
        CoordinateSpaceMapping::Transform(m) => {
            ScaleOffset::from_transform(&m).expect("bug: pictures caches don't support complex transforms")
        }
    };

    // Compositors expect things to be aligned on device pixels. Logic at a higher level ensures that is
    // true, but floating point inaccuracy can sometimes result in small differences, so remove
    // them here.
    scale_offset.offset = scale_offset.offset.round();

    scale_offset
}

/// Update dirty rects, ensure that tiles have backing surfaces and build
/// the tile render tasks.
fn prepare_tiled_picture_surface(
    surface_index: SurfaceIndex,
    slice_id: SliceId,
    surface_spatial_node_index: SpatialNodeIndex,
    map_pic_to_device: &SpaceMapper<PicturePixel, DevicePixel>,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
) {
    let tile_cache = tile_caches.get_mut(&slice_id).unwrap();
    let mut debug_info = SliceDebugInfo::new();
    let mut surface_render_tasks = FastHashMap::default();
    let mut surface_local_dirty_rect = PictureRect::zero();
    let device_pixel_scale = frame_state
        .surfaces[surface_index.0]
        .device_pixel_scale;
    let mut at_least_one_tile_visible = false;

    // Get the overall device space rect of the picture cache. Used to clip
    // the tile rects below for occlusion testing to the relevant area.
    let device_pic_rect = map_pic_to_device
        .map(&tile_cache.local_clip_rect)
        .expect("bug: unable to map clip rect")
        .round();

    // The composite clip must be on the same device grid the tiles are placed
    // on (the compositor transform), not the raw pic->device transform. When the
    // tile cache's device offset is fractional (e.g. a transformed, flex-centered
    // scroller), `get_relative_scale_offset` rounds the compositor offset to an
    // integer, so the two grids differ by up to a pixel; deriving the clip from
    // pic->device then clips content that was placed on the compositor grid,
    // cropping the max-side edges.
    let device_clip_rect = frame_state
        .composite_state
        .get_device_rect(&tile_cache.local_clip_rect, tile_cache.transform_index);

    for (sub_slice_index, sub_slice) in tile_cache.sub_slices.iter_mut().enumerate() {
        for tile in sub_slice.tiles.values_mut() {
            // Ensure that the dirty rect doesn't extend outside the local valid rect.
            tile.cached_surface.local_dirty_rect = tile.cached_surface.local_dirty_rect
                .intersection(&tile.cached_surface.current_descriptor.local_valid_rect)
                .unwrap_or_else(|| { tile.cached_surface.is_valid = true; PictureRect::zero() });

            let valid_rect = frame_state.composite_state.get_surface_rect(
                &tile.cached_surface.current_descriptor.local_valid_rect,
                &tile.cached_surface.local_rect,
                tile_cache.transform_index,
            ).to_i32();

            let scissor_rect = frame_state.composite_state.get_surface_rect(
                &tile.cached_surface.local_dirty_rect,
                &tile.cached_surface.local_rect,
                tile_cache.transform_index,
            ).to_i32().intersection(&valid_rect).unwrap_or_else(|| { Box2D::zero() });

            if tile.is_visible {
                // Get the world space rect that this tile will actually occupy on screen
                let device_draw_rect = device_pic_rect.intersection(&tile.device_valid_rect);

                // If that draw rect is occluded by some set of tiles in front of it,
                // then mark it as not visible and skip drawing. When it's not occluded
                // it will fail this test, and get rasterized by the render task setup
                // code below.
                match device_draw_rect {
                    Some(device_draw_rect) => {
                        let check_occluded_tiles = match frame_state.composite_state.compositor_kind {
                            CompositorKind::Layer { .. } => true,
                            CompositorKind::Native { .. } | CompositorKind::Draw { .. } => {
                                // Only check for occlusion on visible tiles that are fixed position.
                                tile_cache.spatial_node_index == frame_context.root_spatial_node_index
                            }
                        };
                        if check_occluded_tiles &&
                           frame_state.composite_state.occluders.is_tile_occluded(tile.z_id, device_draw_rect) {
                            // If this tile has an allocated native surface, free it, since it's completely
                            // occluded. We will need to re-allocate this surface if it becomes visible,
                            // but that's likely to be rare (e.g. when there is no content display list
                            // for a frame or two during a tab switch).
                            let surface = tile.surface.as_mut().expect("no tile surface set!");

                            if let TileSurface::Texture { descriptor: SurfaceTextureDescriptor::Native { id, .. }, .. } = surface {
                                if let Some(id) = id.take() {
                                    frame_state.resource_cache.destroy_compositor_tile(id);
                                }
                            }

                            tile.is_visible = false;

                            if frame_context.fb_config.testing {
                                debug_info.tiles.insert(
                                    tile.tile_offset,
                                    TileDebugInfo::Occluded,
                                );
                            }

                            continue;
                        }
                    }
                    None => {
                        tile.is_visible = false;
                    }
                }

                // In extreme zoom/offset cases, we may end up with a local scissor/valid rect
                // that becomes empty after transformation to device space (e.g. if the local
                // rect height is 0.00001 and the compositor transform has large scale + offset).
                // DirectComposition panics if we try to BeginDraw with an empty rect, so catch
                // that here and mark the tile non-visible. This is a bit of a hack - we should
                // ideally handle these in a more accurate way so we don't end up with an empty
                // rect here.
                if !tile.cached_surface.is_valid && (scissor_rect.is_empty() || valid_rect.is_empty()) {
                    tile.is_visible = false;
                }
            }

            // If we get here, we want to ensure that the surface remains valid in the texture
            // cache, _even if_ it's not visible due to clipping or being scrolled off-screen.
            // This ensures that we retain valid tiles that are off-screen, but still in the
            // display port of this tile cache instance.
            if let Some(TileSurface::Texture { descriptor, .. }) = tile.surface.as_ref() {
                if let SurfaceTextureDescriptor::TextureCache { handle: Some(handle), .. } = descriptor {
                    frame_state.resource_cache
                        .picture_textures.request(handle);
                }
            }

            // If the tile has been found to be off-screen / clipped, skip any further processing.
            if !tile.is_visible {
                if frame_context.fb_config.testing {
                    debug_info.tiles.insert(
                        tile.tile_offset,
                        TileDebugInfo::Culled,
                    );
                }

                continue;
            }

            at_least_one_tile_visible = true;

            if let TileSurface::Texture { descriptor, .. } = tile.surface.as_mut().unwrap() {
                match descriptor {
                    SurfaceTextureDescriptor::TextureCache { ref handle, .. } => {
                        let exists = handle.as_ref().map_or(false,
                            |handle| frame_state.resource_cache.picture_textures.entry_exists(handle)
                        );
                        // Invalidate if the backing texture was evicted.
                        if exists {
                            // Request the backing texture so it won't get evicted this frame.
                            // We specifically want to mark the tile texture as used, even
                            // if it's detected not visible below and skipped. This is because
                            // we maintain the set of tiles we care about based on visibility
                            // during pre_update. If a tile still exists after that, we are
                            // assuming that it's either visible or we want to retain it for
                            // a while in case it gets scrolled back onto screen soon.
                            // TODO(gw): Consider switching to manual eviction policy?
                            frame_state.resource_cache
                                .picture_textures
                                .request(handle.as_ref().unwrap());
                        } else {
                            // If the texture was evicted on a previous frame, we need to assume
                            // that the entire tile rect is dirty.
                            tile.invalidate(None, InvalidationReason::NoTexture);
                        }
                    }
                    SurfaceTextureDescriptor::Native { id, .. } => {
                        if id.is_none() {
                            // There is no current surface allocation, so ensure the entire tile is invalidated
                            tile.invalidate(None, InvalidationReason::NoSurface);
                        }
                    }
                }
            }

            // Ensure - again - that the dirty rect doesn't extend outside the local valid rect,
            // as the tile could have been invalidated since the first computation.
            tile.cached_surface.local_dirty_rect = tile.cached_surface.local_dirty_rect
                .intersection(&tile.cached_surface.current_descriptor.local_valid_rect)
                .unwrap_or_else(|| { tile.cached_surface.is_valid = true; PictureRect::zero() });

            surface_local_dirty_rect = surface_local_dirty_rect.union(&tile.cached_surface.local_dirty_rect);

            // Update the device dirty rect
            let device_dirty_rect = map_pic_to_device
                .map(&tile.cached_surface.local_dirty_rect)
                .expect("bug");

            let device_rect = tile.device_tile_rect.round();
            tile.device_dirty_rect = device_dirty_rect
                .round_out()
                .intersection(&device_rect)
                .unwrap_or_else(DeviceRect::zero);

            if tile.cached_surface.is_valid {
                if frame_context.fb_config.testing {
                    debug_info.tiles.insert(
                        tile.tile_offset,
                        TileDebugInfo::Valid,
                    );
                }
            } else {
                // Track that actual tile rasterization is occurring
                frame_state.composite_state.did_rasterize_any_tile = true;

                // Add this dirty rect to the dirty region tracker. This must be done outside the if statement below,
                // so that we include in the dirty region tiles that are handled by a background color only (no
                // surface allocation).
                tile_cache.dirty_region.add_dirty_region(
                    tile.cached_surface.local_dirty_rect,
                    frame_context.spatial_tree,
                );

                // Ensure that this texture is allocated.
                if let TileSurface::Texture { ref mut descriptor } = tile.surface.as_mut().unwrap() {
                    match descriptor {
                        SurfaceTextureDescriptor::TextureCache { ref mut handle } => {

                            frame_state.resource_cache.picture_textures.update(
                                tile_cache.current_tile_size,
                                handle,
                                &mut frame_state.resource_cache.texture_cache.next_id,
                                &mut frame_state.resource_cache.texture_cache.pending_updates,
                            );
                        }
                        SurfaceTextureDescriptor::Native { id } => {
                            if id.is_none() {
                                // Allocate a native surface id if we're in native compositing mode,
                                // and we don't have a surface yet (due to first frame, or destruction
                                // due to tile size changing etc).
                                if sub_slice.native_surface.is_none() {
                                    let opaque = frame_state
                                        .resource_cache
                                        .create_compositor_surface(
                                            tile_cache.virtual_offset,
                                            tile_cache.current_tile_size,
                                            true,
                                        );

                                    let alpha = frame_state
                                        .resource_cache
                                        .create_compositor_surface(
                                            tile_cache.virtual_offset,
                                            tile_cache.current_tile_size,
                                            false,
                                        );

                                    sub_slice.native_surface = Some(NativeSurface {
                                        opaque,
                                        alpha,
                                    });
                                }

                                // Create the tile identifier and allocate it.
                                let surface_id = if tile.is_opaque {
                                    sub_slice.native_surface.as_ref().unwrap().opaque
                                } else {
                                    sub_slice.native_surface.as_ref().unwrap().alpha
                                };

                                let tile_id = NativeTileId {
                                    surface_id,
                                    x: tile.tile_offset.x,
                                    y: tile.tile_offset.y,
                                };

                                frame_state.resource_cache.create_compositor_tile(tile_id);

                                *id = Some(tile_id);
                            }
                        }
                    }

                    // The cast_unit() here is because the `content_origin` is expected to be in
                    // device pixels, however we're establishing raster roots for picture cache
                    // tiles meaning the `content_origin` needs to be in the local space of that root.
                    // TODO(gw): `content_origin` should actually be in RasterPixels to be consistent
                    //           with both local / screen raster modes, but this involves a lot of
                    //           changes to render task and picture code.
                    let content_origin_f = tile.cached_surface.local_rect.min.cast_unit() * device_pixel_scale;
                    let content_origin = content_origin_f.round();
                    // TODO: these asserts used to have a threshold of 0.01 but failed intermittently the
                    // gfx/layers/apz/test/mochitest/test_group_double_tap_zoom-2.html test on android.
                    // moving the rectangles in space mapping conversion code to the Box2D representaton
                    // made the failure happen more often.
                    debug_assert!((content_origin_f.x - content_origin.x).abs() < 0.15);
                    debug_assert!((content_origin_f.y - content_origin.y).abs() < 0.15);

                    let surface = descriptor.resolve(
                        frame_state.resource_cache,
                        tile_cache.current_tile_size,
                    );

                    // Recompute the scissor rect as the tile could have been invalidated since the first computation.
                    let scissor_rect = frame_state.composite_state.get_surface_rect(
                        &tile.cached_surface.local_dirty_rect,
                        &tile.cached_surface.local_rect,
                        tile_cache.transform_index,
                    ).to_i32();

                    let composite_task_size = tile_cache.current_tile_size;

                    let tile_key = TileKey {
                        sub_slice_index: SubSliceIndex::new(sub_slice_index),
                        tile_offset: tile.tile_offset,
                    };

                    let mut clear_color = ColorF::TRANSPARENT;

                    if SubSliceIndex::new(sub_slice_index).is_primary() {
                        if let Some(background_color) = tile_cache.background_color {
                            clear_color = background_color;
                        }

                        // If this picture cache has a spanning_opaque_color, we will use
                        // that as the clear color. The primitive that was detected as a
                        // spanning primitive will have been set with IS_BACKDROP, causing
                        // it to be skipped and removing everything added prior to it
                        // during batching.
                        if let Some(color) = tile_cache.backdrop.spanning_opaque_color {
                            clear_color = color;
                        }
                    }

                    let cmd_buffer_index = frame_state.cmd_buffers.create_cmd_buffer();

                    // TODO(gw): As a performance optimization, we could skip the resolve picture
                    //           if the dirty rect is the same as the resolve rect (probably quite
                    //           common for effects that scroll underneath a backdrop-filter, for example).
                    let use_tile_composite = !tile.cached_surface.sub_graphs.is_empty();

                    if use_tile_composite {
                        let mut local_content_rect = tile.cached_surface.local_dirty_rect;

                        for (sub_graph_rect, surface_stack) in &tile.cached_surface.sub_graphs {
                            if let Some(dirty_sub_graph_rect) = sub_graph_rect.intersection(&tile.cached_surface.local_dirty_rect) {
                                for (composite_mode, surface_index) in surface_stack {
                                    let surface = &frame_state.surfaces[surface_index.0];

                                    let rect = composite_mode.get_coverage(
                                        surface,
                                        Some(dirty_sub_graph_rect.cast_unit()),
                                    ).cast_unit();

                                    local_content_rect = local_content_rect.union(&rect);
                                }
                            }
                        }

                        // We know that we'll never need to sample > 300 device pixels outside the tile
                        // for blurring, so clamp the content rect here so that we don't try to allocate
                        // a really large surface in the case of a drop-shadow with large offset.
                        let max_content_rect = (tile.cached_surface.local_dirty_rect.cast_unit() * device_pixel_scale)
                            .inflate(
                                MAX_BLUR_RADIUS * BLUR_SAMPLE_SCALE,
                                MAX_BLUR_RADIUS * BLUR_SAMPLE_SCALE,
                            )
                            .round_out()
                            .to_i32();

                        let content_device_rect = (local_content_rect.cast_unit() * device_pixel_scale)
                            .round_out()
                            .to_i32();

                        let content_device_rect = content_device_rect
                            .intersection(&max_content_rect)
                            .expect("bug: no intersection with tile dirty rect: {content_device_rect:?} / {max_content_rect:?}");

                        let content_task_size = content_device_rect.size();
                        let normalized_content_rect = content_task_size.into();

                        let inner_offset = content_origin + scissor_rect.min.to_vector().to_f32();
                        let outer_offset = content_device_rect.min.to_f32();
                        let sub_rect_offset = (inner_offset - outer_offset).round().to_i32();

                        let render_task_id = frame_state.rg_builder.add().init(
                            RenderTask::new_dynamic(
                                content_task_size,
                                RenderTaskKind::new_picture(
                                    content_task_size,
                                    true,
                                    content_device_rect.min.to_f32(),
                                    surface_spatial_node_index,
                                    // raster == surface implicitly for picture cache tiles
                                    surface_spatial_node_index,
                                    device_pixel_scale,
                                    Some(normalized_content_rect),
                                    None,
                                    Some(clear_color),
                                    cmd_buffer_index,
                                    false,
                                    None,
                                )
                            ),
                        );

                        let composite_task_id = frame_state.rg_builder.add().init(
                            RenderTask::new(
                                RenderTaskLocation::Static {
                                    surface: StaticRenderTaskSurface::PictureCache {
                                        surface,
                                    },
                                    rect: composite_task_size.into(),
                                },
                                RenderTaskKind::new_tile_composite(
                                    sub_rect_offset,
                                    scissor_rect,
                                    valid_rect,
                                    clear_color,
                                ),
                            ),
                        );

                        surface_render_tasks.insert(
                            tile_key,
                            SurfaceTileDescriptor {
                                current_task_id: render_task_id,
                                composite_task_id: Some(composite_task_id),
                                dirty_rect: tile.cached_surface.local_dirty_rect,
                            },
                        );
                    } else {
                        let render_task_id = frame_state.rg_builder.add().init(
                            RenderTask::new(
                                RenderTaskLocation::Static {
                                    surface: StaticRenderTaskSurface::PictureCache {
                                        surface,
                                    },
                                    rect: composite_task_size.into(),
                                },
                                RenderTaskKind::new_picture(
                                    composite_task_size,
                                    true,
                                    content_origin,
                                    surface_spatial_node_index,
                                    // raster == surface implicitly for picture cache tiles
                                    surface_spatial_node_index,
                                    device_pixel_scale,
                                    Some(scissor_rect),
                                    Some(valid_rect),
                                    Some(clear_color),
                                    cmd_buffer_index,
                                    false,
                                    None,
                                )
                            ),
                        );

                        surface_render_tasks.insert(
                            tile_key,
                            SurfaceTileDescriptor {
                                current_task_id: render_task_id,
                                composite_task_id: None,
                                dirty_rect: tile.cached_surface.local_dirty_rect,
                            },
                        );
                    }
                }

                if frame_context.fb_config.testing {
                    debug_info.tiles.insert(
                        tile.tile_offset,
                        TileDebugInfo::Dirty(DirtyTileDebugInfo {
                            local_valid_rect: tile.cached_surface.current_descriptor.local_valid_rect,
                            local_dirty_rect: tile.cached_surface.local_dirty_rect,
                        }),
                    );
                }
            }

            let surface = tile.surface.as_ref().expect("no tile surface set!");

            let descriptor = CompositeTileDescriptor {
                surface_kind: surface.into(),
                tile_id: tile.id,
            };

            let (surface, is_opaque) = match surface {
                TileSurface::Color { color } => {
                    (CompositeTileSurface::Color { color: *color }, true)
                }
                TileSurface::Texture { descriptor, .. } => {
                    let surface = descriptor.resolve(frame_state.resource_cache, tile_cache.current_tile_size);
                    (
                        CompositeTileSurface::Texture { surface },
                        tile.is_opaque
                    )
                }
            };

            if is_opaque {
                sub_slice.opaque_tile_descriptors.push(descriptor);
            } else {
                sub_slice.alpha_tile_descriptors.push(descriptor);
            }

            let composite_tile = CompositeTile {
                kind: tile_kind(&surface, is_opaque),
                surface,
                local_rect: tile.cached_surface.local_rect,
                local_valid_rect: tile.cached_surface.current_descriptor.local_valid_rect,
                local_dirty_rect: tile.cached_surface.local_dirty_rect,
                device_clip_rect,
                z_id: tile.z_id,
                transform_index: tile_cache.transform_index,
                clip_index: tile_cache.compositor_clip,
                tile_id: Some(tile.id),
            };

            sub_slice.composite_tiles.push(composite_tile);

            // Now that the tile is valid, reset the dirty rect.
            tile.cached_surface.local_dirty_rect = PictureRect::zero();
            tile.cached_surface.is_valid = true;
        }

        // Sort the tile descriptor lists, since iterating values in the tile_cache.tiles
        // hashmap doesn't provide any ordering guarantees, but we want to detect the
        // composite descriptor as equal if the tiles list is the same, regardless of
        // ordering.
        sub_slice.opaque_tile_descriptors.sort_by_key(|desc| desc.tile_id);
        sub_slice.alpha_tile_descriptors.sort_by_key(|desc| desc.tile_id);
    }

    // Check to see if we should add backdrops as native surfaces.
    let backdrop_rect = tile_cache.backdrop.backdrop_rect
        .intersection(&tile_cache.local_rect)
        .and_then(|r| {
            r.intersection(&tile_cache.local_clip_rect)
    });

    let mut backdrop_in_use_and_visible = false;
    if let Some(backdrop_rect) = backdrop_rect {
        let supports_surface_for_backdrop = match frame_state.composite_state.compositor_kind {
            CompositorKind::Draw { .. } | CompositorKind::Layer { .. } => {
                false
            }
            CompositorKind::Native { capabilities, .. } => {
                capabilities.supports_surface_for_backdrop
            }
        };
        if supports_surface_for_backdrop && !tile_cache.found_prims_after_backdrop && at_least_one_tile_visible {
            if let Some(BackdropKind::Color { color }) = tile_cache.backdrop.kind {
                backdrop_in_use_and_visible = true;

                // We're going to let the compositor handle the backdrop as a native surface.
                // Hide all of our sub_slice tiles so they aren't also trying to draw it.
                for sub_slice in &mut tile_cache.sub_slices {
                    for tile in sub_slice.tiles.values_mut() {
                        tile.is_visible = false;
                    }
                }

                // Destroy our backdrop surface if it doesn't match the new color.
                // TODO: This is a performance hit for animated color backdrops.
                if let Some(backdrop_surface) = &tile_cache.backdrop_surface {
                    if backdrop_surface.color != color {
                        frame_state.resource_cache.destroy_compositor_surface(backdrop_surface.id);
                        tile_cache.backdrop_surface = None;
                    }
                }

                // Calculate the device_rect for the backdrop, which is just the backdrop_rect
                // converted into world space and scaled to device pixels.
                let backdrop_device_rect = map_pic_to_device
                    .map(&backdrop_rect)
                    .expect("bug: unable to map backdrop rect")
                    .round();

                // If we already have a backdrop surface, update the device rect. Otherwise, create
                // a backdrop surface.
                if let Some(backdrop_surface) = &mut tile_cache.backdrop_surface {
                    backdrop_surface.device_rect = backdrop_device_rect;
                } else {
                    // Create native compositor surface with color for the backdrop and store the id.
                    tile_cache.backdrop_surface = Some(BackdropSurface {
                        id: frame_state.resource_cache.create_compositor_backdrop_surface(color),
                        color,
                        device_rect: backdrop_device_rect,
                    });
                }
            }
        }
    }

    if !backdrop_in_use_and_visible {
        if let Some(backdrop_surface) = &tile_cache.backdrop_surface {
            // We've already allocated a backdrop surface, but we're not using it.
            // Tell the compositor to get rid of it.
            frame_state.resource_cache.destroy_compositor_surface(backdrop_surface.id);
            tile_cache.backdrop_surface = None;
        }
    }

    // If invalidation debugging is enabled, dump the picture cache state to a tree printer.
    if frame_context.debug_flags.contains(DebugFlags::INVALIDATION_DBG) {
        tile_cache.print();
    }

    // If testing mode is enabled, write some information about the current state
    // of this picture cache (made available in RenderResults).
    if frame_context.fb_config.testing {
        debug_info.compositor_clip = tile_cache.compositor_clip.map(|clip_index| {
            let clip = frame_state.composite_state.get_compositor_clip(clip_index);
            CompositorClipDebugInfo {
                rect: clip.rect,
                radius: clip.radius,
            }
        });

        frame_state.composite_state
            .picture_cache_debug
            .slices
            .insert(
                tile_cache.slice,
                debug_info,
            );
    }

    let descriptor = SurfaceDescriptor::new_tiled(surface_render_tasks);

    frame_state.surface_builder.push_surface(
        surface_index,
        false,
        surface_local_dirty_rect,
        Some(descriptor),
        frame_state.surfaces,
        frame_state.rg_builder,
    );
}

fn compute_subpixel_mode(
    raster_config: &Option<RasterConfig>,
    tile_caches: &FastHashMap<SliceId, Box<TileCacheInstance>>,
    parent_subpixel_mode: SubpixelMode,
) -> SubpixelMode {

    // Disallow subpixel AA if an intermediate surface is needed.
    // TODO(lsalzman): allow overriding parent if intermediate surface is opaque
    let subpixel_mode = match raster_config {
        Some(RasterConfig { ref composite_mode, .. }) => {
            let subpixel_mode = match composite_mode {
                PictureCompositeMode::TileCache { slice_id } => {
                    tile_caches[&slice_id].subpixel_mode
                }
                PictureCompositeMode::Blit(..) |
                PictureCompositeMode::ComponentTransferFilter(..) |
                PictureCompositeMode::Filter(..) |
                PictureCompositeMode::MixBlend(..) |
                PictureCompositeMode::IntermediateSurface |
                PictureCompositeMode::SVGFEGraph(..) => {
                    // TODO(gw): We can take advantage of the same logic that
                    //           exists in the opaque rect detection for tile
                    //           caches, to allow subpixel text on other surfaces
                    //           that can be detected as opaque.
                    SubpixelMode::Deny
                }
            };

            subpixel_mode
        }
        None => {
            SubpixelMode::Allow
        }
    };

    // Still disable subpixel AA if parent forbids it
    let subpixel_mode = match (parent_subpixel_mode, subpixel_mode) {
        (SubpixelMode::Allow, SubpixelMode::Allow) => {
            // Both parent and this surface unconditionally allow subpixel AA
            SubpixelMode::Allow
        }
        (SubpixelMode::Allow, SubpixelMode::Conditional { allowed_rect, prohibited_rect }) => {
            // Parent allows, but we are conditional subpixel AA
            SubpixelMode::Conditional {
                allowed_rect,
                prohibited_rect,
            }
        }
        (SubpixelMode::Conditional { allowed_rect, prohibited_rect }, SubpixelMode::Allow) => {
            // Propagate conditional subpixel mode to child pictures that allow subpixel AA
            SubpixelMode::Conditional {
                allowed_rect,
                prohibited_rect,
            }
        }
        (SubpixelMode::Conditional { .. }, SubpixelMode::Conditional { ..}) => {
            unreachable!("bug: only top level picture caches have conditional subpixel");
        }
        (SubpixelMode::Deny, _) | (_, SubpixelMode::Deny) => {
            // Either parent or this surface explicitly deny subpixel, these take precedence
            SubpixelMode::Deny
        }
    };

    subpixel_mode
}

pub fn prepare_picture_clips(
    pic: &PictureInstance,
    clip_chain: &ClipChainInstance,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    pic_scratch: &mut PictureScratch,
    clip_mask_instances: &mut Vec<ClipMaskKind>,
    prim_spatial_node_index: SpatialNodeIndex,
    data_stores: &DataStores,
    use_quads: bool,
    composite_target_clip_range: &mut Option<ClipNodeRange>,
    pic_context: &PictureContext,
) -> Option<ClipTaskIndex> {
    // TODO(gw): Much of the code in this branch could be moved in to a common
    //           function as we move more primitives to the new clip-mask paths.

    // We are going to split the clip mask tasks in to a list to be rendered
    // on the source picture, and those to be rendered in to a mask for
    // compositing the picture in to the target.
    let mut source_masks = Vec::new();
    let mut target_masks = Vec::new();

    // For some composite modes, we force target mask due to limitations. That
    // might results in artifacts for these modes (which are already an existing
    // problem) but we can handle these cases as follow ups.
    let force_target_mask = match pic.composite_mode {
        // We can't currently render over top of these filters as their size
        // may have changed due to downscaling. We could handle this separate
        // case as a follow up.
        Some(PictureCompositeMode::Filter(Filter::Blur { .. })) |
        Some(PictureCompositeMode::Filter(Filter::DropShadows { .. })) |
        Some(PictureCompositeMode::SVGFEGraph( .. )) => {
            true
        }
        _ => {
            false
        }
    };

    // Work out which clips get drawn in to the source / target mask
    for i in 0 .. clip_chain.clips_range.count {
        let clip_instance = frame_state.clip_store.get_instance_from_range(&clip_chain.clips_range, i);

        if !force_target_mask && clip_instance.flags.contains(ClipNodeFlags::SAME_COORD_SYSTEM) {
            source_masks.push(i);
        } else {
            target_masks.push(i);
        }
    }

    let pic_surface_index = pic.raster_config.as_ref().unwrap().surface_index;
    let prim_local_rect: LayoutRect = frame_state
        .surfaces[pic_surface_index.0]
        .clipped_local_rect
        .cast_unit();

    // Handle masks on the source. This is the common case, and occurs for:
    // (a) Any masks in the same coord space as the surface
    // (b) All masks if the surface and parent are axis-aligned
    if !source_masks.is_empty() {
        let first_clip_node_index = frame_state.clip_store.clip_node_instances.len() as u32;
        let parent_task_id = pic_scratch.primary_render_task_id.expect("bug: no composite mode");

        // Construct a new clip node range, also add image-mask dependencies as needed
        for instance in source_masks {
            let clip_instance = frame_state.clip_store.get_instance_from_range(&clip_chain.clips_range, instance);

            for tile in frame_state.clip_store.visible_mask_tiles(clip_instance) {
                frame_state.rg_builder.add_dependency(
                    parent_task_id,
                    tile.task_id,
                );
            }

            frame_state.clip_store.clip_node_instances.push(clip_instance.clone());
        }

        let clip_node_range = ClipNodeRange {
            first: first_clip_node_index,
            count: frame_state.clip_store.clip_node_instances.len() as u32 - first_clip_node_index,
        };

        // Add the mask as a sub-pass of the picture
        let pic_task_id = pic_scratch.primary_render_task_id.expect("uh oh");
        let pic_task = frame_state.rg_builder.get_task_mut(pic_task_id);

        let RenderTaskKind::Picture(info) = &pic_task.kind else { unreachable!() };

        let task_rect = DeviceRect::from_origin_and_size(
            info.content_origin,
            pic_task.get_target_size().to_f32(),
        );

        quad::prepare_clip_range(
            clip_node_range,
            pic_task_id,
            &task_rect,
            &prim_local_rect,
            prim_spatial_node_index,
            info.raster_spatial_node_index,
            info.device_pixel_scale,
            &data_stores.clip,
            frame_state.clip_store,
            frame_context.spatial_tree,
            frame_state.rg_builder,
            &mut frame_state.frame_gpu_data.f32,
            frame_state.transforms,
        );
    }

    // Handle masks on the target: clips applied while compositing the
    // picture. This is forced for some composite modes and otherwise
    // occurs for masks in parent space when non-axis-aligned to the
    // source space.
    if !target_masks.is_empty() {
        // Build a contiguous clip node range for the target masks.
        let first_clip_node_index = frame_state.clip_store.clip_node_instances.len() as u32;
        for instance in target_masks {
            let clip_instance = frame_state.clip_store.get_instance_from_range(&clip_chain.clips_range, instance);
            frame_state.clip_store.clip_node_instances.push(clip_instance.clone());
        }
        let clip_node_range = ClipNodeRange {
            first: first_clip_node_index,
            count: frame_state.clip_store.clip_node_instances.len() as u32 - first_clip_node_index,
        };

        if use_quads {
            // The quad compositing path applies these clips directly
            // (it renders/depends on any image-mask tiles itself).
            *composite_target_clip_range = Some(clip_node_range);
        } else {
            // Legacy brush path: draw a screen-space alpha mask that is
            // sampled when compositing this picture.
            let surface = &frame_state.surfaces[pic_context.surface_index.0];
            let coverage_rect = clip_chain.pic_coverage_rect;

            let device_pixel_scale = surface.device_pixel_scale;
            let raster_spatial_node_index = surface.raster_spatial_node_index;

            let Some(clipped_surface_rect) = surface.get_surface_rect(
                &coverage_rect,
                frame_context.spatial_tree,
            ) else {
                return None;
            };

            let empty_task = EmptyTask {
                content_origin: clipped_surface_rect.min.to_f32(),
                device_pixel_scale,
                raster_spatial_node_index,
            };

            let task_size = clipped_surface_rect.size();

            let clip_task_id = frame_state.rg_builder.add().init(RenderTask::new_dynamic(
                task_size,
                RenderTaskKind::Empty(empty_task),
            ));

            // Add image-mask tile dependencies to the mask task.
            for i in 0 .. clip_node_range.count {
                let clip_instance = frame_state.clip_store.get_instance_from_range(&clip_node_range, i);
                for tile in frame_state.clip_store.visible_mask_tiles(clip_instance) {
                    frame_state.rg_builder.add_dependency(
                        clip_task_id,
                        tile.task_id,
                    );
                }
            }

            let task_rect = clipped_surface_rect.to_f32();

            quad::prepare_clip_range(
                clip_node_range,
                clip_task_id,
                &task_rect,
                &prim_local_rect,
                prim_spatial_node_index,
                raster_spatial_node_index,
                device_pixel_scale,
                &data_stores.clip,
                frame_state.clip_store,
                frame_context.spatial_tree,
                frame_state.rg_builder,
                &mut frame_state.frame_gpu_data.f32,
                frame_state.transforms,
            );

            let clip_task_index = ClipTaskIndex(clip_mask_instances.len() as _);
            clip_mask_instances.push(ClipMaskKind::Mask(clip_task_id));
            frame_state.surface_builder.add_child_render_task(
                clip_task_id,
                frame_state.rg_builder,
            );

            return Some(clip_task_index);
        }
    }

    None
}

/// Returns the clip-task index produced for this picture, if any. The caller
/// records it on the draw header; this function only holds a shared reference to
/// the header, and `pic_scratch` borrows the frame scratch for its whole body.
pub fn prepare_picture_primitive(
    pic: &PictureInstance,
    raster_config: &RasterConfig,
    draw_index: PrimitiveDrawIndex,
    prim_spatial_node_index: SpatialNodeIndex,
    _clip_chain: &ClipChainInstance,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    scratch: &mut PrimitiveScratchBuffer,
    data_stores: &DataStores,
    pic_context: &PictureContext,
    pic_scratch_handle: StorageIndex<PictureScratch>,
    prim_info: &PrimitiveDrawHeader,
    plane_split_anchor: PlaneSplitAnchor,
    quad_transform: &mut QuadTransformState,
    targets: &[CommandBufferIndex],
) -> Option<ClipTaskIndex> {
    let pic_scratch = &mut scratch.frame.pictures[pic_scratch_handle];

    // Write the composite-mode gpu blocks first: the filter eligibility
    // check below reads the resulting extra_gpu_data.
    raster_config.composite_mode.write_gpu_blocks(
        &mut frame_state.frame_gpu_data,
        data_stores,
        &mut pic_scratch.extra_gpu_data,
    );

    // Decide whether this picture's compositing is migrated to the quad
    // path. This is computed before clip-mask handling so that target
    // masks (those applied while compositing, rather than baked onto the
    // source task) can be routed to the quad path rather than the legacy
    // brush path.
    //
    // Pictures that are part of a 3D context are composited through the
    // plane splitter, so they are left on the legacy path here.
    let use_quads = match raster_config.composite_mode {
        PictureCompositeMode::TileCache { .. } => false,
        PictureCompositeMode::IntermediateSurface => false,
        _ => matches!(pic.context_3d, Picture3DContext::Out),
    };

    // Clip masks are split into "source" masks (baked onto the picture's
    // source task) and "target" masks (applied while compositing). When
    // the picture composites via the quad path, target masks are carried
    // here and applied by that path; otherwise the legacy brush path
    // renders a screen-space alpha mask task.
    let mut composite_target_clip_range: Option<ClipNodeRange> = None;

    let mut clip_task_index = None;

    if prim_info.clip_chain.needs_mask {
        clip_task_index = prepare_picture_clips(
            pic,
            &prim_info.clip_chain,
            frame_context,
            frame_state,
            pic_scratch,
            &mut scratch.frame.clip_mask_instances,
            prim_spatial_node_index,
            data_stores,
            use_quads,
            &mut composite_target_clip_range,
            pic_context,
        );
    }

    if let Picture3DContext::In { root_data: None, plane_splitter_index, ancestor_index, .. } = pic.context_3d {
        let dirty_rect = frame_state.current_dirty_region().combined;
        let visibility_spatial_node = frame_state.current_dirty_region().visibility_spatial_node;

        let splitter = &mut frame_state.plane_splitters[plane_splitter_index.0];
        let surface_index = raster_config.surface_index;
        let surface = &frame_state.surfaces[surface_index.0];
        let local_prim_rect = surface.clipped_local_rect.cast_unit();

        PictureInstance::add_split_plane(
            splitter,
            frame_context.spatial_tree,
            prim_spatial_node_index,
            ancestor_index,
            visibility_spatial_node,
            local_prim_rect,
            &prim_info.clip_chain.local_clip_rect,
            dirty_rect,
            plane_split_anchor,
        );

        // The PrimitiveCommand is pushed by PictureInstance::restore_context.
        return clip_task_index;
    }

    if !use_quads {
        return clip_task_index;
    }

    // Detached snapshot pictures are not composited.
    let detached = pic.snapshot.map_or(false, |s| s.detached);
    if detached {
        return clip_task_index;
    }

    let pic_task_id = pic_scratch
        .primary_render_task_id
        .expect("bug: no render task for composited picture");

    let surface = &frame_state.surfaces[raster_config.surface_index.0];
    let pic_local_rect = raster_config.composite_mode.get_rect(surface, None);
    let surface_spatial_node_index = surface.surface_spatial_node_index;
    let is_same_coord_system = surface_spatial_node_index == surface.raster_spatial_node_index;

    // For a raster root, the baked raster transform must not
    // be applied again at composite time, so use a dedicated
    // local-to-raster scale-offset transform (and the clip
    // rect it implies) rather than the cluster's transform.
    let mut local_transform;
    let (local_clip_rect, transform) = if is_same_coord_system {
        (prim_info.clip_chain.local_clip_rect, quad_transform)
    } else {
        let map_local_to_raster = SpaceMapper::new_with_target(
            pic_context.raster_spatial_node_index,
            surface_spatial_node_index,
            LayoutRect::max_rect(),
            frame_context.spatial_tree,
        );

        let raster_rect = map_local_to_raster.map(&pic_local_rect).unwrap();

        // TODO(nical): This matches what the brush code does in batch.rs but
        // it does not make sense to me.
        let sx = raster_rect.width() / pic_local_rect.width();
        let sy = raster_rect.height() / pic_local_rect.height();
        let tx = raster_rect.min.x - sx * pic_local_rect.min.x;
        let ty = raster_rect.min.y - sy * pic_local_rect.min.y;
        let local_to_raster_so = ScaleOffset::new(sx, sy, tx, ty);

        let local_clip_rect = prim_info.clip_chain.local_clip_rect;
        let raster_clip_rect = map_local_to_raster.map(&local_clip_rect).unwrap();
        let adjusted_clip_rect = local_to_raster_so.unmap_rect(&raster_clip_rect);

        local_transform = QuadTransformState::from_scale_offset(
            local_to_raster_so,
            prim_spatial_node_index,
            pic_context.raster_spatial_node_index,
            quad_transform.device_pixel_scale(),
        );

        (adjusted_clip_rect, &mut local_transform)
    };

    // Source clip masks (if any) were drawn onto the picture's
    // source task above, so the compositing quad must not
    // re-apply them (which would mask twice). Target clip masks
    // are applied here by the quad path via their own clip
    // range.
    let mut composite_clip_chain = prim_info.clip_chain;
    match composite_target_clip_range {
        Some(clips_range) => {
            composite_clip_chain.needs_mask = true;
            composite_clip_chain.clips_range = clips_range;
        }
        None => {
            composite_clip_chain.needs_mask = false;
        }
    }

    let mut opacity = 1.0;
    // (filter_mode, amount-or-gpu-address) for CSS/SVG filters that map
    // to the ps_quad_blend shader.
    let mut filter = None;
    // Software mix-blend mode mapping to the ps_quad_mix_blend shader.
    let mut mix_blend = None;
    // GPU-blend-equation mix-blend mode (Screen/Exclusion/PlusLighter)
    // drawn as a blended image quad.
    let mut hw_blend = None;

    match raster_config.composite_mode {
        PictureCompositeMode::MixBlend(mode) => {
            match BlendMode::from_mix_blend_mode(
                mode,
                frame_context.fb_config.gpu_supports_advanced_blend,
                frame_context.fb_config.advanced_blend_is_coherent,
            ) {
                // No GPU blend equation available: composite via a
                // software readback of the backdrop (ps_quad_mix_blend).
                None => {
                    mix_blend = Some(mode);
                }
                // Advanced blend equation, or a fixed-function blend
                // (Screen / Exclusion / PlusLighter): draw the picture
                // content as a blended image quad.
                Some(bm) => {
                    hw_blend = Some(bm);
                }
            }
        }
        PictureCompositeMode::Filter(Filter::Opacity(_, amount)) => {
            opacity = amount;
        }
        PictureCompositeMode::Filter(ref f) => {
            let extra_gpu_data = pic_scratch
                .extra_gpu_data
                .as_slice();
            filter = blend_filter_param(f, extra_gpu_data);
        }
        PictureCompositeMode::ComponentTransferFilter(handle) => {
            let filter_data = &data_stores.filter_data[handle];
            let filter_mode: i32 = Filter::ComponentTransfer.as_int()
                | ((filter_data.data.r_func.to_int() << 28
                    | filter_data.data.g_func.to_int() << 24
                    | filter_data.data.b_func.to_int() << 20
                    | filter_data.data.a_func.to_int() << 16)
                    as i32);
            let addr = pic_scratch
                .extra_gpu_data[0]
                .as_int();
            filter = Some((filter_mode, addr));
        }
        _ => {}
    };

    let img_pattern;
    let mix_blend_pattern;
    let ff_mix_blend_pattern;
    let filter_pattern;

    let pattern: &dyn PatternBuilder = if let PictureCompositeMode::Filter(Filter::DropShadows(ref shadows)) =
        raster_config.composite_mode
    {
        // Draw each shadow (the blurred source tinted by the
        // shadow color, sampled through its alpha) and then
        // the unblurred content on top.
        for shadow in shadows {
            let shadow_rect = pic_local_rect.translate(shadow.offset);
            let shadow_pattern = ShadowPattern {
                src_task_id: pic_task_id,
                color: shadow.color,
            };
            quad::prepare_quad(
                &shadow_pattern,
                &QuadDescriptor {
                    pattern_rect: shadow_rect,
                    bounds: local_clip_rect.intersection_unchecked(&shadow_rect),
                    aligned_aa_edges: EdgeMask::empty(),
                    transformed_aa_edges: EdgeMask::all(),
                },
                draw_index,
                &None,
                &composite_clip_chain,
                transform,
                frame_context,
                pic_context,
                targets,
                &data_stores.clip,
                frame_state,
                scratch,
            );
        }

        let content_task_id = scratch.frame.pictures[pic_scratch_handle]
            .secondary_render_task_id
            .expect("bug: no content task for drop shadow");
        img_pattern = ImagePattern {
            src_task_id: content_task_id,
            src_is_opaque: false,
            premultiplied: true,
            sampler_kind: ImageBufferKind::Texture2D,
            color: ColorF::WHITE,
        };

        &img_pattern
    } else if let Some(mode) = mix_blend {
        // The backdrop was captured into a readback task during
        // composite-mode setup; blend the picture (source) over it.
        let backdrop_task_id = pic_scratch
            .secondary_render_task_id
            .expect("bug: no backdrop readback task for mix-blend");

        mix_blend_pattern = MixBlendPattern {
            backdrop_task_id,
            src_task_id: pic_task_id,
            mode,
        };

        &mix_blend_pattern
    } else if let Some(blend_mode) = hw_blend {
        ff_mix_blend_pattern = FixedFunctionMixBlendPattern {
            src_task_id: pic_task_id,
            blend_mode,
        };
        &ff_mix_blend_pattern
    } else if let Some((filter_mode, param)) = filter {
        filter_pattern = BlendFilterPattern {
            src_task_id: pic_task_id,
            filter_mode,
            param,
        };
        &filter_pattern
    } else {
        img_pattern = ImagePattern {
            src_task_id: pic_task_id,
            src_is_opaque: false,
            premultiplied: true,
            sampler_kind: ImageBufferKind::Texture2D,
            color: ColorF::new(1.0, 1.0, 1.0, opacity),
        };
        &img_pattern
    };

    quad::prepare_quad(
        pattern,
        &QuadDescriptor {
            pattern_rect: pic_local_rect,
            bounds: local_clip_rect.intersection_unchecked(&pic_local_rect),
            aligned_aa_edges: EdgeMask::empty(),
            transformed_aa_edges: EdgeMask::all(),
        },
        draw_index,
        &None,
        &composite_clip_chain,
        transform,
        frame_context,
        pic_context,
        targets,
        &data_stores.clip,
        frame_state,
        scratch,
    );

    clip_task_index
}

#[test]
fn test_large_surface_scale_1() {
    use crate::spatial_tree::{SceneSpatialTree, SpatialTree};

    let mut cst = SceneSpatialTree::new();
    let root_reference_frame_index = cst.root_reference_frame_index();

    let mut spatial_tree = SpatialTree::new();
    spatial_tree.apply_updates(cst.end_frame_and_get_pending_updates());
    spatial_tree.update_tree(&SceneProperties::new());

    let map_local_to_picture = SpaceMapper::new_with_target(
        root_reference_frame_index,
        root_reference_frame_index,
        PictureRect::max_rect(),
        &spatial_tree,
    );

    let mut surfaces = vec![
        SurfaceInfo {
            unclipped_local_rect: PictureRect::max_rect(),
            clipped_local_rect: PictureRect::max_rect(),
            is_opaque: true,
            clipping_rect: PictureRect::max_rect(),
            culling_rect: VisRect::max_rect(),
            map_local_to_picture: map_local_to_picture.clone(),
            raster_spatial_node_index: root_reference_frame_index,
            surface_spatial_node_index: root_reference_frame_index,
            visibility_spatial_node_index: root_reference_frame_index,
            device_pixel_scale: DevicePixelScale::new(1.0),
            world_scale_factors: (1.0, 1.0),
            local_scale: (1.0, 1.0),
            allow_snapping: true,
            force_scissor_rect: false,
            svgfe_source_map: ScaleOffset::identity(),
        },
        SurfaceInfo {
            unclipped_local_rect: PictureRect::new(
                PicturePoint::new(52.76350021362305, 0.0),
                PicturePoint::new(159.6738739013672, 35.0),
            ),
            clipped_local_rect: PictureRect::max_rect(),
            is_opaque: true,
            clipping_rect: PictureRect::max_rect(),
            culling_rect: VisRect::max_rect(),
            map_local_to_picture,
            raster_spatial_node_index: root_reference_frame_index,
            surface_spatial_node_index: root_reference_frame_index,
            visibility_spatial_node_index: root_reference_frame_index,
            device_pixel_scale: DevicePixelScale::new(43.82798767089844),
            world_scale_factors: (1.0, 1.0),
            local_scale: (1.0, 1.0),
            allow_snapping: true,
            force_scissor_rect: false,
            svgfe_source_map: ScaleOffset::identity(),
        },
    ];

    get_surface_rects(
        SurfaceIndex(1),
        &PictureCompositeMode::Blit(BlitReason::BLEND_MODE),
        SurfaceIndex(0),
        &mut surfaces,
        &spatial_tree,
        MAX_SURFACE_SIZE as f32,
        false,
    );
}

/// Maps a filter to the (filter_mode, parameter) pair consumed by the
/// blend shader.
fn blend_filter_param(filter: &Filter, extra_gpu_data: &[GpuBufferAddress]) -> Option<(i32, i32)> {
    let param = match filter {
        Filter::Contrast(amount)
        | Filter::Grayscale(amount)
        | Filter::Invert(amount)
        | Filter::Saturate(amount)
        | Filter::Sepia(amount)
        | Filter::Brightness(amount)
        => (amount * 65536.0) as i32,
        Filter::HueRotate(angle) => (0.01745329251 * angle * 65536.0) as i32,
        Filter::ColorMatrix(..)
        | Filter::Flood(..)
        => extra_gpu_data[0].as_int(),
        Filter::SrgbToLinear
        | Filter::LinearToSrgb
        => 0,
        // Component transfer is handled separately.
        _ => return None,
    };
    Some((filter.as_int(), param))
}



#[test]
fn test_drop_filter_dirty_region_outside_prim() {
    // Ensure that if we have a drop-filter where the content of the
    // shadow is outside the dirty rect, but blurred pixels from that
    // content will affect the dirty rect, that we correctly calculate
    // the required region of the drop-filter input

    use api::Shadow;
    use crate::spatial_tree::{SceneSpatialTree, SpatialTree};

    let mut cst = SceneSpatialTree::new();
    let root_reference_frame_index = cst.root_reference_frame_index();

    let mut spatial_tree = SpatialTree::new();
    spatial_tree.apply_updates(cst.end_frame_and_get_pending_updates());
    spatial_tree.update_tree(&SceneProperties::new());

    let map_local_to_picture = SpaceMapper::new_with_target(
        root_reference_frame_index,
        root_reference_frame_index,
        PictureRect::max_rect(),
        &spatial_tree,
    );

    let mut surfaces = vec![
        SurfaceInfo {
            unclipped_local_rect: PictureRect::max_rect(),
            clipped_local_rect: PictureRect::max_rect(),
            is_opaque: true,
            clipping_rect: PictureRect::max_rect(),
            map_local_to_picture: map_local_to_picture.clone(),
            raster_spatial_node_index: root_reference_frame_index,
            surface_spatial_node_index: root_reference_frame_index,
            visibility_spatial_node_index: root_reference_frame_index,
            device_pixel_scale: DevicePixelScale::new(1.0),
            world_scale_factors: (1.0, 1.0),
            local_scale: (1.0, 1.0),
            allow_snapping: true,
            force_scissor_rect: false,
            svgfe_source_map: ScaleOffset::identity(),
            culling_rect: VisRect::max_rect(),
        },
        SurfaceInfo {
            unclipped_local_rect: PictureRect::new(
                PicturePoint::new(0.0, 0.0),
                PicturePoint::new(750.0, 450.0),
            ),
            clipped_local_rect: PictureRect::new(
                PicturePoint::new(0.0, 0.0),
                PicturePoint::new(750.0, 450.0),
            ),
            is_opaque: true,
            clipping_rect: PictureRect::max_rect(),
            map_local_to_picture,
            raster_spatial_node_index: root_reference_frame_index,
            surface_spatial_node_index: root_reference_frame_index,
            visibility_spatial_node_index: root_reference_frame_index,
            device_pixel_scale: DevicePixelScale::new(1.0),
            world_scale_factors: (1.0, 1.0),
            local_scale: (1.0, 1.0),
            allow_snapping: true,
            force_scissor_rect: false,
            svgfe_source_map: ScaleOffset::identity(),
            culling_rect: VisRect::max_rect(),
        },
    ];

    let shadows = smallvec![
        Shadow {
            offset: LayoutVector2D::zero(),
            color: ColorF::BLACK,
            blur_radius: 75.0,
        },
    ];

    let composite_mode = PictureCompositeMode::Filter(Filter::DropShadows(shadows));

    // Ensure we get a valid and correct render task size when dirty region covers entire screen
    let info = get_surface_rects(
        SurfaceIndex(1),
        &composite_mode,
        SurfaceIndex(0),
        &mut surfaces,
        &spatial_tree,
        MAX_SURFACE_SIZE as f32,
        false,
    ).expect("No surface rect");
    assert_eq!(info.task_size, DeviceIntSize::new(1200, 900));

    // Ensure we get a valid and correct render task size when dirty region is outside filter content
    surfaces[0].clipping_rect = PictureRect::new(
        PicturePoint::new(768.0, 128.0),
        PicturePoint::new(1024.0, 256.0),
    );
    let info = get_surface_rects(
        SurfaceIndex(1),
        &composite_mode,
        SurfaceIndex(0),
        &mut surfaces,
        &spatial_tree,
        MAX_SURFACE_SIZE as f32,
        false,
    ).expect("No surface rect");
    assert_eq!(info.task_size, DeviceIntSize::new(432, 578));
}

#[test]
fn test_drop_filter_partial_dirty_content_inflate() {
    // Bug 1822189: When the parent's dirty region (clipping_rect here) overlaps
    // the drop-shadow's image content but stops short of the picture's full
    // unclipped extent, the source-texture allocation must include enough blur
    // margin around the image content to keep the picture_task texture's edges
    // in transparent space. Otherwise the content quad's blur margin samples
    // UVs > 1 and the texture-edge image content bleeds into the visible
    // result.

    use api::Shadow;
    use crate::spatial_tree::{SceneSpatialTree, SpatialTree};

    let mut cst = SceneSpatialTree::new();
    let root_reference_frame_index = cst.root_reference_frame_index();

    let mut spatial_tree = SpatialTree::new();
    spatial_tree.apply_updates(cst.end_frame_and_get_pending_updates());
    spatial_tree.update_tree(&SceneProperties::new());

    let map_local_to_picture = SpaceMapper::new_with_target(
        root_reference_frame_index,
        root_reference_frame_index,
        PictureRect::max_rect(),
        &spatial_tree,
    );

    // 500x500 image content, drop-shadow with non-zero offset.
    let mut surfaces = vec![
        SurfaceInfo {
            unclipped_local_rect: PictureRect::max_rect(),
            clipped_local_rect: PictureRect::max_rect(),
            is_opaque: true,
            // Parent's clipping_rect = dirty region that partially overlaps
            // the image but stops short of the full picture extent. This is
            // the scenario where the bug used to leave the texture's right
            // and bottom edges on image content.
            clipping_rect: PictureRect::new(
                PicturePoint::new(0.0, 0.0),
                PicturePoint::new(683.0, 341.0),
            ),
            map_local_to_picture: map_local_to_picture.clone(),
            raster_spatial_node_index: root_reference_frame_index,
            surface_spatial_node_index: root_reference_frame_index,
            visibility_spatial_node_index: root_reference_frame_index,
            device_pixel_scale: DevicePixelScale::new(1.0),
            world_scale_factors: (1.0, 1.0),
            local_scale: (1.0, 1.0),
            allow_snapping: true,
            force_scissor_rect: false,
            svgfe_source_map: ScaleOffset::identity(),
            culling_rect: VisRect::max_rect(),
        },
        SurfaceInfo {
            unclipped_local_rect: PictureRect::new(
                PicturePoint::new(0.0, 0.0),
                PicturePoint::new(500.0, 500.0),
            ),
            clipped_local_rect: PictureRect::new(
                PicturePoint::new(0.0, 0.0),
                PicturePoint::new(500.0, 500.0),
            ),
            is_opaque: true,
            clipping_rect: PictureRect::max_rect(),
            map_local_to_picture,
            raster_spatial_node_index: root_reference_frame_index,
            surface_spatial_node_index: root_reference_frame_index,
            visibility_spatial_node_index: root_reference_frame_index,
            device_pixel_scale: DevicePixelScale::new(1.0),
            world_scale_factors: (1.0, 1.0),
            local_scale: (1.0, 1.0),
            allow_snapping: true,
            force_scissor_rect: false,
            svgfe_source_map: ScaleOffset::identity(),
            culling_rect: VisRect::max_rect(),
        },
    ];

    let shadows = smallvec![
        Shadow {
            offset: LayoutVector2D::new(400.0, 100.0),
            color: ColorF::BLACK,
            blur_radius: 20.0,
        },
    ];

    let composite_mode = PictureCompositeMode::Filter(Filter::DropShadows(shadows));

    let info = get_surface_rects(
        SurfaceIndex(1),
        &composite_mode,
        SurfaceIndex(0),
        &mut surfaces,
        &spatial_tree,
        MAX_SURFACE_SIZE as f32,
        false,
    ).expect("No surface rect");

    // With the fix, the image-content side of required_local_rect is inflated
    // by blur (20 * BLUR_SAMPLE_SCALE = 60) so the texture extends to
    // (-60, -60)..(560, 401) — placing the right and bottom edges in the
    // transparent blur margin around the image rather than on image content.
    // Width=620, height=461. Without the fix this would be (560, 401) i.e.
    // 560x401 with the texture's right and bottom edges sitting on the
    // 500x500 image content, producing the visible bleed.
    assert_eq!(info.task_size, DeviceIntSize::new(620, 461));
}
