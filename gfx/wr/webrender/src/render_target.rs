/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


use api::units::*;
use api::{ColorF, LineOrientation, BorderStyle};
use crate::batch::{AlphaBatchBuilder, AlphaBatchContainer, BatchTextures, TextureSet};
use crate::batch::{BatchBuilder, INVALID_SEGMENT_INDEX, ClipMaskInstanceList};
use crate::render_task::{SubTask, RectangleClipSubTask, ImageClipSubTask, PathClipSubTask};
use crate::path_tiler::{PathInfo, PathTiler, PathTilerOutput};
use crate::command_buffer::{CommandBufferList, QuadFlags};
use crate::pattern::{Pattern, PatternKind, PatternShaderInput};
use crate::segment::EdgeMask;
use crate::spatial_tree::SpatialTree;
use crate::gpu_types::{BorderInstance, SVGFEFilterInstance, BlurDirection, BlurInstance, PrimitiveHeaders, ScalingInstance};
use crate::gpu_types::{ZBufferIdGenerator, MaskInstance, BlurEdgeMode, ClipSpace};
use crate::gpu_types::{ZBufferId, PrimitiveInstanceData};
use crate::transform::GpuTransformId;
use crate::util::ScaleOffset;
use crate::internal_types::{CacheTextureId, FastHashMap, FrameAllocator, FrameMemory, FrameVec, TextureSource};
use crate::svg_filter::FilterGraphOp;
use crate::picture::ResolvedSurfaceTexture;
use crate::tile_cache::{SliceId, TileCacheInstance};
use crate::transform::TransformPalette;
use crate::quad;
use crate::prim_store::{PrimitiveInstance, PrimitiveStore, PrimitiveScratchBuffer};
use crate::profiler::{self, TransactionProfile};
use crate::renderer::{BlendMode, GpuBufferAddress, GpuBufferBuilder};
use crate::render_backend::{DataStores, DlStores};
use crate::render_task::{RenderTaskKind, RenderTaskAddress};
use crate::render_task::{RenderTask, ScalingTask, SVGFEFilterTask};
use crate::render_task_graph::{RenderTaskGraph, RenderTaskId};
use crate::resource_cache::ResourceCache;
use crate::spatial_tree::SpatialNodeIndex;


const STYLE_SOLID: i32 = ((BorderStyle::Solid as i32) << 8) | ((BorderStyle::Solid as i32) << 16);
const STYLE_MASK: i32 = 0x00FF_FF00;
const SUPERELLIPSE_MASK: i32 = 1 << 29;

/// A tag used to identify the output format of a `RenderTarget`.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum RenderTargetKind {
    Color, // RGBA8
    Alpha, // R8
}

pub struct RenderTargetContext<'a, 'rc> {
    pub prim_store: &'a PrimitiveStore,
    pub resource_cache: &'rc mut ResourceCache,
    pub use_dual_source_blending: bool,
    pub break_advanced_blend_batches: bool,
    pub batch_lookback_count: usize,
    pub spatial_tree: &'a SpatialTree,
    pub data_stores: &'a DataStores,
    pub dl_stores: &'a DlStores,
    pub scratch: &'a PrimitiveScratchBuffer,
    pub screen_device_rect: DeviceRect,
    pub tile_caches: &'a FastHashMap<SliceId, Box<TileCacheInstance>>,
    pub root_spatial_node_index: SpatialNodeIndex,
    pub frame_memory: &'a mut FrameMemory,
    pub profile: &'a mut TransactionProfile,
}

/// A series of `RenderTarget` instances, serving as the high-level container
/// into which `RenderTasks` are assigned.
///
/// During the build phase, we iterate over the tasks in each `RenderPass`. For
/// each task, we invoke `allocate()` on the `RenderTargetList`, which in turn
/// attempts to allocate an output region in the last `RenderTarget` in the
/// list. If allocation fails (or if the list is empty), a new `RenderTarget` is
/// created and appended to the list. The build phase then assign the task into
/// the target associated with the final allocation.
///
/// The result is that each `RenderPass` is associated with one or two
/// `RenderTargetLists`, depending on whether we have all our tasks have the
/// same `RenderTargetKind`. The lists are then shipped to the `Renderer`, which
/// allocates a device texture array, with one slice per render target in the
/// list.
///
/// The upshot of this scheme is that it maximizes batching. In a given pass,
/// we need to do a separate batch for each individual render target. But with
/// the texture array, we can expose the entirety of the previous pass to each
/// task in the current pass in a single batch, which generally allows each
/// task to be drawn in a single batch regardless of how many results from the
/// previous pass it depends on.
///
/// Note that in some cases (like drop-shadows), we can depend on the output of
/// a pass earlier than the immediately-preceding pass.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct RenderTargetList {
    pub targets: FrameVec<RenderTarget>,
}

impl RenderTargetList {
    pub fn new(allocator: FrameAllocator) -> Self {
        RenderTargetList {
            targets: allocator.new_vec(),
        }
    }

