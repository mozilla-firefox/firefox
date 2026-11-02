/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Internal representation of clips in WebRender.
//!
//! # Data structures
//!
//! There are a number of data structures involved in the clip module:
//!
//! - ClipStore - Main interface used by other modules.
//!
//! - ClipItem - A single clip item (e.g. a rounded rect, or a box shadow).
//!              These are an exposed API type, stored inline in a ClipNode.
//!
//! - ClipNode - A ClipItem with an attached GPU handle. The GPU handle is populated
//!              when a ClipNodeInstance is built from this node (which happens while
//!              preparing primitives for render).
//!
//! ClipNodeInstance - A ClipNode with attached positioning information (a spatial
//!                    node index). This is stored as a contiguous array of nodes
//!                    within the ClipStore.
//!
//! ```ascii
//! +-----------------------+-----------------------+-----------------------+
//! | ClipNodeInstance      | ClipNodeInstance      | ClipNodeInstance      |
//! +-----------------------+-----------------------+-----------------------+
//! | ClipItem              | ClipItem              | ClipItem              |
//! | Spatial Node Index    | Spatial Node Index    | Spatial Node Index    |
//! | GPU cache handle      | GPU cache handle      | GPU cache handle      |
//! | ...                   | ...                   | ...                   |
//! +-----------------------+-----------------------+-----------------------+
//!            0                        1                       2
//!    +----------------+    |                                              |
//!    | ClipNodeRange  |____|                                              |
//!    |    index: 1    |                                                   |
//!    |    count: 2    |___________________________________________________|
//!    +----------------+
//! ```
//!
//! - ClipNodeRange - A clip item range identifies a range of clip nodes instances.
//!                   It is stored as an (index, count).
//!
//! - ClipChainNode - A clip chain node contains a handle to an interned clip item,
//!                   positioning information (from where the clip was defined), and
//!                   an optional parent link to another ClipChainNode. ClipChainId
//!                   is an index into an array, or ClipChainId::NONE for no parent.
//!
//! ```ascii
//! +----------------+    ____+----------------+    ____+----------------+   /---> ClipChainId::NONE
//! | ClipChainNode  |   |    | ClipChainNode  |   |    | ClipChainNode  |   |
//! +----------------+   |    +----------------+   |    +----------------+   |
//! | ClipDataHandle |   |    | ClipDataHandle |   |    | ClipDataHandle |   |
//! | Spatial index  |   |    | Spatial index  |   |    | Spatial index  |   |
//! | Parent Id      |___|    | Parent Id      |___|    | Parent Id      |___|
//! | ...            |        | ...            |        | ...            |
//! +----------------+        +----------------+        +----------------+
//! ```
//!
//! - ClipChainInstance - A ClipChain that has been built for a specific primitive + positioning node.
//!
//!    When given a clip chain ID, and a local primitive rect and its spatial node, the clip module
//!    creates a clip chain instance. This is a struct with various pieces of useful information
//!    (such as a local clip rect). It also contains a (index, count)
//!    range specifier into an index buffer of the ClipNodeInstance structures that are actually relevant
//!    for this clip chain instance. The index buffer structure allows a single array to be used for
//!    all of the clip-chain instances built in a single frame. Each entry in the index buffer
//!    also stores some flags relevant to the clip node in this positioning context.
//!
//! ```ascii
//! +----------------------+
//! | ClipChainInstance    |
//! +----------------------+
//! | ...                  |
//! | local_clip_rect      |________________________________________________________________________
//! | clips_range          |_______________                                                        |
//! +----------------------+              |                                                        |
//!                                       |                                                        |
//! +------------------+------------------+------------------+------------------+------------------+
//! | ClipNodeInstance | ClipNodeInstance | ClipNodeInstance | ClipNodeInstance | ClipNodeInstance |
//! +------------------+------------------+------------------+------------------+------------------+
//! | flags            | flags            | flags            | flags            | flags            |
//! | ...              | ...              | ...              | ...              | ...              |
//! +------------------+------------------+------------------+------------------+------------------+
//! ```
//!
//! # Rendering clipped primitives
//!
//! See the [`segment` module documentation][segment.rs].
//!
//!
//! [segment.rs]: ../segment/index.html
//!

use api::{BorderRadius, ClipMode, ImageMask, ClipId, ClipChainId};
use api::{FillRule, ImageKey, ImageRendering, Path};
use api::{BorderStyle, BorderSide, ColorF, NormalBorder};
use api::units::*;
use crate::image_tiling::{self, Repetition};
use crate::border::BorderRadiusAu;
use crate::renderer::GpuBufferBuilderF;
use crate::spatial_tree::{SceneSpatialTree, SpatialTree, SpatialNodeIndex};
use crate::surface::SurfaceInfo;
use crate::ellipse::Ellipse;
use crate::dl_interner::DlHandle;
use crate::render_backend::DlStores;
use crate::intern::{self, ItemUid};
use crate::internal_types::{FastHashMap, FastHashSet};
use crate::prim_store::{VisibleMaskImageTile};
use crate::quad_clip::{QuadClipStack, QuadMaskTile};
use crate::prim_store::{ClipSnap, RectKey, PolygonKey};
use crate::render_task::RenderTask;
use crate::render_task_graph::RenderTaskGraphBuilder;
use crate::resource_cache::{ImageRequest, ResourceCache};
use crate::space::{SpaceMapper, SpaceSnapper};
use crate::space::SnapRounding;
use crate::util::{extract_inner_rect_safe, project_rect, MatrixHelpers, MaxRect, ScaleOffset};
use euclid::approxeq::ApproxEq;
use std::{iter, ops, mem};
use std::hash::{Hash, Hasher};

/// A (non-leaf) node inside a clip-tree
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(MallocSizeOf)]
pub struct ClipTreeNode {
    pub handle: ClipDataHandle,
    pub spatial_node_index: SpatialNodeIndex,
    /// Clip rect as authored by the display list (not snapped to the device
    /// pixel grid). Snapped on demand by `ClipTreeNode::snapped_clip_rect`
    /// during clip-chain construction.
    pub unsnapped_clip_rect: LayoutRect,
    /// Snap "outset". Zero means snap `unsnapped_clip_rect` directly. Non-zero
    /// anchors the clip to a snapped source rect: inflate the clip by the
    /// outset to recover that source, snap it, then inset by the outset again.
    /// Used by the box-shadow fast path so the inner `ClipOut` edge tracks the
    /// snapped element rect at a constant `spread` offset, instead of rounding
    /// each edge on its own — which would make the fake-border sides thicken at
    /// different times as the spread animates, and the ring width breathe as
    /// the element re-snaps under motion (bug 2052033).
    pub snap_outset: f32,
    /// Keep this clip at its exact position even for primitives that snap
    /// their clips. See `RectClipDisplayItem::anti_aliased`.
    pub anti_aliased: bool,
    pub parent: ClipNodeId,

    children: FastHashMap<ClipEntry, ClipNodeId>,

    // TODO(gw): Consider adding a default leaf for cases when the local_clip_rect is not relevant,
    //           that can be shared among primitives (to reduce amount of clip-chain building).
}

impl ClipTreeNode {
    /// Snap `unsnapped_clip_rect` against the current spatial tree, in this
    /// node's own spatial-node space, relative to the consuming prim's surface
    /// raster node. Built on demand during clip-chain construction: the snapped
    /// rect depends on the per-frame spatial tree, and a clip node can be shared
    /// by prims in different surfaces, so it can't be pre-snapped to a single
    /// space. Only the root sentinel node carries an `INVALID` spatial node, and
    /// that node is never visited during clip-chain construction.
    fn snapped_clip_rect(
        &self,
        snapper: &mut SpaceSnapper,
        spatial_tree: &SpatialTree,
    ) -> LayoutRect {
        debug_assert!(self.spatial_node_index != SpatialNodeIndex::INVALID);
        snapper.set_target_spatial_node(self.spatial_node_index, spatial_tree);
        let outset = self.snap_outset;
        if outset != 0.0 {
            // Anchor to the snapped source rect: inflate out by the outset to
            // recover it (e.g. the box-shadow element), snap that, then inset
            // by the outset. See `snap_outset`.
            let anchor = self.unsnapped_clip_rect.inflate(outset, outset);
            snapper.snap_rect(&anchor).inflate(-outset, -outset)
        } else {
            snapper.snap_rect(&self.unsnapped_clip_rect)
        }
    }
}

/// ID for a ClipTreeNode
#[derive(Copy, Clone, PartialEq, MallocSizeOf, Eq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipNodeId(u32);

impl ClipNodeId {
    pub const NONE: ClipNodeId = ClipNodeId(0);
}

impl std::fmt::Debug for ClipNodeId {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        if *self == Self::NONE {
            write!(f, "<none>")
        } else {
            write!(f, "#{}", self.0)
        }
    }
}

/// Snap a primitive's own local clip rect for this frame, ready to seed
/// `ClipStore::set_active_clips`. `snapper` must already target the owning
/// primitive's spatial node.
///
/// A primitive with no local clip carries `max_rect` and passes through
/// unchanged: snapping it would overflow the snap transform. Otherwise the rect
/// rounds per the prim's clip policy - nearest for snapping prims (crisp fill
/// and border edges), exact for device-space prims (bug 2050692).
pub fn snap_local_clip_rect(
    unsnapped: LayoutRect,
    snapper: &SpaceSnapper,
    clip_snap: ClipSnap,
) -> LayoutRect {
    if unsnapped == LayoutRect::max_rect() {
        return unsnapped;
    }

    match clip_snap {
        ClipSnap::Nearest => snapper.snap_rect(&unsnapped),
        ClipSnap::Exact => unsnapped,
    }
}

/// A clip-tree built during scene building and used during frame-building to apply clips to primitives.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipTree {
    nodes: Vec<ClipTreeNode>,
}

impl ClipTree {
    pub fn new() -> Self {
        ClipTree {
            nodes: vec![
                ClipTreeNode {
                    handle: ClipDataHandle::INVALID,
                    spatial_node_index: SpatialNodeIndex::INVALID,
                    unsnapped_clip_rect: LayoutRect::zero(),
                    snap_outset: 0.0,
                    anti_aliased: false,
                    children: FastHashMap::default(),
                    parent: ClipNodeId::NONE,
                }
            ],
        }
    }

    pub fn reset(&mut self) {
        self.nodes.clear();
        self.nodes.push(ClipTreeNode {
            handle: ClipDataHandle::INVALID,
            spatial_node_index: SpatialNodeIndex::INVALID,
            unsnapped_clip_rect: LayoutRect::zero(),
            snap_outset: 0.0,
            anti_aliased: false,
            children: FastHashMap::default(),
            parent: ClipNodeId::NONE,
        });

    }

    /// Add a set of clips to the provided tree node id, reusing existing
    /// nodes in the tree where possible
    fn add_impl(
        mut id: ClipNodeId,
        clips: &[ClipEntry],
        nodes: &mut Vec<ClipTreeNode>,
    ) -> ClipNodeId {
        if clips.is_empty() {
            return id;
        }
        
        for clip in clips {
            let key = *clip; // ClipEntry is Copy
            
            let node_index = nodes[id.0 as usize]
                .children
                .get(&key)
                .cloned();
            
            let node_index = match node_index {
                Some(node_index) => node_index,
                None => {
                    let node_index = ClipNodeId(nodes.len() as u32);
                    nodes[id.0 as usize].children.insert(key, node_index);
                    nodes.push(ClipTreeNode {
                        handle: key.handle,
                        spatial_node_index: key.spatial_node_index,
                        unsnapped_clip_rect: key.clip_rect.into(),
                        snap_outset: key.snap_outset,
                        anti_aliased: key.anti_aliased,
                        children: FastHashMap::default(),
                        parent: id,
                    });
                    node_index
                }
            };
            id = node_index;
        }
        id
    }

