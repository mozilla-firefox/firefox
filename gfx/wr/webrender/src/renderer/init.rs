/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{BlobImageHandler, ColorF, CrashAnnotator, DocumentId, IdNamespace};
use api::{VoidPtrToSizeFn, FontRenderMode, ImageFormat};
use api::{RenderNotifier, ImageBufferKind};
use api::units::*;
use api::channel::unbounded_channel;
pub use api::DebugFlags;

use crate::bump_allocator::ChunkPool;
use crate::render_api::{RenderApiSender, FrameMsg};
use crate::composite::{CompositorKind, CompositorConfig};
use crate::device::{
    UploadMethod, UploadPBOPool, VertexUsageHint, Device, ProgramCache, TextureFilter
};
use crate::frame_builder::FrameBuilderConfig;
use crate::glyph_cache::GlyphCache;
use glyph_rasterizer::{GlyphRasterThread, GlyphRasterizer, SharedFontResources};
use crate::gpu_types::PrimitiveInstanceData;
use crate::internal_types::{FastHashMap, FastHashSet, FrameId};
use crate::picture;
use crate::profiler::{self, Profiler, TransactionProfile};
use crate::device::query::{GpuProfiler, GpuDebugMethod};
use crate::render_backend::RenderBackend;
use crate::resource_cache::ResourceCache;
use crate::scene_builder_thread::{SceneBuilderThread, SceneBuilderThreadChannels, LowPrioritySceneBuilderThread};
use crate::texture_cache::{TextureCache, TextureCacheConfig};
use crate::picture_textures::PictureTextures;
use crate::renderer::{
    debug, gpu_cache, vertex, gl,
    Renderer, DebugOverlayState, BufferDamageTracker, PipelineInfo, TextureResolver,
    RendererError, ShaderPrecacheFlags, VERTEX_DATA_TEXTURE_COUNT,
    upload::UploadTexturePool,
    shade::{Shaders, SharedShaders},
};

use std::{
    mem,
    thread,
    cell::RefCell,
    collections::VecDeque,
    rc::Rc,
    sync::{Arc, atomic::{AtomicBool, Ordering}},
    num::NonZeroUsize,
    path::PathBuf,
};

use tracy_rs::register_thread_with_profiler;
use rayon::{ThreadPool, ThreadPoolBuilder};
use malloc_size_of::MallocSizeOfOps;

/// Use this hint for all vertex data re-initialization. This allows
/// the driver to better re-use RBOs internally.
pub const ONE_TIME_USAGE_HINT: VertexUsageHint = VertexUsageHint::Stream;

/// Is only false if no WR instances have ever been created.
static HAS_BEEN_INITIALIZED: AtomicBool = AtomicBool::new(false);

/// Returns true if a WR instance has ever been initialized in this process.
pub fn wr_has_been_initialized() -> bool {
    HAS_BEEN_INITIALIZED.load(Ordering::SeqCst)
}

/// Allows callers to hook in at certain points of the async scene build. These
/// functions are all called from the scene builder thread.
pub trait SceneBuilderHooks {
    /// This is called exactly once, when the scene builder thread is started
    /// and before it processes anything.
    fn register(&self);
    /// This is called before each scene build starts.
    fn pre_scene_build(&self);
    /// This is called before each scene swap occurs.
    fn pre_scene_swap(&self);
    /// This is called after each scene swap occurs. The PipelineInfo contains
    /// the updated epochs and pipelines removed in the new scene compared to
    /// the old scene.
    fn post_scene_swap(&self, document_id: &Vec<DocumentId>, info: PipelineInfo, schedule_frame: bool);
    /// This is called after a resource update operation on the scene builder
    /// thread, in the case where resource updates were applied without a scene
    /// build.
    fn post_resource_update(&self, document_ids: &Vec<DocumentId>);
    /// This is called after a scene build completes without any changes being
    /// made. We guarantee that each pre_scene_build call will be matched with
    /// exactly one of post_scene_swap, post_resource_update or
    /// post_empty_scene_build.
    fn post_empty_scene_build(&self);
    /// This is a generic callback which provides an opportunity to run code
    /// on the scene builder thread. This is called as part of the main message
    /// loop of the scene builder thread, but outside of any specific message
    /// handler.
    fn poke(&self);
    /// This is called exactly once, when the scene builder thread is about to
    /// terminate.
    fn deregister(&self);
}

