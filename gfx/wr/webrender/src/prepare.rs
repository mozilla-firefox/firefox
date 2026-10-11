/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! # Prepare pass
//!
//! The second frame building traversal of the picture tree. It runs after the
//! [visibility pass](crate::visibility).
//! Where visibility decides what is drawn, prepare decides how: it turns each
//! visible primitive into the render tasks, GPU data and draw commands needed
//! to render it.
//!
//! Its outputs are:
//!
//! - Render tasks and their dependencies in the render task graph.
//! - GPU buffer data.
//! - [`PrimitiveCommand`]s pushed into the command buffers of the surfaces and
//!   tiles the primitive is drawn into. `batch.rs` replays these at the
//!   end of frame building to produce the actual draw calls.
//!
//! ## Traversal
//!
//! [`prepare_picture`] is the entry point. It does a recursive traversal of
//! the picture and it's visible items.
//!
//! ## Per-primitive work
//!
//! The bulk of the module is a large match over [`PrimitiveKind`] in
//! `prepare_prim_for_render`, where each kind:
//!
//! - Requests the resources it needs, such as texture cache entries (images,
//!   glyphs) or render tasks (rasterized borders, blurred box shadow,cached
//!   gradients, etc.).
//! - Writes the GPU data for the primitive.
//! - Push potentially multiple commands to command buffers.
//!

use api::{ColorF, DebugFlags, ExtendMode, ExternalImageData, ExternalImageType, GradientStop, ImageBufferKind};
use crate::border_image::prepare_border_image_nine_patch;
use crate::pattern::cutout::Cutout;
use crate::render_task_graph::RenderTaskId;

use crate::util::MaxRect;
use crate::box_shadow::prepare_box_shadow;

use crate::pattern::gradient::linear_gradient_pattern;
use crate::pattern::{Pattern, PatternBuilder, PatternBuilderState};
use crate::prim_store::gradient::{decompose_axis_aligned_gradient, linear_gradient_decomposes};
use crate::segment::EdgeMask;
use api::units::*;
use euclid::Scale;
use crate::composite::CompositorSurfaceKind;
use crate::command_buffer::{CommandBufferIndex, PrimitiveCommand};

use crate::clip::{ClipDataStore, ClipNodeRange};
use crate::pattern::image::ImagePattern;

use crate::pattern::yuv::YuvPattern;
use crate::pattern::backdrop::BackdropPattern;

use crate::picture::prepare_picture_primitive;
use crate::surface::calculate_screen_uv;
use crate::space::SpaceMapper;
use crate::spatial_tree::SpatialNodeIndex;
use crate::frame_builder::{FrameBuildingContext, FrameBuildingState, PictureContext, PictureState};
use crate::gpu_types::UvRectKind;

use crate::internal_types::{FastHashMap, PlaneSplitAnchor};
use crate::picture::PictureScratch;
use crate::surface::{SubpixelMode, SurfaceIndex};
use crate::tile_cache::{SliceId, TileCacheInstance};
use crate::prim_store::*;
use crate::quad::{self, QuadDescriptor, QuadTransformState};
use crate::quad_clip::QuadClipStack;
use crate::render_backend::DataStores;
use crate::scene_debug::{HighlightMode, SceneDebugOverride};


use crate::render_task::{EmptyTask, RenderTask, RenderTaskKind};

use crate::visibility::{DrawState, KindScratchHandle, PrimitiveDrawIndex};


const MAX_MASK_SIZE: i32 = 4096;

/// The entry point of the prepare pass.
pub fn prepare_picture(
    pic_index: PictureIndex,
    store: &mut PrimitiveStore,
    surface_index: Option<SurfaceIndex>,
    subpixel_mode: SubpixelMode,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    data_stores: &DataStores,
    scratch: &mut PrimitiveScratchBuffer,
    tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
    prim_instances: &mut Vec<PrimitiveInstance>,
) -> Option<storage::Index<PictureScratch>> {
    // Only successfully-prepared pictures are cached here, so a cache hit is
    // always a valid scratch handle. Pictures that yielded no scratch are not
    // memoized: take_context's only None path (invisible picture) is a cheap,
    // side-effect-free query that is stable within a frame, so re-consulting it
    // on a repeat visit is fine and avoids caching an invalid handle.
    if let Some(handle) = frame_state.picture_scratch_handles[pic_index.0 as usize] {
        return Some(handle);
    }

    let pic = &mut store.pictures[pic_index.0 as usize];
    let Some((pic_context, mut pic_state, scratch_handle)) = pic.take_context(
        pic_index,
        surface_index,
        subpixel_mode,
        frame_state,
        frame_context,
        data_stores,
        scratch,
        tile_caches,
    ) else {
        return None;
    };

    frame_state.picture_scratch_handles[pic_index.0 as usize] = Some(scratch_handle);
    frame_state.num_pictures += 1;

    prepare_primitives(
        store,
        &pic_context,
        &mut pic_state,
        frame_context,
        frame_state,
        data_stores,
        scratch,
        tile_caches,
        prim_instances,
    );

    store.pictures[pic_context.pic_index.0 as usize].restore_context(
        pic_context.pic_index,
        pic_context,
        frame_context,
        frame_state,
        scratch,
    );

    Some(scratch_handle)
}

