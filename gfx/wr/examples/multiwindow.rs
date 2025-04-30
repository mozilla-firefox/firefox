/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

extern crate euclid;
extern crate gleam;
extern crate glutin;
extern crate webrender;
extern crate winit;

use gleam::gl;
use glutin::NotCurrent;
use std::fs::File;
use std::io::Read;
use webrender::api::*;
use webrender::api::units::*;
use webrender::render_api::*;
use webrender::DebugFlags;
use winit::dpi::LogicalSize;
use winit::platform::run_return::EventLoopExtRunReturn;

struct Notifier {
    events_proxy: winit::event_loop::EventLoopProxy<()>,
}

impl Notifier {
    fn new(events_proxy: winit::event_loop::EventLoopProxy<()>) -> Notifier {
        Notifier { events_proxy }
    }
}

impl RenderNotifier for Notifier {
    fn clone(&self) -> Box<dyn RenderNotifier> {
        Box::new(Notifier {
            events_proxy: self.events_proxy.clone(),
        })
    }

    fn wake_up(&self, _composite_needed: bool) {
        #[cfg(not(target_os = "android"))]
        let _ = self.events_proxy.send_event(());
    }

    fn new_frame_ready(&self,
                       _: DocumentId,
                       _: FramePublishId,
                       params: &FrameReadyParams) {
        self.wake_up(params.render);
    }
}

struct Window {
    events_loop: winit::event_loop::EventLoop<()>, //TODO: share events loop?
    context: Option<glutin::WindowedContext<NotCurrent>>,
    renderer: webrender::Renderer,
    name: &'static str,
    pipeline_id: PipelineId,
    document_id: DocumentId,
    epoch: Epoch,
    api: RenderApi,
    font_instance_key: FontInstanceKey,
}

impl Window {
    fn new(name: &'static str, clear_color: ColorF) -> Self {
        let events_loop = winit::event_loop::EventLoop::new();
        let window_builder = winit::window::WindowBuilder::new()
            .with_title(name)
            .with_inner_size(LogicalSize::new(800. as f64, 600. as f64));
        let context = glutin::ContextBuilder::new()
            .with_gl(glutin::GlRequest::GlThenGles {
                opengl_version: (3, 2),
                opengles_version: (3, 0),
            })
            .build_windowed(window_builder, &events_loop)
            .unwrap();

        let context = unsafe { context.make_current().unwrap() };

        let gl = match context.get_api() {
            glutin::Api::OpenGl => unsafe {
                gl::GlFns::load_with(|symbol| context.get_proc_address(symbol) as *const _)
            },
            glutin::Api::OpenGlEs => unsafe {
                gl::GlesFns::load_with(|symbol| context.get_proc_address(symbol) as *const _)
            },
            glutin::Api::WebGl => unimplemented!(),
        };

        let opts = webrender::WebRenderOptions {
            clear_color,
            ..webrender::WebRenderOptions::default()
        };

        let device_size = {
            let size = context
                .window()
                .inner_size();
            DeviceIntSize::new(size.width as i32, size.height as i32)
        };
        let notifier = Box::new(Notifier::new(events_loop.create_proxy()));
        let (renderer, sender) = webrender::create_webrender_instance(gl.clone(), notifier, opts, None).unwrap();
        let mut api = sender.create_api();
        let document_id = api.add_document(device_size);

        let epoch = Epoch(0);
        let pipeline_id = PipelineId(0, 0);
        let mut txn = Transaction::new();

        let font_key = api.generate_font_key();
        let font_bytes = load_file("../wrench/reftests/text/FreeSans.ttf");
        txn.add_raw_font(font_key, font_bytes, 0);

        let font_instance_key = api.generate_font_instance_key();
        txn.add_font_instance(font_instance_key, font_key, 32.0, None, None, Vec::new());

        api.send_transaction(document_id, txn);

        Window {
            events_loop,
            context: Some(unsafe { context.make_not_current().unwrap() }),
            renderer,
            name,
            epoch,
            pipeline_id,
            document_id,
            api,
            font_instance_key,
        }
    }

