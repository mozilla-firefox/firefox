/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#[macro_use]
extern crate clap;
#[macro_use]
extern crate log;
#[macro_use]
extern crate serde;
#[macro_use]
extern crate tracy_rs;

/// App units per device pixel that wrench declares for its display lists.
/// WebRender normalizes items by their accumulated external scroll offset in whole
/// app units on this grid, so declaring the same value Gecko uses at dpr 1.0 means
/// wrench exercises the same arithmetic rather than a bypass. Yaml coordinates are
/// therefore quantized to 1/60 px (bug 2059570).
pub const AU_PER_DEV_PX: f32 = 60.0;

mod angle;
mod bench;
mod blob;
#[cfg(target_os = "windows")]
mod composite;
mod egl;
#[cfg(not(target_os = "windows"))]
mod null_compositor;
mod parse_function;
mod perf;
mod png;
mod premultiply;
mod rawtest;
mod rawtests;
mod reftest;
mod test_invalidation;
mod test_shaders;
mod wrench;
mod yaml_frame_reader;
mod yaml_helper;

#[cfg(target_os = "windows")]
use composite::WrCompositor;
#[cfg(not(target_os = "windows"))]
use null_compositor::NullLayerCompositor;
use gleam::gl;
#[cfg(feature = "software")]
use gleam::gl::Gl;
use glutin::config::{ConfigTemplateBuilder, GlConfig};
use glutin::context::{ContextApi, ContextAttributesBuilder, NotCurrentGlContext, PossiblyCurrentContext, Version};
use glutin::display::{GetGlDisplay, GlDisplay};
use glutin::surface::{GlSurface, Surface, SurfaceAttributesBuilder, SwapInterval, WindowSurface};
use glutin_winit::DisplayBuilder;
use crate::bench::{BenchHarness, BenchInput, BenchMode, BenchOptions};
use crate::perf::PerfHarness;
use crate::rawtest::RawtestHarness;
use crate::reftest::{ReftestHarness, ReftestOptions};
#[cfg(feature = "headless")]
use std::ffi::CString as HeadlessCString;
#[cfg(feature = "headless")]
use std::mem;
use std::ffi::CString;
use std::num::NonZeroU32;
use std::os::raw::c_void;
use std::path::{Path, PathBuf};
use std::process;
use std::ptr;
use std::rc::Rc;
#[cfg(feature = "software")]
use std::slice;
use std::sync::mpsc::{channel, Sender, Receiver};
use webrender::{DebugFlags, Compositor};
use webrender::api::*;
use webrender::render_api::*;
use webrender::api::units::*;
use winit::application::ApplicationHandler;
use winit::dpi::{LogicalPosition, LogicalSize};
use winit::event::{ElementState, KeyEvent, WindowEvent};
use winit::event_loop::{ActiveEventLoop, ControlFlow, EventLoop, EventLoopProxy};
use winit::keyboard::{Key, NamedKey};
use winit::raw_window_handle::{HasWindowHandle, RawWindowHandle};
use winit::window::{Window, WindowId};
use crate::wrench::{CapturedSequence, Wrench, WrenchThing};
use crate::yaml_frame_reader::YamlFrameReader;
#[cfg(target_os = "android")]
use android_activity::AndroidApp;

pub const PLATFORM_DEFAULT_FACE_NAME: &str = "Arial";

pub static mut CURRENT_FRAME_NUMBER: u32 = 0;

/// GL API request — replaces the removed glutin::GlRequest.
#[derive(Debug, Clone, Copy)]
pub enum GlApiRequest {
    OpenGl { major: u8, minor: u8 },
    OpenGlEs { major: u8, minor: u8 },
    GlThenGles { opengl_version: (u8, u8), opengles_version: (u8, u8) },
}

#[cfg(feature = "headless")]
pub struct HeadlessContext {
    width: i32,
    height: i32,
    _context: osmesa_sys::OSMesaContext,
    _buffer: Vec<u32>,
}

#[cfg(not(feature = "headless"))]
pub struct HeadlessContext {
    width: i32,
    height: i32,
}

impl HeadlessContext {
    #[cfg(feature = "headless")]
    fn new(width: i32, height: i32) -> Self {
        let mut attribs = Vec::new();

        attribs.push(osmesa_sys::OSMESA_PROFILE);
        attribs.push(osmesa_sys::OSMESA_CORE_PROFILE);
        attribs.push(osmesa_sys::OSMESA_CONTEXT_MAJOR_VERSION);
        attribs.push(3);
        attribs.push(osmesa_sys::OSMESA_CONTEXT_MINOR_VERSION);
        attribs.push(3);
        attribs.push(osmesa_sys::OSMESA_DEPTH_BITS);
        attribs.push(24);
        attribs.push(0);

        let context =
            unsafe { osmesa_sys::OSMesaCreateContextAttribs(attribs.as_ptr(), ptr::null_mut()) };

        assert!(!context.is_null());

        let mut buffer = vec![0; (width * height) as usize];

        unsafe {
            let ret = osmesa_sys::OSMesaMakeCurrent(
                context,
                buffer.as_mut_ptr() as *mut _,
                gl::UNSIGNED_BYTE,
                width,
                height,
            );
            assert!(ret != 0);
        };

        HeadlessContext {
            width,
            height,
            _context: context,
            _buffer: buffer,
        }
    }

    #[cfg(not(feature = "headless"))]
    fn new(width: i32, height: i32) -> Self {
        HeadlessContext { width, height }
    }

    #[cfg(feature = "headless")]
    fn get_proc_address(s: &str) -> *const c_void {
        let c_str = HeadlessCString::new(s).expect("Unable to create CString");
        unsafe { mem::transmute(osmesa_sys::OSMesaGetProcAddress(c_str.as_ptr())) }
    }

    #[cfg(not(feature = "headless"))]
    fn get_proc_address(_: &str) -> *const c_void {
        ptr::null() as *const _
    }
}

#[cfg(not(feature = "software"))]
mod swgl {
    pub struct Context;
}

pub enum WindowWrapper {
    Windowed {
        window: Window,
        gl_surface: Surface<WindowSurface>,
        gl_context: PossiblyCurrentContext,
        is_gles: bool,
        gl: Rc<dyn gl::Gl>,
        sw_ctx: Option<swgl::Context>,
    },
    Angle {
        window: Window,
        context: angle::Context,
        gl: Rc<dyn gl::Gl>,
        sw_ctx: Option<swgl::Context>,
    },
    Headless {
        context: HeadlessContext,
        gl: Rc<dyn gl::Gl>,
        sw_ctx: Option<swgl::Context>,
    },
}

pub struct HeadlessEventIterater;

