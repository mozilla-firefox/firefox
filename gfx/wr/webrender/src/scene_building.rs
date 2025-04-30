/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! # Scene building
//!
//! Scene building is the phase during which display lists, a representation built for
//! serialization, are turned into a scene, webrender's internal representation that is
//! suited for rendering frames.
//!
//! This phase is happening asynchronously on the scene builder thread.
//!
//! # General algorithm
//!
//! The important aspects of scene building are:
//! - Building up primitive lists (much of the cost of scene building goes here).
//! - Creating pictures for content that needs to be rendered into a surface, be it so that
//!   filters can be applied or for caching purposes.
//! - Maintaining a temporary stack of stacking contexts to keep track of some of the
//!   drawing states.
//! - Stitching multiple display lists which reference each other (without cycles) into
//!   a single scene (see build_reference_frame).
//! - Interning, which detects when some of the retained state stays the same between display
//!   lists.
//!
//! The scene builder linearly traverses the serialized display list which is naturally
//! ordered back-to-front, accumulating primitives in the top-most stacking context's
//! primitive list.
//! At the end of each stacking context (see pop_stacking_context), its primitive list is
//! either handed over to a picture if one is created, or it is concatenated into the parent
//! stacking context's primitive list.
//!
//! The flow of the algorithm is mostly linear except when handling:
//!  - shadow stacks (see push_shadow and pop_all_shadows),
//!  - backdrop filters (see add_backdrop_filter)
//!

use api::{AlphaType, BorderDetails, BorderDisplayItem, BuiltDisplayList, BuiltDisplayListIter, PrimitiveFlags, SnapshotInfo};
use api::{ClipId, ColorF, CommonItemProperties, ComplexClipRegion, ComponentTransferFuncType, RasterSpace};
use api::{DebugFlags, DisplayItem, DisplayItemRef, ExtendMode, ExternalScrollId, FilterData};
use api::{FilterOp, FilterPrimitive, FontInstanceKey, FontSize, GlyphInstance, GlyphOptions, GradientStop};
use api::{IframeDisplayItem, ImageKey, ImageRendering, ItemRange, ColorDepth, QualitySettings};
use api::{LineOrientation, LineStyle, NinePatchBorderSource, PipelineId, MixBlendMode, StackingContextFlags};
use api::{PropertyBinding, ReferenceFrameKind, ScrollFrameDescriptor};
use api::{APZScrollGeneration, HasScrollLinkedEffect, Shadow, SpatialId, StickyFrameDescriptor, ImageMask, ItemTag};
use api::{ClipMode, PrimitiveKeyKind, TransformStyle, YuvColorSpace, ColorRange, YuvData, TempFilterData};
use api::{ReferenceTransformBinding, Rotation, FillRule, SpatialTreeItem, ReferenceFrameDescriptor};
use api::{FilterOpGraphPictureBufferId, SVGFE_GRAPH_MAX};
use api::channel::{unbounded_channel, Receiver, Sender};
use api::units::*;
use crate::image_tiling::simplify_repeated_primitive;
use crate::box_shadow::BLUR_SAMPLE_SCALE;
use crate::clip::{ClipIntern, ClipItemKey, ClipItemKeyKind, ClipStore};
use crate::clip::{ClipInternData, ClipNodeId, ClipLeafId};
use crate::clip::{PolygonDataHandle, ClipTreeBuilder};
use crate::segment::EdgeAaSegmentMask;
use crate::spatial_tree::{SceneSpatialTree, SpatialNodeContainer, SpatialNodeIndex, get_external_scroll_offset};
use crate::frame_builder::FrameBuilderConfig;
use glyph_rasterizer::{FontInstance, SharedFontResources};
use crate::hit_test::HitTestingScene;
use crate::intern::Interner;
use crate::internal_types::{FastHashMap, LayoutPrimitiveInfo, Filter, FilterGraphNode, FilterGraphOp, FilterGraphPictureReference, PlaneSplitterIndex, PipelineInstanceId};
use crate::picture::{Picture3DContext, PictureCompositeMode, PicturePrimitive};
use crate::picture::{BlitReason, OrderedPictureChild, PrimitiveList, SurfaceInfo, PictureFlags};
use crate::picture_graph::PictureGraph;
use crate::prim_store::{PrimitiveInstance, PrimitiveStoreStats};
use crate::prim_store::{PrimitiveInstanceKind, NinePatchDescriptor, PrimitiveStore};
use crate::prim_store::{InternablePrimitive, PictureIndex};
use crate::prim_store::PolygonKey;
use crate::prim_store::backdrop::{BackdropCapture, BackdropRender};
use crate::prim_store::borders::{ImageBorder, NormalBorderPrim};
use crate::prim_store::gradient::{
    GradientStopKey, LinearGradient, RadialGradient, RadialGradientParams, ConicGradient,
    ConicGradientParams, optimize_radial_gradient, apply_gradient_local_clip,
    optimize_linear_gradient, self,
};
use crate::prim_store::image::{Image, YuvImage};
use crate::prim_store::line_dec::{LineDecoration, LineDecorationCacheKey, get_line_decoration_size};
use crate::prim_store::picture::{Picture, PictureCompositeKey, PictureKey};
use crate::prim_store::text_run::TextRun;
use crate::render_backend::SceneView;
use crate::resource_cache::ImageRequest;
use crate::scene::{BuiltScene, Scene, ScenePipeline, SceneStats, StackingContextHelpers};
use crate::scene_builder_thread::Interners;
use crate::space::SpaceSnapper;
use crate::spatial_node::{
    ReferenceFrameInfo, StickyFrameInfo, ScrollFrameKind, SpatialNodeUid, SpatialNodeType
};
use crate::tile_cache::TileCacheBuilder;
use euclid::approxeq::ApproxEq;
use std::{f32, mem, usize};
use std::collections::vec_deque::VecDeque;
use std::sync::Arc;
use crate::util::{VecHelper, MaxRect};
use crate::filterdata::{SFilterDataComponent, SFilterData, SFilterDataKey};
use log::Level;

/// Offsets primitives (and clips) by the external scroll offset
/// supplied to scroll nodes.
pub struct ScrollOffsetMapper {
    pub current_spatial_node: SpatialNodeIndex,
    pub current_offset: LayoutVector2D,
}

impl ScrollOffsetMapper {
    fn new() -> Self {
        ScrollOffsetMapper {
            current_spatial_node: SpatialNodeIndex::INVALID,
            current_offset: LayoutVector2D::zero(),
        }
    }

    /// Return the accumulated external scroll offset for a spatial
    /// node. This caches the last result, which is the common case,
    /// or defers to the spatial tree to build the value.
    fn external_scroll_offset(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        spatial_tree: &SceneSpatialTree,
    ) -> LayoutVector2D {
        if spatial_node_index != self.current_spatial_node {
            self.current_spatial_node = spatial_node_index;
            self.current_offset = get_external_scroll_offset(spatial_tree, spatial_node_index);
        }

        self.current_offset
    }
}

/// A data structure that keeps track of mapping between API Ids for spatials and the indices
/// used internally in the SpatialTree to avoid having to do HashMap lookups for primitives
/// and clips during frame building.
#[derive(Default)]
pub struct NodeIdToIndexMapper {
    spatial_node_map: FastHashMap<SpatialId, SpatialNodeIndex>,
}

impl NodeIdToIndexMapper {
    fn add_spatial_node(&mut self, id: SpatialId, index: SpatialNodeIndex) {
        let _old_value = self.spatial_node_map.insert(id, index);
        assert!(_old_value.is_none());
    }

    fn get_spatial_node_index(&self, id: SpatialId) -> SpatialNodeIndex {
        self.spatial_node_map[&id]
    }
}

#[derive(Debug, Clone, Default)]
pub struct CompositeOps {
    // Requires only a single texture as input (e.g. most filters)
    pub filters: Vec<Filter>,
    pub filter_datas: Vec<FilterData>,
    pub filter_primitives: Vec<FilterPrimitive>,
    pub snapshot: Option<SnapshotInfo>,

    // Requires two source textures (e.g. mix-blend-mode)
    pub mix_blend_mode: Option<MixBlendMode>,
}

impl CompositeOps {
    pub fn new(
        filters: Vec<Filter>,
        filter_datas: Vec<FilterData>,
        filter_primitives: Vec<FilterPrimitive>,
        mix_blend_mode: Option<MixBlendMode>,
        snapshot: Option<SnapshotInfo>,
    ) -> Self {
        CompositeOps {
            filters,
            filter_datas,
            filter_primitives,
            mix_blend_mode,
            snapshot,
        }
    }

    pub fn is_empty(&self) -> bool {
        self.filters.is_empty() &&
            self.filter_primitives.is_empty() &&
            self.mix_blend_mode.is_none() &&
            self.snapshot.is_none()
    }

    /// Returns true if this CompositeOps contains any filters that affect
    /// the content (false if no filters, or filters are all no-ops).
    fn has_valid_filters(&self) -> bool {
        // For each filter, create a new image with that composite mode.
        let mut current_filter_data_index = 0;
        for filter in &self.filters {
            match filter {
                Filter::ComponentTransfer => {
                    let filter_data =
                        &self.filter_datas[current_filter_data_index];
                    let filter_data = filter_data.sanitize();
                    current_filter_data_index = current_filter_data_index + 1;
                    if filter_data.is_identity() {
                        continue
                    } else {
                        return true;
                    }
                }
                Filter::SVGGraphNode(..) => {return true;}
                _ => {
                    if filter.is_noop() {
                        continue;
                    } else {
                        return true;
                    }
                }
            }
        }

        if !self.filter_primitives.is_empty() {
            return true;
        }

        false
    }
}

/// Represents the current input for a picture chain builder (either a
/// prim list from the stacking context, or a wrapped picture instance).
enum PictureSource {
    PrimitiveList {
        prim_list: PrimitiveList,
    },
    WrappedPicture {
        instance: PrimitiveInstance,
    },
}

/// Helper struct to build picture chains during scene building from
/// a flattened stacking context struct.
struct PictureChainBuilder {
    /// The current input source for the next picture
    current: PictureSource,

    /// Positioning node for this picture chain
    spatial_node_index: SpatialNodeIndex,
    /// Prim flags for any pictures in this chain
    flags: PrimitiveFlags,
    /// Requested raster space for enclosing stacking context
    raster_space: RasterSpace,
    /// If true, set first picture as a resolve target
    set_resolve_target: bool,
    /// If true, mark the last picture as a sub-graph
    establishes_sub_graph: bool,
}

impl PictureChainBuilder {
    /// Create a new picture chain builder, from a primitive list
    fn from_prim_list(
        prim_list: PrimitiveList,
        flags: PrimitiveFlags,
        spatial_node_index: SpatialNodeIndex,
        raster_space: RasterSpace,
        is_sub_graph: bool,
    ) -> Self {
        PictureChainBuilder {
            current: PictureSource::PrimitiveList {
                prim_list,
            },
            spatial_node_index,
            flags,
            raster_space,
            establishes_sub_graph: is_sub_graph,
            set_resolve_target: is_sub_graph,
        }
    }

    /// Create a new picture chain builder, from a picture wrapper instance
    fn from_instance(
        instance: PrimitiveInstance,
        flags: PrimitiveFlags,
        spatial_node_index: SpatialNodeIndex,
        raster_space: RasterSpace,
    ) -> Self {
        PictureChainBuilder {
            current: PictureSource::WrappedPicture {
                instance,
            },
            flags,
            spatial_node_index,
            raster_space,
            establishes_sub_graph: false,
            set_resolve_target: false,
        }
    }

    /// Wrap the existing content with a new picture with the given parameters
    #[must_use]
    fn add_picture(
        self,
        composite_mode: PictureCompositeMode,
        clip_node_id: ClipNodeId,
        context_3d: Picture3DContext<OrderedPictureChild>,
        interners: &mut Interners,
        prim_store: &mut PrimitiveStore,
        prim_instances: &mut Vec<PrimitiveInstance>,
        clip_tree_builder: &mut ClipTreeBuilder,
    ) -> PictureChainBuilder {
        let prim_list = match self.current {
            PictureSource::PrimitiveList { prim_list } => {
                prim_list
            }
            PictureSource::WrappedPicture { instance } => {
                let mut prim_list = PrimitiveList::empty();

                prim_list.add_prim(
                    instance,
                    LayoutRect::zero(),
                    self.spatial_node_index,
                    self.flags,
                    prim_instances,
                    clip_tree_builder,
                );

                prim_list
            }
        };

        let flags = if self.set_resolve_target {
            PictureFlags::IS_RESOLVE_TARGET
        } else {
            PictureFlags::empty()
        };

        let pic_index = PictureIndex(prim_store.pictures
            .alloc()
            .init(PicturePrimitive::new_image(
                Some(composite_mode.clone()),
                context_3d,
                self.flags,
                prim_list,
                self.spatial_node_index,
                self.raster_space,
                flags,
                None,
            ))
        );

        let instance = create_prim_instance(
            pic_index,
            Some(composite_mode).into(),
            self.raster_space,
            clip_node_id,
            interners,
            clip_tree_builder,
        );

        PictureChainBuilder {
            current: PictureSource::WrappedPicture {
                instance,
            },
            spatial_node_index: self.spatial_node_index,
            flags: self.flags,
            raster_space: self.raster_space,
            // We are now on a subsequent picture, so set_resolve_target has been handled
            set_resolve_target: false,
            establishes_sub_graph: self.establishes_sub_graph,
        }
    }

    /// Finish building this picture chain. Set the clip chain on the outermost picture
    fn finalize(
        self,
        clip_node_id: ClipNodeId,
        interners: &mut Interners,
        prim_store: &mut PrimitiveStore,
        clip_tree_builder: &mut ClipTreeBuilder,
        snapshot: Option<SnapshotInfo>,
    ) -> PrimitiveInstance {
        let mut flags = PictureFlags::empty();
        if self.establishes_sub_graph {
            flags |= PictureFlags::IS_SUB_GRAPH;
        }

        match self.current {
            PictureSource::WrappedPicture { instance } => {
                let pic_index = instance.kind.as_pic();
                let picture = &mut prim_store.pictures[pic_index.0];
                picture.flags |= flags;
                picture.snapshot = snapshot;

                instance
            }
            PictureSource::PrimitiveList { prim_list } => {
                if self.set_resolve_target {
                    flags |= PictureFlags::IS_RESOLVE_TARGET;
                }

                // If no picture was created for this stacking context, create a
                // pass-through wrapper now. This is only needed in 1-2 edge cases
                // now, and will be removed as a follow up.

                // If the picture is snapshotted, it needs to have a surface rather
                // than being pass-through.
                let composite_mode = snapshot.map(|_| PictureCompositeMode::Blit(BlitReason::SNAPSHOT));

                let pic_index = PictureIndex(prim_store.pictures
                    .alloc()
                    .init(PicturePrimitive::new_image(
                        composite_mode,
                        Picture3DContext::Out,
                        self.flags,
                        prim_list,
                        self.spatial_node_index,
                        self.raster_space,
                        flags,
                        snapshot,
                    ))
                );

                create_prim_instance(
                    pic_index,
                    None.into(),
                    self.raster_space,
                    clip_node_id,
                    interners,
                    clip_tree_builder,
                )
            }
        }
    }

    /// Returns true if this builder wraps a picture
    #[allow(dead_code)]
    fn has_picture(&self) -> bool {
        match self.current {
            PictureSource::WrappedPicture { .. } => true,
            PictureSource::PrimitiveList { .. } => false,
        }
    }
}

bitflags! {
    /// Slice flags
    #[derive(Debug, Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash)]
    pub struct SliceFlags : u8 {
        /// Slice created by a prim that has PrimitiveFlags::IS_SCROLLBAR_CONTAINER
        const IS_SCROLLBAR = 1;
        /// Represents an atomic container (can't split out compositor surfaces in this slice)
        const IS_ATOMIC = 2;
    }
}

/// A structure that converts a serialized display list into a form that WebRender
/// can use to later build a frame. This structure produces a BuiltScene. Public
/// members are typically those that are destructured into the BuiltScene.
pub struct SceneBuilder<'a> {
    /// The scene that we are currently building.
    scene: &'a Scene,

    /// The map of all font instances.
    fonts: SharedFontResources,

    /// The data structure that converts between ClipId/SpatialId and the various
    /// index types that the SpatialTree uses.
    id_to_index_mapper_stack: Vec<NodeIdToIndexMapper>,

    /// A stack of stacking context properties.
    sc_stack: Vec<FlattenedStackingContext>,

    /// Stack of spatial node indices forming containing block for 3d contexts
    containing_block_stack: Vec<SpatialNodeIndex>,

    /// Stack of requested raster spaces for stacking contexts
    raster_space_stack: Vec<RasterSpace>,

    /// Maintains state for any currently active shadows
    pending_shadow_items: VecDeque<ShadowItem>,

    /// The SpatialTree that we are currently building during building.
    pub spatial_tree: &'a mut SceneSpatialTree,

    /// The store of primitives.
    pub prim_store: PrimitiveStore,

    /// Information about all primitives involved in hit testing.
    pub hit_testing_scene: HitTestingScene,

    /// The store which holds all complex clipping information.
    pub clip_store: ClipStore,

    /// The configuration to use for the FrameBuilder. We consult this in
    /// order to determine the default font.
    pub config: FrameBuilderConfig,

    /// Reference to the set of data that is interned across display lists.
    pub interners: &'a mut Interners,

    /// Helper struct to map spatial nodes to external scroll offsets.
    external_scroll_mapper: ScrollOffsetMapper,

    /// The current recursion depth of iframes encountered. Used to restrict picture
    /// caching slices to only the top-level content frame.
    iframe_size: Vec<LayoutSize>,

    /// Clip-chain for root iframes applied to any tile caches created within this iframe
    root_iframe_clip: Option<ClipId>,

    /// The current quality / performance settings for this scene.
    quality_settings: QualitySettings,

    /// Maintains state about the list of tile caches being built for this scene.
    tile_cache_builder: TileCacheBuilder,

    /// A helper struct to snap local rects in device space. During frame
    /// building we may establish new raster roots, however typically that is in
    /// cases where we won't be applying snapping (e.g. has perspective), or in
    /// edge cases (e.g. SVG filter) where we can accept slightly incorrect
    /// behaviour in favour of getting the common case right.
    snap_to_device: SpaceSnapper,

    /// A DAG that represents dependencies between picture primitives. This builds
    /// a set of passes to run various picture processing passes in during frame
    /// building, in a way that pictures are processed before (or after) their
    /// dependencies, without relying on recursion for those passes.
    picture_graph: PictureGraph,

    /// Keep track of snapshot pictures to ensure that they are rendered even if they
    /// are off-screen and the visibility traversal does not reach them.
    snapshot_pictures: Vec<PictureIndex>,

    /// Keep track of allocated plane splitters for this scene. A plane
    /// splitter is allocated whenever we encounter a new 3d rendering context.
    /// They are stored outside the picture since it makes it easier for them
    /// to be referenced by both the owning 3d rendering context and the child
    /// pictures that contribute to the splitter.
    /// During scene building "allocating" a splitter is just incrementing an index.
    /// Splitter objects themselves are allocated and recycled in the frame builder.
    next_plane_splitter_index: usize,

    /// A list of all primitive instances in the scene. We store them as a single
    /// array so that multiple different systems (e.g. tile-cache, visibility, property
    /// animation bindings) can store index buffers to prim instances.
    prim_instances: Vec<PrimitiveInstance>,

    /// A map of pipeline ids encountered during scene build - used to create unique
    /// pipeline instance ids as they are encountered.
    pipeline_instance_ids: FastHashMap<PipelineId, u32>,

    /// A list of surfaces (backing textures) that are relevant for this scene.
    /// Every picture is assigned to a surface (either a new surface if the picture
    /// has a composite mode, or the parent surface if it's a pass-through).
    surfaces: Vec<SurfaceInfo>,

    /// Used to build a ClipTree from the clip-chains, clips and state during scene building.
    clip_tree_builder: ClipTreeBuilder,

    /// Some primitives need to nest two stacking contexts instead of one
    /// (see push_stacking_context). We keep track of the extra stacking context info
    /// here and set a boolean on the inner stacking context info to remember to
    /// pop from this stack (see StackingContextInfo::needs_extra_stacking_context)
    extra_stacking_context_stack: Vec<StackingContextInfo>,
}

