/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::units::*;
use api::{ColorF, ImageBufferKind, ImageRendering, PremultipliedColorF};
use crate::batch::BatchTextures;
use crate::composite::{
    CompositeState, CompositeSurfaceFormat, CompositeTileSurface,
    CompositorInputLayer, CompositorSurfaceUsage, CompositorSurfaceTransform,
    CompositeRoundedCorner, NativeTileId, ResolvedExternalSurface,
    ResolvedExternalSurfaceColorData, ClipRadius, CompositeFeatures, CompositorKind, TileKind,
};
use crate::frame_builder::Frame;
use crate::{CompositorInputConfig, PictureCacheDebugInfo, debug_colors};
use api::{ClipMode, DebugFlags};
use std::collections::HashSet;
use std::mem;
use crate::debug_item::DebugItem;
use crate::segment::EdgeMask;
use crate::device::{BlendMode, DrawTarget, LoadOp, RenderPassDescriptor, StoreOp};
use crate::gpu_types::{CompositeInstance, ZBufferId};
use crate::internal_types::{FastHashMap, TextureSource};
use crate::picture::ResolvedSurfaceTexture;
use super::RenderResults;
use crate::profiler::{self};
use crate::rectangle_occlusion as occlusion;
use crate::renderer::{
    GPU_SAMPLER_TAG_OPAQUE, GPU_SAMPLER_TAG_TRANSPARENT, GPU_TAG_COMPOSITE, PartialPresentMode,
};
use super::layer_tree::{self, LayerMode};
use crate::renderer::{FramebufferKind, Renderer, RendererStats, VertexArrayKind};
use crate::segment::SegmentBuilder;
use crate::tile_cache::TileId;
use euclid::{Scale, Transform3D, default};

#[derive(Debug, Copy, Clone)]
pub(super) struct OcclusionItemKey {
    pub tile_index: usize,
    pub needs_mask: bool,
}

pub(super) struct SwapChainLayer {
    pub occlusion: occlusion::FrontToBackBuilder<OcclusionItemKey>,
}

pub(super) struct CompositeTileState {
    pub local_rect: PictureRect,
    pub local_valid_rect: PictureRect,
    pub device_clip_rect: DeviceRect,
    pub z_id: ZBufferId,
    pub device_tile_box: DeviceRect,
    pub visible_rects: Vec<DeviceRect>,
}

impl CompositeTileState {
    pub fn same_state(&self, other: &CompositeTileState) -> bool {
        self.local_rect == other.local_rect
            && self.local_valid_rect == other.local_valid_rect
            && self.device_clip_rect == other.device_clip_rect
            && self.z_id == other.z_id
            && self.device_tile_box == other.device_tile_box
    }
}

// Only surfaces drawn into a content layer are stored here. Matching a state
// in the previous frame therefore also verifies that it was not externally
// composited. Compare the complete draw state, not just the destination rect.
#[derive(PartialEq)]
pub(super) struct ExternalContentSurfaceState {
    rect: DeviceRect,
    local_rect: PictureRect,
    local_valid_rect: PictureRect,
    device_clip_rect: DeviceRect,
    transform: CompositorSurfaceTransform,
    clip: Option<(DeviceRect, api::BorderRadius)>,
    z_id: ZBufferId,
    kind: TileKind,
    layer_index: usize,
    external_image_id: Option<api::ExternalImageId>,
    image_buffer_kind: ImageBufferKind,
    color_data: ResolvedExternalSurfaceColorData,
}

pub(super) struct LayerCompositorFrameState {
    pub tile_states: FastHashMap<TileId, CompositeTileState>,
    pub external_content_surfaces: Vec<ExternalContentSurfaceState>,
}

impl Renderer {
    /// Rasterize any external compositor surfaces that require updating
    fn update_external_native_surfaces(
        &mut self,
        external_surfaces: &[ResolvedExternalSurface],
        results: &mut RenderResults,
    ) {
        if external_surfaces.is_empty() {
            return;
        }

        let opaque_sampler = self.gpu_profiler.start_sampler(GPU_SAMPLER_TAG_OPAQUE);

        self.device.set_depth_test(None);
        self.set_blend_mode(BlendMode::None, FramebufferKind::Main);

        for surface in external_surfaces {
            // See if this surface needs to be updated
            let (native_surface_id, surface_size) = match surface.update_params {
                Some(params) => params,
                None => continue,
            };

            // When updating an external surface, the entire surface rect is used
            // for all of the draw, dirty, valid and clip rect parameters.
            let surface_rect = surface_size.into();

            // Bind the native compositor surface to update
            let surface_info = self.compositor_config
                .compositor()
                .unwrap()
                .bind(
                    NativeTileId {
                        surface_id: native_surface_id,
                        x: 0,
                        y: 0,
                    },
                    surface_rect,
                    surface_rect,
                );

            // Bind the native surface to current FBO target
            let draw_target = DrawTarget::NativeSurface {
                offset: surface_info.origin,
                handle: surface_info.handle,
                dimensions: surface_size,
            };
            self.device.begin_render_pass(&RenderPassDescriptor {
                target: draw_target,
                render_area: None,
                color_load: LoadOp::DontCare,
                depth_load: LoadOp::DontCare,
            });

            let projection = Transform3D::ortho(
                0.0,
                surface_size.width as f32,
                0.0,
                surface_size.height as f32,
                self.device.ortho_near_plane(),
                self.device.ortho_far_plane(),
            );

            let ( textures, instance ) = match surface.color_data {
                ResolvedExternalSurfaceColorData::Yuv{
                        ref planes, color_space, format, channel_bit_depth, .. } => {

                    let textures = BatchTextures::composite_yuv(
                        planes[0].texture,
                        planes[1].texture,
                        planes[2].texture,
                    );

                    // When the texture is an external texture, the UV rect is not known when
                    // the external surface descriptor is created, because external textures
                    // are not resolved until the lock() callback is invoked at the start of
                    // the frame render. To handle this, query the texture resolver for the
                    // UV rect if it's an external texture, otherwise use the default UV rect.
                    let uv_rects = [
                        self.texture_resolver.get_uv_rect(&textures.input.colors[0], planes[0].uv_rect),
                        self.texture_resolver.get_uv_rect(&textures.input.colors[1], planes[1].uv_rect),
                        self.texture_resolver.get_uv_rect(&textures.input.colors[2], planes[2].uv_rect),
                    ];

                    let instance = CompositeInstance::new_yuv(
                        surface_rect.to_f32(),
                        surface_rect.to_f32(),
                        // z-id is not relevant when updating a native compositor surface.
                        // TODO(gw): Support compositor surfaces without z-buffer, for memory / perf win here.
                        color_space,
                        format,
                        channel_bit_depth,
                        uv_rects,
                        (false, false),
                        None,
                    );

                    // Bind an appropriate YUV shader for the texture format kind
                    self.shaders
                        .borrow_mut()
                        .get_composite_shader(
                            CompositeSurfaceFormat::Yuv,
                            surface.image_buffer_kind,
                            instance.get_yuv_features(),
                        ).bind(
                            &mut self.device,
                            &projection,
                            None,
                            &mut self.renderer_errors,
                            &mut self.profile,
                            &mut self.command_log,
                        );

                    ( textures, instance )
                },
                ResolvedExternalSurfaceColorData::Rgb{ ref plane, .. } => {
                    let textures = BatchTextures::composite_rgb(plane.texture);
                    let uv_rect = self.texture_resolver.get_uv_rect(&textures.input.colors[0], plane.uv_rect);
                    let instance = CompositeInstance::new_rgb(
                        surface_rect.to_f32(),
                        surface_rect.to_f32(),
                        PremultipliedColorF::WHITE,
                        uv_rect,
                        plane.texture.uses_normalized_uvs(),
                        (false, false),
                        None,
                    );
                    let features = instance.get_rgb_features();

                    self.shaders
                        .borrow_mut()
                        .get_composite_shader(
                            CompositeSurfaceFormat::Rgba,
                            surface.image_buffer_kind,
                            features,
                        ).bind(
                            &mut self.device,
                            &projection,
                            None,
                            &mut self.renderer_errors,
                            &mut self.profile,
                            &mut self.command_log,
                        );

                    ( textures, instance )
                },
            };

            self.draw_instanced_batch(
                &[instance],
                VertexArrayKind::Composite,
                &textures,
                &mut results.stats,
            );

            self.device.end_render_pass(StoreOp::Store);

            self.compositor_config
                .compositor()
                .unwrap()
                .unbind();
        }

        self.gpu_profiler.finish_sampler(opaque_sampler);
    }

