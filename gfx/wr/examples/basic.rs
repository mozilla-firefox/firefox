/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

extern crate euclid;
extern crate gleam;
extern crate glutin;
extern crate webrender;
extern crate winit;

#[path = "common/boilerplate.rs"]
mod boilerplate;

use crate::boilerplate::{Example, HandyDandyRectBuilder};
use euclid::vec2;
use webrender::ShaderPrecacheFlags;
use webrender::api::*;
use webrender::render_api::*;
use webrender::api::units::*;

fn main() {
    let mut app = App {
    };
    boilerplate::main_wrapper(&mut app, None);
}

struct App {
}

impl Example for App {
    // Make this the only example to test all shaders for compile errors.
    const PRECACHE_SHADER_FLAGS: ShaderPrecacheFlags = ShaderPrecacheFlags::FULL_COMPILE;

    fn render(
        &mut self,
        _api: &mut RenderApi,
        builder: &mut DisplayListBuilder,
        _txn: &mut Transaction,
        _: DeviceIntSize,
        pipeline_id: PipelineId,
        _document_id: DocumentId,
    ) {
        let content_bounds = LayoutRect::from_size(LayoutSize::new(800.0, 600.0));
        let root_space_and_clip = SpaceAndClipInfo::root_scroll(pipeline_id);
        let spatial_id = root_space_and_clip.spatial_id;

        builder.push_simple_stacking_context(
            spatial_id,
            PrimitiveFlags::IS_BACKFACE_VISIBLE,
        );

        let complex = ComplexClipRegion::new(
            (50, 50).to(150, 150),
            BorderRadius::uniform(20.0),
            LayoutSideOffsets::zero(),
            ClipMode::Clip
        );
        let clip_id = builder.define_clip_rounded_rect(
            root_space_and_clip.spatial_id,
            complex,
        );
        let clip_chain_id = builder.define_clip_chain(None, [clip_id]);

        builder.push_rect(
            &CommonItemProperties::new(
                (100, 100).to(200, 200),
                SpaceAndClipInfo { spatial_id, clip_chain_id },
            ),
            (100, 100).to(200, 200),
            ColorF::new(0.0, 1.0, 0.0, 1.0),
        );

        builder.push_rect(
            &CommonItemProperties::new(
                (250, 100).to(350, 200),
                SpaceAndClipInfo { spatial_id, clip_chain_id },
            ),
            (250, 100).to(350, 200),
            ColorF::new(0.0, 1.0, 0.0, 1.0),
        );
        let border_side = BorderSide {
            color: ColorF::new(0.0, 0.0, 1.0, 1.0),
            style: BorderStyle::Groove,
        };
        let border_widths = LayoutSideOffsets::new_all_same(10.0);
        let border_details = BorderDetails::Normal(NormalBorder {
            top: border_side,
            right: border_side,
            bottom: border_side,
            left: border_side,
            radius: BorderRadius::uniform(20.0),
            inset: LayoutSideOffsets::zero(),
            do_aa: true,
        });

        let bounds = (100, 100).to(200, 200);
        builder.push_border(
            &CommonItemProperties::new(
                bounds,
                SpaceAndClipInfo { spatial_id, clip_chain_id },
            ),
            bounds,
            border_widths,
            border_details,
            &[],
        );

        if false {
            // draw box shadow?
            let simple_box_bounds = (20, 200).by(50, 50);
            let offset = vec2(10.0, 10.0);
            let color = ColorF::new(1.0, 1.0, 1.0, 1.0);
            let blur_radius = 0.0;
            let spread_radius = 0.0;
            let simple_border_radius = 8.0;
            let box_shadow_type = BoxShadowClipMode::Inset;

            builder.push_box_shadow(
                &CommonItemProperties::new(content_bounds, root_space_and_clip),
                simple_box_bounds,
                offset,
                color,
                blur_radius,
                spread_radius,
                BorderRadius::uniform(simple_border_radius),
                BorderRadius::uniform(simple_border_radius - spread_radius),
                box_shadow_type,
            );
        }

        builder.pop_stacking_context();
    }
}
