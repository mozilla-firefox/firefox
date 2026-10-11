/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{ColorF, DebugFlags, ExternalScrollId, FontRenderMode, ImageKey, MinimapData};
use api::units::*;
use plane_split::BspSplitter;
use crate::batch::{BatchBuilder, AlphaBatchBuilder, AlphaBatchContainer};
use crate::clip::{ClipNodeId, ClipStore, ClipTree};
use crate::command_buffer::{PrimitiveCommand, CommandBufferList, CommandBufferIndex};
use crate::{debug_colors, ChunkPool};
use crate::spatial_node::SpatialNodeType;
use crate::spatial_tree::{SpatialTree, SpatialNodeIndex};
use crate::composite::{CompositorKind, CompositeState, CompositeStatePreallocator};
use crate::debug_item::DebugItem;
use crate::gpu_types::{PrimitiveHeaders, ZBufferIdGenerator};
use crate::gpu_types::QuadSegment;
use crate::internal_types::{FastHashMap, PlaneSplitter, FrameStamp};
use crate::invalidation::DirtyRegion;
use crate::tile_cache::{max_surface_size_for_screen, SliceId, TileCacheInstance};
use crate::picture::PictureInstance;
use crate::picture::ResolvedSurfaceTexture;
use crate::picture::{RasterConfig, PictureScratch};
use crate::picture_composite_mode::PictureCompositeMode;
use crate::prepare::prepare_picture;
use crate::prim_store::{PictureIndex, PrimitiveScratchBuffer};
use crate::prim_store::{DeferredResolve, PrimitiveInstance};
use crate::prim_store::storage;
use crate::profiler::{self, TransactionProfile};
use crate::render_backend::{DataStores, ScratchBuffer};
use crate::renderer::{GpuBufferBuilder, GpuBufferBuilderF, GpuBufferBuilderI, GpuBufferF, GpuBufferI};
use crate::render_target::{PictureCacheTarget, PictureCacheTargetKind};
use crate::render_target::{RenderTargetContext, RenderTargetKind, RenderTarget};
use crate::render_task_graph::{Pass, RenderTaskGraph, RenderTaskId, SubPassSurface};
use crate::render_task_graph::{RenderPass, RenderTaskGraphBuilder};
use crate::render_task::{RenderTaskKind, StaticRenderTaskSurface};
use crate::resource_cache::ResourceCache;
use crate::scene::{BuiltScene, SceneProperties};
use crate::scene_debug::SceneDebugOverride;
use crate::space::SpaceMapper;
use crate::surface::{SubpixelMode, SurfaceBuilder, SurfaceIndex, SurfaceInfo};
use crate::transform::{TransformPalette, TransformData};
use std::sync::Arc;
use std::{f32, mem};
use crate::util::{MaxRect, VecHelper, Preallocator};
use crate::visibility::{new_clip_root_stack, update_prim_visibility, FrameVisibilityState, FrameVisibilityContext};
use crate::internal_types::{FrameVec, FrameMemory};

#[derive(Clone, Copy, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct FrameBuilderConfig {
    pub default_font_render_mode: FontRenderMode,
    pub dual_source_blending_is_supported: bool,
    /// True if we're running tests (i.e. via wrench).
    pub testing: bool,
    pub gpu_supports_fast_clears: bool,
    pub gpu_supports_advanced_blend: bool,
    pub advanced_blend_is_coherent: bool,
    pub gpu_supports_render_target_partial_update: bool,
    /// Whether ImageBufferKind::TextureExternal images must first be copied
    /// to a regular texture before rendering.
    pub external_images_require_copy: bool,
    pub batch_lookback_count: usize,
    pub background_color: Option<ColorF>,
    pub compositor_kind: CompositorKind,
    pub tile_size_override: Option<DeviceIntSize>,
    pub max_surface_override: Option<usize>,
    pub max_depth_ids: i32,
    pub max_target_size: i32,
    pub force_invalidation: bool,
    pub is_software: bool,
    pub low_quality_pinch_zoom: bool,
    pub max_shared_surface_size: i32,
    pub enable_dithering: bool,
    pub enable_yuv_overlay_stability: bool,
}

pub struct FrameScratchBuffer {
    dirty_region_stack: Vec<DirtyRegion>,
    surface_stack: Vec<(PictureIndex, SurfaceIndex)>,
    clip_root_stack: Vec<ClipNodeId>,
}

impl Default for FrameScratchBuffer {
    fn default() -> Self {
        FrameScratchBuffer {
            dirty_region_stack: Vec::new(),
            surface_stack: Vec::new(),
            clip_root_stack: Vec::new(),
        }
    }
}

impl FrameScratchBuffer {
    pub fn begin_frame(&mut self) {
        self.dirty_region_stack.clear();
        self.surface_stack.clear();
        self.clip_root_stack.clear();
    }
}

/// Produces the frames that are sent to the renderer.
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct FrameBuilder {
    #[cfg_attr(feature = "capture", serde(skip))]
    prim_headers_prealloc: Preallocator,
    #[cfg_attr(feature = "capture", serde(skip))]
    composite_state_prealloc: CompositeStatePreallocator,
    #[cfg_attr(feature = "capture", serde(skip))]
    plane_splitters: Vec<PlaneSplitter>,
}

pub struct FrameBuildingContext<'a> {
    pub scene_properties: &'a SceneProperties,
    pub global_screen_device_rect: DeviceRect,
    pub spatial_tree: &'a SpatialTree,
    pub max_local_clip: LayoutRect,
    pub debug_flags: DebugFlags,
    pub debug_override: &'a SceneDebugOverride,
    pub fb_config: &'a FrameBuilderConfig,
    pub root_spatial_node_index: SpatialNodeIndex,
}

impl<'a> FrameBuildingContext<'a> {
    /// The maximum size per axis, in device pixels, of a surface allocated
    /// during this frame. Surfaces larger than this are scaled down to fit.
    pub fn max_surface_size(&self) -> usize {
        // Tests pin the limit so they can exercise the scale-down path at a
        // size that fits in a reftest window.
        if let Some(size) = self.fb_config.max_surface_override {
            return size;
        }

        max_surface_size_for_screen(
            self.global_screen_device_rect.size().round().to_i32(),
            self.fb_config.max_target_size,
        )
    }
}

