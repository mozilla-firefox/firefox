/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{ImageBufferKind, units::DeviceSize};
use crate::batch::{BatchKey, BatchKind, BrushBatchKind, BatchFeatures};
use crate::composite::{CompositeFeatures, CompositeSurfaceFormat};
use crate::device::{Device, Program, ShaderError};
use crate::pattern::PatternKind;
use crate::telemetry::Telemetry;
use euclid::default::Transform3D;
use glyph_rasterizer::GlyphFormat;
use crate::renderer::{
    desc,
    BlendMode, DebugFlags, RendererError, WebRenderOptions,
    TextureSampler, VertexArrayKind, ShaderPrecacheFlags,
};
use crate::profiler::{self, TransactionProfile, ns_to_ms};

use gleam::gl::GlType;
use time::precise_time_ns;

use std::cell::RefCell;
use std::collections::VecDeque;
use std::rc::Rc;

use webrender_build::shader::{ShaderFeatures, ShaderFeatureFlags, get_shader_features};

/// Which extension version to use for texture external support.
#[derive(Clone, Copy, Debug, PartialEq)]
enum TextureExternalVersion {
    // GL_OES_EGL_image_external_essl3 (Compatible with ESSL 3.0 and
    // later shaders, but not supported on all GLES 3 devices.)
    ESSL3,
    // GL_OES_EGL_image_external (Compatible with ESSL 1.0 shaders)
    ESSL1,
}

fn get_feature_string(kind: ImageBufferKind, texture_external_version: TextureExternalVersion) -> &'static str {
    match (kind, texture_external_version) {
        (ImageBufferKind::Texture2D, _) => "TEXTURE_2D",
        (ImageBufferKind::TextureRect, _) => "TEXTURE_RECT",
        (ImageBufferKind::TextureExternal, TextureExternalVersion::ESSL3) => "TEXTURE_EXTERNAL",
        (ImageBufferKind::TextureExternal, TextureExternalVersion::ESSL1) => "TEXTURE_EXTERNAL_ESSL1",
        (ImageBufferKind::TextureExternalBT709, _) => "TEXTURE_EXTERNAL_BT709",
    }
}

fn has_platform_support(kind: ImageBufferKind, device: &Device) -> bool {
    match (kind, device.gl().get_type()) {
        (ImageBufferKind::Texture2D, _) => true,
        (ImageBufferKind::TextureRect, GlType::Gles) => false,
        (ImageBufferKind::TextureRect, GlType::Gl) => true,
        (ImageBufferKind::TextureExternal, GlType::Gles) => true,
        (ImageBufferKind::TextureExternal, GlType::Gl) => false,
        (ImageBufferKind::TextureExternalBT709, GlType::Gles) => device.supports_extension("GL_EXT_YUV_target"),
        (ImageBufferKind::TextureExternalBT709, GlType::Gl) => false,
    }
}

pub const IMAGE_BUFFER_KINDS: [ImageBufferKind; 4] = [
    ImageBufferKind::Texture2D,
    ImageBufferKind::TextureRect,
    ImageBufferKind::TextureExternal,
    ImageBufferKind::TextureExternalBT709,
];

const ADVANCED_BLEND_FEATURE: &str = "ADVANCED_BLEND";
const ALPHA_FEATURE: &str = "ALPHA_PASS";
const DEBUG_OVERDRAW_FEATURE: &str = "DEBUG_OVERDRAW";
const DITHERING_FEATURE: &str = "DITHERING";
const DUAL_SOURCE_FEATURE: &str = "DUAL_SOURCE_BLENDING";
const FAST_PATH_FEATURE: &str = "FAST_PATH";

pub(crate) enum ShaderKind {
    Primitive,
    Cache(VertexArrayKind),
    ClipCache(VertexArrayKind),
    Brush,
    Text,
    #[allow(dead_code)]
    VectorStencil,
    #[allow(dead_code)]
    VectorCover,
    #[allow(dead_code)]
    Resolve,
    Composite,
    Clear,
    Copy,
}

pub struct LazilyCompiledShader {
    program: Option<Program>,
    name: &'static str,
    kind: ShaderKind,
    cached_projection: Transform3D<f32>,
    features: Vec<&'static str>,
}

