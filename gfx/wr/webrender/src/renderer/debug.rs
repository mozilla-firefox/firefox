/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Drawing of the debug and profiler overlays.

use api::{ColorU, DebugFlags, ImageFormat, ImageBufferKind, ImageRendering, TextureCacheCategory};
use api::units::*;
use crate::composite::{ClipRadius, CompositorConfig, CompositorKind, CompositorSurfaceTransform};
use super::layer_tree::{self, LayerMode, LayerTree};
use crate::composite::{NativeSurfaceId, NativeTileId};
use crate::debug_colors;
use crate::debug_font_data;
use crate::debug_item::DebugItem;
use crate::device::{BlendMode, Buffer, BufferKind, Device, Program, Texture, TextureSlot, VertexArray, VertexDescriptor, ShaderError};
use crate::device::{DrawTarget, LoadOp, ReadTarget, RenderPassDescriptor, TextureFlags};
use crate::device::{TextureFilter, VertexAttribute, VertexAttributeKind, VertexUsageHint};
use euclid::{rect, Point2D, Rect, Size2D, Transform3D, default};
use crate::internal_types::{RenderTargetInfo, Swizzle};

use super::{PipelineInfo, TextureResolver};

#[derive(Debug, Copy, Clone)]
enum DebugSampler {
    Font,
}

impl Into<TextureSlot> for DebugSampler {
    fn into(self) -> TextureSlot {
        match self {
            DebugSampler::Font => TextureSlot(0),
        }
    }
}

const DESC_FONT: VertexDescriptor = VertexDescriptor {
    vertex_attributes: &[
        VertexAttribute {
            name: "aPosition",
            count: 2,
            kind: VertexAttributeKind::F32,
        },
        VertexAttribute {
            name: "aColor",
            count: 4,
            kind: VertexAttributeKind::U8Norm,
        },
        VertexAttribute {
            name: "aColorTexCoord",
            count: 2,
            kind: VertexAttributeKind::F32,
        },
    ],
    instance_attributes: &[],
};

const DESC_COLOR: VertexDescriptor = VertexDescriptor {
    vertex_attributes: &[
        VertexAttribute {
            name: "aPosition",
            count: 2,
            kind: VertexAttributeKind::F32,
        },
        VertexAttribute {
            name: "aColor",
            count: 4,
            kind: VertexAttributeKind::U8Norm,
        },
    ],
    instance_attributes: &[],
};

#[repr(C)]
pub struct DebugFontVertex {
    pub x: f32,
    pub y: f32,
    pub color: ColorU,
    pub u: f32,
    pub v: f32,
}

impl DebugFontVertex {
    pub fn new(x: f32, y: f32, u: f32, v: f32, color: ColorU) -> DebugFontVertex {
        DebugFontVertex { x, y, color, u, v }
    }
}

#[repr(C)]
pub struct DebugColorVertex {
    pub x: f32,
    pub y: f32,
    pub color: ColorU,
}

impl DebugColorVertex {
    pub fn new(x: f32, y: f32, color: ColorU) -> DebugColorVertex {
        DebugColorVertex { x, y, color }
    }
}

pub struct DebugRenderer {
    font_vertices: Vec<DebugFontVertex>,
    font_indices: Vec<u32>,
    font_program: Program,
    font_vao: VertexArray,
    font_vertex_buffer: Buffer,
    font_index_buffer: Buffer,
    font_texture: Texture,

    tri_vertices: Vec<DebugColorVertex>,
    tri_indices: Vec<u32>,
    tri_vao: VertexArray,
    tri_vertex_buffer: Buffer,
    tri_index_buffer: Buffer,
    line_vertices: Vec<DebugColorVertex>,
    line_vao: VertexArray,
    line_vertex_buffer: Buffer,
    color_program: Program,
}