pub struct FrameBuildingState<'a> {
    pub rg_builder: &'a mut RenderTaskGraphBuilder,
    pub clip_store: &'a mut ClipStore,
    pub resource_cache: &'a mut ResourceCache,
    pub transforms: &'a mut TransformPalette,
    pub surfaces: &'a mut Vec<SurfaceInfo>,
    pub dirty_region_stack: Vec<DirtyRegion>,
    pub composite_state: &'a mut CompositeState,
    pub num_visible_primitives: u32,
    /// Primitives visited by the prepare traversal, whether or not they
    /// produced a draw. Accumulated here and reported once per frame, in the
    /// same way as `num_visible_primitives`.
    pub num_visited_primitives: u32,
    /// Total (primitive, command buffer) pairs emitted.
    pub num_cmd_targets: u32,
    /// Pictures that obtained a context this frame.
    pub num_pictures: u32,
    pub plane_splitters: &'a mut [PlaneSplitter],
    pub surface_builder: SurfaceBuilder,
    pub cmd_buffers: &'a mut CommandBufferList,
    pub clip_tree: &'a ClipTree,
    pub frame_gpu_data: &'a mut GpuBufferBuilder,
    /// When using a render task to produce pixels that are associated with
    /// an image key (for example snapshotted pictures), inserting the image
    /// key / task id association in this hashmap allows the image item to
    /// register a dependency to the render task. This ensures that the
    /// render task is produced before the image that renders it if they
    /// are happening in the same frame.
    /// This mechanism relies on the item producing the render task to be
    /// traversed before the image that displays it (in other words, the
    /// picture must appear before the image in the display list).
    pub image_dependencies: FastHashMap<ImageKey, RenderTaskId>,
    /// Per-picture scratch-index slot used by the prepare pass. `None`
    /// means "not yet visited this frame"; `Some(handle)` means the
    /// picture was visited and `handle` indexes into
    /// `PrimitiveFrameScratch.pictures` (or is `INVALID` if take_context
    /// failed). Replaces the prepare pass's earlier `visited_pictures`
    /// bool slice — visibility keeps its own bool slice on
    /// `FrameVisibilityState`.
    pub picture_scratch_handles: &'a mut [Option<storage::Index<PictureScratch>>],
}

impl<'a> FrameBuildingState<'a> {
    /// Retrieve the current dirty region during primitive traversal.
    pub fn current_dirty_region(&self) -> &DirtyRegion {
        self.dirty_region_stack.last().unwrap()
    }

    /// Push a new dirty region for child primitives to cull / clip against.
    pub fn push_dirty_region(&mut self, region: DirtyRegion) {
        self.dirty_region_stack.push(region);
    }

    /// Pop the top dirty region from the stack.
    pub fn pop_dirty_region(&mut self) {
        self.dirty_region_stack.pop().unwrap();
    }

    /// Push a primitive command to a set of command buffers
    pub fn push_prim(
        &mut self,
        cmd: &PrimitiveCommand,
        spatial_node_index: SpatialNodeIndex,
        targets: &[CommandBufferIndex],
    ) {
        for cmd_buffer_index in targets {
            let cmd_buffer = self.cmd_buffers.get_mut(*cmd_buffer_index);
            cmd_buffer.add_prim(cmd, spatial_node_index);
        }
    }

    /// Push a command to a set of command buffers
    pub fn push_cmd(
        &mut self,
        cmd: &PrimitiveCommand,
        targets: &[CommandBufferIndex],
    ) {
        for cmd_buffer_index in targets {
            let cmd_buffer = self.cmd_buffers.get_mut(*cmd_buffer_index);
            cmd_buffer.add_cmd(cmd);
        }
    }

    /// Set the active list of segments in a set of command buffers
    pub fn set_segments(
        &mut self,
        segments: &[QuadSegment],
        targets: &[CommandBufferIndex],
    ) {
        for cmd_buffer_index in targets {
            let cmd_buffer = self.cmd_buffers.get_mut(*cmd_buffer_index);
            cmd_buffer.set_segments(segments);
        }
    }
}

/// Immutable context of a picture when processing children.
#[derive(Debug)]
pub struct PictureContext {
    pub pic_index: PictureIndex,
    pub surface_spatial_node_index: SpatialNodeIndex,
    pub raster_spatial_node_index: SpatialNodeIndex,
    /// The surface that this picture will render on.
    pub surface_index: SurfaceIndex,
    pub dirty_region_count: usize,
    pub subpixel_mode: SubpixelMode,
}

/// Mutable state of a picture that gets modified when
/// the children are processed.
pub struct PictureState {
    /// Maps this picture's space to the screen framebuffer, for the debug
    /// overlays that draw into it.
    pub map_pic_to_device: SpaceMapper<PicturePixel, DevicePixel>,
}

impl FrameBuilder {
    pub fn new() -> Self {
        FrameBuilder {
            prim_headers_prealloc: Preallocator::new(0),
            composite_state_prealloc: CompositeStatePreallocator::default(),
            plane_splitters: Vec::new(),
        }
    }