    /// Add a set of clips to the provided tree node id, reusing existing
    /// nodes in the tree where possible
    pub fn add(
        &mut self,
        root: ClipNodeId,
        clips: &[ClipEntry],
    ) -> ClipNodeId {
        ClipTree::add_impl(
            root,
            clips,
            &mut self.nodes,
        )
    }

    /// Retrieve a clip tree node by id
    pub fn get_node(&self, id: ClipNodeId) -> &ClipTreeNode {
        assert!(id != ClipNodeId::NONE);

        &self.nodes[id.0 as usize]
    }

    pub fn get_parent(&self, id: ClipNodeId) -> Option<ClipNodeId> {
        // Invalid ids point to the first item in the nodes vector which
        // has an invalid id for the parent so we don't need to handle
        // `id` being invalid separately.
        let parent = self.nodes[id.0 as usize].parent;
        if parent == ClipNodeId::NONE {
            return None;
        }

        return Some(parent)
    }

    /// Retrieve a clip tree leaf by id
    /// Debug print the clip-tree
    #[allow(unused)]
    pub fn print(&self) {
        use crate::print_tree::PrintTree;

        fn print_node<T: crate::print_tree::PrintTreePrinter>(
            id: ClipNodeId,
            nodes: &[ClipTreeNode],
            pt: &mut T,
        ) {
            let node = &nodes[id.0 as usize];

            pt.new_level(format!("{:?}", id));
            pt.add_item(format!("{:?}", node.handle));

            for child_id in node.children.values() {
                print_node(*child_id, nodes, pt);
            }

            pt.end_level();
        }

        let mut pt = PrintTree::new("clip tree");
        print_node(ClipNodeId::NONE, &self.nodes, &mut pt);
    }

    /// Find the lowest common ancestor of two clip tree nodes. This is useful
    /// to identify shared clips between primitives attached to different clip-leaves.
    pub fn find_lowest_common_ancestor(
        &self,
        mut node1: ClipNodeId,
        mut node2: ClipNodeId,
    ) -> ClipNodeId {
        // TODO(gw): Consider caching / storing the depth in the node?
        fn get_node_depth(
            id: ClipNodeId,
            nodes: &[ClipTreeNode],
        ) -> usize {
            let mut depth = 0;
            let mut current = id;

            while current != ClipNodeId::NONE {
                let node = &nodes[current.0 as usize];
                depth += 1;
                current = node.parent;
            }

            depth
        }

        let mut depth1 = get_node_depth(node1, &self.nodes);
        let mut depth2 = get_node_depth(node2, &self.nodes);

        while depth1 > depth2 {
            node1 = self.nodes[node1.0 as usize].parent;
            depth1 -= 1;
        }

        while depth2 > depth1 {
            node2 = self.nodes[node2.0 as usize].parent;
            depth2 -= 1;
        }

        while node1 != node2 {
            node1 = self.nodes[node1.0 as usize].parent;
            node2 = self.nodes[node2.0 as usize].parent;
        }

        node1
    }
}

/// A reference to an interned clip paired with the spatial node that positions it.
#[derive(Copy, Clone, PartialEq, MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipEntry {
    pub handle: ClipDataHandle,
    pub spatial_node_index: SpatialNodeIndex,
    pub clip_rect: RectKey,
    /// Propagated to `ClipTreeNode::snap_outset`. See that field. Kept as an
    /// exact `f32` (not quantized to `Au`) so the box-shadow anchor reconstructs
    /// the source rect precisely: a sub-app-unit rounding error here can flip a
    /// clip edge that lands on a device-pixel tie to the opposite pixel, leaving
    /// one side of a fake-border ring a pixel thin (bug 2051177).
    pub snap_outset: f32,
    /// Propagated to `ClipTreeNode::anti_aliased`.
    pub anti_aliased: bool,
}

impl Eq for ClipEntry {}

impl Hash for ClipEntry {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.handle.hash(state);
        self.spatial_node_index.hash(state);
        self.clip_rect.hash(state);
        self.snap_outset.to_bits().hash(state);
        self.anti_aliased.hash(state);
    }
}

/// Represents a clip-chain as defined by the public API that we decompose in to
/// the clip-tree. In future, we would like to remove this and have Gecko directly
/// build the clip-tree.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipChain {
    parent: Option<usize>,
    clips: Vec<ClipEntry>,
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipStackEntry {
    /// Cache the previous clip-chain build, since this is a common case
    last_clip_chain_cache: Option<(ClipChainId, ClipNodeId)>,

    /// Set of clips that were already seen and included in clip_node_id
    seen_clips: FastHashSet<ClipEntry>,

    /// The build clip_node_id for this level of the stack
    clip_node_id: ClipNodeId,
}

/// Used by the scene builder to build the clip-tree that is part of the built scene.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipTreeBuilder {
    /// Clips defined by the display list
    clip_map: FastHashMap<ClipId, ClipEntry>,

    /// Clip-chains defined by the display list
    clip_chains: Vec<ClipChain>,
    clip_chain_map: FastHashMap<ClipChainId, usize>,

    /// List of clips pushed/popped by grouping items, such as stacking contexts and iframes
    clip_stack: Vec<ClipStackEntry>,

    /// The tree we are building
    tree: ClipTree,

    /// A temporary buffer stored here to avoid constant heap allocs/frees
    clip_handles_buffer: Vec<ClipEntry>,
}

impl ClipTreeBuilder {
    pub fn new() -> Self {
        ClipTreeBuilder {
            clip_map: FastHashMap::default(),
            clip_chain_map: FastHashMap::default(),
            clip_chains: Vec::new(),
            clip_stack: vec![
                ClipStackEntry {
                    clip_node_id: ClipNodeId::NONE,
                    last_clip_chain_cache: None,
                    seen_clips: FastHashSet::default(),
                },
            ],
            tree: ClipTree::new(),
            clip_handles_buffer: Vec::new(),
        }
    }

    pub fn begin(&mut self) {
        self.clip_map.clear();
        self.clip_chain_map.clear();
        self.clip_chains.clear();
        self.clip_stack.clear();
        self.clip_stack.push(ClipStackEntry {
            clip_node_id: ClipNodeId::NONE,
            last_clip_chain_cache: None,
            seen_clips: FastHashSet::default(),
        });
        self.tree.reset();
        self.clip_handles_buffer.clear();
    }

    pub fn recycle_tree(&mut self, tree: ClipTree) {
        self.tree = tree;
    }

    /// Define a new rect clip
    pub fn define_rect_clip(
        &mut self,
        id: ClipId,
        handle: ClipDataHandle,
        spatial_node_index: SpatialNodeIndex,
        clip_rect: LayoutRect,
        anti_aliased: bool,
    ) {
        self.clip_map.insert(id, ClipEntry { handle, spatial_node_index, clip_rect: clip_rect.into(), snap_outset: 0.0, anti_aliased });
    }

    /// Define a new rounded rect clip
    pub fn define_rounded_rect_clip(
        &mut self,
        id: ClipId,
        handle: ClipDataHandle,
        spatial_node_index: SpatialNodeIndex,
        clip_rect: LayoutRect,
        snap_outset: f32,
        anti_aliased: bool,
    ) {
        self.clip_map.insert(id, ClipEntry { handle, spatial_node_index, clip_rect: clip_rect.into(), snap_outset, anti_aliased });
    }

    /// Define a image mask clip
    pub fn define_image_mask_clip(
        &mut self,
        id: ClipId,
        handle: ClipDataHandle,
        spatial_node_index: SpatialNodeIndex,
        clip_rect: LayoutRect,
    ) {
        self.clip_map.insert(id, ClipEntry { handle, spatial_node_index, clip_rect: clip_rect.into(), snap_outset: 0.0, anti_aliased: false });
    }

    /// Define a path clip
    pub fn define_path_clip(
        &mut self,
        id: ClipId,
        handle: ClipDataHandle,
        spatial_node_index: SpatialNodeIndex,
        clip_rect: LayoutRect,
    ) {
        self.clip_map.insert(id, ClipEntry { handle, spatial_node_index, clip_rect: clip_rect.into(), snap_outset: 0.0, anti_aliased: false });
    }

    /// Define a clip-chain
    pub fn define_clip_chain<I: Iterator<Item = ClipId>>(
        &mut self,
        id: ClipChainId,
        parent: Option<ClipChainId>,
        clips: I,
    ) {
        let parent = parent.map(|ref parent_id| match self.clip_chain_map.get(parent_id) {
            Some(index) => *index,
            None => panic!(
                "webrender: missing clip-chain {:?} in define_clip_chain parent of {:?} (defined_chains={})",
                parent_id,
                id,
                self.clip_chain_map.len(),
            ),
        });
        let index = self.clip_chains.len();
        let clips = clips.map(|clip_id| match self.clip_map.get(&clip_id) {
            Some(entry) => *entry,
            None => panic!(
                "webrender: missing clip {:?} in define_clip_chain (defined_clips={})",
                clip_id,
                self.clip_map.len(),
            ),
        }).collect();
        self.clip_chains.push(ClipChain {
            parent,
            clips,
        });
        self.clip_chain_map.insert(id, index);
    }

    /// Push a clip-chain that will be applied to any prims built prior to next pop
    pub fn push_clip_chain(
        &mut self,
        clip_chain_id: Option<ClipChainId>,
        reset_seen: bool,
        ignore_ancestor_clips: bool,
    ) {
        let (mut clip_node_id, mut seen_clips) = {
            let prev = self.clip_stack.last().unwrap();
            let clip_node_id = if ignore_ancestor_clips {
                ClipNodeId::NONE
            } else {
                prev.clip_node_id
            };
            (clip_node_id, prev.seen_clips.clone())
        };

        if let Some(clip_chain_id) = clip_chain_id {
            if clip_chain_id != ClipChainId::INVALID {
                self.clip_handles_buffer.clear();

                let clip_chain_index = match self.clip_chain_map.get(&clip_chain_id) {
                    Some(index) => *index,
                    None => panic!(
                        "webrender: missing clip-chain {:?} in push_clip_chain (defined_chains={})",
                        clip_chain_id,
                        self.clip_chain_map.len(),
                    ),
                };
                ClipTreeBuilder::add_clips(
                    clip_chain_index,
                    &mut seen_clips,
                    &mut self.clip_handles_buffer,
                    &self.clip_chains,
                );

                clip_node_id = self.tree.add(
                    clip_node_id,
                    &self.clip_handles_buffer,
                );
            }
        }

        if reset_seen {
            seen_clips.clear();
        }

        self.clip_stack.push(ClipStackEntry {
            last_clip_chain_cache: None,
            clip_node_id,
            seen_clips,
        });
    }

    /// Push a clip-id that will be applied to any prims built prior to next pop
    pub fn push_clip_id(
        &mut self,
        clip_id: ClipId,
    ) {
        let (clip_node_id, mut seen_clips) = {
            let prev = self.clip_stack.last().unwrap();
            (prev.clip_node_id, prev.seen_clips.clone())
        };

        self.clip_handles_buffer.clear();
        let clip_entry = match self.clip_map.get(&clip_id) {
            Some(entry) => *entry,
            None => panic!(
                "webrender: missing clip {:?} in push_clip_id (defined_clips={})",
                clip_id,
                self.clip_map.len(),
            ),
        };

        if seen_clips.insert(clip_entry) {
            self.clip_handles_buffer.push(clip_entry);
        }

        let clip_node_id = self.tree.add(
            clip_node_id,
            &self.clip_handles_buffer,
        );

        self.clip_stack.push(ClipStackEntry {
            last_clip_chain_cache: None,
            seen_clips,
            clip_node_id,
        });
    }

