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
use api::{IdNamespace, PipelineId, RenderBackendId, RenderNotifier, SampledScrollOffset};
use api::{NotificationRequest, Checkpoint, QualitySettings};
use api::{FramePublishId, RenderReasons};
use api::units::*;
use api::channel::{single_msg_channel, Sender, Receiver};
use crate::bump_allocator::ChunkPool;
use crate::AsyncPropertySampler;
#[cfg(feature = "replay")]
use crate::dl_interner::DlBuilderMap;
#[cfg(any(feature = "capture", feature = "replay"))]
use crate::render_api::CaptureBits;
#[cfg(feature = "replay")]
use crate::render_api::CapturedDocument;
use crate::glyph_cache::GlyphCache;
use crate::picture_textures::PictureTextures;
use crate::render_api::{MemoryReport, TransactionMsg, ResourceUpdate, ApiMsg, FrameMsg, ClearCache, DebugCommand, ResourceCacheInit, WindowRegistration};
use crate::texture_cache::TextureCache;
use glyph_rasterizer::GlyphRasterizer;
use crate::clip::{ClipIntern, PolygonIntern, ClipStoreScratchBuffer};
use crate::filterdata::FilterDataIntern;
#[cfg(any(feature = "capture", feature = "replay"))]
use crate::capture::CaptureConfig;
use crate::composite::CompositeDescriptor;
use crate::frame_builder::{FrameBuilder, FrameBuilderConfig, FrameScratchBuffer};
use glyph_rasterizer::FontInstance;
use crate::hit_test::{HitTest, HitTester, SharedHitTester};
use crate::intern::DataStore;
#[cfg(any(feature = "capture", feature = "replay"))]
use crate::internal_types::DebugOutput;
use crate::internal_types::{FastHashMap, FrameId, FrameStamp, RenderedDocument, ResultMsg};
use malloc_size_of::{MallocSizeOf, MallocSizeOfOps};
use crate::picture::{PictureScratchBuffer, RasterConfig};
use crate::surface::SurfaceInfo;
use crate::tile_cache::{SliceId, TileCacheInstance, TileCacheParams};
use crate::picture::PictureInstance;
use crate::prim_store::{PrimitiveScratchBuffer, PrimitiveInstance};
use crate::prim_store::{PrimitiveKind, PrimTemplateCommonData};
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
use crate::scene_debug::SceneDebugOverride;
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
use std::mem;
#[cfg(feature = "capture")]
use std::path::PathBuf;
#[cfg(feature = "replay")]
use crate::frame_builder::Frame;
use core::time::Duration;
use crate::util::{MaxRect, Recycler, VecHelper, drain_filter};
#[cfg(feature = "debugger")]
use crate::debugger::DebugQueryKind;

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
                let mut insertions = 0;
                let mut removals = 0;
                $(
                    let (added, removed) = self.$name.apply_updates(
                        updates.$name,
                        profile,
                    );
                    insertions += added;
                    removals += removed;
                )+
                profile.set(profiler::INTERN_INSERTIONS, insertions);
                profile.set(profiler::INTERN_REMOVALS, removals);
            }

            /// Fill in any data store slots that are missing relative to the
            /// interners. Used when loading a capture, where the serialized
            /// data store can lag the interners by a scene build.
            #[cfg(feature = "replay")]
            fn reconcile_from_interners(&mut self, interners: &Interners) {
                $(
                    interners.$name.reconcile_datastore(&mut self.$name);
                )+
            }
        }
    }
}

crate::enumerate_interners!(declare_data_stores);

macro_rules! declare_dl_stores {
    ( $( $field:ident : $key:ty => $template:ty, $gauge:ident, $report:ident, )* ) => {
        /// The follower stores for everything interned by the content display
        /// list builders, fed by `DlUpdates` from the scene builder.
        ///
        /// Separate from `DataStores` rather than a field on it: these are per
        /// render backend while `DataStores` is per document, so a field would
        /// mean either duplicating entries per document or introducing
        /// sharing. It also means "which store does this handle refer to?" is
        /// answered by the handle's type, with no runtime discriminator.
        #[cfg_attr(feature = "capture", derive(Serialize))]
        #[cfg_attr(feature = "replay", derive(Deserialize))]
        #[derive(Default)]
        pub struct DlStores {
            $( pub $field: crate::dl_interner::DlStore<$key, $template>, )*
            uids: crate::dl_interner::DlUids,
        }

        impl DlStores {
            /// Apply a transaction's ops and report what they did. The counts
            /// join the interners' in `INTERN_INSERTIONS` / `INTERN_REMOVALS`,
            /// and the store sizes go to the per-type `INTERNED_*` gauges.
            fn apply(
                &mut self,
                updates: DlUpdates,
                profile: &mut TransactionProfile,
            ) -> (usize, usize) {
                let mut totals = (0, 0);
                $(
                    // `apply` consumes the op list, so the count comes first.
                    let (ins, rem) = crate::dl_interner::count_dl_ops(&updates.$field);
                    totals.0 += ins;
                    totals.1 += rem;
                    self.$field.apply(updates.$field, &mut self.uids);
                    profile.set(profiler::$gauge, self.$field.len());
                )*
                totals
            }

            /// A copy of these stores with `updates` applied, closes aside,
            /// for frame building against ops that are held back from the
            /// stores themselves.
            fn with_pending(&self, updates: &DlUpdates) -> DlStores {
                let mut stores = DlStores {
                    $( $field: self.$field.clone(), )*
                    uids: self.uids.clone(),
                };
                $( stores.$field.apply(updates.$field.clone(), &mut stores.uids); )*
                stores
            }

            /// Release the namespaces a transaction's `DlUpdates::closes`
            /// held back from `apply`.
            fn close(&mut self, namespaces: &[crate::dl_interner::DlNamespace]) {
                for &namespace in namespaces {
                    $( self.$field.close(namespace); )*
                }
            }

            fn report_memory(&self, ops: &mut MallocSizeOfOps, r: &mut MemoryReport) {
                $( r.interning.dl_stores.$report += self.$field.size_of(ops); )*
            }

            /// See `DlStore::reconcile`.
            #[cfg(feature = "replay")]
            fn reconcile(&mut self, builders: &crate::dl_interner::DlBuilderMap) {
                $( self.$field.reconcile(builders); )*
            }
        }
    }
}

crate::enumerate_dl_stores!(declare_dl_stores);