    /// Draw a list of tiles to the framebuffer
    fn draw_tile_list<'a, I: Iterator<Item = &'a occlusion::Item<OcclusionItemKey>>>(
        &mut self,
        tiles_iter: I,
        composite_state: &CompositeState,
        external_surfaces: &[ResolvedExternalSurface],
        projection: &default::Transform3D<f32>,
        stats: &mut RendererStats,
    ) {
        let mut current_shader_params = (
            CompositeSurfaceFormat::Rgba,
            ImageBufferKind::Texture2D,
            CompositeFeatures::empty(),
            None,
        );
        let mut current_textures = BatchTextures::empty();
        let mut instances = Vec::new();

        for item in tiles_iter {
            let tile = &composite_state.tiles[item.key.tile_index];

            let clip_rect = item.rectangle;
            let tile_rect = composite_state.get_device_rect(&tile.local_rect, tile.transform_index);
            let transform = composite_state.get_device_transform(tile.transform_index);
            let flip = (transform.scale.x < 0.0, transform.scale.y < 0.0);

            let clip = if item.key.needs_mask {
                tile.clip_index.map(|index| {
                    composite_state.get_compositor_clip(index)
                })
            } else {
                None
            };

            // Work out the draw params based on the tile surface
            let (instance, textures, shader_params) = match tile.surface {
                CompositeTileSurface::Color { color } => {
                    let dummy = TextureSource::Dummy;
                    let image_buffer_kind = dummy.image_buffer_kind();
                    let instance = CompositeInstance::new(
                        tile_rect,
                        clip_rect,
                        color.premultiplied(),
                        flip,
                        clip,
                    );
                    let features = instance.get_rgb_features();
                    (
                        instance,
                        BatchTextures::composite_rgb(dummy),
                        (CompositeSurfaceFormat::Rgba, image_buffer_kind, features, None),
                    )
                }
                CompositeTileSurface::Texture { surface: ResolvedSurfaceTexture::TextureCache { texture } } => {
                    let instance = CompositeInstance::new(
                        tile_rect,
                        clip_rect,
                        PremultipliedColorF::WHITE,
                        flip,
                        clip,
                    );
                    let features = instance.get_rgb_features();
                    (
                        instance,
                        BatchTextures::composite_rgb(texture),
                        (
                            CompositeSurfaceFormat::Rgba,
                            ImageBufferKind::Texture2D,
                            features,
                            None,
                        ),
                    )
                }
                CompositeTileSurface::ExternalSurface { external_surface_index } => {
                    let surface = &external_surfaces[external_surface_index.0];

                    match surface.color_data {
                        ResolvedExternalSurfaceColorData::Yuv{ ref planes, color_space, format, channel_bit_depth, .. } => {
                            let textures = BatchTextures::composite_yuv(
                                planes[0].texture,
                                planes[1].texture,
                                planes[2].texture,
                            );

                            // When the texture is an external texture, the UV rect is not known when
                            // the external surface descriptor is created, because external textures
                            // are not resolved until the lock() callback is invoked at the start of
                            // the frame render. To handle this, query the texture resolver for the
                            // UV rect if it's an external texture, otherwise use the default UV rect.
                            let uv_rects = [
                                self.texture_resolver.get_uv_rect(&textures.input.colors[0], planes[0].uv_rect),
                                self.texture_resolver.get_uv_rect(&textures.input.colors[1], planes[1].uv_rect),
                                self.texture_resolver.get_uv_rect(&textures.input.colors[2], planes[2].uv_rect),
                            ];

                            let instance = CompositeInstance::new_yuv(
                                tile_rect,
                                clip_rect,
                                color_space,
                                format,
                                channel_bit_depth,
                                uv_rects,
                                flip,
                                clip,
                            );
                            let features = instance.get_yuv_features();

                            (
                                instance,
                                textures,
                                (
                                    CompositeSurfaceFormat::Yuv,
                                    surface.image_buffer_kind,
                                    features,
                                    None
                                ),
                            )
                        },
                        ResolvedExternalSurfaceColorData::Rgb { ref plane, .. } => {
                            let uv_rect = self.texture_resolver.get_uv_rect(&plane.texture, plane.uv_rect);
                            let instance = CompositeInstance::new_rgb(
                                tile_rect,
                                clip_rect,
                                PremultipliedColorF::WHITE,
                                uv_rect,
                                plane.texture.uses_normalized_uvs(),
                                flip,
                                clip,
                            );
                            let features = instance.get_rgb_features();
                            (
                                instance,
                                BatchTextures::composite_rgb(plane.texture),
                                (
                                    CompositeSurfaceFormat::Rgba,
                                    surface.image_buffer_kind,
                                    features,
                                    Some(self.texture_resolver.get_texture_size(&plane.texture).to_f32()),
                                ),
                            )
                        },
                    }
                }
                CompositeTileSurface::Texture { surface: ResolvedSurfaceTexture::Native { .. } } => {
                    unreachable!("bug: found native surface in simple composite path");
                }
            };

            // Flush batch if shader params or textures changed
            let flush_batch = !current_textures.is_compatible_with(&textures) ||
                shader_params != current_shader_params;

            if flush_batch && !instances.is_empty() {
                self.shaders
                    .borrow_mut()
                    .get_composite_shader(
                        current_shader_params.0,
                        current_shader_params.1,
                        current_shader_params.2,
                    ).bind(
                        &mut self.device,
                        projection,
                        current_shader_params.3,
                        &mut self.renderer_errors,
                        &mut self.profile,
                        &mut self.command_log,
                    );
                self.draw_instanced_batch(
                    &instances,
                    VertexArrayKind::Composite,
                    &current_textures,
                    stats,
                );

                instances.clear();
            }
            current_shader_params = shader_params;
            current_textures = textures;

            // Add instance to current batch
            instances.push(instance);
        }

        // Flush the last batch
        if !instances.is_empty() {
            self.shaders
                .borrow_mut()
                .get_composite_shader(
                    current_shader_params.0,
                    current_shader_params.1,
                    current_shader_params.2,
                ).bind(
                    &mut self.device,
                    projection,
                    current_shader_params.3,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );
            self.draw_instanced_batch(
                &instances,
                VertexArrayKind::Composite,
                &current_textures,
                stats,
            );
        }
    }