impl<'a> SceneBuilder<'a> {
    pub fn build(
        scene: &Scene,
        fonts: SharedFontResources,
        view: &SceneView,
        frame_builder_config: &FrameBuilderConfig,
        interners: &mut Interners,
        spatial_tree: &mut SceneSpatialTree,
        recycler: &mut SceneRecycler,
        stats: &SceneStats,
        debug_flags: DebugFlags,
    ) -> BuiltScene {
        profile_scope!("build_scene");

        // We checked that the root pipeline is available on the render backend.
        let root_pipeline_id = scene.root_pipeline_id.unwrap();
        let root_pipeline = scene.pipelines.get(&root_pipeline_id).unwrap();
        let root_reference_frame_index = spatial_tree.root_reference_frame_index();

        // During scene building, we assume a 1:1 picture -> raster pixel scale
        let snap_to_device = SpaceSnapper::new(
            root_reference_frame_index,
            RasterPixelScale::new(1.0),
        );

        let mut builder = SceneBuilder {
            scene,
            spatial_tree,
            fonts,
            config: *frame_builder_config,
            id_to_index_mapper_stack: mem::take(&mut recycler.id_to_index_mapper_stack),
            hit_testing_scene: recycler.hit_testing_scene.take().unwrap_or_else(|| HitTestingScene::new(&stats.hit_test_stats)),
            pending_shadow_items: mem::take(&mut recycler.pending_shadow_items),
            sc_stack: mem::take(&mut recycler.sc_stack),
            containing_block_stack: mem::take(&mut recycler.containing_block_stack),
            raster_space_stack: mem::take(&mut recycler.raster_space_stack),
            prim_store: mem::take(&mut recycler.prim_store),
            clip_store: mem::take(&mut recycler.clip_store),
            interners,
            external_scroll_mapper: ScrollOffsetMapper::new(),
            iframe_size: mem::take(&mut recycler.iframe_size),
            root_iframe_clip: None,
            quality_settings: view.quality_settings,
            tile_cache_builder: TileCacheBuilder::new(
                root_reference_frame_index,
                frame_builder_config.background_color,
                debug_flags,
            ),
            snap_to_device,
            picture_graph: mem::take(&mut recycler.picture_graph),
            // This vector is empty most of the time, don't bother with recycling it for now.
            snapshot_pictures: Vec::new(),
            next_plane_splitter_index: 0,
            prim_instances: mem::take(&mut recycler.prim_instances),
            pipeline_instance_ids: FastHashMap::default(),
            surfaces: mem::take(&mut recycler.surfaces),
            clip_tree_builder: recycler.clip_tree_builder.take().unwrap_or_else(|| ClipTreeBuilder::new()),
            extra_stacking_context_stack: Vec::new(),
        };

        // Reset
        builder.hit_testing_scene.reset();
        builder.prim_store.reset();
        builder.clip_store.reset();
        builder.picture_graph.reset();
        builder.prim_instances.clear();
        builder.surfaces.clear();
        builder.sc_stack.clear();
        builder.containing_block_stack.clear();
        builder.id_to_index_mapper_stack.clear();
        builder.pending_shadow_items.clear();
        builder.iframe_size.clear();

        builder.raster_space_stack.clear();
        builder.raster_space_stack.push(RasterSpace::Screen);

        builder.clip_tree_builder.begin();

        builder.build_all(
            root_pipeline_id,
            &root_pipeline,
        );

        // Construct the picture cache primitive instance(s) from the tile cache builder
        let (tile_cache_config, tile_cache_pictures) = builder.tile_cache_builder.build(
            &builder.config,
            &mut builder.prim_store,
            &builder.spatial_tree,
            &builder.prim_instances,
            &mut builder.clip_tree_builder,
            &builder.interners,
        );

        for pic_index in &builder.snapshot_pictures {
            builder.picture_graph.add_root(*pic_index);
        }

        // Add all the tile cache pictures as roots of the picture graph
        for pic_index in &tile_cache_pictures {
            builder.picture_graph.add_root(*pic_index);
            SceneBuilder::finalize_picture(
                *pic_index,
                None,
                &mut builder.prim_store.pictures,
                None,
                &builder.clip_tree_builder,
                &builder.prim_instances,
                &builder.interners.clip,
            );
        }

        let clip_tree = builder.clip_tree_builder.finalize();

        recycler.clip_tree_builder = Some(builder.clip_tree_builder);
        recycler.sc_stack = builder.sc_stack;
        recycler.id_to_index_mapper_stack = builder.id_to_index_mapper_stack;
        recycler.containing_block_stack = builder.containing_block_stack;
        recycler.raster_space_stack = builder.raster_space_stack;
        recycler.pending_shadow_items = builder.pending_shadow_items;
        recycler.iframe_size = builder.iframe_size;

        BuiltScene {
            has_root_pipeline: scene.has_root_pipeline(),
            pipeline_epochs: scene.pipeline_epochs.clone(),
            output_rect: view.device_rect.size().into(),
            hit_testing_scene: Arc::new(builder.hit_testing_scene),
            prim_store: builder.prim_store,
            clip_store: builder.clip_store,
            config: builder.config,
            tile_cache_config,
            snapshot_pictures: builder.snapshot_pictures,
            tile_cache_pictures,
            picture_graph: builder.picture_graph,
            num_plane_splitters: builder.next_plane_splitter_index,
            prim_instances: builder.prim_instances,
            surfaces: builder.surfaces,
            clip_tree,
            recycler_tx: Some(recycler.tx.clone()),
        }
    }

    /// Traverse the picture prim list and update any late-set spatial nodes.
    /// Also, for each picture primitive, store the lowest-common-ancestor
    /// of all of the contained primitives' clips.
    // TODO(gw): This is somewhat hacky - it's unfortunate we need to do this, but it's
    //           because we can't determine the scroll root until we have checked all the
    //           primitives in the slice. Perhaps we could simplify this by doing some
    //           work earlier in the DL builder, so we know what scroll root will be picked?
    fn finalize_picture(
        pic_index: PictureIndex,
        prim_index: Option<usize>,
        pictures: &mut [PicturePrimitive],
        parent_spatial_node_index: Option<SpatialNodeIndex>,
        clip_tree_builder: &ClipTreeBuilder,
        prim_instances: &[PrimitiveInstance],
        clip_interner: &Interner<ClipIntern>,
    ) {
        // Extract the prim_list (borrow check) and select the spatial node to
        // assign to unknown clusters
        let (mut prim_list, spatial_node_index) = {
            let pic = &mut pictures[pic_index.0];
            assert_ne!(pic.spatial_node_index, SpatialNodeIndex::UNKNOWN);

            if pic.flags.contains(PictureFlags::IS_RESOLVE_TARGET) {
                pic.flags |= PictureFlags::DISABLE_SNAPPING;
            }

            // If we're a surface, use that spatial node, otherwise the parent
            let spatial_node_index = match pic.composite_mode {
                Some(_) => pic.spatial_node_index,
                None => parent_spatial_node_index.expect("bug: no parent"),
            };

            (
                mem::replace(&mut pic.prim_list, PrimitiveList::empty()),
                spatial_node_index,
            )
        };

        // Update the spatial node of any unknown clusters
        for cluster in &mut prim_list.clusters {
            if cluster.spatial_node_index == SpatialNodeIndex::UNKNOWN {
                cluster.spatial_node_index = spatial_node_index;
            }
        }

        // Work out the lowest common clip which is shared by all the
        // primitives in this picture.  If it is the same as the picture clip
        // then store it as the clip tree root for the picture so that it is
        // applied later as part of picture compositing.  Gecko gives every
        // primitive a viewport clip which, if applied within the picture,
        // will mess up tile caching and mean we have to redraw on every
        // scroll event (for tile caching to work usefully we specifically
        // want to draw things even if they are outside the viewport).
        let mut shared_clip_node_id = None;

        // Snapshot picture are special. All clips belonging to parents
        // *must* be extracted from the snapshot, so we rely on this optimization
        // taking out parent clips and it overrides other conditions.
        // In addition we need to ensure that only parent clips are extracted.
        let is_snapshot = pictures[pic_index.0].snapshot.is_some();

        if is_snapshot {
            // In the general case, if all of the children of a picture share the
            // same clips, then these clips are hoisted up in the parent picture,
            // however we rely on child clips of snapshotted pictures to be baked
            // into the snapshot.
            // Snapshotted pictures use the parent of their clip node (if any)
            // as the clip root, to ensure that the parent clip hierarchy is
            // extracted from clip chains inside the snapshot, and to make sure
            // that child clips of the snapshots are not hoisted out of the
            // snapshot even when all children of the snapshotted picture share
            // a clip.
            if let Some(idx) = prim_index {
                let clip_node = clip_tree_builder.get_leaf(prim_instances[idx].clip_leaf_id).node_id;
                shared_clip_node_id = clip_tree_builder.get_parent(clip_node);
            }
        } else {
            for cluster in &prim_list.clusters {
                for prim_instance in &prim_instances[cluster.prim_range()] {
                    let leaf = clip_tree_builder.get_leaf(prim_instance.clip_leaf_id);

                    shared_clip_node_id = match shared_clip_node_id {
                        Some(current) => {
                            Some(clip_tree_builder.find_lowest_common_ancestor(
                                current,
                                leaf.node_id,
                            ))
                        }
                        None => Some(leaf.node_id)
                    };
                }
            }
        }

        let lca_tree_node = shared_clip_node_id
            .and_then(|node_id| (node_id != ClipNodeId::NONE).then_some(node_id))
            .map(|node_id| clip_tree_builder.get_node(node_id));
        let lca_node = lca_tree_node
            .map(|tree_node| &clip_interner[tree_node.handle]);
        let pic_node_id = prim_index
            .map(|prim_index| clip_tree_builder.get_leaf(prim_instances[prim_index].clip_leaf_id).node_id)
            .and_then(|node_id| (node_id != ClipNodeId::NONE).then_some(node_id));
        let pic_node = pic_node_id
            .map(|node_id| clip_tree_builder.get_node(node_id))
            .map(|tree_node| &clip_interner[tree_node.handle]);

        // The logic behind this optimisation is that there's no need to clip
        // the contents of a picture when the crop will be applied anyway as
        // part of compositing the picture.  However, this is not true if the
        // picture includes a blur filter as the blur result depends on the
        // offscreen pixels which may or may not be cropped away.
        let has_blur = match &pictures[pic_index.0].composite_mode {
            Some(PictureCompositeMode::Filter(Filter::Blur { .. })) => true,
            Some(PictureCompositeMode::Filter(Filter::DropShadows { .. })) => true,
            Some(PictureCompositeMode::SvgFilter( .. )) => true,
            Some(PictureCompositeMode::SVGFEGraph( .. )) => true,
            _ => false,
        };

        // It is only safe to apply this optimisation if the old pic clip node
        // is the direct parent of the new LCA node.  If this is not the case
        // then there could be other more restrictive clips in between the two
        // which we would ignore by changing the clip root.  See Bug 1854062
        // for an example of this.
        let direct_parent = lca_tree_node
            .zip(pic_node_id)
            .map(|(lca_tree_node, pic_node_id)| lca_tree_node.parent == pic_node_id)
            .unwrap_or(false);

        let should_set_clip_root = is_snapshot || lca_node.zip(pic_node).map_or(false, |(lca_node, pic_node)| {
            // It is only safe to ignore the LCA clip (by making it the clip
            // root) if it is equal to or larger than the picture clip. But
            // this comparison also needs to take into account spatial nodes
            // as the two clips may in general be on different spatial nodes.
            // For this specific Gecko optimisation we expect the the two
            // clips to be identical and have the same spatial node so it's
            // simplest to just test for ClipItemKey equality (which includes
            // both spatial node and the actual clip).
            lca_node.key == pic_node.key && !has_blur && direct_parent
        });

        if should_set_clip_root {
            pictures[pic_index.0].clip_root = shared_clip_node_id;
        }

        // Update the spatial node of any child pictures
        for cluster in &prim_list.clusters {
            for prim_instance_index in cluster.prim_range() {
                if let PrimitiveInstanceKind::Picture { pic_index: child_pic_index, .. } = prim_instances[prim_instance_index].kind {
                    let child_pic = &mut pictures[child_pic_index.0];

                    if child_pic.spatial_node_index == SpatialNodeIndex::UNKNOWN {
                        child_pic.spatial_node_index = spatial_node_index;
                    }

                    // Recurse into child pictures which may also have unknown spatial nodes
                    SceneBuilder::finalize_picture(
                        child_pic_index,
                        Some(prim_instance_index),
                        pictures,
                        Some(spatial_node_index),
                        clip_tree_builder,
                        prim_instances,
                        clip_interner,
                    );

                    if pictures[child_pic_index.0].flags.contains(PictureFlags::DISABLE_SNAPPING) {
                        pictures[pic_index.0].flags |= PictureFlags::DISABLE_SNAPPING;
                    }
                }
            }
        }

        // Restore the prim_list
        pictures[pic_index.0].prim_list = prim_list;
    }

    /// Retrieve the current external scroll offset on the provided spatial node.
    fn current_external_scroll_offset(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
    ) -> LayoutVector2D {
        // Get the external scroll offset, if applicable.
        self.external_scroll_mapper
            .external_scroll_offset(
                spatial_node_index,
                self.spatial_tree,
            )
    }

    fn build_spatial_tree_for_display_list(
        &mut self,
        dl: &BuiltDisplayList,
        pipeline_id: PipelineId,
        instance_id: PipelineInstanceId,
    ) {
        dl.iter_spatial_tree(|item| {
            match item {
                SpatialTreeItem::ScrollFrame(descriptor) => {
                    let parent_space = self.get_space(descriptor.parent_space);
                    self.build_scroll_frame(
                        descriptor,
                        parent_space,
                        pipeline_id,
                        instance_id,
                    );
                }
                SpatialTreeItem::ReferenceFrame(descriptor) => {
                    let parent_space = self.get_space(descriptor.parent_spatial_id);
                    self.build_reference_frame(
                        descriptor,
                        parent_space,
                        pipeline_id,
                        instance_id,
                    );
                }
                SpatialTreeItem::StickyFrame(descriptor) => {
                    let parent_space = self.get_space(descriptor.parent_spatial_id);
                    self.build_sticky_frame(
                        descriptor,
                        parent_space,
                        instance_id,
                    );
                }
                SpatialTreeItem::Invalid => {
                    unreachable!();
                }
            }
        });
    }

