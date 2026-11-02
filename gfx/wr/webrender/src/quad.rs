/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{BorderRadius, ClipMode, ColorF, units::*};
use euclid::{SideOffsets2D, Size2D, point2};

use crate::ItemUid;
use crate::border::NinePatchDescriptorExt;
use crate::gpu_types::ClipSpace;
use crate::pattern::repeat::RepeatedPattern;
use crate::path_tiler::PathTransform;
use crate::render_task::{ImageClipSubTask, PathClipSubTask, RectangleClipSubTask, SubTask};
use crate::transform::TransformPalette;
use crate::batch::{BatchKey, BatchKind, BatchTextures};
use crate::clip::clamped_radius;
use crate::command_buffer::{CommandBufferIndex, PrimitiveCommand, QuadFlags};
use crate::command_buffer::CommandBufferList;
use crate::frame_builder::FrameBuildingState;
use crate::gpu_types::{PrimitiveInstanceData, QuadHeader, QuadInstance, QuadPrimitive, QuadSegment, ZBufferId};
use crate::internal_types::TextureSource;
use crate::pattern::{Pattern, PatternBuilder, PatternBuilderState, PatternKind, PatternShaderInput};
use crate::prim_store::{NinePatchDescriptor, PrimitiveScratchBuffer};
use crate::quad_clip::{QuadClip, QuadClipShape, QuadClipStack, QuadMaskTile};
use crate::render_task::{RenderTask, RenderTaskAddress, RenderTaskKind};
use crate::render_task_cache::{RenderTaskCacheKey, RenderTaskCacheKeyKind, RenderTaskParent};
use crate::render_task_graph::{RenderTaskGraph, RenderTaskGraphBuilder, RenderTaskId, SubTaskRange};
use crate::renderer::{BlendMode, GpuBufferAddress, GpuBufferBuilder, GpuBufferBuilderF, GpuBufferDataI};
use crate::segment::EdgeMask;
use crate::space::SpaceMapper;
use crate::spatial_tree::{CoordinateSpaceMapping, SpatialNodeIndex, SpatialTree};
use crate::transform::GpuTransformId;
use crate::util::{extract_inner_rect_k, MatrixHelpers, MaxRect, ScaleOffset};
use crate::visibility::compute_surface_visible_rect;

/// This type reflects the unfortunate situation with quad coordinates where we
/// sometimes use layout and sometimes device coordinates.
pub type LayoutOrDeviceRect = api::euclid::default::Box2D<f32>;

const MIN_AA_SEGMENTS_SIZE: f32 = 4.0;
const MIN_QUAD_SPLIT_SIZE: f32 = 256.0;
// We merge compatible neighbor tiles in the same rows which means that allowing
// more tiles on the x axis doesn't generally produce more tiles, but it allows
// more precise segmentation.
// The segmentation right now is still quite coarse.
const MAX_TILES_PER_QUAD_X: usize = 8;
const MAX_TILES_PER_QUAD_Y: usize = 4;


#[derive(Clone, Debug, Hash, PartialEq, Eq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct QuadCacheKey {
    pub prim: u64,
    pub clips: [u64; 3],
    pub transform: [u32; 4],
}

/// The geometry and anti-aliasing parameters that most quad primitives need.
#[derive(Copy, Clone, Debug)]
pub struct QuadDescriptor {
    /// Situates the pattern (gradient line, image UV space, box shadow geometry,
    /// ...) in the primitive's local space. Contributes nothing to coverage, and
    /// is unconstrained relative to `bounds`: it can be larger, smaller or
    /// disjoint.
    pub pattern_rect: LayoutRect,
    /// The coverage rect in local space.
    pub bounds: LayoutRect,
    /// Which edges are anti-aliased if the primitive is axis-aligned.
    ///
    /// Typically no edges for CSS primitives and all edges for SVG primitives:
    /// web specs require anti-aliasing the axis-aligned edges of SVG content but
    /// not those of CSS content.
    pub aligned_aa_edges: EdgeMask,
    /// Which edges are anti-aliased if the primitive is not axis-aligned.
    ///
    /// Typically all edges, or at least a superset of `aligned_aa_edges`.
    pub transformed_aa_edges: EdgeMask,
}

/// Contains some transform-related information that is computed
/// per primitive cluster.
pub struct QuadTransformState {
    map_prim_to_raster: CoordinateSpaceMapping<LayoutPixel, LayoutPixel>,
    as_scale_offset: Option<ScaleOffset>, // local-to-device
    is_2d_axis_aligned: bool,
    prim_spatial_node: SpatialNodeIndex,
    raster_spatial_node: SpatialNodeIndex,
    device_pixel_scale: DevicePixelScale,
}

impl QuadTransformState {
    pub fn new() -> QuadTransformState {
        QuadTransformState {
            map_prim_to_raster: CoordinateSpaceMapping::Local,
            as_scale_offset: Some(ScaleOffset::identity()),
            is_2d_axis_aligned: true,
            prim_spatial_node: SpatialNodeIndex::INVALID,
            raster_spatial_node: SpatialNodeIndex::INVALID,
            device_pixel_scale: DevicePixelScale::identity(),
        }
    }

    pub fn from_scale_offset(
        local_to_raster: ScaleOffset,
        prim_node: SpatialNodeIndex,
        raster_node: SpatialNodeIndex,
        scale: DevicePixelScale,
    ) -> QuadTransformState {
        QuadTransformState {
            map_prim_to_raster: CoordinateSpaceMapping::ScaleOffset(local_to_raster),
            as_scale_offset: Some(local_to_raster.then_scale(scale.0)),
            is_2d_axis_aligned: true,
            prim_spatial_node: prim_node,
            raster_spatial_node: raster_node,
            device_pixel_scale: scale,
        }
    }

    pub fn set(
        &mut self,
        src_node: SpatialNodeIndex,
        dst_node: SpatialNodeIndex,
        spatial_tree: &SpatialTree,
        scale: DevicePixelScale,
    ) {
        if self.prim_spatial_node == src_node && self.raster_spatial_node == dst_node {
            return;
        }

        self.map_prim_to_raster = spatial_tree.get_relative_transform(src_node, dst_node);

        self.as_scale_offset = self.map_prim_to_raster
            .as_2d_scale_offset()
            .map(|t| t.then_scale(scale.0));

        self.is_2d_axis_aligned = self.as_scale_offset.is_some()
            || self.map_prim_to_raster.is_2d_axis_aligned();

        self.prim_spatial_node = src_node;
        self.raster_spatial_node = dst_node;
        self.device_pixel_scale = scale;
    }

    pub fn is_2d_scale_offset(&self) -> bool {
        self.as_scale_offset.is_some()
    }

    pub fn is_2d_axis_aligned(&self) -> bool {
        self.is_2d_axis_aligned
    }

    // The local to device transform as a scale+offset transform if it
    // can be represented as such.
    pub fn as_2d_scale_offset(&self) -> Option<&ScaleOffset> {
        self.as_scale_offset.as_ref()
    }

    // X and Y scale factors of the local to raster transform, excluding the
    // target surface's device pixel scale.
    pub fn raster_scale_factors(&self) -> (f32, f32) {
        self.map_prim_to_raster.scale_factors()
    }

    // X and Y scale factors of the local to device transform.
    pub fn scale_factors(&self) -> (f32, f32) {
        let s = self.raster_scale_factors();

        (s.0 * self.device_pixel_scale().0, s.1 * self.device_pixel_scale().0)
    }

    // X and Y scale factors of the local to device transform, or None if the
    // transform's perspective divide varies across the primitive's plane, where
    // no single scale factor describes it.
    pub fn coplanar_scale_factors(&self) -> Option<(f32, f32)> {
        let (x, y) = self.map_prim_to_raster.coplanar_scale_factors()?;
        let device_pixel_scale = self.device_pixel_scale().0;

        Some((x * device_pixel_scale, y * device_pixel_scale))
    }

    pub fn prim_spatial_node_index(&self) -> SpatialNodeIndex {
        self.prim_spatial_node
    }

    /// Map a rect in the target surface's device space back into the
    /// primitive's local space, or `None` if the transform cannot be inverted.
    pub fn unmap_rect(&self, device_rect: &DeviceRect) -> Option<LayoutRect> {
        if let Some(ref local_to_device) = self.as_scale_offset {
            return Some(local_to_device.unmap_rect(device_rect));
        }

        let inv_scale = 1.0 / self.device_pixel_scale.0;
        let raster_rect: LayoutRect = device_rect.cast_unit().scale(inv_scale, inv_scale);

        match self.map_prim_to_raster {
            CoordinateSpaceMapping::Local => Some(raster_rect),
            CoordinateSpaceMapping::ScaleOffset(ref scale_offset) => {
                Some(scale_offset.unmap_rect(&raster_rect))
            }
            CoordinateSpaceMapping::Transform(ref transform) => {
                transform.inverse_rect_footprint(&raster_rect)
            }
        }
    }

    pub fn raster_spatial_node_index(&self) -> SpatialNodeIndex {
        self.raster_spatial_node
    }

    pub fn device_pixel_scale(&self) -> DevicePixelScale {
        self.device_pixel_scale
    }
}

/// Describes how clipping affects the rendering of a quad primitive.
///
/// As a general rule, parts of the quad that require masking are prerendered in an
/// intermediate target and the mask is applied using multiplicative blending to
/// the intermediate result before compositing it into the destination target.
///
/// Each segment can opt in or out of masking independently.
#[derive(Debug, Copy, Clone)]
pub enum QuadRenderStrategy {
    /// The quad is not affected by any mask and is drawn directly in the destination
    /// target.
    Direct,
    /// The quad is drawn entirely in an intermediate target and a mask is applied
    /// before compositing in the destination target.
    Indirect,
    /// A rounded rectangle clip is applied to the quad primitive via a nine-patch.
    /// The segments of the nine-patch that require a mask are rendered and masked in
    /// an intermediate target, while other segments are drawn directly in the destination
    /// target.
    NinePatch {
        radius: LayoutVector2D,
        clip_rect: LayoutRect,
    },
    /// Split the primitive into coarse tiles so that each tile independently
    /// has the opportunity to be drawn directly in the destination target or
    /// via an intermediate target if it is affected by a mask.
    Tiled,
}

pub fn prepare_quad(
    pattern_builder: &dyn PatternBuilder,
    desc: &QuadDescriptor,
    cache_key: &Option<QuadCacheKey>,
    clips: &QuadClipStack,
    transform: &mut QuadTransformState,

    spatial_tree: &SpatialTree,
    targets: &[CommandBufferIndex],

    frame_state: &mut FrameBuildingState,
    scratch: &mut PrimitiveScratchBuffer,
) {
    let pattern = pattern_builder.build(
        &desc.pattern_rect,
        &mut PatternBuilderState {
            frame_gpu_data: frame_state.frame_gpu_data,
            transforms: frame_state.transforms,
        },
    );

    let strategy = match cache_key {
        Some(_) => QuadRenderStrategy::Indirect,
        None => get_prim_render_strategy(
            transform.prim_spatial_node_index(),
            clips,
            transform.is_2d_scale_offset(),
            spatial_tree,
        ),
    };

    prepare_quad_impl(
        strategy,
        &pattern,
        desc,
        cache_key,
        clips,

        transform,
        spatial_tree,
        targets,

        frame_state,
        scratch,
    )
}