    /// Compute the contribution (bounding rectangles, and resources) of layers and their
    /// primitives in screen space.
    fn build_layer_screen_rects_and_cull_layers(
        &mut self,
        scene: &mut BuiltScene,
        present: bool,
        global_screen_device_rect: DeviceRect,
        resource_cache: &mut ResourceCache,
        rg_builder: &mut RenderTaskGraphBuilder,
        scene_properties: &SceneProperties,
        transform_palette: &mut TransformPalette,
        data_stores: &DataStores,
        scratch: &mut ScratchBuffer,
        debug_flags: DebugFlags,
        debug_override: &SceneDebugOverride,
        composite_state: &mut CompositeState,
        tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
        spatial_tree: &SpatialTree,
        cmd_buffers: &mut CommandBufferList,
        frame_gpu_data: &mut GpuBufferBuilder,
        frame_memory: &FrameMemory,
        profile: &mut TransactionProfile,
    ) {
        tracy_rs::profile_scope!("build_layer_screen_rects_and_cull_layers");

        let render_picture_cache_slices = present;

        let root_spatial_node_index = spatial_tree.root_reference_frame_index();

        const MAX_CLIP_COORD: f32 = 1.0e9;

        // Reset all plane splitters. These are retained from frame to frame to reduce
        // per-frame allocations
        self.plane_splitters.resize_with(scene.num_plane_splitters, BspSplitter::new);
        for splitter in &mut self.plane_splitters {
            splitter.reset();
        }

        let frame_context = FrameBuildingContext {
            scene_properties,
            global_screen_device_rect,
            spatial_tree,
            max_local_clip: LayoutRect {
                min: LayoutPoint::new(-MAX_CLIP_COORD, -MAX_CLIP_COORD),
                max: LayoutPoint::new(MAX_CLIP_COORD, MAX_CLIP_COORD),
            },
            debug_flags,
            debug_override,
            fb_config: &scene.config,
            root_spatial_node_index,
        };

        scene.picture_graph.build_update_passes(
            &mut scene.prim_store.pictures,
            &frame_context,
        );

        scene.picture_graph.assign_surfaces(
            &mut scene.prim_store.pictures,
            &mut scene.surfaces,
            tile_caches,
            &frame_context,
        );

        // Add a "fake" surface that we will use as parent for
        // snapshotted pictures.
        let root_spatial_node = frame_context.spatial_tree.root_reference_frame_index();
        let snapshot_surface = SurfaceIndex(scene.surfaces.len());
        scene.surfaces.push(SurfaceInfo::new(
            root_spatial_node,
            root_spatial_node,
            DeviceRect::max_rect(),
            &frame_context.spatial_tree,
            euclid::Scale::new(1.0),
            (1.0, 1.0),
            (1.0, 1.0),
            (1.0, 1.0),
            false,
            false,
        ));

        // Empty the per-frame draw storage. The visibility pass pushes into it
        // as it finds drawn primitives; the scene's picture count only sizes
        // the per-picture draw lists.
        scratch.primitive.frame.reset_draws(scene.prim_store.pictures.len());

        // Cluster, prim, and clip-leaf rects are snapped to the device pixel
        // grid as they are produced by the in-frame picture-graph passes:
        // `propagate_bounding_rects` snaps each cluster bounding rect, and the
        // visibility pass snaps each prim's `snapped_pattern_rect` and clip-leaf
        // rect. Both snap against the consuming surface's raster node, so only
        // pictures reachable this frame are touched.
        scene.picture_graph.propagate_bounding_rects(
            &mut scene.prim_store.pictures,
            &mut scene.surfaces,
            &frame_context,
        );

        // In order to handle picture snapshots consistently we need
        // the visibility and prepare passes to visit them first before
        // traversing the scene. This ensures that out-of-view snapshots
        // are rendered and that snapshots are consistently produced
        // relative to the root spatial node.
        // However it means that the visibility and prepare passes may
        // visit some pictures multiple times, so we keep track of visited
        // pictures during each traversal to avoid that.
        let n_pics = scene.prim_store.pictures.len();
        let mut visited_pictures = frame_memory.new_vec_with_capacity(n_pics);
        for _ in 0..n_pics {
            visited_pictures.push(false);
        }

        {
            profile_marker!("UpdateVisibility");
            profile.start_time(profiler::FRAME_VISIBILITY_TIME);

            let visibility_context = FrameVisibilityContext {
                spatial_tree,
                global_screen_device_rect,
                debug_flags,
                debug_override,
                scene_properties,
                config: scene.config,
                root_spatial_node_index,
            };

            for pic_index in scene.snapshot_pictures.iter() {
                let mut visibility_state = FrameVisibilityState {
                    clip_store: &mut scene.clip_store,
                    resource_cache,
                    frame_gpu_data,
                    data_stores,
                    clip_tree: &scene.clip_tree,
                    composite_state,
                    rg_builder,
                    prim_instances: &mut scene.prim_instances,
                    surfaces: &mut scene.surfaces,
                    surface_stack: scratch.frame.surface_stack.take(),
                    clip_root_stack: new_clip_root_stack(scratch.frame.clip_root_stack.take()),
                    profile,
                    scratch,
                    visited_pictures: &mut visited_pictures,
                };

                // For now, snapshots are updated every frame. For the
                // pictures displaying the snapshot via images pick up
                // the changes, we have to make sure that the image's
                // generation counter is incremented early in the frame,
                // before the main visibility pass visits the image items.
                let pic = &scene.prim_store.pictures[pic_index.0 as usize];
                let snapshot = pic.snapshot
                    .unwrap();
                let key = snapshot.key.as_image();
                visibility_state.resource_cache
                    .increment_image_generation(key);

                if let Some(node) = pic.clip_root {
                    visibility_state.push_clip_root(node);
                }

                update_prim_visibility(
                    *pic_index,
                    None,
                    &scene.prim_store,
                    true,
                    &visibility_context,
                    &mut visibility_state,
                    &mut None,
                );
                if scene.prim_store.pictures[pic_index.0 as usize].clip_root.is_some() {
                    visibility_state.pop_clip_root();
                }
            }

            for pic_index in scene.tile_cache_pictures.iter().rev() {
                if !render_picture_cache_slices {
                    break;
                }
                let pic = &mut scene.prim_store.pictures[pic_index.0 as usize];

                match pic.raster_config {
                    Some(RasterConfig { surface_index, composite_mode: PictureCompositeMode::TileCache { slice_id }, .. }) => {
                        let tile_cache = tile_caches
                            .get_mut(&slice_id)
                            .expect("bug: non-existent tile cache");

                        let mut visibility_state = FrameVisibilityState {
                            clip_store: &mut scene.clip_store,
                            resource_cache,
                            frame_gpu_data,
                            data_stores,
                            clip_tree: &scene.clip_tree,
                            composite_state,
                            rg_builder,
                            prim_instances: &mut scene.prim_instances,
                            surfaces: &mut scene.surfaces,
                            surface_stack: scratch.frame.surface_stack.take(),
                            clip_root_stack: new_clip_root_stack(scratch.frame.clip_root_stack.take()),
                            profile,
                            scratch,
                            visited_pictures: &mut visited_pictures,
                        };

                        // If we have a tile cache for this picture, see if any of the
                        // relative transforms have changed, which means we need to
                        // re-map the dependencies of any child primitives.
                        tile_cache.pre_update(
                            surface_index,
                            &visibility_context,
                            &mut visibility_state,
                        );

                        // Push a new surface, supplying the list of clips that should be
                        // ignored, since they are handled by clipping when drawing this surface.
                        visibility_state.push_surface(
                            *pic_index,
                            surface_index,
                        );
                        visibility_state.push_clip_root(tile_cache.shared_clip_node_id);

                        update_prim_visibility(
                            *pic_index,
                            None,
                            &scene.prim_store,
                            true,
                            &visibility_context,
                            &mut visibility_state,
                            &mut Some(tile_cache),
                        );

                        // Build the dirty region(s) for this tile cache.
                        tile_cache.post_update(
                            &visibility_context,
                            &mut visibility_state.prim_instances,
                            &mut visibility_state.composite_state,
                            &mut visibility_state.resource_cache,
                            &mut visibility_state.scratch.primitive,
                        );

                        visibility_state.pop_clip_root();
                        visibility_state.pop_surface();
                        visibility_state.scratch.frame.surface_stack = visibility_state.surface_stack.take();
                        visibility_state.scratch.frame.clip_root_stack = visibility_state.clip_root_stack.take();
                    }
                    _ => {
                        panic!("bug: not a tile cache");
                    }
                }
            }

            scratch.primitive.frame.assert_draws_resolved();

            profile.end_time(profiler::FRAME_VISIBILITY_TIME);
        }

        self.skip_occluded_pictures_with_clips(
            &scene.tile_cache_pictures,
            &mut scene.prim_store.pictures,
            tile_caches,
            &frame_context,
            composite_state);

        profile.start_time(profiler::FRAME_PREPARE_TIME);

        let mut picture_scratch_handles = frame_memory.new_vec_with_capacity(n_pics);
        for _ in 0..n_pics {
            picture_scratch_handles.push(None);
        }
        let mut frame_state = FrameBuildingState {
            rg_builder,
            clip_store: &mut scene.clip_store,
            resource_cache,
            transforms: transform_palette,
            surfaces: &mut scene.surfaces,
            dirty_region_stack: scratch.frame.dirty_region_stack.take(),
            composite_state,
            num_visible_primitives: 0,
            num_visited_primitives: 0,
            num_cmd_targets: 0,
            num_pictures: 0,
            plane_splitters: &mut self.plane_splitters,
            surface_builder: SurfaceBuilder::new(),
            cmd_buffers,
            clip_tree: &scene.clip_tree,
            frame_gpu_data,
            image_dependencies: FastHashMap::default(),
            picture_scratch_handles: &mut picture_scratch_handles,
        };


        if !scene.snapshot_pictures.is_empty() {
            // Push a default dirty region which does not cull any
            // primitive.
            let mut default_dirty_region = DirtyRegion::new(
                root_spatial_node_index,
                root_spatial_node_index,
            );
            default_dirty_region.add_dirty_region(
                PictureRect::max_rect(),
                frame_context.spatial_tree,
            );
            frame_state.push_dirty_region(default_dirty_region);

            frame_state.surface_builder.push_surface(
                snapshot_surface,
                false,
                DeviceRect::max_rect(),
                None,
                frame_state.surfaces,
                frame_state.rg_builder,
            );
        }

        for pic_index in &scene.snapshot_pictures {

            prepare_picture(
                *pic_index,
                &mut scene.prim_store,
                Some(snapshot_surface),
                SubpixelMode::Allow,
                &frame_context,
                &mut frame_state,
                data_stores,
                &mut scratch.primitive,
                tile_caches,
                &mut scene.prim_instances
            );
        }

        if !scene.snapshot_pictures.is_empty() {
            frame_state.surface_builder.pop_empty_surface();
            frame_state.pop_dirty_region();
        }

        // Push a default dirty region which culls primitives
        // against the screen world rect, in absence of any
        // other dirty regions.
        let mut default_dirty_region = DirtyRegion::new(
            root_spatial_node_index,
            root_spatial_node_index,
        );
        default_dirty_region.add_dirty_region(
            frame_context.global_screen_device_rect.cast_unit(),
            frame_context.spatial_tree,
        );
        frame_state.push_dirty_region(default_dirty_region);

        for pic_index in &scene.tile_cache_pictures {
            if !render_picture_cache_slices {
                break;
            }

            prepare_picture(
                *pic_index,
                &mut scene.prim_store,
                None,
                SubpixelMode::Allow,
                &frame_context,
                &mut frame_state,
                data_stores,
                &mut scratch.primitive,
                tile_caches,
                &mut scene.prim_instances
            );
        }

        frame_state.pop_dirty_region();
        frame_state.surface_builder.finalize();
        profile.end_time(profiler::FRAME_PREPARE_TIME);
        profile.set(profiler::VISIBLE_PRIMITIVES, frame_state.num_visible_primitives);
        profile.set(profiler::PREPARE_VISITED_PRIMS, frame_state.num_visited_primitives);
        profile.set(profiler::PREPARE_CMD_TARGETS, frame_state.num_cmd_targets);
        profile.set(profiler::PREPARE_PICTURES, frame_state.num_pictures);

        scratch.frame.dirty_region_stack = frame_state.dirty_region_stack.take();

        {
            profile_marker!("BlockOnResources");

            resource_cache.block_until_all_resources_added(
                frame_gpu_data,
                profile,
            );
        }
    }