    fn build_all(
        &mut self,
        root_pipeline_id: PipelineId,
        root_pipeline: &ScenePipeline,
    ) {
        enum ContextKind<'a> {
            Root,
            StackingContext {
                sc_info: StackingContextInfo,
            },
            ReferenceFrame,
            Iframe {
                parent_traversal: BuiltDisplayListIter<'a>,
            }
        }
        struct BuildContext<'a> {
            pipeline_id: PipelineId,
            kind: ContextKind<'a>,
        }

        self.id_to_index_mapper_stack.push(NodeIdToIndexMapper::default());

        let instance_id = self.get_next_instance_id_for_pipeline(root_pipeline_id);

        self.push_root(
            root_pipeline_id,
            instance_id,
        );
        self.build_spatial_tree_for_display_list(
            &root_pipeline.display_list.display_list,
            root_pipeline_id,
            instance_id,
        );

        let mut stack = vec![BuildContext {
            pipeline_id: root_pipeline_id,
            kind: ContextKind::Root,
        }];
        let mut traversal = root_pipeline.display_list.iter();

        'outer: while let Some(bc) = stack.pop() {
            loop {
                let item = match traversal.next() {
                    Some(item) => item,
                    None => break,
                };

                match item.item() {
                    DisplayItem::PushStackingContext(ref info) => {
                        profile_scope!("build_stacking_context");
                        let spatial_node_index = self.get_space(info.spatial_id);
                        let mut subtraversal = item.sub_iter();
                        // Avoid doing unnecessary work for empty stacking contexts.
                        // We still have to process it if it has filters, they
                        // may be things like SVGFEFlood or various specific
                        // ways to use ComponentTransfer, ColorMatrix, Composite
                        // which are still visible on an empty stacking context
                        if subtraversal.current_stacking_context_empty() && item.filters().is_empty() {
                            subtraversal.skip_current_stacking_context();
                            traversal = subtraversal;
                            continue;
                        }

                        let snapshot = info.snapshot.map(|snapshot| {
                            // Offset the snapshot area by the stacking context origin
                            // so that the area is expressed in the same coordinate space
                            // as the items in the stacking context.
                            SnapshotInfo {
                                area: snapshot.area.translate(info.origin.to_vector()),
                                .. snapshot
                            }
                        });

                        let composition_operations = CompositeOps::new(
                            filter_ops_for_compositing(item.filters()),
                            filter_datas_for_compositing(item.filter_datas()),
                            filter_primitives_for_compositing(item.filter_primitives()),
                            info.stacking_context.mix_blend_mode_for_compositing(),
                            snapshot,
                        );

                        let sc_info = self.push_stacking_context(
                            composition_operations,
                            info.stacking_context.transform_style,
                            info.prim_flags,
                            spatial_node_index,
                            info.stacking_context.clip_chain_id,
                            info.stacking_context.raster_space,
                            info.stacking_context.flags,
                            info.ref_frame_offset + info.origin.to_vector(),
                        );

                        let new_context = BuildContext {
                            pipeline_id: bc.pipeline_id,
                            kind: ContextKind::StackingContext {
                                sc_info,
                            },
                        };
                        stack.push(bc);
                        stack.push(new_context);

                        subtraversal.merge_debug_stats_from(&mut traversal);
                        traversal = subtraversal;
                        continue 'outer;
                    }
                    DisplayItem::PushReferenceFrame(..) => {
                        profile_scope!("build_reference_frame");
                        let mut subtraversal = item.sub_iter();

                        let new_context = BuildContext {
                            pipeline_id: bc.pipeline_id,
                            kind: ContextKind::ReferenceFrame,
                        };
                        stack.push(bc);
                        stack.push(new_context);

                        subtraversal.merge_debug_stats_from(&mut traversal);
                        traversal = subtraversal;
                        continue 'outer;
                    }
                    DisplayItem::PopReferenceFrame |
                    DisplayItem::PopStackingContext => break,
                    DisplayItem::Iframe(ref info) => {
                        profile_scope!("iframe");

                        let space = self.get_space(info.space_and_clip.spatial_id);
                        let subtraversal = match self.push_iframe(info, space) {
                            Some(pair) => pair,
                            None => continue,
                        };

                        let new_context = BuildContext {
                            pipeline_id: info.pipeline_id,
                            kind: ContextKind::Iframe {
                                parent_traversal: mem::replace(&mut traversal, subtraversal),
                            },
                        };
                        stack.push(bc);
                        stack.push(new_context);
                        continue 'outer;
                    }
                    _ => {
                        self.build_item(item);
                    }
                };
            }

            match bc.kind {
                ContextKind::Root => {}
                ContextKind::StackingContext { sc_info } => {
                    self.pop_stacking_context(sc_info);
                }
                ContextKind::ReferenceFrame => {
                }
                ContextKind::Iframe { parent_traversal } => {
                    self.iframe_size.pop();
                    self.clip_tree_builder.pop_clip();
                    self.clip_tree_builder.pop_clip();

                    if self.iframe_size.is_empty() {
                        assert!(self.root_iframe_clip.is_some());
                        self.root_iframe_clip = None;
                        self.add_tile_cache_barrier_if_needed(SliceFlags::empty());
                    }

                    self.id_to_index_mapper_stack.pop().unwrap();

                    traversal = parent_traversal;
                }
            }

            // TODO: factor this out to be part of capture
            if cfg!(feature = "display_list_stats") {
                let stats = traversal.debug_stats();
                let total_bytes: usize = stats.iter().map(|(_, stats)| stats.num_bytes).sum();
                debug!("item, total count, total bytes, % of DL bytes, bytes per item");
                for (label, stats) in stats {
                    debug!("{}, {}, {}kb, {}%, {}",
                        label,
                        stats.total_count,
                        stats.num_bytes / 1000,
                        ((stats.num_bytes as f32 / total_bytes.max(1) as f32) * 100.0) as usize,
                        stats.num_bytes / stats.total_count.max(1));
                }
                debug!("");
            }
        }

        debug_assert!(self.sc_stack.is_empty());

        self.id_to_index_mapper_stack.pop().unwrap();
        assert!(self.id_to_index_mapper_stack.is_empty());
    }

    fn build_sticky_frame(
        &mut self,
        info: &StickyFrameDescriptor,
        parent_node_index: SpatialNodeIndex,
        instance_id: PipelineInstanceId,
    ) {
        let external_scroll_offset = self.current_external_scroll_offset(parent_node_index);

        let sticky_frame_info = StickyFrameInfo::new(
            info.bounds.translate(external_scroll_offset),
            info.margins,
            info.vertical_offset_bounds,
            info.horizontal_offset_bounds,
            info.previously_applied_offset,
            info.transform,
        );

        let index = self.spatial_tree.add_sticky_frame(
            parent_node_index,
            sticky_frame_info,
            info.id.pipeline_id(),
            info.key,
            instance_id,
        );
        self.id_to_index_mapper_stack.last_mut().unwrap().add_spatial_node(info.id, index);
    }

    fn build_reference_frame(
        &mut self,
        info: &ReferenceFrameDescriptor,
        parent_space: SpatialNodeIndex,
        pipeline_id: PipelineId,
        instance_id: PipelineInstanceId,
    ) {
        let transform = match info.reference_frame.transform {
            ReferenceTransformBinding::Static { binding } => binding,
            ReferenceTransformBinding::Computed { scale_from, vertical_flip, rotation } => {
                let content_size = &self.iframe_size.last().unwrap();

                let mut transform = if let Some(scale_from) = scale_from {
                    // If we have a 90/270 degree rotation, then scale_from
                    // and content_size are in different coordinate spaces and
                    // we need to swap width/height for them to be correct.
                    match rotation {
                        Rotation::Degree0 |
                        Rotation::Degree180 => {
                            LayoutTransform::scale(
                                content_size.width / scale_from.width,
                                content_size.height / scale_from.height,
                                1.0
                            )
                        },
                        Rotation::Degree90 |
                        Rotation::Degree270 => {
                            LayoutTransform::scale(
                                content_size.height / scale_from.width,
                                content_size.width / scale_from.height,
                                1.0
                            )

                        }
                    }
                } else {
                    LayoutTransform::identity()
                };

                if vertical_flip {
                    let content_size = &self.iframe_size.last().unwrap();
                    let content_height = match rotation {
                        Rotation::Degree0 | Rotation::Degree180 => content_size.height,
                        Rotation::Degree90 | Rotation::Degree270 => content_size.width,
                    };
                    transform = transform
                        .then_translate(LayoutVector3D::new(0.0, content_height, 0.0))
                        .pre_scale(1.0, -1.0, 1.0);
                }

                let rotate = rotation.to_matrix(**content_size);
                let transform = transform.then(&rotate);

                PropertyBinding::Value(transform)
            },
        };

        let external_scroll_offset = self.current_external_scroll_offset(parent_space);

        self.push_reference_frame(
            info.reference_frame.id,
            parent_space,
            pipeline_id,
            info.reference_frame.transform_style,
            transform,
            info.reference_frame.kind,
            (info.origin + external_scroll_offset).to_vector(),
            SpatialNodeUid::external(info.reference_frame.key, pipeline_id, instance_id),
        );
    }

    fn build_scroll_frame(
        &mut self,
        info: &ScrollFrameDescriptor,
        parent_node_index: SpatialNodeIndex,
        pipeline_id: PipelineId,
        instance_id: PipelineInstanceId,
    ) {
        // This is useful when calculating scroll extents for the
        // SpatialNode::scroll(..) API as well as for properly setting sticky
        // positioning offsets.
        let content_size = info.content_rect.size();
        let external_scroll_offset = self.current_external_scroll_offset(parent_node_index);

        self.add_scroll_frame(
            info.scroll_frame_id,
            parent_node_index,
            info.external_id,
            pipeline_id,
            &info.frame_rect.translate(external_scroll_offset),
            &content_size,
            ScrollFrameKind::Explicit,
            info.external_scroll_offset,
            info.scroll_offset_generation,
            info.has_scroll_linked_effect,
            SpatialNodeUid::external(info.key, pipeline_id, instance_id),
        );
    }

    /// Advance and return the next instance id for a given pipeline id
    fn get_next_instance_id_for_pipeline(
        &mut self,
        pipeline_id: PipelineId,
    ) -> PipelineInstanceId {
        let next_instance = self.pipeline_instance_ids
            .entry(pipeline_id)
            .or_insert(0);

        let instance_id = PipelineInstanceId::new(*next_instance);
        *next_instance += 1;

        instance_id
    }

    fn push_iframe(
        &mut self,
        info: &IframeDisplayItem,
        spatial_node_index: SpatialNodeIndex,
    ) -> Option<BuiltDisplayListIter<'a>> {
        let iframe_pipeline_id = info.pipeline_id;
        let pipeline = match self.scene.pipelines.get(&iframe_pipeline_id) {
            Some(pipeline) => pipeline,
            None => {
                debug_assert!(info.ignore_missing_pipeline);
                return None
            },
        };

        self.clip_tree_builder.push_clip_chain(Some(info.space_and_clip.clip_chain_id), false);

        // TODO(gw): This is the only remaining call site that relies on ClipId parenting, remove me!
        self.add_rect_clip_node(
            ClipId::root(iframe_pipeline_id),
            info.space_and_clip.spatial_id,
            &info.clip_rect,
        );

        self.clip_tree_builder.push_clip_id(ClipId::root(iframe_pipeline_id));

        let instance_id = self.get_next_instance_id_for_pipeline(iframe_pipeline_id);

        self.id_to_index_mapper_stack.push(NodeIdToIndexMapper::default());

        let bounds = self.normalize_scroll_offset_and_snap_rect(
            &info.bounds,
            spatial_node_index,
        );

        let spatial_node_index = self.push_reference_frame(
            SpatialId::root_reference_frame(iframe_pipeline_id),
            spatial_node_index,
            iframe_pipeline_id,
            TransformStyle::Flat,
            PropertyBinding::Value(LayoutTransform::identity()),
            ReferenceFrameKind::Transform {
                is_2d_scale_translation: true,
                should_snap: true,
                paired_with_perspective: false,
            },
            bounds.min.to_vector(),
            SpatialNodeUid::root_reference_frame(iframe_pipeline_id, instance_id),
        );

        let iframe_rect = LayoutRect::from_size(bounds.size());
        let is_root_pipeline = self.iframe_size.is_empty();

        self.add_scroll_frame(
            SpatialId::root_scroll_node(iframe_pipeline_id),
            spatial_node_index,
            ExternalScrollId(0, iframe_pipeline_id),
            iframe_pipeline_id,
            &iframe_rect,
            &bounds.size(),
            ScrollFrameKind::PipelineRoot {
                is_root_pipeline,
            },
            LayoutVector2D::zero(),
            APZScrollGeneration::default(),
            HasScrollLinkedEffect::No,
            SpatialNodeUid::root_scroll_frame(iframe_pipeline_id, instance_id),
        );

        // If this is a root iframe, force a new tile cache both before and after
        // adding primitives for this iframe.
        if self.iframe_size.is_empty() {
            assert!(self.root_iframe_clip.is_none());
            self.root_iframe_clip = Some(ClipId::root(iframe_pipeline_id));
            self.add_tile_cache_barrier_if_needed(SliceFlags::empty());
        }
        self.iframe_size.push(bounds.size());

        self.build_spatial_tree_for_display_list(
            &pipeline.display_list.display_list,
            iframe_pipeline_id,
            instance_id,
        );

        Some(pipeline.display_list.iter())
    }

    fn get_space(
        &self,
        spatial_id: SpatialId,
    ) -> SpatialNodeIndex {
        self.id_to_index_mapper_stack.last().unwrap().get_spatial_node_index(spatial_id)
    }

    fn get_clip_node(
        &mut self,
        clip_chain_id: api::ClipChainId,
    ) -> ClipNodeId {
        self.clip_tree_builder.build_clip_set(
            clip_chain_id,
        )
    }

    fn process_common_properties(
        &mut self,
        common: &CommonItemProperties,
        bounds: Option<LayoutRect>,
    ) -> (LayoutPrimitiveInfo, LayoutRect, SpatialNodeIndex, ClipNodeId) {
        let spatial_node_index = self.get_space(common.spatial_id);

        // If no bounds rect is given, default to clip rect.
        let mut clip_rect = common.clip_rect;
        let mut prim_rect = bounds.unwrap_or(clip_rect);
        let unsnapped_rect = self.normalize_rect_scroll_offset(&prim_rect, spatial_node_index);

        // If antialiased, no need to snap but we still need to remove the
        // external scroll offset (it's applied later during frame building,
        // so that we don't intern to a different hash and invalidate content
        // in picture caches unnecessarily).
        if common.flags.contains(PrimitiveFlags::ANTIALISED) {
            prim_rect = self.normalize_rect_scroll_offset(&prim_rect, spatial_node_index);
            clip_rect = self.normalize_rect_scroll_offset(&clip_rect, spatial_node_index);
        } else {
            clip_rect = self.normalize_scroll_offset_and_snap_rect(
                &clip_rect,
                spatial_node_index,
            );

            prim_rect = self.normalize_scroll_offset_and_snap_rect(
                &prim_rect,
                spatial_node_index,
            );
        }

        let clip_node_id = self.get_clip_node(
            common.clip_chain_id,
        );

        let layout = LayoutPrimitiveInfo {
            rect: prim_rect,
            clip_rect,
            flags: common.flags,
        };

        (layout, unsnapped_rect, spatial_node_index, clip_node_id)
    }

    fn process_common_properties_with_bounds(
        &mut self,
        common: &CommonItemProperties,
        bounds: LayoutRect,
    ) -> (LayoutPrimitiveInfo, LayoutRect, SpatialNodeIndex, ClipNodeId) {
        self.process_common_properties(
            common,
            Some(bounds),
        )
    }

    // Remove the effect of the external scroll offset embedded in the display list
    // coordinates by Gecko. This ensures that we don't necessarily invalidate picture
    // cache tiles due to the embedded scroll offsets.
    fn normalize_rect_scroll_offset(
        &mut self,
        rect: &LayoutRect,
        spatial_node_index: SpatialNodeIndex,
    ) -> LayoutRect {
        let current_offset = self.current_external_scroll_offset(spatial_node_index);

        rect.translate(current_offset)
    }

    // Remove external scroll offset and snap a rect. The external scroll offset must
    // be removed first, as it may be fractional (which we don't want to affect the
    // snapping behavior during scene building).
    fn normalize_scroll_offset_and_snap_rect(
        &mut self,
        rect: &LayoutRect,
        target_spatial_node: SpatialNodeIndex,
    ) -> LayoutRect {
        let rect = self.normalize_rect_scroll_offset(rect, target_spatial_node);

        self.snap_to_device.set_target_spatial_node(
            target_spatial_node,
            self.spatial_tree,
        );
        self.snap_to_device.snap_rect(&rect)
    }

    fn build_item<'b>(
        &'b mut self,
        item: DisplayItemRef,
    ) {
        match *item.item() {
            DisplayItem::Image(ref info) => {
                profile_scope!("image");

                let (layout, _, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.bounds,
                );

                self.add_image(
                    spatial_node_index,
                    clip_node_id,
                    &layout,
                    layout.rect.size(),
                    LayoutSize::zero(),
                    info.image_key,
                    info.image_rendering,
                    info.alpha_type,
                    info.color,
                );
            }
            DisplayItem::RepeatingImage(ref info) => {
                profile_scope!("repeating_image");

                let (layout, unsnapped_rect, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.bounds,
                );

                let stretch_size = process_repeat_size(
                    &layout.rect,
                    &unsnapped_rect,
                    info.stretch_size,
                );

                self.add_image(
                    spatial_node_index,
                    clip_node_id,
                    &layout,
                    stretch_size,
                    info.tile_spacing,
                    info.image_key,
                    info.image_rendering,
                    info.alpha_type,
                    info.color,
                );
            }
            DisplayItem::YuvImage(ref info) => {
                profile_scope!("yuv_image");

                let (layout, _, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.bounds,
                );

                self.add_yuv_image(
                    spatial_node_index,
                    clip_node_id,
                    &layout,
                    info.yuv_data,
                    info.color_depth,
                    info.color_space,
                    info.color_range,
                    info.image_rendering,
                );
            }
            DisplayItem::Text(ref info) => {
                profile_scope!("text");

                // TODO(aosmond): Snapping text primitives does not make much sense, given the
                // primitive bounds and clip are supposed to be conservative, not definitive.
                // E.g. they should be able to grow and not impact the output. However there
                // are subtle interactions between the primitive origin and the glyph offset
                // which appear to be significant (presumably due to some sort of accumulated
                // error throughout the layers). We should fix this at some point.
                let (layout, _, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.bounds,
                );

                self.add_text(
                    spatial_node_index,
                    clip_node_id,
                    &layout,
                    &info.font_key,
                    &info.color,
                    item.glyphs(),
                    info.glyph_options,
                    info.ref_frame_offset,
                );
            }
            DisplayItem::Rectangle(ref info) => {
                profile_scope!("rect");

                let (layout, _, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.bounds,
                );

                self.add_primitive(
                    spatial_node_index,
                    clip_node_id,
                    &layout,
                    Vec::new(),
                    PrimitiveKeyKind::Rectangle {
                        color: info.color.into(),
                    },
                );

                if info.common.flags.contains(PrimitiveFlags::CHECKERBOARD_BACKGROUND) {
                    self.add_tile_cache_barrier_if_needed(SliceFlags::empty());
                }
            }
            DisplayItem::HitTest(ref info) => {
                profile_scope!("hit_test");

                let spatial_node_index = self.get_space(info.spatial_id);

                let rect = self.normalize_scroll_offset_and_snap_rect(
                    &info.rect,
                    spatial_node_index,
                );

                let layout = LayoutPrimitiveInfo {
                    rect,
                    clip_rect: rect,
                    flags: info.flags,
                };

                let spatial_node = self.spatial_tree.get_node_info(spatial_node_index);
                let anim_id: u64 =  match spatial_node.node_type {
                    SpatialNodeType::ReferenceFrame(ReferenceFrameInfo {
                        source_transform: PropertyBinding::Binding(key, _),
                        ..
                    }) => key.clone().into(),
                    SpatialNodeType::StickyFrame(StickyFrameInfo {
                        transform: Some(PropertyBinding::Binding(key, _)),
                        ..
                    }) => key.clone().into(),
                    _ => 0,
                };

                let clip_node_id = self.get_clip_node(info.clip_chain_id);

                self.add_primitive_to_hit_testing_list(
                    &layout,
                    spatial_node_index,
                    clip_node_id,
                    info.tag,
                    anim_id,
                );
            }
            DisplayItem::ClearRectangle(ref info) => {
                profile_scope!("clear");

                let (layout, _, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.bounds,
                );

                self.add_clear_rectangle(
                    spatial_node_index,
                    clip_node_id,
                    &layout,
                );
            }
            DisplayItem::Line(ref info) => {
                profile_scope!("line");

                let (layout, _, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.area,
                );

                self.add_line(
                    spatial_node_index,
                    clip_node_id,
                    &layout,
                    info.wavy_line_thickness,
                    info.orientation,
                    info.color,
                    info.style,
                );
            }
            DisplayItem::Gradient(ref info) => {
                profile_scope!("gradient");

                if !info.gradient.is_valid() {
                    return;
                }

                let (mut layout, unsnapped_rect, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.bounds,
                );

                let mut tile_size = process_repeat_size(
                    &layout.rect,
                    &unsnapped_rect,
                    info.tile_size,
                );

                let mut stops = read_gradient_stops(item.gradient_stops());
                let mut start = info.gradient.start_point;
                let mut end = info.gradient.end_point;
                let flags = layout.flags;

                let optimized = optimize_linear_gradient(
                    &mut layout.rect,
                    &mut tile_size,
                    info.tile_spacing,
                    &layout.clip_rect,
                    &mut start,
                    &mut end,
                    info.gradient.extend_mode,
                    &mut stops,
                    &mut |rect, start, end, stops, edge_aa_mask| {
                        let layout = LayoutPrimitiveInfo { rect: *rect, clip_rect: *rect, flags };
                        if let Some(prim_key_kind) = self.create_linear_gradient_prim(
                            &layout,
                            start,
                            end,
                            stops.to_vec(),
                            ExtendMode::Clamp,
                            rect.size(),
                            LayoutSize::zero(),
                            None,
                            edge_aa_mask,
                        ) {
                            self.add_nonshadowable_primitive(
                                spatial_node_index,
                                clip_node_id,
                                &layout,
                                Vec::new(),
                                prim_key_kind,
                            );
                        }
                    }
                );

                if !optimized && !tile_size.ceil().is_empty() {
                    if let Some(prim_key_kind) = self.create_linear_gradient_prim(
                        &layout,
                        start,
                        end,
                        stops,
                        info.gradient.extend_mode,
                        tile_size,
                        info.tile_spacing,
                        None,
                        EdgeAaSegmentMask::all(),
                    ) {
                        self.add_nonshadowable_primitive(
                            spatial_node_index,
                            clip_node_id,
                            &layout,
                            Vec::new(),
                            prim_key_kind,
                        );
                    }
                }
            }
            DisplayItem::RadialGradient(ref info) => {
                profile_scope!("radial");

                if !info.gradient.is_valid() {
                    return;
                }

                let (mut layout, unsnapped_rect, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.bounds,
                );

                let mut center = info.gradient.center;

                let stops = read_gradient_stops(item.gradient_stops());

                let mut tile_size = process_repeat_size(
                    &layout.rect,
                    &unsnapped_rect,
                    info.tile_size,
                );

                let mut prim_rect = layout.rect;
                let mut tile_spacing = info.tile_spacing;
                optimize_radial_gradient(
                    &mut prim_rect,
                    &mut tile_size,
                    &mut center,
                    &mut tile_spacing,
                    &layout.clip_rect,
                    info.gradient.radius,
                    info.gradient.end_offset,
                    info.gradient.extend_mode,
                    &stops,
                    &mut |solid_rect, color| {
                        self.add_nonshadowable_primitive(
                            spatial_node_index,
                            clip_node_id,
                            &LayoutPrimitiveInfo {
                                rect: *solid_rect,
                                .. layout
                            },
                            Vec::new(),
                            PrimitiveKeyKind::Rectangle { color: PropertyBinding::Value(color) },
                        );
                    }
                );

                // TODO: create_radial_gradient_prim already calls
                // this, but it leaves the info variable that is
                // passed to add_nonshadowable_primitive unmodified
                // which can cause issues.
                simplify_repeated_primitive(&tile_size, &mut tile_spacing, &mut prim_rect);

                if !tile_size.ceil().is_empty() {
                    layout.rect = prim_rect;
                    let prim_key_kind = self.create_radial_gradient_prim(
                        &layout,
                        center,
                        info.gradient.start_offset * info.gradient.radius.width,
                        info.gradient.end_offset * info.gradient.radius.width,
                        info.gradient.radius.width / info.gradient.radius.height,
                        stops,
                        info.gradient.extend_mode,
                        tile_size,
                        tile_spacing,
                        None,
                    );

                    self.add_nonshadowable_primitive(
                        spatial_node_index,
                        clip_node_id,
                        &layout,
                        Vec::new(),
                        prim_key_kind,
                    );
                }
            }
            DisplayItem::ConicGradient(ref info) => {
                profile_scope!("conic");

                if !info.gradient.is_valid() {
                    return;
                }

                let (mut layout, unsnapped_rect, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.bounds,
                );

                let tile_size = process_repeat_size(
                    &layout.rect,
                    &unsnapped_rect,
                    info.tile_size,
                );

                let offset = apply_gradient_local_clip(
                    &mut layout.rect,
                    &tile_size,
                    &info.tile_spacing,
                    &layout.clip_rect,
                );
                let center = info.gradient.center + offset;

                if !tile_size.ceil().is_empty() {
                    let prim_key_kind = self.create_conic_gradient_prim(
                        &layout,
                        center,
                        info.gradient.angle,
                        info.gradient.start_offset,
                        info.gradient.end_offset,
                        item.gradient_stops(),
                        info.gradient.extend_mode,
                        tile_size,
                        info.tile_spacing,
                        None,
                    );

                    self.add_nonshadowable_primitive(
                        spatial_node_index,
                        clip_node_id,
                        &layout,
                        Vec::new(),
                        prim_key_kind,
                    );
                }
            }
            DisplayItem::BoxShadow(ref info) => {
                profile_scope!("box_shadow");

                let (layout, _, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.box_bounds,
                );

                self.add_box_shadow(
                    spatial_node_index,
                    clip_node_id,
                    &layout,
                    &info.offset,
                    info.color,
                    info.blur_radius,
                    info.spread_radius,
                    info.border_radius,
                    info.clip_mode,
                    self.spatial_tree.is_root_coord_system(spatial_node_index),
                );
            }
            DisplayItem::Border(ref info) => {
                profile_scope!("border");

                let (layout, _, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.bounds,
                );

                self.add_border(
                    spatial_node_index,
                    clip_node_id,
                    &layout,
                    info,
                    item.gradient_stops(),
                );
            }
            DisplayItem::ImageMaskClip(ref info) => {
                profile_scope!("image_clip");

                self.add_image_mask_clip_node(
                    info.id,
                    info.spatial_id,
                    &info.image_mask,
                    info.fill_rule,
                    item.points(),
                );
            }
            DisplayItem::RoundedRectClip(ref info) => {
                profile_scope!("rounded_clip");

                self.add_rounded_rect_clip_node(
                    info.id,
                    info.spatial_id,
                    &info.clip,
                );
            }
            DisplayItem::RectClip(ref info) => {
                profile_scope!("rect_clip");

                self.add_rect_clip_node(
                    info.id,
                    info.spatial_id,
                    &info.clip_rect,
                );
            }
            DisplayItem::ClipChain(ref info) => {
                profile_scope!("clip_chain");

                self.clip_tree_builder.define_clip_chain(
                    info.id,
                    info.parent,
                    item.clip_chain_items().into_iter(),
                );
            },
            DisplayItem::BackdropFilter(ref info) => {
                profile_scope!("backdrop");

                let (layout, _, spatial_node_index, clip_node_id) = self.process_common_properties(
                    &info.common,
                    None,
                );

                let filters = filter_ops_for_compositing(item.filters());
                let filter_datas = filter_datas_for_compositing(item.filter_datas());
                let filter_primitives = filter_primitives_for_compositing(item.filter_primitives());

                self.add_backdrop_filter(
                    spatial_node_index,
                    clip_node_id,
                    &layout,
                    filters,
                    filter_datas,
                    filter_primitives,
                );
            }

            // Do nothing; these are dummy items for the display list parser
            DisplayItem::SetGradientStops |
            DisplayItem::SetFilterOps |
            DisplayItem::SetFilterData |
            DisplayItem::SetFilterPrimitives |
            DisplayItem::SetPoints => {}

            // Special items that are handled in the parent method
            DisplayItem::PushStackingContext(..) |
            DisplayItem::PushReferenceFrame(..) |
            DisplayItem::PopReferenceFrame |
            DisplayItem::PopStackingContext |
            DisplayItem::Iframe(_) => {
                unreachable!("Handled in `build_all`")
            }

            DisplayItem::ReuseItems(key) |
            DisplayItem::RetainedItems(key) => {
                unreachable!("Iterator logic error: {:?}", key);
            }

            DisplayItem::PushShadow(info) => {
                profile_scope!("push_shadow");

                let spatial_node_index = self.get_space(info.space_and_clip.spatial_id);

                self.push_shadow(
                    info.shadow,
                    spatial_node_index,
                    info.space_and_clip.clip_chain_id,
                    info.should_inflate,
                );
            }
            DisplayItem::PopAllShadows => {
                profile_scope!("pop_all_shadows");

                self.pop_all_shadows();
            }
            DisplayItem::DebugMarker(..) => {}
        }
    }

    /// Create a primitive and add it to the prim store. This method doesn't
    /// add the primitive to the draw list, so can be used for creating
    /// sub-primitives.
    ///
    /// TODO(djg): Can this inline into `add_interned_prim_to_draw_list`
    fn create_primitive<P>(
        &mut self,
        info: &LayoutPrimitiveInfo,
        clip_leaf_id: ClipLeafId,
        prim: P,
    ) -> PrimitiveInstance
    where
        P: InternablePrimitive,
        Interners: AsMut<Interner<P>>,
    {
        // Build a primitive key.
        let prim_key = prim.into_key(info);

        let interner = self.interners.as_mut();
        let prim_data_handle = interner
            .intern(&prim_key, || ());

        let instance_kind = P::make_instance_kind(
            prim_key,
            prim_data_handle,
            &mut self.prim_store,
        );

        PrimitiveInstance::new(
            instance_kind,
            clip_leaf_id,
        )
    }

    fn add_primitive_to_hit_testing_list(
        &mut self,
        info: &LayoutPrimitiveInfo,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        tag: ItemTag,
        anim_id: u64,
    ) {
        self.hit_testing_scene.add_item(
            tag,
            anim_id,
            info,
            spatial_node_index,
            clip_node_id,
            &self.clip_tree_builder,
            self.interners,
        );
    }

    /// Add an already created primitive to the draw lists.
    pub fn add_primitive_to_draw_list(
        &mut self,
        prim_instance: PrimitiveInstance,
        prim_rect: LayoutRect,
        spatial_node_index: SpatialNodeIndex,
        flags: PrimitiveFlags,
    ) {
        // Add primitive to the top-most stacking context on the stack.

        // If we have a valid stacking context, the primitive gets added to that.
        // Otherwise, it gets added to a top-level picture cache slice.

        match self.sc_stack.last_mut() {
            Some(stacking_context) => {
                stacking_context.prim_list.add_prim(
                    prim_instance,
                    prim_rect,
                    spatial_node_index,
                    flags,
                    &mut self.prim_instances,
                    &self.clip_tree_builder,
                );
            }
            None => {
                self.tile_cache_builder.add_prim(
                    prim_instance,
                    prim_rect,
                    spatial_node_index,
                    flags,
                    self.spatial_tree,
                    self.interners,
                    &self.quality_settings,
                    &mut self.prim_instances,
                    &self.clip_tree_builder,
                );
            }
        }
    }

    /// Convenience interface that creates a primitive entry and adds it
    /// to the draw list.
    pub fn add_nonshadowable_primitive<P>(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        info: &LayoutPrimitiveInfo,
        clip_items: Vec<ClipItemKey>,
        prim: P,
    )
    where
        P: InternablePrimitive + IsVisible,
        Interners: AsMut<Interner<P>>,
    {
        if prim.is_visible() {
            let clip_leaf_id = self.clip_tree_builder.build_for_prim(
                clip_node_id,
                info,
                &clip_items,
                &mut self.interners,
            );

            self.add_prim_to_draw_list(
                info,
                spatial_node_index,
                clip_leaf_id,
                prim,
            );
        }
    }

    pub fn add_primitive<P>(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        info: &LayoutPrimitiveInfo,
        clip_items: Vec<ClipItemKey>,
        prim: P,
    )
    where
        P: InternablePrimitive + IsVisible,
        Interners: AsMut<Interner<P>>,
        ShadowItem: From<PendingPrimitive<P>>
    {
        // If a shadow context is not active, then add the primitive
        // directly to the parent picture.
        if self.pending_shadow_items.is_empty() {
            self.add_nonshadowable_primitive(
                spatial_node_index,
                clip_node_id,
                info,
                clip_items,
                prim,
            );
        } else {
            debug_assert!(clip_items.is_empty(), "No per-prim clips expected for shadowed primitives");

            // There is an active shadow context. Store as a pending primitive
            // for processing during pop_all_shadows.
            self.pending_shadow_items.push_back(PendingPrimitive {
                spatial_node_index,
                clip_node_id,
                info: *info,
                prim,
            }.into());
        }
    }

    fn add_prim_to_draw_list<P>(
        &mut self,
        info: &LayoutPrimitiveInfo,
        spatial_node_index: SpatialNodeIndex,
        clip_leaf_id: ClipLeafId,
        prim: P,
    )
    where
        P: InternablePrimitive,
        Interners: AsMut<Interner<P>>,
    {
        let prim_instance = self.create_primitive(
            info,
            clip_leaf_id,
            prim,
        );
        self.add_primitive_to_draw_list(
            prim_instance,
            info.rect,
            spatial_node_index,
            info.flags,
        );
    }

    fn make_current_slice_atomic_if_required(&mut self) {
        let has_non_wrapping_sc = self.sc_stack
            .iter()
            .position(|sc| {
                !sc.flags.contains(StackingContextFlags::WRAPS_BACKDROP_FILTER)
            })
            .is_some();

        if has_non_wrapping_sc {
            return;
        }

        // Shadows can only exist within a stacking context
        assert!(self.pending_shadow_items.is_empty());
        self.tile_cache_builder.make_current_slice_atomic();
    }

    /// If no stacking contexts are present (i.e. we are adding prims to a tile
    /// cache), set a barrier to force creation of a slice before the next prim
    fn add_tile_cache_barrier_if_needed(
        &mut self,
        slice_flags: SliceFlags,
    ) {
        if self.sc_stack.is_empty() {
            // Shadows can only exist within a stacking context
            assert!(self.pending_shadow_items.is_empty());

            self.tile_cache_builder.add_tile_cache_barrier(
                slice_flags,
                self.root_iframe_clip,
            );
        }
    }

    /// Push a new stacking context. Returns context that must be passed to pop_stacking_context().
    fn push_stacking_context(
        &mut self,
        mut composite_ops: CompositeOps,
        transform_style: TransformStyle,
        prim_flags: PrimitiveFlags,
        spatial_node_index: SpatialNodeIndex,
        clip_chain_id: Option<api::ClipChainId>,
        requested_raster_space: RasterSpace,
        flags: StackingContextFlags,
        subregion_offset: LayoutVector2D,
    ) -> StackingContextInfo {
        profile_scope!("push_stacking_context");

        // Filters have to be baked into the snapshot. Most filters are applied
        // when rendering the picture into its parent, so if the stacking context
        // needs to be snapshotted, we nest it into an extra stacking context and
        // capture the outer stacking context into which the filter is drawn.
        // Note: blur filters don't actually need an extra stacking context
        // since the blur is baked into a render task instead of being applied
        // when compositing the picture into its parent. This case is fairly rare
        // so we pay the cost of the extra render pass for now.
        let needs_extra_stacking_context = composite_ops.snapshot.is_some()
            && composite_ops.has_valid_filters();

        if needs_extra_stacking_context {
            let snapshot = mem::take(&mut composite_ops.snapshot);
            let mut info = self.push_stacking_context(
                CompositeOps {
                    filters: Vec::new(),
                    filter_datas: Vec::new(),
                    filter_primitives: Vec::new(),
                    mix_blend_mode: None,
                    snapshot,
                },
                TransformStyle::Flat,
                prim_flags,
                spatial_node_index,
                clip_chain_id,
                requested_raster_space,
                flags,
                LayoutVector2D::zero(),
            );
            info.pop_stacking_context = true;
            self.extra_stacking_context_stack.push(info);
        }

        let clip_node_id = match clip_chain_id {
            Some(id) => {
                self.clip_tree_builder.build_clip_set(id)
            }
            None => {
                self.clip_tree_builder.build_clip_set(api::ClipChainId::INVALID)
            }
        };

        self.clip_tree_builder.push_clip_chain(
            clip_chain_id,
            !composite_ops.is_empty(),
        );

        let new_space = match (self.raster_space_stack.last(), requested_raster_space) {
            // If no parent space, just use the requested space
            (None, _) => requested_raster_space,
            // If screen, use the parent
            (Some(parent_space), RasterSpace::Screen) => *parent_space,
            // If currently screen, select the requested
            (Some(RasterSpace::Screen), space) => space,
            // If both local, take the maximum scale
            (Some(RasterSpace::Local(parent_scale)), RasterSpace::Local(scale)) => RasterSpace::Local(parent_scale.max(scale)),
        };
        self.raster_space_stack.push(new_space);

        // Get the transform-style of the parent stacking context,
        // which determines if we *might* need to draw this on
        // an intermediate surface for plane splitting purposes.
        let (parent_is_3d, extra_3d_instance, plane_splitter_index) = match self.sc_stack.last_mut() {
            Some(ref mut sc) if sc.is_3d() => {
                let (flat_items_context_3d, plane_splitter_index) = match sc.context_3d {
                    Picture3DContext::In { ancestor_index, plane_splitter_index, .. } => {
                        (
                            Picture3DContext::In {
                                root_data: None,
                                ancestor_index,
                                plane_splitter_index,
                            },
                            plane_splitter_index,
                        )
                    }
                    Picture3DContext::Out => panic!("Unexpected out of 3D context"),
                };
                // Cut the sequence of flat children before starting a child stacking context,
                // so that the relative order between them and our current SC is preserved.
                let extra_instance = sc.cut_item_sequence(
                    &mut self.prim_store,
                    &mut self.interners,
                    Some(PictureCompositeMode::Blit(BlitReason::PRESERVE3D)),
                    flat_items_context_3d,
                    &mut self.clip_tree_builder,
                );
                let extra_instance = extra_instance.map(|(_, instance)| {
                    ExtendedPrimitiveInstance {
                        instance,
                        spatial_node_index: sc.spatial_node_index,
                        flags: sc.prim_flags,
                    }
                });
                (true, extra_instance, Some(plane_splitter_index))
            },
            _ => (false, None, None),
        };

        if let Some(instance) = extra_3d_instance {
            self.add_primitive_instance_to_3d_root(instance);
        }

        // If this is preserve-3d *or* the parent is, then this stacking
        // context is participating in the 3d rendering context. In that
        // case, hoist the picture up to the 3d rendering context
        // container, so that it's rendered as a sibling with other
        // elements in this context.
        let participating_in_3d_context =
            composite_ops.is_empty() &&
            (parent_is_3d || transform_style == TransformStyle::Preserve3D);

        let context_3d = if participating_in_3d_context {
            // Get the spatial node index of the containing block, which
            // defines the context of backface-visibility.
            let ancestor_index = self.containing_block_stack
                .last()
                .cloned()
                .unwrap_or(self.spatial_tree.root_reference_frame_index());

            let plane_splitter_index = plane_splitter_index.unwrap_or_else(|| {
                let index = self.next_plane_splitter_index;
                self.next_plane_splitter_index += 1;
                PlaneSplitterIndex(index)
            });

            Picture3DContext::In {
                root_data: if parent_is_3d {
                    None
                } else {
                    Some(Vec::new())
                },
                plane_splitter_index,
                ancestor_index,
            }
        } else {
            Picture3DContext::Out
        };

        // Force an intermediate surface if the stacking context has a
        // complex clip node. In the future, we may decide during
        // prepare step to skip the intermediate surface if the
        // clip node doesn't affect the stacking context rect.
        let mut blit_reason = BlitReason::empty();

        // Stacking context snapshots are offscreen syrfaces.
        if composite_ops.snapshot.is_some() {
            blit_reason = BlitReason::SNAPSHOT;
        }

        // If this stacking context has any complex clips, we need to draw it
        // to an off-screen surface.
        if let Some(clip_chain_id) = clip_chain_id {
            if self.clip_tree_builder.clip_chain_has_complex_clips(clip_chain_id, &self.interners) {
                blit_reason |= BlitReason::CLIP;
            }
        }

        // Check if we know this stacking context is redundant (doesn't need a surface)
        // The check for blend-container redundancy is more involved so it's handled below.
        let mut is_redundant = FlattenedStackingContext::is_redundant(
            &context_3d,
            &composite_ops,
            blit_reason,
            self.sc_stack.last(),
            prim_flags,
        );

        // If the stacking context is a blend container, and if we're at the top level
        // of the stacking context tree, we may be able to make this blend container into a tile
        // cache. This means that we get caching and correct scrolling invalidation for
        // root level blend containers. For these cases, the readbacks of the backdrop
        // are handled by doing partial reads of the picture cache tiles during rendering.
        if flags.contains(StackingContextFlags::IS_BLEND_CONTAINER) {
            // Check if we're inside a stacking context hierarchy with an existing surface
            match self.sc_stack.last() {
                Some(_) => {
                    // If we are already inside a stacking context hierarchy with a surface, then we
                    // need to do the normal isolate of this blend container as a regular surface
                    blit_reason |= BlitReason::ISOLATE;
                    is_redundant = false;
                }
                None => {
                    // If the current slice is empty, then we can just mark the slice as
                    // atomic (so that compositor surfaces don't get promoted within it)
                    // and use that slice as the backing surface for the blend container
                    if self.tile_cache_builder.is_current_slice_empty() &&
                       self.spatial_tree.is_root_coord_system(spatial_node_index) &&
                       !self.clip_tree_builder.clip_node_has_complex_clips(clip_node_id, &self.interners)
                    {
                        self.add_tile_cache_barrier_if_needed(SliceFlags::IS_ATOMIC);
                        self.tile_cache_builder.make_current_slice_atomic();
                    } else {
                        // If the slice wasn't empty, we need to isolate a separate surface
                        // to ensure that the content already in the slice is not used as
                        // an input to the mix-blend composite
                        blit_reason |= BlitReason::ISOLATE;
                        is_redundant = false;
                    }
                }
            }
        }

        // If stacking context is a scrollbar, force a new slice for the primitives
        // within. The stacking context will be redundant and removed by above check.
        let set_tile_cache_barrier = prim_flags.contains(PrimitiveFlags::IS_SCROLLBAR_CONTAINER);

        if set_tile_cache_barrier {
            self.add_tile_cache_barrier_if_needed(SliceFlags::IS_SCROLLBAR);
        }

        let mut sc_info = StackingContextInfo {
            pop_stacking_context: false,
            pop_containing_block: false,
            set_tile_cache_barrier,
            needs_extra_stacking_context,
        };

        // If this is not 3d, then it establishes an ancestor root for child 3d contexts.
        if !participating_in_3d_context {
            sc_info.pop_containing_block = true;
            self.containing_block_stack.push(spatial_node_index);
        }

        // If not redundant, create a stacking context to hold primitive clusters
        if !is_redundant {
            sc_info.pop_stacking_context = true;

            // Push the SC onto the stack, so we know how to handle things in
            // pop_stacking_context.
            self.sc_stack.push(FlattenedStackingContext {
                prim_list: PrimitiveList::empty(),
                prim_flags,
                spatial_node_index,
                clip_node_id,
                composite_ops,
                blit_reason,
                transform_style,
                context_3d,
                flags,
                raster_space: new_space,
                subregion_offset,
            });
        }

        sc_info
    }

    fn pop_stacking_context(
        &mut self,
        info: StackingContextInfo,
    ) {
        profile_scope!("pop_stacking_context");

        self.clip_tree_builder.pop_clip();

        // Pop off current raster space (pushed unconditionally in push_stacking_context)
        self.raster_space_stack.pop().unwrap();

        // If the stacking context formed a containing block, pop off the stack
        if info.pop_containing_block {
            self.containing_block_stack.pop().unwrap();
        }

        if info.set_tile_cache_barrier {
            self.add_tile_cache_barrier_if_needed(SliceFlags::empty());
        }

        // If the stacking context was otherwise redundant, early exit
        if !info.pop_stacking_context {
            return;
        }

        let stacking_context = self.sc_stack.pop().unwrap();

        let mut source = match stacking_context.context_3d {
            // TODO(gw): For now, as soon as this picture is in
            //           a 3D context, we draw it to an intermediate
            //           surface and apply plane splitting. However,
            //           there is a large optimization opportunity here.
            //           During culling, we can check if there is actually
            //           perspective present, and skip the plane splitting
            //           completely when that is not the case.
            Picture3DContext::In { ancestor_index, plane_splitter_index, .. } => {
                let composite_mode = Some(
                    PictureCompositeMode::Blit(BlitReason::PRESERVE3D | stacking_context.blit_reason)
                );

                // Add picture for this actual stacking context contents to render into.
                let pic_index = PictureIndex(self.prim_store.pictures
                    .alloc()
                    .init(PicturePrimitive::new_image(
                        composite_mode.clone(),
                        Picture3DContext::In { root_data: None, ancestor_index, plane_splitter_index },
                        stacking_context.prim_flags,
                        stacking_context.prim_list,
                        stacking_context.spatial_node_index,
                        stacking_context.raster_space,
                        PictureFlags::empty(),
                        None,
                    ))
                );

                let instance = create_prim_instance(
                    pic_index,
                    composite_mode.into(),
                    stacking_context.raster_space,
                    stacking_context.clip_node_id,
                    &mut self.interners,
                    &mut self.clip_tree_builder,
                );

                PictureChainBuilder::from_instance(
                    instance,
                    stacking_context.prim_flags,
                    stacking_context.spatial_node_index,
                    stacking_context.raster_space,
                )
            }
            Picture3DContext::Out => {
                if stacking_context.blit_reason.is_empty() {
                    PictureChainBuilder::from_prim_list(
                        stacking_context.prim_list,
                        stacking_context.prim_flags,
                        stacking_context.spatial_node_index,
                        stacking_context.raster_space,
                        false,
                    )
                } else {
                    let composite_mode = Some(
                        PictureCompositeMode::Blit(stacking_context.blit_reason)
                    );

                    // Add picture for this actual stacking context contents to render into.
                    let pic_index = PictureIndex(self.prim_store.pictures
                        .alloc()
                        .init(PicturePrimitive::new_image(
                            composite_mode.clone(),
                            Picture3DContext::Out,
                            stacking_context.prim_flags,
                            stacking_context.prim_list,
                            stacking_context.spatial_node_index,
                            stacking_context.raster_space,
                            PictureFlags::empty(),
                            None,
                        ))
                    );

                    let instance = create_prim_instance(
                        pic_index,
                        composite_mode.into(),
                        stacking_context.raster_space,
                        stacking_context.clip_node_id,
                        &mut self.interners,
                        &mut self.clip_tree_builder,
                    );

                    PictureChainBuilder::from_instance(
                        instance,
                        stacking_context.prim_flags,
                        stacking_context.spatial_node_index,
                        stacking_context.raster_space,
                    )
                }
            }
        };

        // If establishing a 3d context, the `cur_instance` represents
        // a picture with all the *trailing* immediate children elements.
        // We append this to the preserve-3D picture set and make a container picture of them.
        if let Picture3DContext::In { root_data: Some(mut prims), ancestor_index, plane_splitter_index } = stacking_context.context_3d {
            let instance = source.finalize(
                ClipNodeId::NONE,
                &mut self.interners,
                &mut self.prim_store,
                &mut self.clip_tree_builder,
                None,
            );

            prims.push(ExtendedPrimitiveInstance {
                instance,
                spatial_node_index: stacking_context.spatial_node_index,
                flags: stacking_context.prim_flags,
            });

            let mut prim_list = PrimitiveList::empty();

            // Web content often specifies `preserve-3d` on pages that don't actually need
            // a 3d rendering context (as a hint / hack to convince other browsers to
            // layerize these elements to an off-screen surface). Detect cases where the
            // preserve-3d has no effect on correctness and convert them to pass-through
            // pictures instead. This has two benefits for WR:
            //
            // (1) We get correct subpixel-snapping behavior between preserve-3d elements
            //     that don't have complex transforms without additional complexity of
            //     handling subpixel-snapping across different surfaces.
            // (2) We can draw this content directly in to the parent surface / tile cache,
            //     which is a performance win by avoiding allocating, drawing,
            //     plane-splitting and blitting an off-screen surface.
            let mut needs_3d_context = false;

            for ext_prim in prims.drain(..) {
                // If all the preserve-3d elements are in the root coordinate system, we
                // know that there is no need for a true 3d rendering context / plane-split.
                // TODO(gw): We can expand this in future to handle this in more cases
                //           (e.g. a non-root coord system that is 2d within the 3d context).
                if !self.spatial_tree.is_root_coord_system(ext_prim.spatial_node_index) {
                    needs_3d_context = true;
                }

                prim_list.add_prim(
                    ext_prim.instance,
                    LayoutRect::zero(),
                    ext_prim.spatial_node_index,
                    ext_prim.flags,
                    &mut self.prim_instances,
                    &self.clip_tree_builder,
                );
            }

            let context_3d = if needs_3d_context {
                Picture3DContext::In {
                    root_data: Some(Vec::new()),
                    ancestor_index,
                    plane_splitter_index,
                }
            } else {
                // If we didn't need a 3d rendering context, walk the child pictures
                // that make up this context and disable the off-screen surface and
                // 3d render context.
                for child_pic_index in &prim_list.child_pictures {
                    let child_pic = &mut self.prim_store.pictures[child_pic_index.0];
                    let needs_surface = child_pic.snapshot.is_some();
                    if !needs_surface {
                        child_pic.composite_mode = None;
                    }
                    child_pic.context_3d = Picture3DContext::Out;
                }

                Picture3DContext::Out
            };

            // This is the acttual picture representing our 3D hierarchy root.
            let pic_index = PictureIndex(self.prim_store.pictures
                .alloc()
                .init(PicturePrimitive::new_image(
                    None,
                    context_3d,
                    stacking_context.prim_flags,
                    prim_list,
                    stacking_context.spatial_node_index,
                    stacking_context.raster_space,
                    PictureFlags::empty(),
                    None,
                ))
            );

            let instance = create_prim_instance(
                pic_index,
                PictureCompositeKey::Identity,
                stacking_context.raster_space,
                stacking_context.clip_node_id,
                &mut self.interners,
                &mut self.clip_tree_builder,
            );

            source = PictureChainBuilder::from_instance(
                instance,
                stacking_context.prim_flags,
                stacking_context.spatial_node_index,
                stacking_context.raster_space,
            );
        }

        let has_filters = stacking_context.composite_ops.has_valid_filters();

        let spatial_node_context_offset =
            stacking_context.subregion_offset +
            self.current_external_scroll_offset(stacking_context.spatial_node_index);
        source = self.wrap_prim_with_filters(
            source,
            stacking_context.clip_node_id,
            stacking_context.composite_ops.filters,
            stacking_context.composite_ops.filter_primitives,
            stacking_context.composite_ops.filter_datas,
            None,
            spatial_node_context_offset,
        );

        // Same for mix-blend-mode, except we can skip if this primitive is the first in the parent
        // stacking context.
        // From https://drafts.fxtf.org/compositing-1/#generalformula, the formula for blending is:
        // Cs = (1 - ab) x Cs + ab x Blend(Cb, Cs)
        // where
        // Cs = Source color
        // ab = Backdrop alpha
        // Cb = Backdrop color
        //
        // If we're the first primitive within a stacking context, then we can guarantee that the
        // backdrop alpha will be 0, and then the blend equation collapses to just
        // Cs = Cs, and the blend mode isn't taken into account at all.
        if let Some(mix_blend_mode) = stacking_context.composite_ops.mix_blend_mode {
            let composite_mode = PictureCompositeMode::MixBlend(mix_blend_mode);

            source = source.add_picture(
                composite_mode,
                stacking_context.clip_node_id,
                Picture3DContext::Out,
                &mut self.interners,
                &mut self.prim_store,
                &mut self.prim_instances,
                &mut self.clip_tree_builder,
            );
        }

        // Set the stacking context clip on the outermost picture in the chain,
        // unless we already set it on the leaf picture.
        let cur_instance = source.finalize(
            stacking_context.clip_node_id,
            &mut self.interners,
            &mut self.prim_store,
            &mut self.clip_tree_builder,
            stacking_context.composite_ops.snapshot,
        );

        if stacking_context.composite_ops.snapshot.is_some() {
            let pic_index = cur_instance.kind.as_pic();
            self.snapshot_pictures.push(pic_index);
        }

        // The primitive instance for the remainder of flat children of this SC
        // if it's a part of 3D hierarchy but not the root of it.
        let trailing_children_instance = match self.sc_stack.last_mut() {
            // Preserve3D path (only relevant if there are no filters/mix-blend modes)
            Some(ref parent_sc) if !has_filters && parent_sc.is_3d() => {
                Some(cur_instance)
            }
            // Regular parenting path
            Some(ref mut parent_sc) => {
                parent_sc.prim_list.add_prim(
                    cur_instance,
                    LayoutRect::zero(),
                    stacking_context.spatial_node_index,
                    stacking_context.prim_flags,
                    &mut self.prim_instances,
                    &self.clip_tree_builder,
                );
                None
            }
            // This must be the root stacking context
            None => {
                self.add_primitive_to_draw_list(
                    cur_instance,
                    LayoutRect::zero(),
                    stacking_context.spatial_node_index,
                    stacking_context.prim_flags,
                );

                None
            }
        };

        // finally, if there any outstanding 3D primitive instances,
        // find the 3D hierarchy root and add them there.
        if let Some(instance) = trailing_children_instance {
            self.add_primitive_instance_to_3d_root(ExtendedPrimitiveInstance {
                instance,
                spatial_node_index: stacking_context.spatial_node_index,
                flags: stacking_context.prim_flags,
            });
        }

        assert!(
            self.pending_shadow_items.is_empty(),
            "Found unpopped shadows when popping stacking context!"
        );

        if info.needs_extra_stacking_context {
            let inner_info = self.extra_stacking_context_stack.pop().unwrap();
            self.pop_stacking_context(inner_info);
        }
    }

    pub fn push_reference_frame(
        &mut self,
        reference_frame_id: SpatialId,
        parent_index: SpatialNodeIndex,
        pipeline_id: PipelineId,
        transform_style: TransformStyle,
        source_transform: PropertyBinding<LayoutTransform>,
        kind: ReferenceFrameKind,
        origin_in_parent_reference_frame: LayoutVector2D,
        uid: SpatialNodeUid,
    ) -> SpatialNodeIndex {
        let index = self.spatial_tree.add_reference_frame(
            parent_index,
            transform_style,
            source_transform,
            kind,
            origin_in_parent_reference_frame,
            pipeline_id,
            uid,
        );
        self.id_to_index_mapper_stack.last_mut().unwrap().add_spatial_node(reference_frame_id, index);

        index
    }

    fn push_root(
        &mut self,
        pipeline_id: PipelineId,
        instance: PipelineInstanceId,
    ) {
        let spatial_node_index = self.push_reference_frame(
            SpatialId::root_reference_frame(pipeline_id),
            self.spatial_tree.root_reference_frame_index(),
            pipeline_id,
            TransformStyle::Flat,
            PropertyBinding::Value(LayoutTransform::identity()),
            ReferenceFrameKind::Transform {
                is_2d_scale_translation: true,
                should_snap: true,
                paired_with_perspective: false,
            },
            LayoutVector2D::zero(),
            SpatialNodeUid::root_reference_frame(pipeline_id, instance),
        );

        let viewport_rect = LayoutRect::max_rect();

        self.add_scroll_frame(
            SpatialId::root_scroll_node(pipeline_id),
            spatial_node_index,
            ExternalScrollId(0, pipeline_id),
            pipeline_id,
            &viewport_rect,
            &viewport_rect.size(),
            ScrollFrameKind::PipelineRoot {
                is_root_pipeline: true,
            },
            LayoutVector2D::zero(),
            APZScrollGeneration::default(),
            HasScrollLinkedEffect::No,
            SpatialNodeUid::root_scroll_frame(pipeline_id, instance),
        );
    }

    fn add_image_mask_clip_node(
        &mut self,
        new_node_id: ClipId,
        spatial_id: SpatialId,
        image_mask: &ImageMask,
        fill_rule: FillRule,
        points_range: ItemRange<LayoutPoint>,
    ) {
        let spatial_node_index = self.get_space(spatial_id);

        let snapped_mask_rect = self.normalize_scroll_offset_and_snap_rect(
            &image_mask.rect,
            spatial_node_index,
        );

        let points: Vec<LayoutPoint> = points_range.iter().collect();

        // If any points are provided, then intern a polygon with the points and fill rule.
        let mut polygon_handle: Option<PolygonDataHandle> = None;
        if points.len() > 0 {
            let item = PolygonKey::new(&points, fill_rule);

            let handle = self
                .interners
                .polygon
                .intern(&item, || item);
            polygon_handle = Some(handle);
        }

        let item = ClipItemKey {
            kind: ClipItemKeyKind::image_mask(image_mask, snapped_mask_rect, polygon_handle),
            spatial_node_index,
        };

        let handle = self
            .interners
            .clip
            .intern(&item, || {
                ClipInternData {
                    key: item,
                }
            });

        self.clip_tree_builder.define_image_mask_clip(
            new_node_id,
            handle,
        );
    }

    /// Add a new rectangle clip, positioned by the spatial node in the `space_and_clip`.
    fn add_rect_clip_node(
        &mut self,
        new_node_id: ClipId,
        spatial_id: SpatialId,
        clip_rect: &LayoutRect,
    ) {
        let spatial_node_index = self.get_space(spatial_id);

        let snapped_clip_rect = self.normalize_scroll_offset_and_snap_rect(
            clip_rect,
            spatial_node_index,
        );

        let item = ClipItemKey {
            kind: ClipItemKeyKind::rectangle(snapped_clip_rect, ClipMode::Clip),
            spatial_node_index,
        };
        let handle = self
            .interners
            .clip
            .intern(&item, || {
                ClipInternData {
                    key: item,
                }
            });

        self.clip_tree_builder.define_rect_clip(
            new_node_id,
            handle,
        );
    }

    fn add_rounded_rect_clip_node(
        &mut self,
        new_node_id: ClipId,
        spatial_id: SpatialId,
        clip: &ComplexClipRegion,
    ) {
        let spatial_node_index = self.get_space(spatial_id);

        let snapped_region_rect = self.normalize_scroll_offset_and_snap_rect(
            &clip.rect,
            spatial_node_index,
        );

        let item = ClipItemKey {
            kind: ClipItemKeyKind::rounded_rect(
                snapped_region_rect,
                clip.radii,
                clip.mode,
            ),
            spatial_node_index,
        };

        let handle = self
            .interners
            .clip
            .intern(&item, || {
                ClipInternData {
                    key: item,
                }
            });

        self.clip_tree_builder.define_rounded_rect_clip(
            new_node_id,
            handle,
        );
    }

    pub fn add_scroll_frame(
        &mut self,
        new_node_id: SpatialId,
        parent_node_index: SpatialNodeIndex,
        external_id: ExternalScrollId,
        pipeline_id: PipelineId,
        frame_rect: &LayoutRect,
        content_size: &LayoutSize,
        frame_kind: ScrollFrameKind,
        external_scroll_offset: LayoutVector2D,
        scroll_offset_generation: APZScrollGeneration,
        has_scroll_linked_effect: HasScrollLinkedEffect,
        uid: SpatialNodeUid,
    ) -> SpatialNodeIndex {
        let node_index = self.spatial_tree.add_scroll_frame(
            parent_node_index,
            external_id,
            pipeline_id,
            frame_rect,
            content_size,
            frame_kind,
            external_scroll_offset,
            scroll_offset_generation,
            has_scroll_linked_effect,
            uid,
        );
        self.id_to_index_mapper_stack.last_mut().unwrap().add_spatial_node(new_node_id, node_index);
        node_index
    }

    pub fn push_shadow(
        &mut self,
        shadow: Shadow,
        spatial_node_index: SpatialNodeIndex,
        clip_chain_id: api::ClipChainId,
        should_inflate: bool,
    ) {
        self.clip_tree_builder.push_clip_chain(Some(clip_chain_id), false);

        // Store this shadow in the pending list, for processing
        // during pop_all_shadows.
        self.pending_shadow_items.push_back(ShadowItem::Shadow(PendingShadow {
            shadow,
            spatial_node_index,
            should_inflate,
        }));
    }

    pub fn pop_all_shadows(
        &mut self,
    ) {
        assert!(!self.pending_shadow_items.is_empty(), "popped shadows, but none were present");

        let mut items = mem::replace(&mut self.pending_shadow_items, VecDeque::new());

        //
        // The pending_shadow_items queue contains a list of shadows and primitives
        // that were pushed during the active shadow context. To process these, we:
        //
        // Iterate the list, popping an item from the front each iteration.
        //
        // If the item is a shadow:
        //      - Create a shadow picture primitive.
        //      - Add *any* primitives that remain in the item list to this shadow.
        // If the item is a primitive:
        //      - Add that primitive as a normal item (if alpha > 0)
        //

        while let Some(item) = items.pop_front() {
            match item {
                ShadowItem::Shadow(pending_shadow) => {
                    // Quote from https://drafts.csswg.org/css-backgrounds-3/#shadow-blur
                    // "the image that would be generated by applying to the shadow a
                    // Gaussian blur with a standard deviation equal to half the blur radius."
                    let std_deviation = pending_shadow.shadow.blur_radius * 0.5;

                    // Add any primitives that come after this shadow in the item
                    // list to this shadow.
                    let mut prim_list = PrimitiveList::empty();
                    let blur_filter = Filter::Blur {
                        width: std_deviation,
                        height: std_deviation,
                        should_inflate: pending_shadow.should_inflate,
                    };
                    let blur_is_noop = blur_filter.is_noop();

                    for item in &items {
                        let (instance, info, spatial_node_index) = match item {
                            ShadowItem::Image(ref pending_image) => {
                                self.create_shadow_prim(
                                    &pending_shadow,
                                    pending_image,
                                    blur_is_noop,
                                )
                            }
                            ShadowItem::LineDecoration(ref pending_line_dec) => {
                                self.create_shadow_prim(
                                    &pending_shadow,
                                    pending_line_dec,
                                    blur_is_noop,
                                )
                            }
                            ShadowItem::NormalBorder(ref pending_border) => {
                                self.create_shadow_prim(
                                    &pending_shadow,
                                    pending_border,
                                    blur_is_noop,
                                )
                            }
                            ShadowItem::Primitive(ref pending_primitive) => {
                                self.create_shadow_prim(
                                    &pending_shadow,
                                    pending_primitive,
                                    blur_is_noop,
                                )
                            }
                            ShadowItem::TextRun(ref pending_text_run) => {
                                self.create_shadow_prim(
                                    &pending_shadow,
                                    pending_text_run,
                                    blur_is_noop,
                                )
                            }
                            _ => {
                                continue;
                            }
                        };

                        if blur_is_noop {
                            self.add_primitive_to_draw_list(
                                instance,
                                info.rect,
                                spatial_node_index,
                                info.flags,
                            );
                        } else {
                            prim_list.add_prim(
                                instance,
                                info.rect,
                                spatial_node_index,
                                info.flags,
                                &mut self.prim_instances,
                                &self.clip_tree_builder,
                            );
                        }
                    }

                    // No point in adding a shadow here if there were no primitives
                    // added to the shadow.
                    if !prim_list.is_empty() {
                        // Create a picture that the shadow primitives will be added to. If the
                        // blur radius is 0, the code in Picture::prepare_for_render will
                        // detect this and mark the picture to be drawn directly into the
                        // parent picture, which avoids an intermediate surface and blur.
                        assert!(!blur_filter.is_noop());
                        let composite_mode = Some(PictureCompositeMode::Filter(blur_filter));
                        let composite_mode_key = composite_mode.clone().into();
                        let raster_space = RasterSpace::Screen;

                        // Create the primitive to draw the shadow picture into the scene.
                        let shadow_pic_index = PictureIndex(self.prim_store.pictures
                            .alloc()
                            .init(PicturePrimitive::new_image(
                                composite_mode,
                                Picture3DContext::Out,
                                PrimitiveFlags::IS_BACKFACE_VISIBLE,
                                prim_list,
                                pending_shadow.spatial_node_index,
                                raster_space,
                                PictureFlags::empty(),
                                None,
                            ))
                        );

                        let shadow_pic_key = PictureKey::new(
                            Picture { composite_mode_key, raster_space },
                        );

                        let shadow_prim_data_handle = self.interners
                            .picture
                            .intern(&shadow_pic_key, || ());

                        let clip_node_id = self.clip_tree_builder.build_clip_set(api::ClipChainId::INVALID);

                        let shadow_prim_instance = PrimitiveInstance::new(
                            PrimitiveInstanceKind::Picture {
                                data_handle: shadow_prim_data_handle,
                                pic_index: shadow_pic_index,
                            },
                            self.clip_tree_builder.build_for_picture(clip_node_id),
                        );

                        // Add the shadow primitive. This must be done before pushing this
                        // picture on to the shadow stack, to avoid infinite recursion!
                        self.add_primitive_to_draw_list(
                            shadow_prim_instance,
                            LayoutRect::zero(),
                            pending_shadow.spatial_node_index,
                            PrimitiveFlags::IS_BACKFACE_VISIBLE,
                        );
                    }

                    self.clip_tree_builder.pop_clip();
                }
                ShadowItem::Image(pending_image) => {
                    self.add_shadow_prim_to_draw_list(
                        pending_image,
                    )
                },
                ShadowItem::LineDecoration(pending_line_dec) => {
                    self.add_shadow_prim_to_draw_list(
                        pending_line_dec,
                    )
                },
                ShadowItem::NormalBorder(pending_border) => {
                    self.add_shadow_prim_to_draw_list(
                        pending_border,
                    )
                },
                ShadowItem::Primitive(pending_primitive) => {
                    self.add_shadow_prim_to_draw_list(
                        pending_primitive,
                    )
                },
                ShadowItem::TextRun(pending_text_run) => {
                    self.add_shadow_prim_to_draw_list(
                        pending_text_run,
                    )
                },
            }
        }

        debug_assert!(items.is_empty());
        self.pending_shadow_items = items;
    }

    fn create_shadow_prim<P>(
        &mut self,
        pending_shadow: &PendingShadow,
        pending_primitive: &PendingPrimitive<P>,
        blur_is_noop: bool,
    ) -> (PrimitiveInstance, LayoutPrimitiveInfo, SpatialNodeIndex)
    where
        P: InternablePrimitive + CreateShadow,
        Interners: AsMut<Interner<P>>,
    {
        // Offset the local rect and clip rect by the shadow offset. The pending
        // primitive has already been snapped, but we will need to snap the
        // shadow after translation. We don't need to worry about the size
        // changing because the shadow has the same raster space as the
        // primitive, and thus we know the size is already rounded.
        let mut info = pending_primitive.info.clone();
        info.rect = info.rect.translate(pending_shadow.shadow.offset);
        info.clip_rect = info.clip_rect.translate(pending_shadow.shadow.offset);

        let clip_set = self.clip_tree_builder.build_for_prim(
            pending_primitive.clip_node_id,
            &info,
            &[],
            &mut self.interners,
        );

        // Construct and add a primitive for the given shadow.
        let shadow_prim_instance = self.create_primitive(
            &info,
            clip_set,
            pending_primitive.prim.create_shadow(
                &pending_shadow.shadow,
                blur_is_noop,
                self.raster_space_stack.last().cloned().unwrap(),
            ),
        );

        (shadow_prim_instance, info, pending_primitive.spatial_node_index)
    }

    fn add_shadow_prim_to_draw_list<P>(
        &mut self,
        pending_primitive: PendingPrimitive<P>,
    ) where
        P: InternablePrimitive + IsVisible,
        Interners: AsMut<Interner<P>>,
    {
        // For a normal primitive, if it has alpha > 0, then we add this
        // as a normal primitive to the parent picture.
        if pending_primitive.prim.is_visible() {
            let clip_set = self.clip_tree_builder.build_for_prim(
                pending_primitive.clip_node_id,
                &pending_primitive.info,
                &[],
                &mut self.interners,
            );

            self.add_prim_to_draw_list(
                &pending_primitive.info,
                pending_primitive.spatial_node_index,
                clip_set,
                pending_primitive.prim,
            );
        }
    }

    pub fn add_clear_rectangle(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        info: &LayoutPrimitiveInfo,
    ) {
        // Clear prims must be in their own picture cache slice to
        // be composited correctly.
        self.add_tile_cache_barrier_if_needed(SliceFlags::empty());

        self.add_primitive(
            spatial_node_index,
            clip_node_id,
            info,
            Vec::new(),
            PrimitiveKeyKind::Clear,
        );

        self.add_tile_cache_barrier_if_needed(SliceFlags::empty());
    }

    pub fn add_line(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        info: &LayoutPrimitiveInfo,
        wavy_line_thickness: f32,
        orientation: LineOrientation,
        color: ColorF,
        style: LineStyle,
    ) {
        // For line decorations, we can construct the render task cache key
        // here during scene building, since it doesn't depend on device
        // pixel ratio or transform.
        let size = get_line_decoration_size(
            &info.rect.size(),
            orientation,
            style,
            wavy_line_thickness,
        );

        let cache_key = size.map(|size| {
            LineDecorationCacheKey {
                style,
                orientation,
                wavy_line_thickness: Au::from_f32_px(wavy_line_thickness),
                size: size.to_au(),
            }
        });

        self.add_primitive(
            spatial_node_index,
            clip_node_id,
            &info,
            Vec::new(),
            LineDecoration {
                cache_key,
                color: color.into(),
            },
        );
    }

    pub fn add_border(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        info: &LayoutPrimitiveInfo,
        border_item: &BorderDisplayItem,
        gradient_stops: ItemRange<GradientStop>,
    ) {
        match border_item.details {
            BorderDetails::NinePatch(ref border) => {
                let nine_patch = NinePatchDescriptor {
                    width: border.width,
                    height: border.height,
                    slice: border.slice,
                    fill: border.fill,
                    repeat_horizontal: border.repeat_horizontal,
                    repeat_vertical: border.repeat_vertical,
                    widths: border_item.widths.into(),
                };

                match border.source {
                    NinePatchBorderSource::Image(key, rendering) => {
                        let prim = ImageBorder {
                            request: ImageRequest {
                                key,
                                rendering,
                                tile: None,
                            },
                            nine_patch,
                        };

                        self.add_nonshadowable_primitive(
                            spatial_node_index,
                            clip_node_id,
                            info,
                            Vec::new(),
                            prim,
                        );
                    }
                    NinePatchBorderSource::Gradient(gradient) => {
                        let prim = match self.create_linear_gradient_prim(
                            &info,
                            gradient.start_point,
                            gradient.end_point,
                            read_gradient_stops(gradient_stops),
                            gradient.extend_mode,
                            LayoutSize::new(border.height as f32, border.width as f32),
                            LayoutSize::zero(),
                            Some(Box::new(nine_patch)),
                            EdgeAaSegmentMask::all(),
                        ) {
                            Some(prim) => prim,
                            None => return,
                        };

                        self.add_nonshadowable_primitive(
                            spatial_node_index,
                            clip_node_id,
                            info,
                            Vec::new(),
                            prim,
                        );
                    }
                    NinePatchBorderSource::RadialGradient(gradient) => {
                        let prim = self.create_radial_gradient_prim(
                            &info,
                            gradient.center,
                            gradient.start_offset * gradient.radius.width,
                            gradient.end_offset * gradient.radius.width,
                            gradient.radius.width / gradient.radius.height,
                            read_gradient_stops(gradient_stops),
                            gradient.extend_mode,
                            LayoutSize::new(border.height as f32, border.width as f32),
                            LayoutSize::zero(),
                            Some(Box::new(nine_patch)),
                        );

                        self.add_nonshadowable_primitive(
                            spatial_node_index,
                            clip_node_id,
                            info,
                            Vec::new(),
                            prim,
                        );
                    }
                    NinePatchBorderSource::ConicGradient(gradient) => {
                        let prim = self.create_conic_gradient_prim(
                            &info,
                            gradient.center,
                            gradient.angle,
                            gradient.start_offset,
                            gradient.end_offset,
                            gradient_stops,
                            gradient.extend_mode,
                            LayoutSize::new(border.height as f32, border.width as f32),
                            LayoutSize::zero(),
                            Some(Box::new(nine_patch)),
                        );

                        self.add_nonshadowable_primitive(
                            spatial_node_index,
                            clip_node_id,
                            info,
                            Vec::new(),
                            prim,
                        );
                    }
                };
            }
            BorderDetails::Normal(ref border) => {
                self.add_normal_border(
                    info,
                    border,
                    border_item.widths,
                    spatial_node_index,
                    clip_node_id,
                );
            }
        }
    }

    pub fn create_linear_gradient_prim(
        &mut self,
        info: &LayoutPrimitiveInfo,
        start_point: LayoutPoint,
        end_point: LayoutPoint,
        stops: Vec<GradientStopKey>,
        extend_mode: ExtendMode,
        stretch_size: LayoutSize,
        mut tile_spacing: LayoutSize,
        nine_patch: Option<Box<NinePatchDescriptor>>,
        edge_aa_mask: EdgeAaSegmentMask,
    ) -> Option<LinearGradient> {
        let mut prim_rect = info.rect;
        simplify_repeated_primitive(&stretch_size, &mut tile_spacing, &mut prim_rect);

        let mut has_hard_stops = false;
        let mut is_entirely_transparent = true;
        let mut prev_stop = None;
        for stop in &stops {
            if Some(stop.offset) == prev_stop {
                has_hard_stops = true;
            }
            prev_stop = Some(stop.offset);
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

        // We set a limit to the resolution at which cached gradients are rendered.
        // For most gradients this is fine but when there are hard stops this causes
        // noticeable artifacts. If so, fall back to non-cached gradients.
        let max = gradient::LINEAR_MAX_CACHED_SIZE;
        let caching_causes_artifacts = has_hard_stops && (stretch_size.width > max || stretch_size.height > max);

        let is_tiled = prim_rect.width() > stretch_size.width
         || prim_rect.height() > stretch_size.height;
        // SWGL has a fast-path that can render gradients faster than it can sample from the
        // texture cache so we disable caching in this configuration. Cached gradients are
        // faster on hardware.
        let cached = (!self.config.is_software || is_tiled) && !caching_causes_artifacts;

        Some(LinearGradient {
            extend_mode,
            start_point: sp.into(),
            end_point: ep.into(),
            stretch_size: stretch_size.into(),
            tile_spacing: tile_spacing.into(),
            stops,
            reverse_stops,
            nine_patch,
            cached,
            edge_aa_mask,
        })
    }

    pub fn create_radial_gradient_prim(
        &mut self,
        info: &LayoutPrimitiveInfo,
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
        let mut prim_rect = info.rect;
        simplify_repeated_primitive(&stretch_size, &mut tile_spacing, &mut prim_rect);

        let params = RadialGradientParams {
            start_radius,
            end_radius,
            ratio_xy,
        };

        RadialGradient {
            extend_mode,
            center: center.into(),
            params,
            stretch_size: stretch_size.into(),
            tile_spacing: tile_spacing.into(),
            nine_patch,
            stops,
        }
    }

    pub fn create_conic_gradient_prim(
        &mut self,
        info: &LayoutPrimitiveInfo,
        center: LayoutPoint,
        angle: f32,
        start_offset: f32,
        end_offset: f32,
        stops: ItemRange<GradientStop>,
        extend_mode: ExtendMode,
        stretch_size: LayoutSize,
        mut tile_spacing: LayoutSize,
        nine_patch: Option<Box<NinePatchDescriptor>>,
    ) -> ConicGradient {
        let mut prim_rect = info.rect;
        simplify_repeated_primitive(&stretch_size, &mut tile_spacing, &mut prim_rect);

        let stops = stops.iter().map(|stop| {
            GradientStopKey {
                offset: stop.offset,
                color: stop.color.into(),
            }
        }).collect();

        ConicGradient {
            extend_mode,
            center: center.into(),
            params: ConicGradientParams { angle, start_offset, end_offset },
            stretch_size: stretch_size.into(),
            tile_spacing: tile_spacing.into(),
            nine_patch,
            stops,
        }
    }

    pub fn add_text(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        prim_info: &LayoutPrimitiveInfo,
        font_instance_key: &FontInstanceKey,
        text_color: &ColorF,
        glyph_range: ItemRange<GlyphInstance>,
        glyph_options: Option<GlyphOptions>,
        ref_frame_offset: LayoutVector2D,
    ) {
        let offset = self.current_external_scroll_offset(spatial_node_index) + ref_frame_offset;

        let text_run = {
            let shared_key = self.fonts.instance_keys.map_key(font_instance_key);
            let font_instance = match self.fonts.instances.get_font_instance(shared_key) {
                Some(instance) => instance,
                None => {
                    warn!("Unknown font instance key");
                    debug!("key={:?} shared={:?}", font_instance_key, shared_key);
                    return;
                }
            };

            // Trivial early out checks
            if font_instance.size <= FontSize::zero() {
                return;
            }

            // TODO(gw): Use a proper algorithm to select
            // whether this item should be rendered with
            // subpixel AA!
            let mut render_mode = self.config
                .default_font_render_mode
                .limit_by(font_instance.render_mode);
            let mut flags = font_instance.flags;
            if let Some(options) = glyph_options {
                render_mode = render_mode.limit_by(options.render_mode);
                flags |= options.flags;
            }

            let font = FontInstance::new(
                font_instance,
                (*text_color).into(),
                render_mode,
                flags,
            );

            // TODO(gw): It'd be nice not to have to allocate here for creating
            //           the primitive key, when the common case is that the
            //           hash will match and we won't end up creating a new
            //           primitive template.
            let prim_offset = prim_info.rect.min.to_vector() - offset;
            let glyphs = glyph_range
                .iter()
                .map(|glyph| {
                    GlyphInstance {
                        index: glyph.index,
                        point: glyph.point - prim_offset,
                    }
                })
                .collect();

            // Query the current requested raster space (stack handled by push/pop
            // stacking context).
            let requested_raster_space = self.raster_space_stack
                .last()
                .cloned()
                .unwrap();

            TextRun {
                glyphs: Arc::new(glyphs),
                font,
                shadow: false,
                requested_raster_space,
                reference_frame_offset: ref_frame_offset,
            }
        };

        self.add_primitive(
            spatial_node_index,
            clip_node_id,
            prim_info,
            Vec::new(),
            text_run,
        );
    }

    pub fn add_image(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        info: &LayoutPrimitiveInfo,
        stretch_size: LayoutSize,
        mut tile_spacing: LayoutSize,
        image_key: ImageKey,
        image_rendering: ImageRendering,
        alpha_type: AlphaType,
        color: ColorF,
    ) {
        let mut prim_rect = info.rect;
        simplify_repeated_primitive(&stretch_size, &mut tile_spacing, &mut prim_rect);
        let info = LayoutPrimitiveInfo {
            rect: prim_rect,
            .. *info
        };

        self.add_primitive(
            spatial_node_index,
            clip_node_id,
            &info,
            Vec::new(),
            Image {
                key: image_key,
                tile_spacing: tile_spacing.into(),
                stretch_size: stretch_size.into(),
                color: color.into(),
                image_rendering,
                alpha_type,
            },
        );
    }

    pub fn add_yuv_image(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        info: &LayoutPrimitiveInfo,
        yuv_data: YuvData,
        color_depth: ColorDepth,
        color_space: YuvColorSpace,
        color_range: ColorRange,
        image_rendering: ImageRendering,
    ) {
        let format = yuv_data.get_format();
        let yuv_key = match yuv_data {
            YuvData::NV12(plane_0, plane_1) => [plane_0, plane_1, ImageKey::DUMMY],
            YuvData::P010(plane_0, plane_1) => [plane_0, plane_1, ImageKey::DUMMY],
            YuvData::NV16(plane_0, plane_1) => [plane_0, plane_1, ImageKey::DUMMY],
            YuvData::PlanarYCbCr(plane_0, plane_1, plane_2) => [plane_0, plane_1, plane_2],
            YuvData::InterleavedYCbCr(plane_0) => [plane_0, ImageKey::DUMMY, ImageKey::DUMMY],
        };

        self.add_nonshadowable_primitive(
            spatial_node_index,
            clip_node_id,
            info,
            Vec::new(),
            YuvImage {
                color_depth,
                yuv_key,
                format,
                color_space,
                color_range,
                image_rendering,
            },
        );
    }

    fn add_primitive_instance_to_3d_root(
        &mut self,
        prim: ExtendedPrimitiveInstance,
    ) {
        // find the 3D root and append to the children list
        for sc in self.sc_stack.iter_mut().rev() {
            match sc.context_3d {
                Picture3DContext::In { root_data: Some(ref mut prims), .. } => {
                    prims.push(prim);
                    break;
                }
                Picture3DContext::In { .. } => {}
                Picture3DContext::Out => panic!("Unable to find 3D root"),
            }
        }
    }

    #[allow(dead_code)]
    pub fn add_backdrop_filter(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        info: &LayoutPrimitiveInfo,
        filters: Vec<Filter>,
        filter_datas: Vec<FilterData>,
        filter_primitives: Vec<FilterPrimitive>,
    ) {
        // We don't know the spatial node for a backdrop filter, as it's whatever is the
        // backdrop root, but we can't know this if the root is a picture cache slice
        // (which is the common case). It will get resolved later during `finalize_picture`.
        let filter_spatial_node_index = SpatialNodeIndex::UNKNOWN;

        self.make_current_slice_atomic_if_required();

        // Ensure we create a clip-chain for the capture primitive that matches
        // the render primitive, otherwise one might get culled while the other
        // is considered visible.
        let clip_leaf_id = self.clip_tree_builder.build_for_prim(
            clip_node_id,
            info,
            &[],
            &mut self.interners,
        );

        // Create the backdrop prim - this is a placeholder which sets the size of resolve
        // picture that reads from the backdrop root
        let backdrop_capture_instance = self.create_primitive(
            info,
            clip_leaf_id,
            BackdropCapture {
            },
        );

        // Create a prim_list for this backdrop prim and add to a picture chain builder, which
        // is needed for the call to `wrap_prim_with_filters` below
        let mut prim_list = PrimitiveList::empty();
        prim_list.add_prim(
            backdrop_capture_instance,
            info.rect,
            spatial_node_index,
            info.flags,
            &mut self.prim_instances,
            &self.clip_tree_builder,
        );

        let mut source = PictureChainBuilder::from_prim_list(
            prim_list,
            info.flags,
            filter_spatial_node_index,
            RasterSpace::Screen,
            true,
        );

        // Wrap the backdrop primitive picture with the filters that were specified. This
        // produces a picture chain with 1+ pictures with the filter composite modes set.
        source = self.wrap_prim_with_filters(
            source,
            clip_node_id,
            filters,
            filter_primitives,
            filter_datas,
            Some(false),
            LayoutVector2D::zero(),
        );

        // If all the filters were no-ops (e.g. opacity(0)) then we don't get a picture here
        // and we can skip adding the backdrop-filter.
        if source.has_picture() {
            source = source.add_picture(
                PictureCompositeMode::IntermediateSurface,
                clip_node_id,
                Picture3DContext::Out,
                &mut self.interners,
                &mut self.prim_store,
                &mut self.prim_instances,
                &mut self.clip_tree_builder,
            );

            let filtered_instance = source.finalize(
                clip_node_id,
                &mut self.interners,
                &mut self.prim_store,
                &mut self.clip_tree_builder,
                None,
            );

            // Extract the pic index for the intermediate surface. We need to
            // supply this to the capture prim below.
            let output_pic_index = match filtered_instance.kind {
                PrimitiveInstanceKind::Picture { pic_index, .. } => pic_index,
                _ => panic!("bug: not a picture"),
            };

            // Find which stacking context (or root tile cache) to add the
            // backdrop-filter chain to
            let sc_index = self.sc_stack.iter().rposition(|sc| {
                !sc.flags.contains(StackingContextFlags::WRAPS_BACKDROP_FILTER)
            });

            match sc_index {
                Some(sc_index) => {
                    self.sc_stack[sc_index].prim_list.add_prim(
                        filtered_instance,
                        info.rect,
                        filter_spatial_node_index,
                        info.flags,
                        &mut self.prim_instances,
                        &self.clip_tree_builder,
                    );
                }
                None => {
                    self.tile_cache_builder.add_prim(
                        filtered_instance,
                        info.rect,
                        filter_spatial_node_index,
                        info.flags,
                        self.spatial_tree,
                        self.interners,
                        &self.quality_settings,
                        &mut self.prim_instances,
                        &self.clip_tree_builder,
                    );
                }
            }

            // Add the prim that renders the result of the backdrop filter chain
            let mut backdrop_render_instance = self.create_primitive(
                info,
                clip_leaf_id,
                BackdropRender {
                },
            );

            // Set up the picture index for the backdrop-filter output in the prim
            // that will draw it
            match backdrop_render_instance.kind {
                PrimitiveInstanceKind::BackdropRender { ref mut pic_index, .. } => {
                    assert_eq!(*pic_index, PictureIndex::INVALID);
                    *pic_index = output_pic_index;
                }
                _ => panic!("bug: unexpected prim kind"),
            }

            self.add_primitive_to_draw_list(
                backdrop_render_instance,
                info.rect,
                spatial_node_index,
                info.flags,
            );
        }
    }

    #[must_use]
    fn wrap_prim_with_filters(
        &mut self,
        mut source: PictureChainBuilder,
        clip_node_id: ClipNodeId,
        mut filter_ops: Vec<Filter>,
        mut filter_primitives: Vec<FilterPrimitive>,
        filter_datas: Vec<FilterData>,
        should_inflate_override: Option<bool>,
        context_offset: LayoutVector2D,
    ) -> PictureChainBuilder {
        // TODO(cbrewster): Currently CSS and SVG filters live side by side in WebRender, but unexpected results will
        // happen if they are used simulataneously. Gecko only provides either filter ops or filter primitives.
        // At some point, these two should be combined and CSS filters should be expressed in terms of SVG filters.
        assert!(filter_ops.is_empty() || filter_primitives.is_empty(),
            "Filter ops and filter primitives are not allowed on the same stacking context.");

        // For each filter, create a new image with that composite mode.
        let mut current_filter_data_index = 0;
        // Check if the filter chain is actually an SVGFE filter graph DAG
        //
        // TODO: We technically could translate all CSS filters to SVGFE here if
        // we want to reduce redundant code.
        if let Some(Filter::SVGGraphNode(..)) = filter_ops.first() {
            // The interesting parts of the handling of SVG filters are:
            // * scene_building.rs : wrap_prim_with_filters (you are here)
            // * picture.rs : get_coverage_svgfe
            // * render_task.rs : new_svg_filter_graph
            // * render_target.rs : add_svg_filter_node_instances

            // The SVG spec allows us to drop the entire filter graph if it is
            // unreasonable, so we limit the number of filters in a graph
            const BUFFER_LIMIT: usize = SVGFE_GRAPH_MAX;
            // Easily tunable for debugging proper handling of inflated rects,
            // this should normally be 1
            const SVGFE_INFLATE: i16 = 1;

            // Validate inputs to all filters.
            //
            // Several assumptions can be made about the DAG:
            // * All filters take a specific number of inputs (feMerge is not
            //   supported, the code that built the display items had to convert
            //   any feMerge ops to SVGFECompositeOver already).
            // * All input buffer ids are < the output buffer id of the node.
            // * If SourceGraphic or SourceAlpha are used, they are standalone
            //   nodes with no inputs.
            // * Whenever subregion of a node is smaller than the subregion
            //   of the inputs, it is a deliberate clip of those inputs to the
            //   new rect, this can occur before/after blur and dropshadow for
            //   example, so we must explicitly handle subregion correctly, but
            //   we do not have to allocate the unused pixels as the transparent
            //   black has no efect on any of the filters, only certain filters
            //   like feFlood can generate something from nothing.
            // * Coordinate basis of the graph has to be adjusted by
            //   context_offset to put the subregions in the same space that the
            //   primitives are in, as they do that offset as well.
            let mut reference_for_buffer_id: [FilterGraphPictureReference; BUFFER_LIMIT] = [
                FilterGraphPictureReference{
                    // This value is deliberately invalid, but not a magic
                    // number, it's just this way to guarantee an assertion
                    // failure if something goes wrong.
                    buffer_id: FilterOpGraphPictureBufferId::BufferId(-1),
                    subregion: LayoutRect::zero(), // Always overridden
                    offset: LayoutVector2D::zero(),
                    inflate: 0,
                    source_padding: LayoutRect::zero(),
                    target_padding: LayoutRect::zero(),
                }; BUFFER_LIMIT];
            let mut filters: Vec<(FilterGraphNode, FilterGraphOp)> = Vec::new();
            filters.reserve(BUFFER_LIMIT);
            for (original_id, parsefilter) in filter_ops.iter().enumerate() {
                if filters.len() >= BUFFER_LIMIT {
                    // If the DAG is too large to process, the spec requires
                    // that we drop all filters and display source image as-is.
                    return source;
                }

                let newfilter = match parsefilter {
                    Filter::SVGGraphNode(parsenode, op) => {
                        // We need to offset the subregion by the stacking context
                        // offset or we'd be in the wrong coordinate system, prims
                        // are already offset by this same amount.
                        let clip_region = parsenode.subregion
                            .translate(context_offset);

                        let mut newnode = FilterGraphNode {
                            kept_by_optimizer: false,
                            linear: parsenode.linear,
                            inflate: SVGFE_INFLATE,
                            inputs: Vec::new(),
                            subregion: clip_region,
                        };

                        // Initialize remapped versions of the inputs, this is
                        // done here to share code between the enum variants.
                        let mut remapped_inputs: Vec<FilterGraphPictureReference> = Vec::new();
                        remapped_inputs.reserve_exact(parsenode.inputs.len());
                        for input in &parsenode.inputs {
                            match input.buffer_id {
                                FilterOpGraphPictureBufferId::BufferId(buffer_id) => {
                                    // Reference to earlier node output, if this
                                    // is None, it's a bug
                                    let pic = *reference_for_buffer_id
                                        .get(buffer_id as usize)
                                        .expect("BufferId not valid?");
                                    // We have to adjust the subregion and
                                    // padding based on the input offset for
                                    // feOffset ops, the padding may be inflated
                                    // further by other ops such as blurs below.
                                    let offset = input.offset;
                                    let subregion = pic.subregion
                                        .translate(offset);
                                    let source_padding = LayoutRect::zero()
                                        .translate(-offset);
                                    let target_padding = LayoutRect::zero()
                                        .translate(offset);
                                    remapped_inputs.push(
                                        FilterGraphPictureReference {
                                            buffer_id: pic.buffer_id,
                                            subregion,
                                            offset,
                                            inflate: pic.inflate,
                                            source_padding,
                                            target_padding,
                                        });
                                }
                                FilterOpGraphPictureBufferId::None => panic!("Unsupported FilterOpGraphPictureBufferId"),
                            }
                        }

                        fn union_unchecked(a: LayoutRect, b: LayoutRect) -> LayoutRect {
                            let mut r = a;
                            if r.min.x > b.min.x {r.min.x = b.min.x}
                            if r.min.y > b.min.y {r.min.y = b.min.y}
                            if r.max.x < b.max.x {r.max.x = b.max.x}
                            if r.max.y < b.max.y {r.max.y = b.max.y}
                            r
                        }

                        match op {
                            FilterGraphOp::SVGFEFlood{..} |
                            FilterGraphOp::SVGFESourceAlpha |
                            FilterGraphOp::SVGFESourceGraphic |
                            FilterGraphOp::SVGFETurbulenceWithFractalNoiseWithNoStitching{..} |
                            FilterGraphOp::SVGFETurbulenceWithFractalNoiseWithStitching{..} |
                            FilterGraphOp::SVGFETurbulenceWithTurbulenceNoiseWithNoStitching{..} |
                            FilterGraphOp::SVGFETurbulenceWithTurbulenceNoiseWithStitching{..} => {
                                assert!(remapped_inputs.len() == 0);
                                (newnode.clone(), op.clone())
                            }
                            FilterGraphOp::SVGFEColorMatrix{..} |
                            FilterGraphOp::SVGFEIdentity |
                            FilterGraphOp::SVGFEImage{..} |
                            FilterGraphOp::SVGFEOpacity{..} |
                            FilterGraphOp::SVGFEToAlpha => {
                                assert!(remapped_inputs.len() == 1);
                                newnode.inputs = remapped_inputs;
                                (newnode.clone(), op.clone())
                            }
                            FilterGraphOp::SVGFEComponentTransfer => {
                                assert!(remapped_inputs.len() == 1);
                                // Convert to SVGFEComponentTransferInterned
                                let filter_data =
                                    &filter_datas[current_filter_data_index];
                                let filter_data = filter_data.sanitize();
                                current_filter_data_index = current_filter_data_index + 1;

                                // filter data is 4KiB of gamma ramps used
                                // only by SVGFEComponentTransferWithHandle.
                                //
                                // The gamma ramps are interleaved as RGBA32F
                                // pixels (unlike in regular ComponentTransfer,
                                // where the values are not interleaved), so
                                // r_values[3] is the alpha of the first color,
                                // not the 4th red value.  This layout makes the
                                // shader more compatible with buggy compilers that
                                // do not like indexing components on a vec4.
                                let creates_pixels =
                                    if let Some(a) = filter_data.r_values.get(3) {
                                        *a != 0.0
                                    } else {
                                        false
                                    };
                                let filter_data_key = SFilterDataKey {
                                    data:
                                        SFilterData {
                                            r_func: SFilterDataComponent::from_functype_values(
                                                filter_data.func_r_type, &filter_data.r_values),
                                            g_func: SFilterDataComponent::from_functype_values(
                                                filter_data.func_g_type, &filter_data.g_values),
                                            b_func: SFilterDataComponent::from_functype_values(
                                                filter_data.func_b_type, &filter_data.b_values),
                                            a_func: SFilterDataComponent::from_functype_values(
                                                filter_data.func_a_type, &filter_data.a_values),
                                        },
                                };

                                let handle = self.interners
                                    .filter_data
                                    .intern(&filter_data_key, || ());

                                newnode.inputs = remapped_inputs;
                                (newnode.clone(), FilterGraphOp::SVGFEComponentTransferInterned{handle, creates_pixels})
                            }
                            FilterGraphOp::SVGFEComponentTransferInterned{..} => unreachable!(),
                            FilterGraphOp::SVGFETile => {
                                assert!(remapped_inputs.len() == 1);
                                // feTile usually uses every pixel of input
                                remapped_inputs[0].source_padding =
                                    LayoutRect::max_rect();
                                remapped_inputs[0].target_padding =
                                    LayoutRect::max_rect();
                                newnode.inputs = remapped_inputs;
                                (newnode.clone(), op.clone())
                            }
                            FilterGraphOp::SVGFEConvolveMatrixEdgeModeDuplicate{kernel_unit_length_x, kernel_unit_length_y, ..} |
                            FilterGraphOp::SVGFEConvolveMatrixEdgeModeNone{kernel_unit_length_x, kernel_unit_length_y, ..} |
                            FilterGraphOp::SVGFEConvolveMatrixEdgeModeWrap{kernel_unit_length_x, kernel_unit_length_y, ..} |
                            FilterGraphOp::SVGFEMorphologyDilate{radius_x: kernel_unit_length_x, radius_y: kernel_unit_length_y} => {
                                assert!(remapped_inputs.len() == 1);
                                let padding = LayoutSize::new(
                                    kernel_unit_length_x.ceil(),
                                    kernel_unit_length_y.ceil(),
                                );
                                // Add source padding to represent the kernel pixels
                                // needed relative to target pixels
                                remapped_inputs[0].source_padding =
                                    remapped_inputs[0].source_padding
                                    .inflate(padding.width, padding.height);
                                // Add target padding to represent the area affected
                                // by a source pixel
                                remapped_inputs[0].target_padding =
                                    remapped_inputs[0].target_padding
                                    .inflate(padding.width, padding.height);
                                newnode.inputs = remapped_inputs;
                                (newnode.clone(), op.clone())
                            },
                            FilterGraphOp::SVGFEDiffuseLightingDistant{kernel_unit_length_x, kernel_unit_length_y, ..} |
                            FilterGraphOp::SVGFEDiffuseLightingPoint{kernel_unit_length_x, kernel_unit_length_y, ..} |
                            FilterGraphOp::SVGFEDiffuseLightingSpot{kernel_unit_length_x, kernel_unit_length_y, ..} |
                            FilterGraphOp::SVGFESpecularLightingDistant{kernel_unit_length_x, kernel_unit_length_y, ..} |
                            FilterGraphOp::SVGFESpecularLightingPoint{kernel_unit_length_x, kernel_unit_length_y, ..} |
                            FilterGraphOp::SVGFESpecularLightingSpot{kernel_unit_length_x, kernel_unit_length_y, ..} |
                            FilterGraphOp::SVGFEMorphologyErode{radius_x: kernel_unit_length_x, radius_y: kernel_unit_length_y} => {
                                assert!(remapped_inputs.len() == 1);
                                let padding = LayoutSize::new(
                                    kernel_unit_length_x.ceil(),
                                    kernel_unit_length_y.ceil(),
                                );
                                // Add source padding to represent the kernel pixels
                                // needed relative to target pixels
                                remapped_inputs[0].source_padding =
                                    remapped_inputs[0].source_padding
                                    .inflate(padding.width, padding.height);
                                // Add target padding to represent the area affected
                                // by a source pixel
                                remapped_inputs[0].target_padding =
                                    remapped_inputs[0].target_padding
                                    .inflate(padding.width, padding.height);
                                newnode.inputs = remapped_inputs;
                                (newnode.clone(), op.clone())
                            },
                            FilterGraphOp::SVGFEDisplacementMap { scale, .. } => {
                                assert!(remapped_inputs.len() == 2);
                                let padding = LayoutSize::new(
                                    scale.ceil(),
                                    scale.ceil(),
                                );
                                // Add padding to both inputs for source and target
                                // rects, we might be able to skip some of these,
                                // but it's not that important to optimize here, a
                                // loose fit is fine.
                                remapped_inputs[0].source_padding =
                                    remapped_inputs[0].source_padding
                                    .inflate(padding.width, padding.height);
                                remapped_inputs[1].source_padding =
                                    remapped_inputs[1].source_padding
                                    .inflate(padding.width, padding.height);
                                remapped_inputs[0].target_padding =
                                    remapped_inputs[0].target_padding
                                    .inflate(padding.width, padding.height);
                                remapped_inputs[1].target_padding =
                                    remapped_inputs[1].target_padding
                                    .inflate(padding.width, padding.height);
                                newnode.inputs = remapped_inputs;
                                (newnode.clone(), op.clone())
                            },
                            FilterGraphOp::SVGFEDropShadow{ dx, dy, std_deviation_x, std_deviation_y, .. } => {
                                assert!(remapped_inputs.len() == 1);
                                let padding = LayoutSize::new(
                                    std_deviation_x.ceil() * BLUR_SAMPLE_SCALE,
                                    std_deviation_y.ceil() * BLUR_SAMPLE_SCALE,
                                );
                                // Add source padding to represent the shadow
                                remapped_inputs[0].source_padding =
                                    union_unchecked(
                                        remapped_inputs[0].source_padding,
                                        remapped_inputs[0].source_padding
                                            .inflate(padding.width, padding.height)
                                            .translate(
                                                LayoutVector2D::new(-dx, -dy)
                                            )
                                    );
                                // Add target padding to represent the area needed
                                // to calculate pixels of the shadow
                                remapped_inputs[0].target_padding =
                                    union_unchecked(
                                        remapped_inputs[0].target_padding,
                                        remapped_inputs[0].target_padding
                                            .inflate(padding.width, padding.height)
                                            .translate(
                                                LayoutVector2D::new(*dx, *dy)
                                            )
                                    );
                                newnode.inputs = remapped_inputs;
                                (newnode.clone(), op.clone())
                            },
                            FilterGraphOp::SVGFEGaussianBlur{std_deviation_x, std_deviation_y} => {
                                assert!(remapped_inputs.len() == 1);
                                let padding = LayoutSize::new(
                                    std_deviation_x.ceil() * BLUR_SAMPLE_SCALE,
                                    std_deviation_y.ceil() * BLUR_SAMPLE_SCALE,
                                );
                                // Add source padding to represent the blur
                                remapped_inputs[0].source_padding =
                                    remapped_inputs[0].source_padding
                                    .inflate(padding.width, padding.height);
                                // Add target padding to represent the blur
                                remapped_inputs[0].target_padding =
                                    remapped_inputs[0].target_padding
                                    .inflate(padding.width, padding.height);
                                newnode.inputs = remapped_inputs;
                                (newnode.clone(), op.clone())
                            }
                            FilterGraphOp::SVGFEBlendColor |
                            FilterGraphOp::SVGFEBlendColorBurn |
                            FilterGraphOp::SVGFEBlendColorDodge |
                            FilterGraphOp::SVGFEBlendDarken |
                            FilterGraphOp::SVGFEBlendDifference |
                            FilterGraphOp::SVGFEBlendExclusion |
                            FilterGraphOp::SVGFEBlendHardLight |
                            FilterGraphOp::SVGFEBlendHue |
                            FilterGraphOp::SVGFEBlendLighten |
                            FilterGraphOp::SVGFEBlendLuminosity|
                            FilterGraphOp::SVGFEBlendMultiply |
                            FilterGraphOp::SVGFEBlendNormal |
                            FilterGraphOp::SVGFEBlendOverlay |
                            FilterGraphOp::SVGFEBlendSaturation |
                            FilterGraphOp::SVGFEBlendScreen |
                            FilterGraphOp::SVGFEBlendSoftLight |
                            FilterGraphOp::SVGFECompositeArithmetic{..} |
                            FilterGraphOp::SVGFECompositeATop |
                            FilterGraphOp::SVGFECompositeIn |
                            FilterGraphOp::SVGFECompositeLighter |
                            FilterGraphOp::SVGFECompositeOut |
                            FilterGraphOp::SVGFECompositeOver |
                            FilterGraphOp::SVGFECompositeXOR => {
                                assert!(remapped_inputs.len() == 2);
                                newnode.inputs = remapped_inputs;
                                (newnode, op.clone())
                            }
                        }
                    }
                    Filter::Opacity(valuebinding, value) => {
                        // Opacity filter is sometimes appended by
                        // wr_dp_push_stacking_context before we get here,
                        // convert to SVGFEOpacity in the graph.  Note that
                        // linear is set to false because it has no meaning for
                        // opacity (which scales all of the RGBA uniformly).
                        let pic = reference_for_buffer_id[original_id as usize - 1];
                        (
                            FilterGraphNode {
                                kept_by_optimizer: false,
                                linear: false,
                                inflate: SVGFE_INFLATE,
                                inputs: [pic].to_vec(),
                                subregion: pic.subregion,
                            },
                            FilterGraphOp::SVGFEOpacity{
                                valuebinding: *valuebinding,
                                value: *value,
                            },
                        )
                    }
                    _ => {
                        log!(Level::Warn, "wrap_prim_with_filters: unexpected filter after SVG filters filter[{:?}]={:?}", original_id, parsefilter);
                        // If we can't figure out how to process the graph, spec
                        // requires that we drop all filters and display source
                        // image as-is.
                        return source;
                    }
                };
                let id = filters.len();
                filters.push(newfilter);

                // Set the reference remapping for the last (or only) node
                // that we just pushed
                reference_for_buffer_id[original_id] = FilterGraphPictureReference {
                    buffer_id: FilterOpGraphPictureBufferId::BufferId(id as i16),
                    subregion: filters[id].0.subregion,
                    offset: LayoutVector2D::zero(),
                    inflate: filters[id].0.inflate,
                    source_padding: LayoutRect::zero(),
                    target_padding: LayoutRect::zero(),
                };
            }

            if filters.len() >= BUFFER_LIMIT {
                // If the DAG is too large to process, the spec requires
                // that we drop all filters and display source image as-is.
                return source;
            }

            // Mark used graph nodes, starting at the last graph node, since
            // this is a DAG in sorted order we can just iterate backwards and
            // know we will find children before parents in order.
            //
            // Per SVG spec the last node (which is the first we encounter this
            // way) is the final output, so its dependencies are what we want to
            // mark as kept_by_optimizer
            let mut kept_node_by_buffer_id = [false; BUFFER_LIMIT];
            kept_node_by_buffer_id[filters.len() - 1] = true;
            for (index, (node, _op)) in filters.iter_mut().enumerate().rev() {
                let mut keep = false;
                // Check if this node's output was marked to be kept
                if let Some(k) = kept_node_by_buffer_id.get(index) {
                    if *k {
                        keep = true;
                    }
                }
                if keep {
                    // If this node contributes to the final output we need
                    // to mark its inputs as also contributing when they are
                    // encountered later
                    node.kept_by_optimizer = true;
                    for input in &node.inputs {
                        if let FilterOpGraphPictureBufferId::BufferId(id) = input.buffer_id {
                            if let Some(k) = kept_node_by_buffer_id.get_mut(id as usize) {
                                *k = true;
                            }
                        }
                    }
                }
            }

            // Validate the DAG nature of the graph - if we find anything wrong
            // here it means the above code is bugged.
            let mut invalid_dag = false;
            for (id, (node, _op)) in filters.iter().enumerate() {
                for input in &node.inputs {
                    if let FilterOpGraphPictureBufferId::BufferId(buffer_id) = input.buffer_id {
                        if buffer_id < 0 || buffer_id as usize >= id {
                            invalid_dag = true;
                        }
                    }
                }
            }

            if invalid_dag {
                log!(Level::Warn, "List of FilterOp::SVGGraphNode filter primitives appears to be invalid!");
                for (id, (node, op)) in filters.iter().enumerate() {
                    log!(Level::Warn, " node:     buffer=BufferId({}) op={} inflate={} subregion {:?} linear={} kept={}",
                         id, op.kind(), node.inflate,
                         node.subregion,
                         node.linear,
                         node.kept_by_optimizer,
                    );
                    for input in &node.inputs {
                        log!(Level::Warn, "input: buffer={} inflate={} subregion {:?} offset {:?} target_padding={:?} source_padding={:?}",
                            match input.buffer_id {
                                FilterOpGraphPictureBufferId::BufferId(id) => format!("BufferId({})", id),
                                FilterOpGraphPictureBufferId::None => "None".into(),
                            },
                            input.inflate,
                            input.subregion,
                            input.offset,
                            input.target_padding,
                            input.source_padding,
                        );
                    }
                }
            }
            if invalid_dag {
                // if the DAG is invalid, we can't render it
                return source;
            }

            let composite_mode = PictureCompositeMode::SVGFEGraph(
                filters,
            );

            source = source.add_picture(
                composite_mode,
                clip_node_id,
                Picture3DContext::Out,
                &mut self.interners,
                &mut self.prim_store,
                &mut self.prim_instances,
                &mut self.clip_tree_builder,
            );

            return source;
        }

        // Handle regular CSS filter chains
        for filter in &mut filter_ops {
            let composite_mode = match filter {
                Filter::ComponentTransfer => {
                    let filter_data =
                        &filter_datas[current_filter_data_index];
                    let filter_data = filter_data.sanitize();
                    current_filter_data_index = current_filter_data_index + 1;
                    if filter_data.is_identity() {
                        continue
                    } else {
                        let filter_data_key = SFilterDataKey {
                            data:
                                SFilterData {
                                    r_func: SFilterDataComponent::from_functype_values(
                                        filter_data.func_r_type, &filter_data.r_values),
                                    g_func: SFilterDataComponent::from_functype_values(
                                        filter_data.func_g_type, &filter_data.g_values),
                                    b_func: SFilterDataComponent::from_functype_values(
                                        filter_data.func_b_type, &filter_data.b_values),
                                    a_func: SFilterDataComponent::from_functype_values(
                                        filter_data.func_a_type, &filter_data.a_values),
                                },
                        };

                        let handle = self.interners
                            .filter_data
                            .intern(&filter_data_key, || ());
                        PictureCompositeMode::ComponentTransferFilter(handle)
                    }
                }
                Filter::SVGGraphNode(_, _) => {
                    // SVG filter graphs were handled above
                    panic!("SVGGraphNode encountered in regular CSS filter chain?");
                }
                _ => {
                    if filter.is_noop() {
                        continue;
                    } else {
                        let mut filter = filter.clone();

                        // backdrop-filter spec says that blurs should assume edgeMode=Duplicate
                        // We can do this by not inflating the bounds, which means the blur
                        // shader will duplicate pixels outside the sample rect
                        if let Some(should_inflate_override) = should_inflate_override {
                            if let Filter::Blur { ref mut should_inflate, .. } = filter {
                                *should_inflate = should_inflate_override;
                            }
                        }

                        PictureCompositeMode::Filter(filter)
                    }
                }
            };

            source = source.add_picture(
                composite_mode,
                clip_node_id,
                Picture3DContext::Out,
                &mut self.interners,
                &mut self.prim_store,
                &mut self.prim_instances,
                &mut self.clip_tree_builder,
            );
        }

        if !filter_primitives.is_empty() {
            let filter_datas = filter_datas.iter()
                .map(|filter_data| filter_data.sanitize())
                .map(|filter_data| {
                    SFilterData {
                        r_func: SFilterDataComponent::from_functype_values(
                            filter_data.func_r_type, &filter_data.r_values),
                        g_func: SFilterDataComponent::from_functype_values(
                            filter_data.func_g_type, &filter_data.g_values),
                        b_func: SFilterDataComponent::from_functype_values(
                            filter_data.func_b_type, &filter_data.b_values),
                        a_func: SFilterDataComponent::from_functype_values(
                            filter_data.func_a_type, &filter_data.a_values),
                    }
                })
                .collect();

            // Sanitize filter inputs
            for primitive in &mut filter_primitives {
                primitive.sanitize();
            }

            let composite_mode = PictureCompositeMode::SvgFilter(
                filter_primitives,
                filter_datas,
            );

            source = source.add_picture(
                composite_mode,
                clip_node_id,
                Picture3DContext::Out,
                &mut self.interners,
                &mut self.prim_store,
                &mut self.prim_instances,
                &mut self.clip_tree_builder,
            );
        }

        source
    }
}


