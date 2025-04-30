/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! The high-level module responsible for managing the pipeline and preparing
//! commands to be issued by the `Renderer`.
//!
//! See the comment at the top of the `renderer` module for a description of
//! how these two pieces interact.

use api::{DebugFlags, Parameter, BoolParameter, PrimitiveFlags, MinimapData};
use api::{DocumentId, ExternalScrollId, HitTestResult};
use api::{IdNamespace, PipelineId, RenderNotifier, SampledScrollOffset};
use api::{NotificationRequest, Checkpoint, QualitySettings};
use api::{FramePublishId, PrimitiveKeyKind, RenderReasons};
use api::units::*;
use api::channel::{single_msg_channel, Sender, Receiver};
use crate::bump_allocator::ChunkPool;
use crate::AsyncPropertySampler;
use crate::box_shadow::BoxShadow;
#[cfg(any(feature = "capture", feature = "replay"))]
use crate::render_api::CaptureBits;
#[cfg(feature = "replay")]
use crate::render_api::CapturedDocument;
use crate::render_api::{MemoryReport, TransactionMsg, ResourceUpdate, ApiMsg, FrameMsg, ClearCache, DebugCommand};
use crate::clip::{ClipIntern, PolygonIntern, ClipStoreScratchBuffer};
use crate::filterdata::FilterDataIntern;
#[cfg(any(feature = "capture", feature = "replay"))]
use crate::capture::CaptureConfig;
use crate::composite::{CompositorKind, CompositeDescriptor};
use crate::frame_builder::{FrameBuilder, FrameBuilderConfig, FrameScratchBuffer};
use glyph_rasterizer::FontInstance;
use crate::gpu_cache::GpuCache;
use crate::hit_test::{HitTest, HitTester, SharedHitTester};
use crate::intern::DataStore;
#[cfg(any(feature = "capture", feature = "replay"))]
use crate::internal_types::DebugOutput;
use crate::internal_types::{FastHashMap, FrameId, FrameMemory, FrameStamp, RenderedDocument, ResultMsg};
use malloc_size_of::{MallocSizeOf, MallocSizeOfOps};
use crate::picture::{PictureScratchBuffer, SliceId, TileCacheInstance, TileCacheParams, SurfaceInfo, RasterConfig};
use crate::picture::PicturePrimitive;
use crate::prim_store::{PrimitiveScratchBuffer, PrimitiveInstance};
use crate::prim_store::{PrimitiveInstanceKind, PrimTemplateCommonData};
use crate::prim_store::interned::*;
use crate::profiler::{self, TransactionProfile};
use crate::render_task_graph::RenderTaskGraphBuilder;
use crate::renderer::{FullFrameStats, PipelineInfo};
use crate::resource_cache::ResourceCache;
#[cfg(feature = "replay")]
use crate::resource_cache::PlainCacheOwn;
#[cfg(feature = "replay")]
use crate::resource_cache::PlainResources;
#[cfg(feature = "replay")]
use crate::scene::Scene;
use crate::scene::{BuiltScene, SceneProperties};
use crate::scene_builder_thread::*;
use crate::spatial_tree::SpatialTree;
#[cfg(feature = "replay")]
use crate::spatial_tree::SceneSpatialTree;
use crate::telemetry::Telemetry;
#[cfg(feature = "capture")]
use serde::Serialize;
#[cfg(feature = "replay")]
use serde::Deserialize;
#[cfg(feature = "replay")]
use std::collections::hash_map::Entry::{Occupied, Vacant};
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::{mem, u32};
#[cfg(feature = "capture")]
use std::path::PathBuf;
#[cfg(feature = "replay")]
use crate::frame_builder::Frame;
use time::precise_time_ns;
use core::time::Duration;
use crate::util::{Recycler, VecHelper, drain_filter};

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Copy, Clone)]
pub struct DocumentView {
    scene: SceneView,
}

/// Some rendering parameters applying at the scene level.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Copy, Clone)]
pub struct SceneView {
    pub device_rect: DeviceIntRect,
    pub quality_settings: QualitySettings,
}

enum RenderBackendStatus {
    Continue,
    StopRenderBackend,
    ShutDown(Option<Sender<()>>),
}

macro_rules! declare_data_stores {
    ( $( $name:ident : $ty:ty, )+ ) => {
        /// A collection of resources that are shared by clips, primitives
        /// between display lists.
        #[cfg_attr(feature = "capture", derive(Serialize))]
        #[cfg_attr(feature = "replay", derive(Deserialize))]
        #[derive(Default)]
        pub struct DataStores {
            $(
                pub $name: DataStore<$ty>,
            )+
        }

        impl DataStores {
            /// Reports CPU heap usage.
            fn report_memory(&self, ops: &mut MallocSizeOfOps, r: &mut MemoryReport) {
                $(
                    r.interning.data_stores.$name += self.$name.size_of(ops);
                )+
            }

            fn apply_updates(
                &mut self,
                updates: InternerUpdates,
                profile: &mut TransactionProfile,
            ) {
                $(
                    self.$name.apply_updates(
                        updates.$name,
                        profile,
                    );
                )+
            }
        }
    }
}

crate::enumerate_interners!(declare_data_stores);

impl DataStores {
    /// Returns the local rect for a primitive. For most primitives, this is
    /// stored in the template. For pictures, this is stored inside the picture
    /// primitive instance itself, since this is determined during frame building.
    pub fn get_local_prim_rect(
        &self,
        prim_instance: &PrimitiveInstance,
        pictures: &[PicturePrimitive],
        surfaces: &[SurfaceInfo],
    ) -> LayoutRect {
        match prim_instance.kind {
            PrimitiveInstanceKind::Picture { pic_index, .. } => {
                let pic = &pictures[pic_index.0];

                match pic.raster_config {
                    Some(RasterConfig { surface_index, ref composite_mode, .. }) => {
                        let surface = &surfaces[surface_index.0];

                        composite_mode.get_rect(surface, None)
                    }
                    None => {
                        panic!("bug: get_local_prim_rect should not be called for pass-through pictures");
                    }
                }
            }
            _ => {
                self.as_common_data(prim_instance).prim_rect
            }
        }
    }

    /// Returns the local coverage (space occupied) for a primitive. For most primitives,
    /// this is stored in the template. For pictures, this is stored inside the picture
    /// primitive instance itself, since this is determined during frame building.
    pub fn get_local_prim_coverage_rect(
        &self,
        prim_instance: &PrimitiveInstance,
        pictures: &[PicturePrimitive],
        surfaces: &[SurfaceInfo],
    ) -> LayoutRect {
        match prim_instance.kind {
            PrimitiveInstanceKind::Picture { pic_index, .. } => {
                let pic = &pictures[pic_index.0];

                match pic.raster_config {
                    Some(RasterConfig { surface_index, ref composite_mode, .. }) => {
                        let surface = &surfaces[surface_index.0];

                        composite_mode.get_coverage(surface, None)
                    }
                    None => {
                        panic!("bug: get_local_prim_coverage_rect should not be called for pass-through pictures");
                    }
                }
            }
            _ => {
                self.as_common_data(prim_instance).prim_rect
            }
        }
    }

    /// Returns true if this primitive might need repition.
    // TODO(gw): This seems like the wrong place for this - maybe this flag should
    //           not be in the common prim template data?
    pub fn prim_may_need_repetition(
        &self,
        prim_instance: &PrimitiveInstance,
    ) -> bool {
        match prim_instance.kind {
            PrimitiveInstanceKind::Picture { .. } => {
                false
            }
            _ => {
                self.as_common_data(prim_instance).may_need_repetition
            }
        }
    }

    /// Returns true if this primitive has anti-aliasing enabled.
    pub fn prim_has_anti_aliasing(
        &self,
        prim_instance: &PrimitiveInstance,
    ) -> bool {
        match prim_instance.kind {
            PrimitiveInstanceKind::Picture { .. } => {
                false
            }
            _ => {
                self.as_common_data(prim_instance).flags.contains(PrimitiveFlags::ANTIALISED)
            }
        }
    }