impl DebugRenderer {
    pub fn new(device: &mut Device) -> Result<Self, ShaderError> {
        let font_program = device.create_program_linked(
            "debug_font",
            &[],
            &DESC_FONT,
            &[("sColor0", DebugSampler::Font.into())],
        )?;

        let color_program = device.create_program_linked(
            "debug_color",
            &[],
            &DESC_COLOR,
            &[],
        )?;

        let font_vertex_buffer = device.create_buffer(BufferKind::Vertex);
        let font_index_buffer = device.create_buffer(BufferKind::Index);
        let font_vao = device.create_vertex_array(&DESC_FONT, &font_vertex_buffer, None, Some(&font_index_buffer), 1);
        let line_vertex_buffer = device.create_buffer(BufferKind::Vertex);
        let line_vao = device.create_vertex_array(&DESC_COLOR, &line_vertex_buffer, None, None, 1);
        let tri_vertex_buffer = device.create_buffer(BufferKind::Vertex);
        let tri_index_buffer = device.create_buffer(BufferKind::Index);
        let tri_vao = device.create_vertex_array(&DESC_COLOR, &tri_vertex_buffer, None, Some(&tri_index_buffer), 1);

        let font_texture = device.create_texture(
            ImageBufferKind::Texture2D,
            ImageFormat::R8,
            debug_font_data::BMP_WIDTH,
            debug_font_data::BMP_HEIGHT,
            TextureFilter::Linear,
            None,
        );
        device.upload_texture_immediate(
            &font_texture,
            &debug_font_data::FONT_BITMAP
        );

        Ok(DebugRenderer {
            font_vertices: Vec::new(),
            font_indices: Vec::new(),
            line_vertices: Vec::new(),
            tri_vao,
            tri_vertex_buffer,
            tri_index_buffer,
            tri_vertices: Vec::new(),
            tri_indices: Vec::new(),
            font_program,
            color_program,
            font_vao,
            font_vertex_buffer,
            font_index_buffer,
            line_vao,
            line_vertex_buffer,
            font_texture,
        })
    }

    pub fn deinit(self, device: &mut Device) {
        device.delete_texture(self.font_texture);
        device.delete_program(self.font_program);
        device.delete_program(self.color_program);
        device.delete_vertex_array(self.tri_vao);
        device.delete_vertex_array(self.line_vao);
        device.delete_vertex_array(self.font_vao);
        device.delete_buffer(self.tri_vertex_buffer);
        device.delete_buffer(self.tri_index_buffer);
        device.delete_buffer(self.line_vertex_buffer);
        device.delete_buffer(self.font_vertex_buffer);
        device.delete_buffer(self.font_index_buffer);
    }

    pub fn line_height(&self) -> f32 {
        debug_font_data::FONT_SIZE as f32 * 1.1
    }

    /// Draws a line of text at the provided starting coordinates.
    ///
    /// If |bounds| is specified, glyphs outside the bounds are discarded.
    ///
    /// Y-coordinates is relative to screen top, along with everything else in
    /// this file.
    pub fn add_text(
        &mut self,
        x: f32,
        y: f32,
        text: &str,
        color: ColorU,
        bounds: Option<DeviceRect>,
    ) -> default::Rect<f32> {
        let mut x_start = x;
        let ipw = 1.0 / debug_font_data::BMP_WIDTH as f32;
        let iph = 1.0 / debug_font_data::BMP_HEIGHT as f32;

        let mut min_x = f32::MAX;
        let mut max_x = -f32::MAX;
        let mut min_y = f32::MAX;
        let mut max_y = -f32::MAX;

        for c in text.chars() {
            let c = c as usize - debug_font_data::FIRST_GLYPH_INDEX as usize;
            if c < debug_font_data::GLYPHS.len() {
                let glyph = &debug_font_data::GLYPHS[c];

                let x0 = (x_start + glyph.xo + 0.5).floor();
                let y0 = (y + glyph.yo + 0.5).floor();

                let x1 = x0 + glyph.x1 as f32 - glyph.x0 as f32;
                let y1 = y0 + glyph.y1 as f32 - glyph.y0 as f32;

                // If either corner of the glyph will end up out of bounds, drop it.
                if let Some(b) = bounds {
                    let rect = DeviceRect {
                        min: DevicePoint::new(x0, y0),
                        max: DevicePoint::new(x1, y1),
                    };
                    if !b.contains_box(&rect) {
                        continue;
                    }
                }

                let s0 = glyph.x0 as f32 * ipw;
                let t0 = glyph.y0 as f32 * iph;
                let s1 = glyph.x1 as f32 * ipw;
                let t1 = glyph.y1 as f32 * iph;

                x_start += glyph.xa;

                let vertex_count = self.font_vertices.len() as u32;

                self.font_vertices
                    .push(DebugFontVertex::new(x0, y0, s0, t0, color));
                self.font_vertices
                    .push(DebugFontVertex::new(x1, y0, s1, t0, color));
                self.font_vertices
                    .push(DebugFontVertex::new(x0, y1, s0, t1, color));
                self.font_vertices
                    .push(DebugFontVertex::new(x1, y1, s1, t1, color));

                self.font_indices.push(vertex_count + 0);
                self.font_indices.push(vertex_count + 1);
                self.font_indices.push(vertex_count + 2);
                self.font_indices.push(vertex_count + 2);
                self.font_indices.push(vertex_count + 1);
                self.font_indices.push(vertex_count + 3);

                min_x = min_x.min(x0);
                max_x = max_x.max(x1);
                min_y = min_y.min(y0);
                max_y = max_y.max(y1);
            }
        }

        Rect::new(
            Point2D::new(min_x, min_y),
            Size2D::new(max_x - min_x, max_y - min_y),
        )
    }