pub trait CreateShadow {
    fn create_shadow(
        &self,
        shadow: &Shadow,
        blur_is_noop: bool,
        current_raster_space: RasterSpace,
    ) -> Self;
}

pub trait IsVisible {
    fn is_visible(&self) -> bool;
}

/// A primitive instance + some extra information about the primitive. This is
/// stored when constructing 3d rendering contexts, which involve cutting
/// primitive lists.
struct ExtendedPrimitiveInstance {
    instance: PrimitiveInstance,
    spatial_node_index: SpatialNodeIndex,
    flags: PrimitiveFlags,
}

/// Internal tracking information about the currently pushed stacking context.
/// Used to track what operations need to happen when a stacking context is popped.
struct StackingContextInfo {
    /// If true, pop and entry from the containing block stack.
    pop_containing_block: bool,
    /// If true, pop an entry from the flattened stacking context stack.
    pop_stacking_context: bool,
    /// If true, set a tile cache barrier when popping the stacking context.
    set_tile_cache_barrier: bool,
    /// If true, this stacking context was nested into two pushes instead of
    /// one, and requires an extra pop to compensate. The info to pop is stored
    /// at the top of `extra_stacking_context_stack`.
    needs_extra_stacking_context: bool,
}

/// Properties of a stacking context that are maintained
/// during creation of the scene. These structures are
/// not persisted after the initial scene build.
struct FlattenedStackingContext {
    /// The list of primitive instances added to this stacking context.
    prim_list: PrimitiveList,