impl WindowWrapper {
    #[cfg(feature = "software")]
    fn upload_software_to_native(&self) {
        if matches!(self, WindowWrapper::Headless { .. }) { return }
        let swgl = match self.software_gl() {
            Some(swgl) => swgl,
            None => return,
        };
        swgl.finish();
        let gl = self.native_gl();
        let tex = gl.gen_textures(1)[0];
        gl.bind_texture(gl::TEXTURE_2D, tex);
        let (data_ptr, w, h, stride) = swgl.get_color_buffer(0, true);
        assert!(stride == w * 4);
        let buffer = unsafe { slice::from_raw_parts(data_ptr as *const u8, w as usize * h as usize * 4) };
        gl.tex_image_2d(gl::TEXTURE_2D, 0, gl::RGBA8 as gl::GLint, w, h, 0, gl::BGRA, gl::UNSIGNED_BYTE, Some(buffer));
        let fb = gl.gen_framebuffers(1)[0];
        gl.bind_framebuffer(gl::READ_FRAMEBUFFER, fb);
        gl.framebuffer_texture_2d(gl::READ_FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, tex, 0);
        gl.blit_framebuffer(0, 0, w, h, 0, 0, w, h, gl::COLOR_BUFFER_BIT, gl::NEAREST);
        gl.delete_framebuffers(&[fb]);
        gl.delete_textures(&[tex]);
        gl.finish();
    }

    #[cfg(not(feature = "software"))]
    fn upload_software_to_native(&self) {
    }

    fn swap_buffers(&self) {
        match self {
            WindowWrapper::Windowed { gl_surface, gl_context, .. } => {
                gl_surface.swap_buffers(gl_context).unwrap()
            }
            WindowWrapper::Angle { context, .. } => context.swap_buffers().unwrap(),
            WindowWrapper::Headless { .. } => {}
        }
    }

    fn get_inner_size(&self) -> DeviceIntSize {
        fn inner_size(window: &Window) -> DeviceIntSize {
            let size = window.inner_size();
            DeviceIntSize::new(size.width as i32, size.height as i32)
        }
        match self {
            WindowWrapper::Windowed { window, .. } => inner_size(window),
            WindowWrapper::Angle { window, .. } => inner_size(window),
            WindowWrapper::Headless { context, .. } => DeviceIntSize::new(context.width, context.height),
        }
    }

    fn hidpi_factor(&self) -> f32 {
        match self {
            WindowWrapper::Windowed { window, .. } => window.scale_factor() as f32,
            WindowWrapper::Angle { window, .. } => window.scale_factor() as f32,
            WindowWrapper::Headless { .. } => 1.0,
        }
    }

    fn resize(&mut self, size: DeviceIntSize) {
        match self {
            WindowWrapper::Windowed { window, .. } => {
                let _ = window.request_inner_size(LogicalSize::new(size.width as f64, size.height as f64));
            },
            WindowWrapper::Angle { window, .. } => {
                let _ = window.request_inner_size(LogicalSize::new(size.width as f64, size.height as f64));
            },
            WindowWrapper::Headless { .. } => unimplemented!(),
        }
    }

    fn set_title(&mut self, title: &str) {
        match self {
            WindowWrapper::Windowed { window, .. } => window.set_title(title),
            WindowWrapper::Angle { window, .. } => window.set_title(title),
            WindowWrapper::Headless { .. } => (),
        }
    }

    pub fn software_gl(&self) -> Option<&swgl::Context> {
        match self {
            WindowWrapper::Windowed { sw_ctx, .. } |
            WindowWrapper::Angle { sw_ctx, .. } |
            WindowWrapper::Headless { sw_ctx, .. } => sw_ctx.as_ref(),
        }
    }

    pub fn native_gl(&self) -> &dyn gl::Gl {
        match self {
            WindowWrapper::Windowed { gl, .. } |
            WindowWrapper::Angle { gl, .. } |
            WindowWrapper::Headless { gl, .. } => &**gl,
        }
    }

    #[cfg(feature = "software")]
    pub fn gl(&self) -> &dyn gl::Gl {
        if let Some(swgl) = self.software_gl() {
            swgl
        } else {
            self.native_gl()
        }
    }

    pub fn is_software(&self) -> bool {
        self.software_gl().is_some()
    }

    #[cfg(not(feature = "software"))]
    pub fn gl(&self) -> &dyn gl::Gl {
        self.native_gl()
    }

    pub fn clone_gl(&self) -> Rc<dyn gl::Gl> {
        match self {
            WindowWrapper::Windowed { gl, sw_ctx, .. } |
            WindowWrapper::Angle { gl, sw_ctx, .. } |
            WindowWrapper::Headless { gl, sw_ctx, .. } => {
                match sw_ctx {
                    #[cfg(feature = "software")]
                    Some(ref swgl) => Rc::new(*swgl),
                    None => gl.clone(),
                    #[cfg(not(feature = "software"))]
                    _ => panic!(),
                }
            }
        }
    }

    #[cfg(feature = "software")]
    fn update_software(&self, dim: DeviceIntSize) {
        if let Some(swgl) = self.software_gl() {
            swgl.init_default_framebuffer(0, 0, dim.width, dim.height, 0, std::ptr::null_mut());
        }
    }

    #[cfg(not(feature = "software"))]
    fn update_software(&self, _dim: DeviceIntSize) {
    }

    fn update(&self, wrench: &mut Wrench) {
        let dim = self.get_inner_size();
        self.update_software(dim);
        wrench.update(dim);
    }

    #[cfg(target_os = "windows")]
    pub fn get_d3d11_device(&self) -> *const c_void {
        match self {
            WindowWrapper::Windowed { .. } |
            WindowWrapper::Headless { .. } => unreachable!(),
            WindowWrapper::Angle { context, .. } => context.get_d3d11_device(),
        }
    }

    #[cfg(target_os = "windows")]
    pub fn create_compositor(&self) -> Option<Box<dyn Compositor>> {
        Some(Box::new(WrCompositor::new(self)) as Box<dyn Compositor>)
    }

    #[cfg(not(target_os = "windows"))]
    pub fn create_compositor(&self) -> Option<Box<dyn Compositor>> {
        Some(Box::new(NullLayerCompositor) as Box<dyn Compositor>)
    }
}

#[cfg(feature = "software")]
fn make_software_context() -> swgl::Context {
    let ctx = swgl::Context::create();
    ctx.make_current();
    ctx
}

#[cfg(not(feature = "software"))]
fn make_software_context() -> swgl::Context {
    panic!("software feature not enabled")
}

/// Create a windowed GL context using glutin 0.32 + glutin-winit.
/// For GlThenGles, use make_window_with_fallback instead.
fn make_window(
    event_loop: &ActiveEventLoop,
    size: DeviceIntSize,
    vsync: bool,
    gl_request: GlApiRequest,
    software: bool,
) -> WindowWrapper {
    let sw_ctx = if software { Some(make_software_context()) } else { None };

    let window_attrs = Window::default_attributes()
        .with_title("WRench")
        .with_inner_size(LogicalSize::new(size.width as f64, size.height as f64));

    let template = ConfigTemplateBuilder::new().with_depth_size(24);

    let (window, gl_config) = DisplayBuilder::new()
        .with_window_attributes(Some(window_attrs))
        .build(event_loop, template, |configs| {
            configs.reduce(|acc, config| {
                if config.num_samples() > acc.num_samples() { config } else { acc }
            }).unwrap()
        })
        .expect("failed to create GL display and window");

    let window = window.unwrap();
    let gl_display = gl_config.display();

    let raw_window_handle: RawWindowHandle = window
        .window_handle()
        .expect("failed to get window handle")
        .as_raw();

    let (context_attrs, is_gles) = build_context_attrs(gl_request, Some(raw_window_handle));

    let not_current_ctx = unsafe {
        gl_display
            .create_context(&gl_config, &context_attrs)
            .expect("failed to create GL context")
    };

    let physical_size = window.inner_size();
    let surface_attrs = SurfaceAttributesBuilder::<WindowSurface>::new()
        .build(
            raw_window_handle,
            NonZeroU32::new(physical_size.width.max(1)).unwrap(),
            NonZeroU32::new(physical_size.height.max(1)).unwrap(),
        );

    let gl_surface = unsafe {
        gl_display
            .create_window_surface(&gl_config, &surface_attrs)
            .expect("failed to create GL surface")
    };

    let gl_context = not_current_ctx
        .make_current(&gl_surface)
        .expect("failed to make GL context current");

    // vsync — Android always enables it; without it context creation may fail (bug 1928322).
    if vsync || cfg!(target_os = "android") {
        gl_surface
            .set_swap_interval(&gl_context, SwapInterval::Wait(NonZeroU32::new(1).unwrap()))
            .ok();
    }

    let gl: Rc<dyn gl::Gl> = load_gl_fns(is_gles, &gl_display);

    WindowWrapper::Windowed { window, gl_surface, gl_context, is_gles, gl, sw_ctx }
}

