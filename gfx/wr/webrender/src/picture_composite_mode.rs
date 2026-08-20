/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{ColorF, ColorU, PropertyBinding, PropertyBindingId, SnapshotInfo};
use api::units::*;
use crate::prim_store::image::AdjustedImageSource;
use crate::{render_task_graph::RenderTaskGraphBuilder, renderer::GpuBufferBuilderF};
use crate::box_shadow::BLUR_SAMPLE_SCALE;
use crate::frame_builder::{FrameBuildingContext, FrameBuildingState};
use crate::gpu_types::{BlurEdgeMode, UvRectKind};
use crate::intern::ItemUid;
use crate::render_backend::DataStores;
use crate::render_task_graph::RenderTaskId;
use crate::render_target::RenderTargetKind;
use crate::render_task::{BlurTask, RenderTask, BlurTaskCache};
use crate::render_task::RenderTaskKind;
use crate::renderer::{BlendMode, GpuBufferAddress, GpuBufferBuilder};
use crate::space::SpaceMapper;
use crate::spatial_tree::{SpatialTree, SpatialNodeIndex};
use crate::surface::{SurfaceDescriptor, SurfaceInfo, calculate_screen_uv};
use crate::surface::SurfaceIndex;
use crate::svg_filter::{get_coverage_source_svgfe, FilterGraphNodeKey, FilterGraphOpKey};
use crate::util::{MaxRect, ScaleOffset};
use smallvec::SmallVec;
use crate::internal_types::Filter;
use crate::profiler;
use core::time::Duration;
use euclid::Scale;
use api::MixBlendMode;
use crate::filterdata::FilterDataHandle;
use crate::tile_cache::SliceId;
use crate::svg_filter::{FilterGraphNode, FilterGraphOp, get_coverage_target_svgfe};
use crate::picture::BlitReason;
use crate::prim_store::VectorKey;
#[cfg(feature = "capture")]
use serde::Serialize;

/// Specifies how this Picture should be composited
/// onto the target it belongs to.
#[allow(dead_code)]
#[derive(Debug, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub enum PictureCompositeMode {
    /// Apply CSS mix-blend-mode effect.
    MixBlend(MixBlendMode),
    /// Apply a CSS filter (except component transfer).
    Filter(Filter),
    /// Apply a component transfer filter.
    ComponentTransferFilter(FilterDataHandle),
    /// Draw to intermediate surface, copy straight across. This
    /// is used for CSS isolation, and plane splitting.
    Blit(BlitReason),
    /// Used to cache a picture as a series of tiles.
    TileCache {
        slice_id: SliceId,
    },
    /// Apply an SVG filter graph. The `SpatialNodeIndex` is the spatial node
    /// the filter subregions are authored against (the filtered element's
    /// node). For a backdrop filter the graph is composited in backdrop-root
    /// space, so the subregions must be mapped from the source node into the
    /// surface's space at frame time. NOT included in the interned
    /// PictureCompositeKey (interning is unaffected).
    SVGFEGraph(Vec<(FilterGraphNode, FilterGraphOp)>, SpatialNodeIndex),
    /// A surface that is used as an input to another primitive
    IntermediateSurface,
}

impl PictureCompositeMode {
    pub fn get_rect(
        &self,
        surface: &SurfaceInfo,
        sub_rect: Option<LayoutRect>,
    ) -> LayoutRect {
        let surface_rect = match sub_rect {
            Some(sub_rect) => sub_rect,
            None => surface.clipped_local_rect.cast_unit(),
        };

        match self {
            PictureCompositeMode::Filter(Filter::Blur { width, height, should_inflate, .. }) => {
                if *should_inflate {
                    let (width_factor, height_factor) = surface.clamp_blur_radius(*width, *height);

                    surface_rect.inflate(
                        width_factor.ceil() * BLUR_SAMPLE_SCALE,
                        height_factor.ceil() * BLUR_SAMPLE_SCALE,
                    )
                } else {
                    surface_rect
                }
            }
            PictureCompositeMode::Filter(Filter::DropShadows(ref shadows)) => {
                let mut max_blur_radius = 0.0;
                for shadow in shadows {
                    max_blur_radius = f32::max(max_blur_radius, shadow.blur_radius);
                }

                let (max_blur_radius_x, max_blur_radius_y) = surface.clamp_blur_radius(
                    max_blur_radius,
                    max_blur_radius,
                );
                let blur_inflation_x = max_blur_radius_x * BLUR_SAMPLE_SCALE;
                let blur_inflation_y = max_blur_radius_y * BLUR_SAMPLE_SCALE;

                surface_rect.inflate(blur_inflation_x, blur_inflation_y)
            }
            PictureCompositeMode::SVGFEGraph(ref filters, _) => {
                // Return prim_subregion for use in get_local_prim_rect, which
                // is the polygon size.
                // This must match surface_rects.unclipped_local.
                let filters = map_svgfe_subregions(filters, &surface.svgfe_source_map);
                get_coverage_target_svgfe(&filters, surface_rect.cast_unit())
            }
            _ => {
                surface_rect
            }
        }
    }

    pub fn get_coverage(
        &self,
        surface: &SurfaceInfo,
        sub_rect: Option<LayoutRect>,
    ) -> LayoutRect {
        let surface_rect = match sub_rect {
            Some(sub_rect) => sub_rect,
            None => surface.clipped_local_rect.cast_unit(),
        };

        match self {
            PictureCompositeMode::Filter(Filter::Blur { width, height, should_inflate, .. }) => {
                if *should_inflate {
                    let (width_factor, height_factor) = surface.clamp_blur_radius(*width, *height);

                    surface_rect.inflate(
                        width_factor.ceil() * BLUR_SAMPLE_SCALE,
                        height_factor.ceil() * BLUR_SAMPLE_SCALE,
                    )
                } else {
                    surface_rect
                }
            }
            PictureCompositeMode::Filter(Filter::DropShadows(ref shadows)) => {
                let mut rect = surface_rect;

                for shadow in shadows {
                    let (blur_radius_x, blur_radius_y) = surface.clamp_blur_radius(
                        shadow.blur_radius,
                        shadow.blur_radius,
                    );
                    let blur_inflation_x = blur_radius_x * BLUR_SAMPLE_SCALE;
                    let blur_inflation_y = blur_radius_y * BLUR_SAMPLE_SCALE;

                    let shadow_rect = surface_rect
                        .translate(shadow.offset)
                        .inflate(blur_inflation_x, blur_inflation_y);
                    rect = rect.union(&shadow_rect);
                }

                rect
            }
            PictureCompositeMode::SVGFEGraph(ref filters, _) => {
                // surface_rect may be for source or target, so invalidate based
                // on both interpretations
                let filters = map_svgfe_subregions(filters, &surface.svgfe_source_map);
                let target_subregion = get_coverage_source_svgfe(&filters, surface_rect.cast());
                let source_subregion = get_coverage_target_svgfe(&filters, surface_rect.cast());
                target_subregion.union(&source_subregion)
            }
            _ => {
                surface_rect
            }
        }
    }