    pub fn add_quad(
        &mut self,
        x0: f32,
        y0: f32,
        x1: f32,
        y1: f32,
        color_top: ColorU,
        color_bottom: ColorU,
    ) {
        let vertex_count = self.tri_vertices.len() as u32;

        self.tri_vertices
            .push(DebugColorVertex::new(x0, y0, color_top));
        self.tri_vertices
            .push(DebugColorVertex::new(x1, y0, color_top));
        self.tri_vertices
            .push(DebugColorVertex::new(x0, y1, color_bottom));
        self.tri_vertices
            .push(DebugColorVertex::new(x1, y1, color_bottom));

        self.tri_indices.push(vertex_count + 0);
        self.tri_indices.push(vertex_count + 1);
        self.tri_indices.push(vertex_count + 2);
        self.tri_indices.push(vertex_count + 2);
        self.tri_indices.push(vertex_count + 1);
        self.tri_indices.push(vertex_count + 3);
    }

    #[allow(dead_code)]
    pub fn add_line(&mut self, x0: i32, y0: i32, color0: ColorU, x1: i32, y1: i32, color1: ColorU) {
        self.line_vertices
            .push(DebugColorVertex::new(x0 as f32, y0 as f32, color0));
        self.line_vertices
            .push(DebugColorVertex::new(x1 as f32, y1 as f32, color1));
    }


    pub fn add_rect(&mut self, rect: &DeviceIntRect, thickness: i32, color: ColorU) {
        let p0 = rect.min;
        let p1 = rect.max;
        if thickness > 1 && rect.width() > thickness * 2 && rect.height() > thickness * 2 {
            let w = thickness as f32;
            let p0 = p0.to_f32();
            let p1 = p1.to_f32();
            self.add_quad(p0.x, p0.y, p1.x, p0.y + w, color, color);
            self.add_quad(p1.x - w, p0.y + w, p1.x, p1.y - w, color, color);
            self.add_quad(p0.x, p1.y - w, p1.x, p1.y, color, color);
            self.add_quad(p0.x, p0.y + w, p0.x + w, p1.y - w, color, color);
        } else {
            self.add_line(p0.x, p0.y, color, p1.x, p0.y, color);
            self.add_line(p1.x, p0.y, color, p1.x, p1.y, color);
            self.add_line(p1.x, p1.y, color, p0.x, p1.y, color);
            self.add_line(p0.x, p1.y, color, p0.x, p0.y, color);    
        }
    }

