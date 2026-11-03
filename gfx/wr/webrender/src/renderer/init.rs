/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{BlobImageHandler, ColorF, CrashAnnotator, DocumentId, IdNamespace};
use api::{RenderBackendId, VoidPtrToSizeFn, FontRenderMode, ImageFormat};
use api::{RenderNotifier, ImageBufferKind, PipelineId};
use api::units::*;
use api::channel::unbounded_channel;
pub use api::DebugFlags;

use crate::bump_allocator::ChunkPool;
use crate::render_api::{ApiMsg, RenderApiSender, FrameMsg, ResourceCacheInit, WindowRegistration};
use crate::render_backend_pool::{PoolMemberSetup, RenderBackendPool};
use crate::scene_builder_thread::SceneBuilderRequest;
use crate::composite::{CompositorKind, CompositorConfig, SwapchainMode};
use super::layer_tree::LayerTree;
use crate::device::{
    DeviceOptions, GpuBackendConfig, UploadMethod, UploadBufferPool, VertexUsageHint, Device, ProgramCache, TextureFilter
};
use crate::frame_builder::FrameBuilderConfig;
use glyph_rasterizer::{GlyphRasterThread, SharedFontResources};
use crate::gpu_types::PrimitiveInstanceData;
use crate::internal_types::{FastHashMap, FastHashSet};
use crate::profiler::{self, Profiler, TransactionProfile};
use crate::render_backend::RenderBackend;
use crate::texture_cache::TextureCacheConfig;
use crate::renderer::{
    debug, vertex,
    debug::DebugOverlayState,
    Renderer, BufferDamageTracker, PipelineInfo, TextureResolver,
    RendererError, ShaderPrecacheFlags,
    upload::UploadTexturePool,
    shade::{Shaders, SharedShaders},
};
#[cfg(feature = "debugger")]
use crate::debugger::Debugger;

