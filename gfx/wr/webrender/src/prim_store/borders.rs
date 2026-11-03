/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{ColorF, NormalBorder};
use api::units::*;
use smallvec::SmallVec;
use crate::border::{NormalBorderSegment, MAX_BORDER_RESOLUTION};
use crate::command_buffer::CommandBufferIndex;
use crate::pattern::image::ImagePattern;
use crate::quad::{self, QuadDescriptor, QuadTransformState};
use crate::quad_clip::QuadClipStack;
use crate::render_task_cache::{RenderTaskCacheKey, RenderTaskCacheKeyKind, RenderTaskParent, to_cache_size};
use crate::frame_builder::{FrameBuildingContext, FrameBuildingState};
use crate::intern;
use crate::internal_types::LayoutPrimitiveInfo;
use crate::prim_store::{
    InternablePrimitive, NinePatchDescriptor, PrimTemplate, PrimTemplateCommonData, PrimitiveKind, PrimitiveScratchBuffer, PrimitiveStore
};
use crate::resource_cache::ImageRequest;
use crate::spatial_tree::SpatialTree;
use crate::render_task::RenderTask;
use crate::render_task_graph::RenderTaskId;
use crate::util::clamp_to_scale_factor;

// `NormalBorderPrim` and its key live in `webrender_api::interned_prims` so
// content-process interning can hold them. Re-exported to keep existing
// references working.
pub use api::interned_prims::{NormalBorderKey, NormalBorderPrim};

impl intern::InternDebug for NormalBorderKey {}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(MallocSizeOf)]
pub struct NormalBorderData {
    pub border: NormalBorder,
    pub widths: LayoutSideOffsets,
}

pub struct BorderRasterization {
    pub border: NormalBorder,
    pub segments: SmallVec<[NormalBorderSegment; 8]>,
    pub scale: LayoutToDeviceScale,
}