impl LazilyCompiledShader {
    pub(crate) fn new(
        kind: ShaderKind,
        name: &'static str,
        unsorted_features: &[&'static str],
        shader_list: &ShaderFeatures,
    ) -> Result<Self, ShaderError> {

        let mut features = unsorted_features.to_vec();
        features.sort();

        // Ensure this shader config is in the available shader list so that we get
        // alerted if the list gets out-of-date when shaders or features are added.
        let config = features.join(",");
        assert!(
            shader_list.get(name).map_or(false, |f| f.contains(&config)),
            "shader \"{}\" with features \"{}\" not in available shader list",
            name,
            config,
        );

        let shader = LazilyCompiledShader {
            program: None,
            name,
            kind,
            //Note: this isn't really the default state, but there is no chance
            // an actual projection passed here would accidentally match.
            cached_projection: Transform3D::identity(),
            features,
        };

        Ok(shader)
    }

    pub fn precache(
        &mut self,
        device: &mut Device,
        flags: ShaderPrecacheFlags,
    ) -> Result<(), ShaderError> {
        let t0 = precise_time_ns();
        let timer_id = Telemetry::start_shaderload_time();
        self.get_internal(device, flags, None)?;
        Telemetry::stop_and_accumulate_shaderload_time(timer_id);
        let t1 = precise_time_ns();
        debug!("[C: {:.1} ms ] Precache {} {:?}",
            (t1 - t0) as f64 / 1000000.0,
            self.name,
            self.features
        );
        Ok(())
    }

    pub fn bind(
        &mut self,
        device: &mut Device,
        projection: &Transform3D<f32>,
        texture_size: Option<DeviceSize>,
        renderer_errors: &mut Vec<RendererError>,
        profile: &mut TransactionProfile,
    ) {
        let update_projection = self.cached_projection != *projection;
        let program = match self.get_internal(device, ShaderPrecacheFlags::FULL_COMPILE, Some(profile)) {
            Ok(program) => program,
            Err(e) => {
                renderer_errors.push(RendererError::from(e));
                return;
            }
        };
        device.bind_program(program);
        if let Some(texture_size) = texture_size {
            device.set_shader_texture_size(program, texture_size);
        }
        if update_projection {
            device.set_uniforms(program, projection);
            // thanks NLL for this (`program` technically borrows `self`)
            self.cached_projection = *projection;
        }
    }

    fn get_internal(
        &mut self,
        device: &mut Device,
        precache_flags: ShaderPrecacheFlags,
        mut profile: Option<&mut TransactionProfile>,
    ) -> Result<&mut Program, ShaderError> {
        if self.program.is_none() {
            let start_time = precise_time_ns();
            let program = match self.kind {
                ShaderKind::Primitive | ShaderKind::Brush | ShaderKind::Text | ShaderKind::Resolve | ShaderKind::Clear | ShaderKind::Copy => {
                    create_prim_shader(
                        self.name,
                        device,
                        &self.features,
                    )
                }
                ShaderKind::Cache(..) => {
                    create_prim_shader(
                        self.name,
                        device,
                        &self.features,
                    )
                }
                ShaderKind::VectorStencil => {
                    create_prim_shader(
                        self.name,
                        device,
                        &self.features,
                    )
                }
                ShaderKind::VectorCover => {
                    create_prim_shader(
                        self.name,
                        device,
                        &self.features,
                    )
                }
                ShaderKind::Composite => {
                    create_prim_shader(
                        self.name,
                        device,
                        &self.features,
                    )
                }
                ShaderKind::ClipCache(..) => {
                    create_clip_shader(
                        self.name,
                        device,
                        &self.features,
                    )
                }
            };
            self.program = Some(program?);

            if let Some(profile) = &mut profile {
                let end_time = precise_time_ns();
                profile.add(profiler::SHADER_BUILD_TIME, ns_to_ms(end_time - start_time));
            }
        }

        let program = self.program.as_mut().unwrap();

        if precache_flags.contains(ShaderPrecacheFlags::FULL_COMPILE) && !program.is_initialized() {
            let start_time = precise_time_ns();

            let vertex_format = match self.kind {
                ShaderKind::Primitive |
                ShaderKind::Brush |
                ShaderKind::Text => VertexArrayKind::Primitive,
                ShaderKind::Cache(format) => format,
                ShaderKind::VectorStencil => VertexArrayKind::VectorStencil,
                ShaderKind::VectorCover => VertexArrayKind::VectorCover,
                ShaderKind::ClipCache(format) => format,
                ShaderKind::Resolve => VertexArrayKind::Resolve,
                ShaderKind::Composite => VertexArrayKind::Composite,
                ShaderKind::Clear => VertexArrayKind::Clear,
                ShaderKind::Copy => VertexArrayKind::Copy,
            };

            let vertex_descriptor = match vertex_format {
                VertexArrayKind::Primitive => &desc::PRIM_INSTANCES,
                VertexArrayKind::LineDecoration => &desc::LINE,
                VertexArrayKind::FastLinearGradient => &desc::FAST_LINEAR_GRADIENT,
                VertexArrayKind::LinearGradient => &desc::LINEAR_GRADIENT,
                VertexArrayKind::RadialGradient => &desc::RADIAL_GRADIENT,
                VertexArrayKind::ConicGradient => &desc::CONIC_GRADIENT,
                VertexArrayKind::Blur => &desc::BLUR,
                VertexArrayKind::ClipRect => &desc::CLIP_RECT,
                VertexArrayKind::ClipBoxShadow => &desc::CLIP_BOX_SHADOW,
                VertexArrayKind::VectorStencil => &desc::VECTOR_STENCIL,
                VertexArrayKind::VectorCover => &desc::VECTOR_COVER,
                VertexArrayKind::Border => &desc::BORDER,
                VertexArrayKind::Scale => &desc::SCALE,
                VertexArrayKind::Resolve => &desc::RESOLVE,
                VertexArrayKind::SvgFilter => &desc::SVG_FILTER,
                VertexArrayKind::SvgFilterNode => &desc::SVG_FILTER_NODE,
                VertexArrayKind::Composite => &desc::COMPOSITE,
                VertexArrayKind::Clear => &desc::CLEAR,
                VertexArrayKind::Copy => &desc::COPY,
                VertexArrayKind::Mask => &desc::MASK,
            };

            device.link_program(program, vertex_descriptor)?;
            device.bind_program(program);
            match self.kind {
                ShaderKind::ClipCache(..) => {
                    device.bind_shader_samplers(
                        &program,
                        &[
                            ("sColor0", TextureSampler::Color0),
                            ("sTransformPalette", TextureSampler::TransformPalette),
                            ("sRenderTasks", TextureSampler::RenderTasks),
                            ("sGpuCache", TextureSampler::GpuCache),
                            ("sPrimitiveHeadersF", TextureSampler::PrimitiveHeadersF),
                            ("sPrimitiveHeadersI", TextureSampler::PrimitiveHeadersI),
                            ("sGpuBufferF", TextureSampler::GpuBufferF),
                            ("sGpuBufferI", TextureSampler::GpuBufferI),
                        ],
                    );
                }
                _ => {
                    device.bind_shader_samplers(
                        &program,
                        &[
                            ("sColor0", TextureSampler::Color0),
                            ("sColor1", TextureSampler::Color1),
                            ("sColor2", TextureSampler::Color2),
                            ("sDither", TextureSampler::Dither),
                            ("sTransformPalette", TextureSampler::TransformPalette),
                            ("sRenderTasks", TextureSampler::RenderTasks),
                            ("sGpuCache", TextureSampler::GpuCache),
                            ("sPrimitiveHeadersF", TextureSampler::PrimitiveHeadersF),
                            ("sPrimitiveHeadersI", TextureSampler::PrimitiveHeadersI),
                            ("sClipMask", TextureSampler::ClipMask),
                            ("sGpuBufferF", TextureSampler::GpuBufferF),
                            ("sGpuBufferI", TextureSampler::GpuBufferI),
                        ],
                    );
                }
            }

            if let Some(profile) = &mut profile {
                let end_time = precise_time_ns();
                profile.add(profiler::SHADER_BUILD_TIME, ns_to_ms(end_time - start_time));
            }
        }

        Ok(program)
    }

    fn deinit(self, device: &mut Device) {
        if let Some(program) = self.program {
            device.delete_program(program);
        }
    }
}

// A brush shader supports two modes:
// opaque:
//   Used for completely opaque primitives,
//   or inside segments of partially
//   opaque primitives. Assumes no need
//   for clip masks, AA etc.
// alpha:
//   Used for brush primitives in the alpha
//   pass. Assumes that AA should be applied
//   along the primitive edge, and also that
//   clip mask is present.
struct BrushShader {
    opaque: ShaderHandle,
    alpha: ShaderHandle,
    advanced_blend: Option<ShaderHandle>,
    dual_source: Option<ShaderHandle>,
    debug_overdraw: ShaderHandle,
}

impl BrushShader {
    fn new(
        name: &'static str,
        features: &[&'static str],
        shader_list: &ShaderFeatures,
        use_advanced_blend: bool,
        use_dual_source: bool,
        loader: &mut ShaderLoader,
    ) -> Result<Self, ShaderError> {
        let opaque_features = features.to_vec();
        let opaque = loader.create_shader(
            ShaderKind::Brush,
            name,
            &opaque_features,
            &shader_list,
        )?;

        let mut alpha_features = opaque_features.to_vec();
        alpha_features.push(ALPHA_FEATURE);

        let alpha = loader.create_shader(
            ShaderKind::Brush,
            name,
            &alpha_features,
            &shader_list,
        )?;

        let advanced_blend = if use_advanced_blend {
            let mut advanced_blend_features = alpha_features.to_vec();
            advanced_blend_features.push(ADVANCED_BLEND_FEATURE);

            let shader = loader.create_shader(
                ShaderKind::Brush,
                name,
                &advanced_blend_features,
                &shader_list,
            )?;

            Some(shader)
        } else {
            None
        };

        let dual_source = if use_dual_source {
            let mut dual_source_features = alpha_features.to_vec();
            dual_source_features.push(DUAL_SOURCE_FEATURE);

            let shader = loader.create_shader(
                ShaderKind::Brush,
                name,
                &dual_source_features,
                &shader_list,
            )?;

            Some(shader)
        } else {
            None
        };

        let mut debug_overdraw_features = features.to_vec();
        debug_overdraw_features.push(DEBUG_OVERDRAW_FEATURE);

        let debug_overdraw = loader.create_shader(
            ShaderKind::Brush,
            name,
            &debug_overdraw_features,
            &shader_list,
        )?;

        Ok(BrushShader {
            opaque,
            alpha,
            advanced_blend,
            dual_source,
            debug_overdraw,
        })
    }

    fn get_handle(
        &mut self,
        blend_mode: BlendMode,
        features: BatchFeatures,
        debug_flags: DebugFlags,
    ) -> ShaderHandle {
        match blend_mode {
            _ if debug_flags.contains(DebugFlags::SHOW_OVERDRAW) => self.debug_overdraw,
            BlendMode::None => self.opaque,
            BlendMode::Alpha |
            BlendMode::PremultipliedAlpha |
            BlendMode::PremultipliedDestOut |
            BlendMode::Screen |
            BlendMode::PlusLighter |
            BlendMode::Exclusion => {
                if features.contains(BatchFeatures::ALPHA_PASS) {
                    self.alpha
                } else {
                    self.opaque
                }
            }
            BlendMode::Advanced(_) => {
                self.advanced_blend.expect("bug: no advanced blend shader loaded")
            }
            BlendMode::SubpixelDualSource |
            BlendMode::MultiplyDualSource => {
                self.dual_source.expect("bug: no dual source shader loaded")
            }
        }
    }
}

pub struct TextShader {
    simple: ShaderHandle,
    glyph_transform: ShaderHandle,
    debug_overdraw: ShaderHandle,
}

impl TextShader {
    fn new(
        name: &'static str,
        features: &[&'static str],
        shader_list: &ShaderFeatures,
        loader: &mut ShaderLoader,
    ) -> Result<Self, ShaderError> {
        let mut simple_features = features.to_vec();
        simple_features.push("ALPHA_PASS");
        simple_features.push("TEXTURE_2D");

        let simple = loader.create_shader(
            ShaderKind::Text,
            name,
            &simple_features,
            &shader_list,
        )?;

        let mut glyph_transform_features = features.to_vec();
        glyph_transform_features.push("GLYPH_TRANSFORM");
        glyph_transform_features.push("ALPHA_PASS");
        glyph_transform_features.push("TEXTURE_2D");

        let glyph_transform = loader.create_shader(
            ShaderKind::Text,
            name,
            &glyph_transform_features,
            &shader_list,
        )?;

        let mut debug_overdraw_features = features.to_vec();
        debug_overdraw_features.push("DEBUG_OVERDRAW");
        debug_overdraw_features.push("TEXTURE_2D");

        let debug_overdraw = loader.create_shader(
            ShaderKind::Text,
            name,
            &debug_overdraw_features,
            &shader_list,
        )?;

        Ok(TextShader { simple, glyph_transform, debug_overdraw })
    }

    pub fn get_handle(
        &mut self,
        glyph_format: GlyphFormat,
        debug_flags: DebugFlags,
    ) -> ShaderHandle {
        match glyph_format {
            _ if debug_flags.contains(DebugFlags::SHOW_OVERDRAW) => self.debug_overdraw,
            GlyphFormat::Alpha |
            GlyphFormat::Subpixel |
            GlyphFormat::Bitmap |
            GlyphFormat::ColorBitmap => self.simple,
            GlyphFormat::TransformedAlpha |
            GlyphFormat::TransformedSubpixel => self.glyph_transform,
        }
    }
}

fn create_prim_shader(
    name: &'static str,
    device: &mut Device,
    features: &[&'static str],
) -> Result<Program, ShaderError> {
    debug!("PrimShader {}", name);

    device.create_program(name, features)
}

fn create_clip_shader(
    name: &'static str,
    device: &mut Device,
    features: &[&'static str],
) -> Result<Program, ShaderError> {
    debug!("ClipShader {}", name);

    device.create_program(name, features)
}

#[derive(Debug, Clone, Copy, PartialOrd, Ord, PartialEq, Eq, Hash)]
pub struct ShaderHandle(usize);

#[derive(Default)]
pub struct ShaderLoader {
    shaders: Vec<LazilyCompiledShader>,
}

impl ShaderLoader {
    pub fn new() -> Self {
        Default::default()
    }

    pub fn create_shader(
        &mut self,
        kind: ShaderKind,
        name: &'static str,
        unsorted_features: &[&'static str],
        shader_list: &ShaderFeatures,
    ) -> Result<ShaderHandle, ShaderError> {
        let index = self.shaders.len();
        let shader = LazilyCompiledShader::new(
            kind,
            name,
            unsorted_features,
            shader_list,
        )?;
        self.shaders.push(shader);
        Ok(ShaderHandle(index))
    }

    pub fn precache(
        &mut self,
        shader: ShaderHandle,
        device: &mut Device,
        flags: ShaderPrecacheFlags,
    ) -> Result<(), ShaderError> {
        if !flags.intersects(ShaderPrecacheFlags::ASYNC_COMPILE | ShaderPrecacheFlags::FULL_COMPILE) {
            return Ok(());
        }

        self.shaders[shader.0].precache(device, flags)
    }

    pub fn all_handles(&self) -> Vec<ShaderHandle> {
        self.shaders.iter().enumerate().map(|(index, _)| ShaderHandle(index)).collect()
    }

    pub fn get(&mut self, handle: ShaderHandle) -> &mut LazilyCompiledShader {
        &mut self.shaders[handle.0]
    }

    pub fn deinit(self, device: &mut Device) {
        for shader in self.shaders {
            shader.deinit(device);
        }
    }
}

pub struct Shaders {
    loader: ShaderLoader,

    // These are "cache shaders". These shaders are used to
    // draw intermediate results to cache targets. The results
    // of these shaders are then used by the primitive shaders.
    cs_blur_rgba8: ShaderHandle,
    cs_border_segment: ShaderHandle,
    cs_border_solid: ShaderHandle,
    cs_scale: Vec<Option<ShaderHandle>>,
    cs_line_decoration: ShaderHandle,
    cs_fast_linear_gradient: ShaderHandle,
    cs_linear_gradient: ShaderHandle,
    cs_radial_gradient: ShaderHandle,
    cs_conic_gradient: ShaderHandle,
    cs_svg_filter: ShaderHandle,
    cs_svg_filter_node: ShaderHandle,

    // Brush shaders
    brush_solid: BrushShader,
    brush_image: Vec<Option<BrushShader>>,
    brush_fast_image: Vec<Option<BrushShader>>,
    brush_blend: BrushShader,
    brush_mix_blend: BrushShader,
    brush_yuv_image: Vec<Option<BrushShader>>,
    brush_linear_gradient: BrushShader,
    brush_opacity: BrushShader,
    brush_opacity_aa: BrushShader,

    /// These are "cache clip shaders". These shaders are used to
    /// draw clip instances into the cached clip mask. The results
    /// of these shaders are also used by the primitive shaders.
    cs_clip_rectangle_slow: ShaderHandle,
    cs_clip_rectangle_fast: ShaderHandle,
    cs_clip_box_shadow: ShaderHandle,

    // The are "primitive shaders". These shaders draw and blend
    // final results on screen. They are aware of tile boundaries.
    // Most draw directly to the framebuffer, but some use inputs
    // from the cache shaders to draw. Specifically, the box
    // shadow primitive shader stretches the box shadow cache
    // output, and the cache_image shader blits the results of
    // a cache shader (e.g. blur) to the screen.
    ps_text_run: TextShader,
    ps_text_run_dual_source: Option<TextShader>,

    ps_split_composite: ShaderHandle,
    ps_quad_textured: ShaderHandle,
    ps_quad_radial_gradient: ShaderHandle,
    ps_quad_conic_gradient: ShaderHandle,
    ps_mask: ShaderHandle,
    ps_mask_fast: ShaderHandle,
    ps_clear: ShaderHandle,
    ps_copy: ShaderHandle,

    composite: CompositorShaders,
}

pub struct PendingShadersToPrecache {
    precache_flags: ShaderPrecacheFlags,
    remaining_shaders: VecDeque<ShaderHandle>,
}

impl Shaders {
    pub fn new(
        device: &mut Device,
        gl_type: GlType,
        options: &WebRenderOptions,
    ) -> Result<Self, ShaderError> {
        let use_dual_source_blending =
            device.get_capabilities().supports_dual_source_blending &&
            options.allow_dual_source_blending;
        let use_advanced_blend_equation =
            device.get_capabilities().supports_advanced_blend_equation &&
            options.allow_advanced_blend_equation;

        let texture_external_version = if device.get_capabilities().supports_image_external_essl3 {
            TextureExternalVersion::ESSL3
        } else {
            TextureExternalVersion::ESSL1
        };
        let mut shader_flags = get_shader_feature_flags(gl_type, texture_external_version, device);
        shader_flags.set(ShaderFeatureFlags::ADVANCED_BLEND_EQUATION, use_advanced_blend_equation);
        shader_flags.set(ShaderFeatureFlags::DUAL_SOURCE_BLENDING, use_dual_source_blending);
        shader_flags.set(ShaderFeatureFlags::DITHERING, options.enable_dithering);
        let shader_list = get_shader_features(shader_flags);

        let mut loader = ShaderLoader::new();

        let brush_solid = BrushShader::new(
            "brush_solid",
            &[],
            &shader_list,
            false /* advanced blend */,
            false /* dual source */,
            &mut loader,
        )?;

        let brush_blend = BrushShader::new(
            "brush_blend",
            &[],
            &shader_list,
            false /* advanced blend */,
            false /* dual source */,
            &mut loader,
        )?;

        let brush_mix_blend = BrushShader::new(
            "brush_mix_blend",
            &[],
            &shader_list,
            false /* advanced blend */,
            false /* dual source */,
            &mut loader,
        )?;

        let brush_linear_gradient = BrushShader::new(
            "brush_linear_gradient",
            if options.enable_dithering {
               &[DITHERING_FEATURE]
            } else {
               &[]
            },
            &shader_list,
            false /* advanced blend */,
            false /* dual source */,
            &mut loader,
        )?;

        let brush_opacity_aa = BrushShader::new(
            "brush_opacity",
            &["ANTIALIASING"],
            &shader_list,
            false /* advanced blend */,
            false /* dual source */,
            &mut loader,
        )?;

        let brush_opacity = BrushShader::new(
            "brush_opacity",
            &[],
            &shader_list,
            false /* advanced blend */,
            false /* dual source */,
            &mut loader,
        )?;

        let cs_blur_rgba8 = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::Blur),
            "cs_blur",
            &["COLOR_TARGET"],
            &shader_list,
        )?;

        let cs_svg_filter = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::SvgFilter),
            "cs_svg_filter",
            &[],
            &shader_list,
        )?;

        let cs_svg_filter_node = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::SvgFilterNode),
            "cs_svg_filter_node",
            &[],
            &shader_list,
        )?;