    pub fn render(
        &mut self,
        device: &mut Device,
        viewport_size: Option<DeviceIntSize>,
        scale: f32,
        surface_origin_is_top_left: bool,
    ) {
        if let Some(viewport_size) = viewport_size {
            device.set_depth_test(None);
            device.set_blend_mode(BlendMode::PremultipliedAlpha);

            let (bottom, top) = if surface_origin_is_top_left {
                (0.0, viewport_size.height as f32 * scale)
            } else {
                (viewport_size.height as f32 * scale, 0.0)
            };

            let projection = Transform3D::ortho(
                0.0,
                viewport_size.width as f32 * scale,
                bottom,
                top,
                device.ortho_near_plane(),
                device.ortho_far_plane(),
            );

            // Triangles
            if !self.tri_vertices.is_empty() {
                device.bind_program(&self.color_program);
                device.set_uniforms(&self.color_program, &projection);
                device.bind_vertex_array(&self.tri_vao);
                device.write_buffer(&mut self.tri_index_buffer, &self.tri_indices, VertexUsageHint::Dynamic);
                device.write_buffer(&mut self.tri_vertex_buffer, &self.tri_vertices, VertexUsageHint::Dynamic);
                device.draw_triangles_u32(0, self.tri_indices.len() as i32);
            }

            // Lines
            if !self.line_vertices.is_empty() {
                device.bind_program(&self.color_program);
                device.set_uniforms(&self.color_program, &projection);
                device.bind_vertex_array(&self.line_vao);
                device.write_buffer(&mut self.line_vertex_buffer, &self.line_vertices, VertexUsageHint::Dynamic);
                device.draw_nonindexed_lines(0, self.line_vertices.len() as i32);
            }

            // Glyph
            if !self.font_indices.is_empty() {
                device.bind_program(&self.font_program);
                device.set_uniforms(&self.font_program, &projection);
                device.bind_texture(DebugSampler::Font, &self.font_texture, Swizzle::default());
                device.bind_vertex_array(&self.font_vao);
                device.write_buffer(&mut self.font_index_buffer, &self.font_indices, VertexUsageHint::Dynamic);
                device.write_buffer(&mut self.font_vertex_buffer, &self.font_vertices, VertexUsageHint::Dynamic);
                device.draw_triangles_u32(0, self.font_indices.len() as i32);
            }
        }

        self.font_indices.clear();
        self.font_vertices.clear();
        self.line_vertices.clear();
        self.tri_vertices.clear();
        self.tri_indices.clear();
    }
}

pub struct LazyInitializedDebugRenderer {
    debug_renderer: Option<DebugRenderer>,
    failed: bool,
}

impl LazyInitializedDebugRenderer {
    pub fn new() -> Self {
        Self {
            debug_renderer: None,
            failed: false,
        }
    }

    pub fn get_mut<'a>(&'a mut self, device: &mut Device) -> Option<&'a mut DebugRenderer> {
        if self.failed {
            return None;
        }
        if self.debug_renderer.is_none() {
            match DebugRenderer::new(device) {
                Ok(renderer) => { self.debug_renderer = Some(renderer); }
                Err(_) => {
                    // The shader compilation code already logs errors.
                    self.failed = true;
                }
            }
        }

        self.debug_renderer.as_mut()
    }

    /// Returns mut ref to `debug::DebugRenderer` if one already exists, otherwise returns `None`.
    pub fn try_get_mut<'a>(&'a mut self) -> Option<&'a mut DebugRenderer> {
        self.debug_renderer.as_mut()
    }

    pub fn deinit(self, device: &mut Device) {
        if let Some(debug_renderer) = self.debug_renderer {
            debug_renderer.deinit(device);
        }
    }
}

/// Information about the state of the debugging / profiler overlay in native compositing mode.
pub struct DebugOverlayState {
    /// True if any of the current debug flags will result in drawing a debug overlay.
    pub is_enabled: bool,

    /// The current size of the debug overlay surface. None implies that the
    /// debug surface isn't currently allocated.
    pub current_size: Option<DeviceIntSize>,

    pub layer_index: usize,
}

impl DebugOverlayState {
    pub fn new() -> Self {
        DebugOverlayState {
            is_enabled: false,
            current_size: None,
            layer_index: 0,
        }
    }
}