    /// Pop a clip off the clip_stack, when exiting a grouping item
    pub fn pop_clip(&mut self) {
        self.clip_stack.pop().unwrap();
    }

    /// Add clips from a given clip-chain to the set of clips for a primitive during clip-set building
    fn add_clips(
        clip_chain_index: usize,
        seen_clips: &mut FastHashSet<ClipEntry>,
        output: &mut Vec<ClipEntry>,
        clip_chains: &[ClipChain],
    ) {
        // TODO(gw): It's possible that we may see clip outputs that include identical clips
        //           (e.g. if there is a clip positioned by two spatial nodes, where one spatial
        //           node is a child of the other, and has an identity transform). If we ever
        //           see this in real-world cases, it might be worth checking for that here and
        //           excluding them, to ensure the shape of the tree matches what we need for
        //           finding shared_clips for tile caches etc.

        let clip_chain = &clip_chains[clip_chain_index];

        if let Some(parent) = clip_chain.parent {
            ClipTreeBuilder::add_clips(
                parent,
                seen_clips,
                output,
                clip_chains,
            );
        }

        for clip_entry in clip_chain.clips.iter().rev() {
            if seen_clips.insert(*clip_entry) {
                output.push(*clip_entry);
            }
        }
    }

    /// Main entry point to build a path in the clip-tree for a given primitive
    pub fn build_clip_set(
        &mut self,
        clip_chain_id: ClipChainId,
    ) -> ClipNodeId {
        let clip_stack = self.clip_stack.last_mut().unwrap();

        if clip_chain_id == ClipChainId::INVALID {
            clip_stack.clip_node_id
        } else {
            if let Some((cached_clip_chain, cached_clip_node)) = clip_stack.last_clip_chain_cache {
                if cached_clip_chain == clip_chain_id {
                    return cached_clip_node;
                }
            }

            // Bug 1782001: a miss means an item referenced a clip-chain that was never
            // defined in this scene. Report the id, pipeline and number of defined chains
            // so crash reports can tell a builder bug (small, in-range id) from a corrupt
            // display list (garbage id / near-empty map) rather than "no entry found for key".
            let clip_chain_index = match self.clip_chain_map.get(&clip_chain_id) {
                Some(index) => *index,
                None => panic!(
                    "webrender: missing clip-chain {:?} in build_clip_set (defined_chains={})",
                    clip_chain_id,
                    self.clip_chain_map.len(),
                ),
            };

            self.clip_handles_buffer.clear();

            ClipTreeBuilder::add_clips(
                clip_chain_index,
                &mut clip_stack.seen_clips,
                &mut self.clip_handles_buffer,
                &self.clip_chains,
            );

            // We mutated the `clip_stack.seen_clips` in order to remove duplicate clips from
            // the supplied `clip_chain_id`. Now step through and remove any clips we added
            // to the set, so we don't get incorrect results next time `build_clip_set` is
            // called for a different clip-chain. Doing it this way rather than cloning means
            // we avoid heap allocations for each `build_clip_set` call.
            for entry in &self.clip_handles_buffer {
                clip_stack.seen_clips.remove(entry);
            }

            let clip_node_id = self.tree.add(
                clip_stack.clip_node_id,
                &self.clip_handles_buffer,
            );

            clip_stack.last_clip_chain_cache = Some((clip_chain_id, clip_node_id));

            clip_node_id
        }
    }

    /// Recursive impl to check if a clip-chain has complex (non-rectangular) clips
    fn has_complex_clips_impl(
        &self,
        clip_chain_index: usize,
        clips: &SceneClipStore,
    ) -> bool {
        let clip_chain = &self.clip_chains[clip_chain_index];

        for clip_entry in &clip_chain.clips {
            let clip_info = &clips[clip_entry.handle];

            if let ClipNodeKind::Complex = clip_info.key.kind.node_kind() {
                return true;
            }
        }

        match clip_chain.parent {
            Some(parent) => self.has_complex_clips_impl(parent, clips),
            None => false,
        }
    }

    /// Check if a clip-chain has complex (non-rectangular) clips
    pub fn clip_chain_has_complex_clips(
        &self,
        clip_chain_id: ClipChainId,
        clips: &SceneClipStore,
    ) -> bool {
        let clip_chain_index = match self.clip_chain_map.get(&clip_chain_id) {
            Some(index) => *index,
            None => panic!(
                "webrender: missing clip-chain {:?} in clip_chain_has_complex_clips (defined_chains={})",
                clip_chain_id,
                self.clip_chain_map.len(),
            ),
        };
        self.has_complex_clips_impl(clip_chain_index, clips)
    }

    /// Check if all complex clips in a clip chain are fixed-position rounded
    /// rectangles (in Clip mode). When true, the intermediate surface for a
    /// root-level stacking context can be skipped because the clips will be
    /// promoted to compositor clips on the tile cache slices.
    pub fn clip_chain_complex_clips_are_promotable(
        &self,
        clip_chain_id: ClipChainId,
        clips: &SceneClipStore,
        spatial_tree: &SceneSpatialTree,
    ) -> bool {
        let clip_chain_index = match self.clip_chain_map.get(&clip_chain_id) {
            Some(index) => *index,
            None => panic!(
                "webrender: missing clip-chain {:?} in clip_chain_complex_clips_are_promotable (defined_chains={})",
                clip_chain_id,
                self.clip_chain_map.len(),
            ),
        };
        self.complex_clips_are_promotable_impl(clip_chain_index, clips, spatial_tree)
    }

    fn complex_clips_are_promotable_impl(
        &self,
        clip_chain_index: usize,
        clips: &SceneClipStore,
        spatial_tree: &SceneSpatialTree,
    ) -> bool {
        let mut index = clip_chain_index;

        loop {
            let clip_chain = &self.clip_chains[index];

            for clip_entry in &clip_chain.clips {
                let clip_info = &clips[clip_entry.handle];

                match clip_info.key.kind {
                    ClipItemKeyKind::Rectangle(ClipMode::Clip) => {}
                    ClipItemKeyKind::RoundedRectangle(_, _, ClipMode::Clip) => {
                        if !spatial_tree.is_root_coord_system(clip_entry.spatial_node_index) {
                            return false;
                        }
                    }
                    _ => return false,
                }
            }

            match clip_chain.parent {
                Some(parent) => index = parent,
                None => return true,
            }
        }
    }

    /// Check if a clip-node has complex (non-rectangular) clips
    pub fn clip_node_has_complex_clips(
        &self,
        clip_node_id: ClipNodeId,
        clips: &SceneClipStore,
    ) -> bool {
        let mut current = clip_node_id;

        while current != ClipNodeId::NONE {
            let node = &self.tree.nodes[current.0 as usize];
            let clip_info = &clips[node.handle];

            if let ClipNodeKind::Complex = clip_info.key.kind.node_kind() {
                return true;
            }

            current = node.parent;
        }

        false
    }

    pub fn get_parent(&self, id: ClipNodeId) -> Option<ClipNodeId> {
        self.tree.get_parent(id)
    }

    /// Finalize building and return the clip-tree
    pub fn finalize(&mut self) -> ClipTree {
        // Note: After this, the builder's clip tree does not hold allocations and
        // is not in valid state. `ClipTreeBuilder::begin()` must be called before
        // building can happen again.
        std::mem::replace(&mut self.tree, ClipTree {
            nodes: Vec::new(),
        })
    }

    /// Get a clip node by id
    pub fn get_node(&self, id: ClipNodeId) -> &ClipTreeNode {
        assert!(id != ClipNodeId::NONE);

        &self.tree.nodes[id.0 as usize]
    }

    /// Get a clip leaf by id
    /// Build a clip-leaf for a tile-cache
    pub fn build_for_tile_cache(
        &mut self,
        clip_node_id: ClipNodeId,
        extra_clips: &[ClipId],
    ) -> ClipNodeId {
        self.clip_handles_buffer.clear();

        for clip_id in extra_clips {
            let entry = match self.clip_map.get(clip_id) {
                Some(entry) => *entry,
                None => panic!(
                    "webrender: missing clip {:?} in build_for_tile_cache (defined_clips={})",
                    clip_id,
                    self.clip_map.len(),
                ),
            };
            self.clip_handles_buffer.push(entry);
        }

        self.tree.add(
            clip_node_id,
            &self.clip_handles_buffer,
        )
    }

    /// Check that `clip_node_id`, the `build_clip_set` result for an item, was
    /// built on top of the clip stack's current inherited root - so that root
    /// must be an ancestor of (or equal to) it. If it isn't, the clip stack was
    /// pushed or popped between `build_clip_set` and here, which run
    /// back-to-back per item.
    #[cfg(debug_assertions)]
    pub fn debug_check_clip_stack(&self, clip_node_id: ClipNodeId) {
        let inherited_root = self.clip_stack.last().unwrap().clip_node_id;
        let mut cur = clip_node_id;

        while cur != inherited_root && cur != ClipNodeId::NONE {
            cur = self.tree.nodes[cur.0 as usize].parent;
        }

        debug_assert_eq!(
            cur, inherited_root,
            "inherited clip root is not an ancestor of the prim's clip node: clip-stack desync between build_clip_set and the prim being added",
        );
    }

    #[cfg(not(debug_assertions))]
    pub fn debug_check_clip_stack(&self, _clip_node_id: ClipNodeId) {}

    // Find the LCA for two given clip nodes
    pub fn find_lowest_common_ancestor(
        &self,
        node1: ClipNodeId,
        node2: ClipNodeId,
    ) -> ClipNodeId {
        self.tree.find_lowest_common_ancestor(node1, node2)
    }
}

// Type definitions for interning clip nodes.

#[derive(Copy, Clone, Debug, MallocSizeOf, PartialEq, Eq, Hash)]
#[cfg_attr(any(feature = "serde"), derive(Deserialize, Serialize))]
pub enum ClipIntern {}

pub type ClipDataStore = intern::DataStore<ClipIntern>;
pub type ClipDataHandle = intern::Handle<ClipIntern>;

/// The scene builder's view of the interned clips, which clip chain building,
/// slice partitioning and hit testing read for the clip's key.
pub type SceneClipStore = intern::Interner<ClipIntern>;

/// Helper to identify simple clips (normal rects) from other kinds of clips,
/// which can often be handled via fast code paths.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, Copy, Clone, MallocSizeOf)]
pub enum ClipNodeKind {
    /// A normal clip rectangle, with Clip mode.
    Rectangle,
    /// A rectangle with ClipOut, or any other kind of clip.
    Complex,
}

// Result of comparing a clip node instance against a local rect.
#[derive(Debug)]
enum ClipResult {
    // The clip does not affect the region at all.
    Accept,
    // The clip prevents the region from being drawn.
    Reject,
    // The clip affects part of the region. This may
    // require a clip mask, depending on other factors.
    Partial,
}

// A clip node is a single clip source, along with some
// positioning information and implementation details
// that control where the GPU data for this clip source
// can be found.
#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(MallocSizeOf)]
pub struct ClipNode {
    pub item: ClipItem,
}

// Convert from an interning key for a clip item
// to a clip node, which is cached in the document.
impl From<ClipItemKey> for ClipNode {
    fn from(item: ClipItemKey) -> Self {
        let kind = match item.kind {
            ClipItemKeyKind::Rectangle(mode) => {
                ClipItemKind::Rectangle { mode }
            }
            ClipItemKeyKind::RoundedRectangle(radius, inset, mode) => {
                ClipItemKind::RoundedRectangle {
                    radius: radius.into(),
                    inset: LayoutSideOffsets::from_au(inset),
                    mode,
                }
            }
            ClipItemKeyKind::Border(key) => ClipItemKind::Border {
                key,
            },
            // The polygon is only read by the hit tester, at scene build.
            ClipItemKeyKind::ImageMask(image, _) => {
                ClipItemKind::Image {
                    image,
                }
            }
            ClipItemKeyKind::Path(path, _, fill_rule, origin) => {
                ClipItemKind::Path {
                    path,
                    fill_rule,
                    origin: LayoutPoint::from_au(origin),
                }
            }
        };

        ClipNode {
            item: ClipItem {
                kind,
            },
        }
    }
}