    pub fn build(
        &mut self,
        ctx: &mut RenderTargetContext,
        render_tasks: &RenderTaskGraph,
        prim_headers: &mut PrimitiveHeaders,
        transforms: &mut TransformPalette,
        z_generator: &mut ZBufferIdGenerator,
        prim_instances: &[PrimitiveInstance],
        cmd_buffers: &CommandBufferList,
        gpu_buffer_builder: &mut GpuBufferBuilder,
        path_tiler: &mut PathTiler,
    ) {
        if self.targets.is_empty() {
            return;
        }

        for target in &mut self.targets {
            target.build(
                ctx,
                render_tasks,
                prim_headers,
                transforms,
                z_generator,
                prim_instances,
                cmd_buffers,
                gpu_buffer_builder,
                path_tiler,
            );
        }
    }
}

const NUM_PATTERNS: usize = crate::pattern::NUM_PATTERNS as usize;

/// Contains the work (in the form of instance arrays) needed to fill a color
/// color (RGBA8) or alpha output surface.
///
/// In graphics parlance, a "render target" usually means "a surface (texture or
/// framebuffer) bound to the output of a shader". This struct has a slightly
/// different meaning, in that it represents the operations on that surface
/// _before_ it's actually bound and rendered. So a `RenderTarget` is built by
/// the `RenderBackend` by inserting tasks, and then shipped over to the
/// `Renderer` where a device surface is resolved and the tasks are transformed
/// into draw commands on that surface.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct RenderTarget {
    pub target_kind: RenderTargetKind,
    pub cached: bool,
    screen_size: DeviceIntSize,
    pub texture_id: CacheTextureId,

    pub alpha_batch_containers: FrameVec<AlphaBatchContainer>,
    // List of blur operations to apply for this render target.
    pub vertical_blurs: FastHashMap<TextureSource, FrameVec<BlurInstance>>,
    pub horizontal_blurs: FastHashMap<TextureSource, FrameVec<BlurInstance>>,
    pub scalings: FastHashMap<TextureSource, FrameVec<ScalingInstance>>,
    pub svg_nodes: FrameVec<(BatchTextures, FrameVec<SVGFEFilterInstance>)>,
    pub blits: FrameVec<BlitJob>,
    alpha_tasks: FrameVec<RenderTaskId>,

    pub prim_instances: [FastHashMap<TextureSet, FrameVec<PrimitiveInstanceData>>; NUM_PATTERNS],
    pub prim_instances_with_scissor: FastHashMap<(DeviceIntRect, PatternKind), FastHashMap<TextureSet, FrameVec<PrimitiveInstanceData>>>,

    pub clip_masks: ClipMaskInstanceList,

    pub border_segments_complex: FrameVec<BorderInstance>,
    pub border_segments_solid: FrameVec<BorderInstance>,
    pub border_segments_complex_superellipse: FrameVec<BorderInstance>,
    pub border_segments_solid_superellipse: FrameVec<BorderInstance>,
    pub line_decorations: FrameVec<LineDecorationJob>,

    // Clearing render targets has a fair amount of special cases.
    // The general rules are:
    // - Depth (for at least the used potion of the target) is always cleared if it
    //   is used by the target. The rest of this explaination focuses on clearing
    //   color/alpha textures.
    // - For non-cached targets we either clear the entire target or the used portion
    //   (unless clear_color is None).
    // - Cached render targets require precise partial clears which are specified
    //   via the vectors below (if clearing is needed at all).
    //
    // See also: Renderer::clear_render_target

    // Areas that *must* be cleared.
    // Even if a global target clear is done, we try to honor clearing the rects that
    // have a different color than the global clear color.
    pub clears: FrameVec<(DeviceIntRect, ColorF)>,

    // Optionally track the used rect of the render target, to give the renderer
    // an opportunity to only clear the used portion of the target as an optimization.
    // Note: We make the simplifying assumption that if clear vectors AND used_rect
    // are specified, then the rects from the clear vectors are contained in
    // used_rect.
    pub used_rect: Option<DeviceIntRect>,
    // The global clear color is Some(TRANSPARENT) by default. If we are drawing
    // a single render task in this target, it can be set to something else.
    // If clear_color is None, only the clears/zero_clears/one_clears are done.
    pub clear_color: Option<ColorF>,
}

impl RenderTarget {
    pub fn new(
        target_kind: RenderTargetKind,
        cached: bool,
        texture_id: CacheTextureId,
        screen_size: DeviceIntSize,
        used_rect: Option<DeviceIntRect>,
        memory: &FrameMemory,
    ) -> Self {
        RenderTarget {
            target_kind,
            cached,
            screen_size,
            texture_id,
            alpha_batch_containers: memory.new_vec(),
            vertical_blurs: FastHashMap::default(),
            horizontal_blurs: FastHashMap::default(),
            scalings: FastHashMap::default(),
            svg_nodes: memory.new_vec(),
            blits: memory.new_vec(),
            alpha_tasks: memory.new_vec(),
            used_rect,
            clear_color: Some(ColorF::TRANSPARENT),
            prim_instances: std::array::from_fn(|_| FastHashMap::default()),
            prim_instances_with_scissor: FastHashMap::default(),
            clip_masks: ClipMaskInstanceList::new(memory),
            border_segments_complex: memory.new_vec(),
            border_segments_solid: memory.new_vec(),
            border_segments_complex_superellipse: memory.new_vec(),
            border_segments_solid_superellipse: memory.new_vec(),
            clears: memory.new_vec(),
            line_decorations: memory.new_vec(),
        }
    }

