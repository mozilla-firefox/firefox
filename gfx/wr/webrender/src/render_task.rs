/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{BorderRadius, ClipMode, LineStyle, LineOrientation, ColorF, FilterOpGraphPictureBufferId};
use api::{MAX_RENDER_TASK_SIZE, SVGFE_GRAPH_MAX, FillRule, Path};
use api::units::*;
use std::time::Duration;
use crate::box_shadow::BLUR_SAMPLE_SCALE;
use crate::render_task_graph::SubTaskRange;
use crate::path_tiler::PathTransform;
use crate::command_buffer::{CommandBufferIndex, QuadFlags};
use crate::pattern::{PatternKind, PatternShaderInput};
use crate::profiler::{add_text_marker};
use crate::spatial_tree::SpatialNodeIndex;
use crate::gpu_types::{BorderInstance, UvRectKind, BlurEdgeMode, ClipSpace};
use crate::internal_types::{CacheTextureId, FastHashMap, TextureSource, Swizzle};
use crate::svg_filter::{FilterGraphNode, FilterGraphOp, FilterGraphPictureReference, SVGFE_CONVOLVE_VALUES_LIMIT};
use crate::picture::ResolvedSurfaceTexture;
use crate::tile_cache::MAX_SURFACE_SIZE;
use crate::transform::GpuTransformId;
use crate::resource_cache::ImageRequest;
use crate::renderer::{GpuBufferAddress, GpuBufferBuilder, GpuBufferBuilderF};
use crate::render_backend::DataStores;
use crate::render_target::RenderTargetKind;
use crate::render_task_graph::{PassId, RenderTaskId, RenderTaskGraphBuilder};
use crate::render_task_cache::RenderTaskCacheEntryHandle;
use crate::segment::EdgeMask;
use smallvec::SmallVec;

const FLOATS_PER_RENDER_TASK_INFO: usize = 8;
pub const MAX_BLUR_STD_DEVIATION: f32 = 4.0;
pub const MIN_DOWNSCALING_RT_SIZE: i32 = 8;

fn render_task_sanity_check(size: &DeviceIntSize) {
    if size.width > MAX_RENDER_TASK_SIZE ||
        size.height > MAX_RENDER_TASK_SIZE {
        error!("Attempting to create a render task of size {}x{}", size.width, size.height);
        panic!();
    }
}

#[derive(Copy, Clone, PartialEq)]
#[repr(C)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct RenderTaskAddress(pub i32);

impl std::fmt::Debug for RenderTaskAddress {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "#{}", self.0)
    }
}

impl Into<RenderTaskAddress> for RenderTaskId {
    fn into(self) -> RenderTaskAddress {
        RenderTaskAddress(self.index as i32)
    }
}

/// A render task location that targets a persistent output buffer which
/// will be retained over multiple frames.
#[derive(Clone, Debug, Eq, PartialEq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum StaticRenderTaskSurface {
    /// The output of the `RenderTask` will be persisted beyond this frame, and
    /// thus should be drawn into the `TextureCache`.
    TextureCache {
        /// Which texture in the texture cache should be drawn into.
        texture: CacheTextureId,
        /// What format this texture cache surface is
        target_kind: RenderTargetKind,
    },
    /// Only used as a source for render tasks, can be any texture including an
    /// external one.
    ReadOnly {
        source: TextureSource,
    },
    /// This render task will be drawn to a picture cache texture that is
    /// persisted between both frames and scenes, if the content remains valid.
    PictureCache {
        /// Describes either a WR texture or a native OS compositor target
        surface: ResolvedSurfaceTexture,
    },
}

/// Identifies the output buffer location for a given `RenderTask`.
#[derive(Clone, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum RenderTaskLocation {
    // Towards the beginning of the frame, most task locations are typically not
    // known yet, in which case they are set to one of the following variants:

    /// A dynamic task that has not yet been allocated a texture and rect.
    Unallocated {
        /// Requested size of this render task
        size: DeviceIntSize,
    },
    /// Will be replaced by a Static location after the texture cache update.
    CacheRequest {
        size: DeviceIntSize,
    },

    // Before batching begins, we expect that locations have been resolved to
    // one of the following variants:

    /// The `RenderTask` should be drawn to a target provided by the atlas
    /// allocator. This is the most common case.
    Dynamic {
        /// Texture that this task was allocated to render on
        texture_id: CacheTextureId,
        /// Rectangle in the texture this task occupies
        rect: DeviceIntRect,
    },
    /// A task that is output to a persistent / retained target.
    Static {
        /// Target to draw to
        surface: StaticRenderTaskSurface,
        /// Rectangle in the texture this task occupies
        rect: DeviceIntRect,
    },
}

impl RenderTaskLocation {
    /// Returns true if this is a dynamic location.
    pub fn is_dynamic(&self) -> bool {
        match *self {
            RenderTaskLocation::Dynamic { .. } => true,
            _ => false,
        }
    }