/// Like make_window but handles GlThenGles fallback explicitly.
fn make_window_with_fallback(
    event_loop: &ActiveEventLoop,
    size: DeviceIntSize,
    vsync: bool,
    gl_request: GlApiRequest,
    software: bool,
) -> WindowWrapper {
    let GlApiRequest::GlThenGles {
        opengl_version: (gl_maj, gl_min),
        opengles_version: (gles_maj, gles_min),
    } = gl_request else {
        return make_window(event_loop, size, vsync, gl_request, software);
    };

    let sw_ctx = if software { Some(make_software_context()) } else { None };

    let window_attrs = Window::default_attributes()
        .with_title("WRench")
        .with_inner_size(LogicalSize::new(size.width as f64, size.height as f64));

    let template = ConfigTemplateBuilder::new().with_depth_size(24);

    let (window, gl_config) = DisplayBuilder::new()
        .with_window_attributes(Some(window_attrs))
        .build(event_loop, template, |configs| {
            configs.reduce(|acc, c| if c.num_samples() > acc.num_samples() { c } else { acc }).unwrap()
        })
        .expect("failed to create GL display and window");

    let window = window.unwrap();
    let gl_display = gl_config.display();

    let raw_window_handle: RawWindowHandle = window
        .window_handle()
        .expect("failed to get window handle")
        .as_raw();

    // Try OpenGL first, fall back to GLES.
    let (not_current_ctx, is_gles) = {
        let gl_attrs = ContextAttributesBuilder::new()
            .with_context_api(ContextApi::OpenGl(Some(Version::new(gl_maj, gl_min))))
            .build(Some(raw_window_handle));
        match unsafe { gl_display.create_context(&gl_config, &gl_attrs) } {
            Ok(ctx) => (ctx, false),
            Err(_) => {
                let gles_attrs = ContextAttributesBuilder::new()
                    .with_context_api(ContextApi::Gles(Some(Version::new(gles_maj, gles_min))))
                    .build(Some(raw_window_handle));
                let ctx = unsafe { gl_display.create_context(&gl_config, &gles_attrs) }
                    .expect("failed to create GLES context as GlThenGles fallback");
                (ctx, true)
            }
        }
    };

    let physical_size = window.inner_size();
    let surface_attrs = SurfaceAttributesBuilder::<WindowSurface>::new()
        .build(
            raw_window_handle,
            NonZeroU32::new(physical_size.width.max(1)).unwrap(),
            NonZeroU32::new(physical_size.height.max(1)).unwrap(),
        );

    let gl_surface = unsafe {
        gl_display
            .create_window_surface(&gl_config, &surface_attrs)
            .expect("failed to create GL surface")
    };

    let gl_context = not_current_ctx
        .make_current(&gl_surface)
        .expect("failed to make GL context current");

    if vsync || cfg!(target_os = "android") {
        gl_surface
            .set_swap_interval(&gl_context, SwapInterval::Wait(NonZeroU32::new(1).unwrap()))
            .ok();
    }

    let gl: Rc<dyn gl::Gl> = load_gl_fns(is_gles, &gl_display);

    WindowWrapper::Windowed { window, gl_surface, gl_context, is_gles, gl, sw_ctx }
}

fn build_context_attrs(
    gl_request: GlApiRequest,
    raw_window_handle: Option<RawWindowHandle>,
) -> (glutin::context::ContextAttributes, bool) {
    match gl_request {
        GlApiRequest::OpenGl { major, minor } => {
            let attrs = ContextAttributesBuilder::new()
                .with_context_api(ContextApi::OpenGl(Some(Version::new(major, minor))))
                .build(raw_window_handle);
            (attrs, false)
        }
        GlApiRequest::OpenGlEs { major, minor } => {
            let attrs = ContextAttributesBuilder::new()
                .with_context_api(ContextApi::Gles(Some(Version::new(major, minor))))
                .build(raw_window_handle);
            (attrs, true)
        }
        GlApiRequest::GlThenGles { opengl_version: (gl_maj, gl_min), .. } => {
            // GlThenGles is handled by make_window_with_fallback; here we
            // just build the OpenGL attrs as the primary attempt.
            let attrs = ContextAttributesBuilder::new()
                .with_context_api(ContextApi::OpenGl(Some(Version::new(gl_maj, gl_min))))
                .build(raw_window_handle);
            (attrs, false)
        }
    }
}