pub fn prepare_repeatable_quad(
    pattern_builder: &dyn PatternBuilder,
    desc: &QuadDescriptor,
    stretch_size: LayoutSize,
    tile_spacing: LayoutSize,
    cache_key: &Option<QuadCacheKey>,
    clips: &QuadClipStack,
    transform: &mut QuadTransformState,

    spatial_tree: &SpatialTree,
    targets: &[CommandBufferIndex],

    frame_state: &mut FrameBuildingState,
    scratch: &mut PrimitiveScratchBuffer,
) {
    let pattern = pattern_builder.build(
        &desc.pattern_rect,
        &mut PatternBuilderState {
            frame_gpu_data: frame_state.frame_gpu_data,
            transforms: frame_state.transforms,
        },
    );

    // This could move back into prepare_quad_impl if it took the tile's
    // coverage rect into account rather than the whole primitive's, but
    // for now it does the latter so we might as well not do the work
    // multiple times.
    let strategy = match cache_key {
        Some(_) => QuadRenderStrategy::Indirect,
        None => get_prim_render_strategy(
            transform.prim_spatial_node_index(),
            clips,
            transform.is_2d_scale_offset(),
            spatial_tree,
        ),
    };

    let needs_repetition = stretch_size.width < desc.pattern_rect.width()
        || stretch_size.height < desc.pattern_rect.height();

    if !needs_repetition {
        // The stretch size may be larger than the local rect's size which
        // should result in some stretching (without repetitions). However,
        // the non-repeated quad code paths don't take a stretch_size, so
        // we bake it into the local rect. The bounds are unchanged: the
        // stretched rect only situates the pattern, and since there are no
        // repetitions it contains the original rect, so it cannot shrink the
        // coverage either.
        let stretched_desc = QuadDescriptor {
            pattern_rect: LayoutRect::from_origin_and_size(
                desc.pattern_rect.min,
                stretch_size,
            ),
            ..*desc
        };

        // Most common path.
        prepare_quad_impl(
            strategy,
            &pattern,
            &stretched_desc,
            &cache_key,
            clips,
            transform,
            spatial_tree,
            targets,
            frame_state,
            scratch,
        );

        return;
    }

    let pattern_rect = LayoutRect::from_origin_and_size(
        desc.pattern_rect.min,
        stretch_size,
    );

    let scales = transform.scale_factors();
    let mut indirect_transform = ScaleOffset::from_scale(scales.into());
    let mut surface_rect: DeviceRect = indirect_transform.map_rect(&pattern_rect);

    // If the source pattern is an image, we can repeat it directly using the repeat
    // shader, without an extra render task.
    let src_task_id = pattern.as_render_task();

    // If the number of repetitions is high, we are better off using the repeat shader,
    // but we want to avoid the extra render task if it is large.
    let num_repetitions = desc.pattern_rect.area() / stretch_size.area();
    let repeat_using_a_shader = src_task_id.is_some()
        || (num_repetitions > 16.0 && surface_rect.width() < 1024.0 && surface_rect.height() < 1024.0)
        || (num_repetitions > 64.0 && surface_rect.area() < 1024.0 * 1024.0);

    if repeat_using_a_shader {
        let (src_task_id, base_color) = match src_task_id {
            Some(task) => (task, pattern.base_color),
            None => {
                // The source is not an image. Make it one by rendering
                // the pattern in a render task.

                adjust_indirect_pattern_resolution(
                    &pattern_rect,
                    2048.0,
                    &mut surface_rect,
                    &mut indirect_transform,
                );

                let Some(task_id) = prepare_indirect_pattern(
                    transform.prim_spatial_node_index(),
                    transform.raster_spatial_node_index(),
                    &pattern_rect,
                    &pattern_rect,
                    &surface_rect,
                    Some(&indirect_transform),
                    DevicePixelScale::identity(),
                    GpuTransformId::IDENTITY,
                    &pattern,
                    QuadFlags::empty(),
                    EdgeMask::empty(),
                    cache_key,
                    None,
                    spatial_tree,
                    frame_state,
                ) else {
                    return;
                };

                (task_id, ColorF::WHITE)
            }
        };

        let repetitions = RepeatedPattern {
            stretch_size,
            spacing: tile_spacing,
            src_task_id,
            src_is_opaque: pattern.is_opaque,
        };

        let repeat_pattern = repetitions.build(
            &desc.pattern_rect,
            &mut PatternBuilderState {
                frame_gpu_data: frame_state.frame_gpu_data,
                transforms: frame_state.transforms,
            },
        ).with_base_color(base_color);

        // Note: caching is disabled when using the repeating shader.
        // The cache key would need more information about the repetition.
        prepare_quad_impl(
            strategy,
            &repeat_pattern,
            desc,
            &None,
            clips,
            transform,
            spatial_tree,
            targets,
            frame_state,
            scratch,
        );

        return;
    }

    // Repeat by duplicating the primitive.

    let visible_rect = compute_surface_visible_rect(
        &clips.surface_clip_rect(),
        clips.coverage_rect(),
        transform,
        &desc.bounds,
    );

    let stride = stretch_size + tile_spacing;
    let repetitions = crate::image_tiling::repetitions(&desc.pattern_rect, &visible_rect, stride);
    for tile in repetitions {
        let tile_rect = LayoutRect::from_origin_and_size(tile.origin, stretch_size);
        // The last tile of each row/column typically extends past the
        // primitive, so the tile rect is part of each tile's coverage on top of
        // the primitive's own bounds.
        let tile_bounds = desc.bounds.intersection_unchecked(&tile_rect);
        if tile_bounds.is_empty() {
            continue;
        }

        let pattern = pattern_builder.build(
            &tile_rect,
            &mut PatternBuilderState {
                frame_gpu_data: frame_state.frame_gpu_data,
                transforms: frame_state.transforms,
            },
        );

        prepare_quad_impl(
            strategy,
            &pattern,
            &QuadDescriptor {
                pattern_rect: tile_rect,
                bounds: tile_bounds,
                aligned_aa_edges: desc.aligned_aa_edges & tile.edge_flags,
                transformed_aa_edges: desc.transformed_aa_edges & tile.edge_flags,
            },
            // Bug 2017832 - Caching breaks manually repeated patterns
            // with SWGL for some reason.
            &None,
            clips,
            transform,
            spatial_tree,
            targets,
            frame_state,
            scratch,
        );
    }
}

pub fn prepare_border_nine_patch(
    nine_patch: &NinePatchDescriptor,
    pattern_builder: &dyn PatternBuilder,
    desc: &QuadDescriptor,
    stretch_size: LayoutSize,
    clips: &QuadClipStack,
    transform: &mut QuadTransformState,

    spatial_tree: &SpatialTree,
    targets: &[CommandBufferIndex],

    frame_state: &mut FrameBuildingState,
    scratch: &mut PrimitiveScratchBuffer,
) {
    let pattern = pattern_builder.build(
        &desc.pattern_rect,
        &mut PatternBuilderState {
            frame_gpu_data: frame_state.frame_gpu_data,
            transforms: frame_state.transforms,
        },
    );

    let strategy = get_prim_render_strategy(
        transform.prim_spatial_node_index(),
        clips,
        transform.is_2d_scale_offset(),
        spatial_tree,
    );

    // The indirect transform drives the resolution at which each segment is going
    // going to be rasterized in intermediate render tasks.
    let scales = transform.scale_factors();
    let base_indirect_transform = ScaleOffset::from_scale(scales.into());

    nine_patch.for_each_segment(&desc.pattern_rect, &mut|dst_rect, src_rect, side, _repeat_h, _repeat_v| {
        // First find the sub-rect of the source pattern that this segment is using.
        let min_x = desc.pattern_rect.min.x + stretch_size.width * src_rect.uv0.x;
        let min_y = desc.pattern_rect.min.y + stretch_size.height * src_rect.uv0.y;
        let max_x = desc.pattern_rect.min.x + stretch_size.width * src_rect.uv1.x;
        let max_y = desc.pattern_rect.min.y + stretch_size.height * src_rect.uv1.y;
        let pattern_rect = LayoutRect {
            min: point2(min_x, min_y),
            max: point2(max_x, max_y),
        };

        // Rasterize the source pattern into a render task.

        // We could get away without the intermediate task in some cases, for example
        // if the segment does not repeat the pattern. However this is fiddly due to
        // how the nine-patch's source slicing distorts the local space of the pattern.
        // Always using an intermediate render task lets us easily handle the additional
        // stretching effect on the image instead of introducing an additional transform
        // for the pattern's coordinate space. On the other hand it means that we have
        // to handle large source patterns and potentially down-scale them.

        let mut indirect_transform = base_indirect_transform;
        let mut surface_rect = indirect_transform.map_rect(&pattern_rect);

        adjust_indirect_pattern_resolution(
            &pattern_rect,
            2048.0,
            &mut surface_rect,
            &mut indirect_transform,
        );

        let Some(task_id) = prepare_indirect_pattern(
            transform.prim_spatial_node_index(),
            transform.raster_spatial_node_index(),
            &pattern_rect,
            &pattern_rect,
            &surface_rect,
            Some(&indirect_transform),
            DevicePixelScale::identity(),
            GpuTransformId::IDENTITY,
            &pattern,
            QuadFlags::empty(),
            EdgeMask::empty(),
            &None,
            None,
            spatial_tree,
            frame_state,
        ) else {
            return;
        };

        let img_pattern = Pattern::texture(task_id, pattern.is_opaque);

        prepare_quad_impl(
            strategy,
            &img_pattern,
            &QuadDescriptor {
                pattern_rect: *dst_rect,
                // Each segment covers its own destination rect, within the
                // primitive's bounds.
                bounds: desc.bounds.intersection_unchecked(dst_rect),
                aligned_aa_edges: desc.aligned_aa_edges & side,
                transformed_aa_edges: desc.transformed_aa_edges & side,
            },
            &None,
            clips,

            transform,
            spatial_tree,
            targets,

            frame_state,
            scratch,
        )
    });
}

fn prepare_quad_impl(
    strategy: QuadRenderStrategy,
    pattern: &Pattern,
    desc: &QuadDescriptor,
    cache_key: &Option<QuadCacheKey>,
    clips: &QuadClipStack,

    transform: &mut QuadTransformState,
    spatial_tree: &SpatialTree,
    targets: &[CommandBufferIndex],

    frame_state: &mut FrameBuildingState,
    scratch: &mut PrimitiveScratchBuffer,
) {
    // If the local-to-device transform can be expressed as a 2D scale-offset,
    // We'll apply the transformation on the CPU and submit geometry in device
    // space to the shaders. Otherwise, the geometry is sent to the shaders in
    // layout space along with a transform.

    let transform_id = if transform.is_2d_scale_offset() {
        GpuTransformId::IDENTITY
    } else {
        frame_state.transforms.gpu.get_id_with_post_scale(
            transform.prim_spatial_node_index(),
            transform.raster_spatial_node_index(),
            transform.device_pixel_scale().get(),
            spatial_tree,
        )
    };

    let prim_is_2d_scale_translation = transform.is_2d_scale_offset();
    let prim_is_2d_axis_aligned = transform.is_2d_axis_aligned();

    let mut quad_flags = QuadFlags::empty();

    let needs_scissor = !prim_is_2d_scale_translation;
    if !needs_scissor {
        quad_flags |= QuadFlags::APPLY_RENDER_TASK_CLIP;
    }

    let aa_flags = if prim_is_2d_axis_aligned {
        desc.aligned_aa_edges
    } else {
        desc.transformed_aa_edges
    };

    let local_bounds = desc.bounds
        .intersection_unchecked(&clips.local_clip_rect());
    let local_pattern_rect = desc.pattern_rect;

    // We round the coordinates of non-antialiased edges of the primitive.
    // This allows us to ensure that indirect axis-aligned primitives cover the render
    // task exactly. Since we do this for indirect primitives, we have to also do it for
    // other rendering strategies to avoid cracks between side-by-side primitives.
    let round_edges = !aa_flags;

    if let QuadRenderStrategy::Direct = strategy {
        if pattern.is_opaque {
            quad_flags |= QuadFlags::IS_OPAQUE;
        }

        let quad = create_quad_primitive(
            &local_bounds,
            &local_pattern_rect,
            &DeviceRect::max_rect(),
            transform.as_2d_scale_offset(),
            round_edges,
            pattern,
        );

        let main_prim_address = frame_state.frame_gpu_data.f32.push(&quad);

        let device_bounds = match transform.as_2d_scale_offset() {
            // The exact device-space footprint of this quad. `local_bounds` is
            // the quad's own coverage rect, so a primitive split into several
            // quads gets a tight rect for each one rather than sharing the
            // primitive's.
            Some(local_to_device) => local_to_device.map_rect(&local_bounds),
            // Not axis-aligned in device space, so there is no tight rect to
            // derive: bound the primitive's whole coverage rect.
            None => clips.coverage_rect(),
        };

        // Only use AA edge instances if the drawn area is large enough to require it.
        // A batch with an inline backdrop readback must hold a single instance, so
        // patterns that need one can't be split into AA edge instances.
        if can_use_aa_segments(&device_bounds, &local_bounds, pattern.kind) {
            quad_flags |= QuadFlags::USE_AA_SEGMENTS;
        }

        // Render the primitive as a single instance. Coordinates are provided to the
        // shader in layout space.
        frame_state.push_prim(
            &PrimitiveCommand::quad(
                pattern.kind,
                pattern.shader_input,
                pattern.texture_input.task_ids,
                device_bounds,
                main_prim_address,
                transform_id,
                quad_flags,
                aa_flags,
                pattern.blend_mode,
            ),
            transform.prim_spatial_node_index(),
            targets,
        );

        // If the pattern samples from one or more textures, add them as
        // dependencies of the surface we're drawing directly on to.
        for task_id in pattern.texture_input.task_ids {
            if task_id != RenderTaskId::INVALID {
                frame_state
                    .surface_builder
                    .add_child_render_task(task_id, frame_state.rg_builder);
            }
        }

        return;
    }

    // Rounding is important here because clipped_surface_rect.min may be used as the origin
    // of render tasks. Fractional values would introduce fractional offsets in the render tasks.
    let mut clipped_surface_rect = clips.coverage_rect()
        .intersection_unchecked(&clips.surface_clip_rect())
        .round();

    if let Some(t) = transform.as_2d_scale_offset() {
        let prim_surface_rect = t.map_rect(&local_bounds);
        // Snap the rectangle to the pixel grid...
        let prim_surface_rect = round_edges.select(
            // ... to the nearest pixel for non-antialiased edges.
            prim_surface_rect.round(),
            // ... conservatively for antialiased edges so that the AA
            // is included.
            prim_surface_rect.round_out()
        );
        clipped_surface_rect = clipped_surface_rect.intersection_unchecked(
            &prim_surface_rect,
        );
    }


    if clipped_surface_rect.is_empty() {
        return;
    }

    // Only use AA edge instances if the drawn area is large enough to require it.
    if clipped_surface_rect.width() > MIN_AA_SEGMENTS_SIZE
        && clipped_surface_rect.height() > MIN_AA_SEGMENTS_SIZE
        && local_bounds.width() > MIN_AA_SEGMENTS_SIZE
        && local_bounds.height() > MIN_AA_SEGMENTS_SIZE {
        quad_flags |= QuadFlags::USE_AA_SEGMENTS;
    }

    match strategy {
        QuadRenderStrategy::Direct => {}
        QuadRenderStrategy::Indirect => {
            let (local_bounds, aa_flags) = clips.adjust_border_clip_coverage(
                local_bounds, aa_flags, transform, &clipped_surface_rect,
            );
            let Some(task_id) = prepare_indirect_pattern(
                transform.prim_spatial_node_index(),
                transform.raster_spatial_node_index(),
                &local_bounds,
                &local_pattern_rect,
                &clipped_surface_rect,
                transform.as_2d_scale_offset(),
                transform.device_pixel_scale(),
                transform_id,
                pattern,
                quad_flags,
                aa_flags,
                cache_key,
                Some(clips),
                spatial_tree,
                frame_state,
            ) else {
                return;
            };

            add_composite_prim(
                pattern.blend_mode,
                &clipped_surface_rect,
                frame_state,
                targets,
                &[QuadSegment { rect: clipped_surface_rect.to_untyped(), task_id }],
            );
        }
        QuadRenderStrategy::Tiled => {
            prepare_tiles(
                &local_bounds,
                &local_pattern_rect,
                &clipped_surface_rect,
                pattern,
                quad_flags,
                aa_flags,
                clips,
                transform_id,
                transform,
                spatial_tree,
                frame_state,
                scratch,
                targets,
            );
        }
        QuadRenderStrategy::NinePatch { clip_rect, radius } => {
            prepare_nine_patch(
                &local_bounds,
                &local_pattern_rect,
                &clipped_surface_rect,
                &clip_rect,
                radius,
                pattern,
                quad_flags,
                aa_flags,
                clips,
                transform,
                transform_id,
                spatial_tree,
                frame_state,
                scratch,
                targets,
            );
        }
    }
}