    pub fn write_gpu_blocks(
        &self,
        gpu_buffers: &mut GpuBufferBuilder,
        data_stores: &DataStores,
        extra_gpu_data: &mut SmallVec<[GpuBufferAddress; 1]>,
    ) {
        // TODO(gw): Almost all of the composite modes below use extra_gpu_data
        //           to store the same type of data. The exception is the filter
        //           with a ColorMatrix, which stores the color matrix here. It's
        //           probably worth tidying this code up to be a bit more consistent.
        //           Perhaps store the color matrix after the common data, even though
        //           it's not used by that shader.

        match *self {
            PictureCompositeMode::TileCache { .. } => {}
            PictureCompositeMode::Filter(Filter::Blur { .. }) => {}
            PictureCompositeMode::Filter(Filter::DropShadows(..)) => {}
            PictureCompositeMode::Filter(ref filter) => {
                match *filter {
                    Filter::ColorMatrix(ref m) => {
                        if extra_gpu_data.is_empty() {
                            extra_gpu_data.push(GpuBufferAddress::INVALID);
                        }
                        let mut writer = gpu_buffers.f32.write_blocks(5);
                        for i in 0..5 {
                            writer.push_one([m[i*4], m[i*4+1], m[i*4+2], m[i*4+3]]);
                        }
                        extra_gpu_data[0] = writer.finish();
                    }
                    Filter::Flood(ref color) => {
                        if extra_gpu_data.is_empty() {
                            extra_gpu_data.push(GpuBufferAddress::INVALID);
                        }
                        let mut writer = gpu_buffers.f32.write_blocks(1);
                        writer.push_one(color.to_array());
                        extra_gpu_data[0] = writer.finish();
                    }
                    _ => {}
                }
            }
            PictureCompositeMode::ComponentTransferFilter(handle) => {
                if extra_gpu_data.is_empty() {
                    extra_gpu_data.push(GpuBufferAddress::INVALID);
                }
                let filter_data = &data_stores.filter_data[handle];
                extra_gpu_data[0] = filter_data.data.write_gpu_blocks(&mut gpu_buffers.f32);
            }
            PictureCompositeMode::MixBlend(..) |
            PictureCompositeMode::Blit(_) |
            PictureCompositeMode::IntermediateSurface => {}
            PictureCompositeMode::SVGFEGraph(..) => {
                // SVGFE component-transfer filter data GPU blocks are written
                // per-node in RenderTask::new_svg_filter_graph, which is the
                // authoritative consumer of the resulting addresses.
            }
        }
    }

    /// Returns a static str describing the type of PictureCompositeMode (and
    /// filter type if applicable)
    pub fn kind(&self) -> &'static str {
        match *self {
            PictureCompositeMode::Blit(..) => "Blit",
            PictureCompositeMode::ComponentTransferFilter(..) => "ComponentTransferFilter",
            PictureCompositeMode::IntermediateSurface => "IntermediateSurface",
            PictureCompositeMode::MixBlend(..) => "MixBlend",
            PictureCompositeMode::SVGFEGraph(..) => "SVGFEGraph",
            PictureCompositeMode::TileCache{..} => "TileCache",
            PictureCompositeMode::Filter(Filter::Blur{..}) => "Filter::Blur",
            PictureCompositeMode::Filter(Filter::Brightness(..)) => "Filter::Brightness",
            PictureCompositeMode::Filter(Filter::ColorMatrix(..)) => "Filter::ColorMatrix",
            PictureCompositeMode::Filter(Filter::ComponentTransfer) => "Filter::ComponentTransfer",
            PictureCompositeMode::Filter(Filter::Contrast(..)) => "Filter::Contrast",
            PictureCompositeMode::Filter(Filter::DropShadows(..)) => "Filter::DropShadows",
            PictureCompositeMode::Filter(Filter::Flood(..)) => "Filter::Flood",
            PictureCompositeMode::Filter(Filter::Grayscale(..)) => "Filter::Grayscale",
            PictureCompositeMode::Filter(Filter::HueRotate(..)) => "Filter::HueRotate",
            PictureCompositeMode::Filter(Filter::Identity) => "Filter::Identity",
            PictureCompositeMode::Filter(Filter::Invert(..)) => "Filter::Invert",
            PictureCompositeMode::Filter(Filter::LinearToSrgb) => "Filter::LinearToSrgb",
            PictureCompositeMode::Filter(Filter::Opacity(..)) => "Filter::Opacity",
            PictureCompositeMode::Filter(Filter::Saturate(..)) => "Filter::Saturate",
            PictureCompositeMode::Filter(Filter::Sepia(..)) => "Filter::Sepia",
            PictureCompositeMode::Filter(Filter::SrgbToLinear) => "Filter::SrgbToLinear",
            PictureCompositeMode::Filter(Filter::SVGGraphNode(..)) => "Filter::SVGGraphNode",
        }
    }
}