/// Update the state of any debug / profiler overlays. This is currently only needed
/// when running with the native compositor enabled.
pub fn update_debug_overlay(
    compositor_config: &mut CompositorConfig,
    compositor_kind: CompositorKind,
    state: &mut DebugOverlayState,
    debug_flags: DebugFlags,
    framebuffer_size: DeviceIntSize,
    has_debug_items: bool,
) {
    // If any of the following debug flags are set, something will be drawn on the debug overlay.
    state.is_enabled = has_debug_items || debug_flags.intersects(
        DebugFlags::PROFILER_DBG |
        DebugFlags::RENDER_TARGET_DBG |
        DebugFlags::TEXTURE_CACHE_DBG |
        DebugFlags::EPOCHS |
        DebugFlags::PICTURE_CACHING_DBG |
        DebugFlags::PICTURE_BORDERS |
        DebugFlags::ZOOM_DBG |
        DebugFlags::WINDOW_VISIBILITY_DBG |
        DebugFlags::EXTERNAL_COMPOSITE_BORDERS
    );

    // Update the debug overlay surface, if we are running in native compositor mode.
    if let CompositorKind::Native { .. } = compositor_kind {
        let compositor = compositor_config.compositor().unwrap();

        // If there is a current surface, destroy it if we don't need it for this frame, or if
        // the size has changed.
        if let Some(current_size) = state.current_size {
            if !state.is_enabled || current_size != framebuffer_size {
                compositor.destroy_surface(NativeSurfaceId::DEBUG_OVERLAY);
                state.current_size = None;
            }
        }

        // Allocate a new surface, if we need it and there isn't one.
        if state.is_enabled && state.current_size.is_none() {
            compositor.create_surface(
                NativeSurfaceId::DEBUG_OVERLAY,
                framebuffer_size,
                false,
            );
            compositor.create_tile(NativeTileId::DEBUG_OVERLAY);
            state.current_size = Some(framebuffer_size);
        }
    }
}

/// Bind a draw target for the debug / profiler overlays, if required.
pub fn bind_debug_overlay(
    device: &mut Device,
    compositor_config: &mut CompositorConfig,
    layer_tree: &mut LayerTree,
    compositor_kind: CompositorKind,
    state: &DebugOverlayState,
    device_size: DeviceIntSize,
) -> Option<DrawTarget> {
    // Debug overlay setup are only required in native compositing mode
    if state.is_enabled {
        match compositor_kind {
            CompositorKind::Native { .. } => {
                let compositor = compositor_config.compositor().unwrap();
                let surface_size = state.current_size.unwrap();

                // Ensure old surface is invalidated before binding
                compositor.invalidate_tile(
                    NativeTileId::DEBUG_OVERLAY,
                    DeviceIntRect::from_size(surface_size),
                );
                // Bind the native surface
                let surface_info = compositor.bind(
                    NativeTileId::DEBUG_OVERLAY,
                    DeviceIntRect::from_size(surface_size),
                    DeviceIntRect::from_size(surface_size),
                );

                // Bind the native surface to current FBO target
                let draw_target = DrawTarget::NativeSurface {
                    offset: surface_info.origin,
                    handle: surface_info.handle,
                    dimensions: surface_size,
                };
                // When native compositing, clear the debug overlay each frame.
                device.begin_render_pass(&RenderPassDescriptor {
                    target: draw_target,
                    render_area: None,
                    color_load: LoadOp::Clear([0.0, 0.0, 0.0, 0.0]),
                    depth_load: LoadOp::DontCare,
                });

                Some(draw_target)
            }
            CompositorKind::Draw { .. } | CompositorKind::Layer { .. } => {
                match layer_tree::layer_mode(compositor_kind) {
                    LayerMode::Swapchains => {
                        layer_tree.bind_layer(compositor_config, state.layer_index, &[]);

                        let draw_target = DrawTarget::new_default(device_size, device.surface_origin_is_top_left());
                        device.begin_render_pass(&RenderPassDescriptor {
                            target: draw_target,
                            render_area: None,
                            color_load: LoadOp::Clear([0.0, 0.0, 0.0, 0.0]),
                            depth_load: LoadOp::DontCare,
                        });

                        Some(draw_target)
                    }
                    LayerMode::Framebuffer => {
                        // If we're not using the native compositor, the overlay is drawn
                        // over the frame in the default frame buffer.
                        let draw_target = DrawTarget::new_default(device_size, device.surface_origin_is_top_left());
                        device.begin_render_pass(&RenderPassDescriptor {
                            target: draw_target,
                            render_area: None,
                            color_load: LoadOp::Load,
                            depth_load: LoadOp::DontCare,
                        });

                        Some(draw_target)
                    }
                }
            }
        }
    } else {
        None
    }
}