    pub fn build(
        &mut self,
        scene: &mut BuiltScene,
        present: bool,
        resource_cache: &mut ResourceCache,
        rg_builder: &mut RenderTaskGraphBuilder,
        stamp: FrameStamp,
        device_origin: DeviceIntPoint,
        scene_properties: &SceneProperties,
        data_stores: &DataStores,
        scratch: &mut ScratchBuffer,
        debug_flags: DebugFlags,
        debug_override: &SceneDebugOverride,
        tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
        spatial_tree: &mut SpatialTree,
        dirty_rects_are_valid: bool,
        profile: &mut TransactionProfile,
        minimap_data: FastHashMap<ExternalScrollId, MinimapData>,
        chunk_pool: Arc<ChunkPool>,
    ) -> Frame {
        profile_marker!("BuildFrame");

        let mut frame_memory = FrameMemory::new(chunk_pool, stamp.frame_id());
        // TODO(gw): Recycle backing vec buffers for gpu buffer builder between frames
        let mut gpu_buffer_builder = GpuBufferBuilder {
            f32: GpuBufferBuilderF::new(&frame_memory, 8 * 1024, stamp.frame_id()),
            i32: GpuBufferBuilderI::new(&frame_memory, 2 * 1024, stamp.frame_id()),
        };

        profile.set(profiler::PRIMITIVES, scene.prim_instances.len());
        profile.set(profiler::PICTURE_CACHE_SLICES, scene.tile_cache_config.picture_cache_slice_count);
        scratch.begin_frame();
        resource_cache.begin_frame(stamp, profile);

        // TODO(gw): Follow up patches won't clear this, as they'll be assigned
        //           statically during scene building.
        scene.surfaces.clear();

        spatial_tree.update_tree(scene_properties);
        let mut transform_palette = spatial_tree.build_transform_palette(&frame_memory);
        scene.clip_store.begin_frame(&mut scratch.clip_store);

        rg_builder.begin_frame(stamp.frame_id());

        let output_size = scene.output_rect.size();
        let screen_device_rect = scene.output_rect.to_f32().round_out();

        let mut composite_state = CompositeState::new(
            scene.config.compositor_kind,
            scene.config.max_depth_ids,
            dirty_rects_are_valid,
            scene.config.low_quality_pinch_zoom,
            &frame_memory,
        );

        self.composite_state_prealloc.preallocate(&mut composite_state);

        let mut cmd_buffers = CommandBufferList::new();

        self.build_layer_screen_rects_and_cull_layers(
            scene,
            present,
            screen_device_rect,
            resource_cache,
            rg_builder,
            scene_properties,
            &mut transform_palette,
            data_stores,
            scratch,
            debug_flags,
            debug_override,
            &mut composite_state,
            tile_caches,
            spatial_tree,
            &mut cmd_buffers,
            &mut gpu_buffer_builder,
            &frame_memory,
            profile,
        );

        self.render_minimap(&mut scratch.primitive, &spatial_tree, minimap_data);

        profile.start_time(profiler::FRAME_BATCHING_TIME);

        let mut deferred_resolves = frame_memory.new_vec();

        // Finish creating the frame graph and build it.
        let render_tasks = rg_builder.end_frame(
            resource_cache,
            &mut gpu_buffer_builder,
            &mut deferred_resolves,
            scene.config.max_shared_surface_size,
            &frame_memory,
        );

        let mut passes = frame_memory.new_vec();
        let mut has_texture_cache_tasks = false;
        let mut prim_headers = PrimitiveHeaders::new(&frame_memory);
        self.prim_headers_prealloc.preallocate_framevec(&mut prim_headers.headers_int);
        self.prim_headers_prealloc.preallocate_framevec(&mut prim_headers.headers_float);

        {
            profile_marker!("Batching");

            // Used to generated a unique z-buffer value per primitive.
            let mut z_generator = ZBufferIdGenerator::new(scene.config.max_depth_ids);
            let use_dual_source_blending = scene.config.dual_source_blending_is_supported;

            for pass in render_tasks.passes.iter().rev() {
                let mut ctx = RenderTargetContext {
                    prim_store: &scene.prim_store,
                    resource_cache,
                    use_dual_source_blending,
                    break_advanced_blend_batches: !scene.config.advanced_blend_is_coherent,
                    batch_lookback_count: scene.config.batch_lookback_count,
                    spatial_tree,
                    data_stores,
                    scratch: &mut scratch.primitive,
                    screen_device_rect,
                    tile_caches,
                    root_spatial_node_index: spatial_tree.root_reference_frame_index(),
                    frame_memory: &mut frame_memory,
                };

                let pass = build_render_pass(
                    pass,
                    output_size,
                    &mut ctx,
                    &mut gpu_buffer_builder,
                    &render_tasks,
                    &mut transform_palette,
                    &mut prim_headers,
                    &mut z_generator,
                    &scene.prim_instances,
                    &cmd_buffers,
                );

                has_texture_cache_tasks |= !pass.texture_cache.is_empty();
                has_texture_cache_tasks |= !pass.picture_cache.is_empty();

                passes.push(pass);
            }

            if present {
                let mut ctx = RenderTargetContext {
                    prim_store: &scene.prim_store,
                    resource_cache,
                    use_dual_source_blending,
                    break_advanced_blend_batches: !scene.config.advanced_blend_is_coherent,
                    batch_lookback_count: scene.config.batch_lookback_count,
                    spatial_tree,
                    data_stores,
                    scratch: &mut scratch.primitive,
                    screen_device_rect,
                    tile_caches,
                    root_spatial_node_index: spatial_tree.root_reference_frame_index(),
                    frame_memory: &mut frame_memory,
                };

                self.build_composite_pass(
                    scene,
                    &mut ctx,
                    &mut gpu_buffer_builder,
                    &mut deferred_resolves,
                    &mut composite_state,
                );
            }
        }

        profile.end_time(profiler::FRAME_BATCHING_TIME);

        resource_cache.end_frame(profile);

        self.prim_headers_prealloc.record_vec(&prim_headers.headers_int);
        self.composite_state_prealloc.record(&composite_state);

        composite_state.end_frame();

        {
            let raster_clip_stats = scene.clip_store.raster_clip_stats();
            profile.set(profiler::VIS_CLIP_PROJECTIONS, raster_clip_stats.projections);
            profile.set(profiler::VIS_CLIP_PROJECTION_FAILS, raster_clip_stats.projection_fails);
            profile.set(profiler::VIS_CLIP_REJECTS, raster_clip_stats.rejects);
            profile.set(profiler::VIS_CLIP_INDETERMINATE, raster_clip_stats.indeterminate);
        }

        scene.clip_store.end_frame(&mut scratch.clip_store);
        scratch.end_frame();

        let gpu_buffer_f = gpu_buffer_builder.f32.finalize(&render_tasks);
        let gpu_buffer_i = gpu_buffer_builder.i32.finalize(&render_tasks);

        Frame {
            device_rect: DeviceIntRect::from_origin_and_size(
                device_origin,
                scene.output_rect.size(),
            ),
            present,
            passes,
            transform_palette: transform_palette.finish(),
            render_tasks,
            deferred_resolves,
            has_been_rendered: false,
            has_texture_cache_tasks,
            prim_headers,
            debug_items: mem::replace(&mut scratch.primitive.frame.debug_items, Vec::new()),
            composite_state,
            gpu_buffer_f,
            gpu_buffer_i,
            allocator_memory: frame_memory,
        }
    }