    /// Primitive instance flags for compositing this stacking context
    prim_flags: PrimitiveFlags,

    /// The positioning node for this stacking context
    spatial_node_index: SpatialNodeIndex,

    /// The clip chain for this stacking context
    clip_node_id: ClipNodeId,

    /// The list of filters / mix-blend-mode for this
    /// stacking context.
    composite_ops: CompositeOps,

    /// Bitfield of reasons this stacking context needs to
    /// be an offscreen surface.
    blit_reason: BlitReason,

    /// CSS transform-style property.
    transform_style: TransformStyle,

    /// Defines the relationship to a preserve-3D hiearachy.
    context_3d: Picture3DContext<ExtendedPrimitiveInstance>,

    /// Flags identifying the type of container (among other things) this stacking context is
    flags: StackingContextFlags,

    /// Requested raster space for this stacking context
    raster_space: RasterSpace,

    /// Offset to be applied to any filter sub-regions
    subregion_offset: LayoutVector2D,
}

impl FlattenedStackingContext {
    /// Return true if the stacking context has a valid preserve-3d property
    pub fn is_3d(&self) -> bool {
        self.transform_style == TransformStyle::Preserve3D && self.composite_ops.is_empty()
    }

    /// Return true if the stacking context isn't needed.
    pub fn is_redundant(
        context_3d: &Picture3DContext<ExtendedPrimitiveInstance>,
        composite_ops: &CompositeOps,
        blit_reason: BlitReason,
        parent: Option<&FlattenedStackingContext>,
        prim_flags: PrimitiveFlags,
    ) -> bool {
        // Any 3d context is required
        if let Picture3DContext::In { .. } = context_3d {
            return false;
        }

        // If any filters are present that affect the output
        if composite_ops.has_valid_filters() {
            return false;
        }

        // If a mix-blend is active, we'll need to apply it in most cases
        if composite_ops.mix_blend_mode.is_some() {
            match parent {
                Some(ref parent) => {
                    // However, if the parent stacking context is empty, then the mix-blend
                    // is a no-op, and we can skip it
                    if !parent.prim_list.is_empty() {
                        return false;
                    }
                }
                None => {
                    // TODO(gw): For now, we apply mix-blend ops that may be no-ops on a root
                    //           level picture cache slice. We could apply a similar optimization
                    //           to above with a few extra checks here, but it's probably quite rare.
                    return false;
                }
            }
        }

        // If need to isolate in surface due to clipping / mix-blend-mode
        if !blit_reason.is_empty() {
            return false;
        }

        // If backface visibility is explicitly set.
        if !prim_flags.contains(PrimitiveFlags::IS_BACKFACE_VISIBLE) {
            return false;
        }

        // It is redundant!
        true
    }