pub fn prepare_composite_mode(
    composite_mode: &PictureCompositeMode,
    surface_index: SurfaceIndex,
    parent_surface_index: SurfaceIndex,
    surface_rects: &SurfaceAllocInfo,
    snapshot: &Option<SnapshotInfo>,
    can_use_shared_surface: bool,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    data_stores: &DataStores,
    extra_gpu_data: &mut SmallVec<[GpuBufferAddress; 1]>,
) -> (SurfaceDescriptor, [Option<RenderTaskId>; 2]) {
    let surface = &frame_state.surfaces[surface_index.0];
    let surface_spatial_node_index = surface.surface_spatial_node_index;
    let raster_spatial_node_index = surface.raster_spatial_node_index;
    let device_pixel_scale = surface.device_pixel_scale;

    let primary_render_task_id;
    let surface_descriptor;
    let mut secondary_render_task_id = None;
    match *composite_mode {
        PictureCompositeMode::TileCache { .. } => {
            unreachable!("handled above");
        }
        PictureCompositeMode::Filter(Filter::Blur { width, height, edge_mode, .. }) => {
            let (width, height) = surface.clamp_blur_radius(width, height);

            let width_std_deviation = width * surface.local_scale.0 * device_pixel_scale.0;
            let height_std_deviation = height * surface.local_scale.1 * device_pixel_scale.0;
            let blur_std_deviation = DeviceSize::new(
                width_std_deviation,
                height_std_deviation,
            );

            let original_size = surface_rects.clipped.size();

            let adjusted_size = BlurTask::adjusted_blur_source_size(
                original_size,
                blur_std_deviation,
            );

            let clear_color = if adjusted_size == original_size {
                None
            } else {
                Some(ColorF::TRANSPARENT)
            };

            let cmd_buffer_index = frame_state.cmd_buffers.create_cmd_buffer();
            let adjusted_size = adjusted_size.to_i32();

            let uv_rect_kind = calculate_uv_rect_kind(
                DeviceRect::from_origin_and_size(surface_rects.clipped.min, adjusted_size.to_f32()),
                surface_rects.unclipped,
            );

            let picture_task_id = frame_state.rg_builder.add().init(
                RenderTask::new_dynamic(
                    adjusted_size,
                    RenderTaskKind::new_picture(
                        adjusted_size,
                        surface_rects.needs_scissor_rect,
                        surface_rects.clipped.min,
                        surface_spatial_node_index,
                        raster_spatial_node_index,
                        device_pixel_scale,
                        None,
                        None,
                        clear_color,
                        cmd_buffer_index,
                        can_use_shared_surface,
                        Some(original_size.round().to_i32()),
                    )
                ).with_uv_rect_kind(uv_rect_kind)
            );


            let blur_render_task_id = request_render_task(
                frame_state,
                snapshot,
                &surface_rects,
                false,
                &mut|rg_builder, _| {
                    RenderTask::new_blur(
                        blur_std_deviation,
                        picture_task_id,
                        rg_builder,
                        RenderTargetKind::Color,
                        None,
                        original_size.to_i32(),
                        edge_mode,
                    )
                }
            );
            primary_render_task_id = blur_render_task_id;

            surface_descriptor = SurfaceDescriptor::new_chained(
                picture_task_id,
                blur_render_task_id,
                surface_rects.clipped_local,
            );
        }
        PictureCompositeMode::Filter(Filter::DropShadows(ref shadows)) => {
            let surface = &frame_state.surfaces[surface_index.0];

            let device_rect = surface_rects.clipped;

            let cmd_buffer_index = frame_state.cmd_buffers.create_cmd_buffer();

            let picture_task_id = frame_state.rg_builder.add().init(
                RenderTask::new_dynamic(
                    surface_rects.task_size,
                    RenderTaskKind::new_picture(
                        surface_rects.task_size,
                        surface_rects.needs_scissor_rect,
                        device_rect.min,
                        surface_spatial_node_index,
                        raster_spatial_node_index,
                        device_pixel_scale,
                        None,
                        None,
                        None,
                        cmd_buffer_index,
                        can_use_shared_surface,
                        None,
                    ),
                ).with_uv_rect_kind(surface_rects.uv_rect_kind)
            );

            let mut blur_tasks = BlurTaskCache::default();

            extra_gpu_data.resize(shadows.len(), GpuBufferAddress::INVALID);

            let mut blur_render_task_id = picture_task_id;
            for shadow in shadows {
                let (blur_radius_x, blur_radius_y) = surface.clamp_blur_radius(
                    shadow.blur_radius,
                    shadow.blur_radius,
                );

                blur_render_task_id = RenderTask::new_blur(
                    DeviceSize::new(
                        blur_radius_x * surface.local_scale.0 * device_pixel_scale.0,
                        blur_radius_y * surface.local_scale.1 * device_pixel_scale.0,
                    ),
                    picture_task_id,
                    frame_state.rg_builder,
                    RenderTargetKind::Color,
                    Some(&mut blur_tasks),
                    device_rect.size().to_i32(),
                    BlurEdgeMode::Duplicate,
                );
            }

            frame_state.surface_builder.add_picture_render_task(picture_task_id);

            primary_render_task_id = blur_render_task_id;
            secondary_render_task_id = Some(picture_task_id);

            surface_descriptor = SurfaceDescriptor::new_chained(
                picture_task_id,
                blur_render_task_id,
                surface_rects.clipped_local,
            );
        }
        PictureCompositeMode::MixBlend(mode) if BlendMode::from_mix_blend_mode(
            mode,
            frame_context.fb_config.gpu_supports_advanced_blend,
            frame_context.fb_config.advanced_blend_is_coherent,
        ).is_none() => {
            let parent_surface = &frame_state.surfaces[parent_surface_index.0];

            let map_pic_to_parent = SpaceMapper::new_with_target(
                parent_surface.surface_spatial_node_index,
                surface_spatial_node_index,
                parent_surface.clipping_rect,
                frame_context.spatial_tree,
            );
            let pic_rect = surface.clipped_local_rect;
            let pic_in_raster_space = map_pic_to_parent
                .map(&pic_rect)
                .expect("bug: unable to map mix-blend content into parent");

            let backdrop_rect = pic_in_raster_space;
            let parent_surface_rect = parent_surface.clipping_rect;

            let readback_task_id = match backdrop_rect.intersection(&parent_surface_rect) {
                Some(available_rect) => {
                    let backdrop_rect = parent_surface.map_to_device_rect(
                        &backdrop_rect,
                        frame_context.spatial_tree,
                    );

                    let available_rect = parent_surface.map_to_device_rect(
                        &available_rect,
                        frame_context.spatial_tree,
                    ).round_out();

                    let backdrop_uv = calculate_uv_rect_kind(
                        available_rect,
                        backdrop_rect,
                    );

                    frame_state.rg_builder.add().init(
                        RenderTask::new_dynamic(
                            available_rect.size().to_i32(),
                            RenderTaskKind::new_readback(Some(available_rect.min)),
                        ).with_uv_rect_kind(backdrop_uv)
                    )
                }
                None => {
                    frame_state.rg_builder.add().init(
                        RenderTask::new_dynamic(
                            DeviceIntSize::new(16, 16),
                            RenderTaskKind::new_readback(None),
                        )
                    )
                }
            };

            frame_state.surface_builder.add_child_render_task(
                readback_task_id,
                frame_state.rg_builder,
            );

            secondary_render_task_id = Some(readback_task_id);

            let task_size = surface_rects.clipped.size().to_i32();

            let cmd_buffer_index = frame_state.cmd_buffers.create_cmd_buffer();

            let is_opaque = false;
            let render_task_id = request_render_task(
                frame_state,
                &snapshot,
                &surface_rects,
                is_opaque,
                &mut|rg_builder, _| {
                    rg_builder.add().init(
                        RenderTask::new_dynamic(
                            task_size,
                            RenderTaskKind::new_picture(
                                task_size,
                                surface_rects.needs_scissor_rect,
                                surface_rects.clipped.min,
                                surface_spatial_node_index,
                                raster_spatial_node_index,
                                device_pixel_scale,
                                None,
                                None,
                                None,
                                cmd_buffer_index,
                                can_use_shared_surface,
                                None,
                            )
                        ).with_uv_rect_kind(surface_rects.uv_rect_kind)
                    )
                }
            );

            primary_render_task_id = render_task_id;

            surface_descriptor = SurfaceDescriptor::new_simple(
                render_task_id,
                surface_rects.clipped_local,
            );
        }
        PictureCompositeMode::Filter(..) => {
            let cmd_buffer_index = frame_state.cmd_buffers.create_cmd_buffer();

            let is_opaque = false;
            let render_task_id = request_render_task(
                frame_state,
                snapshot,
                &surface_rects,
                is_opaque,
                &mut|rg_builder, _| {
                    rg_builder.add().init(
                        RenderTask::new_dynamic(
                            surface_rects.task_size,
                            RenderTaskKind::new_picture(
                                surface_rects.task_size,
                                surface_rects.needs_scissor_rect,
                                surface_rects.clipped.min,
                                surface_spatial_node_index,
                                raster_spatial_node_index,
                                device_pixel_scale,
                                None,
                                None,
                                None,
                                cmd_buffer_index,
                                can_use_shared_surface,
                                None,
                            )
                        ).with_uv_rect_kind(surface_rects.uv_rect_kind)
                    )
                },
            );

            primary_render_task_id = render_task_id;

            surface_descriptor = SurfaceDescriptor::new_simple(
                render_task_id,
                surface_rects.clipped_local,
            );
        }
        PictureCompositeMode::ComponentTransferFilter(..) => {
            let cmd_buffer_index = frame_state.cmd_buffers.create_cmd_buffer();

            let is_opaque = false;
            let render_task_id = request_render_task(
                frame_state,
                snapshot,
                &surface_rects,
                is_opaque,
                &mut|rg_builder, _| {
                    rg_builder.add().init(
                        RenderTask::new_dynamic(
                            surface_rects.task_size,
                            RenderTaskKind::new_picture(
                                surface_rects.task_size,
                                surface_rects.needs_scissor_rect,
                                surface_rects.clipped.min,
                                surface_spatial_node_index,
                                raster_spatial_node_index,
                                device_pixel_scale,
                                None,
                                None,
                                None,
                                cmd_buffer_index,
                                can_use_shared_surface,
                                None,
                            )
                        ).with_uv_rect_kind(surface_rects.uv_rect_kind)
                    )
                }
            );

            primary_render_task_id = render_task_id;

            surface_descriptor = SurfaceDescriptor::new_simple(
                render_task_id,
                surface_rects.clipped_local,
            );
        }
        PictureCompositeMode::MixBlend(..) |
        PictureCompositeMode::Blit(_) => {
            let cmd_buffer_index = frame_state.cmd_buffers.create_cmd_buffer();

            let is_opaque = false;
            let render_task_id = request_render_task(
                frame_state,
                snapshot,
                &surface_rects,
                is_opaque,
                &mut|rg_builder, _| {
                    rg_builder.add().init(
                        RenderTask::new_dynamic(
                            surface_rects.task_size,
                            RenderTaskKind::new_picture(
                                surface_rects.task_size,
                                surface_rects.needs_scissor_rect,
                                surface_rects.clipped.min,
                                surface_spatial_node_index,
                                raster_spatial_node_index,
                                device_pixel_scale,
                                None,
                                None,
                                None,
                                cmd_buffer_index,
                                can_use_shared_surface,
                                None,
                            )
                        ).with_uv_rect_kind(surface_rects.uv_rect_kind)
                    )
                }
            );

            primary_render_task_id = render_task_id;

            surface_descriptor = SurfaceDescriptor::new_simple(
                render_task_id,
                surface_rects.clipped_local,
            );
        }
        PictureCompositeMode::IntermediateSurface => {
            let cmd_buffer_index = frame_state.cmd_buffers.create_cmd_buffer();

            let is_opaque = false;
            let render_task_id = request_render_task(
                frame_state,
                snapshot,
                &surface_rects,
                is_opaque,
                &mut|rg_builder, _| {
                    rg_builder.add().init(
                        RenderTask::new_dynamic(
                            surface_rects.task_size,
                            RenderTaskKind::new_picture(
                                surface_rects.task_size,
                                surface_rects.needs_scissor_rect,
                                surface_rects.clipped.min,
                                surface_spatial_node_index,
                                raster_spatial_node_index,
                                device_pixel_scale,
                                None,
                                None,
                                None,
                                cmd_buffer_index,
                                can_use_shared_surface,
                                None,
                            )
                        ).with_uv_rect_kind(surface_rects.uv_rect_kind)
                    )
                }
            );

            primary_render_task_id = render_task_id;

            surface_descriptor = SurfaceDescriptor::new_simple(
                render_task_id,
                surface_rects.clipped_local,
            );
        }
        PictureCompositeMode::SVGFEGraph(ref filters, _source_spatial_node_index) => {
            let cmd_buffer_index = frame_state.cmd_buffers.create_cmd_buffer();

            // Map the subregions into the surface's (backdrop-root) space to
            // match the coverage computed in get_surface_rects.
            let filters = map_svgfe_subregions(filters, &surface_rects.svgfe_source_map);
            let filters = &filters;

            let prim_subregion = surface_rects.unclipped;
            let target_subregion = surface_rects.clipped;
            let source_subregion = surface_rects.source;

            let source_task_size = source_subregion.round_out().size().to_i32();
            let source_task_size = if source_task_size.width > 0 && source_task_size.height > 0 {
                source_task_size
            } else {
                DeviceIntSize::new(1,1)
            };
            let picture_task_id = frame_state.rg_builder.add().init(
                RenderTask::new_dynamic(
                    source_task_size,
                    RenderTaskKind::new_picture(
                        source_task_size,
                        surface_rects.needs_scissor_rect,
                        source_subregion.min,
                        surface_spatial_node_index,
                        raster_spatial_node_index,
                        device_pixel_scale,
                        None,
                        None,
                        None,
                        cmd_buffer_index,
                        can_use_shared_surface,
                        None,
                    )
                )
            );

            let subregion_to_device_scale_x = surface_rects.clipped_notsnapped.width() / surface_rects.clipped_local.width();
            let subregion_to_device_scale_y = surface_rects.clipped_notsnapped.height() / surface_rects.clipped_local.height();
            let subregion_to_device_offset_x = surface_rects.clipped_notsnapped.min.x - (surface_rects.clipped_local.min.x * subregion_to_device_scale_x).floor();
            let subregion_to_device_offset_y = surface_rects.clipped_notsnapped.min.y - (surface_rects.clipped_local.min.y * subregion_to_device_scale_y).floor();

            let filter_task_id = request_render_task(
                frame_state,
                snapshot,
                &surface_rects,
                false,
                &mut|rg_builder, gpu_buffer| {
                    RenderTask::new_svg_filter_graph(
                        filters,
                        rg_builder,
                        gpu_buffer,
                        data_stores,
                        surface_rects.uv_rect_kind,
                        picture_task_id,
                        source_subregion.cast_unit(),
                        target_subregion.cast_unit(),
                        prim_subregion.cast_unit(),
                        subregion_to_device_scale_x,
                        subregion_to_device_scale_y,
                        subregion_to_device_offset_x,
                        subregion_to_device_offset_y,
                    )
                }
            );

            primary_render_task_id = filter_task_id;

            surface_descriptor = SurfaceDescriptor::new_chained(
                picture_task_id,
                filter_task_id,
                surface_rects.clipped_local,
            );
        }
    }

    (
        surface_descriptor,
        [
            Some(primary_render_task_id),
            secondary_render_task_id,
        ]
    )
}