// Flags that are attached to instances of clip nodes.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash, MallocSizeOf)]
pub struct ClipNodeFlags(u8);

bitflags! {
    impl ClipNodeFlags : u8 {
        const SAME_SPATIAL_NODE = 0x1;
        const SAME_COORD_SYSTEM = 0x2;
    }
}

impl core::fmt::Debug for ClipNodeFlags {
    fn fmt(&self, f: &mut core::fmt::Formatter) -> core::fmt::Result {
        if self.is_empty() {
            write!(f, "{:#x}", Self::empty().bits())
        } else {
            bitflags::parser::to_writer(self, f)
        }
    }
}

// When a clip node is found to be valid for a
// clip chain instance, it's stored in an index
// buffer style structure. This struct contains
// an index to the node data itself, as well as
// some flags describing how this clip node instance
// is positioned.
#[derive(Debug, Clone, MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipNodeInstance {
    pub handle: ClipDataHandle,
    pub spatial_node_index: SpatialNodeIndex,
    pub clip_rect: LayoutRect,
    pub flags: ClipNodeFlags,
    pub visible_tiles: Option<ops::Range<usize>>,
}

// A range of clip node instances that were found by
// building a clip chain instance.
#[derive(Debug, Copy, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipNodeRange {
    pub first: u32,
    pub count: u32,
}

impl ClipNodeRange {
    pub fn to_range(&self) -> ops::Range<usize> {
        let start = self.first as usize;
        let end = start + self.count as usize;

        ops::Range {
            start,
            end,
        }
    }
}

/// A helper struct for converting between coordinate systems
/// of clip sources and primitives.
///
/// Note that the variants don't represent the same transformation
/// because depending on the situation we either map between the
/// clip and primitive spaces or project them both to visibility
/// space.
// todo(gw): optimize:
//  separate arrays for matrices
//  cache and only build as needed.
//TODO: merge with `CoordinateSpaceMapping`?
#[derive(Debug, MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub enum ClipSpaceConversion {
    /// The clip and the clipped primitive are in the same coordinate space.
    Local,
    /// The clip and the clipped primitive are in the same coordinate system.
    ///
    /// This variant represents the transform from the clip's local space to
    /// the clipped primitive's local space.
    ScaleOffset(ScaleOffset),
    /// The clip and the clipped primitive are in different coordinate system.
    ///
    /// This Variant represents the transform from the clip's local space to
    /// the surface's raster space.
    Transform(LayoutToRasterTransform),
    /// The clip's space cannot be related to the surface's raster space, so
    /// whether the clip affects the primitive can't be determined and a mask is
    /// assumed to be needed.
    ///
    /// Reached when the clip sits outside the 3D context that established the
    /// surface's raster root: relating the two would need a transform away from
    /// the root, which the spatial tree does not provide because it is not
    /// always invertible.
    Indeterminate,
}

impl ClipSpaceConversion {
    /// Construct a new clip space converter between two spatial nodes.
    pub fn new(
        prim_spatial_node_index: SpatialNodeIndex,
        clip_spatial_node_index: SpatialNodeIndex,
        raster_spatial_node_index: SpatialNodeIndex,
        spatial_tree: &SpatialTree,
    ) -> Self {
        //Note: this code is different from `get_relative_transform` in a way that we only try
        // getting the relative transform if it's Local or ScaleOffset,
        // falling back to the world transform otherwise.
        let clip_spatial_node = spatial_tree.get_spatial_node(clip_spatial_node_index);
        let prim_spatial_node = spatial_tree.get_spatial_node(prim_spatial_node_index);

        if prim_spatial_node_index == clip_spatial_node_index {
            ClipSpaceConversion::Local
        } else if prim_spatial_node.coordinate_system_id == clip_spatial_node.coordinate_system_id {
            let scale_offset = clip_spatial_node.content_transform
                .then(&prim_spatial_node.content_transform.inverse());
            ClipSpaceConversion::ScaleOffset(scale_offset)
        } else if spatial_tree.can_get_relative_transform(
            clip_spatial_node_index,
            raster_spatial_node_index,
        ) {
            ClipSpaceConversion::Transform(
                spatial_tree.get_relative_transform(
                    clip_spatial_node_index,
                    raster_spatial_node_index,
                ).into_transform().cast_unit()
            )
        } else {
            ClipSpaceConversion::Indeterminate
        }
    }

    fn to_flags(&self) -> ClipNodeFlags {
        match *self {
            ClipSpaceConversion::Local => {
                ClipNodeFlags::SAME_SPATIAL_NODE | ClipNodeFlags::SAME_COORD_SYSTEM
            }
            ClipSpaceConversion::ScaleOffset(..) => {
                ClipNodeFlags::SAME_COORD_SYSTEM
            }
            ClipSpaceConversion::Transform(..) |
            ClipSpaceConversion::Indeterminate => {
                ClipNodeFlags::empty()
            }
        }
    }
}

// Temporary information that is cached and reused
// during building of a clip chain instance.
#[derive(MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
struct ClipNodeInfo {
    conversion: ClipSpaceConversion,
    handle: ClipDataHandle,
    spatial_node_index: SpatialNodeIndex,
    clip_rect: LayoutRect,
}

impl ClipNodeInfo {
    fn create_instance(
        &self,
        node: &ClipNode,
        clipped_rect: &LayoutRect,
        gpu_buffer: &mut GpuBufferBuilderF,
        resource_cache: &mut ResourceCache,
        mask_tiles: &mut Vec<VisibleMaskImageTile>,
        rg_builder: &mut RenderTaskGraphBuilder,
        request_resources: bool,
    ) -> Option<ClipNodeInstance> {
        // Calculate some flags that are required for the segment
        // building logic.
        let flags = self.conversion.to_flags();

        let mut visible_tiles = None;

        if let ClipItemKind::Image { image, .. } = node.item.kind {
            let rect = self.clip_rect;
            let request = ImageRequest {
                key: image,
                rendering: ImageRendering::Auto,
                tile: None,
            };

            if let Some(props) = resource_cache.get_image_properties(image) {
                if let Some(tile_size) = props.tiling {
                    let tile_range_start = mask_tiles.len();

                    // Bug 1648323 - It is unclear why on rare occasions we get
                    // a clipped_rect that does not intersect the clip's mask rect.
                    // defaulting to clipped_rect here results in zero repetitions
                    // which clips the primitive entirely.
                    let visible_rect =
                        clipped_rect.intersection(&rect).unwrap_or(*clipped_rect);

                    let repetitions = image_tiling::repetitions(
                        &rect,
                        &visible_rect.intersection_unchecked(&rect),
                        rect.size(),
                    );

                    for Repetition { origin, .. } in repetitions {
                        let layout_image_rect = LayoutRect::from_origin_and_size(
                            origin,
                            rect.size(),
                        );
                        let tiles = image_tiling::tiles(
                            &layout_image_rect,
                            &visible_rect,
                            &props.visible_rect,
                            tile_size as i32,
                        );
                        for tile in tiles {
                            let req = request.with_tile(tile.offset);

                            if request_resources {
                                resource_cache.request_image(
                                    req,
                                    gpu_buffer,
                                );
                            }

                            let task_id = rg_builder.add().init(
                                RenderTask::new_image(props.descriptor.size, req, false)
                            );

                            mask_tiles.push(VisibleMaskImageTile {
                                tile_offset: tile.offset,
                                tile_rect: tile.rect,
                                task_id,
                            });
                        }
                    }
                    visible_tiles = Some(tile_range_start..mask_tiles.len());
                } else {
                    if request_resources {
                        resource_cache.request_image(request, gpu_buffer);
                    }

                    let tile_range_start = mask_tiles.len();

                    let task_id = rg_builder.add().init(
                        RenderTask::new_image(props.descriptor.size, request, false)
                    );

                    mask_tiles.push(VisibleMaskImageTile {
                        tile_rect: rect,
                        tile_offset: TileOffset::zero(),
                        task_id,
                    });

                    visible_tiles = Some(tile_range_start .. mask_tiles.len());
                }
            } else {
                // If the supplied image key doesn't exist in the resource cache,
                // skip the clip node since there is nothing to mask with.
                warn!("Clip mask with missing image key {:?}", request.key);
                return None;
            }
        }

        Some(ClipNodeInstance {
            handle: self.handle,
            spatial_node_index: self.spatial_node_index,
            clip_rect: self.clip_rect,
            flags,
            visible_tiles,
        })
    }
}

#[derive(Default)]
pub struct ClipStoreScratchBuffer {
    clip_node_instances: Vec<ClipNodeInstance>,
    mask_tiles: Vec<VisibleMaskImageTile>,
}

/// The main clipping public interface that other modules access.
#[derive(MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct ClipStore {
    pub clip_node_instances: Vec<ClipNodeInstance>,
    mask_tiles: Vec<VisibleMaskImageTile>,

    active_clip_node_info: Vec<ClipNodeInfo>,
    active_local_clip_rect: Option<LayoutRect>,
    active_pic_coverage_rect: PictureRect,

    /// Counts of what the visibility-space clip path did this frame, flushed to
    /// the profiler by `end_frame`.
    raster_clip_stats: RasterClipStats,
}

/// Instrumentation for the clip decisions that visibility space affects. See the
/// `VIS_CLIP_*` profiler counters.
#[derive(Clone, Default, MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct RasterClipStats {
    pub projections: usize,
    pub projection_fails: usize,
    pub rejects: usize,
    pub indeterminate: usize,
}

// A clip chain instance is what gets built for a given clip
// chain id + local primitive region + positioning node.
#[derive(Debug, Copy, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct ClipChainInstance {
    pub clips_range: ClipNodeRange,
    // Combined clip rect for clips that are in the
    // same coordinate system as the primitive.
    //
    // Does not include the primitive's own extent, so callers drawing something
    // whose extent is not the primitive's rect (a drop shadow, a border segment,
    // box shadow's re-derived rect) combine this with their own extent instead
    // of using `local_coverage_rect`.
    pub local_clip_rect: LayoutRect,
    // The primitive's coverage rect: `local_clip_rect` intersected with the
    // primitive's own extent. The only rect that bounds where the primitive
    // covers pixels; the rect that situates its pattern contributes nothing to
    // it beyond that extent.
    pub local_coverage_rect: LayoutRect,
    pub has_non_local_clips: bool,
    // If true, this clip chain requires allocation
    // of a clip mask.
    pub needs_mask: bool,
    // Combined clip rect in picture space (may
    // be more conservative that local_clip_rect).
    pub pic_coverage_rect: PictureRect,
    // Space, in which the `pic_coverage_rect` is defined.
    pub pic_spatial_node_index: SpatialNodeIndex,
}

impl ClipChainInstance {
    pub fn empty() -> Self {
        ClipChainInstance {
            clips_range: ClipNodeRange {
                first: 0,
                count: 0,
            },
            local_clip_rect: LayoutRect::zero(),
            local_coverage_rect: LayoutRect::zero(),
            has_non_local_clips: false,
            needs_mask: false,
            pic_coverage_rect: PictureRect::zero(),
            pic_spatial_node_index: SpatialNodeIndex::INVALID,
        }
    }
}

