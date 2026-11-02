/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! This example creates a 200x200 white rect and allows the user to move it
//! around by using the arrow keys and rotate with '<'/'>'.
//! It does this by using the animation API.

//! The example also features seamless opaque/transparent split of a
//! rounded cornered rectangle, which is done automatically during the
//! scene building for render optimization.

extern crate euclid;
extern crate gleam;
extern crate glutin;
extern crate webrender;
extern crate winit;

#[path = "common/boilerplate.rs"]
mod boilerplate;

use crate::boilerplate::{Example, HandyDandyRectBuilder};
use euclid::Angle;
use webrender::api::*;
use webrender::render_api::*;
use webrender::api::units::*;


struct App {
    property_key0: PropertyBindingKey<LayoutTransform>,
    property_key1: PropertyBindingKey<LayoutTransform>,
    property_key2: PropertyBindingKey<LayoutTransform>,
    opacity_key: PropertyBindingKey<f32>,
    opacity: f32,
    angle0: f32,
    angle1: f32,
    angle2: f32,
}

impl App {
    fn add_rounded_rect(
        &mut self,
        bounds: LayoutRect,
        color: ColorF,
        builder: &mut DisplayListBuilder,
        pipeline_id: PipelineId,
        property_key: PropertyBindingKey<LayoutTransform>,
        opacity_key: Option<PropertyBindingKey<f32>>,
    ) {
        let filters = match opacity_key {
            Some(opacity_key) => {
                vec![
                    FilterOp::Opacity(PropertyBinding::Binding(opacity_key, self.opacity), self.opacity),
                ]
            }
            None => {
                vec![]
            }
        };

        let spatial_id = builder.define_reference_frame(
            bounds.min,
            SpatialId::root_scroll_node(pipeline_id),
            TransformStyle::Flat,
            PropertyBinding::Binding(property_key, LayoutTransform::identity()),
            ReferenceFrameKind::Transform {
                is_2d_scale_translation: false,
                should_snap: false,
                paired_with_perspective: false,
            },
        );

        builder.push_simple_stacking_context_with_filters(
            spatial_id,
            PrimitiveFlags::IS_BACKFACE_VISIBLE,
            &filters,
            &[],
        );

        let space_and_clip = SpaceAndClipInfo {
            spatial_id,
            clip_chain_id: ClipChainId::INVALID,
        };
        let clip_bounds = LayoutRect::from_size(bounds.size());
        let complex_clip = ComplexClipRegion {
            rect: clip_bounds,
            radii: BorderRadius::uniform(30.0),
            inset: LayoutSideOffsets::zero(),
            mode: ClipMode::Clip,
        };
        let clip_id = builder.define_clip_rounded_rect(
            space_and_clip.spatial_id,
            complex_clip,
        );
        let clip_chain_id = builder.define_clip_chain(None, [clip_id]);

        // Fill it with a white rect
        builder.push_rect(
            &CommonItemProperties::new(
                LayoutRect::from_size(bounds.size()),
                SpaceAndClipInfo {
                    spatial_id,
                    clip_chain_id,
                }
            ),
            LayoutRect::from_size(bounds.size()),
            color,
        );

        builder.pop_stacking_context();
    }
}

impl Example for App {
    const WIDTH: u32 = 2048;
    const HEIGHT: u32 = 1536;

    fn render(
        &mut self,
        _api: &mut RenderApi,
        builder: &mut DisplayListBuilder,
        _txn: &mut Transaction,
        _device_size: DeviceIntSize,
        pipeline_id: PipelineId,
        _document_id: DocumentId,
    ) {
        let opacity_key = self.opacity_key;

        let bounds = (150, 150).to(250, 250);
        let key0 = self.property_key0;
        self.add_rounded_rect(
            bounds,
            ColorF::new(1.0, 0.0, 0.0, 0.5),
            builder,
            pipeline_id,
            key0,
            Some(opacity_key),
        );

        let bounds = (400, 400).to(600, 600);
        let key1 = self.property_key1;
        self.add_rounded_rect(
            bounds,
            ColorF::new(0.0, 1.0, 0.0, 0.5),
            builder,
            pipeline_id,
            key1,
            None,
        );

        let bounds = (200, 500).to(350, 580);
        let key2 = self.property_key2;
        self.add_rounded_rect(
            bounds,
            ColorF::new(0.0, 0.0, 1.0, 0.5),
            builder,
            pipeline_id,
            key2,
            None,
        );
    }

    fn on_event(
        &mut self,
        win_event: winit::event::WindowEvent,
        _window: &winit::window::Window,
        api: &mut RenderApi,
        document_id: DocumentId
    ) -> bool {
        let mut rebuild_display_list = false;

        match win_event {
            winit::event::WindowEvent::KeyboardInput {
                event: winit::event::KeyEvent {
                    state: winit::event::ElementState::Pressed,
                    ref logical_key,
                    ..
                },
                ..
            } => {
                use winit::keyboard::{Key, NamedKey};
                let (delta_angle, delta_opacity) = match logical_key.as_ref() {
                    Key::Named(NamedKey::ArrowDown) => (0.0, -0.1),
                    Key::Named(NamedKey::ArrowUp) => (0.0, 0.1),
                    Key::Named(NamedKey::ArrowRight) => (1.0, 0.0),
                    Key::Named(NamedKey::ArrowLeft) => (-1.0, 0.0),
                    Key::Character("r") | Key::Character("R") => {
                        rebuild_display_list = true;
                        (0.0, 0.0)
                    }
                    _ => return false,
                };
                // Update the transform based on the keyboard input and push it to
                // webrender using the generate_frame API. This will recomposite with
                // the updated transform.
                self.opacity += delta_opacity;
                self.angle0 += delta_angle * 0.1;
                self.angle1 += delta_angle * 0.2;
                self.angle2 -= delta_angle * 0.15;
                let xf0 = LayoutTransform::rotation(0.0, 0.0, 1.0, Angle::radians(self.angle0));
                let xf1 = LayoutTransform::rotation(0.0, 0.0, 1.0, Angle::radians(self.angle1));
                let xf2 = LayoutTransform::rotation(0.0, 0.0, 1.0, Angle::radians(self.angle2));
                let mut txn = Transaction::new();
                txn.reset_dynamic_properties();
                txn.append_dynamic_properties(
                    DynamicProperties {
                        transforms: vec![
                            PropertyValue {
                                key: self.property_key0,
                                value: xf0,
                            },
                            PropertyValue {
                                key: self.property_key1,
                                value: xf1,
                            },
                            PropertyValue {
                                key: self.property_key2,
                                value: xf2,
                            },
                        ],
                        floats: vec![
                            PropertyValue {
                                key: self.opacity_key,
                                value: self.opacity,
                            }
                        ],
                        colors: vec![],
                    },
                );
                txn.generate_frame(0, true, false, RenderReasons::empty());
                api.send_transaction(document_id, txn);
            }
            _ => (),
        }

        rebuild_display_list
    }
}

fn main() {
    let mut app = App {
        property_key0: PropertyBindingKey::new(42), // arbitrary magic number
        property_key1: PropertyBindingKey::new(44), // arbitrary magic number
        property_key2: PropertyBindingKey::new(45), // arbitrary magic number
        opacity_key: PropertyBindingKey::new(43),
        opacity: 0.5,
        angle0: 0.0,
        angle1: 0.0,
        angle2: 0.0,
    };
    boilerplate::main_wrapper(&mut app, None);
}