fn prepare_primitives(
    store: &mut PrimitiveStore,
    pic_context: &PictureContext,
    pic_state: &mut PictureState,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    data_stores: &DataStores,
    scratch: &mut PrimitiveScratchBuffer,
    tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
    prim_instances: &mut Vec<PrimitiveInstance>,
) {
    tracy_rs::profile_scope!("prepare_primitives");
    let mut cmd_buffer_targets = Vec::new();

    let mut quad_transform = QuadTransformState::new();
    let mut quad_clips = QuadClipStack::new();

    let device_pixel_scale = frame_state.surfaces[pic_context.surface_index.0].device_pixel_scale;

    let draw_range = scratch.frame.picture_draw_range(pic_context.pic_index);
    frame_state.num_visited_primitives += draw_range.len() as u32;

    for position in draw_range {
        let draw_index = scratch.frame.picture_draw(position);
        let draw = scratch.frame.draw(draw_index);
        let prim_instance_index = draw.prim_instance_index.0 as usize;
        let spatial_node_index = draw.spatial_node_index;

        quad_transform.set(
            spatial_node_index,
            pic_context.raster_spatial_node_index,
            frame_context.spatial_tree,
            device_pixel_scale,
        );

        if frame_state.surface_builder.get_cmd_buffer_targets_for_prim(
            draw,
            &mut cmd_buffer_targets,
        ) {
            let plane_split_anchor = PlaneSplitAnchor::new(
                spatial_node_index,
                draw_index,
            );

            prepare_prim_for_render(
                store,
                prim_instance_index,
                draw_index,
                spatial_node_index,
                &mut quad_transform,
                &mut quad_clips,
                pic_context,
                pic_state,
                frame_context,
                frame_state,
                plane_split_anchor,
                data_stores,
                scratch,
                tile_caches,
                prim_instances,
                &cmd_buffer_targets,
            );

            frame_state.num_visible_primitives += 1;
            frame_state.num_cmd_targets += cmd_buffer_targets.len() as u32;
        }

        // A draw that reached no command buffer is simply never referenced
        // by one, so there is nothing to clear here.
    }
}

/// Returns the texture sampler kind used by a YUV image's planes, which selects
/// the matching ps_quad_yuv shader variant. Texture-cache backed images
/// (raw/blob/buffer) are always Texture2D. Returns None if the planes do not all
/// share the same kind: every plane is sampled through the one sampler type of
/// the selected shader variant, so such an image cannot be drawn.
fn yuv_planes_sampler_kind(
    yuv_image_data: &crate::prim_store::image::YuvImageData,
    resource_cache: &crate::resource_cache::ResourceCache,
) -> Option<ImageBufferKind> {
    let plane_count = yuv_image_data.format.get_plane_num();
    let mut sampler_kind = None;
    for key in &yuv_image_data.yuv_key[.. plane_count] {
        let kind = match resource_cache.get_image_properties(*key).and_then(|props| props.external_image) {
            Some(ExternalImageData { image_type: ExternalImageType::TextureHandle(kind), .. }) => kind,
            _ => ImageBufferKind::Texture2D,
        };
        if *sampler_kind.get_or_insert(kind) != kind {
            return None;
        }
    }
    sampler_kind
}