        let ps_mask = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::Mask),
            "ps_quad_mask",
            &[],
            &shader_list,
        )?;

        let ps_mask_fast = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::Mask),
            "ps_quad_mask",
            &[FAST_PATH_FEATURE],
            &shader_list,
        )?;

        let cs_clip_rectangle_slow = loader.create_shader(
            ShaderKind::ClipCache(VertexArrayKind::ClipRect),
            "cs_clip_rectangle",
            &[],
            &shader_list,
        )?;

        let cs_clip_rectangle_fast = loader.create_shader(
            ShaderKind::ClipCache(VertexArrayKind::ClipRect),
            "cs_clip_rectangle",
            &[FAST_PATH_FEATURE],
            &shader_list,
        )?;

        let cs_clip_box_shadow = loader.create_shader(
            ShaderKind::ClipCache(VertexArrayKind::ClipBoxShadow),
            "cs_clip_box_shadow",
            &["TEXTURE_2D"],
            &shader_list,
        )?;

        let mut cs_scale = Vec::new();
        let scale_shader_num = IMAGE_BUFFER_KINDS.len();
        // PrimitiveShader is not clonable. Use push() to initialize the vec.
        for _ in 0 .. scale_shader_num {
            cs_scale.push(None);
        }
        for image_buffer_kind in &IMAGE_BUFFER_KINDS {
            if has_platform_support(*image_buffer_kind, device) {
                let feature_string = get_feature_string(
                    *image_buffer_kind,
                    texture_external_version,
                );

                let mut features = Vec::new();
                if feature_string != "" {
                    features.push(feature_string);
                }

                let shader = loader.create_shader(
                    ShaderKind::Cache(VertexArrayKind::Scale),
                    "cs_scale",
                    &features,
                    &shader_list,
                 )?;

                 let index = Self::get_compositing_shader_index(
                    *image_buffer_kind,
                 );
                 cs_scale[index] = Some(shader);
            }
        }

        // TODO(gw): The split composite + text shader are special cases - the only
        //           shaders used during normal scene rendering that aren't a brush
        //           shader. Perhaps we can unify these in future?

        let ps_text_run = TextShader::new("ps_text_run",
            &[],
            &shader_list,
            &mut loader,
        )?;

        let ps_text_run_dual_source = if use_dual_source_blending {
            let dual_source_features = vec![DUAL_SOURCE_FEATURE];
            Some(TextShader::new("ps_text_run",
                &dual_source_features,
                &shader_list,
                &mut loader,
            )?)
        } else {
            None
        };

        let ps_quad_textured = loader.create_shader(
            ShaderKind::Primitive,
            "ps_quad_textured",
            &[],
            &shader_list,
        )?;

        let ps_quad_radial_gradient = loader.create_shader(
            ShaderKind::Primitive,
            "ps_quad_radial_gradient",
            &[],
            &shader_list,
        )?;

        let ps_quad_conic_gradient = loader.create_shader(
            ShaderKind::Primitive,
            "ps_quad_conic_gradient",
            &[],
            &shader_list,
        )?;

        let ps_split_composite = loader.create_shader(
            ShaderKind::Primitive,
            "ps_split_composite",
            &[],
            &shader_list,
        )?;

        let ps_clear = loader.create_shader(
            ShaderKind::Clear,
            "ps_clear",
            &[],
            &shader_list,
        )?;

        let ps_copy = loader.create_shader(
            ShaderKind::Copy,
            "ps_copy",
            &[],
            &shader_list,
        )?;

        // All image configuration.
        let mut image_features = Vec::new();
        let mut brush_image = Vec::new();
        let mut brush_fast_image = Vec::new();
        // PrimitiveShader is not clonable. Use push() to initialize the vec.
        for _ in 0 .. IMAGE_BUFFER_KINDS.len() {
            brush_image.push(None);
            brush_fast_image.push(None);
        }
        for buffer_kind in 0 .. IMAGE_BUFFER_KINDS.len() {
            if !has_platform_support(IMAGE_BUFFER_KINDS[buffer_kind], device)
                // Brush shaders are not ESSL1 compatible
                || (IMAGE_BUFFER_KINDS[buffer_kind] == ImageBufferKind::TextureExternal
                    && texture_external_version == TextureExternalVersion::ESSL1)
            {
                continue;
            }

            let feature_string = get_feature_string(
                IMAGE_BUFFER_KINDS[buffer_kind],
                texture_external_version,
            );
            if feature_string != "" {
                image_features.push(feature_string);
            }

            brush_fast_image[buffer_kind] = Some(BrushShader::new(
                "brush_image",
                &image_features,
                &shader_list,
                use_advanced_blend_equation,
                use_dual_source_blending,
                &mut loader,
            )?);

            image_features.push("REPETITION");
            image_features.push("ANTIALIASING");

            brush_image[buffer_kind] = Some(BrushShader::new(
                "brush_image",
                &image_features,
                &shader_list,
                use_advanced_blend_equation,
                use_dual_source_blending,
                &mut loader,
            )?);

            image_features.clear();
        }

        // All yuv_image configuration.
        let mut yuv_features = Vec::new();
        let mut rgba_features = Vec::new();
        let mut fast_path_features = Vec::new();
        let yuv_shader_num = IMAGE_BUFFER_KINDS.len();
        let mut brush_yuv_image = Vec::new();
        // PrimitiveShader is not clonable. Use push() to initialize the vec.
        for _ in 0 .. yuv_shader_num {
            brush_yuv_image.push(None);
        }
        for image_buffer_kind in &IMAGE_BUFFER_KINDS {
            if has_platform_support(*image_buffer_kind, device) {
                yuv_features.push("YUV");
                fast_path_features.push("FAST_PATH");

                let index = Self::get_compositing_shader_index(
                    *image_buffer_kind,
                );

                let feature_string = get_feature_string(
                    *image_buffer_kind,
                    texture_external_version,
                );
                if feature_string != "" {
                    yuv_features.push(feature_string);
                    rgba_features.push(feature_string);
                    fast_path_features.push(feature_string);
                }

                // YUV shaders are not compatible with ESSL1
                if *image_buffer_kind != ImageBufferKind::TextureExternal ||
                    texture_external_version == TextureExternalVersion::ESSL3 {
                    let brush_shader = BrushShader::new(
                        "brush_yuv_image",
                        &yuv_features,
                        &shader_list,
                        false /* advanced blend */,
                        false /* dual source */,
                        &mut loader,
                    )?;
                    brush_yuv_image[index] = Some(brush_shader);
                }

                yuv_features.clear();
                rgba_features.clear();
                fast_path_features.clear();
            }
        }

        let cs_line_decoration = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::LineDecoration),
            "cs_line_decoration",
            &[],
            &shader_list,
        )?;

        let cs_fast_linear_gradient = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::FastLinearGradient),
            "cs_fast_linear_gradient",
            &[],
            &shader_list,
        )?;

        let cs_linear_gradient = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::LinearGradient),
            "cs_linear_gradient",
            &[],
            &shader_list,
        )?;

        let cs_radial_gradient = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::RadialGradient),
            "cs_radial_gradient",
            &[],
            &shader_list,
        )?;

        let cs_conic_gradient = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::ConicGradient),
            "cs_conic_gradient",
            &[],
            &shader_list,
        )?;

        let cs_border_segment = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::Border),
            "cs_border_segment",
             &[],
            &shader_list,
        )?;

        let cs_border_solid = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::Border),
            "cs_border_solid",
            &[],
            &shader_list,
        )?;

        let composite = CompositorShaders::new(device, gl_type, &mut loader)?;

        Ok(Shaders {
            loader,

            cs_blur_rgba8,
            cs_border_segment,
            cs_line_decoration,
            cs_fast_linear_gradient,
            cs_linear_gradient,
            cs_radial_gradient,
            cs_conic_gradient,
            cs_border_solid,
            cs_scale,
            cs_svg_filter,
            cs_svg_filter_node,
            brush_solid,
            brush_image,
            brush_fast_image,
            brush_blend,
            brush_mix_blend,
            brush_yuv_image,
            brush_linear_gradient,
            brush_opacity,
            brush_opacity_aa,
            cs_clip_rectangle_slow,
            cs_clip_rectangle_fast,
            cs_clip_box_shadow,
            ps_text_run,
            ps_text_run_dual_source,
            ps_quad_textured,
            ps_quad_radial_gradient,
            ps_quad_conic_gradient,
            ps_mask,
            ps_mask_fast,
            ps_split_composite,
            ps_clear,
            ps_copy,
            composite,
        })
    }

    #[must_use]
    pub fn precache_all(
        &mut self,
        precache_flags: ShaderPrecacheFlags,
    ) -> PendingShadersToPrecache {
        PendingShadersToPrecache {
            precache_flags,
            remaining_shaders: self.loader.all_handles().into(),
        }
    }

    /// Returns true if another call is needed, false if precaching is finished.
    pub fn resume_precache(
        &mut self,
        device: &mut Device,
        pending_shaders: &mut PendingShadersToPrecache,
    ) -> Result<bool, ShaderError> {
        let Some(next_shader) = pending_shaders.remaining_shaders.pop_front() else {
            return Ok(false)
        };

        self.loader.precache(next_shader, device, pending_shaders.precache_flags)?;
        Ok(true)
    }

    fn get_compositing_shader_index(buffer_kind: ImageBufferKind) -> usize {
        buffer_kind as usize
    }

    pub fn get_composite_shader(
        &mut self,
        format: CompositeSurfaceFormat,
        buffer_kind: ImageBufferKind,
        features: CompositeFeatures,
    ) -> &mut LazilyCompiledShader {
        let shader_handle = self.composite.get_handle(format, buffer_kind, features);
        self.loader.get(shader_handle)
    }

    pub fn get_scale_shader(
        &mut self,
        buffer_kind: ImageBufferKind,
    ) -> &mut LazilyCompiledShader {
        let shader_index = Self::get_compositing_shader_index(buffer_kind);
        let shader_handle = self.cs_scale[shader_index]
            .expect("bug: unsupported scale shader requested");
        self.loader.get(shader_handle)
    }

    pub fn get_quad_shader(
        &mut self,
        pattern: PatternKind
    ) -> &mut LazilyCompiledShader {
        let shader_handle = match pattern {
            PatternKind::ColorOrTexture => self.ps_quad_textured,
            PatternKind::RadialGradient => self.ps_quad_radial_gradient,
            PatternKind::ConicGradient => self.ps_quad_conic_gradient,
            PatternKind::Mask => unreachable!(),
        };
        self.loader.get(shader_handle)
    }

    pub fn get(
        &mut self,
        key: &BatchKey,
        features: BatchFeatures,
        debug_flags: DebugFlags,
        device: &Device,
    ) -> &mut LazilyCompiledShader {
        let shader_handle = self.get_handle(key, features, debug_flags, device);
        self.loader.get(shader_handle)
    }

    pub fn get_handle(
        &mut self,
        key: &BatchKey,
        mut features: BatchFeatures,
        debug_flags: DebugFlags,
        device: &Device,
    ) -> ShaderHandle {
        match key.kind {
            BatchKind::Quad(PatternKind::ColorOrTexture) => {
                self.ps_quad_textured
            }
            BatchKind::Quad(PatternKind::RadialGradient) => {
                self.ps_quad_radial_gradient
            }
            BatchKind::Quad(PatternKind::ConicGradient) => {
                self.ps_quad_conic_gradient
            }
            BatchKind::Quad(PatternKind::Mask) => {
                unreachable!();
            }
            BatchKind::SplitComposite => {
                self.ps_split_composite
            }
            BatchKind::Brush(brush_kind) => {
                // SWGL uses a native anti-aliasing implementation that bypasses the shader.
                // Don't consider it in that case when deciding whether or not to use
                // an alpha-pass shader.
                if device.get_capabilities().uses_native_antialiasing {
                    features.remove(BatchFeatures::ANTIALIASING);
                }
                let brush_shader = match brush_kind {
                    BrushBatchKind::Solid => {
                        &mut self.brush_solid
                    }
                    BrushBatchKind::Image(image_buffer_kind) => {
                        if features.contains(BatchFeatures::ANTIALIASING) ||
                            features.contains(BatchFeatures::REPETITION) {

                            self.brush_image[image_buffer_kind as usize]
                                .as_mut()
                                .expect("Unsupported image shader kind")
                        } else {
                            self.brush_fast_image[image_buffer_kind as usize]
                            .as_mut()
                                .expect("Unsupported image shader kind")
                        }
                    }
                    BrushBatchKind::Blend => {
                        &mut self.brush_blend
                    }
                    BrushBatchKind::MixBlend { .. } => {
                        &mut self.brush_mix_blend
                    }
                    BrushBatchKind::LinearGradient => {
                        // SWGL uses a native clip mask implementation that bypasses the shader.
                        // Don't consider it in that case when deciding whether or not to use
                        // an alpha-pass shader.
                        if device.get_capabilities().uses_native_clip_mask {
                            features.remove(BatchFeatures::CLIP_MASK);
                        }
                        // Gradient brushes can optimistically use the opaque shader even
                        // with a blend mode if they don't require any features.
                        if !features.intersects(
                            BatchFeatures::ANTIALIASING
                                | BatchFeatures::REPETITION
                                | BatchFeatures::CLIP_MASK,
                        ) {
                            features.remove(BatchFeatures::ALPHA_PASS);
                        }
                        match brush_kind {
                            BrushBatchKind::LinearGradient => &mut self.brush_linear_gradient,
                            _ => panic!(),
                        }
                    }
                    BrushBatchKind::YuvImage(image_buffer_kind, ..) => {
                        let shader_index =
                            Self::get_compositing_shader_index(image_buffer_kind);
                        self.brush_yuv_image[shader_index]
                            .as_mut()
                            .expect("Unsupported YUV shader kind")
                    }
                    BrushBatchKind::Opacity => {
                        if features.contains(BatchFeatures::ANTIALIASING) {
                            &mut self.brush_opacity_aa
                        } else {
                            &mut self.brush_opacity
                        }
                    }
                };
                brush_shader.get_handle(key.blend_mode, features, debug_flags)
            }
            BatchKind::TextRun(glyph_format) => {
                let text_shader = match key.blend_mode {
                    BlendMode::SubpixelDualSource => self.ps_text_run_dual_source.as_mut().unwrap(),
                    _ => &mut self.ps_text_run,
                };
                text_shader.get_handle(glyph_format, debug_flags)
            }
        }
    }

    pub fn cs_blur_rgba8(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_blur_rgba8) }
    pub fn cs_border_segment(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_border_segment) }
    pub fn cs_border_solid(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_border_solid) }
    pub fn cs_line_decoration(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_line_decoration) }
    pub fn cs_fast_linear_gradient(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_fast_linear_gradient) }
    pub fn cs_linear_gradient(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_linear_gradient) }
    pub fn cs_radial_gradient(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_radial_gradient) }
    pub fn cs_conic_gradient(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_conic_gradient) }
    pub fn cs_svg_filter(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_svg_filter) }
    pub fn cs_svg_filter_node(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_svg_filter_node) }
    pub fn cs_clip_rectangle_slow(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_clip_rectangle_slow) }
    pub fn cs_clip_rectangle_fast(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_clip_rectangle_fast) }
    pub fn cs_clip_box_shadow(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_clip_box_shadow) }
    pub fn ps_quad_textured(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.ps_quad_textured) }
    pub fn ps_mask(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.ps_mask) }
    pub fn ps_mask_fast(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.ps_mask_fast) }
    pub fn ps_clear(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.ps_clear) }
    pub fn ps_copy(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.ps_copy) }

    pub fn deinit(self, device: &mut Device) {
        self.loader.deinit(device);
    }
}

