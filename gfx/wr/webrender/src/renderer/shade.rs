/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{ImageBufferKind, units::DeviceSize};
use crate::batch::{BatchKey, BatchKind, BatchFeatures};
use crate::composite::{CompositeFeatures, CompositeSurfaceFormat};
use crate::device::{Device, Program, ShaderError, VertexDescriptor};
use crate::pattern::PatternKind;
use crate::telemetry::Telemetry;
use euclid::default::Transform3D;
use glyph_rasterizer::GlyphFormat;
use crate::renderer::{
    desc,
    BlendMode, DebugFlags, RendererError, WebRenderOptions,
    TextureSampler, VertexArrayKind, ShaderPrecacheFlags,
};
use crate::profiler::{self, RenderCommandLog, TransactionProfile, ns_to_ms};

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
    let caps = device.get_capabilities();
    match kind {
        ImageBufferKind::Texture2D => true,
        ImageBufferKind::TextureRect => caps.supports_texture_rect,
        ImageBufferKind::TextureExternal => caps.supports_texture_external,
        ImageBufferKind::TextureExternalBT709 => caps.supports_texture_external_bt709,
    }
}

pub const IMAGE_BUFFER_KINDS: [ImageBufferKind; 4] = [
    ImageBufferKind::Texture2D,
    ImageBufferKind::TextureRect,
    ImageBufferKind::TextureExternal,
    ImageBufferKind::TextureExternalBT709,
];