fn prepare_indirect_pattern(
    prim_spatial_node_index: SpatialNodeIndex,
    raster_spatial_node_index: SpatialNodeIndex,
    local_bounds: &LayoutRect,
    local_pattern_rect: &LayoutRect,
    clipped_surface_rect: &DeviceRect,
    local_to_device_scale_offset: Option<&ScaleOffset>,
    device_pixel_scale: DevicePixelScale,
    transform_id: GpuTransformId,
    pattern: &Pattern,
    mut quad_flags: QuadFlags,
    aa_flags: EdgeMask,
    cache_key: &Option<QuadCacheKey>,
    clips: Option<&QuadClipStack>,
    spatial_tree: &SpatialTree,
    frame_state: &mut FrameBuildingState,
) -> Option<RenderTaskId> {
    let round_edges = !aa_flags;
    let quad = create_quad_primitive(
        local_bounds,
        local_pattern_rect,
        clipped_surface_rect,
        local_to_device_scale_offset,
        round_edges,
        pattern,
    );

    let main_prim_address = frame_state.frame_gpu_data.f32.push(&quad);

    let mut clipped_surface_rect = *clipped_surface_rect;
    if local_to_device_scale_offset.is_some() && aa_flags.is_empty() {
        // If the primitive has a simple transform, then quad.bounds is in device
        // space and is a subset of clipped_surface_rect (the clip rect is folded
        // into the bounds). If there is no anti-aliasing, and the pattern is
        // opaque, we want to ensure that the primitive covers the entire render
        // task so that we can safely skip clearing it.
        // In this situation, create_quad_primitive has rounded the edges of
        // quad.bounds so we are not introducing a fractional offset in
        // clipped_surface_rect.
        clipped_surface_rect = quad.bounds.cast_unit();
    }

    let task_size = clipped_surface_rect.size().try_cast::<i32>()?;
    if task_size.is_empty() {
        return None;
    }

    let cache_key = cache_key.as_ref().map(|key| {
        RenderTaskCacheKey {
            origin: clipped_surface_rect.min.to_i32(),
            size: task_size,
            kind: RenderTaskCacheKeyKind::Quad(key.clone()),
        }
    });

    if pattern.is_opaque {
        quad_flags |= QuadFlags::IS_OPAQUE;
    }

    let needs_scissor = local_to_device_scale_offset.is_none();

    Some(add_render_task_with_mask(
        &pattern,
        local_bounds,
        task_size,
        clipped_surface_rect.min,
        clips,
        prim_spatial_node_index,
        raster_spatial_node_index,
        main_prim_address,
        transform_id,
        aa_flags,
        quad_flags,
        device_pixel_scale,
        needs_scissor,
        cache_key.as_ref(),
        spatial_tree,
        frame_state,
    ))
}

fn prepare_nine_patch(
    local_bounds: &LayoutRect,
    local_pattern_rect: &LayoutRect,
    clipped_surface_rect: &DeviceRect,
    ninepatch_rect: &LayoutRect,
    radius: LayoutVector2D,
    pattern: &Pattern,
    mut quad_flags: QuadFlags,
    aa_flags: EdgeMask,
    clips: &QuadClipStack,
    transform: &mut QuadTransformState,
    gpu_transform: GpuTransformId,
    spatial_tree: &SpatialTree,
    frame_state: &mut FrameBuildingState,
    scratch: &mut PrimitiveScratchBuffer,
    targets: &[CommandBufferIndex],
) {
    // Render the primtive as a nine-patch decomposed in device space.
    // Nine-patch segments that need it are drawn in a render task and then composited into the
    // destination picture.

    let local_to_device = transform.as_2d_scale_offset().unwrap();
    let mut device_bounds: DeviceRect = local_to_device.map_rect(local_bounds);
    let device_pattern_rect: DeviceRect = local_to_device.map_rect(local_pattern_rect);

    let rounded_edges = !aa_flags;
    device_bounds = rounded_edges.select(
        device_bounds.round(),
        device_bounds,
    ).intersection_unchecked(clipped_surface_rect);

    let local_corner_0 = LayoutRect::new(
        ninepatch_rect.min,
        ninepatch_rect.min + radius,
    );

    let local_corner_1 = LayoutRect::new(
        ninepatch_rect.max - radius,
        ninepatch_rect.max,
    );

    let surface_rect_0: DeviceRect = local_to_device
        .map_rect(&local_corner_0)
        .round_out();
    let surface_rect_1: DeviceRect = local_to_device
        .map_rect(&local_corner_1)
        .round_out();

    let p0 = surface_rect_0.min;
    let p1 = surface_rect_0.max;
    let p2 = surface_rect_1.min;
    let p3 = surface_rect_1.max;

    let mut x_coords = [p0.x, p1.x, p2.x, p3.x];
    let mut y_coords = [p0.y, p1.y, p2.y, p3.y];

    x_coords.sort_by(|a, b| a.partial_cmp(b).unwrap());
    y_coords.sort_by(|a, b| a.partial_cmp(b).unwrap());

    scratch.frame.quad_direct_segments.clear();
    scratch.frame.quad_indirect_segments.clear();

    // TODO: re-land clip-out mode.
    let mode = ClipMode::Clip;

    if pattern.is_opaque {
        quad_flags |= QuadFlags::IS_OPAQUE;
    }

    fn should_create_task(mode: ClipMode, x: usize, y: usize) -> bool {
        match mode {
            // Only create render tasks for the corners.
            ClipMode::Clip => x != 1 && y != 1,
            // Create render tasks for all segments (the
            // center will be skipped).
            ClipMode::ClipOut => true,
        }
    }

    let indirect_prim_address = write_device_prim_blocks(
        &mut frame_state.frame_gpu_data.f32,
        &device_bounds,
        &device_pattern_rect,
        pattern.base_color,
        pattern.texture_input.task_id(),
        &[],
        local_to_device.inverse(),
    );

    for y in 0 .. y_coords.len()-1 {
        let y0 = y_coords[y];
        let y1 = y_coords[y+1];

        if y1 <= y0 {
            continue;
        }

        for x in 0 .. x_coords.len()-1 {
            if mode == ClipMode::ClipOut && x == 1 && y == 1 {
                continue;
            }

            let x0 = x_coords[x];
            let x1 = x_coords[x+1];

            if x1 <= x0 {
                continue;
            }

            let segment = DeviceRect::new(point2(x0, y0), point2(x1, y1));
            let segment_device_rect = match segment.intersection(&device_bounds) {
                Some(rect) => rect,
                None => {
                    continue;
                }
            };

            if should_create_task(mode, x, y) {
                // The segment rect can have a sub-pixel size where it meets the antialiased
                // bounds of the primitive; round up so the task is never empty
                // and covers the area that gets drawn.
                let Some(task_size) = segment_device_rect.size().ceil().try_cast::<i32>() else {
                    continue;
                };
                let task_id = add_render_task_with_mask(
                    pattern,
                    &local_bounds,
                    task_size,
                    segment_device_rect.min,
                    Some(clips),
                    transform.prim_spatial_node_index(),
                    transform.raster_spatial_node_index(),
                    indirect_prim_address,
                    gpu_transform,
                    aa_flags,
                    quad_flags,
                    transform.device_pixel_scale(),
                    false,
                    None,
                    spatial_tree,
                    frame_state,
                );
                scratch.frame.quad_indirect_segments.push(QuadSegment {
                    rect: segment_device_rect.to_f32().cast_unit(),
                    task_id,
                });
            } else {
                scratch.frame.quad_direct_segments.push(QuadSegment {
                    rect: segment_device_rect.to_f32().cast_unit(),
                    task_id: pattern.texture_input.task_id(),
                });
            };
        }
    }

    if !scratch.frame.quad_direct_segments.is_empty() {
        add_pattern_prim(
            pattern,
            local_to_device.inverse(),
            &device_bounds,
            &device_pattern_rect,
            pattern.is_opaque,
            frame_state,
            targets,
            &scratch.frame.quad_direct_segments,
        );
    }

    if !scratch.frame.quad_indirect_segments.is_empty() {
        add_composite_prim(
            pattern.blend_mode,
            &device_bounds,
            frame_state,
            targets,
            &scratch.frame.quad_indirect_segments,
        );
    }
}