impl BorderRasterization {
    pub fn new(
        rect: LayoutRect,
        mut border: NormalBorder,
        mut widths: LayoutSideOffsets,
        quad_transform: &QuadTransformState,
        snap_thickness: bool,
        spatial_tree: &SpatialTree,
    ) -> Self {
        // The border is rasterized at the scale it is composited at: the
        // primitive to raster transform of the surface being drawn into, times
        // that surface's device pixel scale. The two are kept apart because only
        // the raster part is quantized to a power of two below.
        let raster_scale = quad_transform.raster_scale_factors();
        let device_pixel_scale = quad_transform.device_pixel_scale();

        // Snap the thickness of a border the author declared at >= 1 CSS pixel
        // to a whole device pixel. Border edges are composited onto their
        // layout-space rects, so a transform makes a 1px edge a fractional
        // device thickness: under a downscale it can shrink below a device
        // pixel and be antialiased away entirely, until whole sides of the
        // border vanish (bug 1258112); at other scales the four sides land at
        // different sub-pixel phases and render with visibly uneven thickness
        // (bug 1950029). Rounding the device thickness to the nearest pixel
        // (floored at 1 so a real border can't disappear) makes every side a
        // consistent whole-pixel width. Genuinely sub-CSS-pixel edges are left
        // untouched.
        //
        // Both this and the corner snapping below are expressed in layout
        // space, so they are only valid when a single device scale describes
        // the whole border. Under a perspective transform `w` varies across the
        // border box, there is no such scale, and `coplanar_scale_factors`
        // returns None: widening the layout width by 1/scale would then swell
        // the border by an unbounded amount as the element tilts away from the
        // viewer, eating into the content box. Leave those borders at their
        // authored dimensions.
        //
        // Uses the unclamped device scale factors, since that is the transform
        // the edge is actually composited with, not the power-of-2
        // rasterization scale.
        //
        // An anti-aliased border declines the thickness snapping: its thickness
        // is geometry the caller resolved exactly (an SVG stroke, say) and the
        // shader resolves its edges at their sub-pixel positions, so moving
        // them to the grid is what the anti-aliasing is there to avoid. The
        // corner snapping below still applies, since it is about how the corner
        // texture is sampled rather than where the border's edges are.
        if let Some((device_scale_x, device_scale_y)) = quad_transform.coplanar_scale_factors() {
            let snap_width = |w: f32, s: f32| {
                if w >= 1.0 && s > 0.0 { (w * s).round().max(1.0) / s } else { w }
            };
            if snap_thickness {
                widths.left = snap_width(widths.left, device_scale_x);
                widths.right = snap_width(widths.right, device_scale_x);
                widths.top = snap_width(widths.top, device_scale_y);
                widths.bottom = snap_width(widths.bottom, device_scale_y);
            }

            // A corner's cached texture is rasterized at the whole-pixel size of its
            // corner box and then stretched onto that box, so a box with a fractional
            // device size is resampled on composite: the arc loses contrast against
            // the grid-snapped straight edges, which reads as the curve being thinner
            // than the sides (bug 2062877). The box is `max(radius, width)`, so
            // snapping a radius that exceeds its border width to a whole device pixel
            // makes that composite exact. Radii below the width don't drive the box
            // size, so they are left alone.
            //
            // Rounds down rather than to the nearest pixel. Growing a radius can push
            // the pair that shares an edge past the length of that edge, which makes
            // the two corner segments overlap and double-blend a translucent border;
            // and since the two axes are constrained by different edges, guarding them
            // separately can round one axis up and the other down, turning a circular
            // corner into an elliptical one.
            let snap_radius = |r: f32, w: f32, s: f32| {
                if r > w && s > 0.0 { (r * s).floor().max(w * s) / s } else { r }
            };
            let r = &mut border.radius;
            r.top_left.width = snap_radius(r.top_left.width, widths.left, device_scale_x);
            r.top_left.height = snap_radius(r.top_left.height, widths.top, device_scale_y);
            r.top_right.width = snap_radius(r.top_right.width, widths.right, device_scale_x);
            r.top_right.height = snap_radius(r.top_right.height, widths.top, device_scale_y);
            r.bottom_left.width = snap_radius(r.bottom_left.width, widths.left, device_scale_x);
            r.bottom_left.height = snap_radius(r.bottom_left.height, widths.bottom, device_scale_y);
            r.bottom_right.width = snap_radius(r.bottom_right.width, widths.right, device_scale_x);
            r.bottom_right.height = snap_radius(r.bottom_right.height, widths.bottom, device_scale_y);
        }

        // Under an animated or pinch-zoomed transform, scale factors are
        // normalized to a power of 2 to reduce the number of resolution changes:
        // rounding up to the nearest power-of-2 boundary means we don't keep
        // having to redraw the content as it scales up and down. It ensures we
        // never scale up, only down --- avoiding jaggies. It also ensures we
        // never scale down by more than a factor of 2, avoiding bad downscaling
        // quality.
        // Under a static transform the exact scale is used, so that the corner
        // textures composite 1:1 instead of being resampled.
        let prim_spatial_node = spatial_tree
            .get_spatial_node(quad_transform.prim_spatial_node_index());
        let scale_may_change = prim_spatial_node.is_ancestor_or_self_animating
            || prim_spatial_node.is_ancestor_or_self_zooming;
        let (scale_width, scale_height) = if scale_may_change {
            (
                clamp_to_scale_factor(raster_scale.0, false),
                clamp_to_scale_factor(raster_scale.1, false),
            )
        } else {
            (raster_scale.0.abs(), raster_scale.1.abs())
        };
        // Pick the maximum dimension as scale
        let mut scale = LayoutToDeviceScale::new(
            scale_width.max(scale_height) * device_pixel_scale.0,
        );

        // Build the per-frame border segments up front so we can clamp the
        // rasterization scale against the largest segment before requesting
        // any render tasks. Capping the scale renders very large corners at a
        // lower resolution and stretches them: the right shape, but blurrier.
        let mut segments: SmallVec<[NormalBorderSegment; 8]> = SmallVec::new();
        crate::border::create_border_segments(
            rect,
            &border,
            &widths,
            &mut |segment| segments.push(segment.clone()),
        );

        let mut max_dim = 1.0;
        for segment in &segments {
            if segment.is_solid.is_none() {
                max_dim = segment.task_size.width.max(segment.task_size.height.max(max_dim));
            }
        }
        let max_scale = LayoutToDeviceScale::new(MAX_BORDER_RESOLUTION as f32 / max_dim);
        scale.0 = scale.0.min(max_scale.0);

        Self { border, segments, scale }
    }
}