    pub fn as_common_data(
        &self,
        prim_inst: &PrimitiveInstance
    ) -> &PrimTemplateCommonData {
        match prim_inst.kind {
            PrimitiveInstanceKind::Rectangle { data_handle, .. } |
            PrimitiveInstanceKind::Clear { data_handle, .. } => {
                let prim_data = &self.prim[data_handle];
                &prim_data.common
            }
            PrimitiveInstanceKind::Image { data_handle, .. } => {
                let prim_data = &self.image[data_handle];
                &prim_data.common
            }
            PrimitiveInstanceKind::ImageBorder { data_handle, .. } => {
                let prim_data = &self.image_border[data_handle];
                &prim_data.common
            }
            PrimitiveInstanceKind::LineDecoration { data_handle, .. } => {
                let prim_data = &self.line_decoration[data_handle];
                &prim_data.common
            }
            PrimitiveInstanceKind::LinearGradient { data_handle, .. }
            | PrimitiveInstanceKind::CachedLinearGradient { data_handle, .. } => {
                let prim_data = &self.linear_grad[data_handle];
                &prim_data.common
            }
            PrimitiveInstanceKind::NormalBorder { data_handle, .. } => {
                let prim_data = &self.normal_border[data_handle];
                &prim_data.common
            }
            PrimitiveInstanceKind::Picture { .. } => {
                panic!("BUG: picture prims don't have common data!");
            }
            PrimitiveInstanceKind::RadialGradient { data_handle, .. } => {
                let prim_data = &self.radial_grad[data_handle];
                &prim_data.common
            }
            PrimitiveInstanceKind::ConicGradient { data_handle, .. } => {
                let prim_data = &self.conic_grad[data_handle];
                &prim_data.common
            }
            PrimitiveInstanceKind::TextRun { data_handle, .. }  => {
                let prim_data = &self.text_run[data_handle];
                &prim_data.common
            }
            PrimitiveInstanceKind::YuvImage { data_handle, .. } => {
                let prim_data = &self.yuv_image[data_handle];
                &prim_data.common
            }
            PrimitiveInstanceKind::BackdropCapture { data_handle, .. } => {
                let prim_data = &self.backdrop_capture[data_handle];
                &prim_data.common
            }
            PrimitiveInstanceKind::BackdropRender { data_handle, .. } => {
                let prim_data = &self.backdrop_render[data_handle];
                &prim_data.common
            }
            PrimitiveInstanceKind::BoxShadow { data_handle, .. } => {
                let prim_data = &self.box_shadow[data_handle];
                &prim_data.common
            }
        }
    }
}

#[derive(Default)]
pub struct ScratchBuffer {
    pub primitive: PrimitiveScratchBuffer,
    pub picture: PictureScratchBuffer,
    pub frame: FrameScratchBuffer,
    pub clip_store: ClipStoreScratchBuffer,
}

impl ScratchBuffer {
    pub fn begin_frame(&mut self) {
        self.primitive.begin_frame();
        self.picture.begin_frame();
        self.frame.begin_frame();
    }

    pub fn end_frame(&mut self) {
        self.primitive.end_frame();
    }

    pub fn recycle(&mut self, recycler: &mut Recycler) {
        self.primitive.recycle(recycler);
        self.picture.recycle(recycler);
    }

    pub fn memory_pressure(&mut self) {
        // TODO: causes browser chrome test crashes on windows.
        //self.primitive = Default::default();
        self.picture = Default::default();
        self.frame = Default::default();
        self.clip_store = Default::default();
    }
}

struct Document {
    /// The id of this document
    id: DocumentId,

    /// Temporary list of removed pipelines received from the scene builder
    /// thread and forwarded to the renderer.
    removed_pipelines: Vec<(PipelineId, DocumentId)>,

    view: DocumentView,

    /// The id and time of the current frame.
    stamp: FrameStamp,

    /// The latest built scene, usable to build frames.
    /// received from the scene builder thread.
    scene: BuiltScene,

    /// The builder object that prodces frames, kept around to preserve some retained state.
    frame_builder: FrameBuilder,

    /// Allows graphs of render tasks to be created, and then built into an immutable graph output.
    rg_builder: RenderTaskGraphBuilder,

    /// A data structure to allow hit testing against rendered frames. This is updated
    /// every time we produce a fully rendered frame.
    hit_tester: Option<Arc<HitTester>>,
    /// To avoid synchronous messaging we update a shared hit-tester that other threads
    /// can query.
    shared_hit_tester: Arc<SharedHitTester>,

    /// Properties that are resolved during frame building and can be changed at any time
    /// without requiring the scene to be re-built.
    dynamic_properties: SceneProperties,

    /// Track whether the last built frame is up to date or if it will need to be re-built
    /// before rendering again.
    frame_is_valid: bool,
    hit_tester_is_valid: bool,
    rendered_frame_is_valid: bool,
    /// We track this information to be able to display debugging information from the
    /// renderer.
    has_built_scene: bool,

    data_stores: DataStores,

    /// Retained frame-building version of the spatial tree
    spatial_tree: SpatialTree,

    minimap_data: FastHashMap<ExternalScrollId, MinimapData>,

    /// Contains various vecs of data that is used only during frame building,
    /// where we want to recycle the memory each new display list, to avoid constantly
    /// re-allocating and moving memory around.
    scratch: ScratchBuffer,

    #[cfg(feature = "replay")]
    loaded_scene: Scene,

    /// Tracks the state of the picture cache tiles that were composited on the previous frame.
    prev_composite_descriptor: CompositeDescriptor,

    /// Tracks if we need to invalidate dirty rects for this document, due to the picture
    /// cache slice configuration having changed when a new scene is swapped in.
    dirty_rects_are_valid: bool,

    profile: TransactionProfile,
    frame_stats: Option<FullFrameStats>,
}

impl Document {
    pub fn new(
        id: DocumentId,
        size: DeviceIntSize,
    ) -> Self {
        Document {
            id,
            removed_pipelines: Vec::new(),
            view: DocumentView {
                scene: SceneView {
                    device_rect: size.into(),
                    quality_settings: QualitySettings::default(),
                },
            },
            stamp: FrameStamp::first(id),
            scene: BuiltScene::empty(),
            frame_builder: FrameBuilder::new(),
            hit_tester: None,
            shared_hit_tester: Arc::new(SharedHitTester::new()),
            dynamic_properties: SceneProperties::new(),
            frame_is_valid: false,
            hit_tester_is_valid: false,
            rendered_frame_is_valid: false,
            has_built_scene: false,
            data_stores: DataStores::default(),
            spatial_tree: SpatialTree::new(),
            minimap_data: FastHashMap::default(),
            scratch: ScratchBuffer::default(),
            #[cfg(feature = "replay")]
            loaded_scene: Scene::new(),
            prev_composite_descriptor: CompositeDescriptor::empty(),
            dirty_rects_are_valid: true,
            profile: TransactionProfile::new(),
            rg_builder: RenderTaskGraphBuilder::new(),
            frame_stats: None,
        }
    }

    fn can_render(&self) -> bool {
        self.scene.has_root_pipeline
    }

    fn has_pixels(&self) -> bool {
        !self.view.scene.device_rect.is_empty()
    }

    fn process_frame_msg(
        &mut self,
        message: FrameMsg,
    ) -> DocumentOps {
        match message {
            FrameMsg::UpdateEpoch(pipeline_id, epoch) => {
                self.scene.pipeline_epochs.insert(pipeline_id, epoch);
            }
            FrameMsg::HitTest(point, tx) => {
                if !self.hit_tester_is_valid {
                    self.rebuild_hit_tester();
                }

                let result = match self.hit_tester {
                    Some(ref hit_tester) => {
                        hit_tester.hit_test(HitTest::new(point))
                    }
                    None => HitTestResult { items: Vec::new() },
                };

                tx.send(result).unwrap();
            }
            FrameMsg::RequestHitTester(tx) => {
                tx.send(self.shared_hit_tester.clone()).unwrap();
            }
            FrameMsg::SetScrollOffsets(id, offset) => {
                profile_scope!("SetScrollOffset");

                if self.set_scroll_offsets(id, offset) {
                    self.hit_tester_is_valid = false;
                    self.frame_is_valid = false;
                }

                return DocumentOps {
                    scroll: true,
                    ..DocumentOps::nop()
                };
            }
            FrameMsg::ResetDynamicProperties => {
                self.dynamic_properties.reset_properties();
            }
            FrameMsg::AppendDynamicProperties(property_bindings) => {
                self.dynamic_properties.add_properties(property_bindings);
            }
            FrameMsg::AppendDynamicTransformProperties(property_bindings) => {
                self.dynamic_properties.add_transforms(property_bindings);
            }
            FrameMsg::SetIsTransformAsyncZooming(is_zooming, animation_id) => {
                if let Some(node_index) = self.spatial_tree.find_spatial_node_by_anim_id(animation_id) {
                    let node = self.spatial_tree.get_spatial_node_mut(node_index);

                    if node.is_async_zooming != is_zooming {
                        node.is_async_zooming = is_zooming;
                        self.frame_is_valid = false;
                    }
                }
            }
            FrameMsg::SetMinimapData(id, minimap_data) => {
              self.minimap_data.insert(id, minimap_data);
            }
        }

        DocumentOps::nop()
    }

