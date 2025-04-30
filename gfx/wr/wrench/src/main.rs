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

mod angle;
mod blob;
#[cfg(target_os = "windows")]
mod composite;
mod egl;
mod parse_function;
mod perf;
mod png;
mod premultiply;
mod rawtest;
mod reftest;
mod test_invalidation;
mod test_shaders;
mod wrench;
mod yaml_frame_reader;
mod yaml_helper;

#[cfg(target_os = "windows")]
use composite::WrCompositor;
use gleam::gl;
#[cfg(feature = "software")]
use gleam::gl::Gl;
use crate::perf::PerfHarness;
use crate::rawtest::RawtestHarness;
use crate::reftest::{ReftestHarness, ReftestOptions};
#[cfg(feature = "headless")]
use std::ffi::CString;
#[cfg(feature = "headless")]
use std::mem;
use std::os::raw::c_void;
use std::path::{Path, PathBuf};
use std::process;
use std::ptr;
use std::rc::Rc;
#[cfg(feature = "software")]
use std::slice;
use std::sync::mpsc::{channel, Sender, Receiver};
use webrender::{DebugFlags, LayerCompositor};
use webrender::api::*;
use webrender::render_api::*;
use webrender::api::units::*;
use winit::dpi::{LogicalPosition, LogicalSize};
use winit::event::VirtualKeyCode;
use winit::platform::run_return::EventLoopExtRunReturn;
use crate::wrench::{CapturedSequence, Wrench, WrenchThing};
use crate::yaml_frame_reader::YamlFrameReader;

pub const PLATFORM_DEFAULT_FACE_NAME: &str = "Arial";

pub static mut CURRENT_FRAME_NUMBER: u32 = 0;

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
        let c_str = CString::new(s).expect("Unable to create CString");
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
    WindowedContext(glutin::WindowedContext<glutin::PossiblyCurrent>, Rc<dyn gl::Gl>, Option<swgl::Context>),
    Angle(winit::window::Window, angle::Context, Rc<dyn gl::Gl>, Option<swgl::Context>),
    Headless(HeadlessContext, Rc<dyn gl::Gl>, Option<swgl::Context>),
}

pub struct HeadlessEventIterater;

impl WindowWrapper {
    #[cfg(feature = "software")]
    fn upload_software_to_native(&self) {
        if matches!(*self, WindowWrapper::Headless(..)) { return }
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
        match *self {
            WindowWrapper::WindowedContext(ref windowed_context, _, _) => {
                windowed_context.swap_buffers().unwrap()
            }
            WindowWrapper::Angle(_, ref context, _, _) => context.swap_buffers().unwrap(),
            WindowWrapper::Headless(_, _, _) => {}
        }
    }

    fn get_inner_size(&self) -> DeviceIntSize {
        fn inner_size(window: &winit::window::Window) -> DeviceIntSize {
            let size = window.inner_size();
            DeviceIntSize::new(size.width as i32, size.height as i32)
        }
        match *self {
            WindowWrapper::WindowedContext(ref windowed_context, ..) => {
                inner_size(windowed_context.window())
            }
            WindowWrapper::Angle(ref window, ..) => inner_size(window),
            WindowWrapper::Headless(ref context, ..) => DeviceIntSize::new(context.width, context.height),
        }
    }

    fn hidpi_factor(&self) -> f32 {
        match *self {
            WindowWrapper::WindowedContext(ref windowed_context, ..) => {
                windowed_context.window().scale_factor() as f32
            }
            WindowWrapper::Angle(ref window, ..) => window.scale_factor() as f32,
            WindowWrapper::Headless(..) => 1.0,
        }
    }

    fn resize(&mut self, size: DeviceIntSize) {
        match *self {
            WindowWrapper::WindowedContext(ref mut windowed_context, ..) => {
                windowed_context.window()
                    .set_inner_size(LogicalSize::new(size.width as f64, size.height as f64))
            },
            WindowWrapper::Angle(ref mut window, ..) => {
                window.set_inner_size(LogicalSize::new(size.width as f64, size.height as f64))
            },
            WindowWrapper::Headless(..) => unimplemented!(), // requites Glutin update
        }
    }