/// Allows callers to hook into the main render_backend loop and provide
/// additional frame ops for generate_frame transactions. These functions
/// are all called from the render backend thread.
pub trait AsyncPropertySampler {
    /// This is called exactly once, when the render backend thread is started
    /// and before it processes anything.
    fn register(&self);
    /// This is called for each transaction with the generate_frame flag set
    /// (i.e. that will trigger a render). The list of frame messages returned
    /// are processed as though they were part of the original transaction.
    fn sample(&self, document_id: DocumentId, generated_frame_id: Option<u64>) -> Vec<FrameMsg>;
    /// This is called exactly once, when the render backend thread is about to
    /// terminate.
    fn deregister(&self);
}

pub trait RenderBackendHooks {
    fn init_thread(&self);
}

pub struct WebRenderOptions {
    pub resource_override_path: Option<PathBuf>,
    /// Whether to use shaders that have been optimized at build time.
    pub use_optimized_shaders: bool,
    pub enable_aa: bool,
    pub enable_dithering: bool,
    pub max_recorded_profiles: usize,
    pub precache_flags: ShaderPrecacheFlags,
    /// Enable sub-pixel anti-aliasing if a fast implementation is available.
    pub enable_subpixel_aa: bool,
    pub clear_color: ColorF,
    pub enable_clear_scissor: Option<bool>,
    pub max_internal_texture_size: Option<i32>,
    pub image_tiling_threshold: i32,
    pub upload_method: UploadMethod,
    /// The default size in bytes for PBOs used to upload texture data.
    pub upload_pbo_default_size: usize,
    pub batched_upload_threshold: i32,
    pub workers: Option<Arc<ThreadPool>>,
    /// A pool of large memory chunks used by the per-frame allocators.
    /// Providing the pool here makes it possible to share a single pool for
    /// all WebRender instances.
    pub chunk_pool: Option<Arc<ChunkPool>>,
    pub dedicated_glyph_raster_thread: Option<GlyphRasterThread>,
    pub enable_multithreading: bool,
    pub blob_image_handler: Option<Box<dyn BlobImageHandler>>,
    pub crash_annotator: Option<Box<dyn CrashAnnotator>>,
    pub size_of_op: Option<VoidPtrToSizeFn>,
    pub enclosing_size_of_op: Option<VoidPtrToSizeFn>,
    pub cached_programs: Option<Rc<ProgramCache>>,
    pub debug_flags: DebugFlags,
    pub renderer_id: Option<u64>,
    pub scene_builder_hooks: Option<Box<dyn SceneBuilderHooks + Send>>,
    pub render_backend_hooks: Option<Box<dyn RenderBackendHooks + Send>>,
    pub sampler: Option<Box<dyn AsyncPropertySampler + Send>>,
    pub support_low_priority_transactions: bool,
    pub namespace_alloc_by_client: bool,
    /// If namespaces are allocated by the client, then the namespace for fonts
    /// must also be allocated by the client to avoid namespace collisions with
    /// the backend.
    pub shared_font_namespace: Option<IdNamespace>,
    pub testing: bool,
    /// Set to true if this GPU supports hardware fast clears as a performance
    /// optimization. Likely requires benchmarking on various GPUs to see if
    /// it is a performance win. The default is false, which tends to be best
    /// performance on lower end / integrated GPUs.
    pub gpu_supports_fast_clears: bool,
    pub allow_dual_source_blending: bool,
    pub allow_advanced_blend_equation: bool,
    /// If true, allow textures to be initialized with glTexStorage.
    /// This affects VRAM consumption and data upload paths.
    pub allow_texture_storage_support: bool,
    /// If true, we allow the data uploaded in a different format from the
    /// one expected by the driver, pretending the format is matching, and
    /// swizzling the components on all the shader sampling.
    pub allow_texture_swizzling: bool,
    /// Use `ps_clear` shader with batched quad rendering to clear the rects
    /// in texture cache and picture cache tasks.
    /// This helps to work around some Intel drivers
    /// that incorrectly synchronize clears to following draws.
    pub clear_caches_with_quads: bool,
    /// Output the source of the shader with the given name.
    pub dump_shader_source: Option<String>,
    pub surface_origin_is_top_left: bool,
    /// The configuration options defining how WR composites the final scene.
    pub compositor_config: CompositorConfig,
    pub enable_gpu_markers: bool,
    /// If true, panic whenever a GL error occurs. This has a significant
    /// performance impact, so only use when debugging specific problems!
    pub panic_on_gl_error: bool,
    pub picture_tile_size: Option<DeviceIntSize>,
    pub texture_cache_config: TextureCacheConfig,
    /// If true, we'll use instanced vertex attributes. Each instace is a quad.
    /// If false, we'll duplicate the instance attributes per vertex and issue
    /// regular indexed draws instead.
    pub enable_instancing: bool,
    /// If true, we'll reject contexts backed by a software rasterizer, except
    /// Software WebRender.
    pub reject_software_rasterizer: bool,
    /// If enabled, pinch-zoom will apply the zoom factor during compositing
    /// of picture cache tiles. This is higher performance (tiles are not
    /// re-rasterized during zoom) but lower quality result. For most display
    /// items, if the zoom factor is relatively small, bilinear filtering should
    /// make the result look quite close to the high-quality zoom, except for glyphs.
    pub low_quality_pinch_zoom: bool,
    pub max_shared_surface_size: i32,
}