fn prepare_prim_for_render(
    store: &mut PrimitiveStore,
    prim_instance_index: usize,
    draw_index: PrimitiveDrawIndex,
    prim_spatial_node_index: SpatialNodeIndex,
    mut quad_transform: &mut QuadTransformState,
    quad_clips: &mut QuadClipStack,
    pic_context: &PictureContext,
    pic_state: &mut PictureState,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    plane_split_anchor: PlaneSplitAnchor,
    data_stores: &DataStores,
    scratch: &mut PrimitiveScratchBuffer,
    tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
    prim_instances: &mut Vec<PrimitiveInstance>,
    targets: &[CommandBufferIndex],
) {
    tracy_rs::profile_scope!("prepare_prim_for_render");

    match frame_context.debug_override.highlight(prim_instance_index) {
        Some(HighlightMode::Replace) => {
            // Stand in for the primitive with a solid quad covering its
            // clipped local rect. Pictures are not prepared at all, so their
            // content is not rendered either.
            let prim_info = *scratch.frame.draw(draw_index);
            let coverage_rect = prim_info.clip_chain.local_coverage_rect;
            if coverage_rect.is_empty() {
                return;
            }

            frame_state.clip_store.fill_quad_clips(
                quad_clips,
                &prim_info.clip_chain,
                &frame_state.surfaces[pic_context.surface_index.0],
                &data_stores.clip,
            );

            quad::prepare_quad(
                &SceneDebugOverride::HIGHLIGHT_COLOR,
                &QuadDescriptor {
                    pattern_rect: coverage_rect,
                    bounds: coverage_rect,
                    aligned_aa_edges: EdgeMask::empty(),
                    transformed_aa_edges: EdgeMask::all(),
                },
                &None,
                quad_clips,
                quad_transform,
                frame_context.spatial_tree,
                targets,
                frame_state,
                scratch,
            );

            return;
        }
        Some(HighlightMode::Overlay) => {
            // Outline only, so the primitive's own content stays visible
            // underneath. Debug items are drawn directly onto the framebuffer,
            // so the rect is projected from the primitive's local space to
            // world space (the root node's space, in device pixels).
            let local_rect = scratch.frame.draw(draw_index).clip_chain.local_coverage_rect;
            let world_rect = frame_context
                .spatial_tree
                .get_world_transform(prim_spatial_node_index)
                .into_transform()
                .outer_transformed_box2d(&local_rect);

            if let Some(world_rect) = world_rect {
                scratch.push_debug_rect(
                    world_rect.cast_unit(),
                    2,
                    SceneDebugOverride::HIGHLIGHT_COLOR,
                    ColorF::TRANSPARENT,
                );
            }
        }
        None => {}
    }

    // If we have dependencies, we need to prepare them first, in order
    // to know the actual rect of this primitive.
    // For example, scrolling may affect the location of an item in
    // local space, which may force us to render this item on a larger
    // picture target, if being composited.
    let mut is_passthrough = false;
    if let PrimitiveKind::Picture { pic_index, .. } = prim_instances[prim_instance_index].kind {
        let Some(scratch_handle) = prepare_picture(
            pic_index,
            store,
            Some(pic_context.surface_index),
            pic_context.subpixel_mode,
            frame_context,
            frame_state,
            data_stores,
            scratch,
            tile_caches,
            prim_instances,
        ) else {
            return;
        };

        scratch.frame.draw_mut(draw_index).kind_scratch =
            KindScratchHandle::Picture(scratch_handle);

        is_passthrough = store
            .pictures[pic_index.0 as usize]
            .composite_mode
            .is_none();
    }

    let prim_instance = &mut prim_instances[prim_instance_index];
    if !is_passthrough {
        let should_update_clip_task = match &prim_instance.kind {
            PrimitiveKind::TextRun { .. } => true,
            _ => false,
        };

        if should_update_clip_task {
            let snapped_pattern_rect = scratch.frame.draw(draw_index).snapped_pattern_rect;
            let prim_rect = data_stores.get_local_prim_rect(
                prim_instance,
                snapped_pattern_rect,
                &store.pictures,
                frame_state.surfaces,
            );

            if !update_clip_task(
                draw_index,
                prim_rect,
                prim_spatial_node_index,
                pic_context.raster_spatial_node_index,
                pic_context,
                frame_context,
                frame_state,
                &data_stores.clip,
                scratch,
            ) {
                return;
            }
        }
    }

    let device_pixel_scale = frame_state.surfaces[pic_context.surface_index.0].device_pixel_scale;
    // Snapshot of the per-frame draw header for this prim. Copy is fine here
    // because the only field this function writes (clip_task_index, in the
    // segmented-clip path) isn't read again in this function — and the other
    // fields (state, clip_chain) aren't written by it.
    let prim_info = *scratch.frame.draw(draw_index);

    frame_state.clip_store.fill_quad_clips(
        quad_clips,
        &prim_info.clip_chain,
        &frame_state.surfaces[pic_context.surface_index.0],
        &data_stores.clip,
    );

    match &mut prim_instance.kind {
        PrimitiveKind::BoxShadow { data_handle, .. } => {
            tracy_rs::profile_scope!("BoxShadow");

            let prim_data = &data_stores.box_shadow[*data_handle];

            prepare_box_shadow(
                &prim_data.kind,
                &prim_data.common,
                &prim_data.common.prim_rect,
                quad_clips,
                &mut quad_transform,
                frame_context,
                pic_context,
                frame_state,
                scratch,
                prim_spatial_node_index,
                device_pixel_scale,
                targets,
            );

            return;
        }
        PrimitiveKind::LineDecoration { data_handle } => {
            tracy_rs::profile_scope!("LineDecoration");
            let prim_data = &data_stores.line_decoration[*data_handle];
            let line_dec_data = &prim_data.kind;

            let task = prim_data.kind.prepare(
                prim_info.snapped_pattern_rect.size(),
                quad_transform.scale_factors(),
                frame_state,
            );

            if let Some((src_task_id, stretch_size)) = task {
                let pattern = ImagePattern {
                    src_task_id,
                    src_is_opaque: false,
                    premultiplied: true,
                    sampler_kind: ImageBufferKind::Texture2D,
                    color: line_dec_data.color,
                };

                quad::prepare_repeatable_quad(
                    &pattern,
                    &QuadDescriptor {
                        pattern_rect: prim_info.snapped_pattern_rect,
                        bounds: prim_info.clip_chain.local_coverage_rect,
                        aligned_aa_edges: prim_data.common.aligned_aa_edges,
                        transformed_aa_edges: prim_data.common.transformed_aa_edges,
                    },
                    stretch_size,
                    LayoutSize::zero(),
                    &None,
                    quad_clips,
                    quad_transform,
                    frame_context.spatial_tree,
                    targets,
                    frame_state,
                    scratch,
                );
            } else {
                quad::prepare_quad(
                    &line_dec_data.color,
                    &QuadDescriptor {
                        pattern_rect: prim_info.snapped_pattern_rect,
                        bounds: prim_info.clip_chain.local_coverage_rect,
                        aligned_aa_edges: prim_data.common.aligned_aa_edges,
                        transformed_aa_edges: prim_data.common.transformed_aa_edges,
                    },
                    &None,
                    quad_clips,
                    quad_transform,
                    frame_context.spatial_tree,
                    targets,
                    frame_state,
                    scratch,
                );
            }

            return;
        }
        PrimitiveKind::TextRun { data_handle } => {
            tracy_rs::profile_scope!("TextRun");

            let prim_data = &data_stores.text_run[*data_handle];

            // The transform has to match the prim -> raster transform applied
            // by "ps_text_run" via `transform.m` + `device_pixel_scale`.
            // `request_resources` uses it to map glyph pen positions into
            // absolute device space for snapping.
            let transform = frame_context.spatial_tree
                .get_relative_transform(
                    prim_spatial_node_index,
                    pic_context.raster_spatial_node_index,
                )
                .into_fast_transform();

            // The run anchor is the normalized prim rect origin; glyph
            // positions in the template are stored relative to it. Use the
            // unsnapped rect so the anchor matches what the shader receives in
            // `PrimitiveHeader.local_rect`.
            let pattern_rect = prim_data.common.prim_rect;

            let surface = &frame_state.surfaces[pic_context.surface_index.0];

            // If subpixel AA is disabled due to the backing surface the glyphs
            // are being drawn onto, disable it (unless we are using the
            // specifial subpixel mode that estimates background color).
            let allow_subpixel = match prim_info.state {
                DrawState::Culled |
                DrawState::Unset |
                DrawState::PassThrough => {
                    panic!("bug: invalid visibility state");
                }
                DrawState::Visible { sub_slice_index, .. } => {
                    // For now, we only allow subpixel AA on primary sub-slices. In future we
                    // may support other sub-slices if we find content that does this.
                    if sub_slice_index.is_primary() {
                        match pic_context.subpixel_mode {
                            SubpixelMode::Allow => true,
                            SubpixelMode::Deny => false,
                            SubpixelMode::Conditional { allowed_rect, prohibited_rect } => {
                                // Conditional mode allows subpixel AA to be enabled for this
                                // text run, so long as it's inside the allowed rect.
                                allowed_rect.contains_box(&prim_info.clip_chain.pic_coverage_rect) &&
                                !prohibited_rect.intersects(&prim_info.clip_chain.pic_coverage_rect)
                            }
                        }
                    } else {
                        false
                    }
                }
            };

            let text_run_handle = prim_data.kind.request_resources(
                pattern_rect,
                prim_info.clip_chain.local_clip_rect,
                &transform.to_transform().with_destination::<_>(),
                surface,
                prim_spatial_node_index,
                allow_subpixel,
                frame_context.fb_config.low_quality_pinch_zoom,
                frame_state.resource_cache,
                &mut frame_state.frame_gpu_data.f32,
                frame_context.spatial_tree,
                scratch,
            );
            scratch.frame.draw_mut(draw_index).kind_scratch =
                KindScratchHandle::TextRun(text_run_handle);
        }
        PrimitiveKind::NormalBorder { data_handle } => {
            tracy_rs::profile_scope!("NormalBorder");
            let prim_data = &data_stores.normal_border[*data_handle];
            let aligned_aa_edges = prim_data.common.aligned_aa_edges;
            let transformed_aa_edges = prim_data.common.transformed_aa_edges;
            let border_data = &prim_data.kind;

            border_data.update(
                &QuadDescriptor {
                    pattern_rect: prim_info.snapped_pattern_rect,
                    bounds: prim_info.clip_chain.local_coverage_rect,
                    aligned_aa_edges,
                    transformed_aa_edges,
                },
                quad_clips,
                quad_transform,
                frame_context,
                targets,
                frame_state,
                scratch,
            );

            return;
        }
        PrimitiveKind::ImageBorder { data_handle, .. } => {
            tracy_rs::profile_scope!("ImageBorder");
            let prim_data = &data_stores.image_border[*data_handle];
            let aligned_aa_edges = prim_data.common.aligned_aa_edges;
            let transformed_aa_edges = prim_data.common.transformed_aa_edges;
            let border_data = &prim_data.kind;

            let (task_id, size, is_opaque) = border_data.update(frame_state);

            let prim_rect = prim_info.snapped_pattern_rect;

            let src_image = ImagePattern {
                src_task_id: task_id,
                src_is_opaque: is_opaque,
                premultiplied: true,
                sampler_kind: ImageBufferKind::Texture2D,
                color: ColorF::WHITE,
            };

            prepare_border_image_nine_patch(
                &border_data.nine_patch,
                &src_image,
                size,
                &QuadDescriptor {
                    pattern_rect: prim_rect,
                    bounds: prim_info.clip_chain.local_coverage_rect,
                    aligned_aa_edges,
                    transformed_aa_edges,
                },
                quad_clips,
                quad_transform,
                frame_context,
                targets,
                frame_state,
                scratch,
            );

            return;
        }
        PrimitiveKind::Rectangle { data_handle, .. } => {
            tracy_rs::profile_scope!("Rectangle");

            let prim_data = &data_stores.prim[*data_handle];
            let prim_rect = prim_info.snapped_pattern_rect;
            let color = prim_data.resolve(frame_context.scene_properties);

            quad::prepare_quad(
                &color,
                &QuadDescriptor {
                    pattern_rect: prim_rect,
                    bounds: prim_info.clip_chain.local_coverage_rect,
                    aligned_aa_edges: prim_data.common.aligned_aa_edges,
                    transformed_aa_edges: prim_data.common.transformed_aa_edges,
                },
                &None,
                quad_clips,
                quad_transform,
                frame_context.spatial_tree,
                targets,
                frame_state,
                scratch,
            );

            return;
        }
        PrimitiveKind::YuvImage { data_handle, .. } => {
            tracy_rs::profile_scope!("YuvImage");
            let prim_data = &data_stores.yuv_image[*data_handle];
            let common_data = &prim_data.common;
            let yuv_image_data = &prim_data.kind;

            if prim_info.compositor_surface_kind == CompositorSurfaceKind::Underlay {
                quad::prepare_quad(
                    &Cutout,
                    &QuadDescriptor {
                        pattern_rect: prim_info.snapped_pattern_rect,
                        bounds: prim_info.clip_chain.local_coverage_rect,
                        aligned_aa_edges: common_data.aligned_aa_edges,
                        transformed_aa_edges: common_data.transformed_aa_edges,
                    },
                    &None,
                    quad_clips,
                    quad_transform,
                    frame_context.spatial_tree,
                    targets,
                    frame_state,
                    scratch,
                );

                return;
            }

            let sampler_kind = match yuv_planes_sampler_kind(yuv_image_data, frame_state.resource_cache) {
                Some(sampler_kind) => sampler_kind,
                None => return,
            };

            // Non-composited: draw the YUV image directly through the quad path.
            let planes = yuv_image_data.update(
                prim_info.compositor_surface_kind.is_composited(),
                frame_state,
            );

            let pattern = YuvPattern {
                planes,
                format: yuv_image_data.format,
                color_space: yuv_image_data.color_space.with_range(yuv_image_data.color_range),
                channel_bit_depth: yuv_image_data.color_depth.bit_depth(),
                sampler_kind,
            };

            quad::prepare_quad(
                &pattern,
                &QuadDescriptor {
                    pattern_rect: prim_info.snapped_pattern_rect,
                    bounds: prim_info.clip_chain.local_coverage_rect,
                    aligned_aa_edges: common_data.aligned_aa_edges,
                    transformed_aa_edges: common_data.transformed_aa_edges,
                },
                &None,
                quad_clips,
                quad_transform,
                frame_context.spatial_tree,
                targets,
                frame_state,
                scratch,
            );

            return;
        }
        PrimitiveKind::Image { data_handle, .. } => {
            tracy_rs::profile_scope!("Image");

            let prim_data = &data_stores.image[*data_handle];
            let common_data = &prim_data.common;
            let image_data = &prim_data.kind;

            let prim_rect = prim_info.snapped_pattern_rect;

            if prim_info.compositor_surface_kind == CompositorSurfaceKind::Underlay {
                quad::prepare_quad(
                    &Cutout,
                    &QuadDescriptor {
                        pattern_rect: prim_rect,
                        bounds: prim_info.clip_chain.local_coverage_rect,
                        aligned_aa_edges: common_data.aligned_aa_edges,
                        transformed_aa_edges: common_data.transformed_aa_edges,
                    },
                    &None,
                    quad_clips,
                    quad_transform,
                    frame_context.spatial_tree,
                    targets,
                    frame_state,
                    scratch,
                );

                return;
            }

            crate::prim_store::image::prepare_image_quads(
                &prim_rect,
                common_data,
                image_data,
                &prim_info.clip_chain.local_coverage_rect,
                quad_clips,
                quad_transform,
                frame_context,
                targets,
                frame_state,
                scratch,
            );

            return;
        }
        PrimitiveKind::LinearGradient { data_handle, .. } => {
            tracy_rs::profile_scope!("LinearGradient");
            let prim_data = &data_stores.linear_grad[*data_handle];
            let prim_rect = prim_info.snapped_pattern_rect;
            let stretch_size = LayoutSize::new(
                prim_data.stretch_ratio.width * prim_rect.size().width,
                prim_data.stretch_ratio.height * prim_rect.size().height,
            );

            if let Some(nine_patch) = &prim_data.border_nine_patch {
                quad::prepare_border_nine_patch(
                    &*nine_patch,
                    prim_data,
                    &QuadDescriptor {
                        pattern_rect: prim_rect,
                        bounds: prim_info.clip_chain.local_coverage_rect,
                        aligned_aa_edges: prim_data.common.aligned_aa_edges,
                        transformed_aa_edges: prim_data.common.transformed_aa_edges,
                    },
                    stretch_size,
                    quad_clips,
                    quad_transform,
                    frame_context.spatial_tree,
                    targets,
                    frame_state,
                    scratch,
                );
                return;
            }

            // Fast-path: axis-aligned non-repeating gradients with multiple
            // stops decompose into per-segment two-stop quads so the GPU can
            // take the `sample_gradient_stops_fast` shader path. The
            // decomposition runs at frame-build (against the snapped prim
            // rect) so adjacent segments tile end-to-end at the snapped
            // outer-prim grid, even when the frame-time snap pass nudges
            // the outer rect at fractional DPR.
            //
            // `create_linear_gradient_prim` canonicalises the stored
            // start/end by swapping them when the original gradient line
            // ran "backwards" (and recording that in `reverse_stops`).
            // `LinearGradientTemplate::build` swaps them back at render
            // time; we have to do the same here so the decomposition sees
            // the gecko-original gradient orientation -- otherwise the
            // segment loop produces a gradient with stops in reverse
            // order (e.g. `linear-gradient(to top, red, blue)` rendering
            // as red-on-top instead of red-on-bottom).
            let (effective_start, effective_end) = if prim_data.reverse_stops {
                (prim_data.end_point, prim_data.start_point)
            } else {
                (prim_data.start_point, prim_data.end_point)
            };
            if linear_gradient_decomposes(
                &prim_rect,
                stretch_size,
                prim_data.tile_spacing,
                effective_start,
                effective_end,
                prim_data.extend_mode,
                &prim_data.stops,
                frame_context.fb_config.enable_dithering,
            ) {
                decompose_axis_aligned_gradient(
                    &prim_rect,
                    stretch_size,
                    effective_start,
                    effective_end,
                    &prim_data.stops,
                    &prim_info.clip_chain.local_clip_rect,
                    |seg_rect, seg_start, seg_end, seg_stops, edge_aa_mask| {
                        let pattern = LinearGradientSegmentPattern {
                            start: seg_start,
                            end: seg_end,
                            stops: seg_stops,
                        };
                        quad::prepare_quad(
                            &pattern,
                            &QuadDescriptor {
                                pattern_rect: *seg_rect,
                                bounds: *seg_rect,
                                aligned_aa_edges: EdgeMask::empty(),
                                transformed_aa_edges: edge_aa_mask,
                            },
                            &None,
                            quad_clips,
                            quad_transform,
                            frame_context.spatial_tree,
                            targets,
                            frame_state,
                            scratch,
                        );
                    },
                );
                return;
            }

            // For SWGL, evaluating the gradient is faster than reading from the texture cache.
            let mut should_cache = !frame_context.fb_config.is_software
                && frame_state.resource_cache.texture_cache.allocated_color_bytes() < 10_000_000;
            if should_cache {
                let surface = &frame_state.surfaces[pic_context.surface_index.0];
                let clipped_surface_rect = surface
                    .get_surface_rect(&prim_info.clip_chain.pic_coverage_rect);

                should_cache = if let Some(rect) = clipped_surface_rect {
                    rect.width() < 512 && rect.height() < 512
                } else {
                    false
                };
            }

            let cache_key = if should_cache {
                quad::cache_key(
                    data_handle.uid(),
                    quad_transform,
                    quad_clips,
                )
            } else {
                None
            };

            let pattern_rect = prim_info.snapped_pattern_rect;
            quad::prepare_repeatable_quad(
                prim_data,
                &QuadDescriptor {
                    pattern_rect,
                    bounds: prim_info.clip_chain.local_coverage_rect,
                    aligned_aa_edges: prim_data.common.aligned_aa_edges,
                    transformed_aa_edges: prim_data.common.transformed_aa_edges,
                },
                stretch_size,
                prim_data.tile_spacing,
                &cache_key,
                quad_clips,
                quad_transform,
                frame_context.spatial_tree,
                targets,
                frame_state,
                scratch,
            );

            return;
        }
        PrimitiveKind::RadialGradient { data_handle, .. } => {
            tracy_rs::profile_scope!("RadialGradient");
            let prim_data = &data_stores.radial_grad[*data_handle];
            let pattern_rect = prim_info.snapped_pattern_rect;
            let stretch_size = LayoutSize::new(
                prim_data.stretch_ratio.width * pattern_rect.size().width,
                prim_data.stretch_ratio.height * pattern_rect.size().height,
            );

            if let Some(nine_patch) = &prim_data.border_nine_patch {
                quad::prepare_border_nine_patch(
                    &*nine_patch,
                    prim_data,
                    &QuadDescriptor {
                        pattern_rect,
                        bounds: prim_info.clip_chain.local_coverage_rect,
                        aligned_aa_edges: prim_data.common.aligned_aa_edges,
                        transformed_aa_edges: prim_data.common.transformed_aa_edges,
                    },
                    stretch_size,
                    quad_clips,
                    quad_transform,
                    frame_context.spatial_tree,
                    targets,
                    frame_state,
                    scratch,
                );
                return;
            }

            quad::prepare_repeatable_quad(
                prim_data,
                &QuadDescriptor {
                    pattern_rect,
                    bounds: prim_info.clip_chain.local_coverage_rect,
                    aligned_aa_edges: prim_data.common.aligned_aa_edges,
                    transformed_aa_edges: prim_data.common.transformed_aa_edges,
                },
                stretch_size,
                prim_data.tile_spacing,
                &None,
                quad_clips,
                quad_transform,
                frame_context.spatial_tree,
                targets,
                frame_state,
                scratch,
            );
            return;
        }
        PrimitiveKind::ConicGradient { data_handle, .. } => {
            tracy_rs::profile_scope!("ConicGradient");
            let prim_data = &data_stores.conic_grad[*data_handle];
            let prim_rect = prim_info.snapped_pattern_rect;
            let stretch_size = LayoutSize::new(
                prim_data.stretch_ratio.width * prim_rect.size().width,
                prim_data.stretch_ratio.height * prim_rect.size().height,
            );

            if let Some(nine_patch) = &prim_data.border_nine_patch {
                quad::prepare_border_nine_patch(
                    &*nine_patch,
                    prim_data,
                    &QuadDescriptor {
                        pattern_rect: prim_rect,
                        bounds: prim_info.clip_chain.local_coverage_rect,
                        aligned_aa_edges: prim_data.common.aligned_aa_edges,
                        transformed_aa_edges: prim_data.common.transformed_aa_edges,
                    },
                    stretch_size,
                    quad_clips,
                    quad_transform,
                    frame_context.spatial_tree,
                    targets,
                    frame_state,
                    scratch,
                );
                return;
            }

            // Conic gradients are quite slow with SWGL, so we want to cache
            // them as much as we can, even large ones.
            // TODO: get_surface_rect is not always cheap. We should reorganize
            // the code so that we only call it as much as we really need it,
            // while avoiding this much boilerplate for each primitive that uses
            // caching.
            let mut should_cache = frame_context.fb_config.is_software
                && frame_state.resource_cache.texture_cache.allocated_color_bytes() < 30_000_000;
            if should_cache {
                let surface = &frame_state.surfaces[pic_context.surface_index.0];
                let clipped_surface_rect = surface
                    .get_surface_rect(&prim_info.clip_chain.pic_coverage_rect);

                should_cache = if let Some(rect) = clipped_surface_rect {
                    rect.width() < 4096 && rect.height() < 4096
                } else {
                    false
                };
            }

            let cache_key = if should_cache {
                quad::cache_key(
                    data_handle.uid(),
                    quad_transform,
                    quad_clips,
                )
            } else {
                None
            };

            let pattern_rect = prim_info.snapped_pattern_rect;
            quad::prepare_repeatable_quad(
                prim_data,
                &QuadDescriptor {
                    pattern_rect,
                    bounds: prim_info.clip_chain.local_coverage_rect,
                    aligned_aa_edges: prim_data.common.aligned_aa_edges,
                    transformed_aa_edges: prim_data.common.transformed_aa_edges,
                },
                stretch_size,
                prim_data.tile_spacing,
                &cache_key,
                quad_clips,
                quad_transform,
                frame_context.spatial_tree,
                targets,
                frame_state,
                scratch,
            );
            return;
        }
        PrimitiveKind::Picture { pic_index, .. } => {
            tracy_rs::profile_scope!("Picture");
            let pic_scratch_handle = prim_info.kind_scratch.unwrap_picture();
            let pic = &mut store.pictures[pic_index.0 as usize];

            let Some(raster_config) = &pic.raster_config else {
                return;
            };

            let clip_task_index = prepare_picture_primitive(
                pic,
                raster_config,
                prim_spatial_node_index,
                frame_context,
                frame_state,
                scratch,
                data_stores,
                pic_context,
                pic_scratch_handle,
                &prim_info,
                plane_split_anchor,
                &mut quad_transform,
                targets,
            );

            if let Some(clip_task_index) = clip_task_index {
                scratch.frame.draw_mut(draw_index).clip_task_index = clip_task_index;
            }

            return;
        }
        PrimitiveKind::BackdropCapture { .. } => {
            // Register the owner picture of this backdrop primitive as the
            // target for resolve of the sub-graph
            frame_state.surface_builder.register_resolve_source();

            if frame_context.debug_flags.contains(DebugFlags::HIGHLIGHT_BACKDROP_FILTERS) {
                if let Some(device_rect) = pic_state.map_pic_to_device.map(&prim_info.clip_chain.pic_coverage_rect) {
                    scratch.push_debug_rect(
                        device_rect,
                        2,
                        crate::debug_colors::MAGENTA,
                        ColorF::TRANSPARENT,
                    );
                }
            }
        }
        PrimitiveKind::BackdropRender { pic_index, data_handle, .. } => {
            match frame_state.surface_builder.sub_graph_output_map.get(pic_index).cloned() {
                Some(sub_graph_output_id) => {
                    frame_state.surface_builder.add_child_render_task(
                        sub_graph_output_id,
                        frame_state.rg_builder,
                    );

                    // Compute the four homogeneous screen-space uv corners that map
                    // the primitive rect into the captured backdrop. This mirrors the
                    // legacy brush path in batch.rs.
                    let pic_task = frame_state.rg_builder.get_task(sub_graph_output_id);
                    let uv_rect_kind = pic_task.uv_rect_kind();
                    let RenderTaskKind::Picture(info) = &pic_task.kind else {
                        unreachable!("bug: backdrop sub-graph output is not a picture");
                    };
                    // The shader maps the bilinearly-interpolated screen uv into the
                    // backdrop's texture-cache rect (the segment uv rect), which is
                    // resolved from the source task honoring its uv_rect_kind. When the
                    // backdrop surface is clipped (e.g. by the viewport), that uv rect
                    // is the projection of the *unclipped* surface rect. The screen uvs
                    // must therefore be normalized over the unclipped device rect so the
                    // two are consistent. We reconstruct the unclipped rect from the
                    // clipped rect (content_origin + target size) and the uv_rect_kind.
                    let clipped_origin = info.content_origin;
                    let clipped_size = pic_task.get_target_size().to_f32();
                    let backdrop_rect = match uv_rect_kind {
                        UvRectKind::Rect => {
                            DeviceRect::from_origin_and_size(clipped_origin, clipped_size)
                        }
                        UvRectKind::Quad { top_left, bottom_right, .. } => {
                            DeviceRect {
                                min: clipped_origin + DeviceVector2D::new(
                                    top_left.x * clipped_size.width,
                                    top_left.y * clipped_size.height,
                                ),
                                max: clipped_origin + DeviceVector2D::new(
                                    bottom_right.x * clipped_size.width,
                                    bottom_right.y * clipped_size.height,
                                ),
                            }
                        }
                    };
                    let device_pixel_scale = info.device_pixel_scale;

                    // `content_origin`/`backdrop_rect` are in the surface's raster
                    // device space (world space when the raster root is the scene
                    // root). Map the prim into that same raster space rather than the
                    // surface's own spatial node, otherwise the readback is offset by
                    // the surface node's origin whenever snapping promotes the raster
                    // root away from the surface node.
                    let raster_spatial_node_index = info.raster_spatial_node_index;

                    let map_prim_to_backdrop = SpaceMapper::new_with_target(
                        raster_spatial_node_index,
                        prim_spatial_node_index,
                        WorldRect::max_rect(),
                        frame_context.spatial_tree,
                    );

                    let prim_rect = prim_info.snapped_pattern_rect;
                    let points = [
                        map_prim_to_backdrop.map_point(prim_rect.top_left()),
                        map_prim_to_backdrop.map_point(prim_rect.top_right()),
                        map_prim_to_backdrop.map_point(prim_rect.bottom_left()),
                        map_prim_to_backdrop.map_point(prim_rect.bottom_right()),
                    ];

                    if points.iter().any(|p| p.is_none()) {
                        scratch.frame.draw_mut(draw_index).mark_culled();
                        return;
                    }

                    let uvs = [
                        calculate_screen_uv(points[0].unwrap() * device_pixel_scale, backdrop_rect),
                        calculate_screen_uv(points[1].unwrap() * device_pixel_scale, backdrop_rect),
                        calculate_screen_uv(points[2].unwrap() * device_pixel_scale, backdrop_rect),
                        calculate_screen_uv(points[3].unwrap() * device_pixel_scale, backdrop_rect),
                    ];

                    let prim_data = &data_stores.backdrop_render[*data_handle];
                    let aligned_aa_edges = prim_data.common.aligned_aa_edges;
                    let transformed_aa_edges = prim_data.common.transformed_aa_edges;

                    let pattern = BackdropPattern {
                        src_task_id: sub_graph_output_id,
                        uvs,
                    };

                    quad::prepare_quad(
                        &pattern,
                        &QuadDescriptor {
                            pattern_rect: prim_info.snapped_pattern_rect,
                            bounds: prim_info.clip_chain.local_coverage_rect,
                            aligned_aa_edges,
                            transformed_aa_edges,
                        },
                        &None,
                        quad_clips,
                        quad_transform,
                        frame_context.spatial_tree,
                        targets,
                        frame_state,
                        scratch,
                    );

                    return;
                }
                None => {
                    // Backdrop capture was found not visible, didn't produce a sub-graph
                    // so we can just skip drawing
                    scratch.frame.draw_mut(draw_index).mark_culled();
                }
            }
        }
    }

    match prim_info.state {
        DrawState::Unset => {
            panic!("bug: invalid vis state");
        }
        DrawState::Visible { .. } => {
            // The kinds that reach here (text runs, backdrop captures) have no
            // tighter footprint on hand than the primitive's coverage rect.
            let device_rect = frame_state.surfaces[pic_context.surface_index.0]
                .map_to_device_rect(&prim_info.clip_chain.pic_coverage_rect);

            frame_state.push_prim(
                &PrimitiveCommand::simple(draw_index, device_rect),
                prim_spatial_node_index,
                targets,
            );
        }
        DrawState::PassThrough | DrawState::Culled => {}
    }
}