impl ClipStore {
    pub fn new() -> Self {
        ClipStore {
            clip_node_instances: Vec::new(),
            mask_tiles: Vec::new(),
            active_clip_node_info: Vec::new(),
            active_local_clip_rect: None,
            active_pic_coverage_rect: PictureRect::max_rect(),
            raster_clip_stats: RasterClipStats::default(),
        }
    }

    pub fn reset(&mut self) {
        self.clip_node_instances.clear();
        self.mask_tiles.clear();
        self.active_clip_node_info.clear();
        self.active_local_clip_rect = None;
        self.active_pic_coverage_rect = PictureRect::max_rect();
    }

    pub fn get_instance_from_range(
        &self,
        node_range: &ClipNodeRange,
        index: u32,
    ) -> &ClipNodeInstance {
        &self.clip_node_instances[(node_range.first + index) as usize]
    }

    /// Resolve a clip chain instance into a `QuadClipStack`, which the quad path
    /// consumes without reaching back into the clip store or the interner.
    ///
    /// `surface` is the surface the primitive is drawn into. The caller picks
    /// it because the clip store has no way to: it is the destination surface,
    /// not the one the primitive's own picture belongs to.
    pub fn fill_quad_clips(
        &self,
        dest: &mut QuadClipStack,
        clip_chain: &ClipChainInstance,
        surface: &SurfaceInfo,
        interned_clips: &ClipDataStore,
        dl_stores: &DlStores,
    ) {
        self.fill_quad_clips_from_range(dest, clip_chain.clips_range, interned_clips, dl_stores);

        dest.set_bounds(
            clip_chain.local_clip_rect,
            surface.map_to_device_rect(&clip_chain.pic_coverage_rect),
            surface.clipping_rect,
            clip_chain.needs_mask,
        );
    }

    /// Resolve a range of clip node instances into a `QuadClipStack`, leaving the
    /// stack's bounds alone. For the mask paths that assemble an ad-hoc range
    /// rather than consuming a whole clip chain.
    pub fn fill_quad_clips_from_range(
        &self,
        dest: &mut QuadClipStack,
        range: ClipNodeRange,
        interned_clips: &ClipDataStore,
        dl_stores: &DlStores,
    ) {
        dest.clear();

        for instance in &self.clip_node_instances[range.to_range()] {
            self.push_quad_clip(dest, instance, interned_clips, dl_stores);
        }
    }

    /// Resolve a single clip node instance and append it to a `QuadClipStack`.
    /// For the mask paths that pick out a subset of a clip chain.
    pub fn push_quad_clip(
        &self,
        dest: &mut QuadClipStack,
        instance: &ClipNodeInstance,
        interned_clips: &ClipDataStore,
        dl_stores: &DlStores,
    ) {
        let uid = instance.handle.uid().get_uid();

        match interned_clips[instance.handle].item.kind {
            ClipItemKind::Rectangle { mode } => {
                dest.push_rect(
                    instance.clip_rect,
                    mode,
                    instance.spatial_node_index,
                    uid,
                );
            }
            ClipItemKind::RoundedRectangle { radius, inset, mode } => {
                dest.push_rounded_rect(
                    instance.clip_rect,
                    radius,
                    inset,
                    mode,
                    instance.spatial_node_index,
                    uid,
                );
            }
            ClipItemKind::Border { key } => {
                dest.push_border(instance.clip_rect, key, instance.spatial_node_index, uid);
            }
            ClipItemKind::Image { .. } => {
                dest.push_mask(
                    instance.clip_rect,
                    instance.spatial_node_index,
                    uid,
                    self.visible_mask_tiles(instance).iter().map(|tile| QuadMaskTile {
                        rect: tile.tile_rect,
                        task_id: tile.task_id,
                    }),
                );
            }
            ClipItemKind::Path { path, fill_rule, origin } => {
                dest.push_path(
                    instance.clip_rect,
                    dl_stores.path[path].clone(),
                    fill_rule,
                    origin,
                    instance.spatial_node_index,
                    uid,
                );
            }
        }
    }

    /// Setup the active clip chains for building a clip chain instance.
    pub fn set_active_clips(
        &mut self,
        prim_spatial_node_index: SpatialNodeIndex,
        pic_spatial_node_index: SpatialNodeIndex,
        raster_spatial_node_index: SpatialNodeIndex,
        snapper: &mut SpaceSnapper,
        clip_snap: ClipSnap,
        // Where this primitive's clip chain starts in the tree; the walk below
        // runs from here up to `clip_root`.
        clip_node_id: ClipNodeId,
        // The node in the clip tree at and above which clips are already
        // handled by the enclosing surface, so this primitive can ignore them.
        // Frame state owned by the visibility traversal, not the scene's tree.
        clip_root: ClipNodeId,
        // The prim's own local clip rect, already snapped by the caller against
        // its spatial node (`snap_local_clip_rect`). Passed in rather than
        // snapped here because `snapper` is re-targeted per clip node in the
        // loop below, so it no longer targets the prim.
        snapped_local_clip_rect: LayoutRect,
        spatial_tree: &SpatialTree,
        clip_data_store: &ClipDataStore,
        clip_tree: &ClipTree,
    ) {
        self.active_clip_node_info.clear();
        self.active_local_clip_rect = None;
        self.active_pic_coverage_rect = PictureRect::max_rect();

        // How each clip in the chain rounds to the device grid is the prim's
        // clip policy (`ClipSnap`), resolved by the caller from its clip-leaf
        // sentinel. A snapping prim snaps every clip to nearest so a fractional
        // clip edge (an animated clip-path, a rounded overflow clip, etc.) lands
        // on the same grid as the prim's own snapped geometry; a device-space
        // prim (text run or surface) leaves its clips exact so they stay at the
        // sub-pixel position matching its contents (bug 2050692). The leaf clip
        // rect was pre-snapped accordingly by the caller.
        let mut local_clip_rect = snapped_local_clip_rect;
        let mut current = clip_node_id;

        while current != clip_root && current != ClipNodeId::NONE {
            let node = clip_tree.get_node(current);
            let clip_rect = match (clip_snap, &clip_data_store[node.handle].item.kind) {
                (_, ClipItemKind::Border { key }) => {
                    snapper.set_target_spatial_node(node.spatial_node_index, spatial_tree);
                    snapper.snap_rect_rounded(
                        &node.unsnapped_clip_rect,
                        SnapRounding::BorderInner { widths: LayoutSideOffsets::from_au(key.widths) },
                    )
                }
                // An anti-aliased clip stays exact, like an anti-aliased prim.
                (ClipSnap::Nearest, _) if node.anti_aliased => node.unsnapped_clip_rect,
                (ClipSnap::Nearest, _) => node.snapped_clip_rect(snapper, spatial_tree),
                // Device-space prim: leave the clip exact. A
                // text run's clips must NOT be rounded out to the grid - an
                // overflow clip sits flush with its container's painted
                // border box, and rounding it outward spills a whole device
                // row/column of scrolled content over that border (bug
                // 2065629). Left exact, the clip rasterizes at the pixel
                // centre, which is the same grid the container's own snapped
                // geometry landed on.
                (ClipSnap::Exact, _) => node.unsnapped_clip_rect,
            };

            if !add_clip_node_to_current_chain(
                node.handle,
                node.spatial_node_index,
                clip_rect,
                prim_spatial_node_index,
                pic_spatial_node_index,
                raster_spatial_node_index,
                &mut local_clip_rect,
                &mut self.active_clip_node_info,
                &mut self.active_pic_coverage_rect,
                clip_data_store,
                spatial_tree,
            ) {
                return;
            }

            current = node.parent;
        }

        self.active_local_clip_rect = Some(local_clip_rect);
    }

    /// Given a clip-chain instance, return a safe rect within the visible region
    /// that can be assumed to be unaffected by clip radii. Returns None if it
    /// encounters any complex cases, just handling rounded rects in the same
    /// coordinate system as the clip-chain for now.
    pub fn get_inner_rect_for_clip_chain(
        &self,
        clip_chain: &ClipChainInstance,
        clip_data_store: &ClipDataStore,
        spatial_tree: &SpatialTree,
    ) -> Option<PictureRect> {
        let mut inner_rect = clip_chain.pic_coverage_rect;
        let clip_instances = &self
            .clip_node_instances[clip_chain.clips_range.to_range()];

        for clip_instance in clip_instances {
            // Don't handle mapping between coord systems for now
            if !clip_instance.flags.contains(ClipNodeFlags::SAME_COORD_SYSTEM) {
                return None;
            }

            let clip_node = &clip_data_store[clip_instance.handle];

            match clip_node.item.kind {
                // Ignore any clips which are complex or impossible to calculate
                // inner rects for now
                ClipItemKind::Rectangle { mode: ClipMode::ClipOut, .. } |
                ClipItemKind::Border { .. } |
                ClipItemKind::Image { .. } |
                ClipItemKind::Path { .. } |
                ClipItemKind::RoundedRectangle { mode: ClipMode::ClipOut, .. } => {
                    return None;
                }
                // Normal Clip rects are already handled by the clip-chain pic_coverage_rect,
                // no need to do anything here
                ClipItemKind::Rectangle { mode: ClipMode::Clip, .. } => {}
                ClipItemKind::RoundedRectangle { mode: ClipMode::Clip, radius, inset } => {
                    // Get an inner rect for the rounded-rect clip
                    let radius = clamped_radius(&radius, clip_instance.clip_rect.size());
                    let local_inner_rect = match extract_inner_rect_safe(&clip_instance.clip_rect, &radius, &inset) {
                        Some(rect) => rect,
                        None => return None,
                    };

                    // Map it from local -> picture space
                    let mapper = SpaceMapper::new_with_target(
                        clip_chain.pic_spatial_node_index,
                        clip_instance.spatial_node_index,
                        PictureRect::max_rect(),
                        spatial_tree,
                    );

                    // Accumulate in to the inner_rect, in case there are multiple rounded-rect clips
                    if let Some(pic_inner_rect) = mapper.map(&local_inner_rect) {
                        inner_rect = inner_rect.intersection(&pic_inner_rect).unwrap_or(PictureRect::zero());
                    }
                }
            }
        }

        Some(inner_rect)
    }

