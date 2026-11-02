/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::HashMap;

use webrender::scene::Scene;
use webrender_api::{units::LayoutRect, BorderDetails, BorderStyle, BuiltDisplayList};
use webrender_api::{ColorF, DisplayItem, PipelineId, SpatialId};
use webrender_api::{ClipId, SpatialTreeItem, ClipChainId};

fn color_to_string(
    color: ColorF,
) -> String {
    match (color.r, color.g, color.b, color.a) {
        (1.0, 0.0, 0.0, 1.0) => "red".into(),
        _ => {
            format!("{} {} {} {}",
                color.r * 255.0,
                color.g * 255.0,
                color.b * 255.0,
                color.a,
            )
        }
    }
}

fn color_to_string_array(
    color: ColorF,
) -> String {
    match (color.r, color.g, color.b, color.a) {
        (1.0, 0.0, 0.0, 1.0) => "red".into(),
        _ => {
            format!("{}, {}, {}, {}",
                color.r * 255.0,
                color.g * 255.0,
                color.b * 255.0,
                color.a,
            )
        }
    }
}

fn style_to_string(
    style: BorderStyle,
) -> String {
    match style {
        BorderStyle::None => "none",
        BorderStyle::Solid => "solid",
        BorderStyle::Double => "double",
        BorderStyle::Dotted => "dotted",
        BorderStyle::Dashed => "dashed",
        BorderStyle::Hidden => "hidden",
        BorderStyle::Ridge => "ridge",
        BorderStyle::Inset => "inset",
        BorderStyle::Outset => "outset",
        BorderStyle::Groove => "groove",
    }.into()
}

#[derive(Debug)]
enum SpatialNodeKind {
    Reference {
    },
    Scroll {
    },
    Sticky {
    },
}

#[derive(Debug)]
struct SpatialNode {
    wrench_id: u64,
}

struct YamlWriter {
    out: String,
    indent: String,
    spatial_nodes: HashMap<SpatialId, SpatialNode>,
    clip_id_map: HashMap<ClipId, u64>,
    clipchain_id_map: HashMap<ClipChainId, u64>,
    next_wrench_id: u64,
}

impl YamlWriter {
    fn new() -> Self {
        YamlWriter {
            out: String::new(),
            indent: String::new(),
            spatial_nodes: HashMap::new(),
            next_wrench_id: 2,
            clip_id_map: HashMap::new(),
            clipchain_id_map: HashMap::new(),
        }
    }

    fn push_level(&mut self) {
        self.indent.push_str("  ");
    }

    fn pop_level(&mut self) {
        self.indent.truncate(self.indent.len() - 2);
    }

    fn add_clip_id(
        &mut self,
        clip_id: ClipId
    ) -> u64 {
        let id = self.next_wrench_id;
        self.next_wrench_id += 1;

        let _prev = self.clip_id_map.insert(clip_id, id);
        assert!(_prev.is_none());

        id
    }

    fn add_clipchain_id(
        &mut self,
        clipchain_id: ClipChainId
    ) -> u64 {
        let id = self.next_wrench_id;
        self.next_wrench_id += 1;

        let _prev = self.clipchain_id_map.insert(clipchain_id, id);
        assert!(_prev.is_none());

        id
    }

    fn add_and_write_spatial_node(
        &mut self,
        spatial_id: SpatialId,
        parent: SpatialId,
        kind: SpatialNodeKind,
    ) {
        match kind {
            SpatialNodeKind::Reference {} => {
                self.write_line("- type: reference-frame");
                self.push_level();
                self.write_line(&format!("id: {}", self.next_wrench_id));
                if let Some(parent) = self.spatial_nodes.get(&parent) {
                    self.write_line(&format!("spatial-id: {}", parent.wrench_id));
                }
                self.pop_level();
            }
            SpatialNodeKind::Scroll {} => {
                let parent_id = self.spatial_nodes[&parent].wrench_id;

                self.write_line("- type: scroll-frame");
                self.push_level();
                self.write_line(&format!("id: {}", self.next_wrench_id));
                self.write_bounds(LayoutRect::zero());
                self.write_line(&format!("spatial-id: {}", parent_id));
                self.pop_level();
            }
            SpatialNodeKind::Sticky {} => {
                let parent_id = self.spatial_nodes[&parent].wrench_id;

                self.write_line("- type: sticky-frame");
                self.push_level();
                self.write_line(&format!("id: {}", self.next_wrench_id));
                self.write_line(&format!("spatial-id: {}", parent_id));
                self.write_bounds(LayoutRect::zero());
                self.pop_level();
            }
        }

        let _prev = self.spatial_nodes.insert(
            spatial_id,
            SpatialNode {
                wrench_id: self.next_wrench_id,
            },
        );
        assert!(_prev.is_none());
        self.next_wrench_id += 1;
    }