/// The texture slot each sampler reads from, fixed on every program when it
/// is linked. Names absent from a given shader are skipped.
const SAMPLER_BINDINGS: &[(&'static str, TextureSampler)] = &[
    ("sColor0", TextureSampler::Color0),
    ("sColor1", TextureSampler::Color1),
    ("sColor2", TextureSampler::Color2),
    ("sDither", TextureSampler::Dither),
    ("sTransformPalette", TextureSampler::TransformPalette),
    ("sRenderTasks", TextureSampler::RenderTasks),
    ("sPrimitiveHeadersF", TextureSampler::PrimitiveHeadersF),
    ("sPrimitiveHeadersI", TextureSampler::PrimitiveHeadersI),
    ("sClipMask", TextureSampler::ClipMask),
    ("sGpuBufferF", TextureSampler::GpuBufferF),
    ("sGpuBufferI", TextureSampler::GpuBufferI),
    ("sPathEdges", TextureSampler::PathEdges),
];

const DITHERING_FEATURE: &str = "DITHERING";
const DUAL_SOURCE_FEATURE: &str = "DUAL_SOURCE_BLENDING";
const FAST_PATH_FEATURE: &str = "FAST_PATH";
const SUPERELLIPSE_FEATURE: &str = "SUPERELLIPSE";

pub(crate) enum ShaderKind {
    Primitive,
    Cache(VertexArrayKind),
    Text,
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

    #[cfg(feature = "debugger")]
    pub fn name(&self) -> &'static str {
        self.name
    }

    #[cfg(feature = "debugger")]
    pub fn features(&self) -> &[&'static str] {
        &self.features
    }

    /// The name the driver sees, base filename plus features, as used in
    /// compile and link logs.
    #[cfg(feature = "debugger")]
    pub fn full_name(&self) -> String {
        if self.features.is_empty() {
            self.name.to_string()
        } else {
            format!("{}_{}", self.name, self.features.join("_"))
        }
    }

    /// Whether this shader has a program that has been linked, and so has
    /// source compiled into it that an edit can invalidate.
    ///
    /// This is deliberately not `program.is_some()`. An async precache calls
    /// `create_program` on every variant without linking any of them, which
    /// Firefox does at startup, so a program object existing says nothing
    /// about whether anything has been compiled.
    #[cfg(feature = "debugger")]
    pub fn is_compiled(&self) -> bool {
        self.program.as_ref().map_or(false, Program::is_initialized)
    }

    /// Drop a program that was created but never linked, so that the next use
    /// builds a fresh one.
    ///
    /// `ProgramSourceInfo` is computed by `create_program` and decides, among
    /// other things, whether to compile the `.glsl` source or the variant
    /// optimized at build time. An unlinked program created before a source
    /// override was installed still carries that decision, so linking it as it
    /// stands would compile the build-time source and silently ignore the
    /// edit.
    #[cfg(feature = "debugger")]
    fn discard_unlinked_program(&mut self, device: &mut Device) {
        if self.program.as_ref().map_or(true, Program::is_initialized) {
            return;
        }

        if let Some(program) = self.program.take() {
            device.delete_program(program);
        }
        self.cached_projection = Transform3D::identity();
    }

    fn vertex_descriptor(&self) -> &'static VertexDescriptor {
        let vertex_format = match self.kind {
            ShaderKind::Primitive |
            ShaderKind::Text => VertexArrayKind::Primitive,
            ShaderKind::Cache(format) => format,
            ShaderKind::Composite => VertexArrayKind::Composite,
            ShaderKind::Clear => VertexArrayKind::Clear,
            ShaderKind::Copy => VertexArrayKind::Copy,
        };

        match vertex_format {
            VertexArrayKind::Primitive => &desc::PRIM_INSTANCES,
            VertexArrayKind::LineDecoration => &desc::LINE,
            VertexArrayKind::Blur => &desc::BLUR,
            VertexArrayKind::Border => &desc::BORDER,
            VertexArrayKind::Scale => &desc::SCALE,
            VertexArrayKind::SvgFilterNode => &desc::SVG_FILTER_NODE,
            VertexArrayKind::Composite => &desc::COMPOSITE,
            VertexArrayKind::Clear => &desc::CLEAR,
            VertexArrayKind::Copy => &desc::COPY,
            VertexArrayKind::Mask => &desc::MASK,
            VertexArrayKind::PathTile => &desc::PATH_TILE,
        }
    }

    /// Compile and link a program for this shader from the sources currently
    /// in effect, leaving the one already in use alone.
    #[cfg(feature = "debugger")]
    fn build_program(&self, device: &mut Device) -> Result<Program, ShaderError> {
        let mut program = device.create_program(self.name, &self.features)?;

        if let Err(err) = device.link_program(&mut program, self.vertex_descriptor(), SAMPLER_BINDINGS) {
            device.delete_program(program);
            return Err(err);
        }

        Ok(program)
    }

    pub fn precache(
        &mut self,
        device: &mut Device,
        flags: ShaderPrecacheFlags,
    ) -> Result<(), ShaderError> {
        let t0 = zeitstempel::now();
        let timer_id = Telemetry::start_shaderload_time();
        self.get_internal(device, flags, None)?;
        Telemetry::stop_and_accumulate_shaderload_time(timer_id);
        let t1 = zeitstempel::now();
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
        history: &mut Option<RenderCommandLog>,
    ) {
        if let Some(history) = history {
            history.set_shader(self.name);
        }
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
            let start_time = zeitstempel::now();
            let program = match self.kind {
                ShaderKind::Primitive | ShaderKind::Text | ShaderKind::Clear | ShaderKind::Copy => {
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
                ShaderKind::Composite => {
                    create_prim_shader(
                        self.name,
                        device,
                        &self.features,
                    )
                }
            };
            self.program = Some(program?);

            if let Some(profile) = &mut profile {
                let end_time = zeitstempel::now();
                profile.add(profiler::SHADER_BUILD_TIME, ns_to_ms(end_time - start_time));
            }
        }

        let needs_link = precache_flags.contains(ShaderPrecacheFlags::FULL_COMPILE)
            && !self.program.as_ref().unwrap().is_initialized();

        if needs_link {
            let start_time = zeitstempel::now();

            let vertex_descriptor = self.vertex_descriptor();

            let program = self.program.as_mut().unwrap();
            if let Err(err) = device.link_program(program, vertex_descriptor, SAMPLER_BINDINGS) {
                // A failed link deletes the program object, so drop it rather
                // than retrying against a dead GL name on the next bind. The
                // next attempt builds a fresh one, which is what makes a
                // shader that has been fixed since recover on its own.
                if let Some(program) = self.program.take() {
                    device.delete_program(program);
                }
                return Err(err);
            }

            if let Some(profile) = &mut profile {
                let end_time = zeitstempel::now();
                profile.add(profiler::SHADER_BUILD_TIME, ns_to_ms(end_time - start_time));
            }
        }

        Ok(self.program.as_mut().unwrap())
    }

    fn deinit(self, device: &mut Device) {
        if let Some(program) = self.program {
            device.delete_program(program);
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

    /// Rebuild every compiled shader whose source pulls in `changed_file`,
    /// from the sources currently in effect on the device.
    ///
    /// Either all of them are replaced or none is: if any variant fails to
    /// compile or link, the programs in use are left untouched and the
    /// instance keeps rendering with the shaders it already had. Shaders that
    /// have not been linked yet are skipped; they pick the new source up when
    /// they are first bound.
    #[cfg(feature = "debugger")]
    pub fn reload(
        &mut self,
        device: &mut Device,
        changed_file: &str,
    ) -> Result<usize, Vec<ShaderError>> {
        // A broken shared include fails identically for every variant that
        // pulls it in, so stop once enough have failed to describe the
        // problem rather than reporting the same error a hundred times.
        const MAX_REPORTED_FAILURES: usize = 8;

        let affected: Vec<usize> = {
            let device = &*device;
            self.shaders
                .iter()
                .enumerate()
                .filter(|(_, shader)| {
                    shader.name() == changed_file
                        || device.shader_include_closure(shader.name()).contains(changed_file)
                })
                .map(|(index, _)| index)
                .collect()
        };

        // Unlinked programs carry a source decision that predates the edit, so
        // drop them rather than rebuild them: they have compiled nothing yet,
        // and a fresh one is built when they are first used.
        for &index in &affected {
            self.shaders[index].discard_unlinked_program(device);
        }

        let to_rebuild: Vec<usize> = affected
            .iter()
            .cloned()
            .filter(|&index| self.shaders[index].is_compiled())
            .collect();

        let mut rebuilt = Vec::with_capacity(to_rebuild.len());
        let mut errors = Vec::new();

        for &index in &to_rebuild {
            match self.shaders[index].build_program(device) {
                Ok(program) => rebuilt.push((index, program)),
                Err(err) => {
                    errors.push(err);
                    if errors.len() >= MAX_REPORTED_FAILURES {
                        break;
                    }
                }
            }
        }

        if !errors.is_empty() {
            for (_, program) in rebuilt {
                device.delete_program(program);
            }
            return Err(errors);
        }

        let count = rebuilt.len();

        for (index, program) in rebuilt {
            let shader = &mut self.shaders[index];
            if let Some(old_program) = shader.program.replace(program) {
                device.delete_program(old_program);
            }
            // The cached projection describes uniform state uploaded to the
            // program that has just been replaced, so force the next bind to
            // upload it to the new one.
            shader.cached_projection = Transform3D::identity();
        }

        Ok(count)
    }

    #[cfg(feature = "debugger")]
    pub fn shaders(&self) -> &[LazilyCompiledShader] {
        &self.shaders
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
    cs_border_segment_superellipse: ShaderHandle,
    cs_border_solid_superellipse: ShaderHandle,
    cs_scale: Vec<Option<ShaderHandle>>,
    cs_line_decoration: ShaderHandle,
    cs_svg_filter_node: ShaderHandle,
    cs_path_tile_ssaa4: ShaderHandle,
    cs_path_tile_ssaa8: ShaderHandle,
    cs_path_tile_analytical: Option<ShaderHandle>,

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
    // ps_quad_textured comes in sampler-type-specific variants so that
    // external image sources (e.g. ANGLE DXGI textures) are sampled with the
    // matching sColor0 declaration. The variant is selected via PatternKind.
    ps_quad_textured: ShaderHandle,
    ps_quad_textured_external: Option<ShaderHandle>,
    ps_quad_textured_external_bt709: Option<ShaderHandle>,
    ps_quad_textured_rect: Option<ShaderHandle>,
    ps_quad_repeat: ShaderHandle,
    ps_quad_gradient: ShaderHandle,
    ps_quad_box_shadow: ShaderHandle,
    ps_quad_box_shadow_superellipse: ShaderHandle,
    // ps_quad_yuv, like ps_quad_textured, comes in sampler-type-specific
    // variants so the YUV planes are sampled with the matching sColor
    // declaration. The variant is selected via PatternKind.
    ps_quad_yuv: ShaderHandle,
    ps_quad_yuv_external: Option<ShaderHandle>,
    ps_quad_yuv_external_bt709: Option<ShaderHandle>,
    ps_quad_yuv_rect: Option<ShaderHandle>,
    ps_quad_backdrop: ShaderHandle,
    ps_quad_blend: ShaderHandle,
    ps_quad_mix_blend: ShaderHandle,
    ps_mask: ShaderHandle,
    ps_mask_fast: ShaderHandle,
    ps_mask_superellipse: ShaderHandle,
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
        options: &WebRenderOptions,
    ) -> Result<Self, ShaderError> {
        let use_dual_source_blending =
            device.get_capabilities().supports_dual_source_blending &&
            options.allow_dual_source_blending;
        let use_advanced_blend_equation =
            device.get_capabilities().supports_advanced_blend_equation &&
            options.allow_advanced_blend_equation;

        let texture_external_version = if device.get_capabilities().supports_external_textures_in_all_shaders {
            TextureExternalVersion::ESSL3
        } else {
            TextureExternalVersion::ESSL1
        };
        let mut shader_flags = device.shader_feature_flags();
        shader_flags.set(ShaderFeatureFlags::ADVANCED_BLEND_EQUATION, use_advanced_blend_equation);
        shader_flags.set(ShaderFeatureFlags::DUAL_SOURCE_BLENDING, use_dual_source_blending);
        shader_flags.set(ShaderFeatureFlags::DITHERING, options.enable_dithering);
        let shader_list = get_shader_features(shader_flags);

        let mut loader = ShaderLoader::new();

        let cs_blur_rgba8 = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::Blur),
            "cs_blur",
            &["COLOR_TARGET"],
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

        let ps_mask_superellipse = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::Mask),
            "ps_quad_mask",
            &[SUPERELLIPSE_FEATURE],
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
            &["TEXTURE_2D"],
            &shader_list,
        )?;

        // The TextureExternal variants are only used on devices that expose
        // GL_OES_EGL_image_external via ESSL3. ESSL1 doesn't support the
        // GLSL features used by the quad shaders.
        let ps_quad_textured_external = if has_platform_support(
                ImageBufferKind::TextureExternal, device,
            ) && texture_external_version == TextureExternalVersion::ESSL3
        {
            Some(loader.create_shader(
                ShaderKind::Primitive,
                "ps_quad_textured",
                &["TEXTURE_EXTERNAL"],
                &shader_list,
            )?)
        } else {
            None
        };

        let ps_quad_textured_external_bt709 = if has_platform_support(
            ImageBufferKind::TextureExternalBT709, device,
        ) {
            Some(loader.create_shader(
                ShaderKind::Primitive,
                "ps_quad_textured",
                &["TEXTURE_EXTERNAL_BT709"],
                &shader_list,
            )?)
        } else {
            None
        };

        let ps_quad_textured_rect = if has_platform_support(
            ImageBufferKind::TextureRect, device,
        ) {
            Some(loader.create_shader(
                ShaderKind::Primitive,
                "ps_quad_textured",
                &["TEXTURE_RECT"],
                &shader_list,
            )?)
        } else {
            None
        };

        let ps_quad_repeat = loader.create_shader(
            ShaderKind::Primitive,
            "ps_quad_repeat",
            &[],
            &shader_list,
        )?;

        let ps_quad_gradient = loader.create_shader(
            ShaderKind::Primitive,
            "ps_quad_gradient",
            if options.enable_dithering {
               &[DITHERING_FEATURE]
            } else {
               &[]
            },
            &shader_list,
        )?;

        let ps_quad_box_shadow = loader.create_shader(
            ShaderKind::Primitive,
            "ps_quad_box_shadow",
            &[],
            &shader_list,
        )?;

        let ps_quad_box_shadow_superellipse = loader.create_shader(
            ShaderKind::Primitive,
            "ps_quad_box_shadow",
            &[SUPERELLIPSE_FEATURE],
            &shader_list,
        )?;

        let ps_quad_yuv = loader.create_shader(
            ShaderKind::Primitive,
            "ps_quad_yuv",
            &["TEXTURE_2D"],
            &shader_list,
        )?;

        // The TextureExternal variant is only used on devices that expose
        // GL_OES_EGL_image_external via ESSL3 (ESSL1 doesn't support the GLSL
        // features used by the quad shaders); on ESSL1 such planes are routed
        // through the brush path in prepare.rs instead.
        let ps_quad_yuv_external = if has_platform_support(
                ImageBufferKind::TextureExternal, device,
            ) && texture_external_version == TextureExternalVersion::ESSL3
        {
            Some(loader.create_shader(
                ShaderKind::Primitive,
                "ps_quad_yuv",
                &["TEXTURE_EXTERNAL"],
                &shader_list,
            )?)
        } else {
            None
        };

        let ps_quad_yuv_external_bt709 = if has_platform_support(
            ImageBufferKind::TextureExternalBT709, device,
        ) {
            Some(loader.create_shader(
                ShaderKind::Primitive,
                "ps_quad_yuv",
                &["TEXTURE_EXTERNAL_BT709"],
                &shader_list,
            )?)
        } else {
            None
        };

        let ps_quad_yuv_rect = if has_platform_support(
            ImageBufferKind::TextureRect, device,
        ) {
            Some(loader.create_shader(
                ShaderKind::Primitive,
                "ps_quad_yuv",
                &["TEXTURE_RECT"],
                &shader_list,
            )?)
        } else {
            None
        };

        let ps_quad_backdrop = loader.create_shader(
            ShaderKind::Primitive,
            "ps_quad_backdrop",
            &["TEXTURE_2D"],
            &shader_list,
        )?;

        let ps_quad_blend = loader.create_shader(
            ShaderKind::Primitive,
            "ps_quad_blend",
            &["TEXTURE_2D"],
            &shader_list,
        )?;

        let ps_quad_mix_blend = loader.create_shader(
            ShaderKind::Primitive,
            "ps_quad_mix_blend",
            &["TEXTURE_2D"],
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

        let cs_line_decoration = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::LineDecoration),
            "cs_line_decoration",
            &[],
            &shader_list,
        )?;

        let cs_path_tile_ssaa4 = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::PathTile),
            "cs_path_tile",
            &["PATH_SSAA4"],
            &shader_list,
        )?;

        let cs_path_tile_ssaa8 = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::PathTile),
            "cs_path_tile",
            &["PATH_SSAA8"],
            &shader_list,
        )?;

        let cs_path_tile_analytical =
            if shader_flags.contains(ShaderFeatureFlags::PATH_AA_ALL_MODES) {
                Some(loader.create_shader(
                    ShaderKind::Cache(VertexArrayKind::PathTile),
                    "cs_path_tile",
                    &[],
                    &shader_list,
                )?)
            } else {
                None
            };

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

        let cs_border_segment_superellipse = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::Border),
            "cs_border_segment",
             &[SUPERELLIPSE_FEATURE],
            &shader_list,
        )?;

        let cs_border_solid_superellipse = loader.create_shader(
            ShaderKind::Cache(VertexArrayKind::Border),
            "cs_border_solid",
            &[SUPERELLIPSE_FEATURE],
            &shader_list,
        )?;

        let composite = CompositorShaders::new(device, &mut loader)?;

        Ok(Shaders {
            loader,

            cs_blur_rgba8,
            cs_border_segment,
            cs_border_solid,
            cs_border_segment_superellipse,
            cs_border_solid_superellipse,
            cs_line_decoration,
            cs_scale,
            cs_svg_filter_node,
            cs_path_tile_ssaa4,
            cs_path_tile_ssaa8,
            cs_path_tile_analytical,
            ps_text_run,
            ps_text_run_dual_source,
            ps_quad_textured,
            ps_quad_textured_external,
            ps_quad_textured_external_bt709,
            ps_quad_textured_rect,
            ps_quad_repeat,
            ps_quad_gradient,
            ps_quad_box_shadow,
            ps_quad_box_shadow_superellipse,
            ps_quad_yuv,
            ps_quad_yuv_external,
            ps_quad_yuv_external_bt709,
            ps_quad_yuv_rect,
            ps_quad_backdrop,
            ps_quad_blend,
            ps_quad_mix_blend,
            ps_mask,
            ps_mask_fast,
            ps_mask_superellipse,
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
        pattern: PatternKind,
    ) -> &mut LazilyCompiledShader {
        let shader_handle = match pattern {
            PatternKind::ColorOrTexture => self.ps_quad_textured,
            PatternKind::TextureExternal => self.ps_quad_textured_external
                .expect("bug: ps_quad_textured TEXTURE_EXTERNAL variant not loaded"),
            PatternKind::TextureExternalBT709 => self.ps_quad_textured_external_bt709
                .expect("bug: ps_quad_textured TEXTURE_EXTERNAL_BT709 variant not loaded"),
            PatternKind::TextureRect => self.ps_quad_textured_rect
                .expect("bug: ps_quad_textured TEXTURE_RECT variant not loaded"),
            PatternKind::Gradient => self.ps_quad_gradient,
            PatternKind::Repeat => self.ps_quad_repeat,
            PatternKind::BoxShadow => self.ps_quad_box_shadow,
            PatternKind::BoxShadowSuperellipse => self.ps_quad_box_shadow_superellipse,
            PatternKind::Yuv => self.ps_quad_yuv,
            PatternKind::YuvTextureExternal => self.ps_quad_yuv_external
                .expect("bug: ps_quad_yuv TEXTURE_EXTERNAL variant not loaded"),
            PatternKind::YuvTextureExternalBT709 => self.ps_quad_yuv_external_bt709
                .expect("bug: ps_quad_yuv TEXTURE_EXTERNAL_BT709 variant not loaded"),
            PatternKind::YuvTextureRect => self.ps_quad_yuv_rect
                .expect("bug: ps_quad_yuv TEXTURE_RECT variant not loaded"),
            PatternKind::Backdrop => self.ps_quad_backdrop,
            PatternKind::Blend => self.ps_quad_blend,
            PatternKind::MixBlend => self.ps_quad_mix_blend,
            PatternKind::Mask => unreachable!("clip mask pattern is not a quad shader"),
        };
        self.loader.get(shader_handle)
    }

    pub fn get(
        &mut self,
        key: &BatchKey,
        features: BatchFeatures,
        debug_flags: DebugFlags,
    ) -> &mut LazilyCompiledShader {
        let shader_handle = self.get_handle(key, features, debug_flags);
        self.loader.get(shader_handle)
    }

    pub fn get_handle(
        &mut self,
        key: &BatchKey,
        _features: BatchFeatures,
        debug_flags: DebugFlags,
    ) -> ShaderHandle {
        match key.kind {
            BatchKind::Quad(PatternKind::ColorOrTexture) => {
                self.ps_quad_textured
            }
            BatchKind::Quad(PatternKind::TextureExternal) => {
                self.ps_quad_textured_external
                    .expect("bug: ps_quad_textured TEXTURE_EXTERNAL variant not loaded")
            }
            BatchKind::Quad(PatternKind::TextureExternalBT709) => {
                self.ps_quad_textured_external_bt709
                    .expect("bug: ps_quad_textured TEXTURE_EXTERNAL_BT709 variant not loaded")
            }
            BatchKind::Quad(PatternKind::TextureRect) => {
                self.ps_quad_textured_rect
                    .expect("bug: ps_quad_textured TEXTURE_RECT variant not loaded")
            }
            BatchKind::Quad(PatternKind::Gradient) => {
                self.ps_quad_gradient
            }
            BatchKind::Quad(PatternKind::Repeat) => {
                self.ps_quad_repeat
            }
            BatchKind::Quad(PatternKind::BoxShadow) => {
                self.ps_quad_box_shadow
            }
            BatchKind::Quad(PatternKind::BoxShadowSuperellipse) => {
                self.ps_quad_box_shadow_superellipse
            }
            BatchKind::Quad(PatternKind::Yuv) => {
                self.ps_quad_yuv
            }
            BatchKind::Quad(PatternKind::YuvTextureExternal) => {
                self.ps_quad_yuv_external
                    .expect("bug: ps_quad_yuv TEXTURE_EXTERNAL variant not loaded")
            }
            BatchKind::Quad(PatternKind::YuvTextureExternalBT709) => {
                self.ps_quad_yuv_external_bt709
                    .expect("bug: ps_quad_yuv TEXTURE_EXTERNAL_BT709 variant not loaded")
            }
            BatchKind::Quad(PatternKind::YuvTextureRect) => {
                self.ps_quad_yuv_rect
                    .expect("bug: ps_quad_yuv TEXTURE_RECT variant not loaded")
            }
            BatchKind::Quad(PatternKind::Backdrop) => {
                self.ps_quad_backdrop
            }
            BatchKind::Quad(PatternKind::Blend) => {
                self.ps_quad_blend
            }
            BatchKind::Quad(PatternKind::MixBlend) => {
                self.ps_quad_mix_blend
            }
            BatchKind::Quad(PatternKind::Mask) => {
            unreachable!();
        }
            BatchKind::SplitComposite => {
                self.ps_split_composite
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
    pub fn cs_border_segment_superellipse(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_border_segment_superellipse) }
    pub fn cs_border_solid_superellipse(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_border_solid_superellipse) }
    pub fn cs_line_decoration(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_line_decoration) }
    pub fn cs_svg_filter_node(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.cs_svg_filter_node) }
    /// See IntParameter::PathAaSamples. Falls back to the ssaa8 variant where
    /// the analytical one is not available.
    pub fn cs_path_tile(&mut self, samples: i32) -> &mut LazilyCompiledShader {
        let handle = match samples {
            0 => self.cs_path_tile_analytical.unwrap_or(self.cs_path_tile_ssaa8),
            1..=4 => self.cs_path_tile_ssaa4,
            _ => self.cs_path_tile_ssaa8,
        };
        self.loader.get(handle)
    }
    pub fn ps_quad_textured(&mut self) -> &mut LazilyCompiledShader {
        self.loader.get(self.ps_quad_textured)
    }
    pub fn ps_mask(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.ps_mask) }
    pub fn ps_mask_fast(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.ps_mask_fast) }
    pub fn ps_mask_superellipse(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.ps_mask_superellipse) }
    pub fn ps_clear(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.ps_clear) }
    pub fn ps_copy(&mut self) -> &mut LazilyCompiledShader { self.loader.get(self.ps_copy) }

    /// Rebuild the compiled shaders affected by an edit to `changed_file`.
    /// See `ShaderLoader::reload`.
    #[cfg(feature = "debugger")]
    pub fn reload(
        &mut self,
        device: &mut Device,
        changed_file: &str,
    ) -> Result<usize, Vec<ShaderError>> {
        self.loader.reload(device, changed_file)
    }

    /// Every shader variant this instance may use, whether compiled or not.
    #[cfg(feature = "debugger")]
    pub fn variants(&self) -> &[LazilyCompiledShader] {
        self.loader.shaders()
    }

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

        let texture_external_version = if device.get_capabilities().supports_external_textures_in_all_shaders {
            TextureExternalVersion::ESSL3
        } else {
            TextureExternalVersion::ESSL1
        };

        let feature_flags = device.shader_feature_flags();
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