fn load_gl_fns(is_gles: bool, gl_display: &impl GlDisplay) -> Rc<dyn gl::Gl> {
    if is_gles {
        unsafe {
            gl::GlesFns::load_with(|symbol| {
                gl_display.get_proc_address(&CString::new(symbol).unwrap()) as *const _
            })
        }
    } else {
        unsafe {
            gl::GlFns::load_with(|symbol| {
                gl_display.get_proc_address(&CString::new(symbol).unwrap()) as *const _
            })
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum NotifierEvent {
    WakeUp {
        composite_needed: bool,
    },
    ShutDown,
}

struct Notifier {
    tx: Sender<NotifierEvent>,
}

impl RenderNotifier for Notifier {
    fn clone(&self) -> Box<dyn RenderNotifier> {
        Box::new(Notifier { tx: self.tx.clone() })
    }

    // Both sends can race with teardown: subcommands that end with
    // renderer.deinit() rather than Wrench::shut_down() drop the receiver
    // before the render backend thread notifies its last events. A consumer
    // that has gone away is not an error, so don't unwrap.
    fn wake_up(&self, composite_needed: bool) {
        let _ = self.tx.send(NotifierEvent::WakeUp { composite_needed });
    }

    fn shut_down(&self) {
        let _ = self.tx.send(NotifierEvent::ShutDown);
    }

    fn new_frame_ready(&self, _: DocumentId, _: FramePublishId, params: &FrameReadyParams) {
        self.wake_up(params.render);
    }
}

fn create_notifier() -> (Box<dyn RenderNotifier>, Receiver<NotifierEvent>) {
    let (tx, rx) = channel();
    (Box::new(Notifier { tx }), rx)
}

fn rawtest(mut wrench: Wrench, window: &mut WindowWrapper, rx: Receiver<NotifierEvent>) {
    RawtestHarness::new(&mut wrench, window, &rx).run();
    wrench.shut_down(rx);
}

fn reftest(
    mut wrench: Wrench,
    window: &mut WindowWrapper,
    specific_reftest: Option<&Path>,
    fuzz_tolerance: Option<f64>,
    rx: Receiver<NotifierEvent>,
) -> usize {
    let dim = window.get_inner_size();
    #[cfg(target_os = "android")]
    let base_manifest = {
        let mut list_path = ANDROID_APP.get().unwrap().internal_data_path().unwrap();
        list_path.push("wrench");
        list_path.push("reftests");
        list_path.push("reftest.list");
        list_path
    };
    #[cfg(not(target_os = "android"))]
    let base_manifest = Path::new("reftests/reftest.list").to_owned();

    let mut reftest_options = ReftestOptions::default();
    if let Some(allow_max_diff) = fuzz_tolerance {
        reftest_options.allow_max_difference = allow_max_diff as usize;
        reftest_options.allow_num_differences = dim.width as usize * dim.height as usize;
    }
    let num_failures = ReftestHarness::new(&mut wrench, window, &rx)
        .run(&base_manifest, specific_reftest, &reftest_options);
    wrench.shut_down(rx);
    num_failures
}

/// State for interactive show mode.
struct ShowState {
    thing: Box<dyn WrenchThing>,
    no_block: bool,
    debug_flags: DebugFlags,
    show_help: bool,
    do_loop: bool,
    do_render: bool,
    do_frame: bool,
    cursor_position: WorldPoint,
}

/// Describes the WrenchThing to build in resumed(). The LoadCapture variant
/// needs a live Wrench to execute, so it cannot be built in build_app().
enum ThingToBuild {
    Yaml(YamlFrameReader),
    Ready(Box<dyn WrenchThing>),
    LoadCapture(PathBuf),
}

#[cfg(target_os = "android")]
static ANDROID_APP: std::sync::OnceLock<AndroidApp> = std::sync::OnceLock::new();

struct WrenchApp {
    // -- Config --
    size: DeviceIntSize,
    vsync: bool,
    angle: bool,
    software: bool,
    sw_compositor: bool,
    using_compositor: bool,
    gl_request: GlApiRequest,
    headless: bool,
    res_path: Option<PathBuf>,
    use_optimized_shaders: bool,
    rebuild: bool,
    no_subpixel_aa: bool,
    verbose: bool,
    no_scissor: bool,
    no_batch_global: bool,
    color_target_init: bool,
    precache: bool,
    dump_shader_source: Option<String>,
    profiler_ui: Option<String>,

    // -- Subcommand --
    subcommand: String,

    /// Set by `--compositor-clips`, overriding the default of each subcommand.
    compositor_clips: Option<bool>,

    // Reftest
    reftest_specific: Option<PathBuf>,
    reftest_fuzz: Option<f64>,

    // Show
    thing_to_build: Option<ThingToBuild>,
    show_no_block: bool,
    show_no_batch: bool,

    // Png
    png_reader: Option<YamlFrameReader>,
    png_surface: png::ReadSurface,
    png_output_path: Option<PathBuf>,

    // Perf
    perf_benchmark: String,
    perf_filename: String,
    perf_as_csv: bool,
    perf_warmup_frames: Option<usize>,
    perf_sample_count: Option<usize>,

    // Bench
    bench: Option<(BenchInput, BenchOptions)>,

    // ComparePerf
    compare_first: String,
    compare_second: String,

    // -- Proxy (created from EventLoop before run_app; None in headless) --
    proxy: Option<EventLoopProxy<()>>,

    // -- Created in resumed() --
    window: Option<WindowWrapper>,
    wrench: Option<Wrench>,
    rx: Option<Receiver<NotifierEvent>>,

    // -- Interactive show mode --
    show_state: Option<ShowState>,

    // -- Exit code --
    exit_code: i32,
}

impl ApplicationHandler for WrenchApp {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.window.is_some() {
            return;
        }

        // Create window / GL context.
        let mut window = if self.headless {
            let sw_ctx = if self.software { Some(make_software_context()) } else { None };
            #[cfg_attr(not(feature = "software"), allow(unused_variables))]
            let gl: Rc<dyn gl::Gl> = if let Some(ref sw_ctx) = sw_ctx {
                #[cfg(feature = "software")]
                { Rc::new(*sw_ctx) }
                #[cfg(not(feature = "software"))]
                { unreachable!() }
            } else {
                match gl::GlType::default() {
                    gl::GlType::Gl => unsafe {
                        gl::GlFns::load_with(|s| HeadlessContext::get_proc_address(s) as *const _)
                    },
                    gl::GlType::Gles => unsafe {
                        gl::GlesFns::load_with(|s| HeadlessContext::get_proc_address(s) as *const _)
                    },
                }
            };
            WindowWrapper::Headless {
                context: HeadlessContext::new(self.size.width, self.size.height),
                gl,
                sw_ctx,
            }
        } else if self.angle {
            // Angle is only supported on Windows. The non-Windows stub for
            // angle::Context::with_window always returns Err, so guard this
            // path at compile time to avoid an unreachable-expression warning.
            #[cfg(not(target_os = "windows"))]
            panic!("--angle is only supported on Windows");

            #[cfg(target_os = "windows")]
            {
                let window_attrs = Window::default_attributes()
                    .with_title("WRench")
                    .with_inner_size(LogicalSize::new(self.size.width as f64, self.size.height as f64));

                let (win, ctx) = angle::Context::with_window(
                    window_attrs,
                    &self.gl_request,
                    event_loop,
                    self.using_compositor,
                ).expect("failed to create ANGLE context");

                unsafe { ctx.make_current().expect("unable to make ANGLE context current"); }

                let gl: Rc<dyn gl::Gl> = if ctx.is_gles() {
                    unsafe { gl::GlesFns::load_with(|s| ctx.get_proc_address(s) as *const _) }
                } else {
                    unsafe { gl::GlFns::load_with(|s| ctx.get_proc_address(s) as *const _) }
                };

                let sw_ctx = if self.software { Some(make_software_context()) } else { None };
                WindowWrapper::Angle { window: win, context: ctx, gl, sw_ctx }
            }
        } else {
            make_window_with_fallback(event_loop, self.size, self.vsync, self.gl_request, self.software)
        };

        let gl = window.gl();
        gl.clear_color(0.3, 0.0, 0.0, 1.0);
        println!("OpenGL version {}, {}", gl.get_string(gl::VERSION), gl.get_string(gl::RENDERER));
        println!("hidpi factor: {}", window.hidpi_factor());

        let init_target_dbg = self.color_target_init || matches!(
            self.subcommand.as_str(),
            "reftest"
        );

        let needs_frame_notifier = matches!(
            self.subcommand.as_str(),
            "perf" | "bench" | "reftest" | "png" | "rawtest" | "test_invalidation"
        );
        let (notifier, rx) = if needs_frame_notifier {
            let (n, r) = create_notifier();
            (Some(n), Some(r))
        } else {
            (None, None)
        };

        let layer_compositor = if self.using_compositor {
            window.create_compositor()
        } else {
            None
        };

        let dim = window.get_inner_size();
        let mut wrench = Wrench::new(
            &mut window,
            self.proxy.clone(),
            self.res_path.clone(),
            self.use_optimized_shaders,
            dim,
            self.rebuild,
            self.no_subpixel_aa,
            self.verbose,
            self.no_scissor,
            self.no_batch_global,
            init_target_dbg,
            self.precache,
            self.dump_shader_source.clone(),
            notifier,
            layer_compositor,
            self.sw_compositor,
            self.compositor_clips,
        );

        if let Some(ui_str) = &self.profiler_ui {
            wrench.renderer.set_profiler_ui(ui_str);
        }

        window.update(&mut wrench);

        if let Some(window_title) = wrench.take_title() {
            if !cfg!(windows) {
                window.set_title(&window_title);
            }
        }

        match self.subcommand.as_str() {
            "show" => {
                // Displaying a yaml file mirrors what reftests do, so like
                // reftests it exercises the quad-shader clip path rather than
                // the compositor clip fast path. Recordings and captures are
                // replayed as-is.
                let mut is_yaml = false;
                let thing: Box<dyn WrenchThing> = match self.thing_to_build.take() {
                    Some(ThingToBuild::Yaml(reader)) => {
                        is_yaml = true;
                        Box::new(reader) as Box<dyn WrenchThing>
                    }
                    Some(ThingToBuild::Ready(t)) => t,
                    Some(ThingToBuild::LoadCapture(path)) => {
                        let mut documents = wrench.api.load_capture(path, None);
                        println!("loaded {:?}", documents.iter().map(|cd| cd.document_id).collect::<Vec<_>>());
                        let captured = documents.swap_remove(0);
                        wrench.document_id = captured.document_id;
                        Box::new(captured) as Box<dyn WrenchThing>
                    }
                    None => panic!("no thing_to_build for show command"),
                };

                let mut debug_flags = DebugFlags::empty();
                debug_flags.set(DebugFlags::DISABLE_BATCHING, self.show_no_batch);
                let compositor_clips = self.compositor_clips.unwrap_or(!is_yaml);
                debug_flags.set(DebugFlags::DISABLE_COMPOSITOR_CLIPS, !compositor_clips);
                wrench.set_compositor_clips_enabled(compositor_clips);

                if cfg!(target_os = "android") {
                    debug_flags.toggle(DebugFlags::PROFILER_DBG);
                    wrench.api.send_debug_cmd(DebugCommand::SetFlags(debug_flags));
                }

                // Submit the first frame before the event loop starts driving
                // rendering, mirroring what the old render() function did.
                // Without this, the first user_event() from Wrench::new()'s
                // initial proxy send fires before any content is queued, and
                // wrench.render() presents a transparent frame.
                let mut thing = thing;
                thing.do_frame(&mut wrench);
                if let Some(fb_size) = wrench.renderer.device_size() {
                    window.resize(fb_size);
                }

                self.show_state = Some(ShowState {
                    thing,
                    no_block: self.show_no_block,
                    debug_flags,
                    show_help: false,
                    do_loop: false,
                    do_render: false,
                    do_frame: false,
                    cursor_position: WorldPoint::zero(),
                });

                self.window = Some(window);
                self.wrench = Some(wrench);
                self.rx = rx;
            }
            "png" => {
                let reader = self.png_reader.take().unwrap();
                let rx = rx.unwrap();
                png::png(&mut wrench, self.png_surface, &mut window, reader, &rx, self.png_output_path.clone());
                // shut_down() keeps the receiver alive until the render backend
                // has acknowledged shutdown, which deinit() alone does not.
                wrench.shut_down(rx);
                event_loop.exit();
            }
            "reftest" => {
                let num_failures = reftest(
                    wrench,
                    &mut window,
                    self.reftest_specific.as_deref(),
                    self.reftest_fuzz,
                    rx.unwrap(),
                );
                self.exit_code = num_failures as i32;
                event_loop.exit();
            }
            "rawtest" => {
                rawtest(wrench, &mut window, rx.unwrap());
                event_loop.exit();
            }
            "perf" => {
                wrench.rebuild_display_lists = true;
                let rx = rx.unwrap();
                let harness = PerfHarness::new(
                    &mut wrench,
                    &mut window,
                    &rx,
                    self.perf_warmup_frames,
                    self.perf_sample_count,
                );
                let base_manifest = Path::new(&self.perf_benchmark);
                harness.run(base_manifest, &self.perf_filename, self.perf_as_csv);
                wrench.shut_down(rx);
                event_loop.exit();
            }
            "bench" => {
                let rx = rx.unwrap();
                let (input, options) = self.bench.take().unwrap();
                BenchHarness::new(&mut wrench, &mut window, &rx, options).run(input);
                wrench.shut_down(rx);
                event_loop.exit();
            }
            "test_invalidation" => {
                let rx = rx.unwrap();
                let harness = test_invalidation::TestHarness::new(&mut wrench, &mut window, &rx);
                let num_failures = harness.run();
                wrench.shut_down(rx);
                self.exit_code = num_failures as i32;
                event_loop.exit();
            }
            "compare_perf" => {
                wrench.renderer.deinit();
                perf::compare(&self.compare_first, &self.compare_second);
                event_loop.exit();
            }
            "test_init" => {
                println!("Initialization successful");
                wrench.renderer.deinit();
                event_loop.exit();
            }
            "test_shaders" => {
                wrench.renderer.deinit();
                test_shaders::test_shaders();
                event_loop.exit();
            }
            other => panic!("Unknown subcommand: {:?}", other),
        }
    }

    fn window_event(
        &mut self,
        event_loop: &ActiveEventLoop,
        _window_id: WindowId,
        event: WindowEvent,
    ) {
        let Some(ref mut show) = self.show_state else { return };
        let Some(ref mut wrench) = self.wrench else { return };

        match event {
            WindowEvent::CloseRequested => event_loop.exit(),
            WindowEvent::Focused(..) => show.do_render = true,
            WindowEvent::CursorMoved { position, .. } => {
                if let Some(ref window) = self.window {
                    let pos: LogicalPosition<f32> =
                        position.to_logical(window.hidpi_factor() as f64);
                    show.cursor_position = WorldPoint::new(pos.x, pos.y);
                    wrench.renderer.set_cursor_position(DeviceIntPoint::new(
                        show.cursor_position.x.round() as i32,
                        show.cursor_position.y.round() as i32,
                    ));
                    show.do_render = true;
                }
            }
            WindowEvent::KeyboardInput {
                event: KeyEvent {
                    state: ElementState::Pressed,
                    logical_key: ref key,
                    ..
                },
                ..
            } => {
                let key = key.clone();
                match key.as_ref() {
                    Key::Named(NamedKey::Escape) => event_loop.exit(),
                    Key::Character("b") | Key::Character("B") => {
                        show.debug_flags.toggle(DebugFlags::INVALIDATION_DBG);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(show.debug_flags));
                        show.do_render = true;
                    }
                    Key::Character("p") | Key::Character("P") => {
                        show.debug_flags.toggle(DebugFlags::PROFILER_DBG);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(show.debug_flags));
                        show.do_render = true;
                    }
                    Key::Character("o") | Key::Character("O") => {
                        show.debug_flags.toggle(DebugFlags::RENDER_TARGET_DBG);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(show.debug_flags));
                        show.do_render = true;
                    }
                    Key::Character("i") | Key::Character("I") => {
                        show.debug_flags.toggle(DebugFlags::TEXTURE_CACHE_DBG);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(show.debug_flags));
                        show.do_render = true;
                    }
                    Key::Character("d") | Key::Character("D") => {
                        show.debug_flags.toggle(DebugFlags::PICTURE_CACHING_DBG);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(show.debug_flags));
                        show.do_render = true;
                    }
                    Key::Character("f") | Key::Character("F") => {
                        show.debug_flags.toggle(DebugFlags::PICTURE_BORDERS);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(show.debug_flags));
                        show.do_render = true;
                    }
                    Key::Character("q") | Key::Character("Q") => {
                        show.debug_flags
                            .toggle(DebugFlags::GPU_TIME_QUERIES | DebugFlags::GPU_SAMPLE_QUERIES);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(show.debug_flags));
                        show.do_render = true;
                    }
                    Key::Character("v") | Key::Character("V") => {
                        show.debug_flags.toggle(DebugFlags::SHOW_OVERDRAW);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(show.debug_flags));
                        show.do_render = true;
                    }
                    Key::Character("g") | Key::Character("G") => {
                        show.debug_flags.toggle(DebugFlags::GPU_CACHE_DBG);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(show.debug_flags));
                        let mut txn = Transaction::new();
                        txn.set_root_pipeline(wrench.root_pipeline_id);
                        wrench.api.send_transaction(wrench.document_id, txn);
                        show.do_frame = true;
                    }
                    Key::Character("m") | Key::Character("M") => {
                        wrench.api.notify_memory_pressure();
                        show.do_render = true;
                    }
                    Key::Character("l") | Key::Character("L") => {
                        show.do_loop = !show.do_loop;
                        show.do_render = true;
                    }
                    Key::Named(NamedKey::ArrowLeft) => {
                        show.thing.prev_frame();
                        show.do_frame = true;
                    }
                    Key::Named(NamedKey::ArrowRight) => {
                        show.thing.next_frame();
                        show.do_frame = true;
                    }
                    Key::Character("h") | Key::Character("H") => {
                        show.show_help = !show.show_help;
                        show.do_render = true;
                    }
                    Key::Character("c") | Key::Character("C") => {
                        wrench.api.save_capture(PathBuf::from("../captures/wrench"), CaptureBits::all());
                    }
                    Key::Character("x") | Key::Character("X") => {
                        let results = wrench.api.hit_test(wrench.document_id, show.cursor_position);
                        println!("Hit test results:");
                        for item in &results.items {
                            println!("  • {:?}", item);
                        }
                        println!();
                    }
                    Key::Character("z") | Key::Character("Z") => {
                        show.debug_flags.toggle(DebugFlags::ZOOM_DBG);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(show.debug_flags));
                        show.do_render = true;
                    }
                    Key::Character("y") | Key::Character("Y") => {
                        println!("Clearing all caches...");
                        wrench.api.send_debug_cmd(DebugCommand::ClearCaches(ClearCache::all()));
                        show.do_frame = true;
                    }
                    _ => {}
                }
            }
            _ => {}
        }
    }

    fn user_event(&mut self, _event_loop: &ActiveEventLoop, _event: ()) {
        if let Some(ref mut show) = self.show_state {
            show.do_render = true;
        }
    }

    fn about_to_wait(&mut self, event_loop: &ActiveEventLoop) {
        let Some(ref mut show) = self.show_state else { return };
        let Some(ref mut wrench) = self.wrench else { return };
        let Some(ref mut window) = self.window else { return };

        if show.no_block || cfg!(target_os = "android") {
            event_loop.set_control_flow(ControlFlow::Poll);
            // On Android the notifier never fires send_event() (see wrench.rs),
            // so user_event() never sets do_render. Drive rendering by polling.
            show.do_render = true;
        } else {
            event_loop.set_control_flow(ControlFlow::Wait);
        }

        window.update(wrench);

        if show.do_frame {
            show.do_frame = false;
            let frame_num = show.thing.do_frame(wrench);
            unsafe { CURRENT_FRAME_NUMBER = frame_num; }
        }

        if show.do_render {
            show.do_render = false;

            if show.show_help {
                wrench.show_onscreen_help();
            }

            wrench.render();
            window.upload_software_to_native();
            window.swap_buffers();

            if show.do_loop {
                show.thing.next_frame();
            }
        }
    }

    fn exiting(&mut self, _event_loop: &ActiveEventLoop) {
        // Deinit WebRender renderer. We take ownership so deinit() can consume self.
        if let Some(wrench) = self.wrench.take() {
            wrench.renderer.deinit();
        }
        // On Android, force-exit the process.
        #[cfg(target_os = "android")]
        process::exit(self.exit_code);
    }
}