use std::{
    mem,
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

/// Process-wide allocator for `RenderBackendId`. Starts at 1 so `RenderBackendId(0)`
/// (the `Default`) is distinguishable from a valid id.
static NEXT_RENDER_BACKEND_ID: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(1);

fn next_render_backend_id() -> RenderBackendId {
    let id = NEXT_RENDER_BACKEND_ID.fetch_add(1, Ordering::Relaxed);
    RenderBackendId(id)
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
    /// This is called when the embed policy of `embedder` rejects an iframe
    /// item that references `child`, once per pair and document.
    fn embed_policy_violation(&self, _document_id: DocumentId, _embedder: PipelineId, _child: PipelineId) {}
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
    /// Delay promotion of moving 8-bit YUV images to compositor surfaces until
    /// their picture-space coverage rect has remained stable for several frames.
    pub enable_yuv_overlay_stability: bool,
    /// Promote only the largest visible 8-bit YUV image to external compositing.
    /// Higher bit depths and RGB images retain their existing behavior.
    pub limit_sdr_yuv_external_composites: bool,
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
    pub upload_buffer_default_size: usize,
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
    /// Optional shared render backend pool. When provided, the new window is
    /// assigned to one of the pool's existing backend threads round-robin.
    /// When `None`, a private size-1 pool is created so the new window has
    /// its own dedicated backend thread (the historical behavior).
    pub render_backend_pool: Option<Arc<RenderBackendPool>>,
    pub sampler: Option<Box<dyn AsyncPropertySampler + Send>>,
    pub support_low_priority_transactions: bool,
    pub namespace_alloc_by_client: bool,
    /// If namespaces are allocated by the client, then the namespace for fonts
    /// must also be allocated by the client to avoid namespace collisions with
    /// the backend.
    pub shared_font_namespace: Option<IdNamespace>,
    /// When set, the renderer reuses this `SharedFontResources` instance
    /// instead of constructing a fresh one. Required for the shared-pool
    /// mode so that every window assigned to the pool — and the pool's
    /// scene builders — see the same font / glyph instance maps. Otherwise
    /// each window's `RenderApi` would register fonts into its own
    /// `SharedFontResources` and the pool's SB would never see them.
    pub shared_fonts: Option<crate::glyph_rasterizer::SharedFontResources>,
    pub testing: bool,
    /// Set to true if this GPU supports hardware fast clears as a performance
    /// optimization. Likely requires benchmarking on various GPUs to see if
    /// it is a performance win. The default is false, which tends to be best
    /// performance on lower end / integrated GPUs.
    pub gpu_supports_fast_clears: bool,
    pub allow_dual_source_blending: bool,
    pub allow_advanced_blend_equation: bool,
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
    pub picture_tile_size: Option<DeviceIntSize>,
    pub texture_cache_config: TextureCacheConfig,
    /// If true, we'll use instanced vertex attributes. Each instace is a quad.
    /// If false, we'll duplicate the instance attributes per vertex and issue
    /// regular indexed draws instead.
    pub enable_instancing: bool,
    /// If true, stream instance data into large buffers shared by multiple
    /// draws, rather than allocating a new instance buffer for each draw.
    pub enable_shared_instance_buffer: bool,
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
    /// If true, open a debug socket to listen for remote debugger.
    /// Relies on `debugger` cargo feature being enabled.
    pub enable_debugger: bool,
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
            enable_yuv_overlay_stability: false,
            limit_sdr_yuv_external_composites: false,
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
            upload_buffer_default_size: 512 * 512 * 4,
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
            render_backend_pool: None,
            sampler: None,
            support_low_priority_transactions: false,
            namespace_alloc_by_client: false,
            shared_font_namespace: None,
            shared_fonts: None,
            testing: false,
            gpu_supports_fast_clears: false,
            allow_dual_source_blending: true,
            allow_advanced_blend_equation: false,
            allow_texture_swizzling: true,
            clear_caches_with_quads: true,
            dump_shader_source: None,
            surface_origin_is_top_left: false,
            compositor_config: CompositorConfig::default(),
            enable_gpu_markers: true,
            picture_tile_size: None,
            texture_cache_config: TextureCacheConfig::DEFAULT,
            // Disabling instancing means more vertex data to upload and potentially
            // process by the vertex shaders.
            enable_instancing: true,
            enable_shared_instance_buffer: false,
            reject_software_rasterizer: false,
            low_quality_pinch_zoom: false,
            max_shared_surface_size: 2048,
            enable_debugger: true,
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
    backend: GpuBackendConfig,
    notifier: Box<dyn RenderNotifier>,
    mut options: WebRenderOptions,
    shaders: Option<&SharedShaders>,
) -> Result<(Renderer, RenderApiSender), RendererError> {
    if !wr_has_been_initialized() {
        // If the tracy feature is enabled, try to load the shared library
        // if the path was provided.
        #[cfg(feature = "tracy")]
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
    // turn out to be the case, we can add a query method on the `Compositor` trait.
    match options.compositor_config {
        CompositorConfig::Draw { .. } | CompositorConfig::Native { .. } => {}
        CompositorConfig::Layer { ref compositor } => {
            if let SwapchainMode::OsLayers { .. } = compositor.get_swapchain_mode() {
                options.surface_origin_is_top_left = true;
            }
        }
    }

    // `api_tx` is obtained from the render backend pool further down; only
    // the result channel is created here, since the renderer owns its rx end.
    let (result_tx, result_rx) = unbounded_channel();

    let mut device = Device::new(
        backend,
        DeviceOptions {
            crash_annotator: options.crash_annotator.clone(),
            resource_override_path: options.resource_override_path.clone(),
            use_optimized_shaders: options.use_optimized_shaders,
            upload_method: options.upload_method.clone(),
            batched_upload_threshold: options.batched_upload_threshold,
            cached_programs: options.cached_programs.take(),
            allow_texture_swizzling: options.allow_texture_swizzling,
            dump_shader_source: options.dump_shader_source.take(),
            surface_origin_is_top_left: options.surface_origin_is_top_left,
        },
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
        device.get_capabilities().supports_advanced_blend_equation_coherent;

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
            let mut shaders = Shaders::new(&mut device, &options)?;
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

    // The shared instance buffer requires base_instance support to ensure
    // instance data can be read from the correct offset within the buffer.
    let use_shared_instance_buffer = options.enable_shared_instance_buffer
        && options.enable_instancing
        && device.get_capabilities().supports_base_instance;

    let vaos = vertex::RendererVAOs::new(
        &mut device,
        if options.enable_instancing { None } else { NonZeroUsize::new(max_primitive_instance_count) },
        use_shared_instance_buffer,
    );

    let texture_upload_buffer_pool = UploadBufferPool::new(&mut device, options.upload_buffer_default_size);
    let staging_texture_pool = UploadTexturePool::new();
    let texture_resolver = TextureResolver::new(&mut device);

    let is_software = device.get_capabilities().renderer_name.starts_with("Software");

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
        CompositorConfig::Draw { max_partial_present_rects, .. } => {
            CompositorKind::Draw { max_partial_present_rects }
        }
        CompositorConfig::Native { ref compositor } => {
            let capabilities = compositor.get_capabilities();

            CompositorKind::Native {
                capabilities,
            }
        }
        CompositorConfig::Layer { ref compositor } => {
            CompositorKind::Layer {
                mode: compositor.get_swapchain_mode(),
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
        external_images_require_copy: !device.get_capabilities().supports_external_textures_in_all_shaders,
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
        enable_dithering: options.enable_dithering,
        enable_yuv_overlay_stability: options.enable_yuv_overlay_stability,
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
    //
    // In shared-pool mode (`pref >= 1`) the caller supplies the pool's own
    // `SharedFontResources` via `options.shared_fonts`. Reusing it is
    // mandatory: every RenderApi assigned to the pool, and the pool's SB
    // threads, must observe the same font/instance maps so fonts registered
    // on one window are visible to scene building for any window.
    let fonts = if let Some(fonts) = options.shared_fonts.take() {
        fonts
    } else {
        let font_namespace = if namespace_alloc_by_client {
            options.shared_font_namespace.expect("Shared font namespace must be allocated by client")
        } else {
            RenderBackend::next_namespace_id()
        };
        SharedFontResources::new(font_namespace)
    };

    let blob_image_handler = options.blob_image_handler.take();
    let scene_builder_hooks = options.scene_builder_hooks.take();
    let mut render_backend_hooks = options.render_backend_hooks.take();

    let rb_blob_handler = blob_image_handler
        .as_ref()
        .map(|handler| handler.create_similar());

    let supports_r8_texture_upload = device.get_capabilities().supports_r8_texture_upload;

    let texture_cache_config = options.texture_cache_config.clone();
    let mut picture_tile_size = options.picture_tile_size.unwrap_or(crate::tile_cache::TILE_SIZE_DEFAULT);
    // Clamp the picture tile size to reasonable values.
    picture_tile_size.width = picture_tile_size.width.max(128).min(4096);
    picture_tile_size.height = picture_tile_size.height.max(128).min(4096);

    let picture_texture_filter = if options.low_quality_pinch_zoom {
        TextureFilter::Linear
    } else {
        TextureFilter::Nearest
    };

    let chunk_pool = options.chunk_pool.take().unwrap_or_else(|| {
        Arc::new(ChunkPool::new())
    });

    let enable_multithreading = options.enable_multithreading;
    let backend_id = next_render_backend_id();

    // Build the params for the per-window ResourceCache. The cache itself is
    // constructed on the render-backend thread (inside `register_window`) so
    // that its allocations are attributed to that thread.
    let resource_cache_init = ResourceCacheInit {
        max_internal_texture_size,
        image_tiling_threshold,
        color_cache_formats,
        swizzle_settings,
        texture_cache_config,
        picture_tile_size,
        picture_texture_filter,
        workers,
        dedicated_glyph_raster_thread: options.dedicated_glyph_raster_thread.take(),
        supports_r8_texture_upload,
        fonts: fonts.clone(),
        blob_image_handler: rb_blob_handler,
        enable_multithreading,
    };

    // Either use the caller-supplied pool or construct a private one of
    // size 1, which preserves the historical 1:1 backend-per-window model.
    let owned_pool = match options.render_backend_pool.take() {
        Some(pool) => pool,
        None => {
            let renderer_id_str = options.renderer_id.unwrap_or(0).to_string();
            let pool_fonts = fonts.clone();
            let pool_size_of = options.size_of_op;
            let pool_enclosing_size_of = options.enclosing_size_of_op;
            let support_lp = options.support_low_priority_transactions;
            let pool_config = config;
            let pool_namespace_alloc = namespace_alloc_by_client;
            RenderBackendPool::new(1, move |_idx| PoolMemberSetup {
                frame_builder_config: pool_config.clone(),
                fonts: pool_fonts.clone(),
                support_low_priority_transactions: support_lp,
                size_of_op: pool_size_of,
                enclosing_size_of_op: pool_enclosing_size_of,
                render_backend_hooks: render_backend_hooks.take(),
                namespace_alloc_by_client: pool_namespace_alloc,
                thread_name_suffix: renderer_id_str.clone(),
            })?
        }
    };

    let assigned = owned_pool.assign();
    let api_tx = assigned.api_tx;
    let scene_tx = assigned.scene_tx;
    let low_priority_scene_tx = assigned.lp_scene_tx;

    api_tx.send(ApiMsg::RegisterWindow(Box::new(WindowRegistration {
        id: backend_id,
        result_tx,
        notifier: backend_notifier,
        sampler,
        resource_cache: resource_cache_init,
        chunk_pool,
        frame_config: config,
        debug_flags,
    }))).expect("send RegisterWindow failed");

    // Register the per-window `FrameBuilderConfig` with the SB so that scene
    // building for documents owned by this window uses the right config
    // (notably `compositor_kind`). With a shared SB across windows this is
    // mandatory: each window can have a different config.
    let _ = scene_tx.send(SceneBuilderRequest::SetFrameBuilderConfig(
        backend_id,
        config.clone(),
    ));

    // Install the per-window scene-builder hooks on the SB. We send this
    // via the same scene channel that subsequently carries `AddDocument`
    // and transactions for this window's RenderApi, so the SB is
    // guaranteed to install the hooks before processing any work for
    // this window — no race between hook install and the first frame.
    if let Some(hooks) = scene_builder_hooks {
        let _ = scene_tx.send(SceneBuilderRequest::SetSceneBuilderHooks(
            backend_id,
            Some(hooks),
        ));
    }

    let gpu_profiler = device.create_gpu_profiler(options.enable_gpu_markers);

    let mut renderer = Renderer {
        result_rx,
        api_tx: api_tx.clone(),
        backend_id,
        _render_backend_pool: owned_pool.clone(),
        device,
        active_documents: FastHashMap::default(),
        pending_texture_updates: Vec::new(),
        pending_texture_cache_updates: false,
        pending_native_surface_updates: Vec::new(),
        pending_shader_updates: Vec::new(),
        shaders,
        debug: debug::LazyInitializedDebugRenderer::new(),
        debug_flags: DebugFlags::empty(),
        path_aa_samples: if cfg!(target_os = "android") { 4 } else { 8 },
        profile: TransactionProfile::new(),
        frame_counter: 0,
        resource_upload_time: 0.0,
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
        gpu_buffer_texture_f: None,
        gpu_buffer_texture_f_too_large: 0,
        gpu_buffer_last_data_f: Vec::new(),
        gpu_buffer_texture_i: None,
        gpu_buffer_texture_i_too_large: 0,
        gpu_buffer_last_data_i: Vec::new(),
        gpu_buffer_texture_edges: None,
        gpu_buffer_texture_edges_too_large: 0,
        gpu_buffer_last_data_edges: Vec::new(),
        vertex_data: vertex::VertexDataRing::new(),
        pipeline_info: PipelineInfo::default(),
        dither_matrix_texture,
        external_image_handler: None,
        size_of_ops: make_size_of_ops(),
        cpu_profiles: VecDeque::new(),
        gpu_profiles: VecDeque::new(),
        frame_build_profiles: VecDeque::new(),
        texture_upload_buffer_pool,
        staging_texture_pool,
        texture_resolver,
        renderer_errors: Vec::new(),
        async_frame_recorder: None,
        async_screenshots: None,
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
        layer_tree: LayerTree::new(),
        current_compositor_kind: compositor_kind,
        limit_sdr_yuv_external_composites: options.limit_sdr_yuv_external_composites,
        allocated_native_surfaces: FastHashSet::default(),
        debug_overlay_state: DebugOverlayState::new(),
        buffer_damage_tracker: BufferDamageTracker::default(),
        max_primitive_instance_count,
        enable_instancing: options.enable_instancing,
        use_shared_instance_buffer,
        consecutive_oom_frames: 0,
        target_frame_publish_id: None,
        pending_result_msg: None,
        layer_compositor_frame_state_in_prev_frame: None,
        external_composite_debug_items: Vec::new(),
        #[cfg(feature = "debugger")]
        renderdoc: crate::renderdoc::RenderDocCapture::new(),
        #[cfg(feature = "debugger")]
        renderdoc_capture_reply: None,
        command_log: None,
        #[cfg(feature = "debugger")]
        debugger: Debugger::new(),
    };

    // We initially set the flags to default and then now call set_debug_flags
    // to ensure any potential transition when enabling a flag is run.
    renderer.set_debug_flags(debug_flags);
    renderer.profiler.set_ui("Default");

    let sender = RenderApiSender::new(
        api_tx,
        scene_tx,
        low_priority_scene_tx,
        backend_id,
        blob_image_handler,
        fonts,
        owned_pool.clone(),
    );

    #[cfg(feature = "debugger")]
    if options.enable_debugger {
        let api = if namespace_alloc_by_client {
            sender.create_api_by_client(IdNamespace::DEBUGGER)
        } else {
            sender.create_api()
        };
        crate::debugger::start(api);
    }

    Ok((renderer, sender))
}