impl NormalBorderData {
    pub fn update(
        &self,
        desc: &QuadDescriptor,
        clips: &QuadClipStack,
        quad_transform: &mut QuadTransformState,
        snap_thickness: bool,
        frame_context: &FrameBuildingContext,
        targets: &[CommandBufferIndex],
        frame_state: &mut FrameBuildingState,
        scratch: &mut PrimitiveScratchBuffer,
    ) {
        let BorderRasterization { border, segments, mut scale } = BorderRasterization::new(
            desc.pattern_rect, self.border, self.widths, quad_transform,
            snap_thickness, frame_context.spatial_tree,
        );

        for segment in &segments {
            if let Some(color) = &segment.is_solid {
                quad::prepare_quad(
                    color,
                    &QuadDescriptor {
                        pattern_rect: segment.pattern_rect,
                        bounds: segment.bounds(&desc.bounds),
                        aligned_aa_edges: desc.aligned_aa_edges & segment.edge_flags,
                        transformed_aa_edges: desc.transformed_aa_edges & segment.edge_flags,
                    },
                    &None,
                    clips,
                    quad_transform,
                    frame_context.spatial_tree,
                    targets,
                    frame_state,
                    scratch,
                );

                continue;
            }

            // Update the cache key device size based on requested scale.
            let cache_size = to_cache_size(segment.task_size, &mut scale);
            let cache_key = RenderTaskCacheKey {
                kind: RenderTaskCacheKeyKind::BorderSegment(segment.cache_key.clone()),
                origin: DeviceIntPoint::zero(),
                size: cache_size,
            };

            // TODO(gw): We don't calculate opacity for borders yet!
            let is_opaque = false;

            let task_id = frame_state.resource_cache.request_render_task(
                Some(cache_key),
                is_opaque,
                RenderTaskParent::Surface,
                &mut frame_state.frame_gpu_data.f32,
                frame_state.rg_builder,
                &mut frame_state.surface_builder,
                &mut |rg_builder, gpu_buffer_builder| {
                    rg_builder.add().init(segment.render_task(
                        cache_size, &border, scale, gpu_buffer_builder,
                    ))
                }
            );

            let pattern = ImagePattern {
                src_task_id: task_id,
                src_is_opaque: is_opaque,
                premultiplied: true,
                sampler_kind: api::ImageBufferKind::Texture2D,
                color: ColorF::WHITE,
            };

            // The texture is drawn across the full segment rect (for
            // corners that is the natural corner-image size, which may
            // extend past the visible area). `clip_rect` crops it back to
            // the visible part for corners whose adjacent corner overlaps.
            let segment_pattern_rect = segment.pattern_rect;

            let (stretch_size, spacing) = segment.repetition(cache_size);

            quad::prepare_repeatable_quad(
                &pattern,
                &QuadDescriptor {
                    pattern_rect: segment_pattern_rect,
                    bounds: segment.bounds(&desc.bounds),
                    aligned_aa_edges: desc.aligned_aa_edges & segment.edge_flags,
                    transformed_aa_edges: desc.transformed_aa_edges & segment.edge_flags,
                },
                stretch_size,
                spacing,
                &None,
                clips,
                quad_transform,
                frame_context.spatial_tree,
                targets,
                frame_state,
                scratch,
            );
        }
    }
}

pub type NormalBorderTemplate = PrimTemplate<NormalBorderData>;

impl From<NormalBorderKey> for NormalBorderTemplate {
    fn from(key: NormalBorderKey) -> Self {
        let common = PrimTemplateCommonData::with_key_common(key.common);

        let mut border: NormalBorder = key.kind.border.into();
        let widths = LayoutSideOffsets::from_au(key.kind.widths);

        // FIXME(emilio): Is this the best place to do this?
        border.normalize(&widths);

        NormalBorderTemplate {
            common,
            kind: NormalBorderData {
                border,
                widths,
            }
        }
    }
}

pub type NormalBorderDataHandle = intern::Handle<NormalBorderPrim>;

impl intern::Internable for NormalBorderPrim {
    type Key = NormalBorderKey;
    type StoreData = NormalBorderTemplate;
    type InternData = ();
    const PROFILE_COUNTER: usize = crate::profiler::INTERNED_NORMAL_BORDERS;
}