/// Unbind the draw target for debug / profiler overlays, if required.
pub fn unbind_debug_overlay(
    compositor_config: &mut CompositorConfig,
    layer_tree: &mut LayerTree,
    compositor_kind: CompositorKind,
    state: &DebugOverlayState,
) {
    // Debug overlay setup are only required in native compositing mode
    if state.is_enabled {
        match compositor_kind {
            CompositorKind::Native { .. } => {
                let compositor = compositor_config.compositor().unwrap();
                // Unbind the draw target and add it to the visual tree to be composited
                compositor.unbind();

                let clip_rect = DeviceIntRect::from_size(
                    state.current_size.unwrap(),
                );

                compositor.add_surface(
                    NativeSurfaceId::DEBUG_OVERLAY,
                    CompositorSurfaceTransform::identity(),
                    clip_rect,
                    ImageRendering::Auto,
                    clip_rect,
                    ClipRadius::EMPTY,
                );
            }
            CompositorKind::Draw { .. } | CompositorKind::Layer { .. } => {
                match layer_tree::layer_mode(compositor_kind) {
                    LayerMode::Swapchains => {
                        layer_tree.present_layer(compositor_config, state.layer_index, &[]);
                    }
                    LayerMode::Framebuffer => {}
                }
            }
        }
    }
}

pub fn draw_frame_debug_items(
    device: &mut Device,
    debug: &mut LazyInitializedDebugRenderer,
    items: &[DebugItem],
) {
    if items.is_empty() {
        return;
    }

    let debug_renderer = match debug.get_mut(device) {
        Some(render) => render,
        None => return,
    };

    for item in items {
        match item {
            DebugItem::Rect { rect, outer_color, inner_color, thickness } => {
                if inner_color.a > 0.001 {
                    let rect = rect.inflate(-thickness as f32, -thickness as f32);
                    debug_renderer.add_quad(
                        rect.min.x,
                        rect.min.y,
                        rect.max.x,
                        rect.max.y,
                        (*inner_color).into(),
                        (*inner_color).into(),
                    );
                }

                if outer_color.a > 0.001 {
                    debug_renderer.add_rect(
                        &rect.to_i32(),
                        *thickness,
                        (*outer_color).into(),
                    );
                }
            }
            DebugItem::Text { ref msg, position, color } => {
                debug_renderer.add_text(
                    position.x,
                    position.y,
                    msg,
                    (*color).into(),
                    None,
                );
            }
        }
    }
}

pub fn draw_render_target_debug(
    device: &mut Device,
    debug: &mut LazyInitializedDebugRenderer,
    debug_flags: DebugFlags,
    texture_resolver: &TextureResolver,
    draw_target: &DrawTarget,
) {
    if !debug_flags.contains(DebugFlags::RENDER_TARGET_DBG) {
        return;
    }

    let debug_renderer = match debug.get_mut(device) {
        Some(render) => render,
        None => return,
    };

    let textures = texture_resolver
        .texture_cache_map
        .values()
        .filter(|item| item.category == TextureCacheCategory::RenderTarget)
        .map(|item| &item.texture)
        .collect::<Vec<&Texture>>();

    do_debug_blit(
        device,
        debug_renderer,
        textures,
        draw_target,
        0,
        &|_| [0.0, 1.0, 0.0, 1.0], // Use green for all RTs.
    );
}