fn build_app(args: clap::ArgMatches, proxy: Option<EventLoopProxy<()>>) -> WrenchApp {
    let res_path = args.value_of("shaders").map(PathBuf::from);
    let size = args.value_of("size")
        .map(|s| if s == "720p" {
            DeviceIntSize::new(1280, 720)
        } else if s == "1080p" {
            DeviceIntSize::new(1920, 1080)
        } else if s == "4k" {
            DeviceIntSize::new(3840, 2160)
        } else {
            let x = s.find('x').expect("Size must be specified as 720p, 1080p, 4k, or widthxheight");
            let w = s[0..x].parse::<i32>().expect("Invalid size width");
            let h = s[x+1..].parse::<i32>().expect("Invalid size height");
            DeviceIntSize::new(w, h)
        })
        .unwrap_or(DeviceIntSize::new(1920, 1080));

    let dump_shader_source = args.value_of("dump_shader_source").map(String::from);
    let headless = args.is_present("headless");
    let angle = args.is_present("angle");
    let software = args.is_present("software");
    let sw_compositor = args.is_present("sw_compositor");
    if sw_compositor && !software {
        panic!("--sw-compositor requires --software");
    }
    let vsync = args.is_present("vsync");
    let using_compositor = args.is_present("compositor");
    let use_optimized_shaders = !args.is_present("use_unoptimized_shaders");
    let rebuild = args.is_present("rebuild");
    let no_subpixel_aa = args.is_present("no_subpixel_aa");
    let verbose = args.is_present("verbose");
    let no_scissor = args.is_present("no_scissor");
    let no_batch_global = args.is_present("no_batch");
    let color_target_init = args.is_present("color_target_init");
    let precache = args.is_present("precache");
    let profiler_ui = args.value_of("profiler_ui").map(String::from);
    let compositor_clips = args.value_of("compositor_clips").map(|s| match s {
        "true" | "1" | "yes" | "on" => true,
        "false" | "0" | "no" | "off" => false,
        _ => panic!("Unexpected --compositor-clips value {}", s),
    });

    let opengles_version = (3u8, 0u8);
    let opengl_version = (3u8, 2u8);
    let gl_request = match args.value_of("renderer") {
        Some("es3") => GlApiRequest::OpenGlEs { major: opengles_version.0, minor: opengles_version.1 },
        Some("gl3") => GlApiRequest::OpenGl { major: opengl_version.0, minor: opengl_version.1 },
        Some("default") | None => {
            if angle || cfg!(target_os = "android") {
                // GlThenGles fails on Angle/Android (bug 1928322, bug 1971545).
                GlApiRequest::OpenGlEs { major: opengles_version.0, minor: opengles_version.1 }
            } else {
                GlApiRequest::GlThenGles { opengl_version, opengles_version }
            }
        }
        Some(api) => panic!("Unexpected renderer string {}", api),
    };

    let subcommand = args.subcommand_name().unwrap_or("").to_owned();

    // Reftest
    let (reftest_specific, reftest_fuzz) = if let Some(m) = args.subcommand_matches("reftest") {
        (m.value_of("REFTEST").map(PathBuf::from),
         m.value_of("fuzz_tolerance").and_then(|s| s.parse::<f64>().ok()))
    } else {
        (None, None)
    };

    // Show — build the thing while args is in scope.
    let (thing_to_build, show_no_block, show_no_batch) = if let Some(m) = args.subcommand_matches("show") {
        let input_path = m.value_of("INPUT").map(PathBuf::from).unwrap();
        let no_block = args.is_present("no_block");
        let no_batch = args.is_present("no_batch");
        let thing = if input_path.join("scenes").as_path().is_dir() {
            let scene_id = m.value_of("scene-id").map(|z| z.parse::<u32>().unwrap());
            let frame_id = m.value_of("frame-id").map(|z| z.parse::<u32>().unwrap());
            ThingToBuild::Ready(Box::new(CapturedSequence::new(
                input_path,
                scene_id.unwrap_or(1),
                frame_id.unwrap_or(1),
            )) as Box<dyn WrenchThing>)
        } else if input_path.as_path().is_dir() {
            ThingToBuild::LoadCapture(input_path)
        } else {
            ThingToBuild::Yaml(YamlFrameReader::new_from_show_args(m))
        };
        (Some(thing), no_block, no_batch)
    } else {
        (None, false, false)
    };

    // Png
    let (png_reader, png_surface, png_output_path) = if let Some(m) = args.subcommand_matches("png") {
        let surface = match m.value_of("surface") {
            Some("screen") | None => png::ReadSurface::Screen,
            _ => panic!("Unknown surface argument value"),
        };
        let output_path = m.value_of("OUTPUT").map(PathBuf::from);
        let reader = YamlFrameReader::new_from_args(m);
        (Some(reader), surface, output_path)
    } else {
        (None, png::ReadSurface::Screen, None)
    };

    // Perf
    let (perf_benchmark, perf_filename, perf_as_csv, perf_warmup_frames, perf_sample_count) =
        if let Some(m) = args.subcommand_matches("perf") {
            let as_csv = m.is_present("csv");
            let auto_filename = m.is_present("auto-filename");
            let warmup_frames = m.value_of("warmup_frames").map(|s| s.parse::<usize>().unwrap());
            let sample_count = m.value_of("sample_count").map(|s| s.parse::<usize>().unwrap());
            let benchmark = m.value_of("benchmark").unwrap_or("benchmarks/benchmarks.list").to_owned();
            let mut filename = m.value_of("filename").unwrap().to_string();
            if auto_filename {
                let timestamp = chrono::Local::now().format("%Y-%m-%d-%H-%M-%S");
                filename.push_str(&format!("/wrench-perf-{}.{}", timestamp,
                    if as_csv { "csv" } else { "json" }));
            }
            (benchmark, filename, as_csv, warmup_frames, sample_count)
        } else {
            (String::new(), String::new(), false, None, None)
        };

    // Bench
    let bench = args.subcommand_matches("bench").map(|m| {
        let input = BenchInput::from_path(m.value_of("INPUT").map(PathBuf::from).unwrap());
        let options = BenchOptions {
            mode: if m.is_present("scene-build") {
                BenchMode::SceneBuild
            } else if m.is_present("render") && !m.is_present("frame-build") {
                BenchMode::RenderOnly
            } else {
                BenchMode::FrameBuild
            },
            render: m.is_present("render"),
            gpu_sync: m.is_present("gpu-sync"),
            gpu_queries: m.is_present("gpu-queries"),
            iterations: m.value_of("iterations").map(|s| s.parse::<usize>().expect("Invalid iteration count")).unwrap_or(100),
            warmup: m.value_of("warmup").map(|s| s.parse::<usize>().expect("Invalid warmup count")).unwrap_or(10),
            all_counters: m.is_present("counters"),
            csv: m.value_of("csv").map(PathBuf::from),
            save: m.value_of("save").map(PathBuf::from),
            baseline: m.value_of("baseline").map(PathBuf::from),
        };
        (input, options)
    });

    // ComparePerf
    let (compare_first, compare_second) = if let Some(m) = args.subcommand_matches("compare_perf") {
        (m.value_of("first_filename").unwrap().to_owned(),
         m.value_of("second_filename").unwrap().to_owned())
    } else {
        (String::new(), String::new())
    };

    WrenchApp {
        size, vsync, angle, software, sw_compositor, using_compositor, gl_request, headless,
        res_path, use_optimized_shaders, rebuild, no_subpixel_aa, verbose,
        no_scissor, no_batch_global, color_target_init, precache, dump_shader_source, profiler_ui,
        subcommand, compositor_clips, reftest_specific, reftest_fuzz,
        thing_to_build, show_no_block, show_no_batch,
        png_reader, png_surface, png_output_path,
        perf_benchmark, perf_filename, perf_as_csv, perf_warmup_frames, perf_sample_count,
        bench,
        compare_first, compare_second,
        proxy,
        window: None, wrench: None, rx: None, show_state: None, exit_code: 0,
    }
}