impl InternablePrimitive for NormalBorderPrim {
    fn into_key(
        self,
        info: &LayoutPrimitiveInfo,
    ) -> NormalBorderKey {
        NormalBorderKey::new(
            info.into(),
            self,
        )
    }

    fn make_instance_kind(
        _key: NormalBorderKey,
        data_handle: NormalBorderDataHandle,
        _: &mut PrimitiveStore,
    ) -> PrimitiveKind {
        PrimitiveKind::NormalBorder {
            data_handle,
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

// `ImageBorder` now lives in `webrender_api::interned_prims` (with the image
// request inlined as key/rendering/tile so the value is api-resident). The
// frame-time `ImageBorderData` below rebuilds the `ImageRequest`.
pub use api::interned_prims::{ImageBorder, ImageBorderKey};

impl intern::InternDebug for ImageBorderKey {}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(MallocSizeOf)]
pub struct ImageBorderData {
    #[ignore_malloc_size_of = "Arc"]
    pub request: ImageRequest,
    pub nine_patch: NinePatchDescriptor,
}

impl ImageBorderData {
    pub fn update(
        &self,
        frame_state: &mut FrameBuildingState,
    ) -> (RenderTaskId, DeviceIntSize, bool) {
        let size = frame_state.resource_cache.request_image(
            self.request,
            &mut frame_state.frame_gpu_data.f32,
        );

        let task_id = frame_state.rg_builder.add().init(
            RenderTask::new_image(size, self.request, false)
        );

        let is_opaque = frame_state
            .resource_cache
            .get_image_properties(self.request.key)
            .map(|properties| properties.descriptor.is_opaque())
            .unwrap_or(true);

        (task_id, size, is_opaque)
    }
}

pub type ImageBorderTemplate = PrimTemplate<ImageBorderData>;

impl From<ImageBorderKey> for ImageBorderTemplate {
    fn from(key: ImageBorderKey) -> Self {
        let common = PrimTemplateCommonData::with_key_common(key.common);

        ImageBorderTemplate {
            common,
            kind: ImageBorderData {
                request: ImageRequest {
                    key: key.kind.key,
                    rendering: key.kind.rendering,
                    tile: key.kind.tile,
                },
                nine_patch: key.kind.nine_patch,
            }
        }
    }
}

pub type ImageBorderDataHandle = intern::Handle<ImageBorder>;

impl intern::Internable for ImageBorder {
    type Key = ImageBorderKey;
    type StoreData = ImageBorderTemplate;
    type InternData = ();
    const PROFILE_COUNTER: usize = crate::profiler::INTERNED_IMAGE_BORDERS;
}

impl InternablePrimitive for ImageBorder {
    fn into_key(
        self,
        info: &LayoutPrimitiveInfo,
    ) -> ImageBorderKey {
        ImageBorderKey::new(
            info.into(),
            self,
        )
    }

    fn make_instance_kind(
        _key: ImageBorderKey,
        data_handle: ImageBorderDataHandle,
        _: &mut PrimitiveStore,
    ) -> PrimitiveKind {
        PrimitiveKind::ImageBorder {
            data_handle
        }
    }
}

#[test]
#[cfg(target_pointer_width = "64")]
fn test_struct_sizes() {
    use std::mem;
    // The sizes of these structures are critical for performance on a number of
    // talos stress tests. If you get a failure here on CI, there's two possibilities:
    // (a) You made a structure smaller than it currently is. Great work! Update the
    //     test expectations and move on.
    // (b) You made a structure larger. This is not necessarily a problem, but should only
    //     be done with care, and after checking if talos performance regresses badly.
    assert_eq!(mem::size_of::<NormalBorderPrim>(), 116, "NormalBorderPrim size changed");
    assert_eq!(mem::size_of::<NormalBorderTemplate>(), 200, "NormalBorderTemplate size changed");
    assert_eq!(mem::size_of::<NormalBorderKey>(), 152, "NormalBorderKey size changed");
    assert_eq!(mem::size_of::<ImageBorder>(), 68, "ImageBorder size changed");
    assert_eq!(mem::size_of::<ImageBorderTemplate>(), 104, "ImageBorderTemplate size changed");
    assert_eq!(mem::size_of::<ImageBorderKey>(), 104, "ImageBorderKey size changed");
}