    /// The main interface external code uses. Given a local primitive, positioning
    /// information, and a clip chain id, build an optimized clip chain instance.
    pub fn build_clip_chain_instance(
        &mut self,
        local_prim_rect: LayoutRect,
        prim_to_pic_mapper: &SpaceMapper<LayoutPixel, PicturePixel>,
        pic_to_raster_mapper: &SpaceMapper<PicturePixel, RasterPixel>,
        gpu_buffer: &mut GpuBufferBuilderF,
        resource_cache: &mut ResourceCache,
        culling_rect: &RasterRect,
        clip_data_store: &ClipDataStore,
        rg_builder: &mut RenderTaskGraphBuilder,
        request_resources: bool,
    ) -> Option<ClipChainInstance> {
        let local_clip_rect = match self.active_local_clip_rect {
            Some(rect) => rect,
            None => return None,
        };
        tracy_rs::profile_scope!("build_clip_chain_instance");


        let local_bounding_rect = local_prim_rect.intersection(&local_clip_rect)?;
        let mut pic_coverage_rect = prim_to_pic_mapper.map(&local_bounding_rect)?;
        let raster_clip_rect = pic_to_raster_mapper.map(&pic_coverage_rect)?;

        // Now, we've collected all the clip nodes that *potentially* affect this
        // primitive region, and reduced the size of the prim region as much as possible.

        // Run through the clip nodes, and see which ones affect this prim region.

        let first_clip_node_index = self.clip_node_instances.len() as u32;
        let mut has_non_local_clips = false;
        let mut needs_mask = false;

        // For each potential clip node
        for node_info in self.active_clip_node_info.drain(..) {
            let node = &clip_data_store[node_info.handle];

            // See how this clip affects the prim region.
            let clip_result = match node_info.conversion {
                ClipSpaceConversion::Local => {
                    node.item.kind.get_clip_result(&local_bounding_rect, node_info.clip_rect)
                }
                ClipSpaceConversion::ScaleOffset(ref scale_offset) => {
                    has_non_local_clips = true;
                    node.item.kind.get_clip_result(&scale_offset.unmap_rect(&local_bounding_rect), node_info.clip_rect)
                }
                ClipSpaceConversion::Indeterminate => {
                    // Can't tell whether the clip affects the primitive, so
                    // assume it does. A mask is always a safe answer.
                    has_non_local_clips = true;
                    self.raster_clip_stats.indeterminate += 1;
                    ClipResult::Partial
                }
                ClipSpaceConversion::Transform(ref transform) => {
                    has_non_local_clips = true;
                    self.raster_clip_stats.projections += 1;
                    node.item.kind.get_clip_result_complex(
                        transform,
                        &raster_clip_rect,
                        culling_rect,
                        node_info.clip_rect,
                        &mut self.raster_clip_stats.projection_fails,
                    )
                }
            };

            match clip_result {
                ClipResult::Accept => {
                    // Doesn't affect the primitive at all, so skip adding to list
                }
                ClipResult::Reject => {
                    // Completely clips the supplied prim rect
                    if matches!(node_info.conversion, ClipSpaceConversion::Transform(..)) {
                        self.raster_clip_stats.rejects += 1;
                    }
                    return None;
                }
                ClipResult::Partial => {
                    // Needs a mask -> add to clip node indices

                    // Create the clip node instance for this clip node
                    if let Some(instance) = node_info.create_instance(
                        node,
                        &local_bounding_rect,
                        gpu_buffer,
                        resource_cache,
                        &mut self.mask_tiles,
                        rg_builder,
                        request_resources,
                    ) {
                        // As a special case, a partial accept of a clip rect that is
                        // in the same coordinate system as the primitive doesn't need
                        // a clip mask. Instead, it can be handled by the primitive
                        // vertex shader as part of the local clip rect. This is an
                        // important optimization for reducing the number of clip
                        // masks that are allocated on common pages.
                        needs_mask |= match node.item.kind {
                            ClipItemKind::Rectangle { mode: ClipMode::ClipOut, .. } |
                            ClipItemKind::RoundedRectangle { .. } |
                            ClipItemKind::Border { .. } |
                            ClipItemKind::Image { .. } |
                            ClipItemKind::Path { .. } => {
                                true
                            }

                            ClipItemKind::Rectangle { mode: ClipMode::Clip, .. } => {
                                !instance.flags.contains(ClipNodeFlags::SAME_COORD_SYSTEM)
                            }
                        };

                        // Store this in the index buffer for this clip chain instance.
                        self.clip_node_instances.push(instance);
                    }
                }
            }
        }

        // Get the range identifying the clip nodes in the index buffer.
        let clips_range = ClipNodeRange {
            first: first_clip_node_index,
            count: self.clip_node_instances.len() as u32 - first_clip_node_index,
        };

        // If this clip chain needs a mask, reduce the size of the mask allocation
        // by any clips that were in the same space as the picture. This can result
        // in much smaller clip mask allocations in some cases. Note that the ordering
        // here is important - the reduction must occur *after* the clip item accept
        // reject checks above, so that we don't eliminate masks accidentally (since
        // we currently only support a local clip rect in the vertex shader).
        if needs_mask {
            pic_coverage_rect = pic_coverage_rect.intersection(&self.active_pic_coverage_rect)?;
        }

        // Return a valid clip chain instance
        Some(ClipChainInstance {
            clips_range,
            has_non_local_clips,
            local_clip_rect,
            local_coverage_rect: local_bounding_rect,
            pic_coverage_rect,
            pic_spatial_node_index: prim_to_pic_mapper.ref_spatial_node_index,
            needs_mask,
        })
    }

    pub fn begin_frame(&mut self, scratch: &mut ClipStoreScratchBuffer) {
        mem::swap(&mut self.clip_node_instances, &mut scratch.clip_node_instances);
        mem::swap(&mut self.mask_tiles, &mut scratch.mask_tiles);
        self.clip_node_instances.clear();
        self.mask_tiles.clear();
        self.raster_clip_stats = RasterClipStats::default();
    }

    pub fn raster_clip_stats(&self) -> &RasterClipStats {
        &self.raster_clip_stats
    }

    pub fn end_frame(&mut self, scratch: &mut ClipStoreScratchBuffer) {
        mem::swap(&mut self.clip_node_instances, &mut scratch.clip_node_instances);
        mem::swap(&mut self.mask_tiles, &mut scratch.mask_tiles);
    }

    pub fn visible_mask_tiles(&self, instance: &ClipNodeInstance) -> &[VisibleMaskImageTile] {
        if let Some(range) = &instance.visible_tiles {
            &self.mask_tiles[range.clone()]
        } else {
            &[]
        }
    }
}

impl Default for ClipStore {
    fn default() -> Self {
        ClipStore::new()
    }
}

// The ClipItemKey is a hashable representation of the geometry of
// a clip item. It is used during interning to de-duplicate clip nodes
// between frames and display lists. This allows quick comparison of
// clip node equality by handle, and also allows the uploaded GPU cache
// handle to be retained between display lists. The spatial node index
// and clip rect are intentionally excluded from the key so that clips
// with the same shape but different positioning or size can share
// interned data. For rounded-rect clips the stored radii are unclamped;
// each consumer clamps against the instance clip rect as needed.
// TODO(gw): Maybe we should consider constructing these directly
//           in the DL builder?
#[derive(Copy, Debug, Clone, Eq, MallocSizeOf, PartialEq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum ClipItemKeyKind {
    Rectangle(ClipMode),
    RoundedRectangle(BorderRadiusAu, LayoutSideOffsetsAu, ClipMode),
    ImageMask(ImageKey, Option<PolygonDataHandle>),
    /// The uid is the path's `DlStore::uid`. The handle alone does not
    /// identify the path: content recycles slots, and a retained clip entry
    /// keyed on a recycled slot would keep the uid of the path it replaced.
    ///
    /// The point is the origin of the clip's rect, which positions the path.
    /// Clip rects are snapped to the device pixel grid, but the path stays at
    /// its unsnapped position.
    Path(DlHandle<Path>, ItemUid, FillRule, LayoutPointAu),
    Border(BorderClipKey),
}

#[derive(Copy, Debug, Clone, Eq, MallocSizeOf, PartialEq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BorderClipKey {
    pub styles: [BorderStyle; 4],
    pub radius: BorderRadiusAu,
    pub widths: LayoutSideOffsetsAu,
    pub inset: LayoutSideOffsetsAu,
}

impl BorderClipKey {
    pub fn new(border: &NormalBorder, widths: LayoutSideOffsets) -> Self {
        Self {
            styles: [border.top.style, border.right.style, border.bottom.style, border.left.style],
            radius: border.radius.into(),
            widths: widths.to_au(),
            inset: border.inset.to_au(),
        }
    }

    pub fn border(self) -> NormalBorder {
        let side = |style| BorderSide { color: ColorF::WHITE, style };
        let mut border = NormalBorder {
            top: side(self.styles[0]), right: side(self.styles[1]),
            bottom: side(self.styles[2]), left: side(self.styles[3]),
            radius: self.radius.into(),
            inset: LayoutSideOffsets::from_au(self.inset),
            do_aa: true,
        };
        border.normalize(&LayoutSideOffsets::from_au(self.widths));
        border
    }
}

impl ClipItemKeyKind {
    pub fn rectangle(mode: ClipMode) -> Self {
        ClipItemKeyKind::Rectangle(mode)
    }

    pub fn rounded_rect(radii: BorderRadius, inset: LayoutSideOffsets, mode: ClipMode) -> Self {
        if radii.is_zero() {
            ClipItemKeyKind::rectangle(mode)
        } else {
            ClipItemKeyKind::RoundedRectangle(
                radii.into(),
                inset.to_au(),
                mode,
            )
        }
    }

    pub fn image_mask(image_mask: &ImageMask,
                      polygon_handle: Option<PolygonDataHandle>) -> Self {
        ClipItemKeyKind::ImageMask(
            image_mask.image,
            polygon_handle,
        )
    }

    pub fn node_kind(&self) -> ClipNodeKind {
        match *self {
            ClipItemKeyKind::Rectangle(ClipMode::Clip) => ClipNodeKind::Rectangle,

            ClipItemKeyKind::Rectangle(ClipMode::ClipOut) |
            ClipItemKeyKind::RoundedRectangle(..) |
            ClipItemKeyKind::Border(..) |
            ClipItemKeyKind::ImageMask(..) |
            ClipItemKeyKind::Path(..) => ClipNodeKind::Complex,
        }
    }
}

#[derive(Debug, Copy, Clone, Eq, MallocSizeOf, PartialEq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipItemKey {
    pub kind: ClipItemKeyKind,
}

/// The data available about an interned clip node during scene building
#[derive(Debug, MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipInternData {
    pub key: ClipItemKey,
}

impl intern::InternDebug for ClipItemKey {}

impl intern::Internable for ClipIntern {
    type Key = ClipItemKey;
    type StoreData = ClipNode;
    type InternData = ClipInternData;
    const PROFILE_COUNTER: usize = crate::profiler::INTERNED_CLIPS;
}

#[derive(Debug, MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum ClipItemKind {
    Border {
        key: BorderClipKey,
    },
    Rectangle {
        mode: ClipMode,
    },
    RoundedRectangle {
        radius: BorderRadius,
        inset: LayoutSideOffsets,
        mode: ClipMode,
    },
    Image {
        image: ImageKey,
    },
    Path {
        path: DlHandle<Path>,
        fill_rule: FillRule,
        /// See `ClipItemKeyKind::Path`.
        origin: LayoutPoint,
    },
}

#[derive(Debug, MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipItem {
    pub kind: ClipItemKind,
}

/// Clamp corner radii so adjacent radii don't overlap along an edge of `size`.
/// Rounded-rect radii are interned unclamped, so consumers must clamp against
/// the instance-specific clip rect before use.
///
/// Unlike `ensure_no_corner_overlap`, an edge is only constrained when *both*
/// of its corners are actually curved. A lone oversized radius is left alone:
/// its arc centre simply falls outside the rect and the arc is cut by the far
/// edges, which is a perfectly representable shape.
///
/// The distinction matters because clips arrive here with *derived* radii.
/// `background-clip: padding-box|content-box` clips to the inner border edge,
/// which css-backgrounds-3 4.4 defines as concentric with the outer edge and
/// radii of `outer - border-width` -- deliberately not re-normalised. Scaling
/// those down pulled the background away from the border's inner curve and let
/// a translucent border show over it (bug 1830603).
///
/// This is *not* true of every rounded rect. A box-shadow's spread-adjusted
/// radii are a plain rounded rect and do get the css-backgrounds-3 5.5
/// f-factor, which is why `ensure_no_corner_overlap` keeps the stricter rule;
/// Chrome behaves the same way on both counts. Authored radii are already
/// normalised by Gecko before they reach us (`nsIFrame::ComputeBorderRadii`).
pub fn clamped_radius(radius: &BorderRadius, size: LayoutSize) -> BorderRadius {
    let mut r = *radius;
    let mut ratio: f32 = 1.0;

    let mut constrain = |extent: f32, a: f32, b: f32| {
        if a > 0.0 && b > 0.0 && extent < a + b {
            ratio = f32::min(ratio, extent / (a + b));
        }
    };

    if size.width > 0.0 {
        constrain(size.width, r.top_left.width, r.top_right.width);
        constrain(size.width, r.bottom_left.width, r.bottom_right.width);
    }
    if size.height > 0.0 {
        constrain(size.height, r.top_left.height, r.bottom_left.height);
        constrain(size.height, r.top_right.height, r.bottom_right.height);
    }

    if ratio < 1.0 {
        for corner in [&mut r.top_left, &mut r.top_right,
                       &mut r.bottom_left, &mut r.bottom_right] {
            corner.width *= ratio;
            corner.height *= ratio;
        }
    }

    r
}