/// Create a clip-mask render task by accumulating the clip chain into a blank
/// (white-cleared) alpha target as quad sub-tasks.
fn add_clip_mask_render_task(
    device_rect: DeviceIntRect,
    clip_node_range: ClipNodeRange,
    prim_local_rect: LayoutRect,
    prim_spatial_node_index: SpatialNodeIndex,
    raster_spatial_node_index: SpatialNodeIndex,
    device_pixel_scale: DevicePixelScale,
    clips: &ClipDataStore,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
) -> RenderTaskId {
    // A blank render task that just clears its target to white. The clip chain
    // is multiply-blended on top of it by the sub-tasks below.
    let clip_task_id = frame_state.rg_builder.add().init(RenderTask::new_dynamic(
        device_rect.size(),
        RenderTaskKind::Empty(EmptyTask {
            content_origin: device_rect.min.to_f32(),
            device_pixel_scale,
            raster_spatial_node_index,
        }),
    ));

    let task_rect = device_rect.to_f32();

    let mut quad_clips = QuadClipStack::new();
    frame_state.clip_store.fill_quad_clips_from_range(
        &mut quad_clips,
        clip_node_range,
        clips,
    );

    quad::prepare_clip_range(
        &quad_clips,
        clip_task_id,
        &task_rect,
        &prim_local_rect,
        prim_spatial_node_index,
        raster_spatial_node_index,
        device_pixel_scale,
        frame_context.spatial_tree,
        frame_state.rg_builder,
        &mut frame_state.frame_gpu_data.f32,
        frame_state.transforms,
    );

    clip_task_id
}