    pub fn build(
        &mut self,
        ctx: &mut RenderTargetContext,
        render_tasks: &RenderTaskGraph,
        prim_headers: &mut PrimitiveHeaders,
        transforms: &mut TransformPalette,
        z_generator: &mut ZBufferIdGenerator,
        prim_instances: &[PrimitiveInstance],
        cmd_buffers: &CommandBufferList,
        gpu_buffer_builder: &mut GpuBufferBuilder,
        path_tiler: &mut PathTiler,
    ) {
        tracy_rs::profile_scope!("build");
        let mut merged_batches = AlphaBatchContainer::new(None, &ctx.frame_memory);

        for task_id in &self.alpha_tasks {
            tracy_rs::profile_scope!("alpha_task");
            let task = &render_tasks[*task_id];

            match task.kind {
                RenderTaskKind::Picture(ref pic_task) => {
                    let target_rect = task.get_target_rect();

                    let content_rect = DeviceIntRect::from_origin_and_size(
                        target_rect.min,
                        pic_task.content_size,
                    );
                    let scissor_rect = if pic_task.capture_clear_color.is_some() {
                        Some(content_rect)
                    } else if pic_task.can_merge {
                        None
                    } else {
                        Some(target_rect)
                    };

                    if !pic_task.can_use_shared_surface {
                        self.clear_color = pic_task.clear_color;
                    }
                    if let Some(content_clear_color) = pic_task.capture_clear_color {
                        // The renderer may skip a clear whose color matches the
                        // target's, so the content and padding clears must not
                        // overlap.
                        self.clears.push((content_rect, content_clear_color));
                        let right = DeviceIntRect::new(
                            DeviceIntPoint::new(content_rect.max.x, target_rect.min.y),
                            target_rect.max,
                        );
                        let bottom = DeviceIntRect::new(
                            DeviceIntPoint::new(target_rect.min.x, content_rect.max.y),
                            DeviceIntPoint::new(content_rect.max.x, target_rect.max.y),
                        );
                        for rect in [right, bottom] {
                            if !rect.is_empty() {
                                self.clears.push((rect, ColorF::TRANSPARENT));
                            }
                        }
                    } else if let Some(clear_color) = pic_task.clear_color {
                        self.clears.push((target_rect, clear_color));
                    } else if self.cached {
                        self.clears.push((target_rect, ColorF::TRANSPARENT));
                    }

                    // TODO(gw): The type names of AlphaBatchBuilder and BatchBuilder
                    //           are still confusing. Once more of the picture caching
                    //           improvement code lands, the AlphaBatchBuilder and
                    //           AlphaBatchList types will be collapsed into one, which
                    //           should simplify coming up with better type names.
                    let alpha_batch_builder = AlphaBatchBuilder::new(
                        self.screen_size,
                        ctx.break_advanced_blend_batches,
                        ctx.batch_lookback_count,
                        *task_id,
                        (*task_id).into(),
                        &ctx.frame_memory,
                    );

                    let mut batch_builder = BatchBuilder::new(
                        alpha_batch_builder,
                        DeviceIntRect::from_size(target_rect.size()),
                        pic_task.content_origin,
                    );
                    path_tiler.begin_target(DeviceIntRect::from_size(target_rect.size()));
                    let cmd_buffer = cmd_buffers.get(pic_task.cmd_buffer_index);

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
                            path_tiler,
                            segments,
                        );
                    });

                    let alpha_batch_builder = batch_builder.finalize();

                    alpha_batch_builder.build(
                        &mut self.alpha_batch_containers,
                        &mut merged_batches,
                        target_rect,
                        scissor_rect,
                    );
                }
                _ => {
                    unreachable!();
                }
            }
        }

        if !merged_batches.is_empty() {
            self.alpha_batch_containers.push(merged_batches);
        }
    }

    pub fn texture_id(&self) -> CacheTextureId {
        self.texture_id
    }

    pub fn add_task(
        &mut self,
        task_id: RenderTaskId,
        ctx: &mut RenderTargetContext,
        gpu_buffer_builder: &mut GpuBufferBuilder,
        render_tasks: &RenderTaskGraph,
        transforms: &mut TransformPalette,
        path_tiler: &mut PathTiler,
    ) {
        tracy_rs::profile_scope!("add_task");
        let task = &render_tasks[task_id];
        let target_rect = task.get_target_rect();

        match task.kind {
            RenderTaskKind::Prim(ref info) => {
                // If the transform is not axis aligned, we may not be covering all of the
                // pixels in the render task, so we need to clear it.
                if !info.transform_id.is_2d_axis_aligned() {
                    self.clears.push((target_rect, ColorF::TRANSPARENT));
                }
                // Note: For single-primitive tasks like this we always want to disable blending
                // and overwrite whatever pixel data is in the target, even if the primitive is
                // note entirely opaque. This is why we unconditionally set the opaque quad flag.
                let render_task_address = task_id.into();
                quad::add_to_batch(
                    info.pattern,
                    info.pattern_input,
                    render_task_address,
                    info.transform_id,
                    info.prim_address_f,
                    info.quad_flags | QuadFlags::IS_OPAQUE,
                    info.edge_flags,
                    INVALID_SEGMENT_INDEX as u8,
                    info.texture_input,
                    ZBufferId(0),
                    BlendMode::None, // This parameter is ignored
                    None,
                    render_tasks,
                    gpu_buffer_builder,
                    |key, instance| {
                        if info.prim_needs_scissor_rect {
                            self.prim_instances_with_scissor
                                .entry((target_rect, info.pattern))
                                .or_insert(FastHashMap::default())
                                .entry(key.textures.input)
                                .or_insert_with(|| ctx.frame_memory.new_vec())
                                .push(instance);
                        } else {
                            self.prim_instances[info.pattern as usize]
                                .entry(key.textures.input)
                                .or_insert_with(|| ctx.frame_memory.new_vec())
                                .push(instance);
                        }
                    }
                );
            }
            RenderTaskKind::VerticalBlur(ref info) => {
                if self.target_kind == RenderTargetKind::Alpha {
                    self.clears.push((target_rect, ColorF::TRANSPARENT));
                }
                add_blur_instances(
                    &mut self.vertical_blurs,
                    BlurDirection::Vertical,
                    info.blur_std_deviation,
                    info.blur_region,
                    info.edge_mode,
                    task_id.into(),
                    task.children[0],
                    render_tasks,
                    &ctx.frame_memory,
                );
            }
            RenderTaskKind::HorizontalBlur(ref info) => {
                if self.target_kind == RenderTargetKind::Alpha {
                    self.clears.push((target_rect, ColorF::TRANSPARENT));
                }
                add_blur_instances(
                    &mut self.horizontal_blurs,
                    BlurDirection::Horizontal,
                    info.blur_std_deviation,
                    info.blur_region,
                    info.edge_mode,
                    task_id.into(),
                    task.children[0],
                    render_tasks,
                    &ctx.frame_memory,
                );
            }
            RenderTaskKind::Picture(..) => {
                self.alpha_tasks.push(task_id);
            }
            RenderTaskKind::SVGFENode(ref task_info) => {
                add_svg_filter_node_instances(
                    &mut self.svg_nodes,
                    render_tasks,
                    &task_info,
                    task,
                    task.children.get(0).cloned(),
                    task.children.get(1).cloned(),
                    task_info.extra_gpu_data,
                    &ctx.frame_memory,
                )
            }
            RenderTaskKind::Empty(..) => {
                // TODO(gw): Could likely be more efficient by choosing to clear to 0 or 1
                //           based on the clip chain, or even skipping clear and masking the
                //           prim region with blend disabled.
                self.clears.push((target_rect, ColorF::WHITE));
            }
            RenderTaskKind::ClipRegion(ref region_task) => {
                // The mask is accumulated with multiply blending in handle_clips,
                // so the target must be initialized to one.
                self.clears.push((target_rect, ColorF::WHITE));

                let device_rect = DeviceRect::from_size(target_rect.size().to_f32());

                let (clip_address, fast_path, superellipse) = quad::write_rounded_rect_clip_blocks(
                    &mut gpu_buffer_builder.f32,
                    region_task.clip_rect,
                    &region_task.radius,
                    region_task.inset,
                    region_task.mode,
                );

                let quad_address = quad::write_device_prim_blocks(
                    &mut gpu_buffer_builder.f32,
                    &device_rect,
                    &device_rect,
                    ColorF::WHITE,
                    RenderTaskId::INVALID,
                    &[],
                    ScaleOffset::identity(),
                );

                // The clip parameters are in the clip's local space whereas the quad
                // is positioned directly in device space, so the shader maps from
                // device to clip space with the inverse device pixel scale.
                let inv_scale = region_task.device_pixel_scale.inverse().get();
                let clip_transform_id = transforms.gpu.get_custom(
                    LayoutToPictureTransform::scale(inv_scale, inv_scale, 1.0),
                );

                let task_address = task_id.into();

                quad::add_to_batch(
                    PatternKind::Mask,
                    PatternShaderInput::default(),
                    task_address,
                    GpuTransformId::IDENTITY,
                    quad_address,
                    QuadFlags::IS_MASK | QuadFlags::APPLY_RENDER_TASK_CLIP,
                    EdgeMask::empty(),
                    INVALID_SEGMENT_INDEX as u8,
                    [RenderTaskId::INVALID; 3],
                    ZBufferId(0),
                    BlendMode::None, // This parameter is ignored.
                    None,
                    render_tasks,
                    gpu_buffer_builder,
                    |_, prim| {
                        let instance = MaskInstance {
                            prim,
                            clip_transform_id,
                            clip_address: clip_address.as_int(),
                            clip_space: ClipSpace::Device.as_int(),
                            unused: 0,
                        };

                        if fast_path {
                            self.clip_masks.mask_instances_fast.push(instance);
                        } else if superellipse {
                            self.clip_masks.mask_instances_superellipse.push(instance);
                        } else {
                            self.clip_masks.mask_instances_slow.push(instance);
                        }
                    },
                );
            }
            RenderTaskKind::Scaling(ref info) => {
                add_scaling_instances(
                    info,
                    &mut self.scalings,
                    task,
                    task.children.first().map(|&child| &render_tasks[child]),
                    &ctx.frame_memory,
                );
            }
            RenderTaskKind::Blit(ref task_info) => {
                let target_rect = task.get_target_rect();
                self.blits.push(BlitJob {
                    source: task_info.source,
                    source_rect: task_info.source_rect,
                    target_rect,
                });
            }
            RenderTaskKind::LineDecoration(ref info) => {
                self.clears.push((target_rect, ColorF::TRANSPARENT));

                self.line_decorations.push(LineDecorationJob {
                    task_rect: target_rect.to_f32(),
                    local_size: info.local_size,
                    style: info.style as i32,
                    axis_select: match info.orientation {
                        LineOrientation::Horizontal => 0.0,
                        LineOrientation::Vertical => 1.0,
                    },
                    wavy_line_thickness: info.wavy_line_thickness,
                });
            }
            RenderTaskKind::Border(ref task_info) => {
                self.clears.push((target_rect, ColorF::TRANSPARENT));

                let task_origin = target_rect.min.to_f32();
                // TODO(gw): Clone here instead of a move of this vec, since the frame
                //           graph is immutable by this point. It's rare that borders
                //           are drawn since they are persisted in the texture cache,
                //           but perhaps this could be improved in future.
                let instances = task_info.instances.clone();
                for mut instance in instances {
                    // TODO(gw): It may be better to store the task origin in
                    //           the render task data instead of per instance.
                    instance.task_origin = task_origin;
                    let superellipse = (instance.flags & SUPERELLIPSE_MASK) != 0;
                    if instance.flags & STYLE_MASK == STYLE_SOLID {
                        if superellipse {
                            self.border_segments_solid_superellipse.push(instance);
                        } else {
                            self.border_segments_solid.push(instance);
                        }
                    } else {
                        if superellipse {
                            self.border_segments_complex_superellipse.push(instance);
                        } else {
                            self.border_segments_complex.push(instance);
                        }
                    }
                }
            }
            RenderTaskKind::Image(..) |
            RenderTaskKind::Cached(..) => {
                panic!("Should not be added to color target!");
            }
            RenderTaskKind::Readback(..) => {}
            #[cfg(test)]
            RenderTaskKind::Test(..) => {}
        }

        let task_address = task_id.into();
        for sub_task_id in task.sub_tasks.clone() {
            let sub_task = &render_tasks[sub_task_id];
            match sub_task {
                SubTask::RectangleClip(clip_task) => {
                    add_rect_clip_task_to_batch(
                        clip_task,
                        &target_rect,
                        task_address,
                        &ctx.frame_memory,
                        render_tasks,
                        gpu_buffer_builder,
                        &mut self.clip_masks
                    );
                }
                SubTask::ImageClip(clip_task) => {
                    add_image_clip_task_to_batch(
                        clip_task,
                        &target_rect,
                        task_address,
                        &ctx.frame_memory,
                        render_tasks,
                        gpu_buffer_builder,
                        &mut self.clip_masks
                    );
                }
                SubTask::PathClip(clip_task) => {
                    ctx.profile.add(profiler::PATH_CLIPS, 1);
                    add_path_clip_task_to_batch(
                        clip_task,
                        &target_rect,
                        task_address,
                        gpu_buffer_builder,
                        path_tiler,
                        &mut self.clip_masks
                    );
                }
            }
        }
    }

    pub fn needs_depth(&self) -> bool {
        self.alpha_batch_containers.iter().any(|ab| {
            !ab.opaque_batches.is_empty()
        })
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum PictureCacheTargetKind {
    Draw {
        alpha_batch_container: AlphaBatchContainer,
    },
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PictureCacheTarget {
    pub surface: ResolvedSurfaceTexture,
    pub kind: PictureCacheTargetKind,
    pub clear_color: Option<ColorF>,
    pub dirty_rect: DeviceIntRect,
    pub valid_rect: DeviceIntRect,
}

fn add_blur_instances(
    instances: &mut FastHashMap<TextureSource, FrameVec<BlurInstance>>,
    blur_direction: BlurDirection,
    blur_std_deviation: f32,
    blur_region: DeviceIntSize,
    edge_mode: BlurEdgeMode,
    task_address: RenderTaskAddress,
    src_task_id: RenderTaskId,
    render_tasks: &RenderTaskGraph,
    memory: &FrameMemory,
) {
    let source = render_tasks[src_task_id].get_texture_source();

    let instance = BlurInstance {
        task_address,
        src_task_address: src_task_id.into(),
        blur_direction: blur_direction.as_int(),
        blur_std_deviation,
        edge_mode: edge_mode.as_int(),
        blur_region: blur_region.to_f32(),
    };

    instances
        .entry(source)
        .or_insert_with(|| memory.new_vec())
        .push(instance);
}

fn add_scaling_instances(
    task: &ScalingTask,
    instances: &mut FastHashMap<TextureSource, FrameVec<ScalingInstance>>,
    target_task: &RenderTask,
    source_task: Option<&RenderTask>,
    memory: &FrameMemory,
) {
    let target_rect = target_task
        .get_target_rect()
        .inner_box(task.padding)
        .to_f32();

    let source = source_task.unwrap().get_texture_source();

    let source_rect = source_task.unwrap().get_target_rect().to_f32();

    instances
        .entry(source)
        .or_insert_with(|| memory.new_vec())
        .push(ScalingInstance::new(
            target_rect,
            source_rect,
            source.uses_normalized_uvs(),
        ));
}

/// Generates SVGFEFilterInstances from a single SVGFEFilterTask, this is what
/// prepares vertex data for the shader, and adds it to the appropriate batch.
///
/// The interesting parts of the handling of SVG filters are:
/// * scene_building.rs : wrap_prim_with_filters
/// * picture.rs : get_coverage_svgfe
/// * render_task.rs : new_svg_filter_graph
/// * render_target.rs : add_svg_filter_node_instances (you are here)
fn add_svg_filter_node_instances(
    instances: &mut FrameVec<(BatchTextures, FrameVec<SVGFEFilterInstance>)>,
    render_tasks: &RenderTaskGraph,
    task_info: &SVGFEFilterTask,
    target_task: &RenderTask,
    input_1_task: Option<RenderTaskId>,
    input_2_task: Option<RenderTaskId>,
    extra_data_address: Option<GpuBufferAddress>,
    memory: &FrameMemory,
) {
    let node = &task_info.node;
    let op = &task_info.op;
    let mut textures = BatchTextures::empty();

    // We have to undo the inflate here as the inflated target rect is meant to
    // have a blank border
    let target_rect = target_task
        .get_target_rect()
        .inner_box(DeviceIntSideOffsets::new(node.inflate as i32, node.inflate as i32, node.inflate as i32, node.inflate as i32))
        .to_f32();

    let mut instance = SVGFEFilterInstance {
        target_rect,
        input_1_content_scale_and_offset: [0.0; 4],
        input_2_content_scale_and_offset: [0.0; 4],
        input_1_task_address: RenderTaskId::INVALID.into(),
        input_2_task_address: RenderTaskId::INVALID.into(),
        kind: 0,
        input_count: node.inputs.len() as u16,
        extra_data_address: extra_data_address.unwrap_or(GpuBufferAddress::INVALID).as_int(),
    };

    // Must match FILTER_* in cs_svg_filter_node.glsl
    instance.kind = match op {
        // Identity does not modify color, no linear case
        FilterGraphOp::SVGFEIdentity => 0,
        // SourceGraphic does not have its own shader mode, it uses Identity.
        FilterGraphOp::SVGFESourceGraphic => 0,
        // SourceAlpha does not have its own shader mode, it uses ToAlpha.
        FilterGraphOp::SVGFESourceAlpha => 4,
        // Opacity scales the entire rgba color, so it does not need a linear
        // case as the rgb / a ratio does not change (sRGB is a curve on the RGB
        // before alpha multiply, not after)
        FilterGraphOp::SVGFEOpacity{..} => 2,
        FilterGraphOp::SVGFEToAlpha => 4,
        FilterGraphOp::SVGFEBlendColor => {match node.linear {false => 6, true => 7}},
        FilterGraphOp::SVGFEBlendColorBurn => {match node.linear {false => 8, true => 9}},
        FilterGraphOp::SVGFEBlendColorDodge => {match node.linear {false => 10, true => 11}},
        FilterGraphOp::SVGFEBlendDarken => {match node.linear {false => 12, true => 13}},
        FilterGraphOp::SVGFEBlendDifference => {match node.linear {false => 14, true => 15}},
        FilterGraphOp::SVGFEBlendExclusion => {match node.linear {false => 16, true => 17}},
        FilterGraphOp::SVGFEBlendHardLight => {match node.linear {false => 18, true => 19}},
        FilterGraphOp::SVGFEBlendHue => {match node.linear {false => 20, true => 21}},
        FilterGraphOp::SVGFEBlendLighten => {match node.linear {false => 22, true => 23}},
        FilterGraphOp::SVGFEBlendLuminosity => {match node.linear {false => 24, true => 25}},
        FilterGraphOp::SVGFEBlendMultiply => {match node.linear {false => 26, true => 27}},
        FilterGraphOp::SVGFEBlendNormal => {match node.linear {false => 28, true => 29}},
        FilterGraphOp::SVGFEBlendOverlay => {match node.linear {false => 30, true => 31}},
        FilterGraphOp::SVGFEBlendSaturation => {match node.linear {false => 32, true => 33}},
        FilterGraphOp::SVGFEBlendScreen => {match node.linear {false => 34, true => 35}},
        FilterGraphOp::SVGFEBlendSoftLight => {match node.linear {false => 36, true => 37}},
        FilterGraphOp::SVGFEColorMatrix{..} => {match node.linear {false => 38, true => 39}},
        FilterGraphOp::SVGFEComponentTransfer => unreachable!(),
        FilterGraphOp::SVGFEComponentTransferInterned{..} => {match node.linear {false => 40, true => 41}},
        FilterGraphOp::SVGFECompositeArithmetic{..} => {match node.linear {false => 42, true => 43}},
        FilterGraphOp::SVGFECompositeATop => {match node.linear {false => 44, true => 45}},
        FilterGraphOp::SVGFECompositeIn => {match node.linear {false => 46, true => 47}},
        FilterGraphOp::SVGFECompositeLighter => {match node.linear {false => 48, true => 49}},
        FilterGraphOp::SVGFECompositeOut => {match node.linear {false => 50, true => 51}},
        FilterGraphOp::SVGFECompositeOver => {match node.linear {false => 52, true => 53}},
        FilterGraphOp::SVGFECompositeXOR => {match node.linear {false => 54, true => 55}},
        FilterGraphOp::SVGFEConvolveMatrixEdgeModeDuplicate{..} => {match node.linear {false => 56, true => 57}},
        FilterGraphOp::SVGFEConvolveMatrixEdgeModeNone{..} => {match node.linear {false => 58, true => 59}},
        FilterGraphOp::SVGFEConvolveMatrixEdgeModeWrap{..} => {match node.linear {false => 60, true => 61}},
        FilterGraphOp::SVGFEDiffuseLightingDistant{..} => {match node.linear {false => 62, true => 63}},
        FilterGraphOp::SVGFEDiffuseLightingPoint{..} => {match node.linear {false => 64, true => 65}},
        FilterGraphOp::SVGFEDiffuseLightingSpot{..} => {match node.linear {false => 66, true => 67}},
        FilterGraphOp::SVGFEDisplacementMap{..} => {match node.linear {false => 68, true => 69}},
        FilterGraphOp::SVGFEDropShadow{..} => {match node.linear {false => 70, true => 71}},
        // feFlood takes an sRGB color and does no math on it, no linear case
        FilterGraphOp::SVGFEFlood{..} => 72,
        FilterGraphOp::SVGFEGaussianBlur{..} => {match node.linear {false => 74, true => 75}},
        // feImage does not meaningfully modify the color of its input, though a
        // case could be made for gamma-correct image scaling, that's a bit out
        // of scope for now
        FilterGraphOp::SVGFEImage{..} => 76,
        FilterGraphOp::SVGFEMorphologyDilate{..} => {match node.linear {false => 80, true => 81}},
        FilterGraphOp::SVGFEMorphologyErode{..} => {match node.linear {false => 82, true => 83}},
        FilterGraphOp::SVGFESpecularLightingDistant{..} => {match node.linear {false => 86, true => 87}},
        FilterGraphOp::SVGFESpecularLightingPoint{..} => {match node.linear {false => 88, true => 89}},
        FilterGraphOp::SVGFESpecularLightingSpot{..} => {match node.linear {false => 90, true => 91}},
        // feTile does not modify color, no linear case
        FilterGraphOp::SVGFETile => 92,
        FilterGraphOp::SVGFETurbulenceWithFractalNoiseWithNoStitching{..} => {match node.linear {false => 94, true => 95}},
        FilterGraphOp::SVGFETurbulenceWithFractalNoiseWithStitching{..} => {match node.linear {false => 96, true => 97}},
        FilterGraphOp::SVGFETurbulenceWithTurbulenceNoiseWithNoStitching{..} => {match node.linear {false => 98, true => 99}},
        FilterGraphOp::SVGFETurbulenceWithTurbulenceNoiseWithStitching{..} => {match node.linear {false => 100, true => 101}},
    };

    // This is a bit of an ugly way to do this, but avoids code duplication.
    let mut resolve_input = |index: usize, src_task: Option<RenderTaskId>| -> (RenderTaskAddress, [f32; 4]) {
        let mut src_task_id = RenderTaskId::INVALID;
        let mut resolved_scale_and_offset: [f32; 4] = [0.0; 4];
        if let Some(input) = node.inputs.get(index) {
            src_task_id = src_task.unwrap();
            let src_task = &render_tasks[src_task_id];

            textures.input.colors[index] = src_task.get_texture_source();
            let src_task_size = src_task.location.size();
            let src_scale_x = (src_task_size.width as f32 - input.inflate as f32 * 2.0) / input.subregion.width();
            let src_scale_y = (src_task_size.height as f32 - input.inflate as f32 * 2.0) / input.subregion.height();
            let scale_x = src_scale_x * node.subregion.width();
            let scale_y = src_scale_y * node.subregion.height();
            let offset_x = src_scale_x * (node.subregion.min.x - input.subregion.min.x) + input.inflate as f32;
            let offset_y = src_scale_y * (node.subregion.min.y - input.subregion.min.y) + input.inflate as f32;
            resolved_scale_and_offset = [
                scale_x,
                scale_y,
                offset_x,
                offset_y];
        }
        let address: RenderTaskAddress = src_task_id.into();
        (address, resolved_scale_and_offset)
    };
    (instance.input_1_task_address, instance.input_1_content_scale_and_offset) = resolve_input(0, input_1_task);
    (instance.input_2_task_address, instance.input_2_content_scale_and_offset) = resolve_input(1, input_2_task);

    // Additional instance modifications for certain filters
    match op {
        FilterGraphOp::SVGFEOpacity { valuebinding: _, value } => {
            // opacity only has one input so we can use the other
            // components to store the opacity value
            instance.input_2_content_scale_and_offset = [*value, 0.0, 0.0, 0.0];
        },
        FilterGraphOp::SVGFEMorphologyDilate { radius_x, radius_y } |
        FilterGraphOp::SVGFEMorphologyErode { radius_x, radius_y } => {
            // morphology filters only use one input, so we use the
            // second offset coord to store the radius values.
            instance.input_2_content_scale_and_offset = [*radius_x, *radius_y, 0.0, 0.0];
        },
        FilterGraphOp::SVGFEFlood { color } => {
            // flood filters don't use inputs, so we store color here.
            // We can't do the same trick on DropShadow because it does have two
            // inputs.
            instance.input_2_content_scale_and_offset = [color.r, color.g, color.b, color.a];
        },
        _ => {},
    }

    for (ref mut batch_textures, ref mut batch) in instances.iter_mut() {
        if let Some(combined_textures) = batch_textures.combine_textures(textures) {
            batch.push(instance);
            // Update the batch textures to the newly combined batch textures
            *batch_textures = combined_textures;
            // is this really the intended behavior?
            return;
        }
    }

    let mut vec = memory.new_vec();
    vec.push(instance);

    instances.push((textures, vec));
}

// Information required to do a blit from a source to a target.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BlitJob {
    pub source: RenderTaskId,
    // Normalized region within the source task to blit from
    pub source_rect: DeviceIntRect,
    pub target_rect: DeviceIntRect,
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[repr(C)]
#[derive(Clone, Debug)]
pub struct LineDecorationJob {
    pub task_rect: DeviceRect,
    pub local_size: LayoutSize,
    pub wavy_line_thickness: f32,
    pub style: i32,
    pub axis_select: f32,
}

fn add_rect_clip_task_to_batch(
    task: &RectangleClipSubTask,
    target_rect: &DeviceIntRect,
    masked_task_address: RenderTaskAddress,
    memory: &FrameMemory,
    render_tasks: &RenderTaskGraph,
    gpu_buffers: &mut GpuBufferBuilder,
    results: &mut ClipMaskInstanceList,
) {
    quad::add_to_batch(
        PatternKind::Mask,
        PatternShaderInput::default(),
        masked_task_address,
        task.quad_transform_id,
        task.quad_address,
        task.quad_flags,
        EdgeMask::empty(),
        INVALID_SEGMENT_INDEX as u8,
        [RenderTaskId::INVALID; 3],
        ZBufferId(0),
        BlendMode::None, // This parameter is ignored.
        None,
        render_tasks,
        gpu_buffers,
        |_, prim| {
            let instance = MaskInstance {
                prim,
                clip_transform_id: task.clip_transform_id,
                clip_address: task.clip_address.as_int(),
                clip_space: task.clip_space.as_int(),
                unused: 0,
            };

            if task.needs_scissor_rect {
                if task.rounded_rect_fast_path {
                    results.mask_instances_fast_with_scissor
                            .entry(*target_rect)
                            .or_insert_with(|| memory.new_vec())
                            .push(instance);
                } else if task.rounded_rect_superellipse {
                    results.mask_instances_superellipse_with_scissor
                            .entry(*target_rect)
                            .or_insert_with(|| memory.new_vec())
                            .push(instance);
                } else {
                    results.mask_instances_slow_with_scissor
                            .entry(*target_rect)
                            .or_insert_with(|| memory.new_vec())
                            .push(instance);
                }
            } else {
                if task.rounded_rect_fast_path {
                    results.mask_instances_fast.push(instance);
                } else if task.rounded_rect_superellipse {
                    results.mask_instances_superellipse.push(instance);
                } else {
                    results.mask_instances_slow.push(instance);
                }
            }
        }
    );
}

fn add_path_clip_task_to_batch(
    task: &PathClipSubTask,
    target_rect: &DeviceIntRect,
    masked_task_address: RenderTaskAddress,
    gpu_buffers: &mut GpuBufferBuilder,
    tiler: &mut PathTiler,
    results: &mut ClipMaskInstanceList,
) {
    let path_info = PathInfo {
        render_task_address: masked_task_address.0 as u32,
        fill_rule: task.fill_rule,
        // The tiles cover the outside of the path, which dest-out removes.
        inverted: true,
        opacity: 1.0,
    };
    let mut writer = gpu_buffers.i32.write_blocks(1);
    writer.push_one(path_info.encode());
    let path_address = writer.finish();

    tiler.begin_target(DeviceIntRect::from_size(target_rect.size()));
    tiler.fill_path(
        &task.path,
        &task.transform,
        task.fill_rule,
        path_info.inverted,
        path_address.as_u32(),
        &mut PathTilerOutput {
            edges: &mut gpu_buffers.edges,
            tiles: &mut results.path_tiles,
            solid_tiles: None,
        },
    );
}

fn add_image_clip_task_to_batch(
    task: &ImageClipSubTask,
    target_rect: &DeviceIntRect,
    masked_task_address: RenderTaskAddress,
    memory: &FrameMemory,
    render_tasks: &RenderTaskGraph,
    gpu_buffers: &mut GpuBufferBuilder,
    results: &mut ClipMaskInstanceList,
) {
    // A current oddity of the quads infrastructure is that image sources are encoded in
    // quad segment data so we need to have at least one segment if we have a uv rect as
    // input.
    let segment_index = 0;

    let pattern = Pattern::texture(task.src_task, false);

    quad::add_to_batch(
        pattern.kind,
        pattern.shader_input,
        masked_task_address,
        task.quad_transform_id,
        task.quad_address,
        task.quad_flags,
        EdgeMask::empty(),
        segment_index,
        [task.src_task, RenderTaskId::INVALID, RenderTaskId::INVALID],
        ZBufferId(0),
        BlendMode::None, // This parameter is ignored.
        None,
        render_tasks,
        gpu_buffers,
        |_, prim| {
            let texture = render_tasks
                .resolve_texture(task.src_task)
                .expect("bug: texture not found for tile");

            if task.needs_scissor_rect {
                results
                    .image_mask_instances_with_scissor
                    .entry((*target_rect, texture))
                    .or_insert_with(|| memory.new_vec())
                    .push(prim);
            } else {
                results
                    .image_mask_instances
                    .entry(texture)
                    .or_insert_with(|| memory.new_vec())
                    .push(prim);
            }
        }
    );
}