pub fn draw_zoom_debug(
    device: &mut Device,
    debug: &mut LazyInitializedDebugRenderer,
    debug_flags: DebugFlags,
    zoom_debug_texture: &mut Option<Texture>,
    cursor_position: DeviceIntPoint,
    device_size: DeviceIntSize,
) {
    if !debug_flags.contains(DebugFlags::ZOOM_DBG) {
        return;
    }

    let debug_renderer = match debug.get_mut(device) {
        Some(render) => render,
        None => return,
    };

    let source_size = DeviceIntSize::new(64, 64);
    let target_size = DeviceIntSize::new(1024, 1024);

    let source_origin = DeviceIntPoint::new(
        (cursor_position.x - source_size.width / 2)
            .min(device_size.width - source_size.width)
            .max(0),
        (cursor_position.y - source_size.height / 2)
            .min(device_size.height - source_size.height)
            .max(0),
    );

    let source_rect = DeviceIntRect::from_origin_and_size(
        source_origin,
        source_size,
    );

    let target_rect = DeviceIntRect::from_origin_and_size(
        DeviceIntPoint::new(
            device_size.width - target_size.width - 64,
            device_size.height - target_size.height - 64,
        ),
        target_size,
    );

    let texture_rect = FramebufferIntRect::from_size(
        source_rect.size().cast_unit(),
    );

    debug_renderer.add_rect(
        &target_rect.inflate(1, 1),
        1,
        debug_colors::RED.into(),
    );

    if zoom_debug_texture.is_none() {
        let texture = device.create_texture(
            ImageBufferKind::Texture2D,
            ImageFormat::BGRA8,
            source_rect.width(),
            source_rect.height(),
            TextureFilter::Nearest,
            Some(RenderTargetInfo { has_depth: false }),
        );

        *zoom_debug_texture = Some(texture);
    }

    // Copy frame buffer into the zoom texture
    let read_target = DrawTarget::new_default(device_size, device.surface_origin_is_top_left());
    device.blit_render_target(
        read_target.into(),
        read_target.to_framebuffer_rect(source_rect),
        DrawTarget::from_texture(
            zoom_debug_texture.as_ref().unwrap(),
            false,
        ),
        texture_rect,
        TextureFilter::Nearest,
    );

    // Draw the zoom texture back to the framebuffer
    device.blit_render_target(
        ReadTarget::from_texture(
            zoom_debug_texture.as_ref().unwrap(),
        ),
        texture_rect,
        read_target,
        read_target.to_framebuffer_rect(target_rect),
        TextureFilter::Nearest,
    );
}

pub fn draw_texture_cache_debug(
    device: &mut Device,
    debug: &mut LazyInitializedDebugRenderer,
    debug_flags: DebugFlags,
    texture_resolver: &TextureResolver,
    draw_target: &DrawTarget,
) {
    if !debug_flags.contains(DebugFlags::TEXTURE_CACHE_DBG) {
        return;
    }

    let debug_renderer = match debug.get_mut(device) {
        Some(render) => render,
        None => return,
    };

    let textures = texture_resolver
        .texture_cache_map
        .values()
        .filter(|item| item.category == TextureCacheCategory::Atlas)
        .map(|item| &item.texture)
        .collect::<Vec<&Texture>>();

    fn select_color(texture: &Texture) -> [f32; 4] {
        if texture.flags().contains(TextureFlags::IS_SHARED_TEXTURE_CACHE) {
            [1.0, 0.5, 0.0, 1.0] // Orange for shared.
        } else {
            [1.0, 0.0, 1.0, 1.0] // Fuchsia for standalone.
        }
    }

    do_debug_blit(
        device,
        debug_renderer,
        textures,
        draw_target,
        if debug_flags.contains(DebugFlags::RENDER_TARGET_DBG) { 544 } else { 0 },
        &select_color,
    );
}

fn do_debug_blit(
    device: &mut Device,
    debug_renderer: &mut DebugRenderer,
    mut textures: Vec<&Texture>,
    draw_target: &DrawTarget,
    bottom: i32,
    select_color: &dyn Fn(&Texture) -> [f32; 4],
) {
    let mut spacing = 16;
    let mut size = 512;

    let device_size = draw_target.dimensions();
    let fb_width = device_size.width;
    let fb_height = device_size.height;
    let surface_origin_is_top_left = draw_target.surface_origin_is_top_left();

    let num_textures = textures.len() as i32;

    if num_textures * (size + spacing) > fb_width {
        let factor = fb_width as f32 / (num_textures * (size + spacing)) as f32;
        size = (size as f32 * factor) as i32;
        spacing = (spacing as f32 * factor) as i32;
    }

    let text_height = 14; // Visually approximated.
    let text_margin = 1;
    let tag_height = text_height + text_margin * 2;
    let tag_y = fb_height - (bottom + spacing + tag_height);
    let image_y = tag_y - size;

    // Sort the display by size (in bytes), so that left-to-right is
    // largest-to-smallest.
    //
    // Note that the vec here is in increasing order, because the elements
    // get drawn right-to-left.
    textures.sort_by_key(|t| t.size_in_bytes());

    let mut i = 0;
    for texture in textures.iter() {
        let dimensions = texture.get_dimensions();
        let src_rect = FramebufferIntRect::from_size(
            FramebufferIntSize::new(dimensions.width as i32, dimensions.height as i32),
        );

        let x = fb_width - (spacing + size) * (i as i32 + 1);

        // If we have more targets than fit on one row in screen, just early exit.
        if x > fb_width {
            return;
        }

        // Draw the info tag.
        let tag_rect = rect(x, tag_y, size, tag_height).to_box2d();
        let tag_color = select_color(texture);
        device.clear_rect(
            draw_target.to_framebuffer_rect(tag_rect),
            Some(tag_color),
            None,
        );

        // Draw the dimensions onto the tag.
        let dim = texture.get_dimensions();
        let text_rect = tag_rect.inflate(-text_margin, -text_margin);
        debug_renderer.add_text(
            text_rect.min.x as f32,
            text_rect.max.y as f32, // Top-relative.
            &format!("{}x{}", dim.width, dim.height),
            ColorU::new(0, 0, 0, 255),
            Some(tag_rect.to_f32())
        );

        // Blit the contents of the texture.
        let dest_rect = draw_target.to_framebuffer_rect(rect(x, image_y, size, size).to_box2d());
        let read_target = ReadTarget::from_texture(texture);

        if surface_origin_is_top_left {
            device.blit_render_target(
                read_target,
                src_rect,
                *draw_target,
                dest_rect,
                TextureFilter::Linear,
            );
        } else {
             // Invert y.
             device.blit_render_target_invert_y(
                read_target,
                src_rect,
                *draw_target,
                dest_rect,
            );
        }
        i += 1;
    }
}