impl WebRenderOptions {
    /// Number of batches to look back in history for adding the current
    /// transparent instance into.
    const BATCH_LOOKBACK_COUNT: usize = 10;

    /// Since we are re-initializing the instance buffers on every draw call,
    /// the driver has to internally manage PBOs in flight.
    /// It's typically done by bucketing up to a specific limit, and then
    /// just individually managing the largest buffers.
    /// Having a limit here allows the drivers to more easily manage
    /// the PBOs for us.
    const MAX_INSTANCE_BUFFER_SIZE: usize = 0x20000; // actual threshold in macOS GL drivers
}

impl Default for WebRenderOptions {
    fn default() -> Self {
        WebRenderOptions {
            resource_override_path: None,
            use_optimized_shaders: false,
            enable_aa: true,
            enable_dithering: false,
            debug_flags: DebugFlags::empty(),
            max_recorded_profiles: 0,
            precache_flags: ShaderPrecacheFlags::empty(),
            enable_subpixel_aa: false,
            clear_color: ColorF::new(1.0, 1.0, 1.0, 1.0),
            enable_clear_scissor: None,
            max_internal_texture_size: None,
            image_tiling_threshold: 4096,
            // This is best as `Immediate` on Angle, or `Pixelbuffer(Dynamic)` on GL,
            // but we are unable to make this decision here, so picking the reasonable medium.
            upload_method: UploadMethod::PixelBuffer(ONE_TIME_USAGE_HINT),
            upload_pbo_default_size: 512 * 512 * 4,
            batched_upload_threshold: 512 * 512,
            workers: None,
            chunk_pool: None,
            dedicated_glyph_raster_thread: None,
            enable_multithreading: true,
            blob_image_handler: None,
            crash_annotator: None,
            size_of_op: None,
            enclosing_size_of_op: None,
            renderer_id: None,
            cached_programs: None,
            scene_builder_hooks: None,
            render_backend_hooks: None,
            sampler: None,
            support_low_priority_transactions: false,
            namespace_alloc_by_client: false,
            shared_font_namespace: None,
            testing: false,
            gpu_supports_fast_clears: false,
            allow_dual_source_blending: true,
            allow_advanced_blend_equation: false,
            allow_texture_storage_support: true,
            allow_texture_swizzling: true,
            clear_caches_with_quads: true,
            dump_shader_source: None,
            surface_origin_is_top_left: false,
            compositor_config: CompositorConfig::default(),
            enable_gpu_markers: true,
            panic_on_gl_error: false,
            picture_tile_size: None,
            texture_cache_config: TextureCacheConfig::DEFAULT,
            // Disabling instancing means more vertex data to upload and potentially
            // process by the vertex shaders.
            enable_instancing: true,
            reject_software_rasterizer: false,
            low_quality_pinch_zoom: false,
            max_shared_surface_size: 2048,
        }
    }
}