fn parse_args() -> clap::ArgMatches {
    #[allow(deprecated)] // FIXME(bug 1771450): Use clap-serde or another way
    let args_yaml = load_yaml!("args.yaml");
    #[allow(deprecated)]
    let clap_app = clap::Command::from_yaml(args_yaml).arg_required_else_help(true);

    #[cfg(target_os = "android")]
    {
        std::env::set_var("RUST_BACKTRACE", "full");

        let mut args = vec!["wrench".to_string()];
        let mut args_path = ANDROID_APP.get().unwrap().internal_data_path().unwrap();
        args_path.push("wrench");
        args_path.push("args");

        if let Ok(wrench_args) = std::fs::read_to_string(&args_path) {
            for line in wrench_args.lines() {
                if let Some(envvar) = line.strip_prefix("env: ") {
                    if let Some((lhs, rhs)) = envvar.split_once('=') {
                        std::env::set_var(lhs, rhs);
                    } else {
                        std::env::set_var(envvar, "");
                    }
                    continue;
                }
                for arg in line.split_whitespace() {
                    args.push(arg.to_string());
                }
            }
        }

        clap_app.get_matches_from(&args)
    }

    #[cfg(not(target_os = "android"))]
    clap_app.get_matches()
}

fn run(event_loop: EventLoop<()>, args: clap::ArgMatches) -> i32 {
    let proxy = event_loop.create_proxy();
    let mut app = build_app(args, Some(proxy));
    event_loop.run_app(&mut app).expect("event loop error");
    app.exit_code
}