    /// Cut the sequence of the immediate children recorded so far and generate a picture from them.
    pub fn cut_item_sequence(
        &mut self,
        prim_store: &mut PrimitiveStore,
        interners: &mut Interners,
        composite_mode: Option<PictureCompositeMode>,
        flat_items_context_3d: Picture3DContext<OrderedPictureChild>,
        clip_tree_builder: &mut ClipTreeBuilder,
    ) -> Option<(PictureIndex, PrimitiveInstance)> {
        if self.prim_list.is_empty() {
            return None
        }

        let pic_index = PictureIndex(prim_store.pictures
            .alloc()
            .init(PicturePrimitive::new_image(
                composite_mode.clone(),
                flat_items_context_3d,
                self.prim_flags,
                mem::replace(&mut self.prim_list, PrimitiveList::empty()),
                self.spatial_node_index,
                self.raster_space,
                PictureFlags::empty(),
                None
            ))
        );

        let prim_instance = create_prim_instance(
            pic_index,
            composite_mode.into(),
            self.raster_space,
            self.clip_node_id,
            interners,
            clip_tree_builder,
        );

        Some((pic_index, prim_instance))
    }
}

/// A primitive that is added while a shadow context is
/// active is stored as a pending primitive and only
/// added to pictures during pop_all_shadows.
pub struct PendingPrimitive<T> {
    spatial_node_index: SpatialNodeIndex,
    clip_node_id: ClipNodeId,
    info: LayoutPrimitiveInfo,
    prim: T,
}

