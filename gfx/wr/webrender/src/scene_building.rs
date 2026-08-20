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
use api::{DebugFlags, DisplayItem, DisplayItemRef, ExternalScrollId, FilterData};
use api::{FilterOp, FontInstanceKey, FontSize, GlyphInstance, GlyphOptions, GlyphShadowMode, GradientStop};
use api::{IdNamespace, IframeDisplayItem, ImageKey, ImageRendering, ItemRange, ColorDepth, QualitySettings};
use api::{LineOrientation, LineStyle, NinePatchBorderSource, PipelineId, MixBlendMode, StackingContextFlags};
use api::{PropertyBinding, ReferenceFrameKind, ScrollFrameDescriptor};
use api::{APZScrollGeneration, HasScrollLinkedEffect, SpatialId, StickyFrameDescriptor, ImageMask, ItemTag};
use api::{ClipMode, TransformStyle, YuvColorSpace, ColorRange, YuvData, TempFilterData};
use api::{ReferenceTransformBinding, Rotation, FillRule, SpatialTreeItem, ReferenceFrameDescriptor};
use api::{FilterOpGraphPictureBufferId, SVGFE_GRAPH_MAX};
use api::channel::{unbounded_channel, Receiver, Sender};
use api::units::*;
use crate::image_tiling::simplify_repeated_primitive;
use api::prim_geometry::{
    conic_gradient_prim, linear_gradient_prim, process_repeat_size,
    radial_gradient_prim,
};
use crate::box_shadow::BLUR_SAMPLE_SCALE;
use crate::clip::{ClipIntern, ClipItemKey, ClipItemKeyKind, ClipStore};
use crate::clip::{ClipInternData, ClipNodeId, ClipLeafId};
use crate::clip::{PolygonDataHandle, ClipTreeBuilder};
use crate::gpu_types::BlurEdgeMode;
use crate::segment::EdgeMask;
use crate::spatial_tree::{SceneSpatialTree, SpatialNodeContainer, SpatialNodeIndex};
use crate::frame_builder::FrameBuilderConfig;
use glyph_rasterizer::{FontInstance, SharedFontResources};
use crate::hit_test::HitTestingScene;
use crate::intern::Interner;
use crate::internal_types::{FastHashMap, LayoutPrimitiveInfo, Filter, PlaneSplitterIndex};
use crate::svg_filter::{FilterGraphNode, FilterGraphOp, FilterGraphPictureReference};
use crate::picture::{Picture3DContext, PictureInstance};
use crate::picture::{BlitReason, OrderedPictureChild, PrimitiveList, PictureFlags};
use crate::surface::SurfaceInfo;
use crate::picture_graph::PictureGraph;
use crate::prim_store::{PrimitiveInstance, PrimitiveStoreStats};
use crate::prim_store::{PrimitiveKind, NinePatchDescriptor, PrimitiveStore};
use crate::prim_store::{InternablePrimitive, PictureIndex};
use crate::prim_store::PolygonKey;
use crate::prim_store::rectangle::RectanglePrim;
use crate::prim_store::backdrop::{BackdropCapture, BackdropRender};
use crate::prim_store::borders::ImageBorder;
use crate::prim_store::gradient::{
    GradientStopKey, optimize_radial_gradient, apply_gradient_local_clip,
    optimize_linear_gradient,
};
use crate::prim_store::image::{Image, StretchSizeKey, YuvImage};
use crate::prim_store::line_dec::LineDecoration;
use crate::prim_store::picture::{Picture, PictureKey};
use crate::picture_composite_mode::{PictureCompositeKey, PictureCompositeMode};
use crate::prim_store::text_run::TextRun;
use crate::render_backend::SceneView;
use crate::scene::{BuiltScene, Scene, ScenePipeline, SceneStats, StackingContextHelpers};
use crate::scene_builder_thread::Interners;
use crate::spatial_node::{
    ReferenceFrameInfo, StickyFrameInfo, ScrollFrameKind, SpatialNodeType
};
use crate::tile_cache::TileCacheBuilder;
use euclid::approxeq::ApproxEq;
use std::{f32, mem, usize};
use std::sync::Arc;
use crate::util::{VecHelper, MaxRect};
use crate::filterdata::{SFilterDataComponent, SFilterData, SFilterDataKey};
use log::Level;

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
    pub snapshot: Option<SnapshotInfo>,

    // Requires two source textures (e.g. mix-blend-mode)
    pub mix_blend_mode: Option<MixBlendMode>,
}

impl CompositeOps {
    pub fn new(
        filters: Vec<Filter>,
        filter_datas: Vec<FilterData>,
        mix_blend_mode: Option<MixBlendMode>,
        snapshot: Option<SnapshotInfo>,
    ) -> Self {
        CompositeOps {
            filters,
            filter_datas,
            mix_blend_mode,
            snapshot,
        }
    }