fn request_render_task(
    frame_state: &mut FrameBuildingState,
    snapshot: &Option<SnapshotInfo>,
    surface_rects: &SurfaceAllocInfo,
    is_opaque: bool,
    f: &mut dyn FnMut(&mut RenderTaskGraphBuilder, &mut GpuBufferBuilderF) -> RenderTaskId,
) -> RenderTaskId {

    let task_id = match snapshot {
        Some(info) => {
            // `info.area` is the snapshot's (un-snapped) reference area, but the
            // image primitive this adjustment is later applied to has already
            // been snapped to the device pixel grid. Snap the reference to that
            // same grid first, so the adjustment maps snapped-reference ->
            // rasterized-area; otherwise it re-applies the (already-performed)
            // snap delta to the snapped prim, pushing the 1:1 snapshot paint off
            // the texture grid and blurring it. The surface's `clipped_local ->
            // clipped_notsnapped` rects give the local->device mapping.
            let cl = surface_rects.clipped_local;
            let cd = surface_rects.clipped_notsnapped;
            let reference = if cl.width() > 0.0 && cl.height() > 0.0 {
                let sx = cd.width() / cl.width();
                let sy = cd.height() / cl.height();
                let ox = cd.min.x - cl.min.x * sx;
                let oy = cd.min.y - cl.min.y * sy;
                let snap = |x: f32, y: f32| {
                    let dx = (x * sx + ox).round();
                    let dy = (y * sy + oy).round();
                    LayoutPoint::new((dx - ox) / sx, (dy - oy) / sy)
                };
                LayoutRect::new(
                    snap(info.area.min.x, info.area.min.y),
                    snap(info.area.max.x, info.area.max.y),
                )
            } else {
                info.area
            };
            let adjustment = AdjustedImageSource::from_rects(
                &reference,
                &surface_rects.clipped_local.cast_unit()
            );
            let task_id = frame_state.resource_cache.render_as_image(
                info.key.as_image(),
                surface_rects.task_size,
                frame_state.rg_builder,
                &mut frame_state.frame_gpu_data.f32,
                is_opaque,
                &adjustment,
                f
            );

            // TODO(bug 1929809): adding the dependency in the other branch causes
            // a panic in reftests/blend/backdrop-filter-blend-container.yaml.
            // Presumably if we use backdrop filters with snapshotting it will
            // trigger the panic as well.
            frame_state.surface_builder.add_child_render_task(
                task_id,
                frame_state.rg_builder,
            );

            frame_state.image_dependencies.insert(info.key.as_image(), task_id);

            task_id
        }
        None => {
            f(
                frame_state.rg_builder,
                &mut frame_state.frame_gpu_data.f32,
            )
        }
    };

    task_id
}