pub type SharedShaders = Rc<RefCell<Shaders>>;

pub struct CompositorShaders {
    // Composite shaders. These are very simple shaders used to composite
    // picture cache tiles into the framebuffer on platforms that do not have an
    // OS Compositor (or we cannot use it).  Such an OS Compositor (such as
    // DirectComposite or CoreAnimation) handles the composition of the picture
    // cache tiles at a lower level (e.g. in DWM for Windows); in that case we
    // directly hand the picture cache surfaces over to the OS Compositor, and
    // our own Composite shaders below never run.
    // To composite external (RGB) surfaces we need various permutations of
    // shaders with WR_FEATURE flags on or off based on the type of image
    // buffer we're sourcing from (see IMAGE_BUFFER_KINDS).
    rgba: Vec<Option<ShaderHandle>>,
    // A faster set of rgba composite shaders that do not support UV clamping
    // or color modulation.
    rgba_fast_path: Vec<Option<ShaderHandle>>,
    // The same set of composite shaders but with WR_FEATURE_YUV added.
    yuv_clip: Vec<Option<ShaderHandle>>,
    yuv_fast: Vec<Option<ShaderHandle>>,
}

impl CompositorShaders {
    pub fn new(
        device: &mut Device,
        gl_type: GlType,
        loader: &mut ShaderLoader,
    )  -> Result<Self, ShaderError>  {
        let mut yuv_clip_features = Vec::new();
        let mut yuv_fast_features = Vec::new();
        let mut rgba_features = Vec::new();
        let mut fast_path_features = Vec::new();
        let mut rgba = Vec::new();
        let mut rgba_fast_path = Vec::new();
        let mut yuv_clip = Vec::new();
        let mut yuv_fast = Vec::new();

        let texture_external_version = if device.get_capabilities().supports_image_external_essl3 {
            TextureExternalVersion::ESSL3
        } else {
            TextureExternalVersion::ESSL1
        };

        let feature_flags = get_shader_feature_flags(gl_type, texture_external_version, device);
        let shader_list = get_shader_features(feature_flags);

        for _ in 0..IMAGE_BUFFER_KINDS.len() {
            yuv_clip.push(None);
            yuv_fast.push(None);
            rgba.push(None);
            rgba_fast_path.push(None);
        }

        for image_buffer_kind in &IMAGE_BUFFER_KINDS {
            if !has_platform_support(*image_buffer_kind, device) {
                continue;
            }

            yuv_clip_features.push("YUV");
            yuv_fast_features.push("YUV");
            yuv_fast_features.push("FAST_PATH");
            fast_path_features.push("FAST_PATH");
    
            let index = Self::get_shader_index(*image_buffer_kind);

            let feature_string = get_feature_string(
                *image_buffer_kind,
                texture_external_version,
            );
            if feature_string != "" {
                yuv_clip_features.push(feature_string);
                yuv_fast_features.push(feature_string);
                rgba_features.push(feature_string);
                fast_path_features.push(feature_string);
            }

            // YUV shaders are not compatible with ESSL1
            if *image_buffer_kind != ImageBufferKind::TextureExternal ||
                texture_external_version == TextureExternalVersion::ESSL3 {

                yuv_clip[index] = Some(loader.create_shader(
                    ShaderKind::Composite,
                    "composite",
                    &yuv_clip_features,
                    &shader_list,
                )?);

                yuv_fast[index] = Some(loader.create_shader(
                    ShaderKind::Composite,
                    "composite",
                    &yuv_fast_features,
                    &shader_list,
                )?);
            }

            rgba[index] = Some(loader.create_shader(
                ShaderKind::Composite,
                "composite",
                &rgba_features,
                &shader_list,
            )?);

            rgba_fast_path[index] = Some(loader.create_shader(
                ShaderKind::Composite,
                "composite",
                &fast_path_features,
                &shader_list,
            )?);

            yuv_fast_features.clear();
            yuv_clip_features.clear();
            rgba_features.clear();
            fast_path_features.clear();
        }

        Ok(CompositorShaders {
            rgba,
            rgba_fast_path,
            yuv_clip,
            yuv_fast,
        })
    }