fn prepare_tiles(
    local_bounds: &LayoutRect,
    local_pattern_rect: &LayoutRect,
    device_bounds: &DeviceRect,
    pattern: &Pattern,
    mut quad_flags: QuadFlags,
    aa_flags: EdgeMask,
    clips: &QuadClipStack,
    gpu_transform: GpuTransformId,
    transform: &mut QuadTransformState,
    spatial_tree: &SpatialTree,
    frame_state: &mut FrameBuildingState,
    scratch: &mut PrimitiveScratchBuffer,
    targets: &[CommandBufferIndex],
) {
    // Render the primtive as a grid of tiles decomposed in device space.
    // Tiles that need it are drawn in a render task and then composited into the
    // destination picture.
    // The coordinates are provided to the shaders:
    //  - in layout space for the render task,
    //  - in device space for the instances that draw into the destination picture.

    let unclipped_surface_rect = device_bounds.round_out();

    let force_masks = !transform.is_2d_scale_offset();
    // Set up the tile classifier for the params of this quad
    scratch.retained.quad_tile_classifier.reset(
        unclipped_surface_rect,
        force_masks,
    );

    let mut clip_to_raster = SpaceMapper::<LayoutPixel, RasterPixel>::new(
        transform.raster_spatial_node_index(),
        RasterRect::max_rect(),
    );

    let prim_spatial_node_index = transform.prim_spatial_node_index();

    // Walk each clip, extract the local mask regions and add them to the tile classifier.
    for clip in clips.clips() {
        // A clip outside the 3D context that established this surface's raster
        // root has no transform into raster space to ask for, so don't ask: the
        // spatial tree only builds transforms towards the root and would panic.
        // Such a clip takes the same conservative path as a non-axis-aligned one.
        let clip_to_raster_scale_offset = if spatial_tree.can_get_relative_transform(
            clip.spatial_node,
            transform.raster_spatial_node_index(),
        ) {
            clip_to_raster.set_target_spatial_node(clip.spatial_node, spatial_tree);
            clip_to_raster.as_2d_scale_offset()
        } else {
            None
        };

        let transform = match clip_to_raster_scale_offset {
            Some(t) => t.then_scale(transform.device_pixel_scale.0),
            None => {
                // If the clip transform is not axis-aligned, just assume the entire primitive
                // is affected by the clip, for now.
                // TODO: If we take this path, it means that we would have been better-off using
                // the indirect rendering strategy.
                scratch.retained.quad_tile_classifier.add_mask_region(unclipped_surface_rect);
                continue;
            }
        };

        // A rect clip in the same coordinate system as the primitive is folded
        // into the local clip rect and applied directly by the pattern shader, so
        // tiles straddling its boundary don't need a clip mask.
        let applied_as_local_clip = spatial_tree.is_matching_coord_system(
            prim_spatial_node_index,
            clip.spatial_node,
        );

        // Add regions to the classifier depending on the clip kind
        match clip.shape {
            QuadClipShape::Rectangle { mode } => {
                let rect = transform.map_rect(&clip.rect);
                scratch.retained.quad_tile_classifier.add_clip_rect(rect, mode, applied_as_local_clip);
            }
            QuadClipShape::RoundedRectangle { mode: ClipMode::Clip, ref radius, ref inset } => {
                // For rounded-rects with Clip mode, we need a mask for each corner,
                // and to add the clip rect itself (to cull tiles outside that rect)

                // Map the local rect and radii
                let radius = clamped_radius(radius, clip.rect.size());
                let clip_device_rect = transform.map_rect(&clip.rect);
                // If the transform has a negative scale, the rect will be correctly
                // flipped by the transform so that it isn't empty, but the sizes will
                // be negative. Make sure that the size stay positive.
                let r_tl = transform.map_size(&radius.top_left).abs();
                let r_tr = transform.map_size(&radius.top_right).abs();
                let r_br = transform.map_size(&radius.bottom_right).abs();
                let r_bl = transform.map_size(&radius.bottom_left).abs();

                let inset: SideOffsets2D<f32, DevicePixel> = transform.map_side_offsets(&inset);

                let i_tl = if radius.shape_top_left < 1.0 {
                    Size2D::new(inset.top, inset.left)
                } else {
                    Size2D::zero()
                };
                let i_tr = if radius.shape_top_right < 1.0 {
                    Size2D::new(inset.top, inset.right)
                } else {
                    Size2D::zero()
                };
                let i_br = if radius.shape_bottom_right < 1.0 {
                    Size2D::new(inset.bottom, inset.right)
                } else {
                    Size2D::zero()
                };
                let i_bl = if radius.shape_bottom_left < 1.0 {
                    Size2D::new(inset.bottom, inset.left)
                } else {
                    Size2D::zero()
                };

                // Construct the mask regions for each corner
                let c_tl = DeviceRect::from_origin_and_size(
                    clip_device_rect.min,
                    r_tl + i_tl,
                );
                let c_tr = DeviceRect::from_origin_and_size(
                    DevicePoint::new(
                        clip_device_rect.max.x - r_tr.width,
                        clip_device_rect.min.y,
                    ),
                    r_tr + i_tr,
                );
                let c_br = DeviceRect::from_origin_and_size(
                    DevicePoint::new(
                        clip_device_rect.max.x - r_br.width,
                        clip_device_rect.max.y - r_br.height,
                    ),
                    r_br + i_br,
                );
                let c_bl = DeviceRect::from_origin_and_size(
                    DevicePoint::new(
                        clip_device_rect.min.x,
                        clip_device_rect.max.y - r_bl.height,
                    ),
                    r_bl + i_bl,
                );

                scratch.retained.quad_tile_classifier.add_clip_rect(clip_device_rect, ClipMode::Clip, applied_as_local_clip);
                scratch.retained.quad_tile_classifier.add_mask_region(c_tl);
                scratch.retained.quad_tile_classifier.add_mask_region(c_tr);
                scratch.retained.quad_tile_classifier.add_mask_region(c_br);
                scratch.retained.quad_tile_classifier.add_mask_region(c_bl);
            }
            QuadClipShape::RoundedRectangle { mode: ClipMode::ClipOut, ref radius , ref inset } => {
                let radius = clamped_radius(radius, clip.rect.size());
                // Try to find an inner rect within the clip-out rounded rect that we can
                // use to cull inner tiles. If we can't, the entire rect needs to be masked
                match extract_inner_rect_k(&clip.rect, &radius, &inset, 0.5) {
                    Some(ref inner_rect) => {
                        let rect = transform.map_rect(inner_rect);
                        scratch.retained.quad_tile_classifier.add_clip_rect(rect, ClipMode::ClipOut, false);
                    }
                    None => {
                        let clip_device_rect = transform.map_rect(&clip.rect);
                        scratch.retained.quad_tile_classifier.add_mask_region(clip_device_rect);
                    }
                }
            }
            QuadClipShape::Path { ref path, origin, .. } => {
                // Nothing is visible outside of the path's bounds, and
                // anything inside of them may need a mask.
                let path_rect = path.aabb().translate(origin.to_vector());
                let rect = clip.rect.intersection(&path_rect).unwrap_or(clip.rect);
                let device_rect = transform.map_rect(&rect);
                scratch.retained.quad_tile_classifier.add_clip_rect(device_rect, ClipMode::Clip, false);
                scratch.retained.quad_tile_classifier.add_mask_region(device_rect);
            }
            QuadClipShape::Border { .. } => unreachable!(),
            QuadClipShape::Mask { .. } => {
                panic!("bug: image clips unexpected in this path");
            }
        }
    }

    let indirect_prim_address = write_prim_blocks(
        &mut frame_state.frame_gpu_data.f32,
        local_bounds,
        local_pattern_rect,
        device_bounds,
        transform.as_2d_scale_offset(),
        !aa_flags,
        pattern,
    );

    // Classify each tile within the quad to be Pattern / Mask / Clipped
    scratch.frame.quad_direct_segments.clear();
    scratch.frame.quad_indirect_segments.clear();

    let tiles = scratch.retained.quad_tile_classifier.classify();
    for tile in tiles {
        // Check whether this tile requires a mask
        let is_direct = match tile.kind {
            QuadTileKind::Clipped => {
                // Note: We shouldn't take this branch since clipped tiles are
                // filtered out by the iterator.
                continue;
            }
            QuadTileKind::Pattern { has_mask } => !has_mask,
        };

        // At extreme scales the rect can round to zero size due to
        // f32 precision, causing a panic in new_dynamic, so just
        // skip segments that would produce zero size tasks.
        // https://bugzilla.mozilla.org/show_bug.cgi?id=1941838#c13
        let Some(tile_size) = tile.rect.size().try_cast::<i32>() else {
            continue;
        };
        if tile_size.is_empty() {
            continue;
        }

        if is_direct {
            scratch.frame.quad_direct_segments.push(QuadSegment {
                rect: tile.rect.cast_unit(),
                task_id: RenderTaskId::INVALID
            });
        } else {
            if pattern.is_opaque {
                quad_flags |= QuadFlags::IS_OPAQUE;
            }

            let needs_scissor = !transform.is_2d_scale_offset();
            let task_id = add_render_task_with_mask(
                pattern,
                local_bounds,
                tile_size,
                tile.rect.min,
                Some(clips),
                transform.prim_spatial_node_index(),
                transform.raster_spatial_node_index(),
                indirect_prim_address,
                gpu_transform,
                aa_flags,
                quad_flags,
                transform.device_pixel_scale(),
                needs_scissor,
                None,
                spatial_tree,
                frame_state,
            );

            scratch.frame.quad_indirect_segments.push(QuadSegment {
                rect: tile.rect.cast_unit(),
                task_id,
            });
        }
    }

    if !scratch.frame.quad_direct_segments.is_empty() {
        // Tile segments are only allowed for axis-aligned primitives.
        let local_to_device = transform.as_2d_scale_offset().unwrap();

        let device_pattern_rect: DeviceRect = local_to_device.map_rect(&local_pattern_rect);

        if pattern.texture_input.task_id() != RenderTaskId::INVALID {
            for segment in &mut scratch.frame.quad_direct_segments {
                segment.task_id = pattern.texture_input.task_id();
            }
        }

        add_pattern_prim(
            pattern,
            local_to_device.inverse(),
            &device_bounds,
            &device_pattern_rect,
            pattern.is_opaque,
            frame_state,
            targets,
            &scratch.frame.quad_direct_segments,
        );
    }

    if !scratch.frame.quad_indirect_segments.is_empty() {
        add_composite_prim(
            pattern.blend_mode,
            device_bounds,
            frame_state,
            targets,
            &scratch.frame.quad_indirect_segments,
        );
    }
}

fn get_prim_render_strategy(
    prim_spatial_node_index: SpatialNodeIndex,
    clips: &QuadClipStack,
    prim_is_scale_offset: bool,
    spatial_tree: &SpatialTree,
) -> QuadRenderStrategy {
    if clips.clips().iter().any(|clip| matches!(clip.shape, QuadClipShape::Border { .. })) {
        return QuadRenderStrategy::Indirect;
    }
    if !clips.needs_mask() {
        return QuadRenderStrategy::Direct
    }

    // Both the nine-patch and tiled paths rely on axis-aligned primitive for now.
    // In the case of nine-patch this is currently a hard requirement, while the
    // tiling path works with non-axis-aligned primitives but less efficiently than
    // the indirect path since all tiles end up treated as masks.
    let try_split_prim = if prim_is_scale_offset {
        // TODO: the coverage rect bounds the whole primitive. A tighter rect
        // would split fewer primitives that only need it in part.
        let size = clips.coverage_rect().size();
        size.width > MIN_QUAD_SPLIT_SIZE || size.height > MIN_QUAD_SPLIT_SIZE
    } else {
        false
    };

    if !try_split_prim {
        return QuadRenderStrategy::Indirect;
    }

    if prim_is_scale_offset && clips.len() == 1 {
        let clip = &clips.clips()[0];

        if let QuadClipShape::RoundedRectangle { ref radius, ref inset, mode: ClipMode::Clip } = clip.shape {
            let size = clip.rect.size();
            let radius = clamped_radius(radius, size);
            let max_corner_width = radius.top_left.width
                                        .max(radius.bottom_left.width)
                                        .max(radius.top_right.width)
                                        .max(radius.bottom_right.width);
            let max_corner_height = radius.top_left.height
                                        .max(radius.bottom_left.height)
                                        .max(radius.top_right.height)
                                        .max(radius.bottom_right.height);
            
            let mut max_inset_width = 0.0f32;
            let mut max_inset_height = 0.0f32;
            if radius.shape_top_left < 1.0 {
                max_inset_width = max_inset_width.max(inset.top);
                max_inset_height = max_inset_height.max(inset.left);
            }
            if radius.shape_top_right < 1.0 {
                max_inset_width = max_inset_width.max(inset.top);
                max_inset_height = max_inset_height.max(inset.right);
            }
            if radius.shape_bottom_right < 1.0 {
                max_inset_width = max_inset_width.max(inset.bottom);
                max_inset_height = max_inset_height.max(inset.right);
            }
            if radius.shape_bottom_left < 1.0 {
                max_inset_width = max_inset_width.max(inset.bottom);
                max_inset_height = max_inset_height.max(inset.left);
            }

            if (max_corner_width + max_inset_width) <= 0.5 * size.width &&
                (max_corner_height + max_inset_height) <= 0.5 * size.height {

                let clip_prim_coords_match = spatial_tree.is_matching_coord_system(
                    prim_spatial_node_index,
                    clip.spatial_node,
                );

                if clip_prim_coords_match {
                    let map_clip_to_prim = SpaceMapper::new_with_target(
                        prim_spatial_node_index,
                        clip.spatial_node,
                        LayoutRect::max_rect(),
                        spatial_tree,
                    );

                    if let Some(clip_rect) = map_clip_to_prim.map(&clip.rect) {
                        // The two spaces can be flipped with respect to one another,
                        // in which case the mapped vector has negative components.
                        // The nine-patch decomposition needs positive corner extents.
                        let radius = map_clip_to_prim.map_vector(
                            LayoutVector2D::new(max_corner_width + max_inset_width, max_corner_height + max_inset_height)
                        ).abs();
                        return QuadRenderStrategy::NinePatch {
                            radius,
                            clip_rect,
                        };
                    }
                }
            }
        }
    }

    QuadRenderStrategy::Tiled
}