impl ClipItemKind {
    // Get an optional clip rect that a clip source can provide to
    // reduce the size of a primitive region. This is typically
    // used to eliminate redundant clips, and reduce the size of
    // any clip mask that eventually gets drawn.
    pub fn get_local_clip_rect(&self, clip_rect: LayoutRect) -> Option<LayoutRect> {
        match *self {
            ClipItemKind::Rectangle { mode: ClipMode::Clip } => Some(clip_rect),
            ClipItemKind::Rectangle { mode: ClipMode::ClipOut } => None,
            ClipItemKind::RoundedRectangle { mode: ClipMode::Clip, .. } => Some(clip_rect),
            ClipItemKind::RoundedRectangle { mode: ClipMode::ClipOut, .. } => None,
            ClipItemKind::Border { .. } |
            ClipItemKind::Image { .. } |
            ClipItemKind::Path { .. } => Some(clip_rect),
        }
    }

    fn get_clip_result_complex(
        &self,
        transform: &LayoutToRasterTransform,
        prim_rect: &RasterRect,
        culling_rect: &RasterRect,
        clip_rect: LayoutRect,
        projection_fails: &mut usize,
    ) -> ClipResult {
        let visible_rect = match prim_rect.intersection(culling_rect) {
            Some(rect) => rect,
            None => return ClipResult::Reject,
        };

        let (clip_rect, inner_rect, mode) = match *self {
            ClipItemKind::Rectangle { mode } => {
                (clip_rect, Some(clip_rect), mode)
            }
            ClipItemKind::RoundedRectangle { ref radius, ref inset, mode } => {
                let clamped = clamped_radius(radius, clip_rect.size());
                let inner_clip_rect = extract_inner_rect_safe(&clip_rect, &clamped, &inset);
                (clip_rect, inner_clip_rect, mode)
            }
            ClipItemKind::Border { .. } |
            ClipItemKind::Image { .. } |
            ClipItemKind::Path { .. } => {
                (clip_rect, None, ClipMode::Clip)
            }
        };

        if let Some(ref inner_clip_rect) = inner_rect {
            if let Some(()) = projected_rect_contains(inner_clip_rect, transform, &visible_rect) {
                return match mode {
                    ClipMode::Clip => ClipResult::Accept,
                    ClipMode::ClipOut => ClipResult::Reject,
                };
            }
        }

        match mode {
            ClipMode::Clip => {
                let outer_clip_rect = match project_rect(
                    transform,
                    &clip_rect,
                    &culling_rect,
                ) {
                    Some(outer_clip_rect) => outer_clip_rect,
                    None => {
                        // No exact answer available, so a mask is required even
                        // though the clip may well not affect the primitive.
                        *projection_fails += 1;
                        return ClipResult::Partial;
                    }
                };

                match outer_clip_rect.intersection(prim_rect) {
                    Some(..) => {
                        ClipResult::Partial
                    }
                    None => {
                        ClipResult::Reject
                    }
                }
            }
            ClipMode::ClipOut => ClipResult::Partial,
        }
    }

    // Check how a given clip source affects a local primitive region.
    fn get_clip_result(
        &self,
        prim_rect: &LayoutRect,
        clip_rect: LayoutRect,
    ) -> ClipResult {
        match *self {
            ClipItemKind::Rectangle { mode: ClipMode::Clip } => {
                let rect = clip_rect;
                if rect.contains_box(prim_rect) {
                    return ClipResult::Accept;
                }

                match rect.intersection(prim_rect) {
                    Some(..) => {
                        ClipResult::Partial
                    }
                    None => {
                        ClipResult::Reject
                    }
                }
            }
            ClipItemKind::Rectangle { mode: ClipMode::ClipOut } => {
                let rect = clip_rect;
                if rect.contains_box(prim_rect) {
                    return ClipResult::Reject;
                }

                match rect.intersection(prim_rect) {
                    Some(_) => {
                        ClipResult::Partial
                    }
                    None => {
                        ClipResult::Accept
                    }
                }
            }
            ClipItemKind::RoundedRectangle { ref radius, inset: _, mode: ClipMode::Clip } => {
                let rect = clip_rect;
                let radius = clamped_radius(radius, rect.size());
                // TODO(gw): Consider caching this in the ClipNode
                //           if it ever shows in profiles.
                if rounded_rectangle_contains_box_quick(&rect, &radius, &prim_rect) {
                    return ClipResult::Accept;
                }

                match rect.intersection(prim_rect) {
                    Some(..) => {
                        ClipResult::Partial
                    }
                    None => {
                        ClipResult::Reject
                    }
                }
            }
            ClipItemKind::RoundedRectangle { ref radius, inset: _, mode: ClipMode::ClipOut } => {
                let rect = clip_rect;
                let radius = clamped_radius(radius, rect.size());
                // TODO(gw): Consider caching this in the ClipNode
                //           if it ever shows in profiles.
                if rounded_rectangle_contains_box_quick(&rect, &radius, &prim_rect) {
                    return ClipResult::Reject;
                }

                match rect.intersection(prim_rect) {
                    Some(_) => {
                        ClipResult::Partial
                    }
                    None => {
                        ClipResult::Accept
                    }
                }
            }
            ClipItemKind::Border { .. } |
            ClipItemKind::Image { .. } |
            ClipItemKind::Path { .. } => {
                let rect = clip_rect;
                match rect.intersection(prim_rect) {
                    Some(..) => {
                        ClipResult::Partial
                    }
                    None => {
                        ClipResult::Reject
                    }
                }
            }
        }
    }
}

pub fn rounded_rectangle_contains_point(
    point: &LayoutPoint,
    rect: &LayoutRect,
    radii: &BorderRadius
) -> bool {
    if !rect.contains(*point) {
        return false;
    }

    let top_left_center = rect.min + radii.top_left.to_vector();
    if top_left_center.x > point.x && top_left_center.y > point.y &&
       !Ellipse::new(radii.top_left).contains(*point - top_left_center.to_vector()) {
        return false;
    }

    let bottom_right_center = rect.bottom_right() - radii.bottom_right.to_vector();
    if bottom_right_center.x < point.x && bottom_right_center.y < point.y &&
       !Ellipse::new(radii.bottom_right).contains(*point - bottom_right_center.to_vector()) {
        return false;
    }

    let top_right_center = rect.top_right() +
                           LayoutVector2D::new(-radii.top_right.width, radii.top_right.height);
    if top_right_center.x < point.x && top_right_center.y > point.y &&
       !Ellipse::new(radii.top_right).contains(*point - top_right_center.to_vector()) {
        return false;
    }

    let bottom_left_center = rect.bottom_left() +
                             LayoutVector2D::new(radii.bottom_left.width, -radii.bottom_left.height);
    if bottom_left_center.x > point.x && bottom_left_center.y < point.y &&
       !Ellipse::new(radii.bottom_left).contains(*point - bottom_left_center.to_vector()) {
        return false;
    }

    true
}

/// Return true if the rounded rectangle described by `container` and `radii`
/// definitely contains `containee`. May return false negatives, but never false
/// positives.
fn rounded_rectangle_contains_box_quick(
    container: &LayoutRect,
    radii: &BorderRadius,
    containee: &LayoutRect,
) -> bool {
    if !container.contains_box(containee) {
        return false;
    }

    /// Return true if `point` falls within `corner`. This only covers the
    /// upper-left case; we transform the other corners into that form.
    fn foul(point: LayoutPoint, corner: LayoutPoint) -> bool {
        point.x < corner.x && point.y < corner.y
    }

    /// Flip `pt` about the y axis (i.e. negate `x`).
    fn flip_x(pt: LayoutPoint) -> LayoutPoint {
        LayoutPoint { x: -pt.x, .. pt }
    }

    /// Flip `pt` about the x axis (i.e. negate `y`).
    fn flip_y(pt: LayoutPoint) -> LayoutPoint {
        LayoutPoint { y: -pt.y, .. pt }
    }

    if foul(containee.top_left(), container.top_left() + radii.top_left) ||
        foul(flip_x(containee.top_right()), flip_x(container.top_right()) + radii.top_right) ||
        foul(flip_y(containee.bottom_left()), flip_y(container.bottom_left()) + radii.bottom_left) ||
        foul(-containee.bottom_right(), -container.bottom_right() + radii.bottom_right)
    {
        return false;
    }

    true
}

pub fn projected_rect_contains(
    source_rect: &LayoutRect,
    transform: &LayoutToRasterTransform,
    target_rect: &RasterRect,
) -> Option<()> {
    let points = [
        transform.transform_point2d(source_rect.top_left())?,
        transform.transform_point2d(source_rect.top_right())?,
        transform.transform_point2d(source_rect.bottom_right())?,
        transform.transform_point2d(source_rect.bottom_left())?,
    ];
    let target_points = [
        target_rect.top_left(),
        target_rect.top_right(),
        target_rect.bottom_right(),
        target_rect.bottom_left(),
    ];
    // iterate the edges of the transformed polygon
    for (a, b) in points
        .iter()
        .cloned()
        .zip(points[1..].iter().cloned().chain(iter::once(points[0])))
    {
        // If this edge is redundant, it's a weird, case, and we shouldn't go
        // length in trying to take the fast path (e.g. when the whole rectangle is a point).
        // If any of edges of the target rectangle crosses the edge, it's not completely
        // inside our transformed polygon either.
        if a.approx_eq(&b) || target_points.iter().any(|&c| (b - a).cross(c - a) < 0.0) {
            return None
        }
    }

    Some(())
}


// Add a clip node into the list of clips to be processed
// for the current clip chain. Returns false if the clip
// results in the entire primitive being culled out.
fn add_clip_node_to_current_chain(
    handle: ClipDataHandle,
    clip_spatial_node_index: SpatialNodeIndex,
    clip_rect: LayoutRect,
    prim_spatial_node_index: SpatialNodeIndex,
    pic_spatial_node_index: SpatialNodeIndex,
    raster_spatial_node_index: SpatialNodeIndex,
    local_clip_rect: &mut LayoutRect,
    clip_node_info: &mut Vec<ClipNodeInfo>,
    pic_coverage_rect: &mut PictureRect,
    clip_data_store: &ClipDataStore,
    spatial_tree: &SpatialTree,
) -> bool {
    let clip_node = &clip_data_store[handle];

    // Determine the most efficient way to convert between coordinate
    // systems of the primitive and clip node.
    let conversion = ClipSpaceConversion::new(
        prim_spatial_node_index,
        clip_spatial_node_index,
        raster_spatial_node_index,
        spatial_tree,
    );

    // If we can convert spaces, try to reduce the size of the region
    // requested, and cache the conversion information for the next step.
    if let Some(clip_rect) = clip_node.item.kind.get_local_clip_rect(clip_rect) {
        match conversion {
            ClipSpaceConversion::Local => {
                *local_clip_rect = match local_clip_rect.intersection(&clip_rect) {
                    Some(rect) => rect,
                    None => return false,
                };
            }
            ClipSpaceConversion::ScaleOffset(ref scale_offset) => {
                let clip_rect = scale_offset.map_rect(&clip_rect);
                *local_clip_rect = match local_clip_rect.intersection(&clip_rect) {
                    Some(rect) => rect,
                    None => return false,
                };
            }
            ClipSpaceConversion::Transform(..) |
            ClipSpaceConversion::Indeterminate => {
                // Map the local clip rect directly into the same space as the picture
                // surface. This will often be the same space as the clip itself, which
                // results in a reduction in allocated clip mask size.

                // For simplicity, only apply this optimization if the clip is in the
                // same coord system as the picture. There are some 'advanced' perspective
                // clip tests in wrench that break without this check. Those cases are
                // never used in Gecko, and we aim to remove support in WR for that
                // in future to simplify the clipping pipeline.
                let pic_coord_system = spatial_tree
                    .get_spatial_node(pic_spatial_node_index)
                    .coordinate_system_id;

                let clip_coord_system = spatial_tree
                    .get_spatial_node(clip_spatial_node_index)
                    .coordinate_system_id;

                if pic_coord_system == clip_coord_system {
                    let mapper = SpaceMapper::new_with_target(
                        pic_spatial_node_index,
                        clip_spatial_node_index,
                        PictureRect::max_rect(),
                        spatial_tree,
                    );

                    if let Some(pic_clip_rect) = mapper.map(&clip_rect) {
                        *pic_coverage_rect = pic_clip_rect
                            .intersection(pic_coverage_rect)
                            .unwrap_or(PictureRect::zero());
                    }
                }
            }
        }
    }

    clip_node_info.push(ClipNodeInfo {
        conversion,
        handle,
        spatial_node_index: clip_spatial_node_index,
        clip_rect,
    });

    true
}