#[cfg(not(target_os = "android"))]
fn run_headless(args: clap::ArgMatches) -> i32 {
    let mut app = build_app(args, None);

    assert!(app.headless, "run_headless called without --headless");
    assert!(app.subcommand != "show", "`wrench show` is not supported in headless mode");

    let sw_ctx = if app.software { Some(make_software_context()) } else { None };
    #[cfg_attr(not(feature = "software"), allow(unused_variables))]
    let gl: Rc<dyn gl::Gl> = if let Some(ref sw_ctx) = sw_ctx {
        #[cfg(feature = "software")]
        { Rc::new(*sw_ctx) }
        #[cfg(not(feature = "software"))]
        { unreachable!() }
    } else {
        match gl::GlType::default() {
            gl::GlType::Gl => unsafe {
                gl::GlFns::load_with(|s| HeadlessContext::get_proc_address(s) as *const _)
            },
            gl::GlType::Gles => unsafe {
                gl::GlesFns::load_with(|s| HeadlessContext::get_proc_address(s) as *const _)
            },
        }
    };
    let mut window = WindowWrapper::Headless {
        context: HeadlessContext::new(app.size.width, app.size.height),
        gl,
        sw_ctx,
    };

    let init_target_dbg = app.color_target_init || matches!(
        app.subcommand.as_str(),
        "reftest"
    );

    let needs_frame_notifier = matches!(
        app.subcommand.as_str(),
        "perf" | "bench" | "reftest" | "png" | "rawtest" | "test_invalidation"
    );
    let (notifier, rx) = if needs_frame_notifier {
        let (n, r) = create_notifier();
        (Some(n), Some(r))
    } else {
        (None, None)
    };

    let layer_compositor = if app.using_compositor {
        window.create_compositor()
    } else {
        None
    };

    let dim = window.get_inner_size();
    let mut wrench = Wrench::new(
        &mut window,
        None,
        app.res_path.clone(),
        app.use_optimized_shaders,
        dim,
        app.rebuild,
        app.no_subpixel_aa,
        app.verbose,
        app.no_scissor,
        app.no_batch_global,
        init_target_dbg,
        app.precache,
        app.dump_shader_source.clone(),
        notifier,
        layer_compositor,
        app.sw_compositor,
        app.compositor_clips,
    );

    if let Some(ui_str) = &app.profiler_ui {
        wrench.renderer.set_profiler_ui(ui_str);
    }

    window.update(&mut wrench);

    let gl = window.gl();
    gl.clear_color(0.3, 0.0, 0.0, 1.0);
    println!("OpenGL version {}, {}", gl.get_string(gl::VERSION), gl.get_string(gl::RENDERER));
    println!("hidpi factor: {}", window.hidpi_factor());

    match app.subcommand.as_str() {
        "png" => {
            let reader = app.png_reader.take().unwrap();
            let rx = rx.unwrap();
            png::png(&mut wrench, app.png_surface, &mut window, reader, &rx, app.png_output_path.clone());
            // shut_down() keeps the receiver alive until the render backend has
            // acknowledged shutdown, which deinit() alone does not.
            wrench.shut_down(rx);
            0
        }
        "reftest" => {
            // reftest() calls wrench.shut_down() which calls deinit().
            reftest(
                wrench,
                &mut window,
                app.reftest_specific.as_deref(),
                app.reftest_fuzz,
                rx.unwrap(),
            ) as i32
        }
        "rawtest" => {
            // rawtest() calls wrench.shut_down() which calls deinit().
            rawtest(wrench, &mut window, rx.unwrap());
            0
        }
        "perf" => {
            wrench.rebuild_display_lists = true;
            let rx = rx.unwrap();
            let harness = PerfHarness::new(
                &mut wrench,
                &mut window,
                &rx,
                app.perf_warmup_frames,
                app.perf_sample_count,
            );
            let base_manifest = Path::new(&app.perf_benchmark);
            harness.run(base_manifest, &app.perf_filename, app.perf_as_csv);
            wrench.shut_down(rx);
            0
        }
        "bench" => {
            let rx = rx.unwrap();
            let (input, options) = app.bench.take().unwrap();
            BenchHarness::new(&mut wrench, &mut window, &rx, options).run(input);
            wrench.shut_down(rx);
            0
        }
        "test_invalidation" => {
            let rx = rx.unwrap();
            let harness = test_invalidation::TestHarness::new(&mut wrench, &mut window, &rx);
            let num_failures = harness.run();
            wrench.shut_down(rx);
            num_failures as i32
        }
        "compare_perf" => {
            wrench.renderer.deinit();
            perf::compare(&app.compare_first, &app.compare_second);
            0
        }
        "test_init" => {
            println!("Initialization successful");
            wrench.renderer.deinit();
            0
        }
        "test_shaders" => {
            wrench.renderer.deinit();
            test_shaders::test_shaders();
            0
        }
        other => panic!("Unknown subcommand: {:?}", other),
    }
}