    fn write_color(
        &mut self,
        color: ColorF,
    ) {
        self.write_line(
            &format!("color: {}", color_to_string(color))
        );
    }

    fn write_rect(
        &mut self,
        tag: &str,
        rect: LayoutRect,
    ) {
        self.write_line(
            &format!("{}: {} {} {} {}",
                tag,
                rect.min.x,
                rect.min.y,
                rect.width(),
                rect.height(),
            )
        );
    }

    fn write_bounds(
        &mut self,
        bounds: LayoutRect,
    ) {
        self.write_rect("bounds", bounds);
    }

    fn maybe_write_clip_rect(
        &mut self,
        bounds: LayoutRect,
        clip_rect: LayoutRect,
    ) {
        if bounds != clip_rect {
            self.write_rect("clip-rect", clip_rect);
        }
    }

    fn create_savepoint(
        &mut self,
    ) -> (usize, usize) {
        (self.out.len(), self.indent.len())
    }

    fn restore_savepoint(
        &mut self,
        p: (usize, usize),
    ) {
        self.out.truncate(p.0);
        self.indent.truncate(p.1);
    }

    fn write_line(
        &mut self,
        s: &str,
    ) {
        self.out.push_str(&self.indent);
        self.out.push_str(s);
        self.out.push_str("\n");
    }

    fn write_spatial_id(
        &mut self,
        id: SpatialId,
    ) {
        let spatial_node = self.spatial_nodes
            .get(&id)
            .expect(&format!("unknown spatial node {:?}", id));

        self.write_line(&format!("spatial-id: {}", spatial_node.wrench_id));
    }

    fn write_clip_chain_id(
        &mut self,
        id: ClipChainId,
    ) {
        if id != ClipChainId::INVALID {
            let clip_chain_id = self.clipchain_id_map[&id];
            self.write_line(&format!("clip-chain: {}", clip_chain_id));
        }
    }

    fn build_spatial_tree(
        &mut self,
        dl: &BuiltDisplayList,
        pipeline_id: PipelineId,
    ) {
        // Insert root ref + scroll frames
        self.add_and_write_spatial_node(
            SpatialId::root_reference_frame(pipeline_id),
            SpatialId::root_reference_frame(pipeline_id),
            SpatialNodeKind::Reference {  },
        );
        self.add_and_write_spatial_node(
            SpatialId::root_scroll_node(pipeline_id),
            SpatialId::root_reference_frame(pipeline_id),
            SpatialNodeKind::Scroll {  },
        );

        dl.iter_spatial_tree(|item| {
            match item {
                SpatialTreeItem::ScrollFrame(descriptor) => {
                    self.add_and_write_spatial_node(
                        descriptor.scroll_frame_id,
                        descriptor.parent_space,
                        SpatialNodeKind::Scroll {
                        },
                    );
                }
                SpatialTreeItem::ReferenceFrame(descriptor) => {
                    self.add_and_write_spatial_node(
                        descriptor.reference_frame.id,
                        descriptor.parent_spatial_id,
                        SpatialNodeKind::Reference {
                        },
                    );
                }
                SpatialTreeItem::StickyFrame(descriptor) => {
                    self.add_and_write_spatial_node(
                        descriptor.id,
                        descriptor.parent_spatial_id,
                        SpatialNodeKind::Sticky {
                        },
                    );
                }
                SpatialTreeItem::Invalid => {
                    unreachable!();
                }
            }
        });
    }