/// Initializes WebRender and creates a `Renderer` and `RenderApiSender`.
///
/// # Examples
/// Initializes a `Renderer` with some reasonable values. For more information see
/// [`WebRenderOptions`][WebRenderOptions].
///
/// ```rust,ignore
/// # use webrender::renderer::Renderer;
/// # use std::path::PathBuf;
/// let opts = webrender::WebRenderOptions {
///    device_pixel_ratio: 1.0,
///    resource_override_path: None,
///    enable_aa: false,
/// };
/// let (renderer, sender) = Renderer::new(opts);
/// ```
/// [WebRenderOptions]: struct.WebRenderOptions.html
pub fn create_webrender_instance(
    gl: Rc<dyn gl::Gl>,
    notifier: Box<dyn RenderNotifier>,
    mut options: WebRenderOptions,
    shaders: Option<&SharedShaders>,
) -> Result<(Renderer, RenderApiSender), RendererError> {
    if !wr_has_been_initialized() {
        // If the profiler feature is enabled, try to load the profiler shared library
        // if the path was provided.
        #[cfg(feature = "profiler")]
        unsafe {
            if let Ok(ref tracy_path) = std::env::var("WR_TRACY_PATH") {
                let ok = tracy_rs::load(tracy_path);
                info!("Load tracy from {} -> {}", tracy_path, ok);
            }
        }

        register_thread_with_profiler("Compositor".to_owned());
    }

    HAS_BEEN_INITIALIZED.store(true, Ordering::SeqCst);

    // For now, we assume that native OS compositors are top-left origin. If that doesn't
    // turn out to be the case, we can add a query method on `LayerCompositor`.
    match options.compositor_config {
        CompositorConfig::Draw { .. } | CompositorConfig::Native { .. } => {}
        CompositorConfig::Layer { .. } => {
            options.surface_origin_is_top_left = true;
        }
    }

    let (api_tx, api_rx) = unbounded_channel();
    let (result_tx, result_rx) = unbounded_channel();
    let gl_type = gl.get_type();

    let mut device = Device::new(
        gl,
        options.crash_annotator.clone(),
        options.resource_override_path.clone(),
        options.use_optimized_shaders,
        options.upload_method.clone(),
        options.batched_upload_threshold,
        options.cached_programs.take(),
        options.allow_texture_storage_support,
        options.allow_texture_swizzling,
        options.dump_shader_source.take(),
        options.surface_origin_is_top_left,
        options.panic_on_gl_error,
    );

    let color_cache_formats = device.preferred_color_formats();
    let swizzle_settings = device.swizzle_settings();
    let use_dual_source_blending =
        device.get_capabilities().supports_dual_source_blending &&
        options.allow_dual_source_blending;
    let ext_blend_equation_advanced =
        options.allow_advanced_blend_equation &&
        device.get_capabilities().supports_advanced_blend_equation;
    let ext_blend_equation_advanced_coherent =
        device.supports_extension("GL_KHR_blend_equation_advanced_coherent");

    let enable_clear_scissor = options
        .enable_clear_scissor
        .unwrap_or(device.get_capabilities().prefers_clear_scissor);

    // 2048 is the minimum that the texture cache can work with.
    const MIN_TEXTURE_SIZE: i32 = 2048;
    let mut max_internal_texture_size = device.max_texture_size();
    if max_internal_texture_size < MIN_TEXTURE_SIZE {
        // Broken GL contexts can return a max texture size of zero (See #1260).
        // Better to gracefully fail now than panic as soon as a texture is allocated.
        error!(
            "Device reporting insufficient max texture size ({})",
            max_internal_texture_size
        );
        return Err(RendererError::MaxTextureSize);
    }
    if let Some(internal_limit) = options.max_internal_texture_size {
        assert!(internal_limit >= MIN_TEXTURE_SIZE);
        max_internal_texture_size = max_internal_texture_size.min(internal_limit);
    }

    if options.reject_software_rasterizer {
        let renderer_name_lc = device.get_capabilities().renderer_name.to_lowercase();
        if renderer_name_lc.contains("llvmpipe") || renderer_name_lc.contains("softpipe") || renderer_name_lc.contains("software rasterizer") {
        return Err(RendererError::SoftwareRasterizer);
        }
    }

    let image_tiling_threshold = options.image_tiling_threshold
        .min(max_internal_texture_size);

    device.begin_frame();

    let shaders = match shaders {
        Some(shaders) => Rc::clone(shaders),
        None => {
            let mut shaders = Shaders::new(&mut device, gl_type, &options)?;
            if options.precache_flags.intersects(ShaderPrecacheFlags::ASYNC_COMPILE | ShaderPrecacheFlags::FULL_COMPILE) {
                let mut pending_shaders = shaders.precache_all(options.precache_flags);
                while shaders.resume_precache(&mut device, &mut pending_shaders)? {}
            }
            Rc::new(RefCell::new(shaders))
        }
    };

    let dither_matrix_texture = if options.enable_dithering {
        let dither_matrix: [u8; 64] = [
            0,
            48,
            12,
            60,
            3,
            51,
            15,
            63,
            32,
            16,
            44,
            28,
            35,
            19,
            47,
            31,
            8,
            56,
            4,
            52,
            11,
            59,
            7,
            55,
            40,
            24,
            36,
            20,
            43,
            27,
            39,
            23,
            2,
            50,
            14,
            62,
            1,
            49,
            13,
            61,
            34,
            18,
            46,
            30,
            33,
            17,
            45,
            29,
            10,
            58,
            6,
            54,
            9,
            57,
            5,
            53,
            42,
            26,
            38,
            22,
            41,
            25,
            37,
            21,
        ];

        let texture = device.create_texture(
            ImageBufferKind::Texture2D,
            ImageFormat::R8,
            8,
            8,
            TextureFilter::Nearest,
            None,
        );
        device.upload_texture_immediate(&texture, &dither_matrix);

        Some(texture)
    } else {
        None
    };

    let max_primitive_instance_count =
        WebRenderOptions::MAX_INSTANCE_BUFFER_SIZE / mem::size_of::<PrimitiveInstanceData>();
    let vaos = vertex::RendererVAOs::new(
        &mut device,
        if options.enable_instancing { None } else { NonZeroUsize::new(max_primitive_instance_count) },
    );

    let texture_upload_pbo_pool = UploadPBOPool::new(&mut device, options.upload_pbo_default_size);
    let staging_texture_pool = UploadTexturePool::new();
    let texture_resolver = TextureResolver::new(&mut device);

    let mut vertex_data_textures = Vec::new();
    for _ in 0 .. VERTEX_DATA_TEXTURE_COUNT {
        vertex_data_textures.push(vertex::VertexDataTextures::new());
    }

    // On some (mostly older, integrated) GPUs, the normal GPU texture cache update path
    // doesn't work well when running on ANGLE, causing CPU stalls inside D3D and/or the
    // GPU driver. See https://bugzilla.mozilla.org/show_bug.cgi?id=1576637 for much
    // more detail. To reduce the number of code paths we have active that require testing,
    // we will enable the GPU cache scatter update path on all devices running with ANGLE.
    // We want a better solution long-term, but for now this is a significant performance
    // improvement on HD4600 era GPUs, and shouldn't hurt performance in a noticeable
    // way on other systems running under ANGLE.
    let is_software = device.get_capabilities().renderer_name.starts_with("Software");

    // On other GL platforms, like macOS or Android, creating many PBOs is very inefficient.
    // This is what happens in GPU cache updates in PBO path. Instead, we switch everything
    // except software GL to use the GPU scattered updates.
    let supports_scatter = device.get_capabilities().supports_color_buffer_float;
    let gpu_cache_texture = gpu_cache::GpuCacheTexture::new(
        &mut device,
        supports_scatter && !is_software,
    )?;

    device.end_frame();

    let backend_notifier = notifier.clone();

    let clear_alpha_targets_with_quads = !device.get_capabilities().supports_alpha_target_clears;

    let prefer_subpixel_aa = options.enable_subpixel_aa && use_dual_source_blending;
    let default_font_render_mode = match (options.enable_aa, prefer_subpixel_aa) {
        (true, true) => FontRenderMode::Subpixel,
        (true, false) => FontRenderMode::Alpha,
        (false, _) => FontRenderMode::Mono,
    };

    let compositor_kind = match options.compositor_config {
        CompositorConfig::Draw { max_partial_present_rects, draw_previous_partial_present_regions, .. } => {
            CompositorKind::Draw { max_partial_present_rects, draw_previous_partial_present_regions }
        }
        CompositorConfig::Native { ref compositor } => {
            let capabilities = compositor.get_capabilities(&mut device);

            CompositorKind::Native {
                capabilities,
            }
        }
        CompositorConfig::Layer { .. } => {
            CompositorKind::Layer {
            }
        }
    };

    let config = FrameBuilderConfig {
        default_font_render_mode,
        dual_source_blending_is_supported: use_dual_source_blending,
        testing: options.testing,
        gpu_supports_fast_clears: options.gpu_supports_fast_clears,
        gpu_supports_advanced_blend: ext_blend_equation_advanced,
        advanced_blend_is_coherent: ext_blend_equation_advanced_coherent,
        gpu_supports_render_target_partial_update: device.get_capabilities().supports_render_target_partial_update,
        external_images_require_copy: !device.get_capabilities().supports_image_external_essl3,
        batch_lookback_count: WebRenderOptions::BATCH_LOOKBACK_COUNT,
        background_color: Some(options.clear_color),
        compositor_kind,
        tile_size_override: None,
        max_surface_override: None,
        max_depth_ids: device.max_depth_ids(),
        max_target_size: max_internal_texture_size,
        force_invalidation: false,
        is_software,
        low_quality_pinch_zoom: options.low_quality_pinch_zoom,
        max_shared_surface_size: options.max_shared_surface_size,
    };
    info!("WR {:?}", config);

    let debug_flags = options.debug_flags;
    let size_of_op = options.size_of_op;
    let enclosing_size_of_op = options.enclosing_size_of_op;
    let make_size_of_ops =
        move || size_of_op.map(|o| MallocSizeOfOps::new(o, enclosing_size_of_op));
    let workers = options
        .workers
        .take()
        .unwrap_or_else(|| {
            let worker = ThreadPoolBuilder::new()
                .thread_name(|idx|{ format!("WRWorker#{}", idx) })
                .start_handler(move |idx| {
                    register_thread_with_profiler(format!("WRWorker#{}", idx));
                    profiler::register_thread(&format!("WRWorker#{}", idx));
                })
                .exit_handler(move |_idx| {
                    profiler::unregister_thread();
                })
                .build();
            Arc::new(worker.unwrap())
        });
    let sampler = options.sampler;
    let namespace_alloc_by_client = options.namespace_alloc_by_client;

    // Ensure shared font keys exist within their own unique namespace so
    // that they don't accidentally collide across Renderer instances.
    let font_namespace = if namespace_alloc_by_client {
        options.shared_font_namespace.expect("Shared font namespace must be allocated by client")
    } else {
        RenderBackend::next_namespace_id()
    };
    let fonts = SharedFontResources::new(font_namespace);

    let blob_image_handler = options.blob_image_handler.take();
    let scene_builder_hooks = options.scene_builder_hooks;
    let rb_thread_name = format!("WRRenderBackend#{}", options.renderer_id.unwrap_or(0));
    let scene_thread_name = format!("WRSceneBuilder#{}", options.renderer_id.unwrap_or(0));
    let lp_scene_thread_name = format!("WRSceneBuilderLP#{}", options.renderer_id.unwrap_or(0));

    let glyph_rasterizer = GlyphRasterizer::new(
        workers,
        options.dedicated_glyph_raster_thread,
        device.get_capabilities().supports_r8_texture_upload,
    );

    let (scene_builder_channels, scene_tx) =
        SceneBuilderThreadChannels::new(api_tx.clone());

    let sb_fonts = fonts.clone();

    thread::Builder::new().name(scene_thread_name.clone()).spawn(move || {
        register_thread_with_profiler(scene_thread_name.clone());
        profiler::register_thread(&scene_thread_name);

        let mut scene_builder = SceneBuilderThread::new(
            config,
            sb_fonts,
            make_size_of_ops(),
            scene_builder_hooks,
            scene_builder_channels,
        );
        scene_builder.run();

        profiler::unregister_thread();
    })?;

    let low_priority_scene_tx = if options.support_low_priority_transactions {
        let (low_priority_scene_tx, low_priority_scene_rx) = unbounded_channel();
        let lp_builder = LowPrioritySceneBuilderThread {
            rx: low_priority_scene_rx,
            tx: scene_tx.clone(),
            tile_pool: api::BlobTilePool::new(),
        };

        thread::Builder::new().name(lp_scene_thread_name.clone()).spawn(move || {
            register_thread_with_profiler(lp_scene_thread_name.clone());
            profiler::register_thread(&lp_scene_thread_name);

            let mut scene_builder = lp_builder;
            scene_builder.run();

            profiler::unregister_thread();
        })?;

        low_priority_scene_tx
    } else {
        scene_tx.clone()
    };

    let rb_blob_handler = blob_image_handler
        .as_ref()
        .map(|handler| handler.create_similar());

    let texture_cache_config = options.texture_cache_config.clone();
    let mut picture_tile_size = options.picture_tile_size.unwrap_or(picture::TILE_SIZE_DEFAULT);
    // Clamp the picture tile size to reasonable values.
    picture_tile_size.width = picture_tile_size.width.max(128).min(4096);
    picture_tile_size.height = picture_tile_size.height.max(128).min(4096);

    let picture_texture_filter = if options.low_quality_pinch_zoom {
        TextureFilter::Linear
    } else {
        TextureFilter::Nearest
    };

    let render_backend_hooks = options.render_backend_hooks.take();

    let chunk_pool = options.chunk_pool.take().unwrap_or_else(|| {
        Arc::new(ChunkPool::new())
    });

    let rb_scene_tx = scene_tx.clone();
    let rb_fonts = fonts.clone();
    let enable_multithreading = options.enable_multithreading;
    thread::Builder::new().name(rb_thread_name.clone()).spawn(move || {
        if let Some(hooks) = render_backend_hooks {
            hooks.init_thread();
        }
        register_thread_with_profiler(rb_thread_name.clone());
        profiler::register_thread(&rb_thread_name);

        let texture_cache = TextureCache::new(
            max_internal_texture_size,
            image_tiling_threshold,
            color_cache_formats,
            swizzle_settings,
            &texture_cache_config,
        );

        let picture_textures = PictureTextures::new(
            picture_tile_size,
            picture_texture_filter,
        );

        let glyph_cache = GlyphCache::new();

        let mut resource_cache = ResourceCache::new(
            texture_cache,
            picture_textures,
            glyph_rasterizer,
            glyph_cache,
            rb_fonts,
            rb_blob_handler,
        );

        resource_cache.enable_multithreading(enable_multithreading);

        let mut backend = RenderBackend::new(
            api_rx,
            result_tx,
            rb_scene_tx,
            resource_cache,
            chunk_pool,
            backend_notifier,
            config,
            sampler,
            make_size_of_ops(),
            debug_flags,
            namespace_alloc_by_client,
        );
        backend.run();
        profiler::unregister_thread();
    })?;

    let debug_method = if !options.enable_gpu_markers {
        // The GPU markers are disabled.
        GpuDebugMethod::None
    } else if device.get_capabilities().supports_khr_debug {
        GpuDebugMethod::KHR
    } else if device.supports_extension("GL_EXT_debug_marker") {
        GpuDebugMethod::MarkerEXT
    } else {
        warn!("asking to enable_gpu_markers but no supporting extension was found");
        GpuDebugMethod::None
    };

    info!("using {:?}", debug_method);

    let gpu_profiler = GpuProfiler::new(Rc::clone(device.rc_gl()), debug_method);
    #[cfg(feature = "capture")]
    let read_fbo = device.create_fbo();

    let mut renderer = Renderer {
        result_rx,
        api_tx: api_tx.clone(),
        device,
        active_documents: FastHashMap::default(),
        pending_texture_updates: Vec::new(),
        pending_texture_cache_updates: false,
        pending_native_surface_updates: Vec::new(),
        pending_gpu_cache_updates: Vec::new(),
        pending_gpu_cache_clear: false,
        pending_shader_updates: Vec::new(),
        shaders,
        debug: debug::LazyInitializedDebugRenderer::new(),
        debug_flags: DebugFlags::empty(),
        profile: TransactionProfile::new(),
        frame_counter: 0,
        resource_upload_time: 0.0,
        gpu_cache_upload_time: 0.0,
        profiler: Profiler::new(),
        max_recorded_profiles: options.max_recorded_profiles,
        clear_color: options.clear_color,
        enable_clear_scissor,
        enable_advanced_blend_barriers: !ext_blend_equation_advanced_coherent,
        clear_caches_with_quads: options.clear_caches_with_quads,
        clear_alpha_targets_with_quads,
        last_time: 0,
        gpu_profiler,
        vaos,
        vertex_data_textures,
        current_vertex_data_textures: 0,
        pipeline_info: PipelineInfo::default(),
        dither_matrix_texture,
        external_image_handler: None,
        size_of_ops: make_size_of_ops(),
        cpu_profiles: VecDeque::new(),
        gpu_profiles: VecDeque::new(),
        gpu_cache_texture,
        gpu_cache_debug_chunks: Vec::new(),
        gpu_cache_frame_id: FrameId::INVALID,
        gpu_cache_overflow: false,
        texture_upload_pbo_pool,
        staging_texture_pool,
        texture_resolver,
        renderer_errors: Vec::new(),
        async_frame_recorder: None,
        async_screenshots: None,
        #[cfg(feature = "capture")]
        read_fbo,
        #[cfg(feature = "replay")]
        owned_external_images: FastHashMap::default(),
        notifications: Vec::new(),
        device_size: None,
        zoom_debug_texture: None,
        cursor_position: DeviceIntPoint::zero(),
        shared_texture_cache_cleared: false,
        documents_seen: FastHashSet::default(),
        force_redraw: true,
        compositor_config: options.compositor_config,
        current_compositor_kind: compositor_kind,
        allocated_native_surfaces: FastHashSet::default(),
        debug_overlay_state: DebugOverlayState::new(),
        buffer_damage_tracker: BufferDamageTracker::default(),
        max_primitive_instance_count,
        enable_instancing: options.enable_instancing,
        consecutive_oom_frames: 0,
        target_frame_publish_id: None,
        pending_result_msg: None,
    };

    // We initially set the flags to default and then now call set_debug_flags
    // to ensure any potential transition when enabling a flag is run.
    renderer.set_debug_flags(debug_flags);

    let sender = RenderApiSender::new(
        api_tx,
        scene_tx,
        low_priority_scene_tx,
        blob_image_handler,
        fonts,
    );
    Ok((renderer, sender))
}