    fn render_minimap(
        &self,
        scratch: &mut PrimitiveScratchBuffer,
        spatial_tree: &SpatialTree,
        minimap_data_store: FastHashMap<ExternalScrollId, MinimapData>) {
      // TODO: Replace minimap_data_store with Option<FastHastMap>?
      if minimap_data_store.is_empty() {
        return
      }

      // In our main walk over the spatial tree (below), for nodes inside a
      // subtree rooted at a root-content node, we need some information from
      // that enclosing root-content node. To collect this information, do an
      // preliminary walk over the spatial tree now and collect the root-content
      // info in a HashMap.
      struct RootContentInfo {
        transform: LayoutToWorldTransform,
        clip: LayoutRect
      }
      let mut root_content_info = FastHashMap::<ExternalScrollId, RootContentInfo>::default();
      spatial_tree.visit_nodes(|index, node| {
        if let SpatialNodeType::ScrollFrame(ref scroll_frame_info) = node.node_type {
          if let Some(minimap_data) = minimap_data_store.get(&scroll_frame_info.external_id) {
            if minimap_data.is_root_content {
              let transform = spatial_tree.get_world_viewport_transform(index).into_transform();
              root_content_info.insert(scroll_frame_info.external_id, RootContentInfo{
                transform,
                clip: scroll_frame_info.viewport_rect
              });
            }
          }
        }
      });

      // This is the main walk over the spatial tree. For every scroll frame node which
      // has minimap data, compute the rects we want to render for that minimap in world
      // coordinates and add them to `scratch.frame.debug_items`.
      spatial_tree.visit_nodes(|index, node| {
        if let SpatialNodeType::ScrollFrame(ref scroll_frame_info) = node.node_type {
          if let Some(minimap_data) = minimap_data_store.get(&scroll_frame_info.external_id) {
            const HORIZONTAL_PADDING: f32 = 5.0;
            const VERTICAL_PADDING: f32 = 10.0;
            const PAGE_BORDER_COLOR: ColorF = debug_colors::BLACK;
            const BACKGROUND_COLOR: ColorF = ColorF { r: 0.3, g: 0.3, b: 0.3, a: 0.3};
            const DISPLAYPORT_BACKGROUND_COLOR: ColorF = ColorF { r: 1.0, g: 1.0, b: 1.0, a: 0.4};
            const LAYOUT_PORT_COLOR: ColorF = debug_colors::RED;
            const VISUAL_PORT_COLOR: ColorF = debug_colors::BLUE;
            const DISPLAYPORT_COLOR: ColorF = debug_colors::LIME;

            let viewport = scroll_frame_info.viewport_rect;

            // Scale the minimap to make it 100px wide (if there's space), and the full height
            // of the scroll frame's viewport, minus some padding. Position it at the left edge
            // of the scroll frame's viewport.
            let scale_factor_x = 100f32.min(viewport.width() - (2.0 * HORIZONTAL_PADDING))
                                   / minimap_data.scrollable_rect.width();
            let scale_factor_y = (viewport.height() - (2.0 * VERTICAL_PADDING))
                                / minimap_data.scrollable_rect.height();
            if scale_factor_x <= 0.0 || scale_factor_y <= 0.0 {
              return;
            }
            let transform = LayoutTransform::scale(scale_factor_x, scale_factor_y, 1.0)
                .then_translate(LayoutVector3D::new(HORIZONTAL_PADDING, VERTICAL_PADDING, 0.0))
                .then_translate(LayoutVector3D::new(viewport.min.x, viewport.min.y, 0.0));

            // Transforms for transforming rects in this scroll frame's local coordintes, to world coordinates.
            // For scroll frames inside a root-content subtree, we apply this transform in two parts
            // (local to root-content, and root-content to world), so that we can make additional
            // adjustments in root-content space. For scroll frames outside of a root-content subtree,
            // the entire world transform will be in `local_to_root_content`.
            let world_transform = spatial_tree
                .get_world_viewport_transform(index)
                .into_transform();
            let mut local_to_root_content =
                world_transform.with_destination::<LayoutPixel>();
            let mut root_content_to_world = LayoutToWorldTransform::default();
            let mut root_content_clip = None;
            if minimap_data.root_content_scroll_id != 0 {
              if let Some(RootContentInfo{transform: root_content_transform, clip}) = root_content_info.get(&ExternalScrollId(minimap_data.root_content_scroll_id, minimap_data.root_content_pipeline_id)) {
                // Exclude the root-content node's zoom transform from `local_to_root_content`.
                // This ensures that the minimap remains unaffected by pinch-zooming
                // (in essence, remaining attached to the *visual* viewport, rather than to
                // the *layout* viewport which is what happens by default).
                let zoom_transform = minimap_data.zoom_transform;
                local_to_root_content = world_transform
                  .then(&root_content_transform.inverse().unwrap())
                  .then(&zoom_transform.inverse().unwrap());
                root_content_to_world = root_content_transform.clone();
                root_content_clip = Some(clip);
              }
            }

            let mut add_rect = |rect, border, fill| -> Option<()> {
              const STROKE_WIDTH: f32 = 2.0;
              // Place rect in scroll frame's local coordinate space
              let transformed_rect = transform.outer_transformed_box2d(&rect)?;

              // Transform to world coordinates, using root-content coords as an intermediate step.
              let mut root_content_rect = local_to_root_content.outer_transformed_box2d(&transformed_rect)?;
              // In root-content coords, apply the root content node's viewport clip.
              // This prevents subframe minimaps from leaking into the chrome area when the root
              // scroll frame is scrolled.
              // TODO: The minimaps of nested subframes can still leak outside of the viewports of
              // their containing subframes. Should have a more proper fix for this.
              if let Some(clip) = root_content_clip {
                root_content_rect = root_content_rect.intersection(clip)?;
              }
              let world_rect = root_content_to_world.outer_transformed_box2d(&root_content_rect)?;

              scratch.push_debug_rect_with_stroke_width(world_rect, border, STROKE_WIDTH);

              // Add world coordinate rects to scratch.frame.debug_items
              if let Some(fill_color) = fill {
                let interior_world_rect = WorldRect::new(
                    world_rect.min + WorldVector2D::new(STROKE_WIDTH, STROKE_WIDTH),
                    world_rect.max - WorldVector2D::new(STROKE_WIDTH, STROKE_WIDTH)
                );
                scratch.push_debug_rect(interior_world_rect * DevicePixelScale::new(1.0), 1, border, fill_color);
              }

              Some(())
            };

            add_rect(minimap_data.scrollable_rect, PAGE_BORDER_COLOR, Some(BACKGROUND_COLOR));
            add_rect(minimap_data.displayport, DISPLAYPORT_COLOR, Some(DISPLAYPORT_BACKGROUND_COLOR));
            // Only render a distinct layout viewport for the root content.
            // For other scroll frames, the visual and layout viewports coincide.
            if minimap_data.is_root_content {
              add_rect(minimap_data.layout_viewport, LAYOUT_PORT_COLOR, None);
            }
            add_rect(minimap_data.visual_viewport, VISUAL_PORT_COLOR, None);
          }
        }
      });
    }