/// Information from `get_surface_rects` about the allocated size, UV sampling
/// parameters etc for an off-screen surface
#[derive(Debug)]
pub struct SurfaceAllocInfo {
    pub task_size: DeviceIntSize,
    pub needs_scissor_rect: bool,
    pub clipped: DeviceRect,
    pub unclipped: DeviceRect,
    // Only used for SVGFEGraph currently, this is the source pixels needed to
    // render the pixels in clipped.
    pub source: DeviceRect,
    // Only used for SVGFEGraph, this is the same as clipped before rounding.
    pub clipped_notsnapped: DeviceRect,
    pub clipped_local: PictureRect,
    pub uv_rect_kind: UvRectKind,
    // Only used for SVGFEGraph: mapping from the space the filter subregions
    // are authored in (the filtered element's spatial node) to the space the
    // graph is composited in (the surface's spatial node, which for a backdrop
    // filter is the backdrop root). Identity for regular filters.
    pub svgfe_source_map: ScaleOffset,
}

/// Map every absolute subregion in an SVGFE filter graph through `map`, moving
/// backdrop-filter subregions from the filtered element's space into the space
/// the graph composites in. This must be a full scale+offset rather than a
/// translation: a reference frame between the element and the backdrop root may
/// scale (pdf.js scales its text spans), and a translation alone then misplaces
/// the subregion. The per-input `offset`/`source_padding`/`target_padding`
/// fields are relative deltas and are left untouched. `map` is the identity for
/// regular (non-backdrop) filters, leaving the subregions unchanged.
fn map_svgfe_subregions(
    filters: &[(FilterGraphNode, FilterGraphOp)],
    map: &ScaleOffset,
) -> Vec<(FilterGraphNode, FilterGraphOp)> {
    filters.iter().map(|(node, op)| {
        let mut node = node.clone();
        node.subregion = map.map_rect(&node.subregion);
        for input in &mut node.inputs {
            input.subregion = map.map_rect(&input.subregion);
        }
        (node, op.clone())
    }).collect()
}