    fn tick(&mut self) -> bool {
        let mut do_exit = false;
        let my_name = &self.name;
        let renderer = &mut self.renderer;
        let api = &mut self.api;

        self.events_loop.run_return(|global_event, _elwt, control_flow| {
            *control_flow = winit::event_loop::ControlFlow::Exit;
            match global_event {
                winit::event::Event::WindowEvent { event, .. } => match event {
                    winit::event::WindowEvent::CloseRequested |
                    winit::event::WindowEvent::KeyboardInput {
                        input: winit::event::KeyboardInput {
                            virtual_keycode: Some(winit::event::VirtualKeyCode::Escape),
                            ..
                        },
                        ..
                    } => {
                        do_exit = true
                    }
                    winit::event::WindowEvent::KeyboardInput {
                        input: winit::event::KeyboardInput {
                            state: winit::event::ElementState::Pressed,
                            virtual_keycode: Some(winit::event::VirtualKeyCode::P),
                            ..
                        },
                        ..
                    } => {
                        println!("set flags {}", my_name);
                        api.send_debug_cmd(DebugCommand::SetFlags(DebugFlags::PROFILER_DBG))
                    }
                    _ => {}
                }
                _ => {}
            }
        });
        if do_exit {
            return true
        }

        let context = unsafe { self.context.take().unwrap().make_current().unwrap() };
        let device_pixel_ratio = context.window().scale_factor() as f32;
        let device_size = {
            let size = context
                .window()
                .inner_size();
            DeviceIntSize::new(size.width as i32, size.height as i32)
        };
        let layout_size = device_size.to_f32() / euclid::Scale::new(device_pixel_ratio);
        let mut txn = Transaction::new();
        let mut builder = DisplayListBuilder::new(self.pipeline_id);
        let space_and_clip = SpaceAndClipInfo::root_scroll(self.pipeline_id);
        builder.begin();

        let bounds = LayoutRect::from_size(layout_size);
        builder.push_simple_stacking_context(
            bounds.min,
            space_and_clip.spatial_id,
            PrimitiveFlags::IS_BACKFACE_VISIBLE,
        );

        builder.push_rect(
            &CommonItemProperties::new(
                LayoutRect::from_origin_and_size(
                    LayoutPoint::new(100.0, 200.0),
                    LayoutSize::new(100.0, 200.0),
                ),
                space_and_clip,
            ),
            LayoutRect::from_origin_and_size(
                LayoutPoint::new(100.0, 200.0),
                LayoutSize::new(100.0, 200.0),
            ),
            ColorF::new(0.0, 1.0, 0.0, 1.0));

        let text_bounds = LayoutRect::from_origin_and_size(
            LayoutPoint::new(100.0, 50.0),
            LayoutSize::new(700.0, 200.0)
        );
        let glyphs = vec![
            GlyphInstance {
                index: 48,
                point: LayoutPoint::new(100.0, 100.0),
            },
            GlyphInstance {
                index: 68,
                point: LayoutPoint::new(150.0, 100.0),
            },
            GlyphInstance {
                index: 80,
                point: LayoutPoint::new(200.0, 100.0),
            },
            GlyphInstance {
                index: 82,
                point: LayoutPoint::new(250.0, 100.0),
            },
            GlyphInstance {
                index: 81,
                point: LayoutPoint::new(300.0, 100.0),
            },
            GlyphInstance {
                index: 3,
                point: LayoutPoint::new(350.0, 100.0),
            },
            GlyphInstance {
                index: 86,
                point: LayoutPoint::new(400.0, 100.0),
            },
            GlyphInstance {
                index: 79,
                point: LayoutPoint::new(450.0, 100.0),
            },
            GlyphInstance {
                index: 72,
                point: LayoutPoint::new(500.0, 100.0),
            },
            GlyphInstance {
                index: 83,
                point: LayoutPoint::new(550.0, 100.0),
            },
            GlyphInstance {
                index: 87,
                point: LayoutPoint::new(600.0, 100.0),
            },
            GlyphInstance {
                index: 17,
                point: LayoutPoint::new(650.0, 100.0),
            },
        ];

        builder.push_text(
            &CommonItemProperties::new(
                text_bounds,
                space_and_clip,
            ),
            text_bounds,
            &glyphs,
            self.font_instance_key,
            ColorF::new(1.0, 1.0, 0.0, 1.0),
            None,
        );

        builder.pop_stacking_context();

        txn.set_display_list(
            self.epoch,
            builder.end(),
        );
        txn.set_root_pipeline(self.pipeline_id);
        txn.generate_frame(0, true, RenderReasons::empty());
        api.send_transaction(self.document_id, txn);

        renderer.update();
        renderer.render(device_size, 0).unwrap();
        context.swap_buffers().ok();

        self.context = Some(unsafe { context.make_not_current().unwrap() });

        false
    }

    fn deinit(self) {
        self.renderer.deinit();
    }
}

fn main() {
    let mut win1 = Window::new("window1", ColorF::new(0.3, 0.0, 0.0, 1.0));
    let mut win2 = Window::new("window2", ColorF::new(0.0, 0.3, 0.0, 1.0));

    loop {
        if win1.tick() {
            break;
        }
        if win2.tick() {
            break;
        }
    }

    win1.deinit();
    win2.deinit();
}

fn load_file(name: &str) -> Vec<u8> {
    let mut file = File::open(name).unwrap();
    let mut buffer = vec![];
    file.read_to_end(&mut buffer).unwrap();
    buffer
}