/// Adjust the transform and device rect until the latter fits the provided
/// maximum size.
/// Also ensure that near-zero size tasks do are at least
fn adjust_indirect_pattern_resolution(
    pattern_rect: &LayoutRect,
    max_device_size: f32,
    device_rect: &mut DeviceRect,
    indirect_transform: &mut ScaleOffset,
) {
    // This catches invalid cases such as NaNs or zeroes that would have caused us
    // to loop forever.
    let valid = pattern_rect.width() > 0.0
        && pattern_rect.height() > 0.0
        && indirect_transform.scale.x != 0.0
        && indirect_transform.scale.y != 0.0;

    if !valid {
        return;
    }

    // Down-scale until the render task fits in the provided maximum size.
    while device_rect.width() > max_device_size {
        indirect_transform.scale.x *= 0.5;
        *device_rect = indirect_transform.map_rect(pattern_rect);
    }
    while device_rect.height() > max_device_size {
        indirect_transform.scale.y *= 0.5;
        *device_rect = indirect_transform.map_rect(pattern_rect);
    }

    // Up-scale until the render task size rounds to at least one pixel.
    while device_rect.width() <= 0.5 {
        indirect_transform.scale.x *= 2.0;
        *device_rect = indirect_transform.map_rect(pattern_rect);
    }
    while device_rect.height() <= 0.5 {
        indirect_transform.scale.y *= 2.0;
        *device_rect = indirect_transform.map_rect(pattern_rect);
    }
}

pub fn cache_key(
    prim_uid: ItemUid,
    transform: &QuadTransformState,
    clips: &QuadClipStack,
) -> Option<QuadCacheKey> {
    const CACHE_MAX_CLIPS: usize = 3;

    if clips.clips().len() >= CACHE_MAX_CLIPS {
        return None;
    }

    let prim_spatial_node_index = transform.prim_spatial_node_index();
    // The assumption is here is that the vast majority of transforms
    // are 2d scale offsets and that 3d ones tend to be animated, so
    // in order to keep the key small, we only attempt to cache when
    // the transform is a 2d scale offset.
    // This will miss some caching opportunities, but they should
    // hopefully be rare.
    let Some(transform) = transform.as_2d_scale_offset() else {
        return None;
    };

    let mut clip_uids = [!0; CACHE_MAX_CLIPS];

    for (i, clip) in clips.clips().iter().enumerate() {
        clip_uids[i] = clip.uid;
        if clip.spatial_node != prim_spatial_node_index {
            return None;
        }
    }

    Some(QuadCacheKey {
        prim: prim_uid.get_uid(),
        clips: clip_uids,
        transform: [
            transform.scale.x.to_bits(),
            transform.scale.y.to_bits(),
            transform.offset.x.to_bits(),
            transform.offset.y.to_bits(),
        ],
    })
}

fn add_render_task_with_mask(
    pattern: &Pattern,
    local_bounds: &LayoutRect,
    task_size: DeviceIntSize,
    content_origin: DevicePoint,
    clips: Option<&QuadClipStack>,
    prim_spatial_node_index: SpatialNodeIndex,
    raster_spatial_node_index: SpatialNodeIndex,
    prim_address_f: GpuBufferAddress,
    transform_id: GpuTransformId,
    aa_flags: EdgeMask,
    quad_flags: QuadFlags,
    device_pixel_scale: DevicePixelScale,
    needs_scissor_rect: bool,
    cache_key: Option<&RenderTaskCacheKey>,
    spatial_tree: &SpatialTree,
    frame_state: &mut FrameBuildingState,
) -> RenderTaskId {
    let transforms = &mut frame_state.transforms;
    let cmd_buffers = &mut frame_state.cmd_buffers;
    let clips = clips.filter(|clips| !clips.is_empty());
    let is_opaque = pattern.is_opaque && clips.is_none();
    frame_state.resource_cache.request_render_task(
        cache_key.cloned(),
        is_opaque,
        RenderTaskParent::Surface,
        &mut frame_state.frame_gpu_data.f32,
        frame_state.rg_builder,
        &mut frame_state.surface_builder,
        &mut|rg_builder, gpu_buffer| {
            let task_id = rg_builder.add().init(RenderTask::new_dynamic(
                task_size,
                RenderTaskKind::new_prim(
                    pattern.kind,
                    pattern.shader_input,
                    content_origin,
                    prim_address_f,
                    transform_id,
                    aa_flags,
                    quad_flags,
                    needs_scissor_rect,
                    pattern.texture_input.task_ids,
                ),
            ));

            // If the pattern samples from one or more textures, add them as
            // dependencies of the indirect render task that relies on them.
            for input_task_id in pattern.texture_input.task_ids {
                if input_task_id != RenderTaskId::INVALID {
                    rg_builder.add_dependency(task_id, input_task_id);
                }
            }

            if let Some(clips) = clips {
                let task_rect = DeviceRect::from_origin_and_size(
                    content_origin,
                    task_size.to_f32(),
                );

                prepare_clip_range(
                    clips,
                    task_id,
                    &task_rect,
                    local_bounds,
                    prim_spatial_node_index,
                    raster_spatial_node_index,
                    device_pixel_scale,
                    spatial_tree,
                    rg_builder,
                    gpu_buffer,
                    transforms,
                    cmd_buffers,
                );
            }

            task_id
        }
    )
}

fn add_pattern_prim(
    pattern: &Pattern,
    pattern_transform: ScaleOffset,
    coverage_rect: &DeviceRect,
    pattern_rect: &DeviceRect,
    is_opaque: bool,
    frame_state: &mut FrameBuildingState,
    targets: &[CommandBufferIndex],
    segments: &[QuadSegment],
) {
    let prim_address = write_prim_blocks_impl(
        &mut frame_state.frame_gpu_data.f32,
        coverage_rect.to_untyped(),
        pattern_rect.to_untyped(),
        pattern.base_color,
        pattern.texture_input.task_id(),
        segments,
        pattern_transform,
    );

    frame_state.set_segments(segments, targets);

    let mut quad_flags = QuadFlags::APPLY_RENDER_TASK_CLIP;

    if is_opaque {
        quad_flags |= QuadFlags::IS_OPAQUE;
    }

    frame_state.push_cmd(
        &PrimitiveCommand::quad(
            pattern.kind,
            pattern.shader_input,
            pattern.texture_input.task_ids,
            *coverage_rect,
            prim_address,
            GpuTransformId::IDENTITY,
            quad_flags,
            // TODO(gw): No AA on composite, unless we use it to apply 2d clips
            EdgeMask::empty(),
            pattern.blend_mode,
        ),
        targets,
    );
}

fn add_composite_prim(
    blend_mode: BlendMode,
    rect: &DeviceRect,
    frame_state: &mut FrameBuildingState,
    targets: &[CommandBufferIndex],
    segments: &[QuadSegment],
) {
    assert!(!segments.is_empty());

    // Note: At the primitive level we specify an invalid task ID here, which
    // may look suspicious since we are using the textured shader. However each
    // segment comes with its own render task id, and that's what the batching
    // code uses.

    let composite_prim_address = write_device_prim_blocks(
        &mut frame_state.frame_gpu_data.f32,
        rect,
        rect,
        ColorF::WHITE,
        RenderTaskId::INVALID,
        segments,
        ScaleOffset::identity(),
    );

    frame_state.set_segments(segments, targets);

    let quad_flags = QuadFlags::APPLY_RENDER_TASK_CLIP;

    frame_state.push_cmd(
        &PrimitiveCommand::quad(
            PatternKind::ColorOrTexture,
            PatternShaderInput(
                crate::pattern::TEXTURED_SHADER_MODE_TEXTURE,
                crate::pattern::TEXTURED_SHADER_MAP_TO_SEGMENT,
            ),
            [RenderTaskId::INVALID; 3],
            *rect,
            composite_prim_address,
            GpuTransformId::IDENTITY,
            quad_flags,
            // TODO(gw): No AA on composite, unless we use it to apply 2d clips
            EdgeMask::empty(),
            blend_mode,
        ),
        targets,
    );
}

pub fn prepare_clip_range(
    clips: &QuadClipStack,
    masked_prim_task_id: RenderTaskId,
    task_rect: &DeviceRect,
    prim_local_coverage_rect: &LayoutRect,
    prim_spatial_node_index: SpatialNodeIndex,
    raster_spatial_node_index: SpatialNodeIndex,
    device_pixel_scale: DevicePixelScale,
    spatial_tree: &SpatialTree,
    rg_builder: &mut RenderTaskGraphBuilder,
    gpu_buffer: &mut GpuBufferBuilderF,
    transforms: &mut TransformPalette,
    cmd_buffers: &mut CommandBufferList,
) {
    let mut sub_tasks = rg_builder.begin_sub_tasks();

    for clip in clips.clips() {
        prepare_clip_task(
            clip,
            masked_prim_task_id,
            clips.mask_tiles(clip),
            task_rect,
            prim_local_coverage_rect,
            prim_spatial_node_index,
            raster_spatial_node_index,
            device_pixel_scale,
            spatial_tree,
            gpu_buffer,
            transforms,
            rg_builder,
            &mut sub_tasks,
            cmd_buffers,
        );
    }

    rg_builder
        .get_task_mut(masked_prim_task_id)
        .set_sub_tasks(sub_tasks);
}

/// Write the GPU buffer blocks describing a (rounded) rectangle clip, in the
/// format consumed by ps_quad_mask. Returns the clip's address along with
/// whether the uniform-radius fast path can be used.
pub fn write_rounded_rect_clip_blocks(
    gpu_buffer: &mut GpuBufferBuilderF,
    clip_rect: LayoutRect,
    radius: &BorderRadius,
    inset: LayoutSideOffsets,
    mode: ClipMode,
) -> (GpuBufferAddress, bool, bool) {
    let radius = clamped_radius(radius, clip_rect.size());

    if radius.can_use_fast_path_in(&clip_rect) {
        let mut writer = gpu_buffer.write_blocks(3);
        writer.push_one(clip_rect);
        writer.push_one([
            radius.bottom_right.width,
            radius.top_right.width,
            radius.bottom_left.width,
            radius.top_left.width,
        ]);
        writer.push_one([mode as i32 as f32, 0.0, 0.0, 0.0]);

        (writer.finish(), true, false)
    } else {
        let superellipse = !radius.shapes_all_round();
        let block_count = if superellipse { 6 } else { 4 };
        let mut writer = gpu_buffer.write_blocks(block_count);
        writer.push_one(clip_rect);
        writer.push_one([
            radius.top_left.width,
            radius.top_left.height,
            radius.top_right.width,
            radius.top_right.height,
        ]);
        writer.push_one([
            radius.bottom_left.width,
            radius.bottom_left.height,
            radius.bottom_right.width,
            radius.bottom_right.height,
        ]);
        writer.push_one([mode as i32 as f32, 0.0, 0.0, 0.0]);
        if superellipse {
            writer.push_one([
                radius.shape_top_left,
                radius.shape_top_right,
                radius.shape_bottom_right,
                radius.shape_bottom_left,
            ]);
            writer.push_one(inset);
        }

        (writer.finish(), false, superellipse)
    }
}