pub fn get_surface_rects(
    surface_index: SurfaceIndex,
    composite_mode: &PictureCompositeMode,
    parent_surface_index: SurfaceIndex,
    surfaces: &mut [SurfaceInfo],
    spatial_tree: &SpatialTree,
    max_surface_size: f32,
    force_scissor_rect: bool,
) -> Option<SurfaceAllocInfo> {
    let parent_surface = &surfaces[parent_surface_index.0];

    let local_to_parent = SpaceMapper::new_with_target(
        parent_surface.surface_spatial_node_index,
        surfaces[surface_index.0].surface_spatial_node_index,
        parent_surface.clipping_rect,
        spatial_tree,
    );

    let local_clip_rect = local_to_parent
        .unmap(&parent_surface.clipping_rect)
        .unwrap_or(PictureRect::max_rect())
        .cast_unit();

    let surface = &mut surfaces[surface_index.0];

    let mut svgfe_source_map = ScaleOffset::identity();

    let (clipped_local, unclipped_local, source_local) = match composite_mode {
        PictureCompositeMode::SVGFEGraph(ref filters, _) => {
            // The subregions are authored against the filtered element's
            // spatial node, but the graph is composited in this surface's space
            // (the backdrop root for a backdrop filter). Map them through the
            // relationship computed when the surface was assigned so they line
            // up with the captured source and the output prim.
            svgfe_source_map = surface.svgfe_source_map;
            let filters = map_svgfe_subregions(filters, &svgfe_source_map);
            let filters = &filters;

            let prim_subregion = get_coverage_target_svgfe(filters, surface.clipped_local_rect.cast_unit());

            let visible_subregion: LayoutRect =
                prim_subregion.cast_unit()
                .intersection(&local_clip_rect)
                .unwrap_or(PictureRect::zero())
                .cast_unit();

            if visible_subregion.is_empty() {
                return None;
            }

            let source_potential_subregion = get_coverage_source_svgfe(
                filters, visible_subregion.cast_unit());
            let source_subregion =
                source_potential_subregion
                .intersection(&surface.unclipped_local_rect.cast_unit())
                .unwrap_or(LayoutRect::zero());

            let coverage_subregion = source_subregion.union(&visible_subregion);

            (coverage_subregion.cast_unit(), prim_subregion.cast_unit(), source_subregion.cast_unit())
        }
        PictureCompositeMode::Filter(Filter::DropShadows(ref shadows)) => {
            let local_prim_rect = surface.clipped_local_rect;

            // Track max blur inflation across all shadows so the content-side
            // source region is inflated by blur margin (matching the default
            // branch's `get_rect(Some(sub_rect))` step for Blur). Without this,
            // the picture_task texture's edges land on image content and the
            // content quad's blur margin samples UVs > 1 → image edge bleed.
            let mut max_blur_inflation_x: f32 = 0.0;
            let mut max_blur_inflation_y: f32 = 0.0;
            for shadow in shadows {
                let (blur_radius_x, blur_radius_y) = surface.clamp_blur_radius(
                    shadow.blur_radius,
                    shadow.blur_radius,
                );
                max_blur_inflation_x = max_blur_inflation_x.max(blur_radius_x * BLUR_SAMPLE_SCALE);
                max_blur_inflation_y = max_blur_inflation_y.max(blur_radius_y * BLUR_SAMPLE_SCALE);
            }

            let mut required_local_rect = local_prim_rect
                .intersection(&local_clip_rect)
                .map(|r| r.inflate(max_blur_inflation_x, max_blur_inflation_y))
                .unwrap_or(PictureRect::zero());

            for shadow in shadows {
                let (blur_radius_x, blur_radius_y) = surface.clamp_blur_radius(
                    shadow.blur_radius,
                    shadow.blur_radius,
                );
                let blur_inflation_x = blur_radius_x * BLUR_SAMPLE_SCALE;
                let blur_inflation_y = blur_radius_y * BLUR_SAMPLE_SCALE;

                let local_shadow_rect = local_prim_rect
                    .translate(shadow.offset.cast_unit())
                    .inflate(blur_inflation_x, blur_inflation_y);

                if let Some(clipped_shadow_rect) = local_clip_rect.intersection(&local_shadow_rect) {
                    let required_shadow_rect = clipped_shadow_rect.inflate(blur_inflation_x, blur_inflation_y);

                    let local_clipped_shadow_rect = required_shadow_rect.translate(-shadow.offset.cast_unit());

                    required_local_rect = required_local_rect.union(&local_clipped_shadow_rect);
                }
            }

            let unclipped = composite_mode.get_rect(surface, None);
            let clipped = required_local_rect;

            let clipped = match clipped.intersection(&unclipped.cast_unit()) {
                Some(rect) => rect,
                None => return None,
            };

            (clipped, unclipped, clipped)
        }
        _ => {
            let surface_origin = surface.clipped_local_rect.min.to_vector().cast_unit();

            let normalized_prim_rect = composite_mode
                .get_rect(surface, None)
                .translate(-surface_origin);

            let normalized_clip_rect = local_clip_rect
                .cast_unit()
                .translate(-surface_origin);

            let norm_clipped_rect = match normalized_prim_rect.intersection(&normalized_clip_rect) {
                Some(rect) => rect,
                None => return None,
            };

            let norm_clipped_rect = composite_mode.get_rect(surface, Some(norm_clipped_rect));

            let norm_clipped_rect = match norm_clipped_rect.intersection(&normalized_prim_rect) {
                Some(rect) => rect,
                None => return None,
            };

            let unclipped = normalized_prim_rect.translate(surface_origin);
            let clipped = norm_clipped_rect.translate(surface_origin);

            (clipped.cast_unit(), unclipped.cast_unit(), clipped.cast_unit())
        }
    };

    let (mut clipped, mut unclipped, mut source) = if surface.raster_spatial_node_index != surface.surface_spatial_node_index {
        assert_eq!(surface.device_pixel_scale.0, 1.0);

        let local_to_world = SpaceMapper::new_with_target(
            spatial_tree.root_reference_frame_index(),
            surface.surface_spatial_node_index,
            WorldRect::max_rect(),
            spatial_tree,
        );

        let clipped = local_to_world.map(&clipped_local.cast_unit()).unwrap() * surface.device_pixel_scale;
        let unclipped = local_to_world.map(&unclipped_local).unwrap() * surface.device_pixel_scale;
        let source = local_to_world.map(&source_local.cast_unit()).unwrap() * surface.device_pixel_scale;

        (clipped, unclipped, source)
    } else {
        let clipped = clipped_local.cast_unit() * surface.device_pixel_scale;
        let unclipped = unclipped_local.cast_unit() * surface.device_pixel_scale;
        let source = source_local.cast_unit() * surface.device_pixel_scale;

        (clipped, unclipped, source)
    };
    let mut clipped_snapped = clipped.round_out();
    let mut source_snapped = source.round_out();

    let max_dimension =
        clipped_snapped.width().max(
            clipped_snapped.height().max(
                source_snapped.width().max(
                    source_snapped.height()
                ))).ceil();
    if max_dimension > max_surface_size {
        let max_dimension =
            clipped_local.width().max(
                clipped_local.height().max(
                    source_local.width().max(
                        source_local.height()
                    ))).ceil();
        surface.raster_spatial_node_index = surface.surface_spatial_node_index;
        surface.device_pixel_scale = Scale::new(max_surface_size / max_dimension);
        surface.local_scale = (1.0, 1.0);

        let add_markers = profiler::thread_is_being_profiled();
        if add_markers {
            let new_clipped = (clipped_local.cast_unit() * surface.device_pixel_scale).round();
            let new_source = (source_local.cast_unit() * surface.device_pixel_scale).round();
            profiler::add_text_marker("SurfaceSizeLimited",
                format!("Surface for {:?} reduced from raster {:?} (source {:?}) to local {:?} (source {:?})",
                    composite_mode.kind(),
                    clipped.size(), source.size(),
                    new_clipped, new_source).as_str(),
                Duration::from_secs_f32(new_clipped.width() * new_clipped.height() / 1000000000.0));
        }

        clipped = clipped_local.cast_unit() * surface.device_pixel_scale;
        unclipped = unclipped_local.cast_unit() * surface.device_pixel_scale;
        source = source_local.cast_unit() * surface.device_pixel_scale;
        clipped_snapped = clipped.round();
        source_snapped = source.round();
    }

    let task_size = clipped_snapped.size().to_i32();
    let task_size = task_size.min(DeviceIntSize::new(max_surface_size as i32, max_surface_size as i32));
    debug_assert!(
        task_size.width <= max_surface_size as i32 &&
        task_size.height <= max_surface_size as i32,
        "task_size {:?} for {:?} must be within max_surface_size {}",
        task_size,
        composite_mode.kind(),
        max_surface_size);

    let uv_rect_kind = calculate_uv_rect_kind(
        clipped_snapped,
        unclipped,
    );

    if task_size.width == 0 || task_size.height == 0 {
        return None;
    }

    let needs_scissor_rect = force_scissor_rect || !clipped_local.contains_box(&surface.unclipped_local_rect);

    Some(SurfaceAllocInfo {
        task_size,
        needs_scissor_rect,
        clipped: clipped_snapped,
        unclipped,
        source: source_snapped,
        clipped_notsnapped: clipped,
        clipped_local,
        uv_rect_kind,
        svgfe_source_map,
    })
}