    fn set_title(&mut self, title: &str) {
        match *self {
            WindowWrapper::WindowedContext(ref windowed_context, ..) => {
                windowed_context.window().set_title(title)
            }
            WindowWrapper::Angle(ref window, ..) => window.set_title(title),
            WindowWrapper::Headless(..) => (),
        }
    }

    pub fn software_gl(&self) -> Option<&swgl::Context> {
        match *self {
            WindowWrapper::WindowedContext(_, _, ref swgl) |
            WindowWrapper::Angle(_, _, _, ref swgl) |
            WindowWrapper::Headless(_, _, ref swgl) => swgl.as_ref(),
        }
    }

    pub fn native_gl(&self) -> &dyn gl::Gl {
        match *self {
            WindowWrapper::WindowedContext(_, ref gl, _) |
            WindowWrapper::Angle(_, _, ref gl, _) |
            WindowWrapper::Headless(_, ref gl, _) => &**gl,
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
        match *self {
            WindowWrapper::WindowedContext(_, ref gl, ref swgl) |
            WindowWrapper::Angle(_, _, ref gl, ref swgl) |
            WindowWrapper::Headless(_, ref gl, ref swgl) => {
                match swgl {
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
        match *self {
            WindowWrapper::WindowedContext(_, _, _) |
            WindowWrapper::Headless(_, _, _) => unreachable!(),
            WindowWrapper::Angle(_, ref ctx, _, _) => ctx.get_d3d11_device(),
        }
    }

    #[cfg(target_os = "windows")]
    pub fn create_compositor(&self) -> Option<Box<dyn LayerCompositor>> {
        Some(Box::new(WrCompositor::new(self)) as Box<dyn LayerCompositor>)
    }

    #[cfg(not(target_os = "windows"))]
    pub fn create_compositor(&self) -> Option<Box<dyn LayerCompositor>> {
        None
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

fn make_window(
    size: DeviceIntSize,
    vsync: bool,
    events_loop: &Option<winit::event_loop::EventLoop<()>>,
    angle: bool,
    gl_request: glutin::GlRequest,
    software: bool,
    using_compositor: bool,
) -> WindowWrapper {
    let sw_ctx = if software {
        Some(make_software_context())
    } else {
        None
    };

    let wrapper = if let Some(events_loop) = events_loop {
        let context_builder = glutin::ContextBuilder::new()
            .with_gl(gl_request)
            // Glutin can fail to create a context on Android if vsync is not set
            .with_vsync(vsync || cfg!(target_os = "android"));

        let window_builder = winit::window::WindowBuilder::new()
            .with_title("WRench")
            .with_inner_size(LogicalSize::new(size.width as f64, size.height as f64));

        if angle {
            angle::Context::with_window(
                window_builder, context_builder, events_loop, using_compositor,
            ).map(|(_window, _context)| {
                unsafe {
                    _context
                        .make_current()
                        .expect("unable to make context current!");
                }

                let gl = match _context.get_api() {
                    glutin::Api::OpenGl => unsafe {
                        gl::GlFns::load_with(|symbol| _context.get_proc_address(symbol) as *const _)
                    },
                    glutin::Api::OpenGlEs => unsafe {
                        gl::GlesFns::load_with(|symbol| _context.get_proc_address(symbol) as *const _)
                    },
                    glutin::Api::WebGl => unimplemented!(),
                };

                WindowWrapper::Angle(_window, _context, gl, sw_ctx)
            }).unwrap()
        } else {
            let windowed_context = context_builder
                .build_windowed(window_builder, events_loop)
                .unwrap();

            let windowed_context = unsafe {
                windowed_context
                    .make_current()
                    .expect("unable to make context current!")
            };

            let gl = match windowed_context.get_api() {
                glutin::Api::OpenGl => unsafe {
                    gl::GlFns::load_with(
                        |symbol| windowed_context.get_proc_address(symbol) as *const _
                    )
                },
                glutin::Api::OpenGlEs => unsafe {
                    gl::GlesFns::load_with(
                        |symbol| windowed_context.get_proc_address(symbol) as *const _
                    )
                },
                glutin::Api::WebGl => unimplemented!(),
            };

            WindowWrapper::WindowedContext(windowed_context, gl, sw_ctx)
        }
    } else {
        #[cfg_attr(not(feature = "software"), allow(unused_variables))]
        let gl = if let Some(sw_ctx) = sw_ctx {
            #[cfg(feature = "software")]
            {
                Rc::new(sw_ctx)
            }
            #[cfg(not(feature = "software"))]
            {
                unreachable!("make_software_context() should have failed if 'software' feature is not enabled")
            }
        } else {
            match gl::GlType::default() {
                gl::GlType::Gl => unsafe {
                    gl::GlFns::load_with(|symbol| {
                        HeadlessContext::get_proc_address(symbol) as *const _
                    })
                },
                gl::GlType::Gles => unsafe {
                    gl::GlesFns::load_with(|symbol| {
                        HeadlessContext::get_proc_address(symbol) as *const _
                    })
                },
            }
        };
        WindowWrapper::Headless(HeadlessContext::new(size.width, size.height), gl, sw_ctx)
    };

    let gl = wrapper.gl();

    gl.clear_color(0.3, 0.0, 0.0, 1.0);

    let gl_version = gl.get_string(gl::VERSION);
    let gl_renderer = gl.get_string(gl::RENDERER);

    println!("OpenGL version {}, {}", gl_version, gl_renderer);
    println!(
        "hidpi factor: {}",
        wrapper.hidpi_factor()
    );

    wrapper
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

// setup a notifier so we can wait for frames to be finished
impl RenderNotifier for Notifier {
    fn clone(&self) -> Box<dyn RenderNotifier> {
        Box::new(Notifier {
            tx: self.tx.clone(),
        })
    }

    fn wake_up(
        &self,
        composite_needed: bool,
    ) {
        let msg = NotifierEvent::WakeUp {
            composite_needed,
        };
        self.tx.send(msg).unwrap();
    }

    fn shut_down(&self) {
        self.tx.send(NotifierEvent::ShutDown).unwrap();
    }

    fn new_frame_ready(&self,
                       _: DocumentId,
                       _: FramePublishId,
                       params: &FrameReadyParams) {
        // TODO(gw): Refactor wrench so that it can take advantage of cases
        //           where no composite is required when appropriate.
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

fn reftest<'a>(
    mut wrench: Wrench,
    window: &mut WindowWrapper,
    subargs: &clap::ArgMatches,
    rx: Receiver<NotifierEvent>
) -> usize {
    let dim = window.get_inner_size();
    #[cfg(target_os = "android")]
    let base_manifest = {
        let mut list_path = PathBuf::new();
        list_path.push(ndk_glue::native_activity().internal_data_path().to_str().unwrap());
        list_path.push("wrench");
        list_path.push("reftests");
        list_path.push("reftest.list");
        list_path
    };
    #[cfg(not(target_os = "android"))]
    let base_manifest = Path::new("reftests/reftest.list").to_owned();

    let specific_reftest = subargs.value_of("REFTEST").map(Path::new);
    let mut reftest_options = ReftestOptions::default();
    if let Some(allow_max_diff) = subargs.value_of("fuzz_tolerance") {
        reftest_options.allow_max_difference = allow_max_diff.parse().unwrap_or(1);
        reftest_options.allow_num_differences = dim.width as usize * dim.height as usize;
    }
    let num_failures = ReftestHarness::new(&mut wrench, window, &rx)
        .run(&base_manifest, specific_reftest, &reftest_options);
    wrench.shut_down(rx);
    num_failures
}

#[cfg_attr(target_os = "android", ndk_glue::main)]
pub fn main() {
    #[cfg(feature = "env_logger")]
    env_logger::init();

    // By default on Android, the ndk_glue crate will redirect stdout and stderr to logcat. Logcat,
    // however, truncates long lines, meaning our base64 image dumps will be truncated. To avoid
    // this, copy ndk_glue's code to redirect stdout and stderr to logcat, but additionally write
    // it to a file which can later be pulled from the device.
    #[cfg(target_os = "android")]
    {
        use std::ffi::{CStr, CString};
        use std::fs::File;
        use std::io::{BufRead, BufReader, Write};
        use std::os::unix::io::{FromRawFd, RawFd};
        use std::thread;

        let mut out_path = PathBuf::new();
        out_path.push(ndk_glue::native_activity().internal_data_path().to_str().unwrap());
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
            let tag = CStr::from_bytes_with_nul(b"Wrench\0").unwrap();
            let mut reader = BufReader::new(unsafe { File::from_raw_fd(logpipe[0]) });
            let mut buffer = String::new();
            loop {
                buffer.clear();
                if let Ok(len) = reader.read_line(&mut buffer) {
                    if len == 0 {
                        break;
                    } else if let Ok(msg) = CString::new(buffer.clone()) {
                        out_file.write_all(msg.as_bytes()).ok();
                        ndk_glue::android_log(log::Level::Info, tag, &msg);
                    }
                }
            }
        });
    }

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

    #[allow(deprecated)] // FIXME(bug 1771450): Use clap-serde or another way
    let args_yaml = load_yaml!("args.yaml");
    #[allow(deprecated)] // FIXME(bug 1771450): Use clap-serde or another way
    let clap = clap::Command::from_yaml(args_yaml)
        .arg_required_else_help(true);

    // On android devices, attempt to read command line arguments from a text
    // file located at <internal_data_dir>/wrench/args.
    #[cfg(target_os = "android")]
    let args = {
        // get full backtraces by default because it's hard to request
        // externally on android
        std::env::set_var("RUST_BACKTRACE", "full");

        let mut args = vec!["wrench".to_string()];

        let mut args_path = PathBuf::new();
        args_path.push(ndk_glue::native_activity().internal_data_path().to_str().unwrap());
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

        clap.get_matches_from(&args)
    };

    #[cfg(not(target_os = "android"))]
    let args = clap.get_matches();

    // handle some global arguments
    let res_path = args.value_of("shaders").map(PathBuf::from);
    let size = args.value_of("size")
        .map(|s| if s == "720p" {
            DeviceIntSize::new(1280, 720)
        } else if s == "1080p" {
            DeviceIntSize::new(1920, 1080)
        } else if s == "4k" {
            DeviceIntSize::new(3840, 2160)
        } else {
            let x = s.find('x').expect(
                "Size must be specified exactly as 720p, 1080p, 4k, or width x height",
            );
            let w = s[0 .. x].parse::<i32>().expect("Invalid size width");
            let h = s[x + 1 ..].parse::<i32>().expect("Invalid size height");
            DeviceIntSize::new(w, h)
        })
        .unwrap_or(DeviceIntSize::new(1920, 1080));

    let dump_shader_source = args.value_of("dump_shader_source").map(String::from);

    let mut events_loop = if args.is_present("headless") {
        None
    } else {
        Some(winit::event_loop::EventLoop::new())
    };

    let opengles_version = (3, 0);
    let opengl_version = (3, 2);
    let gl_request = match args.value_of("renderer") {
        Some("es3") => {
            glutin::GlRequest::Specific(glutin::Api::OpenGlEs, opengles_version)
        }
        Some("gl3") => {
            glutin::GlRequest::Specific(glutin::Api::OpenGl, opengl_version)
        }
        Some("default") | None => {
            if cfg!(target_os = "android") {
                // Some Android devices successfully allow binding the OpenGL API but then fail to
                // return any available configs, meaning context creation always fails. So just
                // request GLES by default on Android.
                glutin::GlRequest::Specific(glutin::Api::OpenGlEs, opengles_version)
            } else {
                glutin::GlRequest::GlThenGles {
                    opengl_version,
                    opengles_version,
                }
            }
        }
        Some(api) => {
            panic!("Unexpected renderer string {}", api);
        }
    };

    let software = args.is_present("software");

    // On Android we can only create an OpenGL context when we have a
    // native_window handle, so wait here until we are resumed and have a
    // handle. If the app gets minimized this will no longer be valid, but
    // that's okay for wrench's usage.
    #[cfg(target_os = "android")]
    {
        events_loop.as_mut().unwrap().run_return(|event, _elwt, control_flow| {
            if let winit::event::Event::Resumed = event {
                if ndk_glue::native_window().is_some() {
                    *control_flow = winit::event_loop::ControlFlow::Exit;
                }
            }
        });
    }

    let using_compositor = args.is_present("compositor");

    let mut window = make_window(
        size,
        args.is_present("vsync"),
        &events_loop,
        args.is_present("angle"),
        gl_request,
        software,
        using_compositor,
    );
    let dim = window.get_inner_size();

    let needs_frame_notifier = args.subcommand_name().map_or(false, |name| {
        ["perf", "reftest", "png", "rawtest", "test_invalidation"].contains(&name)
    });
    let (notifier, rx) = if needs_frame_notifier {
        let (notifier, rx) = create_notifier();
        (Some(notifier), Some(rx))
    } else {
        (None, None)
    };

    let layer_compositor = if using_compositor {
        window.create_compositor()
    } else {
        None
    };

    let mut wrench = Wrench::new(
        &mut window,
        events_loop.as_mut().map(|el| el.create_proxy()),
        res_path,
        !args.is_present("use_unoptimized_shaders"),
        dim,
        args.is_present("rebuild"),
        args.is_present("no_subpixel_aa"),
        args.is_present("verbose"),
        args.is_present("no_scissor"),
        args.is_present("no_batch"),
        args.is_present("precache"),
        dump_shader_source,
        notifier,
        layer_compositor,
    );

    if let Some(ui_str) = args.value_of("profiler_ui") {
        wrench.renderer.set_profiler_ui(ui_str);
    }

    window.update(&mut wrench);

    if let Some(window_title) = wrench.take_title() {
        if !cfg!(windows) {
            window.set_title(&window_title);
        }
    }

    if let Some(subargs) = args.subcommand_matches("show") {
        let no_block = args.is_present("no_block");
        let no_batch = args.is_present("no_batch");
        render(
            &mut wrench,
            &mut window,
            events_loop.as_mut().expect("`wrench show` is not supported in headless mode"),
            subargs,
            no_block,
            no_batch,
        );
    } else if let Some(subargs) = args.subcommand_matches("png") {
        let surface = match subargs.value_of("surface") {
            Some("screen") | None => png::ReadSurface::Screen,
            Some("gpu-cache") => png::ReadSurface::GpuCache,
            _ => panic!("Unknown surface argument value")
        };
        let output_path = subargs.value_of("OUTPUT").map(PathBuf::from);
        let reader = YamlFrameReader::new_from_args(subargs);
        png::png(&mut wrench, surface, &mut window, reader, rx.unwrap(), output_path);
    } else if let Some(subargs) = args.subcommand_matches("reftest") {
        // Exit with an error code in order to ensure the CI job fails.
        process::exit(reftest(wrench, &mut window, subargs, rx.unwrap()) as _);
    } else if args.subcommand_matches("rawtest").is_some() {
        rawtest(wrench, &mut window, rx.unwrap());
        return;
    } else if let Some(subargs) = args.subcommand_matches("perf") {
        // Perf mode wants to benchmark the total cost of drawing
        // a new displaty list each frame.
        wrench.rebuild_display_lists = true;

        let as_csv = subargs.is_present("csv");
        let auto_filename = subargs.is_present("auto-filename");

        let warmup_frames = subargs.value_of("warmup_frames").map(|s| s.parse().unwrap());
        let sample_count = subargs.value_of("sample_count").map(|s| s.parse().unwrap());

        let harness = PerfHarness::new(&mut wrench,
                                       &mut window,
                                       rx.unwrap(),
                                       warmup_frames,
                                       sample_count);

        let benchmark = subargs.value_of("benchmark").unwrap_or("benchmarks/benchmarks.list");
        println!("Benchmark: {}", benchmark);
        let base_manifest = Path::new(benchmark);

        let mut filename = subargs.value_of("filename").unwrap().to_string();
        if auto_filename {
            let timestamp = chrono::Local::now().format("%Y-%m-%d-%H-%M-%S");
            filename.push_str(
                &format!("/wrench-perf-{}.{}",
                            timestamp,
                            if as_csv { "csv" } else { "json" }));
        }
        harness.run(base_manifest, &filename, as_csv);
        return;
    } else if args.subcommand_matches("test_invalidation").is_some() {
        let harness = test_invalidation::TestHarness::new(
            &mut wrench,
            &mut window,
            rx.unwrap(),
        );

        harness.run();
    } else if let Some(subargs) = args.subcommand_matches("compare_perf") {
        let first_filename = subargs.value_of("first_filename").unwrap();
        let second_filename = subargs.value_of("second_filename").unwrap();
        perf::compare(first_filename, second_filename);
        return;
    } else if args.subcommand_matches("test_init").is_some() {
        // Wrench::new() unwraps the Renderer initialization, so if
        // we reach this point then we have initialized successfully.
        println!("Initialization successful");
    } else if args.subcommand_matches("test_shaders").is_some() {
        test_shaders::test_shaders();
    } else {
        panic!("Should never have gotten here! {:?}", args);
    };

    wrench.renderer.deinit();

    // On android force-exit the process otherwise it stays running forever.
    #[cfg(target_os = "android")]
    process::exit(0);
}

fn render<'a>(
    wrench: &mut Wrench,
    window: &mut WindowWrapper,
    events_loop: &mut winit::event_loop::EventLoop<()>,
    subargs: &clap::ArgMatches,
    no_block: bool,
    no_batch: bool,
) {
    let input_path = subargs.value_of("INPUT").map(PathBuf::from).unwrap();

    // If the input is a directory, we are looking at a capture.
    let mut thing = if input_path.join("scenes").as_path().is_dir() {
        let scene_id = subargs.value_of("scene-id").map(|z| z.parse::<u32>().unwrap());
        let frame_id = subargs.value_of("frame-id").map(|z| z.parse::<u32>().unwrap());
        Box::new(CapturedSequence::new(
            input_path,
            scene_id.unwrap_or(1),
            frame_id.unwrap_or(1),
        ))
    } else if input_path.as_path().is_dir() {
        let mut documents = wrench.api.load_capture(input_path, None);
        println!("loaded {:?}", documents.iter().map(|cd| cd.document_id).collect::<Vec<_>>());
        let captured = documents.swap_remove(0);
        wrench.document_id = captured.document_id;
        Box::new(captured) as Box<dyn WrenchThing>
    } else {
        match input_path.extension().and_then(std::ffi::OsStr::to_str) {
            Some("yaml") => {
                Box::new(YamlFrameReader::new_from_args(subargs)) as Box<dyn WrenchThing>
            }
            _ => panic!("Tried to render with an unknown file type."),
        }
    };

    window.update(wrench);
    thing.do_frame(wrench);

    if let Some(fb_size) = wrench.renderer.device_size() {
        window.resize(fb_size);
    }

    let mut debug_flags = DebugFlags::empty();
    debug_flags.set(DebugFlags::DISABLE_BATCHING, no_batch);

    // Default the profile overlay on for android.
    if cfg!(target_os = "android") {
        debug_flags.toggle(DebugFlags::PROFILER_DBG);
        wrench.api.send_debug_cmd(DebugCommand::SetFlags(debug_flags));
    }

    let mut show_help = false;
    let mut do_loop = false;
    let mut cursor_position = WorldPoint::zero();
    let mut do_render = false;
    let mut do_frame = false;

    events_loop.run_return(|event, _elwt, control_flow| {
        // By default after each iteration of the event loop we block the thread until the next
        // events arrive. --no-block can be used to run the event loop as quickly as possible.
        // On Android, we are generally profiling when running wrench, and don't want to block
        // on UI events.
        if !no_block && cfg!(not(target_os = "android")) {
            *control_flow = winit::event_loop::ControlFlow::Wait;
        } else {
            *control_flow = winit::event_loop::ControlFlow::Poll;
        }

        match event {
            winit::event::Event::UserEvent(_) => {
                do_render = true;
            }
            winit::event::Event::WindowEvent { event, .. } => match event {
                winit::event::WindowEvent::CloseRequested => {
                    *control_flow = winit::event_loop::ControlFlow::Exit;
                }
                winit::event::WindowEvent::Focused(..) => do_render = true,
                winit::event::WindowEvent::CursorMoved { position, .. } => {
                    let pos: LogicalPosition<f32> = position.to_logical(window.hidpi_factor() as f64);
                    cursor_position = WorldPoint::new(pos.x, pos.y);
                    wrench.renderer.set_cursor_position(
                        DeviceIntPoint::new(
                            cursor_position.x.round() as i32,
                            cursor_position.y.round() as i32,
                        ),
                    );
                    do_render = true;
                }
                winit::event::WindowEvent::KeyboardInput {
                    input: winit::event::KeyboardInput {
                        state: winit::event::ElementState::Pressed,
                        virtual_keycode: Some(vk),
                        ..
                    },
                    ..
                } => match vk {
                    VirtualKeyCode::Escape => {
                        *control_flow = winit::event_loop::ControlFlow::Exit;
                    }
                    VirtualKeyCode::B => {
                        debug_flags.toggle(DebugFlags::INVALIDATION_DBG);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(debug_flags));
                        do_render = true;
                    }
                    VirtualKeyCode::P => {
                        debug_flags.toggle(DebugFlags::PROFILER_DBG);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(debug_flags));
                        do_render = true;
                    }
                    VirtualKeyCode::O => {
                        debug_flags.toggle(DebugFlags::RENDER_TARGET_DBG);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(debug_flags));
                        do_render = true;
                    }
                    VirtualKeyCode::I => {
                        debug_flags.toggle(DebugFlags::TEXTURE_CACHE_DBG);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(debug_flags));
                        do_render = true;
                    }
                    VirtualKeyCode::D => {
                        debug_flags.toggle(DebugFlags::PICTURE_CACHING_DBG);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(debug_flags));
                        do_render = true;
                    }
                    VirtualKeyCode::F => {
                        debug_flags.toggle(DebugFlags::PICTURE_BORDERS);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(debug_flags));
                        do_render = true;
                    }
                    VirtualKeyCode::Q => {
                        debug_flags.toggle(DebugFlags::GPU_TIME_QUERIES | DebugFlags::GPU_SAMPLE_QUERIES);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(debug_flags));
                        do_render = true;
                    }
                    VirtualKeyCode::V => {
                        debug_flags.toggle(DebugFlags::SHOW_OVERDRAW);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(debug_flags));
                        do_render = true;
                    }
                    VirtualKeyCode::G => {
                        debug_flags.toggle(DebugFlags::GPU_CACHE_DBG);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(debug_flags));

                        // force scene rebuild to see the full set of used GPU cache entries
                        let mut txn = Transaction::new();
                        txn.set_root_pipeline(wrench.root_pipeline_id);
                        wrench.api.send_transaction(wrench.document_id, txn);

                        do_frame = true;
                    }
                    VirtualKeyCode::M => {
                        wrench.api.notify_memory_pressure();
                        do_render = true;
                    }
                    VirtualKeyCode::L => {
                        do_loop = !do_loop;
                        do_render = true;
                    }
                    VirtualKeyCode::Left => {
                        thing.prev_frame();
                        do_frame = true;
                    }
                    VirtualKeyCode::Right => {
                        thing.next_frame();
                        do_frame = true;
                    }
                    VirtualKeyCode::H => {
                        show_help = !show_help;
                        do_render = true;
                    }
                    VirtualKeyCode::C => {
                        let path = PathBuf::from("../captures/wrench");
                        wrench.api.save_capture(path, CaptureBits::all());
                    }
                    VirtualKeyCode::X => {
                        let results = wrench.api.hit_test(
                            wrench.document_id,
                            cursor_position,
                        );

                        println!("Hit test results:");
                        for item in &results.items {
                            println!("  • {:?}", item);
                        }
                        println!();
                    }
                    VirtualKeyCode::Z => {
                        debug_flags.toggle(DebugFlags::ZOOM_DBG);
                        wrench.api.send_debug_cmd(DebugCommand::SetFlags(debug_flags));
                        do_render = true;
                    }
                    VirtualKeyCode::Y => {
                        println!("Clearing all caches...");
                        wrench.api.send_debug_cmd(DebugCommand::ClearCaches(ClearCache::all()));
                        do_frame = true;
                    }
                    _ => {}
                }
                _ => {}
            },
            winit::event::Event::MainEventsCleared => {
                window.update(wrench);

                if do_frame {
                    do_frame = false;
                    let frame_num = thing.do_frame(wrench);
                    unsafe {
                        CURRENT_FRAME_NUMBER = frame_num;
                    }
                }

                if do_render {
                    do_render = false;

                    if show_help {
                        wrench.show_onscreen_help();
                    }

                    wrench.render();
                    window.upload_software_to_native();
                    window.swap_buffers();

                    if do_loop {
                        thing.next_frame();
                    }
                }
            }
            _ => {}
        }
    });
}