fn prepare_border_mask(
    clip: &QuadClip,
    border: api::NormalBorder,
    widths: LayoutSideOffsets,
    task_rect: &DeviceRect,
    raster_spatial_node_index: SpatialNodeIndex,
    device_pixel_scale: DevicePixelScale,
    spatial_tree: &SpatialTree,
    gpu_buffer: &mut GpuBufferBuilderF,
    rg_builder: &mut RenderTaskGraphBuilder,
    cmd_buffers: &mut CommandBufferList,
    transforms: &mut TransformPalette,
) -> (DeviceRect, RenderTaskId) {
    use crate::prim_store::borders::BorderRasterization;
    use crate::render_task_cache::to_cache_size;

    let mut transform = QuadTransformState::new();
    transform.set(clip.spatial_node, raster_spatial_node_index, spatial_tree, device_pixel_scale);
    let raster = BorderRasterization::new(clip.rect, border, widths, &transform, true, spatial_tree);
    let mut segment_scale = raster.scale;
    let device_rect = task_rect.round_out();
    let mask_rect = transform.unmap_rect(&device_rect)
        .unwrap_or(clip.rect)
        .inflate(2.0 / raster.scale.0, 2.0 / raster.scale.0);
    let size = device_rect.size().to_i32();
    let transform_id = if transform.is_2d_scale_offset() {
        GpuTransformId::IDENTITY
    } else {
        transforms.gpu.get_id_with_post_scale(
            clip.spatial_node, raster_spatial_node_index, device_pixel_scale.get(), spatial_tree,
        )
    };
    let quad_flags = if transform.is_2d_scale_offset() {
        QuadFlags::APPLY_RENDER_TASK_CLIP
    } else {
        QuadFlags::empty()
    };
    let cmd_buffer = cmd_buffers.create_cmd_buffer();
    let task_id = rg_builder.add().init(RenderTask::new_dynamic(
        size,
        RenderTaskKind::new_picture(
            size, false, device_rect.min, raster_spatial_node_index, raster_spatial_node_index,
            device_pixel_scale, None, None, Some(ColorF::TRANSPARENT),
            cmd_buffer, false, None,
        ),
    ));

    for segment in &raster.segments {
        let visible_bounds = segment.bounds(&mask_rect);
        if visible_bounds.is_empty() {
            continue;
        }
        let (mut pattern, stretch_size, spacing) = if segment.is_solid.is_some() {
            (Pattern::color(ColorF::WHITE), segment.pattern_rect.size(), LayoutSize::zero())
        } else {
            let cache_size = to_cache_size(segment.task_size, &mut segment_scale);
            let src_task = rg_builder.add().init(segment.render_task(
                cache_size, &raster.border, segment_scale, gpu_buffer,
            ));
            rg_builder.add_dependency(task_id, src_task);
            let (stretch_size, spacing) = segment.repetition(cache_size);
            let mut pattern = Pattern::texture(src_task, false);
            pattern.shader_input.0 = crate::pattern::TEXTURED_SHADER_MODE_TEXTURE_ALPHA;
            (pattern, stretch_size, spacing)
        };
        if stretch_size.width <= 0.0 || stretch_size.height <= 0.0 {
            continue;
        }
        let needs_repetition = stretch_size.width < segment.pattern_rect.width()
            || stretch_size.height < segment.pattern_rect.height();
        let pattern_rect = if needs_repetition {
            pattern = crate::pattern::repeat::repeated_pattern(
                &RepeatedPattern {
                    stretch_size, spacing,
                    src_task_id: pattern.texture_input.task_id(),
                    src_is_opaque: false,
                },
                gpu_buffer,
            );
            segment.pattern_rect
        } else {
            LayoutRect::from_origin_and_size(segment.pattern_rect.min, stretch_size)
        };
        let aa_flags = if transform.is_2d_axis_aligned() {
            EdgeMask::empty()
        } else {
            segment.edge_flags
        };
        let prim = create_quad_primitive(
            &visible_bounds, &pattern_rect, &device_rect,
            transform.as_2d_scale_offset(), !aa_flags, &pattern,
        );
        let address = gpu_buffer.push(&prim);
        let device_bounds = transform.as_2d_scale_offset()
            .map(|t| t.map_rect(&visible_bounds))
            .unwrap_or(device_rect);
        let mut quad_flags = quad_flags;
        if can_use_aa_segments(&device_bounds, &visible_bounds, pattern.kind) {
            quad_flags |= QuadFlags::USE_AA_SEGMENTS;
        }
        cmd_buffers.get_mut(cmd_buffer).add_cmd(&PrimitiveCommand::quad(
            pattern.kind, pattern.shader_input, pattern.texture_input.task_ids,
            device_bounds, address, transform_id, quad_flags, aa_flags, pattern.blend_mode,
        ));
    }
    (device_rect, task_id)
}

fn prepare_clip_task(
    clip: &QuadClip,
    masked_prim_task_id: RenderTaskId,
    mask_tiles: &[QuadMaskTile],
    task_rect: &DeviceRect,
    prim_local_coverage_rect: &LayoutRect,
    prim_spatial_node_index: SpatialNodeIndex,
    raster_spatial_node_index: SpatialNodeIndex,
    device_pixel_scale: DevicePixelScale,
    spatial_tree: &SpatialTree,
    gpu_buffer: &mut GpuBufferBuilderF,
    transforms: &mut TransformPalette,
    rg_builder: &mut RenderTaskGraphBuilder,
    sub_tasks: &mut SubTaskRange,
    cmd_buffers: &mut CommandBufferList,
) {
    if let QuadClipShape::Border { key } = clip.shape {
        let (rect, task_id) = prepare_border_mask(
            clip, key.border(), LayoutSideOffsets::from_au(key.widths), task_rect,
            raster_spatial_node_index, device_pixel_scale, spatial_tree, gpu_buffer,
            rg_builder, cmd_buffers, transforms,
        );
        rg_builder.add_dependency(masked_prim_task_id, task_id);
        let quad_address = write_device_prim_blocks(
            gpu_buffer, &rect, &rect, ColorF::WHITE, RenderTaskId::INVALID,
            &[QuadSegment { rect: rect.to_untyped(), task_id }], ScaleOffset::identity(),
        );
        rg_builder.push_sub_task(sub_tasks, SubTask::ImageClip(ImageClipSubTask {
            quad_address,
            quad_transform_id: GpuTransformId::IDENTITY,
            src_task: task_id,
            quad_flags: QuadFlags::IS_MASK | QuadFlags::APPLY_RENDER_TASK_CLIP,
            needs_scissor_rect: false,
        }));
        return;
    }
    let (clip_address, fast_path, superellipse) = match clip.shape {
        QuadClipShape::RoundedRectangle { radius, inset, mode } => {
            write_rounded_rect_clip_blocks(
                gpu_buffer,
                clip.rect,
                &radius,
                inset,
                mode,
            )
        }
        QuadClipShape::Rectangle { mode, .. } => {
            let mut writer = gpu_buffer.write_blocks(3);
            writer.push_one(clip.rect);
            writer.push_one([0.0, 0.0, 0.0, 0.0]);
            writer.push_one([mode as i32 as f32, 0.0, 0.0, 0.0]);
            let clip_address = writer.finish();

            (clip_address, true, false)
        }
        QuadClipShape::Mask { .. } | QuadClipShape::Border { .. } => {
            let transform_id = transforms.gpu.get_id_with_post_scale(
                clip.spatial_node,
                raster_spatial_node_index,
                device_pixel_scale.get(),
                spatial_tree,
            );

            let is_scale_offset = transform_id.is_2d_scale_offset();
            let needs_scissor_rect = !is_scale_offset;

            let pattern = Pattern::color(ColorF::WHITE);
            let mut quad_flags = QuadFlags::IS_MASK;

            if is_scale_offset {
                quad_flags |= QuadFlags::APPLY_RENDER_TASK_CLIP;
            }

            for tile in mask_tiles {
                let prim_address = write_layout_prim_blocks(
                    gpu_buffer,
                    &tile.rect,
                    &tile.rect,
                    pattern.base_color,
                    pattern.texture_input.task_id(),
                    &[QuadSegment {
                        rect: tile.rect.to_untyped(),
                        task_id: tile.task_id,
                    }],
                );

                rg_builder.push_sub_task(
                    sub_tasks,
                    SubTask::ImageClip(ImageClipSubTask {
                        quad_address: prim_address,
                        quad_transform_id: transform_id,
                        src_task: tile.task_id,
                        quad_flags,
                        needs_scissor_rect,
                    }),
                );
            }

            // TODO(gw): For now, we skip the main mask prim below for image masks. Perhaps
            //           we can better merge the logic together?
            // TODO(gw): How to efficiently handle if the image-mask rect doesn't cover local prim rect?
            return;
        }
        QuadClipShape::Path { ref path, fill_rule, origin } => {
            let path_rect = path.aabb().translate(origin.to_vector());
            let transform = path_clip_transform(
                clip,
                origin,
                task_rect,
                raster_spatial_node_index,
                device_pixel_scale,
                spatial_tree,
            );

            let rect = match transform {
                Some(transform) => {
                    rg_builder.push_sub_task(
                        sub_tasks,
                        SubTask::PathClip(PathClipSubTask {
                            path: path.clone(),
                            fill_rule,
                            transform,
                        }),
                    );

                    if clip.rect.contains_box(&path_rect) {
                        return;
                    }

                    // The path is also clipped by its rect.
                    clip.rect
                }
                None => {
                    // There is no transform to rasterize the path with, so
                    // only clip to its bounds.
                    clip.rect.intersection(&path_rect).unwrap_or(LayoutRect::zero())
                }
            };

            let mut writer = gpu_buffer.write_blocks(3);
            writer.push_one(rect);
            writer.push_one([0.0, 0.0, 0.0, 0.0]);
            writer.push_one([ClipMode::Clip as i32 as f32, 0.0, 0.0, 0.0]);
            let clip_address = writer.finish();

            (clip_address, true, false)
        }
    };

    let clip_spatial_node = spatial_tree.get_spatial_node(clip.spatial_node);
    let raster_spatial_node = spatial_tree.get_spatial_node(raster_spatial_node_index);
    let raster_clip = raster_spatial_node.coordinate_system_id == clip_spatial_node.coordinate_system_id;

    // See the documentation of RectangleClipSubTask::clip_space.
    let (clip_space, clip_transform_id, quad_address, quad_transform_id, is_same_coord_system) = if raster_clip {
        let quad_transform_id = GpuTransformId::IDENTITY;
        let pattern = Pattern::color(ColorF::WHITE);

        // TODO: This transform could be set to identity in favor of using the
        // pattern transform which serves the same purpose and is cheaper since
        // is a scale-offset. In this code path the raster-to-clip transform is
        // guaranteed to be representable by a scale and offset.
        let clip_transform_id = transforms.gpu.get_id_with_pre_scale(
            device_pixel_scale.inverse().get(),
            raster_spatial_node_index,
            clip.spatial_node,
            spatial_tree,
        );
        let pattern_transform = ScaleOffset::identity();

        let quad_address = write_device_prim_blocks(
            gpu_buffer,
            &task_rect,
            &task_rect,
            pattern.base_color,
            pattern.texture_input.task_id(),
            &[],
            pattern_transform,
        );

        (ClipSpace::Device, clip_transform_id, quad_address, quad_transform_id, true)
    } else {
        let prim_spatial_node = spatial_tree.get_spatial_node(prim_spatial_node_index);

        let quad_transform_id = transforms.gpu.get_id_with_post_scale(
            prim_spatial_node_index,
            raster_spatial_node_index,
            device_pixel_scale.get(),
            spatial_tree,
        );

        // Conservatively inflate the clip's primitive to ensure that it covers potential
        // anti-aliasing pixels of the original primitive. 2.0 matches AA_PIXEL_RADIUS in
        // ps_quad.glsl.
        let rect = prim_local_coverage_rect.inflate(2.0, 2.0);

        let quad_address = write_layout_prim_blocks(
            gpu_buffer,
            &rect,
            &rect,
            ColorF::WHITE,
            RenderTaskId::INVALID,
            &[],
        );

        let clip_spatial_node = spatial_tree.get_spatial_node(clip.spatial_node);
        let clip_transform_id = if prim_spatial_node.coordinate_system_id < clip_spatial_node.coordinate_system_id {
            transforms.gpu.get_id(
                clip.spatial_node,
                prim_spatial_node_index,
                spatial_tree,
            )
        } else {
            transforms.gpu.get_id(
                prim_spatial_node_index,
                clip.spatial_node,
                spatial_tree,
            )
        };

        let is_same_coord_system = spatial_tree.is_matching_coord_system(
            prim_spatial_node_index,
            raster_spatial_node_index,
        );

        (ClipSpace::Primitive, clip_transform_id, quad_address, quad_transform_id, is_same_coord_system)
    };

    let needs_scissor_rect = !is_same_coord_system;

    let quad_flags = if is_same_coord_system {
        QuadFlags::APPLY_RENDER_TASK_CLIP
    } else {
        QuadFlags::empty()
    };


    rg_builder.push_sub_task(
        sub_tasks,
        SubTask::RectangleClip(RectangleClipSubTask {
            quad_address,
            quad_transform_id,
            clip_address,
            clip_transform_id,
            quad_flags,
            clip_space,
            needs_scissor_rect,
            rounded_rect_fast_path: fast_path,
            rounded_rect_superellipse: superellipse,
        }),
    );
}

/// The transform from the space of a path clip's path to the local device
/// pixels of the render task it is applied to, if any.
fn path_clip_transform(
    clip: &QuadClip,
    origin: LayoutPoint,
    task_rect: &DeviceRect,
    raster_spatial_node_index: SpatialNodeIndex,
    device_pixel_scale: DevicePixelScale,
    spatial_tree: &SpatialTree,
) -> Option<PathTransform> {
    let clip_to_raster = if spatial_tree.can_get_relative_transform(clip.spatial_node, raster_spatial_node_index) {
        spatial_tree
            .get_relative_transform(clip.spatial_node, raster_spatial_node_index)
            .into_transform()
    } else if spatial_tree.can_get_relative_transform(raster_spatial_node_index, clip.spatial_node) {
        spatial_tree
            .get_relative_transform(raster_spatial_node_index, clip.spatial_node)
            .into_transform()
            .inverse()?
    } else {
        return None;
    };

    let transform = LayoutTransform::translation(origin.x, origin.y, 0.0)
        .then(&clip_to_raster)
        .then_scale(device_pixel_scale.0, device_pixel_scale.0, 1.0)
        .then_translate(euclid::vec3(-task_rect.min.x, -task_rect.min.y, 0.0))
        .with_destination::<DevicePixel>();

    Some(PathTransform::new(&transform))
}