pub fn update_clip_task(
    draw_index: PrimitiveDrawIndex,
    prim_local_rect: LayoutRect,
    prim_spatial_node_index: SpatialNodeIndex,
    root_spatial_node_index: SpatialNodeIndex,
    pic_context: &PictureContext,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    clips: &ClipDataStore,
    scratch: &mut PrimitiveScratchBuffer,
) -> bool {
    let device_pixel_scale = frame_state.surfaces[pic_context.surface_index.0].device_pixel_scale;

    let new_clip_task_index = if scratch.frame.draw(draw_index).clip_chain.needs_mask {
        // Get a minimal device space rect, clipped to the screen that we
        // need to allocate for the clip mask, as well as interpolated
        // snap offsets.
        let unadjusted_device_rect = match frame_state.surfaces[pic_context.surface_index.0]
            .get_surface_rect(&scratch.frame.draw(draw_index).clip_chain.pic_coverage_rect)
        {
            Some(rect) => rect,
            None => return false,
        };

        let (device_rect, device_pixel_scale) = adjust_mask_scale_for_max_size(
            unadjusted_device_rect,
            device_pixel_scale,
        );

        if device_rect.size().to_i32().is_empty() {
            log::warn!("Bad adjusted clip task size {:?} (was {:?})", device_rect.size(), unadjusted_device_rect.size());
            return false;
        }

        let clip_task_id = add_clip_mask_render_task(
            device_rect,
            scratch.frame.draw(draw_index).clip_chain.clips_range,
            prim_local_rect,
            prim_spatial_node_index,
            root_spatial_node_index,
            device_pixel_scale,
            clips,
            frame_context,
            frame_state,
        );
        // Set the global clip mask instance for this primitive.
        let clip_task_index = ClipTaskIndex(scratch.frame.clip_mask_instances.len() as _);
        scratch.frame.clip_mask_instances.push(ClipMaskKind::Mask(clip_task_id));
        frame_state.surface_builder.add_child_render_task(
            clip_task_id,
            frame_state.rg_builder,
        );
        clip_task_index
    } else {
        ClipTaskIndex::INVALID
    };
    scratch.frame.draw_mut(draw_index).clip_task_index = new_clip_task_index;

    true
}