    fn build_frame(
        &mut self,
        resource_cache: &mut ResourceCache,
        gpu_cache: &mut GpuCache,
        debug_flags: DebugFlags,
        tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
        frame_stats: Option<FullFrameStats>,
        present: bool,
        render_reasons: RenderReasons,
        frame_memory: FrameMemory,
    ) -> RenderedDocument {
        let frame_build_start_time = precise_time_ns();

        // Advance to the next frame.
        self.stamp.advance();

        assert!(self.stamp.frame_id() != FrameId::INVALID,
                "First frame increment must happen before build_frame()");

        let frame = {
            let frame = self.frame_builder.build(
                &mut self.scene,
                present,
                resource_cache,
                gpu_cache,
                &mut self.rg_builder,
                self.stamp,
                self.view.scene.device_rect.min,
                &self.dynamic_properties,
                &mut self.data_stores,
                &mut self.scratch,
                debug_flags,
                tile_caches,
                &mut self.spatial_tree,
                self.dirty_rects_are_valid,
                &mut self.profile,
                // Consume the minimap data. If APZ wants a minimap rendered
                // on the next frame, it will add new entries to the minimap
                // data during sampling.
                mem::take(&mut self.minimap_data),
                frame_memory,
            );

            frame
        };

        self.frame_is_valid = true;
        self.dirty_rects_are_valid = true;

        self.has_built_scene = false;

        let frame_build_time_ms =
            profiler::ns_to_ms(precise_time_ns() - frame_build_start_time);
        self.profile.set(profiler::FRAME_BUILDING_TIME, frame_build_time_ms);
        self.profile.start_time(profiler::FRAME_SEND_TIME);

        let frame_stats = frame_stats.map(|mut stats| {
            stats.frame_build_time += frame_build_time_ms;
            stats
        });

        RenderedDocument {
            frame,
            profile: self.profile.take_and_reset(),
            frame_stats: frame_stats,
            render_reasons,
        }
    }

    fn rebuild_hit_tester(&mut self) {
        self.spatial_tree.update_tree(&self.dynamic_properties);

        let hit_tester = Arc::new(self.scene.create_hit_tester(&self.spatial_tree));
        self.hit_tester = Some(Arc::clone(&hit_tester));
        self.shared_hit_tester.update(hit_tester);
        self.hit_tester_is_valid = true;
    }

    pub fn updated_pipeline_info(&mut self) -> PipelineInfo {
        let removed_pipelines = self.removed_pipelines.take_and_preallocate();
        PipelineInfo {
            epochs: self.scene.pipeline_epochs.iter()
                .map(|(&pipeline_id, &epoch)| ((pipeline_id, self.id), epoch)).collect(),
            removed_pipelines,
        }
    }

    /// Returns true if the node actually changed position or false otherwise.
    pub fn set_scroll_offsets(
        &mut self,
        id: ExternalScrollId,
        offsets: Vec<SampledScrollOffset>,
    ) -> bool {
        self.spatial_tree.set_scroll_offsets(id, offsets)
    }

    /// Update the state of tile caches when a new scene is being swapped in to
    /// the render backend. Retain / reuse existing caches if possible, and
    /// destroy any now unused caches.
    fn update_tile_caches_for_new_scene(
        &mut self,
        mut requested_tile_caches: FastHashMap<SliceId, TileCacheParams>,
        tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
        resource_cache: &mut ResourceCache,
    ) {
        let mut new_tile_caches = FastHashMap::default();
        new_tile_caches.reserve(requested_tile_caches.len());

        // Step through the tile caches that are needed for the new scene, and see
        // if we have an existing cache that can be reused.
        for (slice_id, params) in requested_tile_caches.drain() {
            let tile_cache = match tile_caches.remove(&slice_id) {
                Some(mut existing_tile_cache) => {
                    // Found an existing cache - update the cache params and reuse it
                    existing_tile_cache.prepare_for_new_scene(
                        params,
                        resource_cache,
                    );
                    existing_tile_cache
                }
                None => {
                    // No cache exists so create a new one
                    Box::new(TileCacheInstance::new(params))
                }
            };

            new_tile_caches.insert(slice_id, tile_cache);
        }

        // Replace current tile cache map, and return what was left over,
        // which are now unused.
        let unused_tile_caches = mem::replace(
            tile_caches,
            new_tile_caches,
        );

        if !unused_tile_caches.is_empty() {
            // If the slice configuration changed, assume we can't rely on the
            // current dirty rects for next composite
            self.dirty_rects_are_valid = false;

            // Destroy any native surfaces allocated by these unused caches
            for (_, tile_cache) in unused_tile_caches {
                tile_cache.destroy(resource_cache);
            }
        }
    }

    pub fn new_async_scene_ready(
        &mut self,
        mut built_scene: BuiltScene,
        recycler: &mut Recycler,
        tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
        resource_cache: &mut ResourceCache,
    ) {
        self.frame_is_valid = false;
        self.hit_tester_is_valid = false;

        self.update_tile_caches_for_new_scene(
            mem::replace(&mut built_scene.tile_cache_config.tile_caches, FastHashMap::default()),
            tile_caches,
            resource_cache,
        );


        let old_scene = std::mem::replace(&mut self.scene, built_scene);
        old_scene.recycle();

        self.scratch.recycle(recycler);
    }
}

struct DocumentOps {
    scroll: bool,
}

impl DocumentOps {
    fn nop() -> Self {
        DocumentOps {
            scroll: false,
        }
    }
}

/// The unique id for WR resource identification.
/// The namespace_id should start from 1.
static NEXT_NAMESPACE_ID: AtomicUsize = AtomicUsize::new(1);

#[cfg(any(feature = "capture", feature = "replay"))]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
struct PlainRenderBackend {
    frame_config: FrameBuilderConfig,
    documents: FastHashMap<DocumentId, DocumentView>,
    resource_sequence_id: u32,
}

/// The render backend is responsible for transforming high level display lists into
/// GPU-friendly work which is then submitted to the renderer in the form of a frame::Frame.
///
/// The render backend operates on its own thread.
pub struct RenderBackend {
    api_rx: Receiver<ApiMsg>,
    result_tx: Sender<ResultMsg>,
    scene_tx: Sender<SceneBuilderRequest>,

    gpu_cache: GpuCache,
    resource_cache: ResourceCache,
    chunk_pool: Arc<ChunkPool>,

    frame_config: FrameBuilderConfig,
    default_compositor_kind: CompositorKind,
    documents: FastHashMap<DocumentId, Document>,

    notifier: Box<dyn RenderNotifier>,
    sampler: Option<Box<dyn AsyncPropertySampler + Send>>,
    size_of_ops: Option<MallocSizeOfOps>,
    debug_flags: DebugFlags,
    namespace_alloc_by_client: bool,

    recycler: Recycler,

    #[cfg(feature = "capture")]
    /// If `Some`, do 'sequence capture' logging, recording updated documents,
    /// frames, etc. This is set only through messages from the scene builder,
    /// so all control of sequence capture goes through there.
    capture_config: Option<CaptureConfig>,

    #[cfg(feature = "replay")]
    loaded_resource_sequence_id: u32,

    /// A map of tile caches. These are stored in the backend as they are
    /// persisted between both frame and scenes.
    tile_caches: FastHashMap<SliceId, Box<TileCacheInstance>>,

    /// The id of the latest PublishDocument
    frame_publish_id: FramePublishId,
}

impl RenderBackend {
    pub fn new(
        api_rx: Receiver<ApiMsg>,
        result_tx: Sender<ResultMsg>,
        scene_tx: Sender<SceneBuilderRequest>,
        resource_cache: ResourceCache,
        chunk_pool: Arc<ChunkPool>,
        notifier: Box<dyn RenderNotifier>,
        frame_config: FrameBuilderConfig,
        sampler: Option<Box<dyn AsyncPropertySampler + Send>>,
        size_of_ops: Option<MallocSizeOfOps>,
        debug_flags: DebugFlags,
        namespace_alloc_by_client: bool,
    ) -> RenderBackend {
        RenderBackend {
            api_rx,
            result_tx,
            scene_tx,
            resource_cache,
            gpu_cache: GpuCache::new(),
            chunk_pool,
            frame_config,
            default_compositor_kind : frame_config.compositor_kind,
            documents: FastHashMap::default(),
            notifier,
            sampler,
            size_of_ops,
            debug_flags,
            namespace_alloc_by_client,
            recycler: Recycler::new(),
            #[cfg(feature = "capture")]
            capture_config: None,
            #[cfg(feature = "replay")]
            loaded_resource_sequence_id: 0,
            tile_caches: FastHashMap::default(),
            frame_publish_id: FramePublishId::first(),
        }
    }

    pub fn next_namespace_id() -> IdNamespace {
        IdNamespace(NEXT_NAMESPACE_ID.fetch_add(1, Ordering::Relaxed) as u32)
    }