    pub fn is_empty(&self) -> bool {
        self.filters.is_empty() &&
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
            .init(PictureInstance::new_image(
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
                    .init(PictureInstance::new_image(
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

    /// Raster space in effect, one entry per open stacking context. The
    /// values arrive already resolved on the item (see
    /// `StackingContext::raster_space`); this only remembers the innermost one
    /// for `add_text`.
    raster_space_stack: Vec<RasterSpace>,

    /// Maintains state for any currently active shadows

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

    /// The current recursion depth of iframes encountered. Used to restrict picture
    /// caching slices to only the top-level content frame.
    iframe_size: Vec<LayoutSize>,

    /// Clip-chain for root iframes applied to any tile caches created within this iframe
    root_iframe_clip: Option<ClipId>,

    /// The current quality / performance settings for this scene.
    quality_settings: QualitySettings,

    /// Maintains state about the list of tile caches being built for this scene.
    tile_cache_builder: TileCacheBuilder,

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

    /// Debug flags for the current scene build. Consulted e.g. to optionally
    /// disable promoting rounded-rect clips to compositor clips.
    debug_flags: DebugFlags,
}

impl<'a> SceneBuilder<'a> {
    pub fn build(
        scene: &Scene,
        root_pipeline: Option<PipelineId>,
        fonts: SharedFontResources,
        view: &SceneView,
        frame_builder_config: &FrameBuilderConfig,
        interners: &mut Interners,
        spatial_tree: &mut SceneSpatialTree,
        recycler: &mut SceneRecycler,
        stats: &SceneStats,
        debug_flags: DebugFlags,
    ) -> BuiltScene {
        tracy_rs::profile_scope!("build_scene");

        // We checked that the root pipeline is available on the render backend.
        let root_pipeline_id = root_pipeline.or(scene.root_pipeline_id).unwrap();
        let root_pipeline = scene.pipelines.get(&root_pipeline_id).unwrap();

        // Start each scene build with a fresh spatial tree containing just the
        // root reference frame.
        spatial_tree.reset();
        let root_reference_frame_index = spatial_tree.root_reference_frame_index();

        let mut builder = SceneBuilder {
            scene,
            spatial_tree,
            fonts,
            config: *frame_builder_config,
            id_to_index_mapper_stack: mem::take(&mut recycler.id_to_index_mapper_stack),
            hit_testing_scene: recycler.hit_testing_scene.take().unwrap_or_else(|| HitTestingScene::new(&stats.hit_test_stats)),
            sc_stack: mem::take(&mut recycler.sc_stack),
            containing_block_stack: mem::take(&mut recycler.containing_block_stack),
            raster_space_stack: mem::take(&mut recycler.raster_space_stack),
            prim_store: mem::take(&mut recycler.prim_store),
            clip_store: mem::take(&mut recycler.clip_store),
            interners,
            iframe_size: mem::take(&mut recycler.iframe_size),
            root_iframe_clip: None,
            quality_settings: view.quality_settings,
            tile_cache_builder: TileCacheBuilder::new(
                root_reference_frame_index,
                frame_builder_config.background_color,
                debug_flags,
            ),
            picture_graph: mem::take(&mut recycler.picture_graph),
            // This vector is empty most of the time, don't bother with recycling it for now.
            snapshot_pictures: Vec::new(),
            next_plane_splitter_index: 0,
            prim_instances: mem::take(&mut recycler.prim_instances),
            surfaces: mem::take(&mut recycler.surfaces),
            clip_tree_builder: recycler.clip_tree_builder.take().unwrap_or_else(|| ClipTreeBuilder::new()),
            extra_stacking_context_stack: Vec::new(),
            debug_flags,
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
        pictures: &mut [PictureInstance],
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

        // SNAPTODO: Scene-build use of unsnapped clip rects to compare
        // an LCA clip against a picture's clip and decide whether the
        // picture's crop is redundant. Snapping isn't possible here
        // (no frame-time spatial tree); audit whether sub-pixel
        // differences between the LCA and picture clip can flip this
        // decision once per-frame snapping is real.
        let lca_tree_node = shared_clip_node_id
            .and_then(|node_id| (node_id != ClipNodeId::NONE).then_some(node_id))
            .map(|node_id| clip_tree_builder.get_node(node_id));
        let lca_node = lca_tree_node
            .map(|tree_node| &clip_interner[tree_node.handle]);
        let lca_clip_rect = lca_tree_node
            .map(|tree_node| tree_node.unsnapped_clip_rect);
        let pic_node_id = prim_index
            .map(|prim_index| clip_tree_builder.get_leaf(prim_instances[prim_index].clip_leaf_id).node_id)
            .and_then(|node_id| (node_id != ClipNodeId::NONE).then_some(node_id));
        let pic_tree_node = pic_node_id
            .map(|node_id| clip_tree_builder.get_node(node_id));
        let pic_node = pic_tree_node
            .map(|tree_node| &clip_interner[tree_node.handle]);
        let pic_clip_rect = pic_tree_node
            .map(|tree_node| tree_node.unsnapped_clip_rect);

        // The logic behind this optimisation is that there's no need to clip
        // the contents of a picture when the crop will be applied anyway as
        // part of compositing the picture.  However, this is not true if the
        // picture includes a blur filter as the blur result depends on the
        // offscreen pixels which may or may not be cropped away.
        let has_blur = match &pictures[pic_index.0].composite_mode {
            Some(PictureCompositeMode::Filter(Filter::Blur { .. })) => true,
            Some(PictureCompositeMode::Filter(Filter::DropShadows { .. })) => true,
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
            lca_node.key == pic_node.key &&
            lca_clip_rect == pic_clip_rect &&
            !has_blur && direct_parent
        });

        if should_set_clip_root {
            pictures[pic_index.0].clip_root = shared_clip_node_id;
        }

        // Update the spatial node of any child pictures
        for cluster in &prim_list.clusters {
            for prim_instance_index in cluster.prim_range() {
                if let PrimitiveKind::Picture { pic_index: child_pic_index, .. } = prim_instances[prim_instance_index].kind {
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
                }
            }
        }

        // Restore the prim_list
        pictures[pic_index.0].prim_list = prim_list;
    }

    fn build_spatial_tree_for_display_list(
        &mut self,
        dl: &BuiltDisplayList,
        pipeline_id: PipelineId,
    ) {
        dl.iter_spatial_tree(|item| {
            match item {
                SpatialTreeItem::ScrollFrame(descriptor) => {
                    let parent_space = self.get_space(descriptor.parent_space);
                    self.build_scroll_frame(
                        descriptor,
                        parent_space,
                        pipeline_id,
                    );
                }
                SpatialTreeItem::ReferenceFrame(descriptor) => {
                    let parent_space = self.get_space(descriptor.parent_spatial_id);
                    self.build_reference_frame(
                        descriptor,
                        parent_space,
                        pipeline_id,
                    );
                }
                SpatialTreeItem::StickyFrame(descriptor) => {
                    let parent_space = self.get_space(descriptor.parent_spatial_id);
                    self.build_sticky_frame(
                        descriptor,
                        parent_space,
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
            /// Namespace the items traversed in this context are allowed to
            /// reference resources from. See `ScenePipeline::namespace`.
            namespace: IdNamespace,
            kind: ContextKind<'a>,
        }

        self.id_to_index_mapper_stack.push(NodeIdToIndexMapper::default());

        self.push_root(
            root_pipeline_id,
        );
        self.build_spatial_tree_for_display_list(
            &root_pipeline.display_list,
            root_pipeline_id,
        );

        let mut stack = vec![BuildContext {
            pipeline_id: root_pipeline_id,
            namespace: root_pipeline.namespace,
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
                        tracy_rs::profile_scope!("build_stacking_context");
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

                        let snapshot = info.snapshot.filter(|snapshot| {
                            validate_image_key(snapshot.key.as_image(), bc.namespace)
                        });

                        let composition_operations = CompositeOps::new(
                            filter_ops_for_compositing(item.filters()),
                            filter_datas_for_compositing(item.filter_datas()),
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
                        );

                        let new_context = BuildContext {
                            pipeline_id: bc.pipeline_id,
                            namespace: bc.namespace,
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
                        tracy_rs::profile_scope!("build_reference_frame");
                        let mut subtraversal = item.sub_iter();

                        let new_context = BuildContext {
                            pipeline_id: bc.pipeline_id,
                            namespace: bc.namespace,
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
                        tracy_rs::profile_scope!("iframe");

                        let space = self.get_space(info.space_and_clip.spatial_id);
                        // Note: the referenced pipeline is not checked against the
                        // parent pipeline's namespace. Nesting pipelines across
                        // namespaces is legitimate (a content process embeds the
                        // pipelines of its out-of-process iframes and of its async
                        // image pipelines), so validating iframe references needs
                        // ownership information webrender doesn't have.
                        let (namespace, subtraversal) = match self.push_iframe(info, space) {
                            Some(pair) => pair,
                            None => continue,
                        };

                        let new_context = BuildContext {
                            pipeline_id: info.pipeline_id,
                            namespace,
                            kind: ContextKind::Iframe {
                                parent_traversal: mem::replace(&mut traversal, subtraversal),
                            },
                        };
                        stack.push(bc);
                        stack.push(new_context);
                        continue 'outer;
                    }
                    _ => {
                        self.build_item(item, bc.namespace);
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
    ) {
        let sticky_frame_info = StickyFrameInfo::new(
            info.bounds,
            info.margins,
            info.vertical_offset_bounds,
            info.horizontal_offset_bounds,
            info.transform,
        );

        let index = self.spatial_tree.add_sticky_frame(
            parent_node_index,
            sticky_frame_info,
            info.id.pipeline_id(),
        );
        self.id_to_index_mapper_stack.last_mut().unwrap().add_spatial_node(info.id, index);
    }

    fn build_reference_frame(
        &mut self,
        info: &ReferenceFrameDescriptor,
        parent_space: SpatialNodeIndex,
        pipeline_id: PipelineId,
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

        // The reference-frame origin is snapped to the device pixel grid at
        // frame time (`SpatialNode::update`, gated on `should_snap`), where the
        // accumulated ancestor transform is known. A local-space round here
        // can't account for a fractional ancestor transform; see bug 1580534.
        let origin = info.origin;

        self.push_reference_frame(
            info.reference_frame.id,
            parent_space,
            pipeline_id,
            info.reference_frame.transform_style,
            transform,
            info.reference_frame.kind,
            origin.to_vector(),
            false,
        );
    }

    fn build_scroll_frame(
        &mut self,
        info: &ScrollFrameDescriptor,
        parent_node_index: SpatialNodeIndex,
        pipeline_id: PipelineId,
    ) {
        // This is useful when calculating scroll extents for the
        // SpatialNode::scroll(..) API as well as for properly setting sticky
        // positioning offsets.
        let content_size = info.content_rect.size();

        self.add_scroll_frame(
            info.scroll_frame_id,
            parent_node_index,
            info.external_id,
            pipeline_id,
            &info.frame_rect,
            &content_size,
            ScrollFrameKind::Explicit,
            info.external_scroll_offset,
            info.scroll_offset_generation,
            info.has_scroll_linked_effect,
        );
    }

    fn push_iframe(
        &mut self,
        info: &IframeDisplayItem,
        spatial_node_index: SpatialNodeIndex,
    ) -> Option<(IdNamespace, BuiltDisplayListIter<'a>)> {
        let iframe_pipeline_id = info.pipeline_id;
        let pipeline = match self.scene.pipelines.get(&iframe_pipeline_id) {
            Some(pipeline) => pipeline,
            None => {
                debug_assert!(info.ignore_missing_pipeline);
                return None
            },
        };

        self.clip_tree_builder.push_clip_chain(Some(info.space_and_clip.clip_chain_id), false, false);

        // TODO(gw): This is the only remaining call site that relies on ClipId parenting, remove me!
        self.add_rect_clip_node(
            ClipId::root(iframe_pipeline_id),
            info.space_and_clip.spatial_id,
            &info.clip_rect,
        );

        self.clip_tree_builder.push_clip_id(ClipId::root(iframe_pipeline_id));

        self.id_to_index_mapper_stack.push(NodeIdToIndexMapper::default());

        let bounds = info.bounds;

        // The iframe's reference-frame origin is snapped to the device pixel
        // grid at frame time (`SpatialNode::update`, gated on `should_snap`),
        // where the accumulated ancestor transform is known. Rounding the
        // local-space `bounds.min` here picks the wrong integer under a
        // fractional ancestor transform (e.g. `transform: translate(0.4px)`);
        // see bug 1580534.
        let origin = bounds.min;
        let iframe_size = bounds.size();

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
            origin.to_vector(),
            true,
        );

        let iframe_rect = LayoutRect::from_size(iframe_size);
        let is_root_pipeline = self.iframe_size.is_empty();

        self.add_scroll_frame(
            SpatialId::root_scroll_node(iframe_pipeline_id),
            spatial_node_index,
            ExternalScrollId(0, iframe_pipeline_id),
            iframe_pipeline_id,
            &iframe_rect,
            &iframe_size,
            ScrollFrameKind::PipelineRoot {
                is_root_pipeline,
            },
            LayoutVector2D::zero(),
            APZScrollGeneration::default(),
            HasScrollLinkedEffect::No,
        );

        // If this is a root iframe, force a new tile cache both before and after
        // adding primitives for this iframe.
        if self.iframe_size.is_empty() {
            assert!(self.root_iframe_clip.is_none());
            self.root_iframe_clip = Some(ClipId::root(iframe_pipeline_id));
            self.add_tile_cache_barrier_if_needed(SliceFlags::empty());
        }
        self.iframe_size.push(iframe_size);

        self.build_spatial_tree_for_display_list(
            &pipeline.display_list,
            iframe_pipeline_id,
        );

        Some((pipeline.namespace, pipeline.display_list.iter()))
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

        // If no bounds rect is given, default to clip rect. The external
        // scroll offset embedded in the DL coordinates by Gecko has already
        // been removed by the display-list builder. Snap is not applied here;
        // it happens at frame time in the in-frame picture-graph passes (see
        // `SpaceSnapper`).
        let clip_rect = common.clip_rect;
        let prim_rect = bounds.unwrap_or(clip_rect);
        let unsnapped_rect = prim_rect;


        let clip_node_id = self.get_clip_node(
            common.clip_chain_id,
        );

        let layout = LayoutPrimitiveInfo {
            rect: prim_rect,
            clip_rect,
            flags: common.flags,
            // TODO: for CSS primitives axis-aligned edges should not get anti-aliased whereas
            // for SVG primitives, they should. WebRender currently does not apply anti-aliasing
            // to SVG aligned primitives as it should, which has gone largely unnoticed because
            // most SVG primitives are rendered via blob-images.
            aligned_aa_edges: EdgeMask::empty(),
            transformed_aa_edges: EdgeMask::all(),
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

    /// `namespace` is the id namespace this display list is allowed to reference
    /// resources from; items referencing anything else are dropped. See
    /// `validate_resource_namespace`.
    fn build_item<'b>(
        &'b mut self,
        item: DisplayItemRef,
        namespace: IdNamespace,
    ) {
        match *item.item() {
            DisplayItem::Image(ref info) => {
                tracy_rs::profile_scope!("image");

                if !validate_image_key(info.image_key, namespace) {
                    return;
                }

                let (layout, _, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.bounds,
                );

                self.add_image(
                    spatial_node_index,
                    clip_node_id,
                    &layout,
                    StretchSizeKey::fills_prim(),
                    LayoutSize::zero(),
                    info.image_key,
                    info.image_rendering,
                    info.alpha_type,
                    info.color,
                );
            }
            DisplayItem::RepeatingImage(ref info) => {
                tracy_rs::profile_scope!("repeating_image");

                if !validate_image_key(info.image_key, namespace) {
                    return;
                }

                let (layout, unsnapped_rect, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.bounds,
                );

                let stretch_size = process_image_stretch_size(
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
                tracy_rs::profile_scope!("yuv_image");

                if !validate_yuv_data(&info.yuv_data, namespace) {
                    return;
                }

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
                tracy_rs::profile_scope!("text");

                if !validate_font_instance_key(info.font_key, namespace) {
                    return;
                }

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
                    info.shadow,
                );
            }
            DisplayItem::Rectangle(ref info) => {
                tracy_rs::profile_scope!("rect");

                let (layout, _, spatial_node_index, clip_node_id) = self.process_common_properties_with_bounds(
                    &info.common,
                    info.bounds,
                );

                self.add_primitive(
                    spatial_node_index,
                    clip_node_id,
                    &layout,
                    RectanglePrim {
                        color: info.color.into(),
                    },
                );

                if info.common.flags.contains(PrimitiveFlags::CHECKERBOARD_BACKGROUND) {
                    self.add_tile_cache_barrier_if_needed(SliceFlags::empty());
                }
            }
            DisplayItem::HitTest(ref info) => {
                tracy_rs::profile_scope!("hit_test");

                let spatial_node_index = self.get_space(info.spatial_id);

                let rect = info.rect;

                let layout = LayoutPrimitiveInfo {
                    rect,
                    clip_rect: rect,
                    flags: info.flags,
                    aligned_aa_edges: EdgeMask::empty(),
                    transformed_aa_edges: EdgeMask::empty(),
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
            DisplayItem::Line(ref info) => {
                tracy_rs::profile_scope!("line");

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
                tracy_rs::profile_scope!("gradient");

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

                let stops = read_gradient_stops(item.gradient_stops());
                let mut start = info.gradient.start_point;
                let mut end = info.gradient.end_point;
                // Run the simplification + clip pass; the fast-path two-stop
                // segment decomposition that used to run here now happens at
                // prepare time so segments tile against the snapped prim_rect
                // (see `decompose_axis_aligned_gradient`).
                optimize_linear_gradient(
                    &mut layout.rect,
                    &mut tile_size,
                    info.tile_spacing,
                    &layout.clip_rect,
                    &mut start,
                    &mut end,
                );

                if !tile_size.ceil().is_empty() {
                    if let Some(prim_key_kind) = linear_gradient_prim(
                        layout.rect,
                        start,
                        end,
                        stops,
                        info.gradient.extend_mode,
                        tile_size,
                        info.tile_spacing,
                        None,
                    ) {
                        self.add_primitive(
                            spatial_node_index,
                            clip_node_id,
                            &layout,
                            prim_key_kind,
                        );
                    }
                }
            }
            DisplayItem::RadialGradient(ref info) => {
                tracy_rs::profile_scope!("radial");

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
                let mut aa_mask = EdgeMask::all();
                optimize_radial_gradient(
                    &mut prim_rect,
                    &mut tile_size,
                    &mut center,
                    &mut tile_spacing,
                    &mut aa_mask,
                    &layout.clip_rect,
                    info.gradient.radius,
                    info.gradient.end_offset,
                    info.gradient.extend_mode,
                    &stops,
                    &mut |solid_rect, color, aa_mask| {
                        self.add_primitive(
                            spatial_node_index,
                            clip_node_id,
                            &LayoutPrimitiveInfo {
                                rect: *solid_rect,
                                aligned_aa_edges: layout.aligned_aa_edges & aa_mask,
                                transformed_aa_edges: layout.transformed_aa_edges & aa_mask,
                                .. layout
                            },
                            RectanglePrim { color: PropertyBinding::Value(color) },
                        );
                    }
                );

                layout.aligned_aa_edges &= aa_mask;
                layout.transformed_aa_edges &= aa_mask;

                // TODO: radial_gradient_prim already calls
                // this, but it leaves the info variable that is
                // passed to add_primitive unmodified
                // which can cause issues.
                simplify_repeated_primitive(&tile_size, &mut tile_spacing, &mut prim_rect);

                if !tile_size.ceil().is_empty() {
                    layout.rect = prim_rect;
                    let prim_key_kind = radial_gradient_prim(
                        layout.rect,
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

                    self.add_primitive(
                        spatial_node_index,
                        clip_node_id,
                        &layout,
                        prim_key_kind,
                    );
                }
            }
            DisplayItem::ConicGradient(ref info) => {
                tracy_rs::profile_scope!("conic");

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
                    let prim_key_kind = conic_gradient_prim(
                        layout.rect,
                        center,
                        info.gradient.angle,
                        info.gradient.start_offset,
                        info.gradient.end_offset,
                        read_gradient_stops(item.gradient_stops()),
                        info.gradient.extend_mode,
                        tile_size,
                        info.tile_spacing,
                        None,
                    );

                    self.add_primitive(
                        spatial_node_index,
                        clip_node_id,
                        &layout,
                        prim_key_kind,
                    );
                }
            }
            DisplayItem::BoxShadow(ref info) => {
                tracy_rs::profile_scope!("box_shadow");

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
                    info.shadow_radius,
                    info.clip_mode,
                );
            }
            DisplayItem::Border(ref info) => {
                tracy_rs::profile_scope!("border");

                if let BorderDetails::NinePatch(ref border) = info.details {
                    if let NinePatchBorderSource::Image(key, _) = border.source {
                        if !validate_image_key(key, namespace) {
                            return;
                        }
                    }
                }

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
                tracy_rs::profile_scope!("image_clip");

                // The clip node has to be defined either way, since later clip
                // chain items refer to it by id. Neutralize a foreign mask into
                // an empty one, which clips everything out, rather than dropping
                // the clip and letting the masked content draw unclipped.
                let image_mask = if validate_image_key(info.image_mask.image, namespace) {
                    info.image_mask
                } else {
                    ImageMask {
                        image: ImageKey::DUMMY,
                        rect: LayoutRect::zero(),
                    }
                };

                self.add_image_mask_clip_node(
                    info.id,
                    info.spatial_id,
                    &image_mask,
                    info.fill_rule,
                    item.points(),
                );
            }
            DisplayItem::RoundedRectClip(ref info) => {
                tracy_rs::profile_scope!("rounded_clip");

                self.add_rounded_rect_clip_node(
                    info.id,
                    info.spatial_id,
                    &info.clip,
                    info.snap_outset,
                );
            }
            DisplayItem::RectClip(ref info) => {
                tracy_rs::profile_scope!("rect_clip");

                self.add_rect_clip_node(
                    info.id,
                    info.spatial_id,
                    &info.clip_rect,
                );
            }
            DisplayItem::ClipChain(ref info) => {
                tracy_rs::profile_scope!("clip_chain");

                self.clip_tree_builder.define_clip_chain(
                    info.id,
                    info.parent,
                    item.clip_chain_items().into_iter(),
                );
            },
            DisplayItem::BackdropFilter(ref info) => {
                tracy_rs::profile_scope!("backdrop");

                let (layout, _, spatial_node_index, clip_node_id) = self.process_common_properties(
                    &info.common,
                    None,
                );

                let filters = filter_ops_for_compositing(item.filters());
                let filter_datas = filter_datas_for_compositing(item.filter_datas());

                self.add_backdrop_filter(
                    spatial_node_index,
                    clip_node_id,
                    &layout,
                    filters,
                    filter_datas,
                );
            }

            // Do nothing; these are dummy items for the display list parser
            DisplayItem::SetGradientStops |
            DisplayItem::SetFilterOps |
            DisplayItem::SetFilterData |
            DisplayItem::SetPoints => {}

            // Special items that are handled in the parent method
            DisplayItem::PushStackingContext(..) |
            DisplayItem::PushReferenceFrame(..) |
            DisplayItem::PopReferenceFrame |
            DisplayItem::PopStackingContext |
            DisplayItem::Iframe(_) => {
                unreachable!("Handled in `build_all`")
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
            info.rect,
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
                    &self.quality_settings,
                    &mut self.prim_instances,
                    &self.clip_tree_builder,
                );
            }
        }
    }

    /// Convenience interface that creates a primitive entry and adds it
    /// to the draw list.
    pub fn add_primitive<P>(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        info: &LayoutPrimitiveInfo,
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
                P::SNAP_CLIPS,
            );

            self.add_prim_to_draw_list(
                info,
                spatial_node_index,
                clip_leaf_id,
                prim,
            );
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

        self.tile_cache_builder.make_current_slice_atomic();
    }

    /// If no stacking contexts are present (i.e. we are adding prims to a tile
    /// cache), set a barrier to force creation of a slice before the next prim
    fn add_tile_cache_barrier_if_needed(
        &mut self,
        slice_flags: SliceFlags,
    ) {
        if self.sc_stack.is_empty() {
            self.tile_cache_builder.add_tile_cache_barrier(
                slice_flags,
                self.root_iframe_clip,
            );
        }
    }

    /// Push a new stacking context. Returns context that must be passed to pop_stacking_context().
    ///
    /// `raster_space` arrives already resolved against the enclosing stacking
    /// contexts; see `StackingContext::raster_space`.
    fn push_stacking_context(
        &mut self,
        mut composite_ops: CompositeOps,
        transform_style: TransformStyle,
        prim_flags: PrimitiveFlags,
        spatial_node_index: SpatialNodeIndex,
        clip_chain_id: Option<api::ClipChainId>,
        raster_space: RasterSpace,
        flags: StackingContextFlags,
    ) -> StackingContextInfo {
        tracy_rs::profile_scope!("push_stacking_context");

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
                    mix_blend_mode: None,
                    snapshot,
                },
                TransformStyle::Flat,
                prim_flags,
                spatial_node_index,
                clip_chain_id,
                raster_space,
                flags,
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
            composite_ops.snapshot.is_some(),
        );

        self.raster_space_stack.push(raster_space);

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

        // If we are forcing a backdrop root here, isolate this context
        // by using an intermediate surface.
        if flags.contains(StackingContextFlags::FORCED_ISOLATION) {
            blit_reason = BlitReason::FORCED_ISOLATION;
        }

        // Stacking context snapshots are offscreen surfaces.
        if composite_ops.snapshot.is_some() {
            blit_reason = BlitReason::SNAPSHOT;
        }

        // If this stacking context has any complex clips, we need to draw it
        // to an off-screen surface.
        if let Some(clip_chain_id) = clip_chain_id {
            if self.clip_tree_builder.clip_chain_has_complex_clips(clip_chain_id, &self.interners) {
                // At the root level, if all complex clips are fixed-position
                // rounded rectangles, we can skip the intermediate surface.
                // The clips will be promoted to compositor clips on the tile
                // cache slices, which applies them once to the composited
                // surface — equivalent to the intermediate surface approach.
                // This allows tile cache barriers to fire normally, enabling
                // proper picture caching with multiple slices.
                //
                // When DISABLE_COMPOSITOR_CLIPS is set, we never take this fast
                // path, so the clip is always applied via an intermediate
                // surface and the quad-shader clip path (useful for testing).
                if self.debug_flags.contains(DebugFlags::DISABLE_COMPOSITOR_CLIPS) ||
                   !self.sc_stack.is_empty() ||
                   !self.clip_tree_builder.clip_chain_complex_clips_are_promotable(
                       clip_chain_id,
                       &self.interners,
                       &self.spatial_tree,
                   )
                {
                    blit_reason |= BlitReason::CLIP;
                }
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
            if !self.sc_stack.is_empty() {
                // If we are already inside a stacking context hierarchy with a surface, then we
                // need to do the normal isolate of this blend container as a regular surface
                blit_reason |= BlitReason::BLEND_MODE;
                is_redundant = false;
            } else {
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
                    blit_reason |= BlitReason::BLEND_MODE;
                    is_redundant = false;
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
                raster_space,
            });
        }

        sc_info
    }

    fn pop_stacking_context(
        &mut self,
        info: StackingContextInfo,
    ) {
        tracy_rs::profile_scope!("pop_stacking_context");

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
                    .init(PictureInstance::new_image(
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
                        .init(PictureInstance::new_image(
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
                .init(PictureInstance::new_image(
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

        source = self.wrap_prim_with_filters(
            source,
            stacking_context.clip_node_id,
            stacking_context.composite_ops.filters,
            stacking_context.composite_ops.filter_datas,
            false,
            stacking_context.spatial_node_index,
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
        is_pipeline_root: bool,
    ) -> SpatialNodeIndex {
        let index = self.spatial_tree.add_reference_frame(
            parent_index,
            transform_style,
            source_transform,
            kind,
            origin_in_parent_reference_frame,
            pipeline_id,
            is_pipeline_root,
        );
        self.id_to_index_mapper_stack.last_mut().unwrap().add_spatial_node(reference_frame_id, index);

        index
    }

    fn push_root(
        &mut self,
        pipeline_id: PipelineId,
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
            true,
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

        let mask_rect = image_mask.rect;

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
            kind: ClipItemKeyKind::image_mask(image_mask, polygon_handle),
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
            spatial_node_index,
            mask_rect,
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

        let clip_rect = *clip_rect;

        let item = ClipItemKey {
            kind: ClipItemKeyKind::rectangle(ClipMode::Clip),
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
            spatial_node_index,
            clip_rect,
        );
    }

    fn add_rounded_rect_clip_node(
        &mut self,
        new_node_id: ClipId,
        spatial_id: SpatialId,
        clip: &ComplexClipRegion,
        snap_outset: f32,
    ) {
        let spatial_node_index = self.get_space(spatial_id);

        let region_rect = clip.rect;

        let item = ClipItemKey {
            kind: ClipItemKeyKind::rounded_rect(
                clip.radii,
                clip.inset,
                clip.mode,
            ),
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
            spatial_node_index,
            region_rect,
            snap_outset,
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
        );
        self.id_to_index_mapper_stack.last_mut().unwrap().add_spatial_node(new_node_id, node_index);
        node_index
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
        self.add_primitive(
            spatial_node_index,
            clip_node_id,
            &info,
            LineDecoration {
                style,
                orientation,
                wavy_line_thickness: Au::from_f32_px(wavy_line_thickness),
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
                            key,
                            rendering,
                            tile: None,
                            nine_patch,
                        };

                        self.add_primitive(
                            spatial_node_index,
                            clip_node_id,
                            info,
                            prim,
                        );
                    }
                    NinePatchBorderSource::Gradient(gradient) => {
                        let prim = match linear_gradient_prim(
                            info.rect,
                            gradient.start_point,
                            gradient.end_point,
                            read_gradient_stops(gradient_stops),
                            gradient.extend_mode,
                            LayoutSize::new(border.height as f32, border.width as f32),
                            LayoutSize::zero(),
                            Some(Box::new(nine_patch)),
                        ) {
                            Some(prim) => prim,
                            None => return,
                        };

                        self.add_primitive(
                            spatial_node_index,
                            clip_node_id,
                            info,
                            prim,
                        );
                    }
                    NinePatchBorderSource::RadialGradient(gradient) => {
                        let prim = radial_gradient_prim(
                            info.rect,
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

                        self.add_primitive(
                            spatial_node_index,
                            clip_node_id,
                            info,
                            prim,
                        );
                    }
                    NinePatchBorderSource::ConicGradient(gradient) => {
                        let prim = conic_gradient_prim(
                            info.rect,
                            gradient.center,
                            gradient.angle,
                            gradient.start_offset,
                            gradient.end_offset,
                            read_gradient_stops(gradient_stops),
                            gradient.extend_mode,
                            LayoutSize::new(border.height as f32, border.width as f32),
                            LayoutSize::zero(),
                            Some(Box::new(nine_patch)),
                        );

                        self.add_primitive(
                            spatial_node_index,
                            clip_node_id,
                            info,
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

    pub fn add_text(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        prim_info: &LayoutPrimitiveInfo,
        font_instance_key: &FontInstanceKey,
        text_color: &ColorF,
        glyph_range: ItemRange<GlyphInstance>,
        glyph_options: Option<GlyphOptions>,
        shadow_mode: GlyphShadowMode,
    ) {
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

            // Glyph pen positions arrive already relative to the prim rect
            // origin: the display-list builder relativizes them at record time
            // (see `DisplayListBuilder::push_text`), which keeps the intern key
            // stable across pre-scroll offset changes.
            //
            // TODO(gw): It'd be nice not to have to allocate here for creating
            //           the primitive key, when the common case is that the
            //           hash will match and we won't end up creating a new
            //           primitive template.
            let glyphs = glyph_range.iter().collect();

            // Query the current requested raster space (stack handled by push/pop
            // stacking context). A blurred shadow copy overrides this with
            // `Local(1.0)`, matching the removed `TextRun::create_shadow`: the
            // blur picture stays in screen space, but its shadow glyphs are
            // rasterized in local space (with texture padding, no subpixel AA).
            let requested_raster_space = match shadow_mode {
                GlyphShadowMode::Blurred => RasterSpace::Local(1.0),
                GlyphShadowMode::None | GlyphShadowMode::Unblurred => {
                    self.raster_space_stack.last().cloned().unwrap()
                }
            };

            TextRun {
                glyphs,
                font,
                shadow: shadow_mode != GlyphShadowMode::None,
                requested_raster_space,
            }
        };

        self.add_primitive(
            spatial_node_index,
            clip_node_id,
            prim_info,
            text_run,
        );
    }

    pub fn add_image(
        &mut self,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        info: &LayoutPrimitiveInfo,
        stretch_size: StretchSizeKey,
        mut tile_spacing: LayoutSize,
        image_key: ImageKey,
        image_rendering: ImageRendering,
        alpha_type: AlphaType,
        color: ColorF,
    ) {
        let mut prim_rect = info.rect;
        // Resolve per-axis: axes that fill the prim use the unsnapped
        // prim-rect size (`prim_rect` here is unsnapped at scene build).
        let prim_size = prim_rect.size();
        let stored: LayoutSize = stretch_size.size.into();
        let stretch_size_for_simplify = LayoutSize::new(
            if stretch_size.fills_width { prim_size.width } else { stored.width },
            if stretch_size.fills_height { prim_size.height } else { stored.height },
        );
        simplify_repeated_primitive(&stretch_size_for_simplify, &mut tile_spacing, &mut prim_rect);
        let info = LayoutPrimitiveInfo {
            rect: prim_rect,
            .. *info
        };

        self.add_primitive(
            spatial_node_index,
            clip_node_id,
            &info,
            Image {
                key: image_key,
                tile_spacing: tile_spacing.into(),
                stretch_size,
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
        let yuv_key = yuv_planes(&yuv_data);

        self.add_primitive(
            spatial_node_index,
            clip_node_id,
            info,
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
            true,
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
            filter_datas,
            true,
            // The filter subregions are authored in the filtered element's
            // space; the backdrop graph is composited in backdrop-root space,
            // so record the element node to resolve that offset at frame time.
            spatial_node_index,
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
                PrimitiveKind::Picture { pic_index, .. } => pic_index,
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
                PrimitiveKind::BackdropRender { ref mut pic_index, .. } => {
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
        filter_datas: Vec<FilterData>,
        is_backdrop_filter: bool,
        source_spatial_node_index: SpatialNodeIndex,
    ) -> PictureChainBuilder {
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
            // * Subregions are in the same space as the primitives: the
            //   display-list builder removes the external scroll offset from
            //   both, so no basis adjustment is needed here.
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
                        // The subregion is already in the same (normalized)
                        // coordinate space as the prims: the display-list
                        // builder removes the external scroll offset from both.
                        let clip_region = parsenode.subregion;

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
                                //
                                // If the alpha value of the lowest alpha index
                                // is more than 0.5/255.0, then the filter
                                // creates pixels from nothing.
                                let creates_pixels =
                                    if let Some(a) = filter_data.r_values.get(3) {
                                        *a >= (0.5/255.0)
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
                source_spatial_node_index,
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

                        // backdrop-filter spec says that blurs should assume edgeMode=Mirror
                        // We can do this by not inflating the bounds and setting the edge
                        // sampling mode to mirror.
                        if is_backdrop_filter {
                            if let Filter::Blur { ref mut should_inflate, ref mut edge_mode, .. } = filter {
                                *should_inflate = false;
                                *edge_mode = BlurEdgeMode::Mirror;
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

        source
    }
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
            .init(PictureInstance::new_image(
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
        PrimitiveKind::Picture {
            data_handle,
            pic_index,
        },
        clip_tree_builder.build_for_picture(
            clip_node_id,
        ),
        LayoutRect::zero(),
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

/// Image-specific stretch-size discriminator. Decided per-axis: if the
/// gecko-specified `repeat_size` matches the unsnapped prim rect on
/// that axis (within an FP-noise epsilon), the axis is flagged
/// `fills_*` and the effective extent is resolved against the snapped
/// prim rect at frame-build. Otherwise the explicit per-axis value is
/// stored verbatim. Per-axis (rather than all-or-nothing) preserves the
/// old `process_repeat_size` behaviour where a width-matching tile with
/// a non-matching height still picks up the snapped prim width.
fn process_image_stretch_size(
    unsnapped_rect: &LayoutRect,
    repeat_size: LayoutSize,
) -> StretchSizeKey {
    const EPSILON: f32 = 0.001;
    let fills_width = repeat_size.width.approx_eq_eps(&unsnapped_rect.width(), &EPSILON);
    let fills_height = repeat_size.height.approx_eq_eps(&unsnapped_rect.height(), &EPSILON);
    // Normalise filling axes to zero so prims that fill both axes share
    // an intern key regardless of their displayed size.
    let stored = LayoutSize::new(
        if fills_width { 0.0 } else { repeat_size.width },
        if fills_height { 0.0 } else { repeat_size.height },
    );
    StretchSizeKey {
        size: stored.into(),
        fills_width,
        fills_height,
    }
}

/// Encode a gradient's per-tile stretch as a fraction of its prim_size.
/// Per-axis: ratio = stretch_size / prim_size, clamped to [0, 1] (the upper
/// bound matches the old `stretch_size.min(prim_size)` clamp on the radial
/// and conic templates and avoids over-allocating render-task pixels).
///
/// If prim_size isn't finite-positive on both axes we fall back to a uniform
/// (1.0, 1.0) ratio, not per-axis. A per-axis fallback can mix a sentinel
/// 1.0 (NaN axis) with a real ratio (finite axis), which at prep produces a
/// partially-NaN stretch_size — that breaks downstream invariants like
/// image_tiling::repetitions's `stride > 0` assertion (NaN width passes the
/// finite-height needs_repetition check and reaches the assert before the
/// NaN-aware intersection short-circuit fires).
fn read_gradient_stops(stops: ItemRange<GradientStop>) -> Vec<GradientStopKey> {
    stops.iter().map(|stop| {
        GradientStopKey {
            offset: stop.offset,
            color: stop.color.into(),
        }
    }).collect()
}

/// Resource keys embedded in display items are only unique within the id
/// namespace that minted them, and all namespaces of a window share a single
/// `ResourceCache`. A key read out of a display item must be checked against
/// the namespace the display list was submitted with before it can be used
/// to look a resource up.
fn resource_namespace_matches(key_namespace: IdNamespace, namespace: IdNamespace) -> bool {
    // Namespace 0 is never handed out, so it is only ever the "no resource"
    // sentinel (`ImageKey::DUMMY` and friends) and cannot name a resource.
    key_namespace == namespace || key_namespace.0 == 0
}

fn validate_resource_namespace(
    key_namespace: IdNamespace,
    namespace: IdNamespace,
    kind: &'static str,
) -> bool {
    if resource_namespace_matches(key_namespace, namespace) {
        return true;
    }

    warn!(
        "Ignoring {} referencing id namespace {:?} from a display list owned by {:?}",
        kind, key_namespace, namespace,
    );
    debug_assert!(false, "display list references a resource of a foreign id namespace");

    false
}

fn validate_image_key(key: ImageKey, namespace: IdNamespace) -> bool {
    validate_resource_namespace(key.0, namespace, "image key")
}

/// The planes a `YuvData` actually references, padded with `ImageKey::DUMMY`.
/// Shared by validation and `add_yuv_image` so that the set of keys checked is
/// by construction the set of keys used.
fn yuv_planes(yuv_data: &YuvData) -> [ImageKey; 3] {
    match *yuv_data {
        YuvData::NV12(p0, p1)
        | YuvData::P010(p0, p1)
        | YuvData::NV16(p0, p1)
        | YuvData::P210(p0, p1) => [p0, p1, ImageKey::DUMMY],
        YuvData::PlanarYCbCr(p0, p1, p2) => [p0, p1, p2],
        YuvData::InterleavedYCbCr(p0) => [p0, ImageKey::DUMMY, ImageKey::DUMMY],
    }
}

fn validate_yuv_data(yuv_data: &YuvData, namespace: IdNamespace) -> bool {
    yuv_planes(yuv_data).iter().all(|key| validate_image_key(*key, namespace))
}

fn validate_font_instance_key(key: FontInstanceKey, namespace: IdNamespace) -> bool {
    validate_resource_namespace(key.0, namespace, "font instance key")
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