impl DataStores {
    /// Returns the local rect for a primitive. For most primitives, this is
    /// the device-snapped local rect carried on the per-draw header. For
    /// pictures, the rect is reconstructed from the picture's raster surface
    /// since it's only known during frame building.
    pub fn get_local_prim_rect(
        &self,
        prim_instance: &PrimitiveInstance,
        snapped_pattern_rect: LayoutRect,
        pictures: &[PictureInstance],
        surfaces: &[SurfaceInfo],
    ) -> LayoutRect {
        match prim_instance.kind {
            PrimitiveKind::Picture { pic_index, .. } => {
                let pic = &pictures[pic_index.0 as usize];

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
            _ => snapped_pattern_rect,
        }
    }

    /// Returns the local coverage (space occupied) for a primitive. For most
    /// primitives, this is the device-snapped local rect carried on the
    /// per-draw header. For pictures, the coverage is reconstructed from the
    /// picture's raster surface since it's only known during frame building.
    pub fn get_local_prim_coverage_rect(
        &self,
        prim_instance: &PrimitiveInstance,
        snapped_pattern_rect: LayoutRect,
        pictures: &[PictureInstance],
        surfaces: &[SurfaceInfo],
    ) -> LayoutRect {
        match prim_instance.kind {
            PrimitiveKind::Picture { pic_index, .. } => {
                let pic = &pictures[pic_index.0 as usize];

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
            _ => snapped_pattern_rect,
        }
    }

    /// Returns true if this primitive has anti-aliasing enabled.
    pub fn prim_has_anti_aliasing(
        &self,
        prim_instance: &PrimitiveInstance,
        dl_stores: &DlStores,
    ) -> bool {
        match prim_instance.kind {
            PrimitiveKind::Picture { .. } => {
                false
            }
            _ => {
                self.as_common_data(prim_instance, dl_stores).flags.contains(PrimitiveFlags::ANTIALISED)
            }
        }
    }

    /// The primitive's authored local rect, before device-pixel snapping. Lives
    /// in the interned template; picture prims have no common data (and their
    /// snapped rect is discarded in favour of the surface coverage rect, see
    /// `get_local_prim_coverage_rect`) so they report an empty rect.
    pub fn prim_rect(
        &self,
        prim_inst: &PrimitiveInstance,
        dl_stores: &DlStores,
    ) -> LayoutRect {
        match prim_inst.kind {
            PrimitiveKind::Picture { .. } => LayoutRect::zero(),
            _ => self.as_common_data(prim_inst, dl_stores).prim_rect,
        }
    }

    /// The primitive's own local clip rect, before device-pixel snapping. Lives
    /// in the interned template alongside `prim_rect`; picture prims have no
    /// common data and carry no local clip of their own, so they report
    /// `max_rect`.
    pub fn local_clip_rect(
        &self,
        prim_inst: &PrimitiveInstance,
        dl_stores: &DlStores,
    ) -> LayoutRect {
        match prim_inst.kind {
            PrimitiveKind::Picture { .. } => LayoutRect::max_rect(),
            _ => self.as_common_data(prim_inst, dl_stores).local_clip_rect,
        }
    }

    /// The common template data of a primitive, whichever side interned it:
    /// `self` for the scene builder's interners, `dl_stores` for the content
    /// display list builder's.
    pub fn as_common_data<'a>(
        &'a self,
        prim_inst: &PrimitiveInstance,
        dl_stores: &'a DlStores,
    ) -> &'a PrimTemplateCommonData {
        match prim_inst.kind {
            PrimitiveKind::Rectangle { handle } => {
                &dl_stores.rectangles[handle].common
            }
            PrimitiveKind::Image { handle } => {
                &dl_stores.images[handle].common
            }
            PrimitiveKind::ImageBorder { data_handle, .. } => {
                let prim_data = &self.image_border[data_handle];
                &prim_data.common
            }
            PrimitiveKind::LineDecoration { handle } => {
                &dl_stores.line_decorations[handle].common
            }
            PrimitiveKind::LinearGradient { data_handle, .. } => {
                let prim_data = &self.linear_grad[data_handle];
                &prim_data.common
            }
            PrimitiveKind::NormalBorder { data_handle, .. } => {
                let prim_data = &self.normal_border[data_handle];
                &prim_data.common
            }
            PrimitiveKind::Picture { .. } => {
                panic!("BUG: picture prims don't have common data!");
            }
            PrimitiveKind::RadialGradient { data_handle, .. } => {
                let prim_data = &self.radial_grad[data_handle];
                &prim_data.common
            }
            PrimitiveKind::ConicGradient { data_handle, .. } => {
                let prim_data = &self.conic_grad[data_handle];
                &prim_data.common
            }
            PrimitiveKind::TextRun { data_handle, .. }  => {
                let prim_data = &self.text_run[data_handle];
                &prim_data.common
            }
            PrimitiveKind::YuvImage { handle } => {
                &dl_stores.yuv_images[handle].common
            }
            PrimitiveKind::BackdropCapture { data_handle, .. } => {
                let prim_data = &self.backdrop_capture[data_handle];
                &prim_data.common
            }
            PrimitiveKind::BackdropRender { data_handle, .. } => {
                let prim_data = &self.backdrop_render[data_handle];
                &prim_data.common
            }
            PrimitiveKind::BoxShadow { handle } => {
                &dl_stores.box_shadows[handle].common
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

    /// Incremented each time a new built scene is swapped in. Lets the remote
    /// debugger detect that primitive indices it holds are stale.
    scene_generation: u64,

    /// Debug-only per-primitive modifications applied during frame building.
    /// Reset whenever a new built scene is swapped in.
    debug_override: SceneDebugOverride,
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
            scene_generation: 0,
            debug_override: SceneDebugOverride::empty(),
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
                tracy_rs::profile_scope!("SetScrollOffset");

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
        dl_stores: &DlStores,
        debug_flags: DebugFlags,
        tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
        frame_stats: Option<FullFrameStats>,
        present: bool,
        render_reasons: RenderReasons,
        chunk_pool: Arc<ChunkPool>,
    ) -> RenderedDocument {
        let frame_build_start_time = zeitstempel::now();

        // Advance to the next frame.
        self.stamp.advance();

        assert!(self.stamp.frame_id() != FrameId::INVALID,
                "First frame increment must happen before build_frame()");

        let frame = {
            let frame = self.frame_builder.build(
                &mut self.scene,
                present,
                resource_cache,
                &mut self.rg_builder,
                self.stamp,
                self.view.scene.device_rect.min,
                &self.dynamic_properties,
                &mut self.data_stores,
                dl_stores,
                &mut self.scratch,
                debug_flags,
                &self.debug_override,
                tile_caches,
                &mut self.spatial_tree,
                self.dirty_rects_are_valid,
                &mut self.profile,
                // Consume the minimap data. If APZ wants a minimap rendered
                // on the next frame, it will add new entries to the minimap
                // data during sampling.
                mem::take(&mut self.minimap_data),
                chunk_pool,
            );

            frame
        };

        self.frame_is_valid = true;
        self.dirty_rects_are_valid = true;

        self.has_built_scene = false;

        let frame_build_time_ms =
            profiler::ns_to_ms(zeitstempel::now() - frame_build_start_time);
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

    /// Build a frame without changing the state of the current scene.
    ///
    /// This is useful to render arbitrary content into to images in
    /// the resource cache for later use without affecting what is
    /// currently being displayed.
    fn process_offscreen_scene(
        &mut self,
        mut txn: OffscreenBuiltScene,
        resource_cache: &mut ResourceCache,
        dl_stores: &DlStores,
        chunk_pool: Arc<ChunkPool>,
        debug_flags: DebugFlags,
    ) -> RenderedDocument {
        let mut profile = TransactionProfile::new();
        self.stamp.advance();

        // The offscreen scene was interned into this document's interners, so
        // its items have already been materialized into the document data store
        // (whose templates are immutable at frame-build time) by the combined
        // interner-update delta applied earlier in this transaction. The frame
        // build below simply borrows that store; the transient offscreen items
        // are GC'd by a later end_frame once the temporary pipeline is gone.

        let mut spatial_tree = SpatialTree::new();
        spatial_tree.apply_updates(txn.spatial_tree_updates);

        let mut tile_caches = FastHashMap::default();
        self.update_tile_caches_for_new_scene(
            mem::take(&mut txn.scene.tile_cache_config.tile_caches),
            &mut tile_caches,
            resource_cache,
        );

        let present = false;

        let frame = self.frame_builder.build(
            &mut txn.scene,
            present,
            resource_cache,
            &mut self.rg_builder,
            self.stamp, // TODO(nical)
            self.view.scene.device_rect.min,
            &self.dynamic_properties,
            &self.data_stores,
            dl_stores,
            &mut self.scratch,
            debug_flags,
            &SceneDebugOverride::empty(),
            &mut tile_caches,
            &mut spatial_tree,
            self.dirty_rects_are_valid,
            &mut profile,
            // Consume the minimap data. If APZ wants a minimap rendered
            // on the next frame, it will add new entries to the minimap
            // data during sampling.
            mem::take(&mut self.minimap_data),
            chunk_pool,
        );

        RenderedDocument {
            frame,
            profile,
            render_reasons: RenderReasons::SNAPSHOT,
            frame_stats: None,
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

        self.scene_generation += 1;
        self.debug_override = SceneDebugOverride::empty();
    }

    /// Serialize the picture / primitive tree of the current built scene for
    /// the remote debugger.
    #[cfg(feature = "debugger")]
    fn scene_debug_tree(&self, dl_stores: &DlStores) -> api::debugger::SceneDebugTree {
        crate::scene_debug::build_debug_tree(
            &self.scene,
            &self.data_stores,
            dl_stores,
            &self.spatial_tree,
            &self.scratch.primitive.frame,
            &self.dynamic_properties,
            self.view.scene.device_rect.to_f32(),
            self.scene_generation,
        )
    }

    /// Install a debug override sent by the remote debugger, rejecting it if
    /// it targets a scene generation other than the current one.
    #[cfg(feature = "debugger")]
    fn set_debug_override(
        &mut self,
        debug_override: &api::debugger::SceneDebugOverride,
    ) -> Result<(), String> {
        self.debug_override = SceneDebugOverride::from_debugger(
            debug_override,
            self.scene_generation,
            self.scene.prim_instances.len(),
        )?;
        self.frame_is_valid = false;
        Ok(())
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

/// State owned by a single window living inside a render backend thread.
///
/// Most fields here used to live directly on `RenderBackend`. They were moved
/// here so that multiple windows can eventually share a backend thread without
/// stepping on each other's GPU-side bookkeeping. Today there is one
/// `WindowState` per `RenderBackend`.
pub struct WindowState {
    /// Outgoing channel to the `Renderer` that owns this window's GL context.
    result_tx: Sender<ResultMsg>,

    /// Set by `SceneBuilderResult::StopWindow` once the window's `Renderer` is
    /// about to be destroyed. The receiving end of `result_tx` dies with it, so a
    /// stopped window must not build frames or produce results any more, even
    /// though it stays registered until `ApiMsg::UnregisterWindow` arrives.
    stopped: bool,

    notifier: Box<dyn RenderNotifier>,
    sampler: Option<Box<dyn AsyncPropertySampler + Send>>,

    /// CPU-side bookkeeping for GPU resources. Per-window because the
    /// resources live on the window's GL context.
    resource_cache: ResourceCache,
    chunk_pool: Arc<ChunkPool>,

    /// Tile caches persisted between scenes for this window's documents.
    tile_caches: FastHashMap<SliceId, Box<TileCacheInstance>>,

    frame_config: FrameBuilderConfig,
    debug_flags: DebugFlags,

    recycler: Recycler,

    /// The id of the latest PublishDocument sent on `result_tx`.
    frame_publish_id: FramePublishId,

    #[cfg(feature = "capture")]
    /// If `Some`, do 'sequence capture' logging, recording updated documents,
    /// frames, etc. This is set only through messages from the scene builder,
    /// so all control of sequence capture goes through there.
    capture_config: Option<CaptureConfig>,

    #[cfg(feature = "replay")]
    loaded_resource_sequence_id: u32,
}

impl WindowState {
    /// Send a message to this window's `Renderer` (infallible).
    fn send(&self, msg: ResultMsg) {
        self.result_tx.send(msg).unwrap();
    }
}

/// The render backend is responsible for transforming high level display lists into
/// GPU-friendly work which is then submitted to the renderer in the form of a frame::Frame.
///
/// The render backend operates on its own thread.
pub struct RenderBackend {
    api_rx: Receiver<ApiMsg>,
    scene_tx: Sender<SceneBuilderRequest>,

    documents: FastHashMap<DocumentId, Document>,

    /// Per-window state, keyed by `RenderBackendId`. Currently contains at
    /// most one entry.
    windows: FastHashMap<RenderBackendId, WindowState>,

    /// Reverse lookup from a document to the window that owns it.
    document_to_window: FastHashMap<DocumentId, RenderBackendId>,

    /// The most recently touched window. Used to spread the per-message
    /// `chunk_pool.purge_chunks` work across windows: each api-message
    /// dispatch purges a couple of chunks from this window only, so the
    /// pool that just allocated work is the one that gets gradually
    /// drained. Cleared when the window is unregistered.
    last_touched_window: Option<RenderBackendId>,

    /// Follower stores for the items interned by the content display list
    /// builders. Per backend rather than per document, matching the scene
    /// builder that feeds them: a handle names its own namespace, so items
    /// from several pipelines - and so several documents and windows - live
    /// in one store and can be batched together.
    dl_stores: DlStores,
    /// Ops for `dl_stores` from transactions that built no scene, per
    /// document, waiting for that document's next scene swap. The store may
    /// only advance together with a swap: the scene being drawn in the
    /// meantime can still reference what these ops remove or, on a builder
    /// change, empty out. Flushed once the document is gone.
    held_dl_updates: FastHashMap<DocumentId, DlUpdates>,

    size_of_ops: Option<MallocSizeOfOps>,
    namespace_alloc_by_client: bool,
}

/// Build a `ResourceCache` (and its sub-caches) from the params shipped via
/// `WindowRegistration`. Called on the render-backend thread so that the
/// allocations are attributed there.
fn build_resource_cache(init: ResourceCacheInit) -> ResourceCache {
    let ResourceCacheInit {
        max_internal_texture_size,
        image_tiling_threshold,
        color_cache_formats,
        swizzle_settings,
        texture_cache_config,
        picture_tile_size,
        picture_texture_filter,
        workers,
        dedicated_glyph_raster_thread,
        supports_r8_texture_upload,
        fonts,
        blob_image_handler,
        enable_multithreading,
    } = init;

    let texture_cache = TextureCache::new(
        max_internal_texture_size,
        image_tiling_threshold,
        color_cache_formats,
        swizzle_settings,
        &texture_cache_config,
    );
    let picture_textures = PictureTextures::new(
        picture_tile_size,
        picture_texture_filter,
    );
    let glyph_rasterizer = GlyphRasterizer::new(
        workers,
        dedicated_glyph_raster_thread,
        supports_r8_texture_upload,
    );
    let glyph_cache = GlyphCache::new();

    let mut resource_cache = ResourceCache::new(
        texture_cache,
        picture_textures,
        glyph_rasterizer,
        glyph_cache,
        fonts,
        blob_image_handler,
    );
    resource_cache.enable_multithreading(enable_multithreading);
    resource_cache
}

impl RenderBackend {
    pub fn new(
        api_rx: Receiver<ApiMsg>,
        scene_tx: Sender<SceneBuilderRequest>,
        size_of_ops: Option<MallocSizeOfOps>,
        namespace_alloc_by_client: bool,
    ) -> RenderBackend {
        RenderBackend {
            api_rx,
            scene_tx,
            documents: FastHashMap::default(),
            windows: FastHashMap::default(),
            document_to_window: FastHashMap::default(),
            last_touched_window: None,
            dl_stores: DlStores::default(),
            held_dl_updates: FastHashMap::default(),
            size_of_ops,
            namespace_alloc_by_client,
        }
    }

    /// Install a window. Called via `ApiMsg::RegisterWindow`.
    ///
    /// `ResourceCache` (and its sub-caches) are constructed here, on the
    /// render-backend thread, so that their allocations are attributed to
    /// the thread that ultimately owns them. The caller ships only
    /// [`ResourceCacheInit`] params across the api channel.
    pub fn register_window(&mut self, reg: Box<WindowRegistration>) {
        let WindowRegistration {
            id,
            result_tx,
            notifier,
            sampler,
            resource_cache,
            chunk_pool,
            frame_config,
            debug_flags,
        } = *reg;

        if let Some(ref sampler) = sampler {
            sampler.register();
        }

        // Note: `SetSceneBuilderHooks` is sent by `renderer::init` directly
        // on the scene channel (the same channel as `AddDocument` and
        // subsequent transactions), so the SB always observes hooks
        // installation before any document or transaction for this window.

        let resource_cache = build_resource_cache(resource_cache);

        let window = WindowState {
            result_tx,
            stopped: false,
            notifier,
            sampler,
            resource_cache,
            chunk_pool,
            tile_caches: FastHashMap::default(),
            frame_config,
            debug_flags,
            recycler: Recycler::new(),
            frame_publish_id: FramePublishId::first(),
            #[cfg(feature = "capture")]
            capture_config: None,
            #[cfg(feature = "replay")]
            loaded_resource_sequence_id: 0,
        };

        let old = self.windows.insert(id, window);
        debug_assert!(old.is_none(), "window {:?} registered twice", id);
    }

    /// Drop the state for a window, including any documents that belong to it.
    fn unregister_window(&mut self, id: RenderBackendId) {
        if let Some(win) = self.windows.remove(&id) {
            // Drop all documents owned by this window.
            let doomed: Vec<DocumentId> = self.document_to_window.iter()
                .filter_map(|(doc_id, win_id)| if *win_id == id { Some(*doc_id) } else { None })
                .collect();
            for doc_id in doomed {
                self.documents.remove(&doc_id);
                self.document_to_window.remove(&doc_id);
            }
            if self.last_touched_window == Some(id) {
                self.last_touched_window = None;
            }

            if let Some(ref sampler) = win.sampler {
                sampler.deregister();
            }
            // Clear any hooks we installed on the SB for this window.
            let _ = self.scene_tx.send(SceneBuilderRequest::SetSceneBuilderHooks(id, None));
            win.notifier.shut_down();
        }
    }

    /// The given window, if it is still able to receive results -- registered,
    /// and not stopped by `stop_render_backend`. Use this rather than
    /// `windows.get_mut` on any path that ends in `WindowState::publish`.
    fn live_window(&mut self, id: RenderBackendId) -> Option<&mut WindowState> {
        self.windows.get_mut(&id).filter(|win| !win.stopped)
    }

    /// Returns the documents owned by the given window. Used to scope
    /// debug commands to one window in a multi-window backend.
    fn documents_for_window(&self, win_id: RenderBackendId) -> Vec<DocumentId> {
        self.document_to_window.iter()
            .filter_map(|(d, w)| if *w == win_id { Some(*d) } else { None })
            .collect()
    }

    /// Send a `SceneBuilderRequest::SetFrameBuilderConfig` reflecting the
    /// given window's current `frame_config`. Scoped per-window because the
    /// scene builder may be shared across windows on the same backend.
    fn update_frame_builder_config_for(&self, win_id: RenderBackendId) {
        if let Some(win) = self.windows.get(&win_id) {
            self.send_backend_message(
                SceneBuilderRequest::SetFrameBuilderConfig(win_id, win.frame_config.clone()),
            );
        }
    }

    pub fn next_namespace_id() -> IdNamespace {
        IdNamespace(NEXT_NAMESPACE_ID.fetch_add(1, Ordering::Relaxed) as u32)
    }

    pub fn run(&mut self) {
        let mut frame_counter: u32 = 0;
        let mut status = RenderBackendStatus::Continue;

        // `sampler.register()` is now called inside `register_window`.

        while let RenderBackendStatus::Continue = status {
            status = match self.api_rx.recv() {
                Ok(msg) => self.process_api_msg(msg, &mut frame_counter),
                Err(..) => RenderBackendStatus::ShutDown(None),
            };

            // Note: we deliberately do NOT shut down on `self.windows.is_empty()`.
            // In shared-pool mode (gfx.webrender.render-backend-thread-count >= 1)
            // this backend thread serves multiple windows over its lifetime and
            // a future window may land on it via round-robin. The thread exits
            // naturally when all `api_tx` clones drop and `api_rx.recv()`
            // returns `Err`, which happens when the owning RenderBackendPool
            // (and the RenderApi instances using it) are dropped.
        }

        // Drain remaining api messages until the scene builder confirms it
        // has exited. This lets the SB push out its in-flight results
        // (including transactions and flush responses) without panicking on
        // a closed channel, and lets late `UnregisterWindow` / `StopWindow` /
        // shutdown calls from the api side get their ack channels signaled
        // before we drop the receiver.
        while let Ok(msg) = self.api_rx.recv() {
            match msg {
                ApiMsg::SceneBuilderResult(SceneBuilderResult::ShutDown(_)) => break,
                ApiMsg::SceneBuilderResult(SceneBuilderResult::FlushComplete(tx)) => {
                    let _ = tx.send(());
                }
                ApiMsg::SceneBuilderResult(SceneBuilderResult::StopWindow(_, tx)) => {
                    let _ = tx.send(());
                }
                ApiMsg::UnregisterWindow(_, Some(ack)) => {
                    let _ = ack.send(());
                }
                _ => {}
            }
        }

        self.documents.clear();
        self.document_to_window.clear();

        // Drain any windows that weren't explicitly unregistered.
        for (_, win) in self.windows.drain() {
            if let Some(ref sampler) = win.sampler {
                sampler.deregister();
            }
            win.notifier.shut_down();
        }

        if let RenderBackendStatus::ShutDown(Some(sender)) = status {
            let _ = sender.send(());
        }
    }

    fn process_transaction(
        &mut self,
        mut txns: Vec<Box<BuiltTransaction>>,
        mut result_tx: Option<Sender<SceneSwapResult>>,
        frame_counter: &mut u32,
    ) -> bool {
        self.maybe_force_nop_documents(
            frame_counter,
            |document_id| txns.iter().any(|txn| txn.document_id == document_id));

        if result_tx.is_some() {
            // TODO: The scene swap handshake logic below is bogus and only works
            // because in practice there scene swapping batches of transaction only
            // contain a single transaction. The result_tx should be associated
            // to the transaction that does the swap.
            debug_assert!(txns.len() == 1);
        }

        let mut built_frame = false;
        let mut dl_closes = Vec::new();
        for mut txn in txns.drain(..) {
           let has_built_scene = txn.built_scene.is_some();

            // Ops held for documents that have since been removed. Nothing is
            // left to protect, and they were produced before anything below,
            // in particular before a later transaction closes their
            // namespaces.
            let dead: Vec<DocumentId> = self.held_dl_updates
                .keys()
                .filter(|id| !self.documents.contains_key(id))
                .copied()
                .collect();
            for id in dead {
                let mut held = self.held_dl_updates.remove(&id).unwrap();
                let closes = mem::take(&mut held.closes);
                self.dl_stores.apply(held, &mut txn.profile);
                self.dl_stores.close(&closes);
            }

            // The previous transaction's namespace releases, held back until
            // its offscreen scenes had been frame-built; those can reference a
            // pipeline removed in the same transaction. Applied ahead of this
            // transaction's ops, which may reopen a released namespace.
            self.dl_stores.close(&mem::take(&mut dl_closes));

            // The stores are per backend and pure followers with no way to ask
            // for a delta a second time, so every transaction's ops are applied
            // whether or not its results are consumed below. They are applied
            // together with the document's scene swap, though: a transaction
            // that built no scene - the document has no root pipeline - leaves
            // the current scene being drawn, and that scene can still reference
            // what these ops remove or empty out. Such ops wait for the next
            // transaction that does swap, unless the document is gone.
            let mut dl_updates = self.held_dl_updates
                .remove(&txn.document_id)
                .unwrap_or_default();
            dl_updates.append(mem::take(&mut txn.dl_updates));
            let mut offscreen_dl_stores = None;
            let dl_counts = if has_built_scene || !self.documents.contains_key(&txn.document_id) {
                dl_closes = mem::take(&mut dl_updates.closes);
                self.dl_stores.apply(dl_updates, &mut txn.profile)
            } else {
                // The offscreen scenes were built against these ops, so they
                // are frame-built against a copy of the stores that has them.
                if !txn.offscreen_scenes.is_empty() {
                    offscreen_dl_stores = Some(self.dl_stores.with_pending(&dl_updates));
                }
                if !dl_updates.is_empty() {
                    self.held_dl_updates.insert(txn.document_id, dl_updates);
                }
                (0, 0)
            };

            // Look up the window that owns this document. Between the scene
            // builder enqueueing this transaction and us getting here the
            // window may have been unregistered (both the document and the
            // routing entry are gone) or stopped (its `Renderer`, and with it
            // the receiving end of `result_tx`, is gone). Either way there is
            // nobody left to consume the results, so drop the txn.
            let win_id = match self.document_to_window.get(&txn.document_id) {
                Some(&id) if self.windows.get(&id).is_some_and(|win| !win.stopped) => id,
                _ => {
                    if let Some(tx) = result_tx.take() {
                        tx.send(SceneSwapResult::Aborted).unwrap();
                    }
                    continue;
                }
            };
            let win = self.windows.get_mut(&win_id).unwrap();
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
                let last_sampled_scroll_offsets = if win.sampler.is_some() {
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
                        &mut win.recycler,
                        &mut win.tile_caches,
                        &mut win.resource_cache,
                    );
                }

                // If there are any additions or removals of clip modes
                // during the scene build, apply them to the data store now.
                // This needs to happen before we build the hit tester.
                if let Some(updates) = txn.interner_updates.take() {
                    doc.data_stores.apply_updates(updates, &mut doc.profile);
                }

                // Added rather than set: `apply_updates` has just set these from
                // the interners, and the follower stores contribute to the same
                // totals.
                let (insertions, removals) = dl_counts;
                doc.profile.add(profiler::INTERN_INSERTIONS, insertions);
                doc.profile.add(profiler::INTERN_REMOVALS, removals);

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

                if let Some(tx) = result_tx.take() {
                    let (resume_tx, resume_rx) = single_msg_channel();
                    tx.send(SceneSwapResult::Complete(resume_tx)).unwrap();
                    // Block until the post-swap hook has completed on
                    // the scene builder thread. We need to do this before
                    // we can sample from the sampler hook which might happen
                    // in the update_document call below.
                    resume_rx.recv().ok();
                }

                win.resource_cache.add_rasterized_blob_images(
                    txn.rasterized_blobs.take(),
                    &mut doc.profile,
                );

                for offscreen_scene in txn.offscreen_scenes.drain(..) {
                    win.resource_cache.post_scene_building_update(
                        txn.resource_updates.take(),
                        &mut doc.profile,
                    );

                    let rendered_document = doc.process_offscreen_scene(
                        offscreen_scene,
                        &mut win.resource_cache,
                        offscreen_dl_stores.as_ref().unwrap_or(&self.dl_stores),
                        win.chunk_pool.clone(),
                        win.debug_flags,
                    );

                    let pending_update = win.resource_cache.pending_updates();

                    let msg = ResultMsg::PublishDocument(
                        win.frame_publish_id,
                        txn.document_id,
                        rendered_document,
                        pending_update,
                    );
                    win.send(msg);

                    let params = api::FrameReadyParams {
                        present: false,
                        render: true,
                        scrolled: false,
                        tracked: false,
                    };

                    win.notifier.new_frame_ready(
                        txn.document_id,
                        win.frame_publish_id,
                        &params
                    );
                }
            } else {
                // The document was removed while we were building it, skip it.
                // TODO: we might want to just ensure that removed documents are
                // always forwarded to the scene builder thread to avoid this case.
                if let Some(tx) = result_tx.take() {
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
                txn.tracked,
                RenderReasons::SCENE,
                None,
                txn.invalidate_rendered_frame,
                frame_counter,
                has_built_scene,
                None,
            );

            if self.windows.get(&win_id).unwrap().debug_flags.contains(DebugFlags::DUMP_SPATIAL_TREE) {
                if let Some(doc) = self.documents.get(&txn.document_id) {
                    let spatial_tree = doc.spatial_tree.print_to_string();
                    if !spatial_tree.is_empty() {
                        eprintln!(
                            "-- WebRender spatial tree ({:?}) --\n{}",
                            txn.document_id, spatial_tree
                        );
                    }
                }
            }
        }
        self.dl_stores.close(&dl_closes);

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
            ApiMsg::RegisterWindow(reg) => {
                self.register_window(reg);
            }
            ApiMsg::UnregisterWindow(id, ack) => {
                self.unregister_window(id);
                if let Some(ack) = ack {
                    let _ = ack.send(());
                }
            }
            ApiMsg::AddDocument(document_id, initial_size, backend_id) => {
                debug_assert!(self.windows.contains_key(&backend_id),
                    "AddDocument targets unregistered backend {:?}", backend_id);
                let document = Document::new(
                    document_id,
                    initial_size,
                );
                let old = self.documents.insert(document_id, document);
                debug_assert!(old.is_none());
                self.document_to_window.insert(document_id, backend_id);
            }
            ApiMsg::TrimTransientResources {
                backend_id,
                trim_upload_buffers,
            } => {
                // Render targets in this pool are inactive and can be freed
                // without clearing persistent caches. The last published frame
                // may still reference them, so the renderer discards that frame
                // before applying the resulting texture frees.
                let document_ids = self.documents_for_window(backend_id);
                for document_id in document_ids {
                    if let Some(doc) = self.documents.get_mut(&document_id) {
                        // The built frame graph can reference the cleared render
                        // targets. Ensure the first GenerateFrame after Resume
                        // builds and publishes a replacement instead of only
                        // requesting a composite of the now-discarded frame.
                        doc.frame_is_valid = false;
                    }
                }

                if let Some(win) = self.windows.get_mut(&backend_id) {
                    win.resource_cache.clear(ClearCache::RENDER_TARGETS);

                    // A paused renderer may not produce another frame, so
                    // forward the texture frees and wake it immediately.
                    //
                    // RenderBackend is the sole ResultMsg producer for this
                    // window's result channel. This update is therefore a FIFO
                    // barrier before any replacement PublishDocument generated
                    // after Resume.
                    let resource_updates = win.resource_cache.pending_updates();
                    let msg = ResultMsg::UpdateResources {
                        resource_updates,
                        memory_pressure: false,
                        discard_active_documents: true,
                        trim_upload_buffers,
                    };
                    win.result_tx.send(msg).unwrap();
                    win.notifier.wake_up(false);
                }
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
                for (_, doc) in &mut self.documents {
                    doc.scratch.memory_pressure();
                }

                for win in self.windows.values_mut() {
                    win.resource_cache.clear(ClearCache::all());

                    for tile_cache in win.tile_caches.values_mut() {
                        tile_cache.memory_pressure(&mut win.resource_cache);
                    }

                    // A stopped window has no `Renderer` left to carry out the
                    // resulting texture deletions, and its GPU resources go away
                    // with the device anyway. Freeing the cpu-side caches above
                    // and the chunk pool below is still worth doing.
                    if !win.stopped {
                        let resource_updates = win.resource_cache.pending_updates();
                        let msg = ResultMsg::UpdateResources {
                            resource_updates,
                            memory_pressure: true,
                            discard_active_documents: false,
                            trim_upload_buffers: false,
                        };
                        win.send(msg);
                        win.notifier.wake_up(false);
                    }

                    win.chunk_pool.purge_all_chunks();
                }
            }
            ApiMsg::ReportMemory(tx) => {
                self.report_memory(tx);
            }
            ApiMsg::DebugCommand(backend_id, option) => {
                let msg = match option {
                    DebugCommand::SetPictureTileSize(tile_size) => {
                        if let Some(win) = self.windows.get_mut(&backend_id) {
                            win.frame_config.tile_size_override = tile_size;
                        }
                        self.update_frame_builder_config_for(backend_id);

                        return RenderBackendStatus::Continue;
                    }
                    DebugCommand::SetMaximumSurfaceSize(surface_size) => {
                        if let Some(win) = self.windows.get_mut(&backend_id) {
                            win.frame_config.max_surface_override = surface_size;
                        }
                        self.update_frame_builder_config_for(backend_id);

                        return RenderBackendStatus::Continue;
                    }
                    DebugCommand::GenerateFrame => {
                        let documents = self.documents_for_window(backend_id);
                        for document_id in documents {
                            let mut invalidation_config = false;
                            if let Some(doc) = self.documents.get_mut(&document_id) {
                                doc.frame_is_valid = false;
                                invalidation_config = doc.scene.config.force_invalidation;
                                doc.scene.config.force_invalidation = true;
                            }

                            self.update_document(
                                document_id,
                                Vec::default(),
                                Vec::default(),
                                Vec::default(),
                                true,
                                true,
                                false,
                                RenderReasons::empty(),
                                None,
                                true,
                                frame_counter,
                                false,
                                None,
                            );

                            if let Some(doc) = self.documents.get_mut(&document_id) {
                                doc.scene.config.force_invalidation = invalidation_config;
                            }
                        }

                        return RenderBackendStatus::Continue;
                    }
                    #[cfg(feature = "debugger")]
                    DebugCommand::CaptureRenderDoc(..) => {
                        // A single-frame RenderDoc capture can't replay WebRender's
                        // persistent caches (picture tiles, glyph atlas, image cache)
                        // populated in earlier frames. So make the captured frame
                        // re-render everything from scratch: clear cached resources so
                        // glyphs/images re-rasterize and re-upload, and force a full
                        // invalidated rebuild so all picture cache tiles re-rasterize.
                        // Then forward the command so the renderer captures that frame.
                        for win in self.windows.values_mut() {
                            win.resource_cache.clear(ClearCache::all());
                        }

                        let documents: Vec<DocumentId> = self.documents.keys()
                            .cloned()
                            .collect();
                        for document_id in documents {
                            let mut invalidation_config = false;
                            if let Some(doc) = self.documents.get_mut(&document_id) {
                                doc.frame_is_valid = false;
                                invalidation_config = doc.scene.config.force_invalidation;
                                doc.scene.config.force_invalidation = true;
                            }

                            self.update_document(
                                document_id,
                                Vec::default(),
                                Vec::default(),
                                Vec::default(),
                                true,
                                true,
                                false,
                                RenderReasons::empty(),
                                None,
                                true,
                                frame_counter,
                                false,
                                None,
                            );

                            if let Some(doc) = self.documents.get_mut(&document_id) {
                                doc.scene.config.force_invalidation = invalidation_config;
                            }
                        }

                        // Forward to the renderer to arm the capture for the frame
                        // just published by the rebuild above.
                        ResultMsg::DebugCommand(option)
                    }
                    #[cfg(feature = "capture")]
                    DebugCommand::SaveCapture(root, bits) => {
                        let output = self.save_capture(backend_id, root, bits);
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

                        self.load_capture(backend_id, config);

                        for doc_id in self.documents_for_window(backend_id) {
                            if let Some(doc) = self.documents.get(&doc_id) {
                                let captured = CapturedDocument {
                                    document_id: doc_id,
                                    root_pipeline_id: doc.loaded_scene.root_pipeline_id,
                                };
                                tx.send(captured).unwrap();
                            }
                        }

                        // Note: we can't pass `LoadCapture` here since it needs to arrive
                        // before the `PublishDocument` messages sent by `load_capture`.
                        return RenderBackendStatus::Continue;
                    }
                    #[cfg(feature = "debugger")]
                    DebugCommand::Query(ref query) => {
                        match query.kind {
                            DebugQueryKind::SpatialTree { .. } => {
                                if let Some(doc_id) = self.documents_for_window(backend_id).first() {
                                    if let Some(doc) = self.documents.get(doc_id) {
                                        let result = doc.spatial_tree.print_to_string();
                                        query.result.send(result).ok();
                                    }
                                }
                                return RenderBackendStatus::Continue;
                            }
                            DebugQueryKind::Scene { .. } => {
                                if let Some(doc_id) = self.documents_for_window(backend_id).first() {
                                    if let Some(doc) = self.documents.get(doc_id) {
                                        let tree = doc.scene_debug_tree(&self.dl_stores);
                                        let result = serde_json::to_string(&tree).expect("bug");
                                        query.result.send(result).ok();
                                    }
                                }
                                return RenderBackendStatus::Continue;
                            }
                            DebugQueryKind::CompositorView { .. } |
                            DebugQueryKind::CompositorConfig { .. } |
                            DebugQueryKind::Textures { .. } |
                            DebugQueryKind::Shaders { .. } |
                            DebugQueryKind::ShaderSource { .. } => {
                                ResultMsg::DebugCommand(option)
                            }
                        }
                    }
                    DebugCommand::ClearCaches(mask) => {
                        if let Some(win) = self.windows.get_mut(&backend_id) {
                            win.resource_cache.clear(mask);
                        }
                        return RenderBackendStatus::Continue;
                    }
                    DebugCommand::SetBatchingLookback(count) => {
                        if let Some(win) = self.windows.get_mut(&backend_id) {
                            win.frame_config.batch_lookback_count = count as usize;
                        }
                        self.update_frame_builder_config_for(backend_id);

                        return RenderBackendStatus::Continue;
                    }
                    DebugCommand::SimulateLongSceneBuild(time_ms) => {
                        let _ = self.scene_tx.send(SceneBuilderRequest::SimulateLongSceneBuild(time_ms));
                        return RenderBackendStatus::Continue;
                    }
                    #[cfg(feature = "debugger")]
                    DebugCommand::SetSceneDebugOverride(ref debug_override, ref tx) => {
                        let mut result = Err("No document".to_string());
                        if let Some(doc_id) = self.documents_for_window(backend_id).first() {
                            if let Some(doc) = self.documents.get_mut(doc_id) {
                                result = doc.set_debug_override(debug_override);
                            }
                        }
                        tx.send(result).ok();
                        return RenderBackendStatus::Continue;
                    }
                    DebugCommand::SetFlags(flags) => {
                        let force_invalidation = flags.contains(DebugFlags::FORCE_PICTURE_INVALIDATION);
                        let needs_update = match self.windows.get_mut(&backend_id) {
                            Some(win) => {
                                win.resource_cache.set_debug_flags(flags);
                                let needs_update = win.frame_config.force_invalidation != force_invalidation;
                                if needs_update {
                                    win.frame_config.force_invalidation = force_invalidation;
                                }
                                win.debug_flags = flags;
                                needs_update
                            }
                            None => false,
                        };
                        if needs_update {
                            let doc_ids = self.documents_for_window(backend_id);
                            for doc_id in doc_ids {
                                if let Some(doc) = self.documents.get_mut(&doc_id) {
                                    doc.scene.config.force_invalidation = force_invalidation;
                                }
                            }
                            self.update_frame_builder_config_for(backend_id);
                        }

                        ResultMsg::DebugCommand(option)
                    }
                    _ => ResultMsg::DebugCommand(option),
                };
                if let Some(win) = self.live_window(backend_id) {
                    win.send(msg);
                    win.notifier.wake_up(true);
                }
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
        //
        // Each api-message dispatch only touches the pool of the window we
        // most recently interacted with — updated in `update_document`.
        // Pools belonging to currently-idle windows are not allocating, so
        // they don't need a purge tick here.
        if let Some(win) = self.last_touched_window.and_then(|id| self.windows.get(&id)) {
            win.chunk_pool.purge_chunks(2, 3);
        }

        RenderBackendStatus::Continue
    }

    fn process_scene_builder_result(
        &mut self,
        msg: SceneBuilderResult,
        frame_counter: &mut u32,
    ) -> RenderBackendStatus {
        tracy_rs::profile_scope!("sb_msg");

        match msg {
            SceneBuilderResult::Transactions(txns, result_tx) => {
                self.process_transaction(
                    txns,
                    result_tx,
                    frame_counter,
                );
            },
            #[cfg(feature = "capture")]
            SceneBuilderResult::CapturedTransactions(txns, capture_config, result_tx) => {
                // All txns in a single batch belong to the same window;
                // route the capture config via that window's id.
                let win_id = txns.first()
                    .and_then(|txn| self.document_to_window.get(&txn.document_id).copied());
                if let Some(win) = win_id.and_then(|id| self.windows.get_mut(&id)) {
                    if let Some(ref mut old_config) = win.capture_config {
                        assert!(old_config.scene_id <= capture_config.scene_id);
                        if old_config.scene_id < capture_config.scene_id {
                            old_config.scene_id = capture_config.scene_id;
                            old_config.frame_id = 0;
                        }
                    } else {
                        win.capture_config = Some(capture_config);
                    }
                }

                let built_frame = self.process_transaction(
                    txns,
                    result_tx,
                    frame_counter,
                );

                if built_frame {
                    self.save_capture_sequence();
                }
            },
            #[cfg(feature = "capture")]
            SceneBuilderResult::StopCaptureSequence => {
                // Stopping a capture sequence is process-wide; clear the
                // active config on every window so `save_capture_sequence`
                // stops emitting captures from any of them.
                for win in self.windows.values_mut() {
                    win.capture_config = None;
                }
            }
            SceneBuilderResult::GetGlyphDimensions(backend_id, request) => {
                let mut glyph_dimensions = Vec::with_capacity(request.glyph_indices.len());
                if let Some(win) = self.windows.get_mut(&backend_id) {
                    let instance_key = win.resource_cache.map_font_instance_key(request.key);
                    if let Some(base) = win.resource_cache.get_font_instance(instance_key) {
                        let font = FontInstance::from_base(Arc::clone(&base));
                        for glyph_index in &request.glyph_indices {
                            let glyph_dim = win.resource_cache.get_glyph_dimensions(&font, *glyph_index);
                            glyph_dimensions.push(glyph_dim);
                        }
                    }
                }
                request.sender.send(glyph_dimensions).unwrap();
            }
            SceneBuilderResult::GetGlyphIndices(backend_id, request) => {
                let mut glyph_indices = Vec::with_capacity(request.text.len());
                if let Some(win) = self.windows.get_mut(&backend_id) {
                    let font_key = win.resource_cache.map_font_key(request.key);
                    for ch in request.text.chars() {
                        let index = win.resource_cache.get_glyph_index(font_key, ch);
                        glyph_indices.push(index);
                    }
                }
                request.sender.send(glyph_indices).unwrap();
            }
            SceneBuilderResult::FlushComplete(tx) => {
                tx.send(()).ok();
            }
            SceneBuilderResult::StopWindow(backend_id, tx) => {
                // Everything this window submitted before `stop_render_backend`
                // has been processed by now: this took the slow path through the
                // scene builder and arrived on the api channel behind it all.
                if let Some(win) = self.windows.get_mut(&backend_id) {
                    win.stopped = true;
                }
                let _ = tx.send(());
            }
            SceneBuilderResult::ExternalEvent(backend_id, evt) => {
                if let Some(win) = self.windows.get_mut(&backend_id) {
                    win.notifier.external_event(evt);
                }
            }
            SceneBuilderResult::ClearNamespace(backend_id, id) => {
                if let Some(win) = self.windows.get_mut(&backend_id) {
                    win.resource_cache.clear_namespace(id);
                }
                self.documents.retain(|doc_id, _doc| doc_id.namespace_id != id);
                self.document_to_window.retain(|doc_id, _| doc_id.namespace_id != id);
            }
            SceneBuilderResult::DeleteDocument(document_id) => {
                self.documents.remove(&document_id);
                self.document_to_window.remove(&document_id);
            }
            SceneBuilderResult::SetParameter(backend_id, param) => {
                if let Some(win) = self.live_window(backend_id) {
                    if let Parameter::Bool(BoolParameter::Multithreading, enabled) = param {
                        win.resource_cache.enable_multithreading(enabled);
                    }
                    win.send(ResultMsg::SetParameter(param));
                }
            }
            SceneBuilderResult::ShutDown(sender) => {
                for (_, win) in &self.windows {
                    info!("Recycling stats: {:?}", win.recycler);
                }
                return RenderBackendStatus::ShutDown(sender);
            }
        }

        RenderBackendStatus::Continue
    }

    fn requires_frame_build(&mut self) -> bool {
        false // TODO(nical)
    }

    fn prepare_transactions(
        &mut self,
        txns: Vec<Box<TransactionMsg>>,
        frame_counter: &mut u32,
    ) {
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
                txn.generate_frame.tracked(),
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
            let mut built_frame = false;
            for &document_id in &nop_documents {
                built_frame |= self.update_document(
                    document_id,
                    Vec::default(),
                    Vec::default(),
                    Vec::default(),
                    false,
                    false,
                    false,
                    RenderReasons::empty(),
                    None,
                    false,
                    frame_counter,
                    false,
                    None);
            }
            match built_frame {
                true =>
                {
                    #[cfg(feature = "capture")]
                    self.save_capture_sequence()
                }
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
        tracked: bool,
        render_reasons: RenderReasons,
        generated_frame_id: Option<u64>,
        invalidate_rendered_frame: bool,
        frame_counter: &mut u32,
        has_built_scene: bool,
        start_time: Option<u64>
    ) -> bool {
        let update_doc_start = zeitstempel::now();

        let requires_frame_build = self.requires_frame_build();

        let win_id = *self.document_to_window.get(&document_id)
            .expect("update_document for unknown document");

        // A stopped window has lost the receiving end of its `result_tx` along
        // with its `Renderer`, so there is nobody left to consume a frame.
        // Dropping `notifications` here notifies `Checkpoint::TransactionDropped`.
        if self.windows.get(&win_id).is_none_or(|win| win.stopped) {
            return false;
        }

        self.last_touched_window = Some(win_id);
        let win = self.windows.get_mut(&win_id).unwrap();
        let requested_frame = render_frame || win.frame_config.force_invalidation;

        let doc = self.documents.get_mut(&document_id).unwrap();

        // If we have a sampler, get more frame ops from it and add them
        // to the transaction. This is a hook to allow the WR user code to
        // fiddle with things after a potentially long scene build, but just
        // before rendering. This is useful for rendering with the latest
        // async transforms.
        if requested_frame {
            if let Some(ref sampler) = win.sampler {
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

        win.resource_cache.post_scene_building_update(
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
                win.send(msg);
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
              Telemetry::record_time_to_frame_build(Duration::from_nanos(zeitstempel::now() - start_time.unwrap()));
            }
            tracy_rs::profile_scope!("generate frame");

            *frame_counter += 1;

            // borrow ck hack for profile_counters
            let (pending_update, mut rendered_document) = {
                let timer_id = Telemetry::start_framebuild_time();

                let frame_stats = doc.frame_stats.take();

                let rendered_document = doc.build_frame(
                    &mut win.resource_cache,
                    &self.dl_stores,
                    win.debug_flags,
                    &mut win.tile_caches,
                    frame_stats,
                    present,
                    render_reasons,
                    win.chunk_pool.clone(),
                );

                debug!("generated frame for document {:?} with {} passes",
                    document_id, rendered_document.frame.passes.len());

                Telemetry::stop_and_accumulate_framebuild_time(timer_id);

                let pending_update = win.resource_cache.pending_updates();
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
            match win.capture_config {
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

            let update_doc_time = profiler::ns_to_ms(zeitstempel::now() - update_doc_start);
            rendered_document.profile.set(profiler::UPDATE_DOCUMENT_TIME, update_doc_time);

            let msg = ResultMsg::PublishPipelineInfo(doc.updated_pipeline_info());
            win.send(msg);

            // Publish the frame
            win.frame_publish_id.advance();
            let msg = ResultMsg::PublishDocument(
                win.frame_publish_id,
                document_id,
                rendered_document,
                pending_update,
            );
            win.send(msg);
        } else if requested_frame {
            // WR-internal optimization to avoid doing a bunch of render work if
            // there's no pixels. We still want to pretend to render and request
            // a render to make sure that the callbacks (particularly the
            // new_frame_ready callback below) has the right flags.
            let msg = ResultMsg::PublishPipelineInfo(doc.updated_pipeline_info());
            win.send(msg);
        }

        drain_filter(
            &mut notifications,
            |n| { n.when() == Checkpoint::FrameBuilt },
            |n| { n.notify(); },
        );

        if !notifications.is_empty() {
            win.send(ResultMsg::AppendNotificationRequests(notifications));
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
                tracked,
            };
            win.notifier.new_frame_ready(document_id, win.frame_publish_id, &params);
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
        for doc in self.documents.values() {
            report.clip_stores += doc.scene.clip_store.size_of(ops);
            report.hit_testers += match &doc.hit_tester {
                Some(hit_tester) => hit_tester.size_of(ops),
                None => 0,
            };

            doc.data_stores.report_memory(ops, &mut report)
        }

        // Per backend, not per document, like the stores themselves.
        self.dl_stores.report_memory(ops, &mut report);

        for win in self.windows.values_mut() {
            (*report) += win.resource_cache.report_memory(op);
            report.texture_cache_structures += win.resource_cache
                .texture_cache
                .report_memory(ops);
        }

        // Send a message to report memory on the scene-builder thread, which
        // will add its report to this one and send the result back to the original
        // thread waiting on the request.
        self.send_backend_message(
            SceneBuilderRequest::ReportMemory(report, tx)
        );
    }

    #[cfg(feature = "capture")]
    fn save_capture_sequence(&mut self) {
        // Per-window capture: each window that has an active `capture_config`
        // serializes its own documents. The plumbing iterates `windows` so
        // that multiple windows on the same backend can capture independently.
        let active_windows: Vec<RenderBackendId> = self.windows.iter()
            .filter_map(|(id, w)| {
                if w.capture_config.is_some() && !w.stopped { Some(*id) } else { None }
            })
            .collect();
        for backend_id in active_windows {
            let owned_doc_views: FastHashMap<DocumentId, DocumentView> = self.document_to_window.iter()
                .filter(|(_, w)| **w == backend_id)
                .filter_map(|(d, _)| self.documents.get(d).map(|doc| (*d, doc.view)))
                .collect();
            let win = self.windows.get_mut(&backend_id).unwrap();
            if let Some(ref mut config) = win.capture_config {
                let deferred = win.resource_cache.save_capture_sequence(config);

                let backend = PlainRenderBackend {
                    frame_config: win.frame_config.clone(),
                    resource_sequence_id: config.resource_id,
                    documents: owned_doc_views,
                };
                config.serialize_for_frame(&backend, "backend");

                if !deferred.is_empty() {
                    let msg = ResultMsg::DebugOutput(DebugOutput::SaveCapture(config.clone(), deferred));
                    win.send(msg);
                }
            }
        }
    }
}

impl RenderBackend {
    #[cfg(feature = "capture")]
    // Note: the mutable `self` is only needed here for resolving blob images
    fn save_capture(
        &mut self,
        backend_id: RenderBackendId,
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

        let win = self.windows.get_mut(&backend_id).unwrap();
        // Only documents owned by this window are captured.
        let doc_ids: Vec<DocumentId> = self.document_to_window.iter()
            .filter_map(|(d, w)| if *w == backend_id { Some(*d) } else { None })
            .collect();
        for id in doc_ids.iter().copied() {
            let doc = match self.documents.get_mut(&id) {
                Some(d) => d,
                None => continue,
            };
            debug!("\tdocument {:?}", id);
            if config.bits.contains(CaptureBits::FRAME) {
                // Temporarily force invalidation otherwise the render task graph dump is empty.
                let force_invalidation = std::mem::replace(&mut doc.scene.config.force_invalidation, true);
                let rendered_document = doc.build_frame(
                    &mut win.resource_cache,
                    &self.dl_stores,
                    win.debug_flags,
                    &mut win.tile_caches,
                    None,
                    true,
                    RenderReasons::empty(),
                    win.chunk_pool.clone(),
                );

                doc.scene.config.force_invalidation = force_invalidation;

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
                win.resource_cache.texture_cache.dump_color8_linear_as_svg(&mut texture_file).unwrap();

                let file_name = format!("texture-cache-color8-glyphs-{}-{}.svg", id.namespace_id.0, id.id);
                let mut texture_file = fs::File::create(&config.file_path_for_frame(file_name, "svg"))
                    .expect("Failed to open the SVG file.");
                win.resource_cache.texture_cache.dump_color8_glyphs_as_svg(&mut texture_file).unwrap();

                let file_name = format!("texture-cache-alpha8-glyphs-{}-{}.svg", id.namespace_id.0, id.id);
                let mut texture_file = fs::File::create(&config.file_path_for_frame(file_name, "svg"))
                    .expect("Failed to open the SVG file.");
                win.resource_cache.texture_cache.dump_alpha8_glyphs_as_svg(&mut texture_file).unwrap();

                let file_name = format!("texture-cache-alpha8-linear-{}-{}.svg", id.namespace_id.0, id.id);
                let mut texture_file = fs::File::create(&config.file_path_for_frame(file_name, "svg"))
                    .expect("Failed to open the SVG file.");
                win.resource_cache.texture_cache.dump_alpha8_linear_as_svg(&mut texture_file).unwrap();
            }

            let data_stores_name = format!("data-stores-{}-{}", id.namespace_id.0, id.id);
            config.serialize_for_frame(&doc.data_stores, data_stores_name);

            let frame_spatial_tree_name = format!("frame-spatial-tree-{}-{}", id.namespace_id.0, id.id);
            config.serialize_for_frame::<SpatialTree, _>(&doc.spatial_tree, frame_spatial_tree_name);

            let properties_name = format!("properties-{}-{}", id.namespace_id.0, id.id);
            config.serialize_for_frame(&doc.dynamic_properties, properties_name);
        }

        config.serialize_for_frame(&self.dl_stores, "dl-stores");

        if config.bits.contains(CaptureBits::FRAME) {
            // TODO: there is no guarantee that we won't hit this case, but we want to
            // report it here if we do. If we don't, it will simply crash in
            // Renderer::render_impl and give us less information about the source.
            assert!(!self.requires_frame_build(), "Caches were cleared during a capture.");
        }

        debug!("\tscene builder");
        self.send_backend_message(
            SceneBuilderRequest::SaveScene(config.clone())
        );

        debug!("\tresource cache");
        let owned_doc_views: FastHashMap<DocumentId, DocumentView> = doc_ids.iter()
            .filter_map(|id| self.documents.get(id).map(|d| (*id, d.view)))
            .collect();
        let win = self.windows.get_mut(&backend_id).unwrap();
        let (resources, deferred) = win.resource_cache.save_capture(&config.root);

        info!("\tbackend");
        let backend = PlainRenderBackend {
            frame_config: win.frame_config.clone(),
            resource_sequence_id: 0,
            documents: owned_doc_views,
        };

        config.serialize_for_frame(&backend, "backend");
        config.serialize_for_frame(&resources, "plain-resources");

        if config.bits.contains(CaptureBits::FRAME) {
            let msg_update_resources = ResultMsg::UpdateResources {
                resource_updates: win.resource_cache.pending_updates(),
                memory_pressure: false,
                discard_active_documents: false,
                trim_upload_buffers: false,
            };
            win.send(msg_update_resources);
            // Save the texture/glyph/image caches.
            info!("\tresource cache");
            let caches = win.resource_cache.save_caches(&config.root);
            config.serialize_for_resource(&caches, "resource_cache");
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
        backend_id: RenderBackendId,
        mut config: CaptureConfig,
    ) {
        debug!("capture: loading {:?}", config.frame_root());
        let backend = config.deserialize_for_frame::<PlainRenderBackend, _>("backend")
            .expect("Unable to open backend.ron");

        // If this is a capture sequence, then the ID will be non-zero, and won't
        // match what is loaded, but for still captures, the ID will be zero.
        let first_load = backend.resource_sequence_id == 0;
        let needs_reload = first_load
            || self.windows.get(&backend_id).unwrap().loaded_resource_sequence_id
                != backend.resource_sequence_id;
        if needs_reload {
            // FIXME(aosmond): We clear this window's documents because when we
            // update the resource cache, we actually wipe and reload, because
            // we don't know what is the same and what has changed. If we were
            // to keep as much of the resource cache state as possible, we
            // could avoid flushing the document state (which has its own
            // dependecies on the cache).
            //
            // FIXME(aosmond): If we try to load the next capture in the
            // sequence too quickly, we may lose resources we depend on in the
            // current frame. This can cause panics. Ideally we would not
            // advance to the next frame until the FrameRendered event for all
            // of the pipelines.
            let doomed: Vec<DocumentId> = self.document_to_window.iter()
                .filter_map(|(d, w)| if *w == backend_id { Some(*d) } else { None })
                .collect();
            for d in doomed {
                self.documents.remove(&d);
                self.document_to_window.remove(&d);
            }
        }

        let win = self.windows.get_mut(&backend_id).unwrap();
        if needs_reload {
            config.resource_id = backend.resource_sequence_id;
            win.loaded_resource_sequence_id = backend.resource_sequence_id;

            let plain_resources = config.deserialize_for_resource::<PlainResources, _>("plain-resources")
                .expect("Unable to open plain-resources.ron");
            let caches_maybe = config.deserialize_for_resource::<PlainCacheOwn, _>("resource_cache");

            // Note: it would be great to have `RenderBackend` to be split
            // rather explicitly on what's used before and after scene building
            // so that, for example, we never miss anything in the code below:

            let plain_externals = win.resource_cache.load_capture(
                plain_resources,
                caches_maybe,
                &config,
            );

            let msg_load = ResultMsg::DebugOutput(
                DebugOutput::LoadCapture(config.clone(), plain_externals)
            );
            win.send(msg_load);
        }

        // The compositor kind describes the replaying renderer, not the captured one.
        let compositor_kind = win.frame_config.compositor_kind;
        win.frame_config = backend.frame_config;
        win.frame_config.compositor_kind = compositor_kind;

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
            let mut data_stores = config.deserialize_for_frame::<DataStores, _>(&data_stores_name)
                .expect(&format!("Unable to open {}.ron", data_stores_name));

            // This is a band-aid to work around the fact that there isn't a
            // proper synchronization between the serialization of the frame
            // and the scene, which can cause the data store snapshot to lag
            // the interners by one or even several scene builds, leaving
            // slots for last-frame interned items empty.
            data_stores.reconcile_from_interners(&interners);

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
                        scene_generation: 0,
                        debug_override: SceneDebugOverride::empty(),
                    };
                    entry.insert(doc);
                }
            };

            self.document_to_window.insert(id, backend_id);

            let frame_name = format!("frame-{}-{}", id.namespace_id.0, id.id);
            // A frame built for a different compositor kind can't be rendered here,
            // so rebuild it from the scene instead.
            let frame = config.deserialize_for_frame::<Frame, _>(frame_name)
                .filter(|frame| frame.composite_state.compositor_kind == compositor_kind);
            let build_frame = match frame {
                Some(frame) => {
                    info!("\tloaded a built frame with {} passes", frame.passes.len());

                    win.frame_publish_id.advance();
                    let msg_publish = ResultMsg::PublishDocument(
                        win.frame_publish_id,
                        id,
                        RenderedDocument {
                            frame,
                            profile: TransactionProfile::new(),
                            render_reasons: RenderReasons::empty(),
                            frame_stats: None,
                        },
                        win.resource_cache.pending_updates(),
                    );
                    win.send(msg_publish);

                    let params = api::FrameReadyParams {
                        present: true,
                        render: true,
                        scrolled: false,
                        tracked: false,
                    };
                    win.notifier.new_frame_ready(id, win.frame_publish_id, &params);

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
                config: win.frame_config.clone(),
                fonts: win.resource_cache.get_fonts(),
                build_frame,
                interners,
                spatial_tree: scene_spatial_tree,
            });
        }

        let dl_builders = config.deserialize_for_scene::<DlBuilderMap, _>("dl-builders")
            .expect("Unable to open dl-builders.ron");
        let scene_stores = config.deserialize_for_scene::<SceneDlStores, _>("scene-dl-stores")
            .expect("Unable to open scene-dl-stores.ron");
        self.dl_stores = config.deserialize_for_frame::<DlStores, _>("dl-stores")
            .expect("Unable to open dl-stores.ron");
        // The same snapshot skew as `data_stores` above, handled as far as it
        // can be: the namespaces follow the map, the entries cannot.
        self.dl_stores.reconcile(&dl_builders);
        self.send_backend_message(
            SceneBuilderRequest::LoadDlStores { dl_builders, scene_stores }
        );

        if !scenes_to_build.is_empty() {
            self.send_backend_message(
                SceneBuilderRequest::LoadScenes(scenes_to_build)
            );
        }
    }
}