fn can_use_aa_segments(
    device_bounds: &DeviceRect,
    local_bounds: &LayoutRect,
    pattern_kind: PatternKind,
) -> bool {
    device_bounds.width() > MIN_AA_SEGMENTS_SIZE
        && device_bounds.height() > MIN_AA_SEGMENTS_SIZE
        && local_bounds.width() > MIN_AA_SEGMENTS_SIZE
        && local_bounds.height() > MIN_AA_SEGMENTS_SIZE
        && !pattern_kind.requires_backdrop_readback()
}

fn create_quad_primitive(
    local_bounds: &LayoutRect,
    local_pattern_rect: &LayoutRect,
    device_clip_rect: &DeviceRect,
    local_to_device: Option<&ScaleOffset>,
    round_edges: EdgeMask,
    pattern: &Pattern,
) -> QuadPrimitive {
    let mut bounds;
    let pattern_rect;
    let pattern_transform;
    if let Some(local_to_device) = local_to_device {
        bounds = local_to_device
            .map_rect(&local_bounds)
            .intersection_unchecked(device_clip_rect)
            .to_untyped();
        // Capture the pattern rect before rounding: rounding is only meant to
        // snap vertex coverage to the device grid, not to move the samples.
        pattern_rect = local_to_device.map_rect(local_pattern_rect);
        bounds = round_edges.select(bounds.round(), bounds);

        pattern_transform = local_to_device.inverse();
    } else {
        bounds = local_bounds.to_untyped();
        pattern_rect = local_pattern_rect.to_untyped();
        pattern_transform = ScaleOffset::identity();
    };

    QuadPrimitive {
        // Fold the clip rect into the bounds: a single coverage rect.
        bounds,
        pattern_rect,
        input_task: pattern.texture_input.task_id(),
        pattern_scale_offset: pattern_transform,
        color: pattern.base_color.premultiplied(),
    }
}

/// Write the GPU blocks, either in local or device space
///
/// If a local-to-device transform is provided, then the
/// primitive is written in device space, otherwise it is
/// written in layout space.
fn write_prim_blocks(
    builder: &mut GpuBufferBuilderF,
    local_bounds: &LayoutRect,
    local_pattern_rect: &LayoutRect,
    device_clip_rect: &DeviceRect,
    local_to_device: Option<&ScaleOffset>,
    round_edges: EdgeMask,
    pattern: &Pattern,
) -> GpuBufferAddress {
    let mut bounds;
    let pattern_rect;
    let pattern_transform;
    if let Some(local_to_device) = local_to_device {
        pattern_rect = local_to_device.map_rect(local_pattern_rect);
        bounds = local_to_device
                .map_rect(&local_bounds)
                .intersection_unchecked(&device_clip_rect)
                .to_untyped();
        // Capture the pattern rect before rounding: rounding is only meant to
        // snap vertex coverage to the device grid, not to move the samples.
        bounds = round_edges.select(bounds.round(), bounds);
        pattern_transform = local_to_device.inverse();
    } else {
        pattern_rect = local_pattern_rect.to_untyped();
        bounds = local_bounds.to_untyped();
        pattern_transform = ScaleOffset::identity();
    };

    write_prim_blocks_impl(
        builder,
        bounds,
        pattern_rect,
        pattern.base_color,
        pattern.texture_input.task_id(),
        &[],
        pattern_transform,
    )
}

/// Write the gpu blocks for a primitive in device space.
pub fn write_device_prim_blocks(
    builder: &mut GpuBufferBuilderF,
    bounds: &DeviceRect,
    pattern_rect: &DeviceRect,
    pattern_base_color: ColorF,
    pattern_texture_input: RenderTaskId,
    segments: &[QuadSegment],
    pattern_scale_offset: ScaleOffset,
) -> GpuBufferAddress {
    write_prim_blocks_impl(
        builder,
        bounds.to_untyped(),
        pattern_rect.to_untyped(),
        pattern_base_color,
        pattern_texture_input,
        segments,
        pattern_scale_offset
    )
}

/// Write the gpu blocks for a primitive in layout space.
pub fn write_layout_prim_blocks(
    builder: &mut GpuBufferBuilderF,
    bounds: &LayoutRect,
    pattern_rect: &LayoutRect,
    pattern_base_color: ColorF,
    pattern_texture_input: RenderTaskId,
    segments: &[QuadSegment],
) -> GpuBufferAddress {
    write_prim_blocks_impl(
        builder,
        bounds.cast_unit(),
        pattern_rect.cast_unit(),
        pattern_base_color,
        pattern_texture_input,
        segments,
        ScaleOffset::identity(),
    )
}

fn write_prim_blocks_impl(
    builder: &mut GpuBufferBuilderF,
    // The coverage rect: the caller has already folded every clip into this.
    bounds: LayoutOrDeviceRect,
    pattern_rect: LayoutOrDeviceRect,
    pattern_base_color: ColorF,
    pattern_texture_input: RenderTaskId,
    segments: &[QuadSegment],
    pattern_scale_offset: ScaleOffset,
) -> GpuBufferAddress {
    let mut writer = builder.write_blocks(5 + segments.len() * 2);

    writer.push(&QuadPrimitive {
        bounds,
        pattern_rect,
        input_task: pattern_texture_input,
        pattern_scale_offset,
        color: pattern_base_color.premultiplied(),
    });

    for segment in segments {
        writer.push(segment);
    }

    writer.finish()
}

pub fn add_to_batch<F>(
    kind: PatternKind,
    pattern_input: PatternShaderInput,
    dst_task_address: RenderTaskAddress,
    transform_id: GpuTransformId,
    prim_address_f: GpuBufferAddress,
    quad_flags: QuadFlags,
    edge_flags: EdgeMask,
    segment_index: u8,
    src_task_ids: [RenderTaskId; 3],
    z_id: ZBufferId,
    blend_mode: BlendMode,
    readback: Option<RenderTaskId>,
    render_tasks: &RenderTaskGraph,
    gpu_buffer_builder: &mut GpuBufferBuilder,
    mut f: F,
) where F: FnMut(BatchKey, PrimitiveInstanceData) {

    // See the corresponding #defines in ps_quad.glsl
    #[repr(u8)]
    enum PartIndex {
        Center = 0,
        Left = 1,
        Top = 2,
        Right = 3,
        Bottom = 4,
        All = 5,
    }

    let mut textures = [TextureSource::Invalid; 3];
    for (i, src_task_id) in src_task_ids.iter().enumerate() {
        textures[i] = match *src_task_id {
            RenderTaskId::INVALID => TextureSource::Invalid,
            _ =>  match render_tasks.resolve_texture(*src_task_id) {
                Some(texture) => texture,
                None => {
                    // If a valid render task does not yield a texture source, render
                    // nothing. This can happen, for example when a stacking context
                    // could not be snapshotted.
                    return;
                },
            }
        };
    }


    // See QuadHeader in ps_quad.glsl
    let mut writer = gpu_buffer_builder.i32.write_blocks(QuadHeader::NUM_BLOCKS);
    writer.push(&QuadHeader {
        transform_id,
        z_id,
        pattern_input,
    });
    let prim_address_i = writer.finish();

    let textures = BatchTextures::composite_yuv(
        textures[0],
        textures[1],
        textures[2],
    );

    let prim_blend_mode = if quad_flags.contains(QuadFlags::IS_OPAQUE)
        && blend_mode == BlendMode::PremultipliedAlpha
    {
        BlendMode::None
    } else {
        blend_mode
    };

    let edge_flags_bits = edge_flags.bits();

    let readback = readback.unwrap_or(RenderTaskId::INVALID);

    let prim_batch_key = BatchKey {
        blend_mode: prim_blend_mode,
        kind: BatchKind::Quad(kind),
        textures,
        readback,
    };

    let aa_batch_key = BatchKey {
        blend_mode,
        kind: BatchKind::Quad(kind),
        textures,
        readback,
    };

    let mut instance = QuadInstance {
        dst_task_address,
        prim_address_i: prim_address_i.as_int(),
        prim_address_f: prim_address_f.as_int(),
        edge_flags: edge_flags_bits,
        quad_flags: quad_flags.bits(),
        part_index: PartIndex::All as u8,
        segment_index,
    };

    if edge_flags.is_empty() {
        // No antialisaing.
        f(prim_batch_key, instance.into());
    } else if quad_flags.contains(QuadFlags::USE_AA_SEGMENTS) {
        // Add instances for the antialisaing. This gives the center part
        // an opportunity to stay in the opaque pass.
        if edge_flags.contains(EdgeMask::LEFT) {
            let instance = QuadInstance {
                part_index: PartIndex::Left as u8,
                ..instance
            };
            f(aa_batch_key, instance.into());
        }
        if edge_flags.contains(EdgeMask::TOP) {
            let instance = QuadInstance {
                part_index: PartIndex::Top as u8,
                ..instance
            };
            f(aa_batch_key, instance.into());
        }
        if edge_flags.contains(EdgeMask::RIGHT) {
            let instance = QuadInstance {
                part_index: PartIndex::Right as u8,
                ..instance
            };
            f(aa_batch_key, instance.into());
        }
        if edge_flags.contains(EdgeMask::BOTTOM) {
            let instance = QuadInstance {
                part_index: PartIndex::Bottom as u8,
                ..instance
            };
            f(aa_batch_key, instance.into());
        }

        instance = QuadInstance {
            part_index: PartIndex::Center as u8,
            ..instance
        };

        f(prim_batch_key, instance.into());
    } else {
        // Render the anti-aliased quad with a single primitive.
        f(aa_batch_key, instance.into());
    }
}

/// Classification result for a tile within a quad
#[allow(dead_code)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[derive(Debug, Copy, Clone, PartialEq)]
pub enum QuadTileKind {
    // Clipped out - can be skipped
    Clipped,
    // Requires the pattern only, can draw directly
    Pattern {
        has_mask: bool,
    },
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[derive(Copy, Clone, Debug)]
pub struct QuadTileInfo {
    pub rect: DeviceRect,
    pub kind: QuadTileKind,
}

/// A `ClipMode::Clip` region registered with the tile classifier.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[derive(Copy, Clone, Debug)]
struct ClipInRegion {
    rect: DeviceRect,
    // Whether tiles straddling the region's boundary require a clip mask. This is
    // false when the clip is already applied via the primitive's local clip rect
    // (i.e. it is in the same coordinate system as the primitive), in which case
    // the pattern shader clips those tiles directly and no mask is needed. Tiles
    // fully outside the region are always culled regardless of this flag.
    needs_mask: bool,
}

impl Default for QuadTileInfo {
    fn default() -> Self {
        QuadTileInfo {
            rect: DeviceRect::zero(),
            kind: QuadTileKind::Pattern { has_mask: false },
        }
    }
}

/// A helper struct for classifying a set of tiles within a quad depending on
/// what strategy they can be used to draw them.
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct QuadTileClassifier {
    buffer: [QuadTileInfo; MAX_TILES_PER_QUAD_X * MAX_TILES_PER_QUAD_Y],
    mask_regions: Vec<DeviceRect>,
    clip_in_regions: Vec<ClipInRegion>,
    clip_out_regions: Vec<DeviceRect>,
    rect: DeviceRect,
    x_tiles: usize,
    y_tiles: usize,
    // Treat all tiles that have some coverage as masked.
    force_masks: bool,
}

impl QuadTileClassifier {
    pub fn new() -> Self {
        QuadTileClassifier {
            buffer: [QuadTileInfo::default(); MAX_TILES_PER_QUAD_X * MAX_TILES_PER_QUAD_Y],
            mask_regions: Vec::new(),
            clip_in_regions: Vec::new(),
            clip_out_regions: Vec::new(),
            rect: DeviceRect::zero(),
            x_tiles: 0,
            y_tiles: 0,
            force_masks: false,
        }
    }

    pub fn reset(
        &mut self,
        rect: DeviceRect,
        force_masks: bool,
    ) {
        let x_tiles = (rect.width() / MIN_QUAD_SPLIT_SIZE)
            .min(MAX_TILES_PER_QUAD_X as f32)
            .max(1.0)
            .ceil() as usize;
        let y_tiles = (rect.height() / MIN_QUAD_SPLIT_SIZE)
            .min(MAX_TILES_PER_QUAD_Y as f32)
            .max(1.0)
            .ceil() as usize;

        self.x_tiles = x_tiles;
        self.y_tiles = y_tiles;
        self.rect = rect;
        self.force_masks = force_masks;
        self.mask_regions.clear();
        self.clip_in_regions.clear();
        self.clip_out_regions.clear();

        // TODO(gw): Might be some f32 accuracy issues with how we construct these,
        //           should be more robust here...

        let dx = rect.width() / x_tiles as f32;
        let dy = rect.height() / y_tiles as f32;

        let mut y0 = rect.min.y;

        for y in 0 .. y_tiles {
            let y1 = if y == y_tiles - 1 {
                rect.max.y
            } else {
                (rect.min.y + (y + 1) as f32 * dy).round()
            };

            let mut x0 = rect.min.x;
            for x in 0 .. x_tiles {
                let info = &mut self.buffer[y * x_tiles + x];

                let x1 = if x == x_tiles - 1 {
                    rect.max.x
                } else {
                    (rect.min.x + (x + 1) as f32 * dx).round()
                };

                let p0 = DevicePoint::new(x0, y0);
                let p1 = DevicePoint::new(x1, y1);
                info.rect = DeviceRect::new(p0, p1);
                info.kind = QuadTileKind::Pattern { has_mask: force_masks };

                x0 = x1;
            }

            y0 = y1;
        }
    }