pub fn draw_epoch_debug(
    device: &mut Device,
    debug: &mut LazyInitializedDebugRenderer,
    debug_flags: DebugFlags,
    pipeline_info: &PipelineInfo,
) {
    if !debug_flags.contains(DebugFlags::EPOCHS) {
        return;
    }

    let debug_renderer = match debug.get_mut(device) {
        Some(render) => render,
        None => return,
    };

    let dy = debug_renderer.line_height();
    let x0: f32 = 30.0;
    let y0: f32 = 30.0;
    let mut y = y0;
    let mut text_width = 0.0;
    for ((pipeline, document_id), epoch) in  &pipeline_info.epochs {
        y += dy;
        let w = debug_renderer.add_text(
            x0, y,
            &format!("({:?}, {:?}): {:?}", pipeline, document_id, epoch),
            ColorU::new(255, 255, 0, 255),
            None,
        ).size.width;
        text_width = f32::max(text_width, w);
    }

    let margin = 10.0;
    debug_renderer.add_quad(
        x0 - margin,
        y0 - margin,
        x0 + text_width + margin,
        y + margin,
        ColorU::new(25, 25, 25, 200),
        ColorU::new(51, 51, 51, 200),
    );
}

pub fn draw_window_visibility_debug(
    device: &mut Device,
    debug: &mut LazyInitializedDebugRenderer,
    debug_flags: DebugFlags,
    compositor_config: &mut CompositorConfig,
) {
    if !debug_flags.contains(DebugFlags::WINDOW_VISIBILITY_DBG) {
        return;
    }

    let debug_renderer = match debug.get_mut(device) {
        Some(render) => render,
        None => return,
    };

    let x: f32 = 30.0;
    let y: f32 = 40.0;

    if let CompositorConfig::Native { ref mut compositor, .. } = *compositor_config {
        let visibility = compositor.get_window_visibility();
        let color = if visibility.is_fully_occluded {
            ColorU::new(255, 0, 0, 255)

        } else {
            ColorU::new(0, 0, 255, 255)
        };

        debug_renderer.add_text(
            x, y,
            &format!("{:?}", visibility),
            color,
            None,
        );
    }
}

pub fn draw_external_composite_borders_debug(
    device: &mut Device,
    debug: &mut LazyInitializedDebugRenderer,
    debug_flags: DebugFlags,
    items: &[DebugItem],
) {
    if !debug_flags.contains(DebugFlags::EXTERNAL_COMPOSITE_BORDERS) {
        return;
    }

    let debug_renderer = match debug.get_mut(device) {
        Some(render) => render,
        None => return,
    };

    for item in items {
        match item {
            DebugItem::Rect { rect, outer_color, inner_color: _, thickness } => {
                if outer_color.a > 0.001 {
                    debug_renderer.add_rect(
                        &rect.to_i32(),
                        *thickness,
                        (*outer_color).into(),
                    );
                }
            }
            DebugItem::Text { .. } => {}
        }
    }
}