    pub fn run(&mut self) {
        let mut frame_counter: u32 = 0;
        let mut status = RenderBackendStatus::Continue;

        if let Some(ref sampler) = self.sampler {
            sampler.register();
        }

        while let RenderBackendStatus::Continue = status {
            status = match self.api_rx.recv() {
                Ok(msg) => {
                    self.process_api_msg(msg, &mut frame_counter)
                }
                Err(..) => { RenderBackendStatus::ShutDown(None) }
            };
        }

        if let RenderBackendStatus::StopRenderBackend = status {
            while let Ok(msg) = self.api_rx.recv() {
                match msg {
                    ApiMsg::SceneBuilderResult(SceneBuilderResult::ExternalEvent(evt)) => {
                        self.notifier.external_event(evt);
                    }
                    ApiMsg::SceneBuilderResult(SceneBuilderResult::FlushComplete(tx)) => {
                        // If somebody's blocked waiting for a flush, how did they
                        // trigger the RB thread to shut down? This shouldn't happen
                        // but handle it gracefully anyway.
                        debug_assert!(false);
                        tx.send(()).ok();
                    }
                    ApiMsg::SceneBuilderResult(SceneBuilderResult::ShutDown(sender)) => {
                        info!("Recycling stats: {:?}", self.recycler);
                        status = RenderBackendStatus::ShutDown(sender);
                        break;
                   }
                    _ => {},
                }
            }
        }

        // Ensure we read everything the scene builder is sending us from
        // inflight messages, otherwise the scene builder might panic.
        while let Ok(msg) = self.api_rx.try_recv() {
            match msg {
                ApiMsg::SceneBuilderResult(SceneBuilderResult::FlushComplete(tx)) => {
                    // If somebody's blocked waiting for a flush, how did they
                    // trigger the RB thread to shut down? This shouldn't happen
                    // but handle it gracefully anyway.
                    debug_assert!(false);
                    tx.send(()).ok();
                }
                _ => {},
            }
        }

        self.documents.clear();

        self.notifier.shut_down();

        if let Some(ref sampler) = self.sampler {
            sampler.deregister();
        }


        if let RenderBackendStatus::ShutDown(Some(sender)) = status {
            let _ = sender.send(());
        }
    }

    fn process_transaction(
        &mut self,
        mut txns: Vec<Box<BuiltTransaction>>,
        result_tx: Option<Sender<SceneSwapResult>>,
        frame_counter: &mut u32,
    ) -> bool {
        self.prepare_for_frames();
        self.maybe_force_nop_documents(
            frame_counter,
            |document_id| txns.iter().any(|txn| txn.document_id == document_id));

        let mut built_frame = false;
        for mut txn in txns.drain(..) {
           let has_built_scene = txn.built_scene.is_some();

            if let Some(doc) = self.documents.get_mut(&txn.document_id) {
                doc.removed_pipelines.append(&mut txn.removed_pipelines);
                doc.view.scene = txn.view;
                doc.profile.merge(&mut txn.profile);

                doc.frame_stats = if let Some(stats) = &doc.frame_stats {
                    Some(stats.merge(&txn.frame_stats))
                } else {
                    Some(txn.frame_stats)
                };

                // Before updating the spatial tree, save the most recently sampled
                // scroll offsets (which include async deltas).
                let last_sampled_scroll_offsets = if self.sampler.is_some() {
                    Some(doc.spatial_tree.get_last_sampled_scroll_offsets())
                } else {
                    None
                };

                if let Some(updates) = txn.spatial_tree_updates.take() {
                    doc.spatial_tree.apply_updates(updates);
                }

                if let Some(built_scene) = txn.built_scene.take() {
                    doc.new_async_scene_ready(
                        built_scene,
                        &mut self.recycler,
                        &mut self.tile_caches,
                        &mut self.resource_cache,
                    );
                }

                // If there are any additions or removals of clip modes
                // during the scene build, apply them to the data store now.
                // This needs to happen before we build the hit tester.
                if let Some(updates) = txn.interner_updates.take() {
                    doc.data_stores.apply_updates(updates, &mut doc.profile);
                }

                // Apply the last sampled scroll offsets from the previous scene,
                // to the current scene. The offsets are identified by scroll ids
                // which are stable across scenes. This ensures that a hit test,
                // which could occur in between post-swap hook and the call to
                // update_document() below, does not observe raw main-thread offsets
                // from the new scene that don't have async deltas applied to them.
                if let Some(last_sampled) = last_sampled_scroll_offsets {
                    doc.spatial_tree
                        .apply_last_sampled_scroll_offsets(last_sampled);
                }

                // Build the hit tester while the APZ lock is held so that its content
                // is in sync with the gecko APZ tree.
                if !doc.hit_tester_is_valid {
                    doc.rebuild_hit_tester();
                }

                if let Some(ref tx) = result_tx {
                    let (resume_tx, resume_rx) = single_msg_channel();
                    tx.send(SceneSwapResult::Complete(resume_tx)).unwrap();
                    // Block until the post-swap hook has completed on
                    // the scene builder thread. We need to do this before
                    // we can sample from the sampler hook which might happen
                    // in the update_document call below.
                    resume_rx.recv().ok();
                }

                self.resource_cache.add_rasterized_blob_images(
                    txn.rasterized_blobs.take(),
                    &mut doc.profile,
                );

            } else {
                // The document was removed while we were building it, skip it.
                // TODO: we might want to just ensure that removed documents are
                // always forwarded to the scene builder thread to avoid this case.
                if let Some(ref tx) = result_tx {
                    tx.send(SceneSwapResult::Aborted).unwrap();
                }
                continue;
            }

            built_frame |= self.update_document(
                txn.document_id,
                txn.resource_updates.take(),
                txn.frame_ops.take(),
                txn.notifications.take(),
                txn.render_frame,
                txn.present,
                RenderReasons::SCENE,
                None,
                txn.invalidate_rendered_frame,
                frame_counter,
                has_built_scene,
                None,
            );
        }

        built_frame
    }