    /// Add an area that needs a clip mask / indirect area
    pub fn add_mask_region(
        &mut self,
        mask_region: DeviceRect,
    ) {
        if !mask_region.is_empty() {
            self.mask_regions.push(mask_region);
        }
    }

    // TODO(gw): Make use of this to skip tiles that are completely clipped out in a follow up!
    pub fn add_clip_rect(
        &mut self,
        clip_rect: DeviceRect,
        clip_mode: ClipMode,
        applied_as_local_clip: bool,
    ) {
        match clip_mode {
            ClipMode::Clip => {
                self.clip_in_regions.push(ClipInRegion {
                    rect: clip_rect,
                    needs_mask: !applied_as_local_clip,
                });
            }
            ClipMode::ClipOut => {
                self.clip_out_regions.push(clip_rect);

                self.add_mask_region(self.rect);
            }
        }
    }

    /// Classify all the tiles in to categories, based on the provided masks and clip regions
    pub fn classify(
        &mut self,
    ) -> QuadTileIterator {
        assert_ne!(self.x_tiles, 0);
        assert_ne!(self.y_tiles, 0);

        let tile_count = self.x_tiles * self.y_tiles;
        let tiles = &mut self.buffer[0 .. tile_count];

        for info in tiles.iter_mut() {
            // A clip-in region culls tiles that fall entirely outside it. Tiles
            // that straddle its boundary require a mask, unless the clip is
            // already applied via the primitive's local clip rect (in which case
            // the pattern shader clips them directly). Tiles fully contained by
            // the region are unaffected by it.
            for clip_region in &self.clip_in_regions {
                match info.kind {
                    QuadTileKind::Clipped => {},
                    QuadTileKind::Pattern { ref mut has_mask } => {
                        if !clip_region.rect.intersects(&info.rect) {
                            info.kind = QuadTileKind::Clipped;
                        } else if clip_region.needs_mask && !clip_region.rect.contains_box(&info.rect) {
                            *has_mask = true;
                        }
                    }
                }

            }

            // If a tile doesn't intersect with a clip-out region, it's clipped
            for clip_region in &self.clip_out_regions {
                match info.kind {
                    QuadTileKind::Clipped => {},
                    QuadTileKind::Pattern { .. } => {
                        if clip_region.contains_box(&info.rect) {
                            info.kind = QuadTileKind::Clipped;
                        }
                    }
                }
            }

            // If a tile intersects with a mask region, and isn't clipped, it needs a mask
            for mask_region in &self.mask_regions {
                match info.kind {
                    QuadTileKind::Clipped | QuadTileKind::Pattern { has_mask: true, .. } => {},
                    QuadTileKind::Pattern { ref mut has_mask, .. } => {
                        if mask_region.intersects(&info.rect) {
                            *has_mask = true;
                        }
                    }
                }
            }
        }

        self.x_tiles = 0;
        self.y_tiles = 0;

        QuadTileIterator { tiles }
    }
}

pub struct QuadTileIterator<'l> {
    tiles: &'l[QuadTileInfo],
}

impl<'l> Iterator for QuadTileIterator<'l> {
    type Item = QuadTileInfo;
    fn next(&mut self) -> Option<QuadTileInfo> {
        if self.tiles.is_empty() {
            return None;
        }

        let mut tile = self.tiles[0];
        self.tiles = &self.tiles[1..];

        // Skip over empty tiles
        while tile.kind == QuadTileKind::Clipped {
            tile = *self.tiles.first()?;
            self.tiles = &self.tiles[1..];
        }

        // Merge consecutive compatible tiles.
        // This reduces some of the per-tile overhead both on CPU and GPU, especially
        // with SWGL which benefits enormously from working with long rows of pixels.
        while let Some(info) = self.tiles.first() {
            if tile.rect.min.y != info.rect.min.y || tile.kind != info.kind {
                // Different row or different kind, stop merging.
                break;
            }

            let max = match info.kind {
                // If a tile must be rendered into an intermediate target, don't make
                // wider than 1024 pixels so that it plays well with the texture atlas.
                QuadTileKind::Pattern { has_mask: true } => 1024.0,
                // If the tile is rendered directly into the destination target let it
                // be as wide as possible.
                QuadTileKind::Pattern { has_mask: false } => f32::MAX,
                QuadTileKind::Clipped => { break; }
            };

            if info.rect.max.x - tile.rect.min.x > max {
                break;
            }

            // At this point we know that this tile on the same row, adjacent
            // and of the same kind as the previous one, so they can be merged.
            tile.rect.max.x = info.rect.max.x;
            self.tiles = &self.tiles[1..];
        }

        Some(tile)
    }
}

#[cfg(test)]
fn qc_new(x0: f32, y0: f32, w: f32, h: f32) -> QuadTileClassifier {
    let mut qc = QuadTileClassifier::new();

    qc.reset(
        DeviceRect::new(DevicePoint::new(x0, y0), DevicePoint::new(x0 + w, y0 + h)),
        false,
    );

    qc
}

#[cfg(test)]
fn qc_verify(mut qc: QuadTileClassifier, expected: &[QuadTileKind]) {
    let tiles = qc.classify();

    let mut n = 0;
    for (tile, ex) in tiles.zip(expected.iter()) {
        assert_eq!(tile.kind, *ex, "Failed for tile {:?}", tile.rect.to_rect());
        n += 1;
    }

    assert_eq!(n, expected.len())
}

#[cfg(test)]
const P: QuadTileKind = QuadTileKind::Pattern { has_mask: false };

#[cfg(test)]
const M: QuadTileKind = QuadTileKind::Pattern { has_mask: true };

#[test]
fn quad_classify_1() {
    let qc = qc_new(0.0, 0.0, 768.0, 768.0);
    qc_verify(qc, &[
        P,
        P,
        P,
    ]);
}

#[test]
fn quad_classify_2() {
    let mut qc = qc_new(0.0, 0.0, 768.0, 768.0);

    let rect = DeviceRect::new(DevicePoint::new(0.0, 0.0), DevicePoint::new(768.0, 768.0));
    qc.add_clip_rect(rect, ClipMode::Clip, false);

    qc_verify(qc, &[
        P,
        P,
        P,
    ]);
}

#[test]
fn quad_classify_3() {
    let mut qc = qc_new(0.0, 0.0, 768.0, 768.0);

    let rect = DeviceRect::new(DevicePoint::new(230.0, 230.0), DevicePoint::new(460.0, 460.0));
    qc.add_clip_rect(rect, ClipMode::Clip, false);

    qc_verify(qc, &[
        M,
        M,
    ]);
}

#[test]
fn quad_classify_4() {
    let mut qc = qc_new(0.0, 0.0, 768.0, 768.0);

    let rect = DeviceRect::new(DevicePoint::new(230.0, 230.0), DevicePoint::new(537.0, 537.0));
    qc.add_clip_rect(rect, ClipMode::Clip, false);

    qc_verify(qc, &[
        M,
        M, P, M,
        M,
    ]);
}

#[test]
fn quad_classify_5() {
    let mut qc = qc_new(0.0, 0.0, 768.0, 768.0);

    let rect = DeviceRect::new(DevicePoint::new(230.0, 230.0), DevicePoint::new(537.0, 537.0));
    qc.add_clip_rect(rect, ClipMode::ClipOut, false);

    qc_verify(qc, &[
        M,
        M, M,
        M,
    ]);
}

#[test]
fn quad_classify_6() {
    let mut qc = qc_new(0.0, 0.0, 768.0, 768.0);

    let rect = DeviceRect::new(DevicePoint::new(40.0, 40.0), DevicePoint::new(60.0, 60.0));
    qc.add_clip_rect(rect, ClipMode::ClipOut, false);

    qc_verify(qc, &[
        M,
        M,
        M,
    ]);
}

#[test]
fn quad_classify_7() {
    let mut qc = qc_new(0.0, 0.0, 768.0, 768.0);

    let rect = DeviceRect::new(DevicePoint::new(154.0, 77.0), DevicePoint::new(691.0, 614.0));
    qc.add_mask_region(rect);

    qc_verify(qc, &[
        M,
        M,
        M,
    ]);
}

#[test]
fn quad_classify_8() {
    let mut qc = qc_new(0.0, 0.0, 768.0, 768.0);

    let rect = DeviceRect::new(DevicePoint::new(307.0, 307.0), DevicePoint::new(460.0, 460.0));
    qc.add_mask_region(rect);

    qc_verify(qc, &[
        P,
        P, M, P,
        P,
    ]);
}

#[test]
fn quad_classify_9() {
    let mut qc = qc_new(100.0, 200.0, 1024.0, 1024.0);

    let rect = DeviceRect::new(DevicePoint::new(90.0, 180.0), DevicePoint::new(250.0, 650.0));
    qc.add_mask_region(rect);

    qc_verify(qc, &[
        M, P,
        M, P,
        P,
        P,
    ]);
}

#[test]
fn quad_classify_10() {
    let mut qc = qc_new(100.0, 200.0, 1024.0, 1024.0);

    let mask_rect = DeviceRect::new(DevicePoint::new(90.0, 180.0), DevicePoint::new(510.0, 710.0));
    qc.add_mask_region(mask_rect);

    let clip_rect = DeviceRect::new(DevicePoint::new(120.0, 220.0), DevicePoint::new(714.0, 1015.0));
    qc.add_clip_rect(clip_rect, ClipMode::Clip, false);

    qc_verify(qc, &[
        M,
        M,
        M, P, M,
        M,
    ]);
}

#[test]
fn quad_classify_11() {
    let mut qc = qc_new(100.0, 200.0, 1024.0, 1024.0);

    let mask_rect = DeviceRect::new(DevicePoint::new(90.0, 180.0), DevicePoint::new(510.0, 710.0));
    qc.add_mask_region(mask_rect);

    let clip_rect = DeviceRect::new(DevicePoint::new(120.0, 220.0), DevicePoint::new(714.0, 1015.0));
    qc.add_clip_rect(clip_rect, ClipMode::Clip, false);

    let clip_out_rect = DeviceRect::new(DevicePoint::new(130.0, 200.0), DevicePoint::new(714.0, 609.0));
    qc.add_clip_rect(clip_out_rect, ClipMode::ClipOut, false);

    qc_verify(qc, &[
        M,
        M,
        M,
        M,
    ]);
}

// A straddling clip that is not applied as the local clip rect masks the
// boundary tiles.
#[test]
fn quad_classify_13() {
    let mut qc = qc_new(0.0, 0.0, 768.0, 768.0);

    let rect = DeviceRect::new(DevicePoint::new(128.0, 128.0), DevicePoint::new(640.0, 640.0));
    qc.add_clip_rect(rect, ClipMode::Clip, false);

    qc_verify(qc, &[
        M,
        M, P, M,
        M,
    ]);
}

// The same straddling clip, but applied as the local clip rect: the boundary
// tiles are clipped by the shader and don't need a mask.
#[test]
fn quad_classify_14() {
    let mut qc = qc_new(0.0, 0.0, 768.0, 768.0);

    let rect = DeviceRect::new(DevicePoint::new(128.0, 128.0), DevicePoint::new(640.0, 640.0));
    qc.add_clip_rect(rect, ClipMode::Clip, true);

    qc_verify(qc, &[
        P,
        P,
        P,
    ]);
}

#[test]
fn quad_classify_12() {
    let mut qc = qc_new(100.0, 200.0, 1024.0, 1024.0);

    let clip_out_rect = DeviceRect::new(DevicePoint::new(130.0, 200.0), DevicePoint::new(714.0, 609.0));
    qc.add_clip_rect(clip_out_rect, ClipMode::ClipOut, false);

    let clip_rect = DeviceRect::new(DevicePoint::new(120.0, 220.0), DevicePoint::new(714.0, 1015.0));
    qc.add_clip_rect(clip_rect, ClipMode::Clip, false);

    let mask_rect = DeviceRect::new(DevicePoint::new(90.0, 180.0), DevicePoint::new(510.0, 710.0));
    qc.add_mask_region(mask_rect);

    qc_verify(qc, &[
        M,
        M,
        M,
        M,
    ]);
}