    // Composite tiles in a swapchain. When using LayerCompositor, we may
    // split the compositing in to multiple swapchains.
    fn composite_pass(
        &mut self,
        composite_state: &CompositeState,
        draw_target: DrawTarget,
        clear_color: ColorF,
        projection: &default::Transform3D<f32>,
        results: &mut RenderResults,
        partial_present_mode: Option<PartialPresentMode>,
        layer: &SwapChainLayer,
    ) {
        let clear_color = clear_color.to_array();

        self.device.begin_render_pass(&RenderPassDescriptor {
            target: draw_target,
            render_area: None,
            // Partial present clears just the dirty rect below; otherwise the
            // entire framebuffer is cleared.
            color_load: match partial_present_mode {
                Some(PartialPresentMode::Single { .. }) => LoadOp::Load,
                None => LoadOp::Clear(clear_color),
            },
            depth_load: LoadOp::DontCare,
        });
        self.device.set_depth_write(false);
        self.device.set_depth_test(None);

        if let Some(PartialPresentMode::Single { dirty_rect }) = partial_present_mode {
            // There is no need to clear if the dirty rect is occluded. Additionally,
            // on Mali-G77 we have observed artefacts when calling glClear (even with
            // the empty scissor rect set) after calling eglSetDamageRegion with an
            // empty damage region. So avoid clearing in that case. See bug 1709548.
            if !dirty_rect.is_empty() && layer.occlusion.test(&dirty_rect) {
                // We have a single dirty rect, so clear only that
                self.device.clear_rect(
                    draw_target.to_framebuffer_rect(dirty_rect.to_i32()),
                    Some(clear_color),
                    None,
                );
            }
        }

        // Draw opaque tiles
        let opaque_items = layer.occlusion.opaque_items();
        if !opaque_items.is_empty() {
            let opaque_sampler = self.gpu_profiler.start_sampler(GPU_SAMPLER_TAG_OPAQUE);
            self.set_blend_mode(BlendMode::None, FramebufferKind::Main);
            self.draw_tile_list(
                opaque_items.iter(),
                &composite_state,
                &composite_state.external_surfaces,
                projection,
                &mut results.stats,
            );
            self.gpu_profiler.finish_sampler(opaque_sampler);
        }

        // Draw alpha tiles
        let alpha_items = layer.occlusion.alpha_items();
        if !alpha_items.is_empty() {
            let transparent_sampler = self.gpu_profiler.start_sampler(GPU_SAMPLER_TAG_TRANSPARENT);
            self.set_blend_mode(BlendMode::PremultipliedAlpha, FramebufferKind::Main);
            self.draw_tile_list(
                alpha_items.iter().rev(),
                &composite_state,
                &composite_state.external_surfaces,
                projection,
                &mut results.stats,
            );
            self.gpu_profiler.finish_sampler(transparent_sampler);
        }

        self.device.end_render_pass(StoreOp::Store);
    }