    /// Skip pictures that are fully covered by another opaque picture with the same rounded rect clip.
    ///
    /// Since the clip adds some transparency to the occluder, the opaque rectangles occlusion detection
    /// fails to discard stacks of opaque rounded corners.
    /// This is done specifically to avoid conflation artifacts with the rounded rectangle around web content
    /// in Firefox.
    fn skip_occluded_pictures_with_clips(
        &self,
        tile_cache_pictures: &Vec<PictureIndex>,
        pictures: &mut [PictureInstance],
        tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
        frame_context: &FrameBuildingContext,
        composite_state: &mut CompositeState,
    ) {
        let mut current_opaque_clip = None;

        for pic_index in tile_cache_pictures.iter().rev() {
            let pic = &mut pictures[pic_index.0 as usize];

            match pic.raster_config {
                Some(RasterConfig { composite_mode: PictureCompositeMode::TileCache { slice_id }, .. }) => {
                    let tile_cache = tile_caches
                        .get_mut(&slice_id)
                        .expect("bug: non-existent tile cache");
                    // Only check for compositor clips (rounded clips). The main
                    // rectangle occlusion for compositor tiles handles the rest fine.
                    if tile_cache.compositor_clip.is_none() {
                        continue;
                    }

                    // Get the rounded clip for this picture cache.
                    let (rounded_clip_rect, rounded_clip_radii) = composite_state.compositor_clip_params(
                        tile_cache.compositor_clip,
                        DeviceRect::max_rect(),
                    );

                    // Simple compare against the previous compositor clip we found
                    if let Some((current_clip_rect, current_clip_radius)) = current_opaque_clip {
                        if current_clip_rect == rounded_clip_rect &&
                        current_clip_radius == rounded_clip_radii {
                            for sub_slice in tile_cache.sub_slices.iter_mut() {
                                for tile in sub_slice.tiles.values_mut() {
                                    tile.is_visible = false;
                                }
                            }
                        }
                    }

                    let backdrop_rect = tile_cache.backdrop.backdrop_rect
                        .intersection(&tile_cache.local_rect)
                        .and_then(|r| {
                            r.intersection(&tile_cache.local_clip_rect)
                    });

                    if let Some(backdrop_rect) = backdrop_rect {
                        let map_local_to_world = SpaceMapper::new_with_target(
                            frame_context.root_spatial_node_index,
                            tile_cache.spatial_node_index,
                            frame_context.global_screen_device_rect,
                            frame_context.spatial_tree,
                        );
                        let world_backdrop_rect = map_local_to_world
                            .map(&backdrop_rect)
                            .expect("bug: unable to map backdrop rect")
                            .round()
                            .cast_unit::<DevicePixel>();
                        let device_backdrop_rect = world_backdrop_rect;

                        if device_backdrop_rect.contains_box(&rounded_clip_rect) {
                            // Save compositor clip for checking against subsequent slices
                            current_opaque_clip = Some((rounded_clip_rect, rounded_clip_radii));
                        }
                    }
                }
                _ => {
                     panic!("bug: found a top-level prim that isn't a tile cache");
                }
            }
        }
    }