    pub fn get_handle(
        &mut self,
        format: CompositeSurfaceFormat,
        buffer_kind: ImageBufferKind,
        features: CompositeFeatures,
    ) -> ShaderHandle {
        match format {
            CompositeSurfaceFormat::Rgba => {
                if features.contains(CompositeFeatures::NO_UV_CLAMP)
                    && features.contains(CompositeFeatures::NO_COLOR_MODULATION)
                    && features.contains(CompositeFeatures::NO_CLIP_MASK)
                {
                    let shader_index = Self::get_shader_index(buffer_kind);
                    self.rgba_fast_path[shader_index]
                        .expect("bug: unsupported rgba fast path shader requested")
                } else {
                    let shader_index = Self::get_shader_index(buffer_kind);
                    self.rgba[shader_index]
                        .expect("bug: unsupported rgba shader requested")
                }
            }
            CompositeSurfaceFormat::Yuv => {
                let shader_index = Self::get_shader_index(buffer_kind);
                if features.contains(CompositeFeatures::NO_CLIP_MASK) {
                    self.yuv_fast[shader_index]
                        .expect("bug: unsupported yuv shader requested")
                } else {
                    self.yuv_clip[shader_index]
                        .expect("bug: unsupported yuv shader requested")
                }
            }
        }
    }

    fn get_shader_index(buffer_kind: ImageBufferKind) -> usize {
        buffer_kind as usize
    }
}

fn get_shader_feature_flags(
    gl_type: GlType,
    texture_external_version: TextureExternalVersion,
    device: &Device
) -> ShaderFeatureFlags {
    match gl_type {
        GlType::Gl => ShaderFeatureFlags::GL,
        GlType::Gles => {
            let mut flags = ShaderFeatureFlags::GLES;
            flags |= match texture_external_version {
                TextureExternalVersion::ESSL3 => ShaderFeatureFlags::TEXTURE_EXTERNAL,
                TextureExternalVersion::ESSL1 => ShaderFeatureFlags::TEXTURE_EXTERNAL_ESSL1,
            };
            if device.supports_extension("GL_EXT_YUV_target") {
                flags |= ShaderFeatureFlags::TEXTURE_EXTERNAL_BT709;
            }
            flags
        }
    }
}