    /// Composite picture cache tiles into the framebuffer. This is currently
    /// the only way that picture cache tiles get drawn. In future, the tiles
    /// will often be handed to the OS compositor, and this method will be
    /// rarely used.
    fn composite_simple(
        &mut self,
        composite_state: &CompositeState,
        frame_device_size: DeviceIntSize,
        fb_draw_target: DrawTarget,
        projection: &default::Transform3D<f32>,
        results: &mut RenderResults,
        partial_present_mode: Option<PartialPresentMode>,
        device_size: DeviceIntSize,
    ) {
        let _gm = self.gpu_profiler.start_marker("framebuffer");
        let _timer = self.gpu_profiler.start_timer(GPU_TAG_COMPOSITE);

        let num_tiles = composite_state.tiles.len();
        self.profile.set(profiler::PICTURE_TILES, num_tiles);

        let layer_mode = layer_tree::layer_mode(&self.compositor_config);
        let allows_external_layers = layer_tree::allows_external_layers(
            &self.compositor_config,
            self.current_compositor_kind,
        );

        let (window_is_opaque, enable_screenshot) = {
            let props = layer_tree::window_properties(&self.compositor_config);
            (props.is_opaque, props.enable_screenshot)
        };

        let mut input_layers: Vec<CompositorInputLayer> = Vec::new();
        let mut swapchain_layers = Vec::new();
        let cap = composite_state.tiles.len();
        let mut segment_builder = SegmentBuilder::new();
        let mut tile_index_to_layer_index = vec![None; composite_state.tiles.len()];
        let mut full_render_occlusion = occlusion::FrontToBackBuilder::with_capacity(cap, cap);
        let mut layer_compositor_frame_state = LayerCompositorFrameState{
            tile_states: FastHashMap::default(),
            external_content_surfaces: Vec::new(),
        };

        // Select among surfaces that could otherwise be externally composited.
        let mut largest_sdr_yuv_tile = None;
        if self.limit_sdr_yuv_external_composites
            && layer_mode == LayerMode::Swapchains
            && !enable_screenshot
        {
            let frame_rect = DeviceRect::from_size(frame_device_size.to_f32());
            let mut largest_area = 0.0;
            for (idx, tile) in composite_state.tiles.iter().enumerate() {
                let external_surface_index = match tile.surface {
                    CompositeTileSurface::ExternalSurface { external_surface_index } => {
                        external_surface_index
                    }
                    _ => continue,
                };
                let surface = &composite_state.external_surfaces[external_surface_index.0];
                if surface.external_image_id.is_none()
                    || !matches!(
                        surface.color_data,
                        ResolvedExternalSurfaceColorData::Yuv { channel_bit_depth: 8, .. }
                    )
                {
                    continue;
                }

                let device_rect = composite_state.get_device_rect(
                    &tile.local_rect,
                    tile.transform_index,
                );
                // Match the existing external-composite size restriction (too big).
                if device_rect.try_cast::<i16>().is_none() {
                    continue;
                }
                let valid_rect = composite_state.get_device_rect(
                    &tile.local_valid_rect,
                    tile.transform_index,
                );
                let mut visible_rect = device_rect
                    .intersection_unchecked(&tile.device_clip_rect)
                    .intersection_unchecked(&valid_rect)
                    .intersection_unchecked(&frame_rect);
                if let Some(clip_index) = tile.clip_index {
                    visible_rect = visible_rect.intersection_unchecked(
                        &composite_state.get_compositor_clip(clip_index).rect,
                    );
                }
                if visible_rect.is_empty() {
                    continue;
                }
                let area = visible_rect.area();
                // Keep the first tile in front-to-back order on equal areas.
                if area > largest_area {
                    largest_area = area;
                    largest_sdr_yuv_tile = Some(idx);
                }
            }
        }

        // Calculate layers with full device rect

        // Add a debug overlay request if enabled
        if self.debug_overlay_state.is_enabled {
            self.debug_overlay_state.layer_index = input_layers.len();

            input_layers.push(CompositorInputLayer {
                usage: CompositorSurfaceUsage::DebugOverlay,
                is_opaque: false,
                offset: DeviceIntPoint::zero(),
                clip_rect: device_size.into(),
                rounded_clip_rect: device_size.into(),
                rounded_clip_radii: ClipRadius::EMPTY,
            });

            swapchain_layers.push(SwapChainLayer {
                occlusion: occlusion::FrontToBackBuilder::with_capacity(cap, cap),
            });
        }

        // NOTE: Tiles here are being iterated in front-to-back order by
        //       z-id, due to the sort in composite_state.end_frame()
        for (idx, tile) in composite_state.tiles.iter().enumerate() {
            let device_tile_box = composite_state.get_device_rect(
                &tile.local_rect,
                tile.transform_index
            );

            if layer_mode == LayerMode::Swapchains {
                match tile.tile_id {
                    Some(tile_id) => {
                        layer_compositor_frame_state.
                            tile_states
                            .insert(
                            tile_id,
                            CompositeTileState {
                                local_rect: tile.local_rect,
                                local_valid_rect: tile.local_valid_rect,
                                device_clip_rect: tile.device_clip_rect,
                                z_id: tile.z_id,
                                device_tile_box: device_tile_box,
                                visible_rects: Vec::new(),
                            },
                        );
                    }
                    None => {}
                }
            }

            // Simple compositor needs the valid rect in device space to match clip rect
            let device_valid_rect = composite_state
                .get_device_rect(&tile.local_valid_rect, tile.transform_index);

            let rect = device_tile_box
                .intersection_unchecked(&tile.device_clip_rect)
                .intersection_unchecked(&device_valid_rect);

            if rect.is_empty() {
                continue;
            }

            let mut disable_external_composite = enable_screenshot;
            if let CompositeTileSurface::ExternalSurface { external_surface_index } = tile.surface {
                let surface = &composite_state.external_surfaces[external_surface_index.0];
                if self.limit_sdr_yuv_external_composites
                    && matches!(
                        surface.color_data,
                        ResolvedExternalSurfaceColorData::Yuv { channel_bit_depth: 8, .. }
                    )
                    && largest_sdr_yuv_tile != Some(idx)
                {
                    // Fall back to the existing Content path. HDR and RGB
                    // surfaces are not affected by the SDR YUV limit.
                    disable_external_composite = true;
                }
                let transformed_rect = composite_state.get_device_rect(
                    &tile.local_rect,
                    tile.transform_index
                );
                if let None = transformed_rect.try_cast::<i16>() {
                    // Disable external composite when rect is big.
                    disable_external_composite = true;
                }
            }

            // Determine if the tile is an external surface or content
            let usage = match tile.surface {
                CompositeTileSurface::Texture { .. } |
                CompositeTileSurface::Color { .. } => {
                    CompositorSurfaceUsage::Content
                }
                CompositeTileSurface::ExternalSurface { external_surface_index } => {
                    if !allows_external_layers || disable_external_composite {
                        CompositorSurfaceUsage::Content
                    } else {
                        let surface = &composite_state.external_surfaces[external_surface_index.0];

                        // TODO(gwc): For now, we only select a hardware overlay swapchain if we
                        // have an external image, but it may make sense to do for compositor
                        // surfaces without in future.
                        match surface.external_image_id {
                            Some(external_image_id) => {
                                let image_key = match surface.color_data {
                                    ResolvedExternalSurfaceColorData::Rgb { image_dependency, .. } => image_dependency.key,
                                    ResolvedExternalSurfaceColorData::Yuv { image_dependencies, .. } => image_dependencies[0].key,
                                };

                                CompositorSurfaceUsage::External {
                                    image_key,
                                    external_image_id,
                                    transform_index: tile.transform_index,
                                }
                            }
                            None => {
                                CompositorSurfaceUsage::Content
                            }
                        }
                    }
                }
            };

            if layer_mode == LayerMode::Swapchains {
                if let CompositeTileSurface::ExternalSurface { .. } = tile.surface {
                    assert!(tile.tile_id.is_none());
                } else {
                    assert!(tile.tile_id.is_some());
                }
            }

            // Determine whether we need a new layer, and if so, what kind
            let new_layer_kind = match input_layers.last() {
                Some(curr_layer) => {
                    match (curr_layer.usage, usage) {
                        // Content -> content, composite in to same layer
                        (CompositorSurfaceUsage::Content, CompositorSurfaceUsage::Content) => None,
                        (CompositorSurfaceUsage::External { .. }, CompositorSurfaceUsage::Content) => Some(usage),

                        // Switch of layer type, or video -> video, need new swapchain
                        (CompositorSurfaceUsage::Content, CompositorSurfaceUsage::External { .. }) |
                        (CompositorSurfaceUsage::External { .. }, CompositorSurfaceUsage::External { .. }) => {
                            // Only create a new layer if we're using LayerCompositor
                            debug_assert!(allows_external_layers);
                            Some(usage)
                        }
                        (CompositorSurfaceUsage::DebugOverlay, _) => {
                            Some(usage)
                        }
                        // Should not encounter debug layers as new layer
                        (_, CompositorSurfaceUsage::DebugOverlay) => {
                            unreachable!();
                        }
                    }
                }
                None => {
                    // No layers yet, so we need a new one
                    Some(usage)
                }
            };

            if let Some(new_layer_kind) = new_layer_kind {
                let (offset, clip_rect, is_opaque, rounded_clip_rect, rounded_clip_radii) = match usage {
                    CompositorSurfaceUsage::Content => {
                        (
                            DeviceIntPoint::zero(),
                            device_size.into(),
                            false,      // Assume not opaque, we'll calculate this later
                            device_size.into(),
                            ClipRadius::EMPTY,
                        )
                    }
                    CompositorSurfaceUsage::External { .. } => {
                        let rect = composite_state.get_device_rect(
                            &tile.local_rect,
                            tile.transform_index
                        );

                        let clip_rect = tile.device_clip_rect.to_i32();
                        let is_opaque = tile.kind != TileKind::Alpha;

                        if self.debug_flags.contains(DebugFlags::EXTERNAL_COMPOSITE_BORDERS) {
                            self.external_composite_debug_items.push(DebugItem::Rect {
                                outer_color: debug_colors::ORANGERED,
                                inner_color: ColorF { r: 0.0, g: 0.0, b: 0.0, a: 0.0 },
                                rect: tile.device_clip_rect,
                                thickness: 10,
                            });
                        }

                        let (rounded_clip_rect, rounded_clip_radii) = match tile.clip_index {
                            Some(clip_index) => {
                                let clip = composite_state.get_compositor_clip(clip_index);
                                let radius = ClipRadius {
                                    top_left: clip.radius.top_left.width.round() as i32,
                                    top_right: clip.radius.top_right.width.round() as i32,
                                    bottom_left: clip.radius.bottom_left.width.round() as i32,
                                    bottom_right: clip.radius.bottom_right.width.round() as i32,
                                };
                                (clip.rect.to_i32(), radius)
                            }
                            None => {
                                (clip_rect, ClipRadius::EMPTY)
                            }
                        };

                        (
                            rect.min.to_i32(),
                            clip_rect,
                            is_opaque,
                            rounded_clip_rect,
                            rounded_clip_radii,
                        )
                    }
                    CompositorSurfaceUsage::DebugOverlay => unreachable!(),
                };

                input_layers.push(CompositorInputLayer {
                    usage: new_layer_kind,
                    is_opaque,
                    offset,
                    clip_rect,
                    rounded_clip_rect,
                    rounded_clip_radii,
                });

                swapchain_layers.push(SwapChainLayer {
                    occlusion: occlusion::FrontToBackBuilder::with_capacity(cap, cap),
                })
            }
            tile_index_to_layer_index[idx] = Some(input_layers.len() - 1);

            if layer_mode == LayerMode::Swapchains {
                if let (CompositeTileSurface::ExternalSurface { external_surface_index },
                        CompositorSurfaceUsage::Content) = (&tile.surface, usage) {
                    let surface = &composite_state.external_surfaces[external_surface_index.0];
                    let mut color_data = surface.color_data.clone();
                    // External textures get their actual UV rect during lock(),
                    // so retain the UV rect used to draw this frame as well.
                    match &mut color_data {
                        ResolvedExternalSurfaceColorData::Yuv { planes, .. } => {
                            for plane in planes {
                                plane.uv_rect = self.texture_resolver.get_uv_rect(
                                    &plane.texture,
                                    plane.uv_rect,
                                );
                            }
                        }
                        ResolvedExternalSurfaceColorData::Rgb { plane, .. } => {
                            plane.uv_rect = self.texture_resolver.get_uv_rect(
                                &plane.texture,
                                plane.uv_rect,
                            );
                        }
                    }
                    let clip = tile.clip_index.map(|index| {
                        let clip = composite_state.get_compositor_clip(index);
                        (clip.rect, clip.radius)
                    });
                    layer_compositor_frame_state.external_content_surfaces.push(
                        ExternalContentSurfaceState {
                            rect,
                            local_rect: tile.local_rect,
                            local_valid_rect: tile.local_valid_rect,
                            device_clip_rect: tile.device_clip_rect,
                            transform: composite_state.get_device_transform(tile.transform_index),
                            clip,
                            z_id: tile.z_id,
                            kind: tile.kind,
                            layer_index: input_layers.len() - 1,
                            external_image_id: surface.external_image_id,
                            image_buffer_kind: surface.image_buffer_kind,
                            color_data,
                        },
                    );
                }
            }

            // Caluclate actual visible tile's rects

            let is_opaque = tile.kind == TileKind::Opaque;

            match tile.clip_index {
                Some(clip_index) => {
                    let clip = composite_state.get_compositor_clip(clip_index);

                    // TODO(gw): Make segment builder generic on unit to avoid casts below.
                    segment_builder.initialize(
                        rect.cast_unit(),
                        None,
                    );
                    segment_builder.push_clip_rect(
                        clip.rect.cast_unit(),
                        Some(clip.radius),
                        None,
                        ClipMode::Clip,
                    );
                    segment_builder.build(|segment| {
                        let key = OcclusionItemKey { tile_index: idx, needs_mask: segment.has_mask };

                        full_render_occlusion.add(
                            &segment.rect.cast_unit(),
                            is_opaque && !segment.has_mask,
                            key,
                        );
                    });
                }
                None => {
                    full_render_occlusion.add(&rect, is_opaque, OcclusionItemKey {
                        tile_index: idx,
                        needs_mask: false,
                    });
                }
            }
        }

        assert_eq!(swapchain_layers.len(), input_layers.len());

        debug_assert!(
            layer_mode == LayerMode::Swapchains ||
            input_layers.iter().filter(|l| matches!(l.usage, CompositorSurfaceUsage::Content)).count() <= 1
        );

        if window_is_opaque {
            match input_layers.last_mut() {
                Some(_layer) => {
                    // If the window is opaque, and the last(back) layer is
                    //  a content layer then mark that as opaque.
                    // TODO: This causes talos performance regressions.
                    // if let CompositorSurfaceUsage::Content = layer.usage {
                    //     layer.is_opaque = true;
                    // }
                }
                None => {
                    // If no tiles were present, and we expect an opaque window,
                    // add an empty layer to force a composite that clears the screen,
                    // to match existing semantics.
                    input_layers.push(CompositorInputLayer {
                        usage: CompositorSurfaceUsage::Content,
                        is_opaque: true,
                        offset: DeviceIntPoint::zero(),
                        clip_rect: device_size.into(),
                        rounded_clip_rect: device_size.into(),
                        rounded_clip_radii: ClipRadius::EMPTY,
                    });

                    swapchain_layers.push(SwapChainLayer {
                        occlusion: occlusion::FrontToBackBuilder::with_capacity(cap, cap),
                    });
                }
            }
        }

        let mut full_render = self.debug_overlay_state.is_enabled;

        // Start compositing if using OS compositor
        {
            let input = CompositorInputConfig {
                enable_screenshot,
                layers: &input_layers,
            };
            full_render |= self.layer_tree.begin_frame(&mut self.compositor_config, &input);
        }

        // Full render is requested when layer tree is updated.
        let mut partial_present_mode = if full_render {
            None
        } else {
            partial_present_mode
        };

        assert_eq!(swapchain_layers.len(), input_layers.len());

        // Recalculate dirty rect for layer compositor
        if layer_mode == LayerMode::Swapchains {
            // Set visible rests of current frame to each tile's CompositeTileState.
            for item in full_render_occlusion
            .opaque_items()
            .iter()
            .chain(full_render_occlusion.alpha_items().iter()) {
                let tile = &composite_state.tiles[item.key.tile_index];
                match tile.tile_id {
                    Some(tile_id) => {
                        if let Some(tile_state) = layer_compositor_frame_state.tile_states.get_mut(&tile_id) {
                            tile_state.visible_rects.push(item.rectangle);
                        } else {
                            unreachable!();
                        }
                    }
                    None => {}
                }
            }

            let can_use_partial_present =
                !self.force_redraw && !full_render &&
                self.layer_compositor_frame_state_in_prev_frame.is_some();

            if can_use_partial_present {
                let mut combined_dirty_rect = DeviceRect::zero();

                for tile in composite_state.tiles.iter() {
                    if tile.tile_id.is_none() {
                        match tile.surface {
                            CompositeTileSurface::ExternalSurface { .. } => {}
                            CompositeTileSurface::Texture { .. }  |
                            CompositeTileSurface::Color { .. } => {
                                unreachable!();
                            },
                        }
                        continue;
                    }

                    assert!(tile.tile_id.is_some());

                    let tiles_exists_in_prev_frame =
                        self.layer_compositor_frame_state_in_prev_frame
                        .as_ref()
                        .unwrap()
                        .tile_states
                        .contains_key(&tile.tile_id.unwrap());
                    let tile_id = tile.tile_id.unwrap();
                    let tile_state = layer_compositor_frame_state.tile_states.get(&tile_id).unwrap();

                    if tiles_exists_in_prev_frame {
                        let prev_tile_state = self.layer_compositor_frame_state_in_prev_frame
                            .as_ref()
                            .unwrap()
                            .tile_states
                            .get(&tile_id)
                            .unwrap();

                        if tile_state.same_state(prev_tile_state) {
                            // Case that tile is same state in previous frame and current frame.
                            // Intersection of tile's dirty rect and tile's visible rects are actual dirty rects.
                            let dirty_rect = composite_state.get_device_rect(
                                &tile.local_dirty_rect,
                                tile.transform_index,
                            );
                            for rect in tile_state.visible_rects.iter()  {
                                let visible_dirty_rect = rect.intersection(&dirty_rect);
                                if visible_dirty_rect.is_some() {
                                    combined_dirty_rect = combined_dirty_rect.union(&visible_dirty_rect.unwrap());
                                }
                            }
                        } else {
                            // If tile is rendered in previous frame, but its state is different,
                            // both visible rects in previous frame and current frame are dirty rects.
                            for rect in tile_state.visible_rects
                                .iter()
                                .chain(prev_tile_state.visible_rects.iter())  {
                                combined_dirty_rect = combined_dirty_rect.union(&rect);
                            }
                        }
                    } else {
                        // If tile is not rendered in previous frame, its all visible rects are dirty rects.
                        for rect in &tile_state.visible_rects {
                            combined_dirty_rect = combined_dirty_rect.union(&rect);
                        }
                    }
                }

                // Case that tile is rendered in pervious frame, but not in current frame.
                for (tile_id, tile_state) in self.layer_compositor_frame_state_in_prev_frame
                    .as_ref()
                    .unwrap()
                    .tile_states
                    .iter() {
                    if !layer_compositor_frame_state.tile_states.contains_key(&tile_id) {
                        for rect in tile_state.visible_rects.iter()  {
                            combined_dirty_rect = combined_dirty_rect.union(&rect);
                        }
                    }
                }

                // Unchanged external surfaces already drawn into a content layer
                // need no damage of their own. Match each previous occurrence
                // only once, since an image may be drawn more than once.
                let prev_surfaces = &self.layer_compositor_frame_state_in_prev_frame
                    .as_ref()
                    .unwrap()
                    .external_content_surfaces;
                let mut matched = vec![false; prev_surfaces.len()];
                for surface in &layer_compositor_frame_state.external_content_surfaces {
                    let previous = prev_surfaces.iter().enumerate().position(|(index, prev)| {
                        !matched[index] && surface == prev
                    });
                    if let Some(index) = previous {
                        matched[index] = true;
                    } else {
                        // New, updated, moved, or previously externally composited.
                        combined_dirty_rect = combined_dirty_rect.union(&surface.rect);
                    }
                }
                for (index, surface) in prev_surfaces.iter().enumerate() {
                    if !matched[index] {
                        // Removed, changed, or now externally composited. Repaint
                        // the old content-layer pixels as well.
                        combined_dirty_rect = combined_dirty_rect.union(&surface.rect);
                    }
                }

                let device_rect = DeviceRect::from_size(device_size.to_f32());
                let clipped_dirty_rect = combined_dirty_rect.intersection_unchecked(&device_rect);

                partial_present_mode = Some(PartialPresentMode::Single {
                    dirty_rect: clipped_dirty_rect,
                });
            } else {
                partial_present_mode = None;
            }

            self.layer_compositor_frame_state_in_prev_frame = Some(layer_compositor_frame_state);
        }

        // Check tiles handling with partial_present_mode

        let mut opaque_rounded_corners: HashSet<CompositeRoundedCorner> = HashSet::new();

        // NOTE: Tiles here are being iterated in front-to-back order by
        //       z-id, due to the sort in composite_state.end_frame()
        for (idx, tile) in composite_state.tiles.iter().enumerate() {
            let device_tile_box = composite_state.get_device_rect(
                &tile.local_rect,
                tile.transform_index
            );

            // Determine a clip rect to apply to this tile, depending on what
            // the partial present mode is.
            let partial_clip_rect = match partial_present_mode {
                Some(PartialPresentMode::Single { dirty_rect }) => dirty_rect,
                None => device_tile_box,
            };

            // Simple compositor needs the valid rect in device space to match clip rect
            let device_valid_rect = composite_state
                .get_device_rect(&tile.local_valid_rect, tile.transform_index);

            let rect = device_tile_box
                .intersection_unchecked(&tile.device_clip_rect)
                .intersection_unchecked(&partial_clip_rect)
                .intersection_unchecked(&device_valid_rect);

            if rect.is_empty() {
                continue;
            }

            let layer_index = match tile_index_to_layer_index[idx] {
                None => {
                    // The rect of partial present should be subset of the rect of full render.
                    error!("rect {:?} should have valid layer index", rect);
                    continue;
                }
                Some(layer_index) => layer_index,
            };

            // For normal tiles, add to occlusion tracker
            let layer = &mut swapchain_layers[layer_index];

            let is_opaque = tile.kind == TileKind::Opaque;

            match tile.clip_index {
                Some(clip_index) => {
                    let clip = composite_state.get_compositor_clip(clip_index);

                        // TODO(gw): Make segment builder generic on unit to avoid casts below.
                    segment_builder.initialize(
                        rect.cast_unit(),
                        None,
                    );
                    segment_builder.push_clip_rect(
                        clip.rect.cast_unit(),
                        Some(clip.radius),
                        None,
                        ClipMode::Clip,
                    );
                    segment_builder.build(|segment| {
                        let key = OcclusionItemKey { tile_index: idx, needs_mask: segment.has_mask };

                        let radius = if segment.edge_flags ==
                            EdgeMask::TOP | EdgeMask::LEFT &&
                            !clip.radius.top_left.is_empty() {
                            Some(clip.radius.top_left)
                        } else if segment.edge_flags ==
                            EdgeMask::TOP | EdgeMask::RIGHT &&
                            !clip.radius.top_right.is_empty() {
                            Some(clip.radius.top_right)
                        } else if segment.edge_flags ==
                            EdgeMask::BOTTOM | EdgeMask::LEFT &&
                            !clip.radius.bottom_left.is_empty() {
                            Some(clip.radius.bottom_left)
                        } else if segment.edge_flags ==
                            EdgeMask::BOTTOM | EdgeMask::RIGHT &&
                            !clip.radius.bottom_right.is_empty() {
                            Some(clip.radius.bottom_right)
                        } else {
                            None
                        };

                        if let Some(radius) = radius {
                            let rounded_corner = CompositeRoundedCorner {
                                    rect: segment.rect.cast_unit(),
                                    radius: radius,
                                    edge_flags: segment.edge_flags,
                            };

                            // Drop overdraw rounded rect
                            if opaque_rounded_corners.contains(&rounded_corner) {
                                return;
                            }

                            if is_opaque {
                                opaque_rounded_corners.insert(rounded_corner);
                            }
                        }

                        layer.occlusion.add(
                            &segment.rect.cast_unit(),
                            is_opaque && !segment.has_mask,
                            key,
                        );
                    });
                }
                None => {
                    layer.occlusion.add(&rect, is_opaque, OcclusionItemKey {
                        tile_index: idx,
                        needs_mask: false,
                    });
                }
            }
        }

        assert_eq!(swapchain_layers.len(), input_layers.len());
        let mut content_clear_color = Some(self.clear_color);

        for (layer_index, (layer, swapchain_layer)) in input_layers.iter().zip(swapchain_layers.iter()).enumerate() {
            self.device.reset_state();

            // Skip compositing external images or debug layers here
            match layer.usage {
                CompositorSurfaceUsage::Content => {}
                CompositorSurfaceUsage::External { .. } | CompositorSurfaceUsage::DebugOverlay => {
                    continue;
                }
            }

            // Only use supplied clear color for first content layer we encounter
            let clear_color = content_clear_color.take().unwrap_or(ColorF::TRANSPARENT);

            if layer_mode == LayerMode::Swapchains {
                if let Some(PartialPresentMode::Single { dirty_rect }) = partial_present_mode {
                    let device_rect = DeviceRect::from_size(device_size.to_f32());
                    let clipped_dirty_rect = dirty_rect.intersection_unchecked(&device_rect);
                    if clipped_dirty_rect.is_empty() {
                        continue;
                    }
                }
            }

            let dirty_rect_storage;
            let dirty_rects: &[DeviceIntRect] = match partial_present_mode {
                Some(PartialPresentMode::Single { dirty_rect }) => {
                    dirty_rect_storage = [dirty_rect.to_i32()];
                    &dirty_rect_storage
                }
                None => &[],
            };

            self.layer_tree.bind_layer(&mut self.compositor_config, layer_index, dirty_rects);

            let draw_target = match layer_mode {
                LayerMode::Swapchains => DrawTarget::NativeSurface {
                    offset: -layer.offset,
                    handle: crate::composite::NativeSurfaceHandle::DEFAULT,
                    dimensions: frame_device_size,
                },
                LayerMode::Framebuffer => fb_draw_target,
            };

            // TODO(gwc): When supporting external attached swapchains, need to skip the composite pass here

            // Draw each compositing pass in to a swap chain
            self.composite_pass(
                composite_state,
                draw_target,
                clear_color,
                projection,
                results,
                partial_present_mode,
                swapchain_layer,
            );

            self.layer_tree.present_layer(&mut self.compositor_config, layer_index, dirty_rects);
        }

        // End frame notify for experimental compositor
        for (layer_index, layer) in input_layers.iter().enumerate().rev() {
            // External surfaces need transform applied, but content
            // surfaces are always at identity
            let transform = match layer.usage {
                CompositorSurfaceUsage::Content => CompositorSurfaceTransform::identity(),
                CompositorSurfaceUsage::External { transform_index, .. } => composite_state.get_compositor_transform(transform_index),
                CompositorSurfaceUsage::DebugOverlay => CompositorSurfaceTransform::identity(),
            };

            self.layer_tree.add_surface(
                &mut self.compositor_config,
                layer_index,
                transform,
                layer.clip_rect,
                ImageRendering::Auto,
                layer.rounded_clip_rect,
                layer.rounded_clip_radii,
            );
        }
    }