    fn process_api_msg(
        &mut self,
        msg: ApiMsg,
        frame_counter: &mut u32,
    ) -> RenderBackendStatus {
        match msg {
            ApiMsg::CloneApi(sender) => {
                assert!(!self.namespace_alloc_by_client);
                sender.send(Self::next_namespace_id()).unwrap();
            }
            ApiMsg::CloneApiByClient(namespace_id) => {
                assert!(self.namespace_alloc_by_client);
                debug_assert!(!self.documents.iter().any(|(did, _doc)| did.namespace_id == namespace_id));
            }
            ApiMsg::AddDocument(document_id, initial_size) => {
                let document = Document::new(
                    document_id,
                    initial_size,
                );
                let old = self.documents.insert(document_id, document);
                debug_assert!(old.is_none());
            }
            ApiMsg::MemoryPressure => {
                // This is drastic. It will basically flush everything out of the cache,
                // and the next frame will have to rebuild all of its resources.
                // We may want to look into something less extreme, but on the other hand this
                // should only be used in situations where are running low enough on memory
                // that we risk crashing if we don't do something about it.
                // The advantage of clearing the cache completely is that it gets rid of any
                // remaining fragmentation that could have persisted if we kept around the most
                // recently used resources.
                self.resource_cache.clear(ClearCache::all());

                self.gpu_cache.clear();

                for (_, doc) in &mut self.documents {
                    doc.scratch.memory_pressure();
                    for tile_cache in self.tile_caches.values_mut() {
                        tile_cache.memory_pressure(&mut self.resource_cache);
                    }
                }

                let resource_updates = self.resource_cache.pending_updates();
                let msg = ResultMsg::UpdateResources {
                    resource_updates,
                    memory_pressure: true,
                };
                self.result_tx.send(msg).unwrap();
                self.notifier.wake_up(false);

                self.chunk_pool.purge_all_chunks();
            }
            ApiMsg::ReportMemory(tx) => {
                self.report_memory(tx);
            }
            ApiMsg::DebugCommand(option) => {
                let msg = match option {
                    DebugCommand::SetPictureTileSize(tile_size) => {
                        self.frame_config.tile_size_override = tile_size;
                        self.update_frame_builder_config();

                        return RenderBackendStatus::Continue;
                    }
                    DebugCommand::SetMaximumSurfaceSize(surface_size) => {
                        self.frame_config.max_surface_override = surface_size;
                        self.update_frame_builder_config();

                        return RenderBackendStatus::Continue;
                    }
                    #[cfg(feature = "capture")]
                    DebugCommand::SaveCapture(root, bits) => {
                        let output = self.save_capture(root, bits);
                        ResultMsg::DebugOutput(output)
                    },
                    #[cfg(feature = "capture")]
                    DebugCommand::StartCaptureSequence(root, bits) => {
                        self.start_capture_sequence(root, bits);
                        return RenderBackendStatus::Continue;
                    },
                    #[cfg(feature = "capture")]
                    DebugCommand::StopCaptureSequence => {
                        self.stop_capture_sequence();
                        return RenderBackendStatus::Continue;
                    },
                    #[cfg(feature = "replay")]
                    DebugCommand::LoadCapture(path, ids, tx) => {
                        NEXT_NAMESPACE_ID.fetch_add(1, Ordering::Relaxed);
                        *frame_counter += 1;

                        let mut config = CaptureConfig::new(path, CaptureBits::all());
                        if let Some((scene_id, frame_id)) = ids {
                            config.scene_id = scene_id;
                            config.frame_id = frame_id;
                        }

                        self.load_capture(config);

                        for (id, doc) in &self.documents {
                            let captured = CapturedDocument {
                                document_id: *id,
                                root_pipeline_id: doc.loaded_scene.root_pipeline_id,
                            };
                            tx.send(captured).unwrap();
                        }

                        // Note: we can't pass `LoadCapture` here since it needs to arrive
                        // before the `PublishDocument` messages sent by `load_capture`.
                        return RenderBackendStatus::Continue;
                    }
                    DebugCommand::ClearCaches(mask) => {
                        self.resource_cache.clear(mask);
                        return RenderBackendStatus::Continue;
                    }
                    DebugCommand::EnableNativeCompositor(enable) => {
                        // Default CompositorKind should be Native
                        if let CompositorKind::Draw { .. } = self.default_compositor_kind {
                            unreachable!();
                        }

                        let compositor_kind = if enable {
                            self.default_compositor_kind
                        } else {
                            CompositorKind::default()
                        };

                        for (_, doc) in &mut self.documents {
                            doc.scene.config.compositor_kind = compositor_kind;
                            doc.frame_is_valid = false;
                        }

                        self.frame_config.compositor_kind = compositor_kind;
                        self.update_frame_builder_config();

                        // We don't want to forward this message to the renderer.
                        return RenderBackendStatus::Continue;
                    }
                    DebugCommand::SetBatchingLookback(count) => {
                        self.frame_config.batch_lookback_count = count as usize;
                        self.update_frame_builder_config();

                        return RenderBackendStatus::Continue;
                    }
                    DebugCommand::SimulateLongSceneBuild(time_ms) => {
                        let _ = self.scene_tx.send(SceneBuilderRequest::SimulateLongSceneBuild(time_ms));
                        return RenderBackendStatus::Continue;
                    }
                    DebugCommand::SetFlags(flags) => {
                        self.resource_cache.set_debug_flags(flags);
                        self.gpu_cache.set_debug_flags(flags);

                        let force_invalidation = flags.contains(DebugFlags::FORCE_PICTURE_INVALIDATION);
                        if self.frame_config.force_invalidation != force_invalidation {
                            self.frame_config.force_invalidation = force_invalidation;
                            self.update_frame_builder_config();
                        }

                        // If we're toggling on the GPU cache debug display, we
                        // need to blow away the cache. This is because we only
                        // send allocation/free notifications to the renderer
                        // thread when the debug display is enabled, and thus
                        // enabling it when the cache is partially populated will
                        // give the renderer an incomplete view of the world.
                        // And since we might as well drop all the debugging state
                        // from the renderer when we disable the debug display,
                        // we just clear the cache on toggle.
                        let changed = self.debug_flags ^ flags;
                        if changed.contains(DebugFlags::GPU_CACHE_DBG) {
                            self.gpu_cache.clear();
                        }
                        self.debug_flags = flags;

                        ResultMsg::DebugCommand(option)
                    }
                    _ => ResultMsg::DebugCommand(option),
                };
                self.result_tx.send(msg).unwrap();
                self.notifier.wake_up(true);
            }
            ApiMsg::UpdateDocuments(transaction_msgs) => {
                self.prepare_transactions(
                    transaction_msgs,
                    frame_counter,
                );
            }
            ApiMsg::SceneBuilderResult(msg) => {
                return self.process_scene_builder_result(msg, frame_counter);
            }
        }

        // Now that we are likely out of the critical path, purge a few chunks
        // from the pool. The underlying deallocation can be expensive, especially
        // with build configurations where all of the memory is zeroed, so we
        // spread the load over potentially many iterations of the event loop.
        self.chunk_pool.purge_chunks(2, 3);

        RenderBackendStatus::Continue
    }

    fn process_scene_builder_result(
        &mut self,
        msg: SceneBuilderResult,
        frame_counter: &mut u32,
    ) -> RenderBackendStatus {
        profile_scope!("sb_msg");

        match msg {
            SceneBuilderResult::Transactions(txns, result_tx) => {
                self.process_transaction(
                    txns,
                    result_tx,
                    frame_counter,
                );
                self.bookkeep_after_frames();
            },
            #[cfg(feature = "capture")]
            SceneBuilderResult::CapturedTransactions(txns, capture_config, result_tx) => {
                if let Some(ref mut old_config) = self.capture_config {
                    assert!(old_config.scene_id <= capture_config.scene_id);
                    if old_config.scene_id < capture_config.scene_id {
                        old_config.scene_id = capture_config.scene_id;
                        old_config.frame_id = 0;
                    }
                } else {
                    self.capture_config = Some(capture_config);
                }

                let built_frame = self.process_transaction(
                    txns,
                    result_tx,
                    frame_counter,
                );

                if built_frame {
                    self.save_capture_sequence();
                }

                self.bookkeep_after_frames();
            },
            #[cfg(feature = "capture")]
            SceneBuilderResult::StopCaptureSequence => {
                self.capture_config = None;
            }
            SceneBuilderResult::GetGlyphDimensions(request) => {
                let mut glyph_dimensions = Vec::with_capacity(request.glyph_indices.len());
                let instance_key = self.resource_cache.map_font_instance_key(request.key);
                if let Some(base) = self.resource_cache.get_font_instance(instance_key) {
                    let font = FontInstance::from_base(Arc::clone(&base));
                    for glyph_index in &request.glyph_indices {
                        let glyph_dim = self.resource_cache.get_glyph_dimensions(&font, *glyph_index);
                        glyph_dimensions.push(glyph_dim);
                    }
                }
                request.sender.send(glyph_dimensions).unwrap();
            }
            SceneBuilderResult::GetGlyphIndices(request) => {
                let mut glyph_indices = Vec::with_capacity(request.text.len());
                let font_key = self.resource_cache.map_font_key(request.key);
                for ch in request.text.chars() {
                    let index = self.resource_cache.get_glyph_index(font_key, ch);
                    glyph_indices.push(index);
                }
                request.sender.send(glyph_indices).unwrap();
            }
            SceneBuilderResult::FlushComplete(tx) => {
                tx.send(()).ok();
            }
            SceneBuilderResult::ExternalEvent(evt) => {
                self.notifier.external_event(evt);
            }
            SceneBuilderResult::ClearNamespace(id) => {
                self.resource_cache.clear_namespace(id);
                self.documents.retain(|doc_id, _doc| doc_id.namespace_id != id);
            }
            SceneBuilderResult::DeleteDocument(document_id) => {
                self.documents.remove(&document_id);
            }
            SceneBuilderResult::SetParameter(param) => {
                if let Parameter::Bool(BoolParameter::Multithreading, enabled) = param {
                    self.resource_cache.enable_multithreading(enabled);
                }
                let _ = self.result_tx.send(ResultMsg::SetParameter(param));
            }
            SceneBuilderResult::StopRenderBackend => {
                return RenderBackendStatus::StopRenderBackend;
            }
            SceneBuilderResult::ShutDown(sender) => {
                info!("Recycling stats: {:?}", self.recycler);
                return RenderBackendStatus::ShutDown(sender);
            }
        }

        RenderBackendStatus::Continue
    }

    fn update_frame_builder_config(&self) {
        self.send_backend_message(
            SceneBuilderRequest::SetFrameBuilderConfig(
                self.frame_config.clone()
            )
        );
    }

    fn prepare_for_frames(&mut self) {
        self.gpu_cache.prepare_for_frames();
    }

    fn bookkeep_after_frames(&mut self) {
        self.gpu_cache.bookkeep_after_frames();
    }

    fn requires_frame_build(&mut self) -> bool {
        self.gpu_cache.requires_frame_build()
    }