// Ensures that the size of mask render tasks are within MAX_MASK_SIZE.
fn adjust_mask_scale_for_max_size(device_rect: DeviceIntRect, device_pixel_scale: DevicePixelScale) -> (DeviceIntRect, DevicePixelScale) {
    if device_rect.width() > MAX_MASK_SIZE || device_rect.height() > MAX_MASK_SIZE {
        // round_out will grow by 1 integer pixel if origin is on a
        // fractional position, so keep that margin for error with -1:
        let device_rect_f = device_rect.to_f32();
        let scale = (MAX_MASK_SIZE - 1) as f32 /
            f32::max(device_rect_f.width(), device_rect_f.height());
        let new_device_pixel_scale = device_pixel_scale * Scale::new(scale);
        let new_device_rect = (device_rect_f * Scale::new(scale))
            .round_out()
            .to_i32();
        (new_device_rect, new_device_pixel_scale)
    } else {
        (device_rect, device_pixel_scale)
    }
}

/// Pattern builder for a single fast-path two-stop segment emitted by
/// `decompose_axis_aligned_gradient`. Holds the segment's gradient line and
/// stop colors (in segment-local coords); `build` translates start/end into
/// the prim's spatial-node space by adding `ctx.prim_origin`.
struct LinearGradientSegmentPattern {
    start: LayoutVector2D,
    end: LayoutVector2D,
    stops: [GradientStop; 2],
}

impl PatternBuilder for LinearGradientSegmentPattern {
    fn build(
        &self,
        pattern_rect: &LayoutRect,
        state: &mut PatternBuilderState,
    ) -> Pattern {
        linear_gradient_pattern(
            pattern_rect.min + self.start,
            pattern_rect.min + self.end,
            ExtendMode::Clamp,
            &self.stops,
            state.frame_gpu_data,
        )
    }
}