/// As shadows are pushed, they are stored as pending
/// shadows, and handled at once during pop_all_shadows.
pub struct PendingShadow {
    shadow: Shadow,
    should_inflate: bool,
    spatial_node_index: SpatialNodeIndex,
}

pub enum ShadowItem {
    Shadow(PendingShadow),
    Image(PendingPrimitive<Image>),
    LineDecoration(PendingPrimitive<LineDecoration>),
    NormalBorder(PendingPrimitive<NormalBorderPrim>),
    Primitive(PendingPrimitive<PrimitiveKeyKind>),
    TextRun(PendingPrimitive<TextRun>),
}

impl From<PendingPrimitive<Image>> for ShadowItem {
    fn from(image: PendingPrimitive<Image>) -> Self {
        ShadowItem::Image(image)
    }
}

impl From<PendingPrimitive<LineDecoration>> for ShadowItem {
    fn from(line_dec: PendingPrimitive<LineDecoration>) -> Self {
        ShadowItem::LineDecoration(line_dec)
    }
}

impl From<PendingPrimitive<NormalBorderPrim>> for ShadowItem {
    fn from(border: PendingPrimitive<NormalBorderPrim>) -> Self {
        ShadowItem::NormalBorder(border)
    }
}

impl From<PendingPrimitive<PrimitiveKeyKind>> for ShadowItem {
    fn from(container: PendingPrimitive<PrimitiveKeyKind>) -> Self {
        ShadowItem::Primitive(container)
    }
}