    fn prepare_transactions(
        &mut self,
        txns: Vec<Box<TransactionMsg>>,
        frame_counter: &mut u32,
    ) {
        self.prepare_for_frames();
        self.maybe_force_nop_documents(
            frame_counter,
            |document_id| txns.iter().any(|txn| txn.document_id == document_id));

        let mut built_frame = false;
        for mut txn in txns {
            if txn.generate_frame.as_bool() {
                txn.profile.end_time(profiler::API_SEND_TIME);
            }

            self.documents.get_mut(&txn.document_id).unwrap().profile.merge(&mut txn.profile);

            built_frame |= self.update_document(
                txn.document_id,
                txn.resource_updates.take(),
                txn.frame_ops.take(),
                txn.notifications.take(),
                txn.generate_frame.as_bool(),
                txn.generate_frame.present(),
                txn.render_reasons,
                txn.generate_frame.id(),
                txn.invalidate_rendered_frame,
                frame_counter,
                false,
                txn.creation_time,
            );
        }
        if built_frame {
            #[cfg(feature = "capture")]
            self.save_capture_sequence();
        }
        self.bookkeep_after_frames();
    }

    /// In certain cases, resources shared by multiple documents have to run
    /// maintenance operations, like cleaning up unused cache items. In those
    /// cases, we are forced to build frames for all documents, however we
    /// may not have a transaction ready for every document - this method
    /// calls update_document with the details of a fake, nop transaction just
    /// to force a frame build.
    fn maybe_force_nop_documents<F>(&mut self,
                                    frame_counter: &mut u32,
                                    document_already_present: F) where
        F: Fn(DocumentId) -> bool {
        if self.requires_frame_build() {
            let nop_documents : Vec<DocumentId> = self.documents.keys()
                .cloned()
                .filter(|key| !document_already_present(*key))
                .collect();
            #[allow(unused_variables)]
            let mut built_frame = false;
            for &document_id in &nop_documents {
                built_frame |= self.update_document(
                    document_id,
                    Vec::default(),
                    Vec::default(),
                    Vec::default(),
                    false,
                    false,
                    RenderReasons::empty(),
                    None,
                    false,
                    frame_counter,
                    false,
                    None);
            }
            #[cfg(feature = "capture")]
            match built_frame {
                true => self.save_capture_sequence(),
                _ => {},
            }
        }
    }

    fn update_document(
        &mut self,
        document_id: DocumentId,
        resource_updates: Vec<ResourceUpdate>,
        mut frame_ops: Vec<FrameMsg>,
        mut notifications: Vec<NotificationRequest>,
        mut render_frame: bool,
        mut present: bool,
        render_reasons: RenderReasons,
        generated_frame_id: Option<u64>,
        invalidate_rendered_frame: bool,
        frame_counter: &mut u32,
        has_built_scene: bool,
        start_time: Option<u64>
    ) -> bool {
        let update_doc_start = precise_time_ns();

        let requested_frame = render_frame;

        let requires_frame_build = self.requires_frame_build();
        let doc = self.documents.get_mut(&document_id).unwrap();

        // If we have a sampler, get more frame ops from it and add them
        // to the transaction. This is a hook to allow the WR user code to
        // fiddle with things after a potentially long scene build, but just
        // before rendering. This is useful for rendering with the latest
        // async transforms.
        if requested_frame {
            if let Some(ref sampler) = self.sampler {
                frame_ops.append(&mut sampler.sample(document_id, generated_frame_id));
            }
        }

        doc.has_built_scene |= has_built_scene;

        // TODO: this scroll variable doesn't necessarily mean we scrolled. It is only used
        // for something wrench specific and we should remove it.
        let mut scroll = false;
        for frame_msg in frame_ops {
            let op = doc.process_frame_msg(frame_msg);
            scroll |= op.scroll;
        }

        for update in &resource_updates {
            if let ResourceUpdate::UpdateImage(..) = update {
                doc.frame_is_valid = false;
            }
        }

        self.resource_cache.post_scene_building_update(
            resource_updates,
            &mut doc.profile,
        );

        if doc.dynamic_properties.flush_pending_updates() {
            doc.frame_is_valid = false;
            doc.hit_tester_is_valid = false;
        }

        if !doc.can_render() {
            // TODO: this happens if we are building the first scene asynchronously and
            // scroll at the same time. we should keep track of the fact that we skipped
            // composition here and do it as soon as we receive the scene.
            render_frame = false;
        }

        // Avoid re-building the frame if the current built frame is still valid.
        // However, if the resource_cache requires a frame build, _always_ do that, unless
        // doc.can_render() is false, as in that case a frame build can't happen anyway.
        // We want to ensure we do this because even if the doc doesn't have pixels it
        // can still try to access stale texture cache items.
        let build_frame = (render_frame && !doc.frame_is_valid && doc.has_pixels()) ||
            (requires_frame_build && doc.can_render());

        // Request composite is true when we want to composite frame even when
        // there is no frame update. This happens when video frame is updated under
        // external image with NativeTexture or when platform requested to composite frame.
        if invalidate_rendered_frame {
            doc.rendered_frame_is_valid = false;
            if doc.scene.config.compositor_kind.should_redraw_on_invalidation() {
                let msg = ResultMsg::ForceRedraw;
                self.result_tx.send(msg).unwrap();
            }
        }

        if build_frame {
            if !requested_frame {
                // When we don't request a frame, present defaults to false. If for some
                // reason we did not request the frame but must render it anyway, set
                // present to true (it was false as a byproduct of expecting we wouldn't
                // produce the frame but we did not explicitly opt out of it).
                present = true;
            }

            if start_time.is_some() {
              Telemetry::record_time_to_frame_build(Duration::from_nanos(precise_time_ns() - start_time.unwrap()));
            }
            profile_scope!("generate frame");

            *frame_counter += 1;

            // borrow ck hack for profile_counters
            let (pending_update, mut rendered_document) = {
                let timer_id = Telemetry::start_framebuild_time();

                let frame_stats = doc.frame_stats.take();

                let frame_memory = FrameMemory::new(self.chunk_pool.clone());

                let rendered_document = doc.build_frame(
                    &mut self.resource_cache,
                    &mut self.gpu_cache,
                    self.debug_flags,
                    &mut self.tile_caches,
                    frame_stats,
                    present,
                    render_reasons,
                    frame_memory,
                );

                debug!("generated frame for document {:?} with {} passes",
                    document_id, rendered_document.frame.passes.len());

                let msg = ResultMsg::UpdateGpuCache(self.gpu_cache.extract_updates());
                self.result_tx.send(msg).unwrap();

                Telemetry::stop_and_accumulate_framebuild_time(timer_id);

                let pending_update = self.resource_cache.pending_updates();
                (pending_update, rendered_document)
            };

            // Invalidate dirty rects if the compositing config has changed significantly
            rendered_document
                .frame
                .composite_state
                .update_dirty_rect_validity(&doc.prev_composite_descriptor);

            // Build a small struct that represents the state of the tiles to be composited.
            let composite_descriptor = rendered_document
                .frame
                .composite_state
                .descriptor
                .clone();

            // If there are texture cache updates to apply, or if the produced
            // frame is not a no-op, or the compositor state has changed,
            // then we cannot skip compositing this frame.
            if !pending_update.is_nop() ||
               !rendered_document.frame.is_nop() ||
               composite_descriptor != doc.prev_composite_descriptor {
                doc.rendered_frame_is_valid = false;
            }
            doc.prev_composite_descriptor = composite_descriptor;

            #[cfg(feature = "capture")]
            match self.capture_config {
                Some(ref mut config) => {
                    // FIXME(aosmond): document splitting causes multiple prepare frames
                    config.prepare_frame();

                    if config.bits.contains(CaptureBits::FRAME) {
                        let file_name = format!("frame-{}-{}", document_id.namespace_id.0, document_id.id);
                        config.serialize_for_frame(&rendered_document.frame, file_name);
                    }

                    let data_stores_name = format!("data-stores-{}-{}", document_id.namespace_id.0, document_id.id);
                    config.serialize_for_frame(&doc.data_stores, data_stores_name);

                    let frame_spatial_tree_name = format!("frame-spatial-tree-{}-{}", document_id.namespace_id.0, document_id.id);
                    config.serialize_for_frame::<SpatialTree, _>(&doc.spatial_tree, frame_spatial_tree_name);

                    let properties_name = format!("properties-{}-{}", document_id.namespace_id.0, document_id.id);
                    config.serialize_for_frame(&doc.dynamic_properties, properties_name);
                },
                None => {},
            }

            let update_doc_time = profiler::ns_to_ms(precise_time_ns() - update_doc_start);
            rendered_document.profile.set(profiler::UPDATE_DOCUMENT_TIME, update_doc_time);

            let msg = ResultMsg::PublishPipelineInfo(doc.updated_pipeline_info());
            self.result_tx.send(msg).unwrap();

            // Publish the frame
            self.frame_publish_id.advance();
            let msg = ResultMsg::PublishDocument(
                self.frame_publish_id,
                document_id,
                rendered_document,
                pending_update,
            );
            self.result_tx.send(msg).unwrap();
        } else if requested_frame {
            // WR-internal optimization to avoid doing a bunch of render work if
            // there's no pixels. We still want to pretend to render and request
            // a render to make sure that the callbacks (particularly the
            // new_frame_ready callback below) has the right flags.
            let msg = ResultMsg::PublishPipelineInfo(doc.updated_pipeline_info());
            self.result_tx.send(msg).unwrap();
        }

        drain_filter(
            &mut notifications,
            |n| { n.when() == Checkpoint::FrameBuilt },
            |n| { n.notify(); },
        );

        if !notifications.is_empty() {
            self.result_tx.send(ResultMsg::AppendNotificationRequests(notifications)).unwrap();
        }

        // Always forward the transaction to the renderer if a frame was requested,
        // otherwise gecko can get into a state where it waits (forever) for the
        // transaction to complete before sending new work.
        if requested_frame {
            // If rendered frame is already valid, there is no need to render frame.
            if doc.rendered_frame_is_valid {
                render_frame = false;
            } else if render_frame {
                doc.rendered_frame_is_valid = true;
            }
            let params = api::FrameReadyParams {
                present,
                render: render_frame,
                scrolled: scroll,
            };
            self.notifier.new_frame_ready(document_id, self.frame_publish_id, &params);
        }

        if !doc.hit_tester_is_valid {
            doc.rebuild_hit_tester();
        }

        build_frame
    }