pub fn calculate_uv_rect_kind(
    clipped: DeviceRect,
    unclipped: DeviceRect,
) -> UvRectKind {
    let top_left = calculate_screen_uv(
        unclipped.top_left().cast_unit(),
        clipped,
    );

    let top_right = calculate_screen_uv(
        unclipped.top_right().cast_unit(),
        clipped,
    );

    let bottom_left = calculate_screen_uv(
        unclipped.bottom_left().cast_unit(),
        clipped,
    );

    let bottom_right = calculate_screen_uv(
        unclipped.bottom_right().cast_unit(),
        clipped,
    );

    UvRectKind::Quad {
        top_left,
        top_right,
        bottom_left,
        bottom_right,
    }
}

/// Represents a hashable description of how a picture primitive
/// will be composited into its parent.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, Clone, MallocSizeOf, PartialEq, Hash, Eq)]
pub enum PictureCompositeKey {
    // No visual compositing effect
    Identity,

    // FilterOp
    Blur(Au, Au, bool, BlurEdgeMode),
    Brightness(Au),
    Contrast(Au),
    Grayscale(Au),
    HueRotate(Au),
    Invert(Au),
    Opacity(Au),
    OpacityBinding(PropertyBindingId, Au),
    Saturate(Au),
    Sepia(Au),
    DropShadows(Vec<(VectorKey, Au, ColorU)>),
    ColorMatrix([Au; 20]),
    SrgbToLinear,
    LinearToSrgb,
    ComponentTransfer(ItemUid),
    Flood(ColorU),
    SVGFEGraph(Vec<(FilterGraphNodeKey, FilterGraphOpKey)>),