impl From<PendingPrimitive<TextRun>> for ShadowItem {
    fn from(text_run: PendingPrimitive<TextRun>) -> Self {
        ShadowItem::TextRun(text_run)
    }
}

fn create_prim_instance(
    pic_index: PictureIndex,
    composite_mode_key: PictureCompositeKey,
    raster_space: RasterSpace,
    clip_node_id: ClipNodeId,
    interners: &mut Interners,
    clip_tree_builder: &mut ClipTreeBuilder,
) -> PrimitiveInstance {
    let pic_key = PictureKey::new(
        Picture {
            composite_mode_key,
            raster_space,
        },
    );

    let data_handle = interners
        .picture
        .intern(&pic_key, || ());

    PrimitiveInstance::new(
        PrimitiveInstanceKind::Picture {
            data_handle,
            pic_index,
        },
        clip_tree_builder.build_for_picture(
            clip_node_id,
        ),
    )
}

fn filter_ops_for_compositing(
    input_filters: ItemRange<FilterOp>,
) -> Vec<Filter> {
    // TODO(gw): Now that we resolve these later on,
    //           we could probably make it a bit
    //           more efficient than cloning these here.
    input_filters.iter().map(|filter| filter.into()).collect()
}

fn filter_datas_for_compositing(
    input_filter_datas: &[TempFilterData],
) -> Vec<FilterData> {
    // TODO(gw): Now that we resolve these later on,
    //           we could probably make it a bit
    //           more efficient than cloning these here.
    let mut filter_datas = vec![];
    for temp_filter_data in input_filter_datas {
        let func_types : Vec<ComponentTransferFuncType> = temp_filter_data.func_types.iter().collect();
        debug_assert!(func_types.len() == 4);
        filter_datas.push( FilterData {
            func_r_type: func_types[0],
            r_values: temp_filter_data.r_values.iter().collect(),
            func_g_type: func_types[1],
            g_values: temp_filter_data.g_values.iter().collect(),
            func_b_type: func_types[2],
            b_values: temp_filter_data.b_values.iter().collect(),
            func_a_type: func_types[3],
            a_values: temp_filter_data.a_values.iter().collect(),
        });
    }
    filter_datas
}

fn filter_primitives_for_compositing(
    input_filter_primitives: ItemRange<FilterPrimitive>,
) -> Vec<FilterPrimitive> {
    // Resolve these in the flattener?
    // TODO(gw): Now that we resolve these later on,
    //           we could probably make it a bit
    //           more efficient than cloning these here.
    input_filter_primitives.iter().map(|primitive| primitive).collect()
}

fn process_repeat_size(
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

fn read_gradient_stops(stops: ItemRange<GradientStop>) -> Vec<GradientStopKey> {
    stops.iter().map(|stop| {
        GradientStopKey {
            offset: stop.offset,
            color: stop.color.into(),
        }
    }).collect()
}

/// A helper for reusing the scene builder's memory allocations and dropping
/// scene allocations on the scene builder thread to avoid lock contention in
/// jemalloc.
pub struct SceneRecycler {
    pub tx: Sender<BuiltScene>,
    rx: Receiver<BuiltScene>,

    // Allocations recycled from BuiltScene:

    pub prim_store: PrimitiveStore,
    pub clip_store: ClipStore,
    pub picture_graph: PictureGraph,
    pub prim_instances: Vec<PrimitiveInstance>,
    pub surfaces: Vec<SurfaceInfo>,
    pub hit_testing_scene: Option<HitTestingScene>,
    pub clip_tree_builder: Option<ClipTreeBuilder>,
    //Could also attempt to recycle the following:
    //pub tile_cache_config: TileCacheConfig,
    //pub pipeline_epochs: FastHashMap<PipelineId, Epoch>,
    //pub tile_cache_pictures: Vec<PictureIndex>,


    // Allocations recycled from SceneBuilder

    id_to_index_mapper_stack: Vec<NodeIdToIndexMapper>,
    sc_stack: Vec<FlattenedStackingContext>,
    containing_block_stack: Vec<SpatialNodeIndex>,
    raster_space_stack: Vec<RasterSpace>,
    pending_shadow_items: VecDeque<ShadowItem>,
    iframe_size: Vec<LayoutSize>,
}

impl SceneRecycler {
    pub fn new() -> Self {
        let (tx, rx) = unbounded_channel();
        SceneRecycler {
            tx,
            rx,

            prim_instances: Vec::new(),
            surfaces: Vec::new(),
            prim_store: PrimitiveStore::new(&PrimitiveStoreStats::empty()),
            clip_store: ClipStore::new(),
            picture_graph: PictureGraph::new(),
            hit_testing_scene: None,
            clip_tree_builder: None,

            id_to_index_mapper_stack: Vec::new(),
            sc_stack: Vec::new(),
            containing_block_stack: Vec::new(),
            raster_space_stack: Vec::new(),
            pending_shadow_items: VecDeque::new(),
            iframe_size: Vec::new(),
        }
    }

    /// Do some bookkeeping of past memory allocations, retaining some of them for
    /// reuse and dropping the rest.
    ///
    /// Should be called once between scene builds, ideally outside of the critical
    /// path since deallocations can take some time.
    #[inline(never)]
    pub fn recycle_built_scene(&mut self) {
        let Ok(scene) = self.rx.try_recv() else {
            return;
        };

        self.prim_store = scene.prim_store;
        self.clip_store = scene.clip_store;
        // We currently retain top-level allocations but don't attempt to retain leaf
        // allocations in the prim store and clip store. We don't have to reset it here
        // but doing so avoids dropping the leaf allocations in the
        self.prim_store.reset();
        self.clip_store.reset();
        self.hit_testing_scene = Arc::try_unwrap(scene.hit_testing_scene).ok();
        self.picture_graph = scene.picture_graph;
        self.prim_instances = scene.prim_instances;
        self.surfaces = scene.surfaces;
        if let Some(clip_tree_builder) = &mut self.clip_tree_builder {
            clip_tree_builder.recycle_tree(scene.clip_tree);
        }

        while let Ok(_) = self.rx.try_recv() {
            // If for some reason more than one scene accumulated in the queue, drop
            // the rest.
        }

        // Note: fields of the scene we don't recycle get dropped here.
    }
}