    pub fn size(&self) -> DeviceIntSize {
        match self {
            RenderTaskLocation::Unallocated { size } => *size,
            RenderTaskLocation::Dynamic { rect, .. } => rect.size(),
            RenderTaskLocation::Static { rect, .. } => rect.size(),
            RenderTaskLocation::CacheRequest { size } => *size,
        }
    }
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct CachedTask {
    pub target_kind: RenderTargetKind,
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ImageRequestTask {
    pub request: ImageRequest,
    pub is_composited: bool,
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipRegionTask {
    pub clip_rect: LayoutRect,
    pub radius: BorderRadius,
    pub inset: LayoutSideOffsets,
    pub mode: ClipMode,
    pub device_pixel_scale: DevicePixelScale,
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct EmptyTask {
    pub content_origin: DevicePoint,
    pub device_pixel_scale: DevicePixelScale,
    pub raster_spatial_node_index: SpatialNodeIndex,
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PrimTask {
    pub pattern: PatternKind,
    pub pattern_input: PatternShaderInput,
    pub content_origin: DevicePoint,
    pub prim_address_f: GpuBufferAddress,
    pub transform_id: GpuTransformId,
    pub edge_flags: EdgeMask,
    pub quad_flags: QuadFlags,
    pub prim_needs_scissor_rect: bool,
    pub texture_input: [RenderTaskId; 3],
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PictureTask {
    pub can_merge: bool,
    pub content_origin: DevicePoint,
    pub surface_spatial_node_index: SpatialNodeIndex,
    pub raster_spatial_node_index: SpatialNodeIndex,
    pub device_pixel_scale: DevicePixelScale,
    pub clear_color: Option<ColorF>,
    pub scissor_rect: Option<DeviceIntRect>,
    pub valid_rect: Option<DeviceIntRect>,
    pub cmd_buffer_index: CommandBufferIndex,
    pub content_size: DeviceIntSize,
    pub can_use_shared_surface: bool,
    /// For the task a backdrop is captured into: the color to clear its content
    /// area to. Drawing is scissored to the content area, and any padding added
    /// for a filter is cleared to transparent.
    pub capture_clear_color: Option<ColorF>,
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BlurTask {
    pub blur_std_deviation: f32,
    pub target_kind: RenderTargetKind,
    pub blur_region: DeviceIntSize,
    pub edge_mode: BlurEdgeMode,
}

impl BlurTask {
    // In order to do the blur down-scaling passes without introducing errors, we need the
    // source of each down-scale pass to be a multuple of two. If need be, this inflates
    // the source size so that each down-scale pass will sample correctly.
    /// The total downscaling factor that `RenderTask::new_blur` applies to a
    /// source of the given size before blurring it.
    pub fn downscale_factor(original_size: DeviceSize, mut std_dev: DeviceSize) -> f32 {
        let mut adjusted_size = original_size;
        let mut scale_factor = 1.0;
        while std_dev.width > MAX_BLUR_STD_DEVIATION && std_dev.height > MAX_BLUR_STD_DEVIATION {
            if adjusted_size.width < MIN_DOWNSCALING_RT_SIZE as f32 ||
               adjusted_size.height < MIN_DOWNSCALING_RT_SIZE as f32 {
                break;
            }
            std_dev = std_dev * 0.5;
            scale_factor *= 2.0;
            adjusted_size = (original_size.to_f32() / scale_factor).ceil();
        }

        scale_factor
    }

    pub fn adjusted_blur_source_size(original_size: DeviceSize, mut std_dev: DeviceSize) -> DeviceSize {
        let mut adjusted_size = original_size;
        let mut scale_factor = 1.0;
        while std_dev.width > MAX_BLUR_STD_DEVIATION && std_dev.height > MAX_BLUR_STD_DEVIATION {
            if adjusted_size.width < MIN_DOWNSCALING_RT_SIZE as f32 ||
               adjusted_size.height < MIN_DOWNSCALING_RT_SIZE as f32 {
                break;
            }
            std_dev = std_dev * 0.5;
            scale_factor *= 2.0;
            adjusted_size = (original_size.to_f32() / scale_factor).ceil();
        }

        (adjusted_size * scale_factor).round()
    }
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ScalingTask {
    pub target_kind: RenderTargetKind,
    pub padding: DeviceIntSideOffsets,
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BorderTask {
    pub instances: Vec<BorderInstance>,
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BlitTask {
    pub source: RenderTaskId,
    // Normalized rect within the source task to blit from
    pub source_rect: DeviceIntRect,
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct LineDecorationTask {
    pub wavy_line_thickness: f32,
    pub style: LineStyle,
    pub orientation: LineOrientation,
    pub local_size: LayoutSize,
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct SVGFEFilterTask {
    pub node: FilterGraphNode,
    pub op: FilterGraphOp,
    pub content_origin: DevicePoint,
    pub extra_gpu_data: Option<GpuBufferAddress>,
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ReadbackTask {
    // The offset of the rect that needs to be read back, in the
    // device space of the surface that will be read back from.
    // If this is None, there is no readback surface available
    // and this is a dummy (empty) readback.
    pub readback_origin: Option<DevicePoint>,
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct RenderTaskData {
    pub data: [f32; FLOATS_PER_RENDER_TASK_INFO],
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum RenderTaskKind {
    Image(ImageRequestTask),
    Cached(CachedTask),
    Picture(PictureTask),
    ClipRegion(ClipRegionTask),
    VerticalBlur(BlurTask),
    HorizontalBlur(BlurTask),
    Readback(ReadbackTask),
    Scaling(ScalingTask),
    Blit(BlitTask),
    Border(BorderTask),
    LineDecoration(LineDecorationTask),
    SVGFENode(SVGFEFilterTask),
    Prim(PrimTask),
    Empty(EmptyTask),
    #[cfg(test)]
    Test(RenderTargetKind),
}

impl RenderTaskKind {
    pub fn is_a_rendering_operation(&self) -> bool {
        match self {
            &RenderTaskKind::Image(..) => false,
            &RenderTaskKind::Cached(..) => false,
            _ => true,
        }
    }

    /// Whether this task can be allocated on a shared render target surface
    pub fn can_use_shared_surface(&self) -> bool {
        match self {
            &RenderTaskKind::Picture(ref info) => info.can_use_shared_surface,
            _ => true,
        }
    }

    pub fn should_advance_pass(&self) -> bool {
        match self {
            &RenderTaskKind::Image(..) => false,
            &RenderTaskKind::Cached(..) => false,
            _ => true,
        }
    }

    pub fn as_str(&self) -> &'static str {
        match *self {
            RenderTaskKind::Image(..) => "Image",
            RenderTaskKind::Cached(..) => "Cached",
            RenderTaskKind::Picture(..) => "Picture",
            RenderTaskKind::ClipRegion(..) => "ClipRegion",
            RenderTaskKind::VerticalBlur(..) => "VerticalBlur",
            RenderTaskKind::HorizontalBlur(..) => "HorizontalBlur",
            RenderTaskKind::Readback(..) => "Readback",
            RenderTaskKind::Scaling(..) => "Scaling",
            RenderTaskKind::Blit(..) => "Blit",
            RenderTaskKind::Border(..) => "Border",
            RenderTaskKind::LineDecoration(..) => "LineDecoration",
            RenderTaskKind::SVGFENode(..) => "SVGFENode",
            RenderTaskKind::Prim(..) => "Prim",
            RenderTaskKind::Empty(..) => "Empty",
            #[cfg(test)]
            RenderTaskKind::Test(..) => "Test",
        }
    }

    pub fn target_kind(&self) -> RenderTargetKind {
        match *self {
            RenderTaskKind::Image(..) |
            RenderTaskKind::LineDecoration(..) |
            RenderTaskKind::Readback(..) |
            RenderTaskKind::Border(..) |
            RenderTaskKind::Picture(..) |
            RenderTaskKind::Blit(..) |
            RenderTaskKind::Prim(..)  => {
                RenderTargetKind::Color
            }
            RenderTaskKind::SVGFENode(..) => {
                RenderTargetKind::Color
            }

            RenderTaskKind::ClipRegion(..) |
            RenderTaskKind::Empty(..) => {
                RenderTargetKind::Alpha
            }

            RenderTaskKind::VerticalBlur(ref task_info) |
            RenderTaskKind::HorizontalBlur(ref task_info) => {
                task_info.target_kind
            }

            RenderTaskKind::Scaling(ref task_info) => {
                task_info.target_kind
            }

            RenderTaskKind::Cached(ref task_info) => {
                task_info.target_kind
            }

            #[cfg(test)]
            RenderTaskKind::Test(kind) => kind,
        }
    }

    pub fn new_picture(
        size: DeviceIntSize,
        needs_scissor_rect: bool,
        content_origin: DevicePoint,
        surface_spatial_node_index: SpatialNodeIndex,
        raster_spatial_node_index: SpatialNodeIndex,
        device_pixel_scale: DevicePixelScale,
        scissor_rect: Option<DeviceIntRect>,
        valid_rect: Option<DeviceIntRect>,
        clear_color: Option<ColorF>,
        cmd_buffer_index: CommandBufferIndex,
        can_use_shared_surface: bool,
        content_size: Option<DeviceIntSize>,
    ) -> Self {
        render_task_sanity_check(&size);

        RenderTaskKind::Picture(PictureTask {
            content_origin,
            can_merge: !needs_scissor_rect,
            capture_clear_color: None,
            surface_spatial_node_index,
            raster_spatial_node_index,
            device_pixel_scale,
            scissor_rect,
            valid_rect,
            clear_color,
            cmd_buffer_index,
            can_use_shared_surface,
            content_size: content_size.unwrap_or(size),
        })
    }

    pub fn new_prim(
        pattern: PatternKind,
        pattern_input: PatternShaderInput,
        content_origin: DevicePoint,
        prim_address_f: GpuBufferAddress,
        transform_id: GpuTransformId,
        edge_flags: EdgeMask,
        quad_flags: QuadFlags,
        prim_needs_scissor_rect: bool,
        texture_input: [RenderTaskId; 3],
    ) -> Self {
        RenderTaskKind::Prim(PrimTask {
            pattern,
            pattern_input,
            content_origin,
            prim_address_f,
            transform_id,
            edge_flags,
            quad_flags,
            prim_needs_scissor_rect,
            texture_input,
        })
    }

    pub fn new_readback(
        readback_origin: Option<DevicePoint>,
    ) -> Self {
        RenderTaskKind::Readback(
            ReadbackTask {
                readback_origin,
            }
        )
    }

    pub fn new_line_decoration(
        style: LineStyle,
        orientation: LineOrientation,
        wavy_line_thickness: f32,
        local_size: LayoutSize,
    ) -> Self {
        RenderTaskKind::LineDecoration(LineDecorationTask {
            style,
            orientation,
            wavy_line_thickness,
            local_size,
        })
    }

    pub fn new_border_segment(
        instances: Vec<BorderInstance>,
    ) -> Self {
        RenderTaskKind::Border(BorderTask {
            instances,
        })
    }

    pub fn new_rounded_rect_mask(
        clip_rect: LayoutRect,
        radius: BorderRadius,
        inset: LayoutSideOffsets,
        mode: ClipMode,
        device_pixel_scale: DevicePixelScale,
    ) -> Self {
        RenderTaskKind::ClipRegion(ClipRegionTask {
            clip_rect,
            radius,
            inset,
            mode,
            device_pixel_scale,
        })
    }

    // Write (up to) 8 floats of data specific to the type
    // of render task that is provided to the GPU shaders
    // via a vertex texture.
    pub fn write_task_data(
        &self,
        target_rect: DeviceIntRect,
    ) -> RenderTaskData {
        // NOTE: The ordering and layout of these structures are required to
        //       match the GPU-side RenderTaskData declared in render_task.glsl,
        //       which is uploaded as `render_task_texture` (renderer/vertex.rs)
        //       and read through the sRenderTasks sampler.
        // TODO(gw): Maybe there's a way to make this stuff a bit
        //           more type-safe. Although, it will always need
        //           to be kept in sync with the GLSL code anyway.

        let data = match self {
            RenderTaskKind::Picture(ref task) => {
                // Note: has to match `PICTURE_TYPE_*` in shaders
                [
                    task.device_pixel_scale.0,
                    task.content_origin.x,
                    task.content_origin.y,
                    0.0,
                ]
            }
            RenderTaskKind::Prim(ref task) => {
                [
                    // NOTE: This must match the render task data format for Picture tasks currently
                    DevicePixelScale::identity().0,
                    task.content_origin.x,
                    task.content_origin.y,
                    0.0,
                ]
            }
            RenderTaskKind::Empty(ref task) => {
                [
                    // NOTE: This must match the render task data format for Picture tasks currently
                    task.device_pixel_scale.0,
                    task.content_origin.x,
                    task.content_origin.y,
                    0.0,
                ]
            }
            RenderTaskKind::ClipRegion(ref task) => {
                [
                    task.device_pixel_scale.0,
                    0.0,
                    0.0,
                    0.0,
                ]
            }
            RenderTaskKind::VerticalBlur(_) |
            RenderTaskKind::HorizontalBlur(_) => {
                // TODO(gw): Make this match Picture tasks so that we can draw
                //           sub-passes on them to apply box-shadow masks.
                [
                    0.0,
                    0.0,
                    0.0,
                    0.0,
                ]
            }
            RenderTaskKind::Image(..) |
            RenderTaskKind::Cached(..) |
            RenderTaskKind::Readback(..) |
            RenderTaskKind::Scaling(..) |
            RenderTaskKind::Border(..) |
            RenderTaskKind::LineDecoration(..) |
            RenderTaskKind::Blit(..) => {
                [0.0; 4]
            }

            RenderTaskKind::SVGFENode(_task) => {
                // we don't currently use this for SVGFE filters.
                // see SVGFEFilterInstance instead
                [0.0; 4]
            }

            #[cfg(test)]
            RenderTaskKind::Test(..) => {
                [0.0; 4]
            }
        };

        RenderTaskData {
            data: [
                target_rect.min.x as f32,
                target_rect.min.y as f32,
                target_rect.max.x as f32,
                target_rect.max.y as f32,
                data[0],
                data[1],
                data[2],
                data[3],
            ]
        }
    }

    pub fn write_gpu_blocks(
        &mut self,
        gpu_buffer: &mut GpuBufferBuilder,
    ) {
        match self {
            RenderTaskKind::SVGFENode(ref mut filter_task) => {
                match filter_task.op {
                    FilterGraphOp::SVGFEBlendDarken => {}
                    FilterGraphOp::SVGFEBlendLighten => {}
                    FilterGraphOp::SVGFEBlendMultiply => {}
                    FilterGraphOp::SVGFEBlendNormal => {}
                    FilterGraphOp::SVGFEBlendScreen => {}
                    FilterGraphOp::SVGFEBlendOverlay => {}
                    FilterGraphOp::SVGFEBlendColorDodge => {}
                    FilterGraphOp::SVGFEBlendColorBurn => {}
                    FilterGraphOp::SVGFEBlendHardLight => {}
                    FilterGraphOp::SVGFEBlendSoftLight => {}
                    FilterGraphOp::SVGFEBlendDifference => {}
                    FilterGraphOp::SVGFEBlendExclusion => {}
                    FilterGraphOp::SVGFEBlendHue => {}
                    FilterGraphOp::SVGFEBlendSaturation => {}
                    FilterGraphOp::SVGFEBlendColor => {}
                    FilterGraphOp::SVGFEBlendLuminosity => {}
                    FilterGraphOp::SVGFEColorMatrix { values: matrix } => {
                        let mut writer = gpu_buffer.f32.write_blocks(5);
                        for i in 0..5 {
                            writer.push_one([matrix[i*4], matrix[i*4+1], matrix[i*4+2], matrix[i*4+3]]);
                        }
                        filter_task.extra_gpu_data = Some(writer.finish());
                    }
                    FilterGraphOp::SVGFEComponentTransfer => unreachable!(),
                    FilterGraphOp::SVGFEComponentTransferInterned{..} => {}
                    FilterGraphOp::SVGFECompositeArithmetic{k1, k2, k3, k4} => {
                        let mut writer = gpu_buffer.f32.write_blocks(1);
                        writer.push_one([k1, k2, k3, k4]);
                        filter_task.extra_gpu_data = Some(writer.finish());
                    }
                    FilterGraphOp::SVGFECompositeATop => {}
                    FilterGraphOp::SVGFECompositeIn => {}
                    FilterGraphOp::SVGFECompositeLighter => {}
                    FilterGraphOp::SVGFECompositeOut => {}
                    FilterGraphOp::SVGFECompositeOver => {}
                    FilterGraphOp::SVGFECompositeXOR => {}
                    FilterGraphOp::SVGFEConvolveMatrixEdgeModeDuplicate{order_x, order_y, kernel, divisor, bias, target_x, target_y, kernel_unit_length_x, kernel_unit_length_y, preserve_alpha} |
                    FilterGraphOp::SVGFEConvolveMatrixEdgeModeNone{order_x, order_y, kernel, divisor, bias, target_x, target_y, kernel_unit_length_x, kernel_unit_length_y, preserve_alpha} |
                    FilterGraphOp::SVGFEConvolveMatrixEdgeModeWrap{order_x, order_y, kernel, divisor, bias, target_x, target_y, kernel_unit_length_x, kernel_unit_length_y, preserve_alpha} => {
                        let mut writer = gpu_buffer.f32.write_blocks(8);
                        assert!(SVGFE_CONVOLVE_VALUES_LIMIT == 25);
                        writer.push_one([-target_x as f32, -target_y as f32, order_x as f32, order_y as f32]);
                        writer.push_one([kernel_unit_length_x as f32, kernel_unit_length_y as f32, 1.0 / divisor, bias]);
                        writer.push_one([kernel[0], kernel[1], kernel[2], kernel[3]]);
                        writer.push_one([kernel[4], kernel[5], kernel[6], kernel[7]]);
                        writer.push_one([kernel[8], kernel[9], kernel[10], kernel[11]]);
                        writer.push_one([kernel[12], kernel[13], kernel[14], kernel[15]]);
                        writer.push_one([kernel[16], kernel[17], kernel[18], kernel[19]]);
                        writer.push_one([kernel[20], 0.0, 0.0, preserve_alpha as f32]);
                        filter_task.extra_gpu_data = Some(writer.finish());
                    }
                    FilterGraphOp::SVGFEDiffuseLightingDistant{..} => {}
                    FilterGraphOp::SVGFEDiffuseLightingPoint{..} => {}
                    FilterGraphOp::SVGFEDiffuseLightingSpot{..} => {}
                    FilterGraphOp::SVGFEDisplacementMap{scale_x, scale_y, x_channel_selector, y_channel_selector} => {
                        let mut writer = gpu_buffer.f32.write_blocks(1);
                        writer.push_one([x_channel_selector as f32, y_channel_selector as f32, scale_x, scale_y]);
                        filter_task.extra_gpu_data = Some(writer.finish());
                    }
                    FilterGraphOp::SVGFEDropShadow { color, .. } |
                    FilterGraphOp::SVGFEFlood { color } => {
                        let mut writer = gpu_buffer.f32.write_blocks(1);
                        writer.push_one(color.to_array());
                        filter_task.extra_gpu_data = Some(writer.finish());
                    }
                    FilterGraphOp::SVGFEGaussianBlur{..} => {}
                    FilterGraphOp::SVGFEIdentity => {}
                    FilterGraphOp::SVGFEImage {..} => {}
                    FilterGraphOp::SVGFEMorphologyDilate { radius_x, radius_y } |
                    FilterGraphOp::SVGFEMorphologyErode { radius_x, radius_y } => {
                        let mut writer = gpu_buffer.f32.write_blocks(1);
                        writer.push_one([radius_x, radius_y, 0.0, 0.0]);
                        filter_task.extra_gpu_data = Some(writer.finish());
                    }
                    FilterGraphOp::SVGFEOpacity{..} => {}
                    FilterGraphOp::SVGFESourceAlpha => {}
                    FilterGraphOp::SVGFESourceGraphic => {}
                    FilterGraphOp::SVGFESpecularLightingDistant{..} => {}
                    FilterGraphOp::SVGFESpecularLightingPoint{..} => {}
                    FilterGraphOp::SVGFESpecularLightingSpot{..} => {}
                    FilterGraphOp::SVGFETile => {}
                    FilterGraphOp::SVGFEToAlpha{..} => {}
                    FilterGraphOp::SVGFETurbulenceWithFractalNoiseWithNoStitching{..} => {}
                    FilterGraphOp::SVGFETurbulenceWithFractalNoiseWithStitching{..} => {}
                    FilterGraphOp::SVGFETurbulenceWithTurbulenceNoiseWithNoStitching{..} => {}
                    FilterGraphOp::SVGFETurbulenceWithTurbulenceNoiseWithStitching{..} => {}
                }
            }
            _ => {}
        }
    }
}

/// In order to avoid duplicating the down-scaling and blur passes when a picture has several blurs,
/// we use a local (primitive-level) cache of the render tasks generated for a single shadowed primitive
/// in a single frame.
pub type BlurTaskCache = FastHashMap<BlurTaskKey, RenderTaskId>;

/// Since we only use it within a single primitive, the key only needs to contain the down-scaling level
/// and the blur std deviation.
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub enum BlurTaskKey {
    DownScale(u32),
    Blur { downscale_level: u32, stddev_x: u32, stddev_y: u32 },
}

impl BlurTaskKey {
    fn downscale_and_blur(downscale_level: u32, blur_stddev: DeviceSize) -> Self {
        // Quantise the std deviations and store it as integers to work around
        // Eq and Hash's f32 allergy.
        // The blur radius is rounded before RenderTask::new_blur so we don't need
        // a lot of precision.
        const QUANTIZATION_FACTOR: f32 = 1024.0;
        let stddev_x = (blur_stddev.width * QUANTIZATION_FACTOR) as u32;
        let stddev_y = (blur_stddev.height * QUANTIZATION_FACTOR) as u32;
        BlurTaskKey::Blur { downscale_level, stddev_x, stddev_y }
    }
}

// The majority of render tasks have 0, 1 or 2 dependencies, except for pictures that
// typically have dozens to hundreds of dependencies. SmallVec with 2 inline elements
// avoids many tiny heap allocations in pages with a lot of text shadows and other
// types of render tasks.
pub type TaskDependencies = SmallVec<[RenderTaskId;2]>;

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct RenderTask {
    pub location: RenderTaskLocation,
    pub children: TaskDependencies,
    pub kind: RenderTaskKind,
    pub sub_tasks: SubTaskRange,

    // TODO(gw): These fields and perhaps others can become private once the
    //           frame_graph / render_task source files are unified / cleaned up.
    pub free_after: PassId,
    pub render_on: PassId,

    /// The gpu cache handle for the render task's destination rect.
    ///
    /// Will be set to None if the render task is cached, in which case the texture cache
    /// manages the handle.
    pub uv_rect_handle: GpuBufferAddress,
    pub cache_handle: Option<RenderTaskCacheEntryHandle>,
    pub uv_rect_kind: UvRectKind,
}

impl RenderTask {
    pub fn new(
        location: RenderTaskLocation,
        kind: RenderTaskKind,
    ) -> Self {
        render_task_sanity_check(&location.size());

        RenderTask {
            location,
            children: TaskDependencies::new(),
            kind,
            free_after: PassId::MAX,
            render_on: PassId::MIN,
            uv_rect_handle: GpuBufferAddress::INVALID,
            uv_rect_kind: UvRectKind::Rect,
            cache_handle: None,
            sub_tasks: SubTaskRange::empty(),
        }
    }

    pub fn new_dynamic(
        size: DeviceIntSize,
        kind: RenderTaskKind,
    ) -> Self {
        assert!(!size.is_empty(), "Bad {} render task size: {:?}", kind.as_str(), size);
        RenderTask::new(
            RenderTaskLocation::Unallocated { size },
            kind,
        )
    }

    pub fn with_uv_rect_kind(mut self, uv_rect_kind: UvRectKind) -> Self {
        self.uv_rect_kind = uv_rect_kind;
        self
    }

    pub fn new_image(
        size: DeviceIntSize,
        request: ImageRequest,
        is_composited: bool,
    ) -> Self {
        // Note: this is a special constructor for image render tasks that does not
        // do the render task size sanity check. This is because with SWGL we purposefully
        // avoid tiling large images. There is no upload with SWGL so whatever was
        // successfully allocated earlier will be what shaders read, regardless of the size
        // and copying into tiles would only slow things down.
        // As a result we can run into very large images being added to the frame graph
        // (this is covered by a few reftests on the CI).

        RenderTask {
            location: RenderTaskLocation::CacheRequest { size, },
            children: TaskDependencies::new(),
            kind: RenderTaskKind::Image(ImageRequestTask {
                request,
                is_composited,
            }),
            free_after: PassId::MAX,
            render_on: PassId::MIN,
            uv_rect_handle: GpuBufferAddress::INVALID,
            uv_rect_kind: UvRectKind::Rect,
            cache_handle: None,
            sub_tasks: SubTaskRange::empty(),
        }
    }


    #[cfg(test)]
    pub fn new_test(
        location: RenderTaskLocation,
        target: RenderTargetKind,
    ) -> Self {
        RenderTask {
            location,
            children: TaskDependencies::new(),
            kind: RenderTaskKind::Test(target),
            free_after: PassId::MAX,
            render_on: PassId::MIN,
            uv_rect_handle: GpuBufferAddress::INVALID,
            uv_rect_kind: UvRectKind::Rect,
            cache_handle: None,
            sub_tasks: SubTaskRange::empty(),
        }
    }

    pub fn new_blit(
        size: DeviceIntSize,
        source: RenderTaskId,
        source_rect: DeviceIntRect,
        rg_builder: &mut RenderTaskGraphBuilder,
    ) -> RenderTaskId {
        // If this blit uses a render task as a source,
        // ensure it's added as a child task. This will
        // ensure it gets allocated in the correct pass
        // and made available as an input when this task
        // executes.

        let blit_task_id = rg_builder.add().init(RenderTask::new_dynamic(
            size,
            RenderTaskKind::Blit(BlitTask { source, source_rect }),
        ));

        rg_builder.add_dependency(blit_task_id, source);

        blit_task_id
    }

    // Construct a render task to apply a blur to a primitive.
    // The render task chain that is constructed looks like:
    //
    //    PrimitiveCacheTask: Draw the primitives.
    //           ^
    //           |
    //    DownscalingTask(s): Each downscaling task reduces the size of render target to
    //           ^            half. Also reduce the std deviation to half until the std
    //           |            deviation less than 4.0.
    //           |
    //           |
    //    VerticalBlurTask: Apply the separable vertical blur to the primitive.
    //           ^
    //           |
    //    HorizontalBlurTask: Apply the separable horizontal blur to the vertical blur.
    //           |
    //           +---- This is stored as the input task to the primitive shader.
    //
    pub fn new_blur(
        blur_std_deviation: DeviceSize,
        src_task_id: RenderTaskId,
        rg_builder: &mut RenderTaskGraphBuilder,
        target_kind: RenderTargetKind,
        mut blur_cache: Option<&mut BlurTaskCache>,
        blur_region: DeviceIntSize,
        edge_mode: BlurEdgeMode,
    ) -> RenderTaskId {
        // Adjust large std deviation value.
        let mut adjusted_blur_std_deviation = blur_std_deviation;
        let (blur_target_size, uv_rect_kind) = {
            let src_task = rg_builder.get_task(src_task_id);
            (src_task.location.size(), src_task.uv_rect_kind())
        };
        let mut adjusted_blur_target_size = blur_target_size;
        let mut downscaling_src_task_id = src_task_id;
        let mut scale_factor = 1.0;
        let mut n_downscales = 1;
        while adjusted_blur_std_deviation.width > MAX_BLUR_STD_DEVIATION &&
              adjusted_blur_std_deviation.height > MAX_BLUR_STD_DEVIATION {
            if adjusted_blur_target_size.width < MIN_DOWNSCALING_RT_SIZE ||
               adjusted_blur_target_size.height < MIN_DOWNSCALING_RT_SIZE {
                break;
            }
            adjusted_blur_std_deviation = adjusted_blur_std_deviation * 0.5;
            scale_factor *= 2.0;
            adjusted_blur_target_size = (blur_target_size.to_f32() / scale_factor).to_i32();

            let cached_task = match blur_cache {
                Some(ref mut cache) => cache.get(&BlurTaskKey::DownScale(n_downscales)).cloned(),
                None => None,
            };

            downscaling_src_task_id = cached_task.unwrap_or_else(|| {
                RenderTask::new_scaling(
                    downscaling_src_task_id,
                    rg_builder,
                    target_kind,
                    adjusted_blur_target_size,
                )
            });

            if let Some(ref mut cache) = blur_cache {
                cache.insert(BlurTaskKey::DownScale(n_downscales), downscaling_src_task_id);
            }

            n_downscales += 1;
        }


        let blur_key = BlurTaskKey::downscale_and_blur(n_downscales, adjusted_blur_std_deviation);

        let cached_task = match blur_cache {
            Some(ref mut cache) => cache.get(&blur_key).cloned(),
            None => None,
        };

        let blur_region = blur_region / (scale_factor as i32);

        let blur_task_id = cached_task.unwrap_or_else(|| {
            let blur_task_v = rg_builder.add().init(RenderTask::new_dynamic(
                adjusted_blur_target_size,
                RenderTaskKind::VerticalBlur(BlurTask {
                    blur_std_deviation: adjusted_blur_std_deviation.height,
                    target_kind,
                    blur_region,
                    edge_mode,
                }),
            ).with_uv_rect_kind(uv_rect_kind));
            rg_builder.add_dependency(blur_task_v, downscaling_src_task_id);

            let task_id = rg_builder.add().init(RenderTask::new_dynamic(
                adjusted_blur_target_size,
                RenderTaskKind::HorizontalBlur(BlurTask {
                    blur_std_deviation: adjusted_blur_std_deviation.width,
                    target_kind,
                    blur_region,
                    edge_mode,
                }),
            ).with_uv_rect_kind(uv_rect_kind));
            rg_builder.add_dependency(task_id, blur_task_v);

            task_id
        });

        if let Some(ref mut cache) = blur_cache {
            cache.insert(blur_key, blur_task_id);
        }

        blur_task_id
    }

    pub fn new_scaling(
        src_task_id: RenderTaskId,
        rg_builder: &mut RenderTaskGraphBuilder,
        target_kind: RenderTargetKind,
        size: DeviceIntSize,
    ) -> RenderTaskId {
        Self::new_scaling_with_padding(
            src_task_id,
            rg_builder,
            target_kind,
            size,
            DeviceIntSideOffsets::zero(),
        )
    }

    pub fn new_scaling_with_padding(
        source: RenderTaskId,
        rg_builder: &mut RenderTaskGraphBuilder,
        target_kind: RenderTargetKind,
        padded_size: DeviceIntSize,
        padding: DeviceIntSideOffsets,
    ) -> RenderTaskId {
        let uv_rect_kind = rg_builder.get_task(source).uv_rect_kind();

        let task_id = rg_builder.add().init(
            RenderTask::new_dynamic(
                padded_size,
                RenderTaskKind::Scaling(ScalingTask {
                    target_kind,
                    padding,
                }),
            ).with_uv_rect_kind(uv_rect_kind)
        );

        rg_builder.add_dependency(task_id, source);

        task_id
    }

    pub fn set_sub_tasks(
        &mut self,
        sub_tasks: SubTaskRange,
    ) {
        assert!(self.sub_tasks.is_empty());
        self.sub_tasks = sub_tasks;
    }

    /// Creates render tasks from PictureCompositeMode::SVGFEGraph.
    ///
    /// The interesting parts of the handling of SVG filters are:
    /// * scene_building.rs : wrap_prim_with_filters
    /// * picture.rs : get_coverage_svgfe
    /// * render_task.rs : new_svg_filter_graph (you are here)
    /// * render_target.rs : add_svg_filter_node_instances
    pub fn new_svg_filter_graph(
        filter_nodes: &[(FilterGraphNode, FilterGraphOp)],
        rg_builder: &mut RenderTaskGraphBuilder,
        gpu_buffer: &mut GpuBufferBuilderF,
        data_stores: &DataStores,
        _uv_rect_kind: UvRectKind,
        original_task_id: RenderTaskId,
        source_subregion: LayoutRect,
        target_subregion: LayoutRect,
        prim_subregion: LayoutRect,
        subregion_to_device_scale_x: f32,
        subregion_to_device_scale_y: f32,
        subregion_to_device_offset_x: f32,
        subregion_to_device_offset_y: f32,
    ) -> RenderTaskId {
        const BUFFER_LIMIT: usize = SVGFE_GRAPH_MAX;
        let mut task_by_buffer_id: [RenderTaskId; BUFFER_LIMIT] = [RenderTaskId::INVALID; BUFFER_LIMIT];
        let mut subregion_by_buffer_id: [LayoutRect; BUFFER_LIMIT] = [LayoutRect::zero(); BUFFER_LIMIT];
        // If nothing replaces this value (all node subregions are empty), we
        // can just return the original picture
        let mut output_task_id = original_task_id;

        // By this point we assume the following about the graph:
        // * BUFFER_LIMIT here should be >= BUFFER_LIMIT in the scene_building.rs code.
        // * input buffer id < output buffer id
        // * output buffer id between 0 and BUFFER_LIMIT
        // * the number of filter_datas matches the number of kept nodes with op
        //   SVGFEComponentTransfer.
        //
        // These assumptions are verified with asserts in this function as
        // appropriate.

        // Make a UvRectKind::Quad that represents a task for a node, which may
        // have an inflate border, must be a Quad because the surface_rects
        // compositing shader expects it to be one, we don't actually use this
        // internally as we use subregions, see calculate_uv_rect_kind for how
        // this works, it projects from clipped rect to unclipped rect, where
        // our clipped rect is simply task_size minus the inflate, and unclipped
        // is our full task_size
        fn uv_rect_kind_for_task_size(clipped: DeviceRect, unclipped: DeviceRect) -> UvRectKind {
            let scale_x = 1.0 / clipped.width();
            let scale_y = 1.0 / clipped.height();
            UvRectKind::Quad{
                top_left: DeviceHomogeneousVector::new(
                    (unclipped.min.x - clipped.min.x) * scale_x,
                    (unclipped.min.y - clipped.min.y) * scale_y,
                    0.0, 1.0),
                top_right: DeviceHomogeneousVector::new(
                    (unclipped.max.x - clipped.min.x) * scale_x,
                    (unclipped.min.y - clipped.min.y) * scale_y,
                    0.0, 1.0),
                bottom_left: DeviceHomogeneousVector::new(
                    (unclipped.min.x - clipped.min.x) * scale_x,
                    (unclipped.max.y - clipped.min.y) * scale_y,
                    0.0, 1.0),
                bottom_right: DeviceHomogeneousVector::new(
                    (unclipped.max.x - clipped.min.x) * scale_x,
                    (unclipped.max.y - clipped.min.y) * scale_y,
                    0.0, 1.0),
            }
        }

        // Iterate the filter nodes and create tasks
        let mut made_dependency_on_source = false;
        for (filter_index, (filter_node, op)) in filter_nodes.iter().enumerate() {
            let node = &filter_node;
            let is_output = filter_index == filter_nodes.len() - 1;

            // Note that this is never set on the final output by design.
            if !node.kept_by_optimizer {
                continue;
            }

            // Certain ops have parameters that need to be scaled to device
            // space.
            let op = match op {
                FilterGraphOp::SVGFEBlendColor => op.clone(),
                FilterGraphOp::SVGFEBlendColorBurn => op.clone(),
                FilterGraphOp::SVGFEBlendColorDodge => op.clone(),
                FilterGraphOp::SVGFEBlendDarken => op.clone(),
                FilterGraphOp::SVGFEBlendDifference => op.clone(),
                FilterGraphOp::SVGFEBlendExclusion => op.clone(),
                FilterGraphOp::SVGFEBlendHardLight => op.clone(),
                FilterGraphOp::SVGFEBlendHue => op.clone(),
                FilterGraphOp::SVGFEBlendLighten => op.clone(),
                FilterGraphOp::SVGFEBlendLuminosity => op.clone(),
                FilterGraphOp::SVGFEBlendMultiply => op.clone(),
                FilterGraphOp::SVGFEBlendNormal => op.clone(),
                FilterGraphOp::SVGFEBlendOverlay => op.clone(),
                FilterGraphOp::SVGFEBlendSaturation => op.clone(),
                FilterGraphOp::SVGFEBlendScreen => op.clone(),
                FilterGraphOp::SVGFEBlendSoftLight => op.clone(),
                FilterGraphOp::SVGFEColorMatrix{..} => op.clone(),
                FilterGraphOp::SVGFEComponentTransfer => unreachable!(),
                FilterGraphOp::SVGFEComponentTransferInterned{..} => op.clone(),
                FilterGraphOp::SVGFECompositeArithmetic{..} => op.clone(),
                FilterGraphOp::SVGFECompositeATop => op.clone(),
                FilterGraphOp::SVGFECompositeIn => op.clone(),
                FilterGraphOp::SVGFECompositeLighter => op.clone(),
                FilterGraphOp::SVGFECompositeOut => op.clone(),
                FilterGraphOp::SVGFECompositeOver => op.clone(),
                FilterGraphOp::SVGFECompositeXOR => op.clone(),
                FilterGraphOp::SVGFEConvolveMatrixEdgeModeDuplicate{
                    kernel_unit_length_x, kernel_unit_length_y, order_x,
                    order_y, kernel, divisor, bias, target_x, target_y,
                    preserve_alpha} => {
                    FilterGraphOp::SVGFEConvolveMatrixEdgeModeDuplicate{
                        kernel_unit_length_x:
                            (kernel_unit_length_x * subregion_to_device_scale_x).round(),
                        kernel_unit_length_y:
                            (kernel_unit_length_y * subregion_to_device_scale_y).round(),
                        order_x: *order_x, order_y: *order_y, kernel: *kernel,
                        divisor: *divisor, bias: *bias, target_x: *target_x,
                        target_y: *target_y, preserve_alpha: *preserve_alpha}
                },
                FilterGraphOp::SVGFEConvolveMatrixEdgeModeNone{
                    kernel_unit_length_x, kernel_unit_length_y, order_x,
                    order_y, kernel, divisor, bias, target_x, target_y,
                    preserve_alpha} => {
                    FilterGraphOp::SVGFEConvolveMatrixEdgeModeNone{
                        kernel_unit_length_x:
                            (kernel_unit_length_x * subregion_to_device_scale_x).round(),
                        kernel_unit_length_y:
                            (kernel_unit_length_y * subregion_to_device_scale_y).round(),
                        order_x: *order_x, order_y: *order_y, kernel: *kernel,
                        divisor: *divisor, bias: *bias, target_x: *target_x,
                        target_y: *target_y, preserve_alpha: *preserve_alpha}
                },
                FilterGraphOp::SVGFEConvolveMatrixEdgeModeWrap{
                    kernel_unit_length_x, kernel_unit_length_y, order_x,
                    order_y, kernel, divisor, bias, target_x, target_y,
                    preserve_alpha} => {
                    FilterGraphOp::SVGFEConvolveMatrixEdgeModeWrap{
                        kernel_unit_length_x:
                            (kernel_unit_length_x * subregion_to_device_scale_x).round(),
                        kernel_unit_length_y:
                            (kernel_unit_length_y * subregion_to_device_scale_y).round(),
                        order_x: *order_x, order_y: *order_y, kernel: *kernel,
                        divisor: *divisor, bias: *bias, target_x: *target_x,
                        target_y: *target_y, preserve_alpha: *preserve_alpha}
                },
                FilterGraphOp::SVGFEDiffuseLightingDistant{
                    surface_scale, diffuse_constant, kernel_unit_length_x,
                    kernel_unit_length_y, azimuth, elevation} => {
                    FilterGraphOp::SVGFEDiffuseLightingDistant{
                        surface_scale: *surface_scale,
                        diffuse_constant: *diffuse_constant,
                        kernel_unit_length_x:
                            (kernel_unit_length_x * subregion_to_device_scale_x).round(),
                        kernel_unit_length_y:
                            (kernel_unit_length_y * subregion_to_device_scale_y).round(),
                        azimuth: *azimuth, elevation: *elevation}
                },
                FilterGraphOp::SVGFEDiffuseLightingPoint{
                    surface_scale, diffuse_constant, kernel_unit_length_x,
                    kernel_unit_length_y, x, y, z} => {
                    FilterGraphOp::SVGFEDiffuseLightingPoint{
                        surface_scale: *surface_scale,
                        diffuse_constant: *diffuse_constant,
                        kernel_unit_length_x:
                            (kernel_unit_length_x * subregion_to_device_scale_x).round(),
                        kernel_unit_length_y:
                            (kernel_unit_length_y * subregion_to_device_scale_y).round(),
                        x: x * subregion_to_device_scale_x + subregion_to_device_offset_x,
                        y: y * subregion_to_device_scale_y + subregion_to_device_offset_y,
                        z: *z}
                },
                FilterGraphOp::SVGFEDiffuseLightingSpot{
                    surface_scale, diffuse_constant, kernel_unit_length_x,
                    kernel_unit_length_y, x, y, z, points_at_x, points_at_y,
                    points_at_z, cone_exponent, limiting_cone_angle} => {
                    FilterGraphOp::SVGFEDiffuseLightingSpot{
                        surface_scale: *surface_scale,
                        diffuse_constant: *diffuse_constant,
                        kernel_unit_length_x:
                            (kernel_unit_length_x * subregion_to_device_scale_x).round(),
                        kernel_unit_length_y:
                            (kernel_unit_length_y * subregion_to_device_scale_y).round(),
                        x: x * subregion_to_device_scale_x + subregion_to_device_offset_x,
                        y: y * subregion_to_device_scale_y + subregion_to_device_offset_y,
                        z: *z,
                        points_at_x: points_at_x * subregion_to_device_scale_x + subregion_to_device_offset_x,
                        points_at_y: points_at_y * subregion_to_device_scale_y + subregion_to_device_offset_y,
                        points_at_z: *points_at_z,
                        cone_exponent: *cone_exponent,
                        limiting_cone_angle: *limiting_cone_angle}
                },
                FilterGraphOp::SVGFEFlood{..} => op.clone(),
                FilterGraphOp::SVGFEDisplacementMap{
                    scale_x, scale_y, x_channel_selector, y_channel_selector} => {
                    FilterGraphOp::SVGFEDisplacementMap{
                        scale_x: scale_x * subregion_to_device_scale_x,
                        scale_y: scale_y * subregion_to_device_scale_y,
                        x_channel_selector: *x_channel_selector,
                        y_channel_selector: *y_channel_selector}
                },
                FilterGraphOp::SVGFEDropShadow{
                    color, dx, dy, std_deviation_x, std_deviation_y} => {
                    FilterGraphOp::SVGFEDropShadow{
                        color: *color,
                        dx: dx * subregion_to_device_scale_x,
                        dy: dy * subregion_to_device_scale_y,
                        std_deviation_x: std_deviation_x * subregion_to_device_scale_x,
                        std_deviation_y: std_deviation_y * subregion_to_device_scale_y}
                },
                FilterGraphOp::SVGFEGaussianBlur{std_deviation_x, std_deviation_y} => {
                    let std_deviation_x = std_deviation_x * subregion_to_device_scale_x;
                    let std_deviation_y = std_deviation_y * subregion_to_device_scale_y;
                    // For blurs that effectively have no radius in display
                    // space, we can convert to identity.
                    if std_deviation_x + std_deviation_y >= 0.125 {
                        FilterGraphOp::SVGFEGaussianBlur{
                            std_deviation_x,
                            std_deviation_y}
                    } else {
                        FilterGraphOp::SVGFEIdentity
                    }
                },
                FilterGraphOp::SVGFEIdentity => op.clone(),
                FilterGraphOp::SVGFEImage{..} => op.clone(),
                FilterGraphOp::SVGFEMorphologyDilate{radius_x, radius_y} => {
                    FilterGraphOp::SVGFEMorphologyDilate{
                        radius_x: (radius_x * subregion_to_device_scale_x).round(),
                        radius_y: (radius_y * subregion_to_device_scale_y).round()}
                },
                FilterGraphOp::SVGFEMorphologyErode{radius_x, radius_y} => {
                    FilterGraphOp::SVGFEMorphologyErode{
                        radius_x: (radius_x * subregion_to_device_scale_x).round(),
                        radius_y: (radius_y * subregion_to_device_scale_y).round()}
                },
                FilterGraphOp::SVGFEOpacity{..} => op.clone(),
                FilterGraphOp::SVGFESourceAlpha => op.clone(),
                FilterGraphOp::SVGFESourceGraphic => op.clone(),
                FilterGraphOp::SVGFESpecularLightingDistant{
                    surface_scale, specular_constant, specular_exponent,
                    kernel_unit_length_x, kernel_unit_length_y, azimuth,
                    elevation} => {
                    FilterGraphOp::SVGFESpecularLightingDistant{
                        surface_scale: *surface_scale,
                        specular_constant: *specular_constant,
                        specular_exponent: *specular_exponent,
                        kernel_unit_length_x:
                            (kernel_unit_length_x * subregion_to_device_scale_x).round(),
                        kernel_unit_length_y:
                            (kernel_unit_length_y * subregion_to_device_scale_y).round(),
                        azimuth: *azimuth, elevation: *elevation}
                },
                FilterGraphOp::SVGFESpecularLightingPoint{
                    surface_scale, specular_constant, specular_exponent,
                    kernel_unit_length_x, kernel_unit_length_y, x, y, z } => {
                    FilterGraphOp::SVGFESpecularLightingPoint{
                        surface_scale: *surface_scale,
                        specular_constant: *specular_constant,
                        specular_exponent: *specular_exponent,
                        kernel_unit_length_x:
                            (kernel_unit_length_x * subregion_to_device_scale_x).round(),
                        kernel_unit_length_y:
                            (kernel_unit_length_y * subregion_to_device_scale_y).round(),
                        x: x * subregion_to_device_scale_x + subregion_to_device_offset_x,
                        y: y * subregion_to_device_scale_y + subregion_to_device_offset_y,
                        z: *z }
                },
                FilterGraphOp::SVGFESpecularLightingSpot{
                    surface_scale, specular_constant, specular_exponent,
                    kernel_unit_length_x, kernel_unit_length_y, x, y, z,
                    points_at_x, points_at_y, points_at_z, cone_exponent,
                    limiting_cone_angle} => {
                    FilterGraphOp::SVGFESpecularLightingSpot{
                        surface_scale: *surface_scale,
                        specular_constant: *specular_constant,
                        specular_exponent: *specular_exponent,
                        kernel_unit_length_x:
                            (kernel_unit_length_x * subregion_to_device_scale_x).round(),
                        kernel_unit_length_y:
                            (kernel_unit_length_y * subregion_to_device_scale_y).round(),
                        x: x * subregion_to_device_scale_x + subregion_to_device_offset_x,
                        y: y * subregion_to_device_scale_y + subregion_to_device_offset_y,
                        z: *z,
                        points_at_x: points_at_x * subregion_to_device_scale_x + subregion_to_device_offset_x,
                        points_at_y: points_at_y * subregion_to_device_scale_y + subregion_to_device_offset_y,
                        points_at_z: *points_at_z,
                        cone_exponent: *cone_exponent,
                        limiting_cone_angle: *limiting_cone_angle}
                },
                FilterGraphOp::SVGFETile => op.clone(),
                FilterGraphOp::SVGFEToAlpha => op.clone(),
                FilterGraphOp::SVGFETurbulenceWithFractalNoiseWithNoStitching{
                    base_frequency_x, base_frequency_y, num_octaves, seed} => {
                    FilterGraphOp::SVGFETurbulenceWithFractalNoiseWithNoStitching{
                        base_frequency_x:
                            base_frequency_x * subregion_to_device_scale_x,
                        base_frequency_y:
                            base_frequency_y * subregion_to_device_scale_y,
                        num_octaves: *num_octaves, seed: *seed}
                },
                FilterGraphOp::SVGFETurbulenceWithFractalNoiseWithStitching{
                    base_frequency_x, base_frequency_y, num_octaves, seed} => {
                    FilterGraphOp::SVGFETurbulenceWithFractalNoiseWithNoStitching{
                        base_frequency_x:
                            base_frequency_x * subregion_to_device_scale_x,
                        base_frequency_y:
                            base_frequency_y * subregion_to_device_scale_y,
                        num_octaves: *num_octaves, seed: *seed}
                },
                FilterGraphOp::SVGFETurbulenceWithTurbulenceNoiseWithNoStitching{
                    base_frequency_x, base_frequency_y, num_octaves, seed} => {
                    FilterGraphOp::SVGFETurbulenceWithFractalNoiseWithNoStitching{
                        base_frequency_x:
                            base_frequency_x * subregion_to_device_scale_x,
                        base_frequency_y:
                            base_frequency_y * subregion_to_device_scale_y,
                        num_octaves: *num_octaves, seed: *seed}
                },
                FilterGraphOp::SVGFETurbulenceWithTurbulenceNoiseWithStitching{
                    base_frequency_x, base_frequency_y, num_octaves, seed} => {
                    FilterGraphOp::SVGFETurbulenceWithFractalNoiseWithNoStitching{
                        base_frequency_x:
                            base_frequency_x * subregion_to_device_scale_x,
                        base_frequency_y:
                            base_frequency_y * subregion_to_device_scale_y,
                        num_octaves: *num_octaves, seed: *seed}
                },
            };

            // Process the inputs and figure out their new subregion, because
            // the SourceGraphic subregion is smaller than it was in scene build
            // now that it reflects the invalidation rect
            //
            // Also look up the child tasks while we are here.
            let mut used_subregion = LayoutRect::zero();
            let mut combined_input_subregion = LayoutRect::zero();
            let node_inputs: Vec<(FilterGraphPictureReference, RenderTaskId)> = node.inputs.iter().map(|input| {
                let (subregion, task) =
                    match input.buffer_id {
                        FilterOpGraphPictureBufferId::BufferId(id) => {
                            (subregion_by_buffer_id[id as usize], task_by_buffer_id[id as usize])
                        }
                        FilterOpGraphPictureBufferId::None => {
                            // Task must resolve so we use the SourceGraphic as
                            // a placeholder for these, they don't actually
                            // contribute anything to the output
                            (LayoutRect::zero(), original_task_id)
                        }
                    };
                // Convert offset to device coordinates.
                let offset = LayoutVector2D::new(
                        (input.offset.x * subregion_to_device_scale_x).round(),
                        (input.offset.y * subregion_to_device_scale_y).round(),
                    );
                // To figure out the portion of the node subregion used by this
                // source image we need to apply the target padding.  Note that
                // this does not affect the subregion of the input, as that
                // can't be modified as it is used for placement (offset).
                let target_padding = input.target_padding
                    .scale(subregion_to_device_scale_x, subregion_to_device_scale_y)
                    .round();
                let target_subregion =
                    LayoutRect::new(
                        LayoutPoint::new(
                            subregion.min.x + target_padding.min.x,
                            subregion.min.y + target_padding.min.y,
                        ),
                        LayoutPoint::new(
                            subregion.max.x + target_padding.max.x,
                            subregion.max.y + target_padding.max.y,
                        ),
                    );
                used_subregion = used_subregion.union(&target_subregion);
                combined_input_subregion = combined_input_subregion.union(&subregion);
                (FilterGraphPictureReference{
                    buffer_id: input.buffer_id,
                    // Apply offset to the placement of the input subregion.
                    subregion: subregion.translate(offset),
                    offset: LayoutVector2D::zero(),
                    inflate: input.inflate,
                    // Nothing past this point uses the padding.
                    source_padding: LayoutRect::zero(),
                    target_padding: LayoutRect::zero(),
                }, task)
            }).collect();

            // Convert subregion from PicturePixels to DevicePixels and round.
            let full_subregion = node.subregion
                .scale(subregion_to_device_scale_x, subregion_to_device_scale_y)
                .translate(LayoutVector2D::new(subregion_to_device_offset_x, subregion_to_device_offset_y))
                .round();

            // Clip the used subregion we calculated from the inputs to fit
            // within the node's specified subregion, but we want to keep a copy
            // of the combined input subregion for sizing tasks that involve
            // blurs as their intermediate stages will have to be downscaled if
            // very large, and we want that to be at the same alignment as the
            // node output itself.
            used_subregion = used_subregion
                .intersection(&full_subregion)
                .unwrap_or(LayoutRect::zero())
                .round();

            // Certain filters need to override the used_subregion directly.
            match op {
                FilterGraphOp::SVGFEBlendColor => {},
                FilterGraphOp::SVGFEBlendColorBurn => {},
                FilterGraphOp::SVGFEBlendColorDodge => {},
                FilterGraphOp::SVGFEBlendDarken => {},
                FilterGraphOp::SVGFEBlendDifference => {},
                FilterGraphOp::SVGFEBlendExclusion => {},
                FilterGraphOp::SVGFEBlendHardLight => {},
                FilterGraphOp::SVGFEBlendHue => {},
                FilterGraphOp::SVGFEBlendLighten => {},
                FilterGraphOp::SVGFEBlendLuminosity => {},
                FilterGraphOp::SVGFEBlendMultiply => {},
                FilterGraphOp::SVGFEBlendNormal => {},
                FilterGraphOp::SVGFEBlendOverlay => {},
                FilterGraphOp::SVGFEBlendSaturation => {},
                FilterGraphOp::SVGFEBlendScreen => {},
                FilterGraphOp::SVGFEBlendSoftLight => {},
                FilterGraphOp::SVGFEColorMatrix{values} => {
                    if values[19] > 0.0 {
                        // Manipulating alpha offset can easily create new
                        // pixels outside of input subregions
                        used_subregion = full_subregion;
                    }
                },
                FilterGraphOp::SVGFEComponentTransfer => unreachable!(),
                FilterGraphOp::SVGFEComponentTransferInterned{handle: _, creates_pixels} => {
                    // Check if the value of alpha[0] is modified, if so
                    // the whole subregion is used because it will be
                    // creating new pixels outside of input subregions
                    if creates_pixels {
                        used_subregion = full_subregion;
                    }
                },
                FilterGraphOp::SVGFECompositeArithmetic { k1, k2, k3, k4 } => {
                    // Optimize certain cases of Arithmetic operator
                    //
                    // See logic for SVG_FECOMPOSITE_OPERATOR_ARITHMETIC
                    // in FilterSupport.cpp for more information.
                    //
                    // Any other case uses the union of input subregions
                    if k4 > 0.0 {
                        // Can produce pixels anywhere in the subregion.
                        used_subregion = full_subregion;
                    } else  if k1 > 0.0 && k2 == 0.0 && k3 == 0.0 {
                        // Can produce pixels where both exist.
                        used_subregion = full_subregion
                            .intersection(&node_inputs[0].0.subregion)
                            .unwrap_or(LayoutRect::zero())
                            .intersection(&node_inputs[1].0.subregion)
                            .unwrap_or(LayoutRect::zero());
                    }
                    else if k2 > 0.0 && k3 == 0.0 {
                        // Can produce pixels where source exists.
                        used_subregion = full_subregion
                            .intersection(&node_inputs[0].0.subregion)
                            .unwrap_or(LayoutRect::zero());
                    }
                    else if k2 == 0.0 && k3 > 0.0 {
                        // Can produce pixels where background exists.
                        used_subregion = full_subregion
                            .intersection(&node_inputs[1].0.subregion)
                            .unwrap_or(LayoutRect::zero());
                    }
                },
                FilterGraphOp::SVGFECompositeATop => {
                    // Can only produce pixels where background exists.
                    used_subregion = full_subregion
                        .intersection(&node_inputs[1].0.subregion)
                        .unwrap_or(LayoutRect::zero());
                },
                FilterGraphOp::SVGFECompositeIn => {
                    // Can only produce pixels where both exist.
                    used_subregion = used_subregion
                        .intersection(&node_inputs[0].0.subregion)
                        .unwrap_or(LayoutRect::zero())
                        .intersection(&node_inputs[1].0.subregion)
                        .unwrap_or(LayoutRect::zero());
                },
                FilterGraphOp::SVGFECompositeLighter => {},
                FilterGraphOp::SVGFECompositeOut => {
                    // Can only produce pixels where source exists.
                    used_subregion = full_subregion
                        .intersection(&node_inputs[0].0.subregion)
                        .unwrap_or(LayoutRect::zero());
                },
                FilterGraphOp::SVGFECompositeOver => {},
                FilterGraphOp::SVGFECompositeXOR => {},
                FilterGraphOp::SVGFEConvolveMatrixEdgeModeDuplicate{..} => {},
                FilterGraphOp::SVGFEConvolveMatrixEdgeModeNone{..} => {},
                FilterGraphOp::SVGFEConvolveMatrixEdgeModeWrap{..} => {},
                FilterGraphOp::SVGFEDiffuseLightingDistant{..} => {},
                FilterGraphOp::SVGFEDiffuseLightingPoint{..} => {},
                FilterGraphOp::SVGFEDiffuseLightingSpot{..} => {},
                FilterGraphOp::SVGFEDisplacementMap{..} => {},
                FilterGraphOp::SVGFEDropShadow{..} => {},
                FilterGraphOp::SVGFEFlood { color } => {
                    // Subregion needs to be set to the full node
                    // subregion for fills (unless the fill is a no-op),
                    // we know at this point that it has no inputs, so the
                    // used_region is empty unless we set it here.
                    if color.a > 0.0 {
                        used_subregion = full_subregion;
                    }
                },
                FilterGraphOp::SVGFEIdentity => {},
                FilterGraphOp::SVGFEImage { sampling_filter: _sampling_filter, matrix: _matrix } => {
                    // TODO: calculate the actual subregion
                    used_subregion = full_subregion;
                },
                FilterGraphOp::SVGFEGaussianBlur{..} => {},
                FilterGraphOp::SVGFEMorphologyDilate{..} => {},
                FilterGraphOp::SVGFEMorphologyErode{..} => {},
                FilterGraphOp::SVGFEOpacity{valuebinding: _valuebinding, value} => {
                    // If fully transparent, we can ignore this node
                    if value <= 0.0 {
                        used_subregion = LayoutRect::zero();
                    }
                },
                FilterGraphOp::SVGFESourceAlpha |
                FilterGraphOp::SVGFESourceGraphic => {
                    used_subregion = source_subregion
                        .intersection(&full_subregion)
                        .unwrap_or(LayoutRect::zero());
                },
                FilterGraphOp::SVGFESpecularLightingDistant{..} => {},
                FilterGraphOp::SVGFESpecularLightingPoint{..} => {},
                FilterGraphOp::SVGFESpecularLightingSpot{..} => {},
                FilterGraphOp::SVGFETile => {
                    if !used_subregion.is_empty() {
                        // This fills the entire target, at least if there are
                        // any input pixels to work with.
                        used_subregion = full_subregion;
                    }
                },
                FilterGraphOp::SVGFEToAlpha => {},
                FilterGraphOp::SVGFETurbulenceWithFractalNoiseWithNoStitching{..} |
                FilterGraphOp::SVGFETurbulenceWithFractalNoiseWithStitching{..} |
                FilterGraphOp::SVGFETurbulenceWithTurbulenceNoiseWithNoStitching{..} |
                FilterGraphOp::SVGFETurbulenceWithTurbulenceNoiseWithStitching{..} => {
                    // Turbulence produces pixel values throughout the
                    // node subregion.
                    used_subregion = full_subregion;
                },
            }

            add_text_marker(
                "SVGFEGraph",
                &format!("{}({})", op.kind(), filter_index),
                Duration::from_micros((used_subregion.width() * used_subregion.height() / 1000.0) as u64),
            );

            // SVG spec requires that a later node sampling pixels outside
            // this node's subregion will receive a transparent black color
            // for those samples, we achieve this by adding a 1 pixel inflate
            // around the target rect, which works fine with the
            // edgemode=duplicate behavior of the texture fetch in the shader,
            // all of the out of bounds reads are transparent black.
            //
            // If this is the output node, we don't apply the inflate, knowing
            // that the pixels outside of the invalidation rect will not be used
            // so it is okay if they duplicate outside the view.
            let mut node_inflate = node.inflate;
            if is_output {
                // Use the provided target subregion (invalidation rect)
                used_subregion = target_subregion;
                node_inflate = 0;
            }

            // We can't render tasks larger than a certain size, if this node
            // is too large (particularly with blur padding), we need to render
            // at a reduced resolution, later nodes can still be full resolution
            // but for example blurs are not significantly harmed by reduced
            // resolution in most cases.
            let mut device_to_render_scale = 1.0;
            let mut render_to_device_scale = 1.0;
            let mut subregion = used_subregion;
            let padded_subregion = match op {
                FilterGraphOp::SVGFEGaussianBlur{std_deviation_x, std_deviation_y} |
                FilterGraphOp::SVGFEDropShadow{std_deviation_x, std_deviation_y, ..} => {
                    used_subregion
                    .inflate(
                        std_deviation_x.clamp(0.0, (i32::MAX / 2) as f32).ceil() * BLUR_SAMPLE_SCALE,
                        std_deviation_y.clamp(0.0, (i32::MAX / 2) as f32).ceil() * BLUR_SAMPLE_SCALE)
                }
                _ => used_subregion,
            }.union(&combined_input_subregion);
            while
                padded_subregion.scale(device_to_render_scale, device_to_render_scale).round().width() + node_inflate as f32 * 2.0 > MAX_SURFACE_SIZE as f32 ||
                padded_subregion.scale(device_to_render_scale, device_to_render_scale).round().height() + node_inflate as f32 * 2.0 > MAX_SURFACE_SIZE as f32 {
                device_to_render_scale *= 0.5;
                render_to_device_scale *= 2.0;
                // If the rendering was scaled, we need to snap used_subregion
                // to the correct granularity or we'd have misaligned sampling
                // when this is used as an input later.
                subregion = used_subregion
                    .scale(device_to_render_scale, device_to_render_scale)
                    .round()
                    .scale(render_to_device_scale, render_to_device_scale);
            }

            // This is the rect we will be actually producing as a render task,
            // it is sometimes the case that subregion is empty, but we
            // must make a task or else the earlier tasks would not be properly
            // linked into the frametree, causing a leak.
            let node_task_rect: DeviceRect =
                subregion
                .scale(device_to_render_scale, device_to_render_scale)
                .round()
                .inflate(node_inflate as f32, node_inflate as f32)
                .cast_unit();
            let node_task_size = node_task_rect.size().to_i32();
            let node_task_size =
                if node_task_size.width < 1 || node_task_size.height < 1 {
                    DeviceIntSize::new(1, 1)
                } else {
                    node_task_size
                };

            // Make the uv_rect_kind for this node's task to use, this matters
            // only on the final node because we don't use it internally
            let node_uv_rect_kind = uv_rect_kind_for_task_size(
                subregion
                .scale(device_to_render_scale, device_to_render_scale)
                .round()
                .inflate(node_inflate as f32, node_inflate as f32)
                .cast_unit(),
                prim_subregion
                .scale(device_to_render_scale, device_to_render_scale)
                .round()
                .inflate(node_inflate as f32, node_inflate as f32)
                .cast_unit(),
            );

            // Create task for this node
            let task_id;
            match op {
                FilterGraphOp::SVGFEGaussianBlur { std_deviation_x, std_deviation_y } => {
                    // Note: wrap_prim_with_filters copies the SourceGraphic to
                    // a node to apply the transparent border around the image,
                    // we rely on that behavior here as the Blur filter is a
                    // different shader without awareness of the subregion
                    // rules in the SVG spec.

                    // Find the input task id
                    assert!(node_inputs.len() == 1);
                    let blur_input = &node_inputs[0].0;
                    let source_task_id = node_inputs[0].1;

                    // We have to make a copy of the input that is padded with
                    // transparent black for the area outside the subregion, so
                    // that the blur task does not duplicate at the edges
                    let adjusted_blur_std_deviation = DeviceSize::new(
                        std_deviation_x.clamp(0.0, (i32::MAX / 2) as f32) * device_to_render_scale,
                        std_deviation_y.clamp(0.0, (i32::MAX / 2) as f32) * device_to_render_scale,
                    );
                    let blur_subregion = blur_input.subregion
                        .scale(device_to_render_scale, device_to_render_scale)
                        .inflate(
                            adjusted_blur_std_deviation.width * BLUR_SAMPLE_SCALE,
                            adjusted_blur_std_deviation.height * BLUR_SAMPLE_SCALE)
                        .round_out();
                    let blur_task_size = blur_subregion
                        .size()
                        .cast_unit()
                        .max(DeviceSize::new(1.0, 1.0));
                    // Adjust task size to prevent potential sampling errors
                    let adjusted_blur_task_size =
                        BlurTask::adjusted_blur_source_size(
                            blur_task_size,
                            adjusted_blur_std_deviation,
                        ).max(DeviceSize::new(1.0, 1.0));
                    // Now change the subregion to match the revised task size,
                    // keeping it centered should keep animated radius smooth.
                    let corner = LayoutPoint::new(
                            blur_subregion.min.x.floor() + ((
                                blur_task_size.width -
                                adjusted_blur_task_size.width) * 0.5).floor(),
                            blur_subregion.min.y.floor() + ((
                                blur_task_size.height -
                                adjusted_blur_task_size.height) * 0.5).floor(),
                        );
                    // Recalculate the blur_subregion to match, and if render
                    // scale is used, undo that so it is in the same subregion
                    // coordinate system as the node
                    let blur_subregion =
                        LayoutRect::new(
                            corner,
                            LayoutPoint::new(
                                corner.x + adjusted_blur_task_size.width,
                                corner.y + adjusted_blur_task_size.height,
                            ),
                        )
                        .scale(render_to_device_scale, render_to_device_scale);

                    let input_subregion_task_id = rg_builder.add().init(RenderTask::new_dynamic(
                        adjusted_blur_task_size.to_i32(),
                        RenderTaskKind::SVGFENode(
                            SVGFEFilterTask{
                                node: FilterGraphNode{
                                    kept_by_optimizer: true,
                                    linear: false,
                                    inflate: 0,
                                    inputs: [blur_input.clone()].to_vec(),
                                    subregion: blur_subregion,
                                },
                                op: FilterGraphOp::SVGFEIdentity,
                                content_origin: DevicePoint::zero(),
                                extra_gpu_data: None,
                            }
                        ),
                    ).with_uv_rect_kind(UvRectKind::Rect));
                    // Adding the dependencies sets the inputs for this task
                    rg_builder.add_dependency(input_subregion_task_id, source_task_id);

                    // TODO: We should do this blur in the correct
                    // colorspace, linear=true is the default in SVG and
                    // new_blur does not currently support it.  If the nodes
                    // that consume the result only use the alpha channel, it
                    // does not matter, but when they use the RGB it matters.
                    let blur_task_id =
                        RenderTask::new_blur(
                            adjusted_blur_std_deviation,
                            input_subregion_task_id,
                            rg_builder,
                            RenderTargetKind::Color,
                            None,
                            adjusted_blur_task_size.to_i32(),
                            BlurEdgeMode::Duplicate,
                        );

                    task_id = rg_builder.add().init(RenderTask::new_dynamic(
                        node_task_size,
                        RenderTaskKind::SVGFENode(
                            SVGFEFilterTask{
                                node: FilterGraphNode{
                                    kept_by_optimizer: true,
                                    linear: node.linear,
                                    inflate: node_inflate,
                                    inputs: [
                                        FilterGraphPictureReference{
                                            buffer_id: blur_input.buffer_id,
                                            subregion: blur_subregion,
                                            inflate: 0,
                                            offset: LayoutVector2D::zero(),
                                            source_padding: LayoutRect::zero(),
                                            target_padding: LayoutRect::zero(),
                                        }].to_vec(),
                                    subregion,
                                },
                                op: FilterGraphOp::SVGFEIdentity,
                                content_origin: node_task_rect.min,
                                extra_gpu_data: None,
                            }
                        ),
                    ).with_uv_rect_kind(node_uv_rect_kind));
                    // Adding the dependencies sets the inputs for this task
                    rg_builder.add_dependency(task_id, blur_task_id);
                }
                FilterGraphOp::SVGFEDropShadow { color, dx, dy, std_deviation_x, std_deviation_y } => {
                    // Note: wrap_prim_with_filters copies the SourceGraphic to
                    // a node to apply the transparent border around the image,
                    // we rely on that behavior here as the Blur filter is a
                    // different shader without awareness of the subregion
                    // rules in the SVG spec.

                    // Find the input task id
                    assert!(node_inputs.len() == 1);
                    let blur_input = &node_inputs[0].0;
                    let source_task_id = node_inputs[0].1;

                    // We have to make a copy of the input that is padded with
                    // transparent black for the area outside the subregion, so
                    // that the blur task does not duplicate at the edges
                    let adjusted_blur_std_deviation = DeviceSize::new(
                        std_deviation_x.clamp(0.0, (i32::MAX / 2) as f32) * device_to_render_scale,
                        std_deviation_y.clamp(0.0, (i32::MAX / 2) as f32) * device_to_render_scale,
                    );
                    let blur_subregion = blur_input.subregion
                        .scale(device_to_render_scale, device_to_render_scale)
                        .inflate(
                            adjusted_blur_std_deviation.width * BLUR_SAMPLE_SCALE,
                            adjusted_blur_std_deviation.height * BLUR_SAMPLE_SCALE)
                        .round_out();
                    let blur_task_size = blur_subregion
                        .size()
                        .cast_unit()
                        .max(DeviceSize::new(1.0, 1.0));
                    // Adjust task size to prevent potential sampling errors
                    let adjusted_blur_task_size =
                        BlurTask::adjusted_blur_source_size(
                            blur_task_size,
                            adjusted_blur_std_deviation,
                        ).max(DeviceSize::new(1.0, 1.0));
                    // Now change the subregion to match the revised task size,
                    // keeping it centered should keep animated radius smooth.
                    let corner = LayoutPoint::new(
                            blur_subregion.min.x.floor() + ((
                                blur_task_size.width -
                                adjusted_blur_task_size.width) * 0.5).floor(),
                            blur_subregion.min.y.floor() + ((
                                blur_task_size.height -
                                adjusted_blur_task_size.height) * 0.5).floor(),
                        );
                    // Recalculate the blur_subregion to match, and if render
                    // scale is used, undo that so it is in the same subregion
                    // coordinate system as the node
                    let blur_subregion =
                        LayoutRect::new(
                            corner,
                            LayoutPoint::new(
                                corner.x + adjusted_blur_task_size.width,
                                corner.y + adjusted_blur_task_size.height,
                            ),
                        )
                        .scale(render_to_device_scale, render_to_device_scale);

                    let input_subregion_task_id = rg_builder.add().init(RenderTask::new_dynamic(
                        adjusted_blur_task_size.to_i32(),
                        RenderTaskKind::SVGFENode(
                            SVGFEFilterTask{
                                node: FilterGraphNode{
                                    kept_by_optimizer: true,
                                    linear: false,
                                    inputs: [
                                        FilterGraphPictureReference{
                                            buffer_id: blur_input.buffer_id,
                                            subregion: blur_input.subregion,
                                            offset: LayoutVector2D::zero(),
                                            inflate: blur_input.inflate,
                                            source_padding: LayoutRect::zero(),
                                            target_padding: LayoutRect::zero(),
                                        }].to_vec(),
                                    subregion: blur_subregion,
                                    inflate: 0,
                                },
                                op: FilterGraphOp::SVGFEIdentity,
                                content_origin: node_task_rect.min,
                                extra_gpu_data: None,
                            }
                        ),
                    ).with_uv_rect_kind(UvRectKind::Rect));
                    // Adding the dependencies sets the inputs for this task
                    rg_builder.add_dependency(input_subregion_task_id, source_task_id);

                    // The shadow compositing only cares about alpha channel
                    // which is always linear, so we can blur this in sRGB or
                    // linear color space and the result is the same as we will
                    // be replacing the rgb completely.
                    let blur_task_id =
                        RenderTask::new_blur(
                            adjusted_blur_std_deviation,
                            input_subregion_task_id,
                            rg_builder,
                            RenderTargetKind::Color,
                            None,
                            adjusted_blur_task_size.to_i32(),
                            BlurEdgeMode::Duplicate,
                        );

                    // Now we make the compositing task, for this we need to put
                    // the blurred shadow image at the correct subregion offset
                    let blur_subregion_translated = blur_subregion
                        .translate(LayoutVector2D::new(dx, dy));
                    task_id = rg_builder.add().init(RenderTask::new_dynamic(
                        node_task_size,
                        RenderTaskKind::SVGFENode(
                            SVGFEFilterTask{
                                node: FilterGraphNode{
                                    kept_by_optimizer: true,
                                    linear: node.linear,
                                    inflate: node_inflate,
                                    inputs: [
                                        // Original picture
                                        *blur_input,
                                        // Shadow picture
                                        FilterGraphPictureReference{
                                            buffer_id: blur_input.buffer_id,
                                            subregion: blur_subregion_translated,
                                            inflate: 0,
                                            offset: LayoutVector2D::zero(),
                                            source_padding: LayoutRect::zero(),
                                            target_padding: LayoutRect::zero(),
                                        }].to_vec(),
                                    subregion,
                                },
                                op: FilterGraphOp::SVGFEDropShadow{
                                    color,
                                    // These parameters don't matter here
                                    dx: 0.0, dy: 0.0,
                                    std_deviation_x: 0.0, std_deviation_y: 0.0,
                                },
                                content_origin: node_task_rect.min,
                                extra_gpu_data: None,
                            }
                        ),
                    ).with_uv_rect_kind(node_uv_rect_kind));
                    // Adding the dependencies sets the inputs for this task
                    rg_builder.add_dependency(task_id, source_task_id);
                    rg_builder.add_dependency(task_id, blur_task_id);
                }
                FilterGraphOp::SVGFESourceAlpha |
                FilterGraphOp::SVGFESourceGraphic => {
                    // These copy from the original task, we have to synthesize
                    // a fake input binding to make the shader do the copy.  In
                    // the case of SourceAlpha the shader will zero the RGB but
                    // we don't have to care about that distinction here.
                    task_id = rg_builder.add().init(RenderTask::new_dynamic(
                        node_task_size,
                        RenderTaskKind::SVGFENode(
                            SVGFEFilterTask{
                                node: FilterGraphNode{
                                    kept_by_optimizer: true,
                                    linear: node.linear,
                                    inflate: node_inflate,
                                    inputs: [
                                        FilterGraphPictureReference{
                                            buffer_id: FilterOpGraphPictureBufferId::None,
                                            // This is what makes the mapping
                                            // actually work.
                                            subregion: source_subregion.cast_unit(),
                                            offset: LayoutVector2D::zero(),
                                            inflate: 0,
                                            source_padding: LayoutRect::zero(),
                                            target_padding: LayoutRect::zero(),
                                        }
                                    ].to_vec(),
                                    subregion: source_subregion.cast_unit(),
                                },
                                op: op.clone(),
                                content_origin: source_subregion.min.cast_unit(),
                                extra_gpu_data: None,
                            }
                        ),
                    ).with_uv_rect_kind(node_uv_rect_kind));
                    rg_builder.add_dependency(task_id, original_task_id);
                    made_dependency_on_source = true;
                }
                FilterGraphOp::SVGFEComponentTransferInterned { handle, creates_pixels: _ } => {
                    // FIXME: Doing this in prepare_interned_prim_for_render
                    // doesn't seem to be enough, where should it be done?
                    let filter_data = &data_stores.filter_data[handle];
                    let filter_data_address = filter_data.data.write_gpu_blocks(gpu_buffer);
                    // ComponentTransfer has a gpu buffer address that we need to
                    // pass along
                    task_id = rg_builder.add().init(RenderTask::new_dynamic(
                        node_task_size,
                        RenderTaskKind::SVGFENode(
                            SVGFEFilterTask {
                                node: FilterGraphNode{
                                    kept_by_optimizer: true,
                                    linear: node.linear,
                                    inputs: node_inputs.iter().map(|input| {input.0}).collect(),
                                    subregion,
                                    inflate: node_inflate,
                                },
                                op: op.clone(),
                                content_origin: node_task_rect.min,
                                extra_gpu_data: Some(filter_data_address),
                            }
                        ),
                    ).with_uv_rect_kind(node_uv_rect_kind));

                    // Add the dependencies for inputs of this node, which will
                    // be used by add_svg_filter_node_instances later
                    for (_input, input_task) in &node_inputs {
                        if *input_task == original_task_id {
                            made_dependency_on_source = true;
                        }
                        if *input_task != RenderTaskId::INVALID {
                            rg_builder.add_dependency(task_id, *input_task);
                        }
                    }
                }
                _ => {
                    // This is the usual case - zero, one or two inputs that
                    // reference earlier node results.
                    task_id = rg_builder.add().init(RenderTask::new_dynamic(
                        node_task_size,
                        RenderTaskKind::SVGFENode(
                            SVGFEFilterTask{
                                node: FilterGraphNode{
                                    kept_by_optimizer: true,
                                    linear: node.linear,
                                    inputs: node_inputs.iter().map(|input| {input.0}).collect(),
                                    subregion,
                                    inflate: node_inflate,
                                },
                                op: op.clone(),
                                content_origin: node_task_rect.min,
                                extra_gpu_data: None,
                            }
                        ),
                    ).with_uv_rect_kind(node_uv_rect_kind));

                    // Add the dependencies for inputs of this node, which will
                    // be used by add_svg_filter_node_instances later
                    for (_input, input_task) in &node_inputs {
                        if *input_task == original_task_id {
                            made_dependency_on_source = true;
                        }
                        if *input_task != RenderTaskId::INVALID {
                            rg_builder.add_dependency(task_id, *input_task);
                        }
                    }
                }
            }

            // We track the tasks we created by output buffer id to make it easy
            // to look them up quickly, since nodes can only depend on previous
            // nodes in the same list
            task_by_buffer_id[filter_index] = task_id;
            subregion_by_buffer_id[filter_index] = subregion;

            // The final task we create is the output picture.
            output_task_id = task_id;
        }

        // If no tasks referenced the SourceGraphic, we actually have to create
        // a fake dependency so that it does not leak.
        if !made_dependency_on_source && output_task_id != original_task_id {
            rg_builder.add_dependency(output_task_id, original_task_id);
        }

        output_task_id
    }

    pub fn uv_rect_kind(&self) -> UvRectKind {
        self.uv_rect_kind
    }

    pub fn get_texture_address(&self) -> GpuBufferAddress {
        self.uv_rect_handle
    }

    pub fn get_target_texture(&self) -> CacheTextureId {
        match self.location {
            RenderTaskLocation::Dynamic { texture_id, .. } => {
                assert_ne!(texture_id, CacheTextureId::INVALID);
                texture_id
            }
            RenderTaskLocation::Static { surface: StaticRenderTaskSurface::TextureCache { texture, .. }, .. } => {
                texture
            }
            _ => {
                unreachable!();
            }
        }
    }

    pub fn get_texture_source(&self) -> TextureSource {
        match self.location {
            RenderTaskLocation::Dynamic { texture_id, .. } => {
                assert_ne!(texture_id, CacheTextureId::INVALID);
                TextureSource::TextureCache(texture_id, Swizzle::default())
            }
            RenderTaskLocation::Static { surface:  StaticRenderTaskSurface::ReadOnly { source }, .. } => {
                source
            }
            RenderTaskLocation::Static { surface: StaticRenderTaskSurface::TextureCache { texture, .. }, .. } => {
                TextureSource::TextureCache(texture, Swizzle::default())
            }
            RenderTaskLocation::Static { .. } |
            RenderTaskLocation::CacheRequest { .. } |
            RenderTaskLocation::Unallocated { .. } => {
                unreachable!();
            }
        }
    }

    pub fn get_target_rect(&self) -> DeviceIntRect {
        match self.location {
            // Previously, we only added render tasks after the entire
            // primitive chain was determined visible. This meant that
            // we could assert any render task in the list was also
            // allocated (assigned to passes). Now, we add render
            // tasks earlier, and the picture they belong to may be
            // culled out later, so we can't assert that the task
            // has been allocated.
            // Render tasks that are created but not assigned to
            // passes consume a row in the render task texture, but
            // don't allocate any space in render targets nor
            // draw any pixels.
            // TODO(gw): Consider some kind of tag or other method
            //           to mark a task as unused explicitly. This
            //           would allow us to restore this debug check.
            RenderTaskLocation::Dynamic { rect, .. } => rect,
            RenderTaskLocation::Static { rect, .. } => rect,
            RenderTaskLocation::CacheRequest { .. } |
            RenderTaskLocation::Unallocated { .. } => {
                panic!("bug: get_target_rect called before allocating");
            }
        }
    }

    pub fn get_target_size(&self) -> DeviceIntSize {
        match self.location {
            RenderTaskLocation::Dynamic { rect, .. } => rect.size(),
            RenderTaskLocation::Static { rect, .. } => rect.size(),
            RenderTaskLocation::CacheRequest { size } => size,
            RenderTaskLocation::Unallocated { size } => size,
        }
    }

    pub fn target_kind(&self) -> RenderTargetKind {
        self.kind.target_kind()
    }

    /// Called by the render task cache.
    ///
    /// Tells the render task that it is cached (which means its gpu cache
    /// handle is managed by the texture cache).
    pub fn mark_cached(&mut self, handle: RenderTaskCacheEntryHandle) {
        self.cache_handle = Some(handle);
    }
}

/// A rendering operation applied on top of a render task.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum SubTask {
    RectangleClip(RectangleClipSubTask),
    ImageClip(ImageClipSubTask),
    PathClip(PathClipSubTask),
}

/// A (rounded) rectangle clip applied to a render task using the multiply
/// blend mode on top of the content being clipped.
#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct RectangleClipSubTask {
    /// Quad primitive address for this clip.
    pub quad_address: GpuBufferAddress,
    /// Location of the (rounded) rectangle parameters.
    pub clip_address: GpuBufferAddress,
    /// The coordinate space of the clip.
    /// - If primitive space, then the clip's quad primitive is positioned using
    ///   the transform of the primitive being clipped. clip_transform_id is used
    ///   by the shader to map from that space to the clip's local space.
    ///   This is used when the clip and raster space are indifferent coordinate
    ///   systems.
    /// - If raster space, then the clip's quad primitive is positioned directly
    ///   using rectangles in raster space (no transforms). The clip parameters
    ///   are still potentially in a different space so clip_transform_id is used
    ///   to map from raster to clip space (this transform is assumed to be a
    ///   scale-offset).
    pub clip_space: ClipSpace,
    /// Transform applied to the quad primitive of the clip.
    pub quad_transform_id: GpuTransformId,
    /// Transform from the quad primitive's space to the clip's local space.
    pub clip_transform_id: GpuTransformId,
    pub quad_flags: QuadFlags,
    pub needs_scissor_rect: bool,
    pub rounded_rect_fast_path: bool,
    pub rounded_rect_superellipse: bool,
}

/// An clip applied to a render task using the multiply blend mode on top of
/// the content being clipped.
#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ImageClipSubTask {
    /// Quad primitive address for this clip.
    pub quad_address: GpuBufferAddress,
    /// Transform from the clip's local space to raster space (to position the
    /// clip's quad primitive).
    pub quad_transform_id: GpuTransformId,
    pub src_task: RenderTaskId,
    pub quad_flags: QuadFlags,
    pub needs_scissor_rect: bool,
}

/// A path clip applied to a render task using the premultiplied dest-out
/// blend mode on top of the content being clipped.
///
/// The path is rasterized into tiles when the render task is added to its
/// render target.
#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PathClipSubTask {
    pub path: Path,
    pub fill_rule: FillRule,
    /// Maps the path to the render task's local device pixels.
    pub transform: PathTransform,
}
