/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Debug-only inspection and per-primitive modifications of a built scene,
//! driven by the remote debugger (see `debugger.rs` and the wrshell scene tree
//! panel). A client can query the picture / primitive tree, disable primitive
//! instances and highlight one of them; the modifications are applied during
//! frame building, without rebuilding the scene.

use api::ColorF;
#[cfg(feature = "debugger")]
use api::debugger::{SceneDebugNode, SceneDebugTree};
#[cfg(feature = "debugger")]
use api::units::{DevicePixel, DeviceRect, LayoutPixel, LayoutRect};
use crate::debug_colors;
use crate::internal_types::FastHashSet;
#[cfg(feature = "debugger")]
use crate::internal_types::FastHashMap;
#[cfg(feature = "debugger")]
use crate::prim_store::{PictureIndex, PrimitiveFrameScratch, PrimitiveKind};
#[cfg(feature = "debugger")]
use crate::render_backend::{DataStores, DlStores};
#[cfg(feature = "debugger")]
use crate::scene::{BuiltScene, SceneProperties};
#[cfg(feature = "debugger")]
use crate::space::SpaceMapper;
#[cfg(feature = "debugger")]
use crate::spatial_tree::{SpatialNodeIndex, SpatialTree};
#[cfg(feature = "debugger")]
use crate::visibility::DrawState;

/// How the highlighted primitive is shown on screen.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HighlightMode {
    /// Draw an opaque quad in place of the primitive, honoring its clips,
    /// transform and position in the z-order.
    Replace,
    /// Outline the primitive's device rect on top of the composited frame, so
    /// that it is visible even when occluded while its content stays visible.
    Overlay,
}

#[derive(Debug, Clone)]
pub struct SceneDebugOverride {
    /// Primitive instance index of the highlighted primitive, or
    /// `NO_HIGHLIGHT`.
    /// We use a plain integer rather than an `Option` because the
    /// latter ran into valgrind issues.
    highlighted: u32,
    pub highlight_mode: HighlightMode,
    /// Primitive instance indices skipped during frame building. Skipping a
    /// picture primitive skips its whole subtree.
    pub disabled: FastHashSet<u32>,
}

impl SceneDebugOverride {
    pub const HIGHLIGHT_COLOR: ColorF = debug_colors::HOTPINK;
    const NO_HIGHLIGHT: u32 = u32::MAX;