#[cfg(not(target_os = "android"))]
pub fn main() {
    #[cfg(feature = "env_logger")]
    env_logger::init();

    #[cfg(target_os = "macos")]
    {
        use core_foundation::{self as cf, base::TCFType};
        let i = cf::bundle::CFBundle::main_bundle().info_dictionary();
        let mut i = unsafe { i.to_mutable() };
        i.set(
            cf::string::CFString::new("NSSupportsAutomaticGraphicsSwitching"),
            cf::boolean::CFBoolean::true_value().into_CFType(),
        );
    }

    let args = parse_args();
    let exit_code = if args.is_present("headless") {
        run_headless(args)
    } else {
        let event_loop = EventLoop::new().expect("failed to create event loop");
        run(event_loop, args)
    };
    if exit_code != 0 {
        process::exit(exit_code);
    }
}

#[cfg(target_os = "android")]
#[no_mangle]
fn android_main(app: AndroidApp) {
    android_logger::init_once(
        android_logger::Config::default()
            .with_tag("Wrench")
            .with_max_level(log::LevelFilter::Info),
    );

    ANDROID_APP.set(app.clone()).ok();

    // Redirect stdout/stderr to a file to avoid logcat line-length truncation
    // breaking base64 image dumps used by the reftest analyser.
    {
        use std::fs::File;
        use std::io::{BufRead, BufReader, Write};
        use std::os::unix::io::{FromRawFd, RawFd};
        use std::thread;

        let mut out_path = app.internal_data_path().unwrap();
        out_path.push("wrench");
        out_path.push("stdout");
        let mut out_file = File::create(&out_path).expect("Failed to create stdout file");

        let mut logpipe: [RawFd; 2] = Default::default();
        unsafe {
            libc::pipe(logpipe.as_mut_ptr());
            libc::dup2(logpipe[1], libc::STDOUT_FILENO);
            libc::dup2(logpipe[1], libc::STDERR_FILENO);
        }

        thread::spawn(move || {
            let mut reader = BufReader::new(unsafe { File::from_raw_fd(logpipe[0]) });
            let mut buffer = String::new();
            loop {
                buffer.clear();
                if let Ok(len) = reader.read_line(&mut buffer) {
                    if len == 0 { break; }
                    let msg = buffer.trim_end_matches('\n');
                    out_file.write_all(msg.as_bytes()).ok();
                    out_file.write_all(b"\n").ok();
                    log::info!("{}", msg);
                }
            }
        });
    }

    use winit::platform::android::EventLoopBuilderExtAndroid;
    let event_loop = EventLoop::builder()
        .with_android_app(app)
        .build()
        .expect("failed to create event loop");

    let args = parse_args();
    let exit_code = run(event_loop, args);
    process::exit(exit_code);
}