    fn write_pipeline(
        &mut self,
        scene: &Scene,
        pipeline_id: PipelineId,
    ) -> Result<(), String> {
        enum ContextKind {
            Root,
            StackingContext {
                // sc_info: StackingContextInfo,
            },
        }
        struct BuildContext {
            kind: ContextKind,
        }

        let pipeline = &scene.pipelines[&pipeline_id];

        self.build_spatial_tree(
            &pipeline.display_list,
            pipeline_id,
        );

        let mut stack = vec![BuildContext {
            kind: ContextKind::Root,
        }];
        let mut traversal = pipeline.display_list.iter();

        'outer: while let Some(bc) = stack.pop() {
            loop {
                let item = match traversal.next() {
                    Some(item) => item,
                    None => break,
                };

                match item.item() {
                    DisplayItem::PushStackingContext(info) => {
                        self.write_line("- type: stacking-context");
                        self.push_level();
                        self.write_spatial_id(info.spatial_id);
                        if let Some(clip_chain_id) = info.stacking_context.clip_chain_id {
                            self.write_clip_chain_id(clip_chain_id);
                        }
                        self.write_line(
                            &format!("bounds: {} {} 0 0",
                                0.0, //info.origin.x + info.ref_frame_offset.x,
                                0.0, //info.origin.y + info.ref_frame_offset.y,
                            )
                        );
                        self.write_line("items:");
                        self.push_level();

                        let new_context = BuildContext {
                            kind: ContextKind::StackingContext {
                                // sc_info,
                            },
                        };
                        stack.push(bc);
                        stack.push(new_context);

                        traversal = item.sub_iter();
                        continue 'outer;
                    }
                    DisplayItem::PopStackingContext => {
                        self.pop_level();
                        self.pop_level();
                    }
                    DisplayItem::Iframe(info) => {
                        self.write_line("- type: iframe");
                        self.push_level();
                        self.write_spatial_id(info.space_and_clip.spatial_id);
                        self.write_clip_chain_id(info.space_and_clip.clip_chain_id);
                        self.write_bounds(info.bounds);
                        self.maybe_write_clip_rect(info.bounds, info.clip_rect);
                        self.write_line(&format!("id: [{}, {}]",
                            info.pipeline_id.0,
                            info.pipeline_id.1,
                        ));
                        self.pop_level();
                    }
                    DisplayItem::Rectangle(info) => {
                        self.write_line("- type: rect");
                        self.push_level();
                        self.write_spatial_id(info.spatial_id);
                        self.write_clip_chain_id(info.clip_chain_id);
                        // The bounds, clip rect and colour live in the content
                        // builder's interner, which a scene capture does not
                        // hold, so write placeholders. See
                        // webrender/doc/dl-builder-interning.md.
                        self.write_bounds(LayoutRect::zero());
                        self.write_color(ColorF::new(0.0, 0.0, 1.0, 0.5));
                        self.pop_level();
                    }
                    DisplayItem::Text(info) => {
                        self.write_line("- type: rect");
                        self.push_level();
                        self.write_spatial_id(info.common.spatial_id);
                        self.write_clip_chain_id(info.common.clip_chain_id);
                        self.write_bounds(info.bounds);
                        self.maybe_write_clip_rect(info.bounds, info.common.clip_rect);
                        self.write_color(ColorF::new(1.0, 0.0, 0.0, 0.5));
                        self.pop_level();
                    }
                    DisplayItem::Border(info) => {
                        let sp = self.create_savepoint();

                        self.write_line("- type: border");
                        self.push_level();
                        self.write_spatial_id(info.common.spatial_id);
                        self.write_clip_chain_id(info.common.clip_chain_id);
                        self.maybe_write_clip_rect(info.bounds, info.common.clip_rect);
                        self.write_bounds(info.bounds);

                        match info.details {
                            BorderDetails::Normal(border) => {
                                self.write_line("border-type: normal");

                                let colors = [
                                    border.top.color,
                                    border.right.color,
                                    border.bottom.color,
                                    border.left.color,
                                ];

                                if colors.iter().all(|c| c.a == 0.0) {
                                    self.restore_savepoint(sp);
                                    continue;
                                }

                                if colors.iter().all(|c| *c == border.top.color) {
                                    self.write_color(border.top.color);
                                } else {
                                    self.write_line(&format!(
                                            "color: [ [{}], [{}], [{}], [{}] ]",
                                            color_to_string_array(colors[0]),
                                            color_to_string_array(colors[1]),
                                            color_to_string_array(colors[2]),
                                            color_to_string_array(colors[3]),
                                        )
                                    );
                                }

                                let styles = [
                                    border.top.style,
                                    border.right.style,
                                    border.bottom.style,
                                    border.left.style,
                                ];

                                if styles.iter().all(|s| *s == border.top.style) {
                                    self.write_line(&format!(
                                            "style: {}", style_to_string(styles[0]),
                                        )
                                    );
                                } else {
                                    self.write_line(&format!(
                                            "style: [ {}, {}, {}, {} ]",
                                            style_to_string(styles[0]),
                                            style_to_string(styles[1]),
                                            style_to_string(styles[2]),
                                            style_to_string(styles[3]),
                                        )
                                    );
                                }

                                self.write_line("width: [1, 1, 1, 1]");

                                if !border.radius.is_zero() {
                                    self.write_line("radius: {");
                                    self.push_level();
                                    self.write_line(
                                        &format!("top-left: [{}, {}],",
                                            border.radius.top_left.width,
                                            border.radius.top_left.height,
                                        )
                                    );
                                    self.write_line(
                                        &format!("top-right: [{}, {}],",
                                            border.radius.top_right.width,
                                            border.radius.top_right.height,
                                        )
                                    );
                                    self.write_line(
                                        &format!("bottom-left: [{}, {}],",
                                            border.radius.bottom_left.width,
                                            border.radius.bottom_left.height,
                                        )
                                    );
                                    self.write_line(
                                        &format!("bottom-right: [{}, {}],",
                                            border.radius.bottom_right.width,
                                            border.radius.bottom_right.height,
                                        )
                                    );
                                    self.pop_level();
                                    self.write_line("}");
                                }
                            }
                            BorderDetails::NinePatch(..) => {
                                todo!();
                            }
                        }

                        self.pop_level();
                    }
                    DisplayItem::Image(info) => {
                        self.write_line("- type: image");
                        self.push_level();
                        self.write_spatial_id(info.spatial_id);
                        self.write_clip_chain_id(info.clip_chain_id);
                        // The bounds live in the content builder's interner,
                        // like a rectangle's; write placeholders.
                        self.write_bounds(LayoutRect::zero());
                        self.write_line("src: checkerboard(2,8,8,1,1)");
                        self.pop_level();
                    }
                    DisplayItem::RectClip(info) => {
                        let clip_id = self.add_clip_id(info.id);
                        self.write_line("- type: clip");
                        self.push_level();
                        self.write_line(&format!("id: {}", clip_id));
                        self.write_spatial_id(info.spatial_id);
                        self.write_rect("bounds", info.clip_rect);
                        self.pop_level();
                    }
                    DisplayItem::ImageMaskClip(info) => {
                        let clip_id = self.add_clip_id(info.id);
                        self.write_line("- type: clip");
                        self.push_level();
                        self.write_line(&format!("id: {}", clip_id));
                        self.write_spatial_id(info.spatial_id);
                        self.write_rect("bounds", info.image_mask.rect);
                        self.pop_level();
                    }
                    DisplayItem::PathClip(info) => {
                        let clip_id = self.add_clip_id(info.id);
                        self.write_line("- type: clip");
                        self.push_level();
                        self.write_line(&format!("id: {}", clip_id));
                        self.write_spatial_id(info.spatial_id);
                        self.write_rect("bounds", info.rect);
                        self.pop_level();
                    }
                    DisplayItem::RoundedRectClip(info) => {
                        let clip_id = self.add_clip_id(info.id);
                        self.write_line("- type: clip");
                        self.push_level();
                        self.write_line(&format!("id: {}", clip_id));
                        self.write_spatial_id(info.spatial_id);
                        self.write_line("complex:");
                        self.push_level();
                        self.write_rect("- rect", info.clip.rect);
                        self.push_level();
                        self.write_line("radius: {");
                        self.push_level();
                        self.write_line(
                            &format!("top-left: [{}, {}],",
                            info.clip.radii.top_left.width,
                            info.clip.radii.top_left.height,
                        ));
                        self.write_line(
                            &format!("top-right: [{}, {}],",
                            info.clip.radii.top_right.width,
                            info.clip.radii.top_right.height,
                        ));
                        self.write_line(
                            &format!("bottom-right: [{}, {}],",
                            info.clip.radii.bottom_right.width,
                            info.clip.radii.bottom_right.height,
                        ));
                        self.write_line(
                            &format!("bottom-left: [{}, {}],",
                            info.clip.radii.bottom_left.width,
                            info.clip.radii.bottom_left.height,
                        ));
                        self.pop_level();
                        self.write_line("}");
                        self.pop_level();
                        self.pop_level();
                        self.pop_level();
                    }
                    DisplayItem::ClipChain(info) => {
                        let clipchain_id = self.add_clipchain_id(info.id);
                        self.write_line("- type: clip-chain");
                        self.push_level();
                        self.write_line(&format!("id: {}", clipchain_id));
                        let mut clips = String::new();
                        clips.push_str("clips: [");
                        for id in item.clip_chain_items().iter() {
                            clips.push_str(&format!("{}, ", self.clip_id_map[&id]))
                        }
                        clips.push_str("]");
                        self.write_line(&clips);
                        self.pop_level();
                    }

                    // TODO(gw): Ignored elements - we should as support for
                    //           these as needed.
                    DisplayItem::SetGradientStops => {}
                    DisplayItem::SetFilterOps => {}
                    DisplayItem::SetFilterData => {}
                    DisplayItem::SetPoints => {}
                    DisplayItem::YuvImage(..) => {}
                    DisplayItem::BackdropFilter(..) => {}
                    DisplayItem::Gradient(..) => {}
                    DisplayItem::RadialGradient(..) => {}
                    DisplayItem::ConicGradient(..) => {}
                    DisplayItem::Line(..) => {}
                    DisplayItem::HitTest(..) => {}
                    DisplayItem::DebugMarker(..) => {}
                    DisplayItem::BoxShadow(..) => {}
                };
            }

            match bc.kind {
                ContextKind::Root => {}
                ContextKind::StackingContext { } => {
                    // self.pop_stacking_context(sc_info);
                }
            }
        }

        assert!(stack.is_empty());

        Ok(())
    }

    fn write_scene(
        mut self,
        scene: &Scene
    ) -> Result<String, String> {
        self.write_line(&format!("# process-capture"));
        self.write_line("---");
        self.write_line("root:");
        self.push_level();
        self.write_line("items:");
        self.push_level();

        if let Some(root_pipeline_id) = scene.root_pipeline_id {
            self.write_pipeline(scene, root_pipeline_id)?;
        }

        self.pop_level();
        self.pop_level();
        assert!(self.indent.is_empty());

        if scene.pipelines.len() > 1 {
            self.write_line("pipelines:");
            self.push_level();
            for (id, _) in &scene.pipelines {
                if Some(*id) == scene.root_pipeline_id {
                    continue;
                }

                self.write_line(&format!("- id: [{}, {}]", id.0, id.1));
                self.push_level();
                self.write_line("items:");
                self.push_level();
                self.write_pipeline(scene, *id)?;
                self.pop_level();
                self.pop_level();
            }
            self.pop_level();
        }

        Ok(self.out)
    }
}

pub fn scene_to_yaml(
    scene: &Scene,
) -> Result<String, String> {
    let writer = YamlWriter::new();

    writer.write_scene(scene)
}