    fn send_backend_message(&self, msg: SceneBuilderRequest) {
        self.scene_tx.send(msg).unwrap();
    }

    fn report_memory(&mut self, tx: Sender<Box<MemoryReport>>) {
        let mut report = Box::new(MemoryReport::default());
        let ops = self.size_of_ops.as_mut().unwrap();
        let op = ops.size_of_op;
        report.gpu_cache_metadata = self.gpu_cache.size_of(ops);
        for doc in self.documents.values() {
            report.clip_stores += doc.scene.clip_store.size_of(ops);
            report.hit_testers += match &doc.hit_tester {
                Some(hit_tester) => hit_tester.size_of(ops),
                None => 0,
            };

            doc.data_stores.report_memory(ops, &mut report)
        }

        (*report) += self.resource_cache.report_memory(op);
        report.texture_cache_structures = self.resource_cache
            .texture_cache
            .report_memory(ops);

        // Send a message to report memory on the scene-builder thread, which
        // will add its report to this one and send the result back to the original
        // thread waiting on the request.
        self.send_backend_message(
            SceneBuilderRequest::ReportMemory(report, tx)
        );
    }

    #[cfg(feature = "capture")]
    fn save_capture_sequence(&mut self) {
        if let Some(ref mut config) = self.capture_config {
            let deferred = self.resource_cache.save_capture_sequence(config);

            let backend = PlainRenderBackend {
                frame_config: self.frame_config.clone(),
                resource_sequence_id: config.resource_id,
                documents: self.documents
                    .iter()
                    .map(|(id, doc)| (*id, doc.view))
                    .collect(),
            };
            config.serialize_for_frame(&backend, "backend");

            if !deferred.is_empty() {
                let msg = ResultMsg::DebugOutput(DebugOutput::SaveCapture(config.clone(), deferred));
                self.result_tx.send(msg).unwrap();
            }
        }
    }
}

impl RenderBackend {
    #[cfg(feature = "capture")]
    // Note: the mutable `self` is only needed here for resolving blob images
    fn save_capture(
        &mut self,
        root: PathBuf,
        bits: CaptureBits,
    ) -> DebugOutput {
        use std::fs;
        use crate::render_task_graph::dump_render_tasks_as_svg;

        debug!("capture: saving {:?}", root);
        if !root.is_dir() {
            if let Err(e) = fs::create_dir_all(&root) {
                panic!("Unable to create capture dir: {:?}", e);
            }
        }
        let config = CaptureConfig::new(root, bits);

        if config.bits.contains(CaptureBits::FRAME) {
            self.prepare_for_frames();
        }

        for (&id, doc) in &mut self.documents {
            debug!("\tdocument {:?}", id);
            if config.bits.contains(CaptureBits::FRAME) {
                // Temporarily force invalidation otherwise the render task graph dump is empty.
                let force_invalidation = std::mem::replace(&mut doc.scene.config.force_invalidation, true);
                let frame_memory = FrameMemory::new(self.chunk_pool.clone());
                let rendered_document = doc.build_frame(
                    &mut self.resource_cache,
                    &mut self.gpu_cache,
                    self.debug_flags,
                    &mut self.tile_caches,
                    None,
                    true,
                    RenderReasons::empty(),
                    frame_memory,
                );

                doc.scene.config.force_invalidation = force_invalidation;

                // After we rendered the frames, there are pending updates to both
                // GPU cache and resources. Instead of serializing them, we are going to make sure
                // they are applied on the `Renderer` side.
                let msg_update_gpu_cache = ResultMsg::UpdateGpuCache(self.gpu_cache.extract_updates());
                self.result_tx.send(msg_update_gpu_cache).unwrap();
                //TODO: write down doc's pipeline info?
                // it has `pipeline_epoch_map`,
                // which may capture necessary details for some cases.
                let file_name = format!("frame-{}-{}", id.namespace_id.0, id.id);
                config.serialize_for_frame(&rendered_document.frame, file_name);
                let file_name = format!("spatial-{}-{}", id.namespace_id.0, id.id);
                config.serialize_tree_for_frame(&doc.spatial_tree, file_name);
                let file_name = format!("built-primitives-{}-{}", id.namespace_id.0, id.id);
                config.serialize_for_frame(&doc.scene.prim_store, file_name);
                let file_name = format!("built-clips-{}-{}", id.namespace_id.0, id.id);
                config.serialize_for_frame(&doc.scene.clip_store, file_name);
                let file_name = format!("scratch-{}-{}", id.namespace_id.0, id.id);
                config.serialize_for_frame(&doc.scratch.primitive, file_name);
                let file_name = format!("render-tasks-{}-{}.svg", id.namespace_id.0, id.id);
                let mut render_tasks_file = fs::File::create(&config.file_path_for_frame(file_name, "svg"))
                    .expect("Failed to open the SVG file.");
                dump_render_tasks_as_svg(
                    &rendered_document.frame.render_tasks,
                    &mut render_tasks_file
                ).unwrap();

                let file_name = format!("texture-cache-color-linear-{}-{}.svg", id.namespace_id.0, id.id);
                let mut texture_file = fs::File::create(&config.file_path_for_frame(file_name, "svg"))
                    .expect("Failed to open the SVG file.");
                self.resource_cache.texture_cache.dump_color8_linear_as_svg(&mut texture_file).unwrap();

                let file_name = format!("texture-cache-color8-glyphs-{}-{}.svg", id.namespace_id.0, id.id);
                let mut texture_file = fs::File::create(&config.file_path_for_frame(file_name, "svg"))
                    .expect("Failed to open the SVG file.");
                self.resource_cache.texture_cache.dump_color8_glyphs_as_svg(&mut texture_file).unwrap();

                let file_name = format!("texture-cache-alpha8-glyphs-{}-{}.svg", id.namespace_id.0, id.id);
                let mut texture_file = fs::File::create(&config.file_path_for_frame(file_name, "svg"))
                    .expect("Failed to open the SVG file.");
                self.resource_cache.texture_cache.dump_alpha8_glyphs_as_svg(&mut texture_file).unwrap();

                let file_name = format!("texture-cache-alpha8-linear-{}-{}.svg", id.namespace_id.0, id.id);
                let mut texture_file = fs::File::create(&config.file_path_for_frame(file_name, "svg"))
                    .expect("Failed to open the SVG file.");
                self.resource_cache.texture_cache.dump_alpha8_linear_as_svg(&mut texture_file).unwrap();
            }

            let data_stores_name = format!("data-stores-{}-{}", id.namespace_id.0, id.id);
            config.serialize_for_frame(&doc.data_stores, data_stores_name);

            let frame_spatial_tree_name = format!("frame-spatial-tree-{}-{}", id.namespace_id.0, id.id);
            config.serialize_for_frame::<SpatialTree, _>(&doc.spatial_tree, frame_spatial_tree_name);

            let properties_name = format!("properties-{}-{}", id.namespace_id.0, id.id);
            config.serialize_for_frame(&doc.dynamic_properties, properties_name);
        }

        if config.bits.contains(CaptureBits::FRAME) {
            // TODO: there is no guarantee that we won't hit this case, but we want to
            // report it here if we do. If we don't, it will simply crash in
            // Renderer::render_impl and give us less information about the source.
            assert!(!self.requires_frame_build(), "Caches were cleared during a capture.");
            self.bookkeep_after_frames();
        }

        debug!("\tscene builder");
        self.send_backend_message(
            SceneBuilderRequest::SaveScene(config.clone())
        );

        debug!("\tresource cache");
        let (resources, deferred) = self.resource_cache.save_capture(&config.root);

        info!("\tbackend");
        let backend = PlainRenderBackend {
            frame_config: self.frame_config.clone(),
            resource_sequence_id: 0,
            documents: self.documents
                .iter()
                .map(|(id, doc)| (*id, doc.view))
                .collect(),
        };

        config.serialize_for_frame(&backend, "backend");
        config.serialize_for_frame(&resources, "plain-resources");

        if config.bits.contains(CaptureBits::FRAME) {
            let msg_update_resources = ResultMsg::UpdateResources {
                resource_updates: self.resource_cache.pending_updates(),
                memory_pressure: false,
            };
            self.result_tx.send(msg_update_resources).unwrap();
            // Save the texture/glyph/image caches.
            info!("\tresource cache");
            let caches = self.resource_cache.save_caches(&config.root);
            config.serialize_for_resource(&caches, "resource_cache");
            info!("\tgpu cache");
            config.serialize_for_resource(&self.gpu_cache, "gpu_cache");
        }

        DebugOutput::SaveCapture(config, deferred)
    }