    // MixBlendMode
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
    Hue,
    Saturation,
    Color,
    Luminosity,
    PlusLighter,
}

impl From<Option<PictureCompositeMode>> for PictureCompositeKey {
    fn from(mode: Option<PictureCompositeMode>) -> Self {
        match mode {
            Some(PictureCompositeMode::MixBlend(mode)) => {
                match mode {
                    MixBlendMode::Normal => PictureCompositeKey::Identity,
                    MixBlendMode::Multiply => PictureCompositeKey::Multiply,
                    MixBlendMode::Screen => PictureCompositeKey::Screen,
                    MixBlendMode::Overlay => PictureCompositeKey::Overlay,
                    MixBlendMode::Darken => PictureCompositeKey::Darken,
                    MixBlendMode::Lighten => PictureCompositeKey::Lighten,
                    MixBlendMode::ColorDodge => PictureCompositeKey::ColorDodge,
                    MixBlendMode::ColorBurn => PictureCompositeKey::ColorBurn,
                    MixBlendMode::HardLight => PictureCompositeKey::HardLight,
                    MixBlendMode::SoftLight => PictureCompositeKey::SoftLight,
                    MixBlendMode::Difference => PictureCompositeKey::Difference,
                    MixBlendMode::Exclusion => PictureCompositeKey::Exclusion,
                    MixBlendMode::Hue => PictureCompositeKey::Hue,
                    MixBlendMode::Saturation => PictureCompositeKey::Saturation,
                    MixBlendMode::Color => PictureCompositeKey::Color,
                    MixBlendMode::Luminosity => PictureCompositeKey::Luminosity,
                    MixBlendMode::PlusLighter => PictureCompositeKey::PlusLighter,
                }
            }
            Some(PictureCompositeMode::Filter(op)) => {
                match op {
                    Filter::Blur { width, height, should_inflate, edge_mode } => {
                        PictureCompositeKey::Blur(
                            Au::from_f32_px(width),
                            Au::from_f32_px(height),
                            should_inflate,
                            edge_mode,
                        )
                    }
                    Filter::Brightness(value) => PictureCompositeKey::Brightness(Au::from_f32_px(value)),
                    Filter::Contrast(value) => PictureCompositeKey::Contrast(Au::from_f32_px(value)),
                    Filter::Grayscale(value) => PictureCompositeKey::Grayscale(Au::from_f32_px(value)),
                    Filter::HueRotate(value) => PictureCompositeKey::HueRotate(Au::from_f32_px(value)),
                    Filter::Invert(value) => PictureCompositeKey::Invert(Au::from_f32_px(value)),
                    Filter::Saturate(value) => PictureCompositeKey::Saturate(Au::from_f32_px(value)),
                    Filter::Sepia(value) => PictureCompositeKey::Sepia(Au::from_f32_px(value)),
                    Filter::SrgbToLinear => PictureCompositeKey::SrgbToLinear,
                    Filter::LinearToSrgb => PictureCompositeKey::LinearToSrgb,
                    Filter::Identity => PictureCompositeKey::Identity,
                    Filter::DropShadows(ref shadows) => {
                        PictureCompositeKey::DropShadows(
                            shadows.iter().map(|shadow| {
                                (shadow.offset.into(), Au::from_f32_px(shadow.blur_radius), shadow.color.into())
                            }).collect()
                        )
                    }
                    Filter::Opacity(binding, _) => {
                        match binding {
                            PropertyBinding::Value(value) => {
                                PictureCompositeKey::Opacity(Au::from_f32_px(value))
                            }
                            PropertyBinding::Binding(key, default) => {
                                PictureCompositeKey::OpacityBinding(key.id, Au::from_f32_px(default))
                            }
                        }
                    }
                    Filter::ColorMatrix(values) => {
                        let mut quantized_values: [Au; 20] = [Au(0); 20];
                        for (value, result) in values.iter().zip(quantized_values.iter_mut()) {
                            *result = Au::from_f32_px(*value);
                        }
                        PictureCompositeKey::ColorMatrix(quantized_values)
                    }
                    Filter::ComponentTransfer => unreachable!(),
                    Filter::Flood(color) => PictureCompositeKey::Flood(color.into()),
                    Filter::SVGGraphNode(_node, _op) => unreachable!(),
                }
            }
            Some(PictureCompositeMode::ComponentTransferFilter(handle)) => {
                PictureCompositeKey::ComponentTransfer(handle.uid())
            }
            Some(PictureCompositeMode::SVGFEGraph(filter_nodes, _source_spatial_node)) => {
                // Deliberately exclude the source spatial node from the interned
                // key: it does not affect the filter graph identity.
                PictureCompositeKey::SVGFEGraph(
                    filter_nodes.into_iter().map(|(node, op)| {
                        (node.into(), op.into())
                    }).collect())
            }
            Some(PictureCompositeMode::Blit(_)) |
            Some(PictureCompositeMode::TileCache { .. }) |
            Some(PictureCompositeMode::IntermediateSurface) |
            None => {
                PictureCompositeKey::Identity
            }
        }
    }
}