    pub fn empty() -> Self {
        SceneDebugOverride {
            highlighted: Self::NO_HIGHLIGHT,
            highlight_mode: HighlightMode::Replace,
            disabled: FastHashSet::default(),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.highlighted == Self::NO_HIGHLIGHT && self.disabled.is_empty()
    }

    pub fn is_disabled(&self, prim_instance_index: usize) -> bool {
        !self.disabled.is_empty() && self.disabled.contains(&(prim_instance_index as u32))
    }

    pub fn highlight(&self, prim_instance_index: usize) -> Option<HighlightMode> {
        if self.highlighted as usize == prim_instance_index {
            Some(self.highlight_mode)
        } else {
            None
        }
    }

    /// Validate an override sent by the remote debugger against the scene it
    /// will be applied to: it must target the current scene generation and
    /// only reference primitive instances that exist.
    #[cfg(feature = "debugger")]
    pub fn from_debugger(
        debug_override: &api::debugger::SceneDebugOverride,
        scene_generation: u64,
        prim_count: usize,
    ) -> Result<Self, String> {
        use api::debugger::SceneDebugHighlightMode;

        if debug_override.scene_generation != scene_generation {
            return Err(format!(
                "Scene override targets generation {} but the current scene is generation {}",
                debug_override.scene_generation,
                scene_generation,
            ));
        }

        let valid = |index: &u32| (*index as usize) < prim_count;
        if !debug_override.disabled.iter().all(valid)
            || !debug_override.highlighted.iter().all(valid)
        {
            return Err(format!(
                "Scene override references a primitive index out of range (scene has {} primitives)",
                prim_count,
            ));
        }

        Ok(SceneDebugOverride {
            highlighted: debug_override.highlighted.unwrap_or(Self::NO_HIGHLIGHT),
            highlight_mode: match debug_override.highlight_mode {
                SceneDebugHighlightMode::Replace => HighlightMode::Replace,
                SceneDebugHighlightMode::Overlay => HighlightMode::Overlay,
            },
            disabled: debug_override.disabled.iter().cloned().collect(),
        })
    }
}

/// Serialize the picture / primitive tree of a built scene for the remote
/// debugger.
///
/// `frame_scratch` is the per-frame scratch of the last frame built from the
/// scene, used to report each primitive's draw state. `device_rect` is the
/// document's device rect, used as the bounds when projecting primitive rects
/// to device space.
#[cfg(feature = "debugger")]
pub fn build_debug_tree(
    scene: &BuiltScene,
    data_stores: &DataStores,
    dl_stores: &DlStores,
    spatial_tree: &SpatialTree,
    frame_scratch: &PrimitiveFrameScratch,
    scene_properties: &SceneProperties,
    device_rect: DeviceRect,
    scene_generation: u64,
) -> SceneDebugTree {
    let mut draw_states = FastHashMap::default();
    for draw in frame_scratch.draws() {
        draw_states.entry(draw.prim_instance_index.0).or_insert(draw.state);
    }

    let mut walker = TreeWalker {
        scene,
        data_stores,
        dl_stores,
        spatial_tree,
        draw_states,
        scene_properties,
        map_to_device: SpaceMapper::new(
            spatial_tree.root_reference_frame_index(),
            device_rect,
        ),
    };

    let mut roots = Vec::new();
    for pic_index in &scene.snapshot_pictures {
        roots.push(walker.root_node(*pic_index, "SnapshotPicture"));
    }
    for pic_index in &scene.tile_cache_pictures {
        roots.push(walker.root_node(*pic_index, "TileCache"));
    }

    SceneDebugTree {
        scene_generation,
        prim_count: scene.prim_instances.len() as u32,
        roots,
    }
}

#[cfg(feature = "debugger")]
struct TreeWalker<'a> {
    scene: &'a BuiltScene,
    data_stores: &'a DataStores,
    dl_stores: &'a DlStores,
    spatial_tree: &'a SpatialTree,
    /// The state of the first draw of each primitive instance drawn by the
    /// last frame, keyed by instance index.
    draw_states: FastHashMap<u32, DrawState>,
    scene_properties: &'a SceneProperties,
    map_to_device: SpaceMapper<LayoutPixel, DevicePixel>,
}

#[cfg(feature = "debugger")]
impl<'a> TreeWalker<'a> {
    fn draw_state(&self, prim_index: usize) -> String {
        match self.draw_states.get(&(prim_index as u32)) {
            Some(state) => match state {
                DrawState::Unset => "Unset".into(),
                DrawState::Culled => "Culled".into(),
                DrawState::PassThrough => "PassThrough".into(),
                DrawState::Visible { .. } => "Visible".into(),
            },
            None => "NotDrawn".into(),
        }
    }

    fn device_rect(
        &mut self,
        local_rect: &LayoutRect,
        spatial_node_index: SpatialNodeIndex,
    ) -> Option<DeviceRect> {
        if local_rect.is_empty() {
            return None;
        }
        self.map_to_device.set_target_spatial_node(spatial_node_index, self.spatial_tree);
        self.map_to_device.map(local_rect)
    }

    fn picture_children(&mut self, pic_index: PictureIndex) -> Vec<SceneDebugNode> {
        let pic = &self.scene.prim_store.pictures[pic_index.0 as usize];
        let mut children = Vec::new();
        for cluster in &pic.prim_list.clusters {
            for prim_index in cluster.prim_range() {
                children.push(self.prim_node(prim_index, cluster.spatial_node_index));
            }
        }
        children
    }