    #[cfg(feature = "capture")]
    fn start_capture_sequence(
        &mut self,
        root: PathBuf,
        bits: CaptureBits,
    ) {
        self.send_backend_message(
            SceneBuilderRequest::StartCaptureSequence(CaptureConfig::new(root, bits))
        );
    }

    #[cfg(feature = "capture")]
    fn stop_capture_sequence(
        &mut self,
    ) {
        self.send_backend_message(
            SceneBuilderRequest::StopCaptureSequence
        );
    }

    #[cfg(feature = "replay")]
    fn load_capture(
        &mut self,
        mut config: CaptureConfig,
    ) {
        debug!("capture: loading {:?}", config.frame_root());
        let backend = config.deserialize_for_frame::<PlainRenderBackend, _>("backend")
            .expect("Unable to open backend.ron");

        // If this is a capture sequence, then the ID will be non-zero, and won't
        // match what is loaded, but for still captures, the ID will be zero.
        let first_load = backend.resource_sequence_id == 0;
        if self.loaded_resource_sequence_id != backend.resource_sequence_id || first_load {
            // FIXME(aosmond): We clear the documents because when we update the
            // resource cache, we actually wipe and reload, because we don't
            // know what is the same and what has changed. If we were to keep as
            // much of the resource cache state as possible, we could avoid
            // flushing the document state (which has its own dependecies on the
            // cache).
            //
            // FIXME(aosmond): If we try to load the next capture in the
            // sequence too quickly, we may lose resources we depend on in the
            // current frame. This can cause panics. Ideally we would not
            // advance to the next frame until the FrameRendered event for all
            // of the pipelines.
            self.documents.clear();

            config.resource_id = backend.resource_sequence_id;
            self.loaded_resource_sequence_id = backend.resource_sequence_id;

            let plain_resources = config.deserialize_for_resource::<PlainResources, _>("plain-resources")
                .expect("Unable to open plain-resources.ron");
            let caches_maybe = config.deserialize_for_resource::<PlainCacheOwn, _>("resource_cache");

            // Note: it would be great to have `RenderBackend` to be split
            // rather explicitly on what's used before and after scene building
            // so that, for example, we never miss anything in the code below:

            let plain_externals = self.resource_cache.load_capture(
                plain_resources,
                caches_maybe,
                &config,
            );

            let msg_load = ResultMsg::DebugOutput(
                DebugOutput::LoadCapture(config.clone(), plain_externals)
            );
            self.result_tx.send(msg_load).unwrap();

            self.gpu_cache = match config.deserialize_for_resource::<GpuCache, _>("gpu_cache") {
                Some(gpu_cache) => gpu_cache,
                None => GpuCache::new(),
            };
        }

        self.frame_config = backend.frame_config;

        let mut scenes_to_build = Vec::new();

        for (id, view) in backend.documents {
            debug!("\tdocument {:?}", id);
            let scene_name = format!("scene-{}-{}", id.namespace_id.0, id.id);
            let scene = config.deserialize_for_scene::<Scene, _>(&scene_name)
                .expect(&format!("Unable to open {}.ron", scene_name));

            let scene_spatial_tree_name = format!("scene-spatial-tree-{}-{}", id.namespace_id.0, id.id);
            let scene_spatial_tree = config.deserialize_for_scene::<SceneSpatialTree, _>(&scene_spatial_tree_name)
                .expect(&format!("Unable to open {}.ron", scene_spatial_tree_name));

            let interners_name = format!("interners-{}-{}", id.namespace_id.0, id.id);
            let interners = config.deserialize_for_scene::<Interners, _>(&interners_name)
                .expect(&format!("Unable to open {}.ron", interners_name));

            let data_stores_name = format!("data-stores-{}-{}", id.namespace_id.0, id.id);
            let data_stores = config.deserialize_for_frame::<DataStores, _>(&data_stores_name)
                .expect(&format!("Unable to open {}.ron", data_stores_name));

            let properties_name = format!("properties-{}-{}", id.namespace_id.0, id.id);
            let properties = config.deserialize_for_frame::<SceneProperties, _>(&properties_name)
                .expect(&format!("Unable to open {}.ron", properties_name));

            let frame_spatial_tree_name = format!("frame-spatial-tree-{}-{}", id.namespace_id.0, id.id);
            let frame_spatial_tree = config.deserialize_for_frame::<SpatialTree, _>(&frame_spatial_tree_name)
                .expect(&format!("Unable to open {}.ron", frame_spatial_tree_name));

            // Update the document if it still exists, rather than replace it entirely.
            // This allows us to preserve state information such as the frame stamp,
            // which is necessary for cache sanity.
            match self.documents.entry(id) {
                Occupied(entry) => {
                    let doc = entry.into_mut();
                    doc.view = view;
                    doc.loaded_scene = scene.clone();
                    doc.data_stores = data_stores;
                    doc.spatial_tree = frame_spatial_tree;
                    doc.dynamic_properties = properties;
                    doc.frame_is_valid = false;
                    doc.rendered_frame_is_valid = false;
                    doc.has_built_scene = false;
                    doc.hit_tester_is_valid = false;
                }
                Vacant(entry) => {
                    let doc = Document {
                        id,
                        scene: BuiltScene::empty(),
                        removed_pipelines: Vec::new(),
                        view,
                        stamp: FrameStamp::first(id),
                        frame_builder: FrameBuilder::new(),
                        dynamic_properties: properties,
                        hit_tester: None,
                        shared_hit_tester: Arc::new(SharedHitTester::new()),
                        frame_is_valid: false,
                        hit_tester_is_valid: false,
                        rendered_frame_is_valid: false,
                        has_built_scene: false,
                        data_stores,
                        scratch: ScratchBuffer::default(),
                        spatial_tree: frame_spatial_tree,
                        minimap_data: FastHashMap::default(),
                        loaded_scene: scene.clone(),
                        prev_composite_descriptor: CompositeDescriptor::empty(),
                        dirty_rects_are_valid: false,
                        profile: TransactionProfile::new(),
                        rg_builder: RenderTaskGraphBuilder::new(),
                        frame_stats: None,
                    };
                    entry.insert(doc);
                }
            };

            let frame_name = format!("frame-{}-{}", id.namespace_id.0, id.id);
            let frame = config.deserialize_for_frame::<Frame, _>(frame_name);
            let build_frame = match frame {
                Some(frame) => {
                    info!("\tloaded a built frame with {} passes", frame.passes.len());

                    let msg_update = ResultMsg::UpdateGpuCache(self.gpu_cache.extract_updates());
                    self.result_tx.send(msg_update).unwrap();

                    self.frame_publish_id.advance();
                    let msg_publish = ResultMsg::PublishDocument(
                        self.frame_publish_id,
                        id,
                        RenderedDocument {
                            frame,
                            profile: TransactionProfile::new(),
                            render_reasons: RenderReasons::empty(),
                            frame_stats: None,
                        },
                        self.resource_cache.pending_updates(),
                    );
                    self.result_tx.send(msg_publish).unwrap();

                    let params = api::FrameReadyParams {
                        present: true,
                        render: true,
                        scrolled: false,
                    };
                    self.notifier.new_frame_ready(id, self.frame_publish_id, &params);

                    // We deserialized the state of the frame so we don't want to build
                    // it (but we do want to update the scene builder's state)
                    false
                }
                None => true,
            };

            scenes_to_build.push(LoadScene {
                document_id: id,
                scene,
                view: view.scene.clone(),
                config: self.frame_config.clone(),
                fonts: self.resource_cache.get_fonts(),
                build_frame,
                interners,
                spatial_tree: scene_spatial_tree,
            });
        }

        if !scenes_to_build.is_empty() {
            self.send_backend_message(
                SceneBuilderRequest::LoadScenes(scenes_to_build)
            );
        }
    }
}