    fn build_composite_pass(
        &self,
        scene: &BuiltScene,
        ctx: &RenderTargetContext,
        gpu_buffers: &mut GpuBufferBuilder,
        deferred_resolves: &mut FrameVec<DeferredResolve>,
        composite_state: &mut CompositeState,
    ) {
        for pic_index in &scene.tile_cache_pictures {
            let pic = &ctx.prim_store.pictures[pic_index.0 as usize];

            match pic.raster_config {
                Some(RasterConfig { composite_mode: PictureCompositeMode::TileCache { slice_id }, .. }) => {
                    // Tile cache instances are added to the composite config, rather than
                    // directly added to batches. This allows them to be drawn with various
                    // present modes during render, such as partial present etc.
                    let tile_cache = &ctx.tile_caches[&slice_id];
                    let map_local_to_device = SpaceMapper::new_with_target(
                        ctx.root_spatial_node_index,
                        tile_cache.spatial_node_index,
                        ctx.screen_device_rect,
                        ctx.spatial_tree,
                    );
                    let device_clip_rect = map_local_to_device
                        .map(&tile_cache.local_clip_rect)
                        .expect("bug: unable to map clip rect")
                        .round();

                    composite_state.push_surface(
                        tile_cache,
                        device_clip_rect,
                        ctx.resource_cache,
                        &mut gpu_buffers.f32,
                        deferred_resolves,
                    );
                }
                _ => {
                    panic!("bug: found a top-level prim that isn't a tile cache");
                }
            }
        }
    }
}