#[cfg(test)]
mod tests {
    use super::*;
    use super::projected_rect_contains;
    use euclid::{Transform3D, rect};
    use api::units::{LayoutRect, LayoutSize, LayoutPoint};

    #[test]
    fn test_empty_projected_rect() {
        assert_eq!(
            None,
            projected_rect_contains(
                &rect(10.0, 10.0, 0.0, 0.0).to_box2d(),
                &Transform3D::identity(),
                &rect(20.0, 20.0, 10.0, 10.0).to_box2d(),
            ),
            "Empty rectangle is considered to include a non-empty!"
        );
    }

    fn lr(x: f32, y: f32, w: f32, h: f32) -> LayoutRect {
        LayoutRect::from_origin_and_size(LayoutPoint::new(x, y), LayoutSize::new(w, h))
    }

    fn uniform_radius(r: f32) -> BorderRadius {
        BorderRadius::uniform(r)
    }

    fn per_corner_radius(tl: f32, tr: f32, bl: f32, br: f32) -> BorderRadius {
        BorderRadius {
            top_left: LayoutSize::new(tl, tl),
            top_right: LayoutSize::new(tr, tr),
            bottom_left: LayoutSize::new(bl, bl),
            bottom_right: LayoutSize::new(br, br),
            shape_top_left: 1.0,
            shape_top_right: 1.0,
            shape_bottom_left: 1.0,
            shape_bottom_right: 1.0,
        }
    }

    #[test]
    fn test_intersect_identical() {
        let rect = lr(0.0, 0.0, 400.0, 400.0);
        let radius = uniform_radius(20.0);
        let result = intersect_rounded_rects(rect, radius, rect, radius);
        assert!(result.is_some());
        let (r, rad) = result.unwrap();
        assert_eq!(r, rect);
        assert_eq!(rad.top_left.width, 20.0);
    }

    #[test]
    fn test_intersect_inner_fully_inside() {
        let outer = lr(0.0, 0.0, 400.0, 400.0);
        let inner = lr(50.0, 50.0, 300.0, 300.0);
        let result = intersect_rounded_rects(
            outer, uniform_radius(20.0),
            inner, uniform_radius(15.0),
        );
        assert!(result.is_some());
        let (r, rad) = result.unwrap();
        assert_eq!(r, inner);
        assert_eq!(rad.top_left.width, 15.0);
        assert_eq!(rad.bottom_right.width, 15.0);
    }

    #[test]
    fn test_intersect_shared_top_different_bottom() {
        let outer = lr(0.0, 0.0, 400.0, 400.0);
        let inner = lr(0.0, 0.0, 400.0, 350.0);
        let result = intersect_rounded_rects(
            outer, uniform_radius(20.0),
            inner, uniform_radius(15.0),
        );
        assert!(result.is_some());
        let (r, rad) = result.unwrap();
        assert_eq!(r, inner);
        assert_eq!(rad.top_left.width, 20.0);
        assert_eq!(rad.top_right.width, 20.0);
        assert_eq!(rad.bottom_left.width, 15.0);
        assert_eq!(rad.bottom_right.width, 15.0);
    }

    #[test]
    fn test_intersect_no_overlap() {
        let a = lr(0.0, 0.0, 100.0, 100.0);
        let b = lr(200.0, 200.0, 100.0, 100.0);
        let result = intersect_rounded_rects(a, uniform_radius(10.0), b, uniform_radius(10.0));
        assert!(result.is_none());
    }

    #[test]
    fn test_intersect_encroaching_corner() {
        let outer = lr(0.0, 0.0, 400.0, 400.0);
        let inner = lr(10.0, 10.0, 380.0, 380.0);
        let result = intersect_rounded_rects(
            outer, uniform_radius(200.0),
            inner, uniform_radius(15.0),
        );
        assert!(result.is_none());
    }

    #[test]
    fn test_intersect_zero_radius_no_encroach() {
        let outer = lr(0.0, 0.0, 400.0, 400.0);
        let inner = lr(50.0, 50.0, 300.0, 300.0);
        let result = intersect_rounded_rects(
            outer, uniform_radius(20.0),
            inner, BorderRadius::zero(),
        );
        assert!(result.is_some());
        let (_, rad) = result.unwrap();
        assert_eq!(rad.top_left.width, 0.0);
        assert_eq!(rad.bottom_right.width, 0.0);
    }

    #[test]
    fn test_intersect_linux_window_corners() {
        let window = lr(0.0, 0.0, 1920.0, 1080.0);
        let content = lr(0.0, 40.0, 1920.0, 1040.0);
        let window_radius = uniform_radius(10.0);
        let content_radius = per_corner_radius(8.0, 0.0, 0.0, 0.0);

        let result = intersect_rounded_rects(window, window_radius, content, content_radius);
        assert!(result.is_some());
        let (r, rad) = result.unwrap();
        assert_eq!(r, content);
        assert_eq!(rad.top_left.width, 8.0);
        assert_eq!(rad.top_right.width, 0.0);
        assert_eq!(rad.bottom_left.width, 10.0);
        assert_eq!(rad.bottom_right.width, 10.0);
    }
}

/// Try to intersect two ClipMode::Clip rounded rects (in the same coordinate
/// space) into a single rounded rect. Returns None if the two rounded rects
/// cannot be combined (e.g. their curved regions overlap in a way that can't
/// be represented by a single rounded rect).
pub fn intersect_rounded_rects(
    rect_a: LayoutRect,
    radius_a: BorderRadius,
    rect_b: LayoutRect,
    radius_b: BorderRadius,
) -> Option<(LayoutRect, BorderRadius)> {
    let result_rect = rect_a.intersection(&rect_b)?;
    if result_rect.is_empty() {
        return None;
    }

    if !radius_a.shapes_all_round() || !radius_b.shapes_all_round() {
        return None;
    }

    let result_radius = BorderRadius {
        top_left: resolve_corner_radius(
            result_rect.min.x, result_rect.min.y,
            rect_a.min.x, rect_a.min.y, radius_a.top_left,
            rect_b.min.x, rect_b.min.y, radius_b.top_left,
            1.0, 1.0,
        )?,
        top_right: resolve_corner_radius(
            result_rect.max.x, result_rect.min.y,
            rect_a.max.x, rect_a.min.y, radius_a.top_right,
            rect_b.max.x, rect_b.min.y, radius_b.top_right,
            -1.0, 1.0,
        )?,
        bottom_left: resolve_corner_radius(
            result_rect.min.x, result_rect.max.y,
            rect_a.min.x, rect_a.max.y, radius_a.bottom_left,
            rect_b.min.x, rect_b.max.y, radius_b.bottom_left,
            1.0, -1.0,
        )?,
        bottom_right: resolve_corner_radius(
            result_rect.max.x, result_rect.max.y,
            rect_a.max.x, rect_a.max.y, radius_a.bottom_right,
            rect_b.max.x, rect_b.max.y, radius_b.bottom_right,
            -1.0, -1.0,
        )?,
        shape_top_left: 1.0,
        shape_top_right: 1.0,
        shape_bottom_left: 1.0,
        shape_bottom_right: 1.0,
    };

    if !result_radius.can_use_fast_path_in(&result_rect) {
        return None;
    }

    Some((result_rect, result_radius))
}

/// Determine the radius at a single corner of the intersection of two rounded
/// rects. Each corner is identified by:
///  - (ix, iy): corner position in the intersection rect
///  - (ax, ay), ra: corner position and radius from rect A
///  - (bx, by), rb: corner position and radius from rect B
///  - (sx, sy): direction signs toward the interior (e.g. top-left = +1,+1)
fn resolve_corner_radius(
    ix: f32, iy: f32,
    ax: f32, ay: f32, ra: LayoutSize,
    bx: f32, by: f32, rb: LayoutSize,
    sx: f32, sy: f32,
) -> Option<LayoutSize> {
    let a_matches = ax == ix && ay == iy;
    let b_matches = bx == ix && by == iy;

    match (a_matches, b_matches) {
        (true, true) => {
            Some(LayoutSize::new(ra.width.max(rb.width), ra.height.max(rb.height)))
        }
        (true, false) => {
            if corner_encroaches(ix, iy, bx, by, rb, sx, sy) {
                None
            } else {
                Some(ra)
            }
        }
        (false, true) => {
            if corner_encroaches(ix, iy, ax, ay, ra, sx, sy) {
                None
            } else {
                Some(rb)
            }
        }
        (false, false) => {
            if corner_encroaches(ix, iy, ax, ay, ra, sx, sy) ||
               corner_encroaches(ix, iy, bx, by, rb, sx, sy) {
                None
            } else {
                Some(LayoutSize::zero())
            }
        }
    }
}

/// Check if a rounded corner region from a rect whose corner is at (cx, cy)
/// with radius r extends into the intersection rect at corner (ix, iy).
/// (sx, sy) are direction signs toward the rect interior from this corner.
fn corner_encroaches(
    ix: f32, iy: f32,
    cx: f32, cy: f32,
    r: LayoutSize,
    sx: f32, sy: f32,
) -> bool {
    if r.width == 0.0 || r.height == 0.0 {
        return false;
    }
    let dx = sx * (ix - cx);
    let dy = sy * (iy - cy);
    r.width > dx && r.height > dy
}

/// PolygonKeys get interned, because it's a convenient way to move the data
/// for the polygons out of the ClipItemKind and ClipItemKeyKind enums. The
/// polygon data is both interned and retrieved by the scene builder, and not
/// accessed at all by the frame builder. Another oddity is that the
/// PolygonKey contains the totality of the information about the polygon, so
/// the InternData and StoreData types are both PolygonKey.
#[derive(Copy, Clone, Debug, Hash, MallocSizeOf, PartialEq, Eq)]
#[cfg_attr(any(feature = "serde"), derive(Deserialize, Serialize))]
pub enum PolygonIntern {}

pub type PolygonDataHandle = intern::Handle<PolygonIntern>;

impl intern::InternDebug for PolygonKey {}

impl intern::Internable for PolygonIntern {
    type Key = PolygonKey;
    type StoreData = PolygonKey;
    type InternData = PolygonKey;
    const PROFILE_COUNTER: usize = crate::profiler::INTERNED_POLYGONS;
}