    pub(super) fn composite_frame(
        &mut self,
        frame: &mut Frame,
        device_size: Option<DeviceIntSize>,
        results: &mut RenderResults,
        present_mode: Option<PartialPresentMode>,
    ) {
        tracy_rs::profile_scope!("main target");
        if let Some(device_size) = device_size {
            if let Some(history) = &mut self.command_log {
                history.begin_render_target("Window", device_size);
            }

            results.stats.color_target_count += 1;
            results.picture_cache_debug = mem::replace(
                &mut frame.composite_state.picture_cache_debug,
                PictureCacheDebugInfo::new(),
            );
            results.did_rasterize_any_tile = frame.composite_state.did_rasterize_any_tile;

            let size = frame.device_rect.size().to_f32();
            let surface_origin_is_top_left = self.device.surface_origin_is_top_left();
            let (bottom, top) = if surface_origin_is_top_left {
                (0.0, size.height)
            } else {
                (size.height, 0.0)
            };

            let projection = Transform3D::ortho(
                0.0,
                size.width,
                bottom,
                top,
                self.device.ortho_near_plane(),
                self.device.ortho_far_plane(),
            );

            let fb_scale = Scale::<_, _, FramebufferPixel>::new(1i32);
            let mut fb_rect = frame.device_rect * fb_scale;

            if !surface_origin_is_top_left {
                let h = fb_rect.height();
                fb_rect.min.y = device_size.height - fb_rect.max.y;
                fb_rect.max.y = fb_rect.min.y + h;
            }

            let draw_target = DrawTarget::Default {
                rect: fb_rect,
                total_size: device_size * fb_scale,
                surface_origin_is_top_left,
            };

            // If we have a native OS compositor, then make use of that interface
            // to specify how to composite each of the picture cache surfaces.
            match self.current_compositor_kind {
                CompositorKind::Native { .. } => {
                    // We have already queued surfaces for early native composition by this point.
                    // All that is left is to finally update any external native surfaces that were
                    // invalidated so that composition can complete.
                    self.update_external_native_surfaces(
                        &frame.composite_state.external_surfaces,
                        results,
                    );
                }
                CompositorKind::Draw { .. } | CompositorKind::Layer { .. } => {
                    self.composite_simple(
                        &frame.composite_state,
                        frame.device_rect.size(),
                        draw_target,
                        &projection,
                        results,
                        present_mode,
                        device_size,
                    );
                }
            }
            // Reset force_redraw. It was used in composite_simple() with layer compositor.
            self.force_redraw = false;
        } else {
            // Rendering a frame without presenting it will confuse the partial
            // present logic, so force a full present for the next frame.
            self.force_redraw = true;
        }
    }