/// Processes this pass to prepare it for rendering.
///
/// Among other things, this allocates output regions for each of our tasks
/// (added via `add_render_task`) in a RenderTarget and assigns it into that
/// target.
pub fn build_render_pass(
    src_pass: &Pass,
    screen_size: DeviceIntSize,
    ctx: &mut RenderTargetContext,
    gpu_buffer_builder: &mut GpuBufferBuilder,
    render_tasks: &RenderTaskGraph,
    transforms: &mut TransformPalette,
    prim_headers: &mut PrimitiveHeaders,
    z_generator: &mut ZBufferIdGenerator,
    prim_instances: &[PrimitiveInstance],
    cmd_buffers: &CommandBufferList,
) -> RenderPass {
    tracy_rs::profile_scope!("build_render_pass");

    // TODO(gw): In this initial frame graph work, we try to maintain the existing
    //           build_render_pass code as closely as possible, to make the review
    //           simpler and reduce chance of regressions. However, future work should
    //           include refactoring this to more closely match the built frame graph.
    let mut pass = RenderPass::new(src_pass, ctx.frame_memory);

    for sub_pass in &src_pass.sub_passes {
        match sub_pass.surface {
            SubPassSurface::Dynamic { target_kind, texture_id, used_rect } => {
                match target_kind {
                    RenderTargetKind::Color => {
                        let mut target = RenderTarget::new(
                            RenderTargetKind::Color,
                            false,
                            texture_id,
                            screen_size,
                            Some(used_rect),
                            &ctx.frame_memory,
                        );

                        for task_id in &sub_pass.task_ids {
                            target.add_task(
                                *task_id,
                                ctx,
                                gpu_buffer_builder,
                                render_tasks,
                                transforms,
                            );
                        }

                        pass.color.targets.push(target);
                    }
                    RenderTargetKind::Alpha => {
                        let mut target = RenderTarget::new(
                            RenderTargetKind::Alpha,
                            false,
                            texture_id,
                            screen_size,
                            Some(used_rect),
                            &ctx.frame_memory,
                        );

                        for task_id in &sub_pass.task_ids {
                            target.add_task(
                                *task_id,
                                ctx,
                                gpu_buffer_builder,
                                render_tasks,
                                transforms,
                            );
                        }

                        pass.alpha.targets.push(target);
                    }
                }
            }
            SubPassSurface::Persistent { surface: StaticRenderTaskSurface::PictureCache { ref surface, .. }, .. } => {
                assert_eq!(sub_pass.task_ids.len(), 1);
                let task_id = sub_pass.task_ids[0];
                let task = &render_tasks[task_id];
                let target_rect = task.get_target_rect();

                match task.kind {
                    RenderTaskKind::Picture(ref pic_task) => {
                        let cmd_buffer = cmd_buffers.get(pic_task.cmd_buffer_index);
                        let mut dirty_rect = pic_task.scissor_rect.expect("bug: must be set for cache tasks");
                        let mut valid_rect = pic_task.valid_rect.expect("bug: must be set for cache tasks");

                        // If we have a surface size, clip the dirty and vaild rects
                        // to that size. This ensures that native compositors will
                        // pass sanity checks (Bug 1971296).
                        if let ResolvedSurfaceTexture::Native { size, .. } = surface {
                            let surface_size_rect = <DeviceIntRect>::from_size(*size);
                            dirty_rect = dirty_rect.intersection(&surface_size_rect).unwrap_or_default();
                            valid_rect = valid_rect.intersection(&surface_size_rect).unwrap_or_default();
                        }

                        let batcher = AlphaBatchBuilder::new(
                            screen_size,
                            ctx.break_advanced_blend_batches,
                            ctx.batch_lookback_count,
                            task_id,
                            task_id.into(),
                            &ctx.frame_memory,
                        );

                        let mut batch_builder = BatchBuilder::new(batcher);

                        cmd_buffer.iter_prims(&mut |cmd, spatial_node_index, segments| {
                            batch_builder.add_prim_to_batch(
                                cmd,
                                spatial_node_index,
                                ctx,
                                render_tasks,
                                prim_headers,
                                transforms,
                                pic_task.raster_spatial_node_index,
                                pic_task.surface_spatial_node_index,
                                pic_task.device_pixel_scale,
                                z_generator,
                                prim_instances,
                                gpu_buffer_builder,
                                segments,
                            );
                        });

                        let batcher = batch_builder.finalize();

                        let mut batch_containers = ctx.frame_memory.new_vec();
                        let mut alpha_batch_container = AlphaBatchContainer::new(
                            Some(dirty_rect),
                            &ctx.frame_memory
                        );

                        batcher.build(
                            &mut batch_containers,
                            &mut alpha_batch_container,
                            target_rect,
                            None,
                        );
                        debug_assert!(batch_containers.is_empty());

                        let target = PictureCacheTarget {
                            surface: surface.clone(),
                            clear_color: pic_task.clear_color,
                            kind: PictureCacheTargetKind::Draw {
                                alpha_batch_container,
                            },
                            dirty_rect,
                            valid_rect,
                        };

                        pass.picture_cache.push(target);
                    }
                    RenderTaskKind::TileComposite(ref tile_task) => {
                        let mut dirty_rect = tile_task.scissor_rect;
                        let mut valid_rect = tile_task.valid_rect;
                        // If we have a surface size, clip the dirty and vaild rects
                        // to that size. This ensures that native compositors will
                        // pass sanity checks (Bug 1971296).
                        if let ResolvedSurfaceTexture::Native { size, .. } = surface {
                            let surface_size_rect = <DeviceIntRect>::from_size(*size);
                            dirty_rect = dirty_rect.intersection(&surface_size_rect).unwrap_or_default();
                            valid_rect = valid_rect.intersection(&surface_size_rect).unwrap_or_default();
                        }

                        let target = PictureCacheTarget {
                            surface: surface.clone(),
                            clear_color: Some(tile_task.clear_color),
                            kind: PictureCacheTargetKind::Blit {
                                task_id: tile_task.task_id.expect("bug: no source task_id set"),
                                sub_rect_offset: tile_task.sub_rect_offset,
                            },
                            dirty_rect,
                            valid_rect,
                        };

                        pass.picture_cache.push(target);
                    }
                    _ => {
                        unreachable!();
                    }
                };
            }
            SubPassSurface::Persistent { surface: StaticRenderTaskSurface::TextureCache { target_kind, texture, .. } } => {
                let texture = pass.texture_cache
                    .entry(texture)
                    .or_insert_with(||
                        RenderTarget::new(
                            target_kind,
                            true,
                            texture,
                            screen_size,
                            None,
                            &ctx.frame_memory
                        )
                    );
                for task_id in &sub_pass.task_ids {
                    texture.add_task(
                        *task_id,
                        ctx,
                        gpu_buffer_builder,
                        render_tasks,
                        transforms,
                    );
                }
            }
            SubPassSurface::Persistent { surface: StaticRenderTaskSurface::ReadOnly { .. } } => {
                panic!("Should not create a render pass for read-only task locations.");
            }
        }
    }

    pass.color.build(
        ctx,
        render_tasks,
        prim_headers,
        transforms,
        z_generator,
        prim_instances,
        cmd_buffers,
        gpu_buffer_builder,
    );
    pass.alpha.build(
        ctx,
        render_tasks,
        prim_headers,
        transforms,
        z_generator,
        prim_instances,
        cmd_buffers,
        gpu_buffer_builder,
    );

    for target in &mut pass.texture_cache.values_mut() {
        target.build(
            ctx,
            render_tasks,
            prim_headers,
            transforms,
            z_generator,
            prim_instances,
            cmd_buffers,
            gpu_buffer_builder,
        );
    }

    pass
}

/// A rendering-oriented representation of the frame built by the render backend
/// and presented to the renderer.
///
/// # Safety
///
/// The frame's allocator memory must be dropped after all of the frame's containers.
/// This is handled in the renderer and in `RenderedDocument`'s Drop implementation.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct Frame {
    /// The rectangle to show the frame in, on screen.
    pub device_rect: DeviceIntRect,
    pub present: bool,
    pub passes: FrameVec<RenderPass>,

    pub transform_palette: FrameVec<TransformData>,
    pub render_tasks: RenderTaskGraph,
    pub prim_headers: PrimitiveHeaders,

    /// List of textures that we don't know about yet
    /// from the backend thread. The render thread
    /// will use a callback to resolve these and
    /// patch the data structures.
    pub deferred_resolves: FrameVec<DeferredResolve>,

    /// True if this frame contains any render tasks
    /// that write to the texture cache.
    pub has_texture_cache_tasks: bool,

    /// True if this frame has been drawn by the
    /// renderer.
    pub has_been_rendered: bool,

    /// Debugging information to overlay for this frame.
    pub debug_items: Vec<DebugItem>,

    /// Contains picture cache tiles, and associated information.
    /// Used by the renderer to composite tiles into the framebuffer,
    /// or hand them off to an OS compositor.
    pub composite_state: CompositeState,

    /// Main GPU data buffer constructed (primarily) during the prepare
    /// pass for primitives that were visible and dirty.
    pub gpu_buffer_f: GpuBufferF,
    pub gpu_buffer_i: GpuBufferI,

    /// The backing store for the frame's allocator.
    ///
    /// # Safety
    ///
    /// Must not be dropped while frame allocations are alive.
    ///
    /// Rust has deterministic drop order [1]. We rely on `allocator_memory`
    /// being the last member of the `Frame` struct so that it is dropped
    /// after the frame's containers.
    ///
    /// [1]: https://doc.rust-lang.org/reference/destructors.html
    pub allocator_memory: FrameMemory,
}

impl Frame {
    // This frame must be flushed if it writes to the
    // texture cache, and hasn't been drawn yet.
    pub fn must_be_drawn(&self) -> bool {
        self.has_texture_cache_tasks && !self.has_been_rendered
    }

    // Returns true if this frame doesn't alter what is on screen currently.
    pub fn is_nop(&self) -> bool {
        // If there are no off-screen passes, that implies that there are no
        // picture cache tiles, and no texture cache tasks being updates. If this
        // is the case, we can consider the frame a nop (higher level checks
        // test if a composite is needed due to picture cache surfaces moving
        // or external surfaces being updated).
        self.passes.is_empty()
    }
}