    fn picture_detail(&self, pic_index: PictureIndex) -> String {
        let pic = &self.scene.prim_store.pictures[pic_index.0 as usize];
        let mut detail = match pic.composite_mode {
            Some(ref mode) => format!("{:?}", mode),
            None => "pass-through".to_string(),
        };
        if !pic.flags.is_empty() {
            detail.push_str(&format!(" {:?}", pic.flags));
        }
        if pic.snapshot.is_some() {
            detail.push_str(" snapshot");
        }
        detail
    }

    fn prim_node(
        &mut self,
        prim_index: usize,
        spatial_node_index: SpatialNodeIndex,
    ) -> SceneDebugNode {
        let data_stores = self.data_stores;
        let dl_stores = self.dl_stores;
        let prim = &self.scene.prim_instances[prim_index];

        let mut color = None;
        let (kind, detail, picture_index, children) = match prim.kind {
            PrimitiveKind::Picture { pic_index, .. } => (
                "Picture",
                self.picture_detail(pic_index),
                Some(pic_index),
                self.picture_children(pic_index),
            ),
            PrimitiveKind::TextRun { data_handle } => {
                let data = &data_stores.text_run[data_handle].kind;
                color = Some(data.font.color.into());
                ("TextRun", format!("{} glyphs", data.glyphs.len()), None, Vec::new())
            }
            PrimitiveKind::Rectangle { handle } => {
                let data = &dl_stores.rectangles[handle];
                color = Some(data.resolve(self.scene_properties));
                ("Rectangle", String::new(), None, Vec::new())
            }
            PrimitiveKind::Image { handle } => {
                let data = &dl_stores.images[handle];
                ("Image", format!("{:?}", data.kind.key), None, Vec::new())
            }
            PrimitiveKind::YuvImage { handle } => {
                let data = &dl_stores.yuv_images[handle];
                ("YuvImage", format!("{:?}", data.kind.format), None, Vec::new())
            }
            PrimitiveKind::LineDecoration { .. } => ("LineDecoration", String::new(), None, Vec::new()),
            PrimitiveKind::NormalBorder { .. } => ("NormalBorder", String::new(), None, Vec::new()),
            PrimitiveKind::ImageBorder { .. } => ("ImageBorder", String::new(), None, Vec::new()),
            PrimitiveKind::LinearGradient { .. } => ("LinearGradient", String::new(), None, Vec::new()),
            PrimitiveKind::RadialGradient { .. } => ("RadialGradient", String::new(), None, Vec::new()),
            PrimitiveKind::ConicGradient { .. } => ("ConicGradient", String::new(), None, Vec::new()),
            PrimitiveKind::BackdropCapture { .. } => ("BackdropCapture", String::new(), None, Vec::new()),
            PrimitiveKind::BackdropRender { pic_index, .. } => (
                "BackdropRender",
                format!("{:?}", pic_index),
                Some(pic_index),
                Vec::new(),
            ),
            PrimitiveKind::BoxShadow { handle } => {
                let data = &dl_stores.box_shadows[handle];
                color = Some(data.kind.color);
                ("BoxShadow", String::new(), None, Vec::new())
            }
        };

        let local_rect = data_stores.prim_rect(prim, dl_stores);
        let device_rect = self.device_rect(&local_rect, spatial_node_index);

        SceneDebugNode {
            prim_index: Some(prim_index as u32),
            picture_index: picture_index.map(|index| index.0),
            kind: kind.to_string(),
            detail,
            color,
            spatial_node_index: spatial_node_index.0,
            local_rect,
            device_rect,
            draw_state: self.draw_state(prim_index),
            children,
        }
    }

    fn root_node(&mut self, pic_index: PictureIndex, kind: &str) -> SceneDebugNode {
        let pic = &self.scene.prim_store.pictures[pic_index.0 as usize];
        SceneDebugNode {
            prim_index: None,
            picture_index: Some(pic_index.0),
            kind: kind.to_string(),
            detail: self.picture_detail(pic_index),
            color: None,
            spatial_node_index: pic.spatial_node_index.0,
            local_rect: LayoutRect::zero(),
            device_rect: None,
            draw_state: String::new(),
            children: self.picture_children(pic_index),
        }
    }
}