    /// Update the dirty rects based on current compositing mode and config
    // TODO(gw): This can be tidied up significantly once the Draw compositor
    //           is implemented in terms of the compositor trait.
    pub(super) fn calculate_dirty_rects(
        &mut self,
        buffer_age: usize,
        composite_state: &CompositeState,
        draw_target_dimensions: DeviceIntSize,
        results: &mut RenderResults,
    ) -> Option<PartialPresentMode> {
        if layer_tree::layer_mode(&self.compositor_config) == LayerMode::Swapchains {
            // Calculate dirty rects of layer compositor in composite_simple()
            return None;
        }

        let mut partial_present_mode = None;

        let (max_partial_present_rects, draw_previous_partial_present_regions) =
            match self.current_compositor_kind {
                CompositorKind::Native { .. } => {
                    // Assume that we can return a single dirty rect for native
                    // compositor for now, and that there is no buffer-age functionality.
                    // These params can be exposed by the compositor capabilities struct
                    // as the Draw compositor is ported to use it.
                    (1, false)
                }
                CompositorKind::Draw {
                    draw_previous_partial_present_regions,
                    max_partial_present_rects,
                } => (
                    max_partial_present_rects,
                    draw_previous_partial_present_regions,
                ),
                CompositorKind::Layer { .. } => {
                    unreachable!();
                }
            };

        if max_partial_present_rects > 0 {
            let prev_frames_damage_rect = if let Some(..) = self.compositor_config.partial_present()
            {
                self.buffer_damage_tracker
                    .get_damage_rect(buffer_age)
                    .or_else(|| Some(DeviceRect::from_size(draw_target_dimensions.to_f32())))
            } else {
                None
            };

            let can_use_partial_present = composite_state.dirty_rects_are_valid
                && !self.force_redraw
                && !(prev_frames_damage_rect.is_none() && draw_previous_partial_present_regions)
                && !self.debug_overlay_state.is_enabled;

            if can_use_partial_present {
                let mut combined_dirty_rect = DeviceRect::zero();
                let fb_rect = DeviceRect::from_size(draw_target_dimensions.to_f32());

                // Work out how many dirty rects WR produced, and if that's more than
                // what the device supports.
                for tile in &composite_state.tiles {
                    let dirty_rect = composite_state
                        .get_device_rect(&tile.local_dirty_rect, tile.transform_index);

                    // In pathological cases where a tile is extremely zoomed, it
                    // may end up with device coords outside the range of an i32,
                    // so clamp it to the frame buffer rect here, before it gets
                    // casted to an i32 rect below.
                    if let Some(dirty_rect) = dirty_rect.intersection(&fb_rect) {
                        combined_dirty_rect = combined_dirty_rect.union(&dirty_rect);
                    }
                }

                let combined_dirty_rect = combined_dirty_rect.round();
                let combined_dirty_rect_i32 = combined_dirty_rect.to_i32();
                // Return this frame's dirty region. If nothing has changed, don't return any dirty
                // rects at all (the client can use this as a signal to skip present completely).
                if !combined_dirty_rect.is_empty() {
                    results.dirty_rects.push(combined_dirty_rect_i32);
                }

                // Track this frame's dirty region, for calculating subsequent frames' damage.
                if draw_previous_partial_present_regions {
                    self.buffer_damage_tracker
                        .push_dirty_rect(&combined_dirty_rect);
                }

                // If the implementation requires manually keeping the buffer consistent,
                // then we must combine this frame's dirty region with that of previous frames
                // to determine the total_dirty_rect. The is used to determine what region we
                // render to, and is what we send to the compositor as the buffer damage region
                // (eg for KHR_partial_update).
                let total_dirty_rect = if draw_previous_partial_present_regions {
                    combined_dirty_rect.union(&prev_frames_damage_rect.unwrap())
                } else {
                    combined_dirty_rect
                };

                partial_present_mode = Some(PartialPresentMode::Single {
                    dirty_rect: total_dirty_rect,
                });
            } else {
                // If we don't have a valid partial present scenario, return a single
                // dirty rect to the client that covers the entire framebuffer.
                let fb_rect = DeviceIntRect::from_size(draw_target_dimensions);
                results.dirty_rects.push(fb_rect);

                if draw_previous_partial_present_regions {
                    self.buffer_damage_tracker
                        .push_dirty_rect(&fb_rect.to_f32());
                }
            }
        }

        partial_present_mode
    }
}
