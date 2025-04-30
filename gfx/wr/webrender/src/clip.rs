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
use api::{BoxShadowClipMode, FillRule, ImageKey, ImageRendering};
use api::units::*;
use crate::image_tiling::{self, Repetition};
use crate::border::{ensure_no_corner_overlap, BorderRadiusAu};
use crate::box_shadow::{BLUR_SAMPLE_SCALE, BoxShadowClipSource, BoxShadowCacheKey};
use crate::spatial_tree::{SpatialTree, SpatialNodeIndex};
use crate::ellipse::Ellipse;
use crate::gpu_cache::GpuCache;
use crate::gpu_types::{BoxShadowStretchMode};
use crate::intern;
use crate::internal_types::{FastHashMap, FastHashSet, LayoutPrimitiveInfo};
use crate::prim_store::{VisibleMaskImageTile};
use crate::prim_store::{PointKey, SizeKey, RectangleKey, PolygonKey};
use crate::render_task_cache::to_cache_size;
use crate::render_task::RenderTask;
use crate::render_task_graph::RenderTaskGraphBuilder;
use crate::resource_cache::{ImageRequest, ResourceCache};
use crate::scene_builder_thread::Interners;
use crate::space::SpaceMapper;
use crate::util::{clamp_to_scale_factor, extract_inner_rect_safe, project_rect, MatrixHelpers, MaxRect, ScaleOffset};
use euclid::approxeq::ApproxEq;
use std::{iter, ops, u32, mem};

/// A (non-leaf) node inside a clip-tree
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(MallocSizeOf)]
pub struct ClipTreeNode {
    pub handle: ClipDataHandle,
    pub parent: ClipNodeId,

    children: Vec<ClipNodeId>,

    // TODO(gw): Consider adding a default leaf for cases when the local_clip_rect is not relevant,
    //           that can be shared among primitives (to reduce amount of clip-chain building).
}

/// A leaf node in a clip-tree. Any primitive that is clipped will have a handle to
/// a clip-tree leaf.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(MallocSizeOf)]
pub struct ClipTreeLeaf {
    pub node_id: ClipNodeId,

    // TODO(gw): For now, this preserves the ability to build a culling rect
    //           from the supplied leaf local clip rect on the primitive. In
    //           future, we'll expand this to be more efficient by combining
    //           it will compatible clip rects from the `node_id`.
    pub local_clip_rect: LayoutRect,
}

/// ID for a ClipTreeNode
#[derive(Debug, Copy, Clone, PartialEq, MallocSizeOf, Eq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipNodeId(u32);

impl ClipNodeId {
    pub const NONE: ClipNodeId = ClipNodeId(0);
}

/// ID for a ClipTreeLeaf
#[derive(Debug, Copy, Clone, PartialEq, MallocSizeOf, Eq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipLeafId(u32);

/// A clip-tree built during scene building and used during frame-building to apply clips to primitives.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipTree {
    nodes: Vec<ClipTreeNode>,
    leaves: Vec<ClipTreeLeaf>,
    clip_root_stack: Vec<ClipNodeId>,
}

impl ClipTree {
    pub fn new() -> Self {
        ClipTree {
            nodes: vec![
                ClipTreeNode {
                    handle: ClipDataHandle::INVALID,
                    children: Vec::new(),
                    parent: ClipNodeId::NONE,
                }
            ],
            leaves: Vec::new(),
            clip_root_stack: vec![
                ClipNodeId::NONE,
            ],
        }
    }

    pub fn reset(&mut self) {
        self.nodes.clear();
        self.nodes.push(ClipTreeNode {
            handle: ClipDataHandle::INVALID,
            children: Vec::new(),
            parent: ClipNodeId::NONE,
        });

        self.leaves.clear();

        self.clip_root_stack.clear();
        self.clip_root_stack.push(ClipNodeId::NONE);
    }

    /// Add a set of clips to the provided tree node id, reusing existing
    /// nodes in the tree where possible
    fn add_impl(
        id: ClipNodeId,
        clips: &[ClipDataHandle],
        nodes: &mut Vec<ClipTreeNode>,
    ) -> ClipNodeId {
        if clips.is_empty() {
            return id;
        }

        let handle = clips[0];
        let next_clips = &clips[1..];

        let node_index = nodes[id.0 as usize]
            .children
            .iter()
            .find(|n| nodes[n.0 as usize].handle == handle)
            .cloned();

        let node_index = match node_index {
            Some(node_index) => node_index,
            None => {
                let node_index = ClipNodeId(nodes.len() as u32);
                nodes[id.0 as usize].children.push(node_index);
                let node = ClipTreeNode {
                    handle,
                    children: Vec::new(),
                    parent: id,
                };
                nodes.push(node);
                node_index
            }
        };

        ClipTree::add_impl(
            node_index,
            next_clips,
            nodes,
        )
    }

    /// Add a set of clips to the provided tree node id, reusing existing
    /// nodes in the tree where possible
    pub fn add(
        &mut self,
        root: ClipNodeId,
        clips: &[ClipDataHandle],
    ) -> ClipNodeId {
        ClipTree::add_impl(
            root,
            clips,
            &mut self.nodes,
        )
    }

    /// Get the current clip root (the node in the clip-tree where clips can be
    /// ignored when building the clip-chain instance for a primitive)
    pub fn current_clip_root(&self) -> ClipNodeId {
        self.clip_root_stack.last().cloned().unwrap()
    }

    /// Push a clip root (e.g. when a surface is encountered) that prevents clips
    /// from this node and above being applied to primitives within the root.
    pub fn push_clip_root_leaf(&mut self, clip_leaf_id: ClipLeafId) {
        let leaf = &self.leaves[clip_leaf_id.0 as usize];
        self.clip_root_stack.push(leaf.node_id);
    }

    /// Push a clip root (e.g. when a surface is encountered) that prevents clips
    /// from this node and above being applied to primitives within the root.
    pub fn push_clip_root_node(&mut self, clip_node_id: ClipNodeId) {
        self.clip_root_stack.push(clip_node_id);
    }

    /// Pop a clip root, when exiting a surface.
    pub fn pop_clip_root(&mut self) {
        self.clip_root_stack.pop().unwrap();
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
    pub fn get_leaf(&self, id: ClipLeafId) -> &ClipTreeLeaf {
        &self.leaves[id.0 as usize]
    }

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

            for child_id in &node.children {
                print_node(*child_id, nodes, pt);
            }

            pt.end_level();
        }

        fn print_leaf<T: crate::print_tree::PrintTreePrinter>(
            id: ClipLeafId,
            leaves: &[ClipTreeLeaf],
            pt: &mut T,
        ) {
            let leaf = &leaves[id.0 as usize];

            pt.new_level(format!("{:?}", id));
            pt.add_item(format!("node_id: {:?}", leaf.node_id));
            pt.add_item(format!("local_clip_rect: {:?}", leaf.local_clip_rect));
            pt.end_level();
        }

        let mut pt = PrintTree::new("clip tree");
        print_node(ClipNodeId::NONE, &self.nodes, &mut pt);

        for i in 0 .. self.leaves.len() {
            print_leaf(ClipLeafId(i as u32), &self.leaves, &mut pt);
        }
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

/// Represents a clip-chain as defined by the public API that we decompose in to
/// the clip-tree. In future, we would like to remove this and have Gecko directly
/// build the clip-tree.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipChain {
    parent: Option<usize>,
    clips: Vec<ClipDataHandle>,
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipStackEntry {
    /// Cache the previous clip-chain build, since this is a common case
    last_clip_chain_cache: Option<(ClipChainId, ClipNodeId)>,

    /// Set of clips that were already seen and included in clip_node_id
    seen_clips: FastHashSet<ClipDataHandle>,

    /// The build clip_node_id for this level of the stack
    clip_node_id: ClipNodeId,
}

/// Used by the scene builder to build the clip-tree that is part of the built scene.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipTreeBuilder {
    /// Clips defined by the display list
    clip_map: FastHashMap<ClipId, ClipDataHandle>,

    /// Clip-chains defined by the display list
    clip_chains: Vec<ClipChain>,
    clip_chain_map: FastHashMap<ClipChainId, usize>,

    /// List of clips pushed/popped by grouping items, such as stacking contexts and iframes
    clip_stack: Vec<ClipStackEntry>,

    /// The tree we are building
    tree: ClipTree,

    /// A temporary buffer stored here to avoid constant heap allocs/frees
    clip_handles_buffer: Vec<ClipDataHandle>,
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
    ) {
        self.clip_map.insert(id, handle);
    }

    /// Define a new rounded rect clip
    pub fn define_rounded_rect_clip(
        &mut self,
        id: ClipId,
        handle: ClipDataHandle,
    ) {
        self.clip_map.insert(id, handle);
    }

    /// Define a image mask clip
    pub fn define_image_mask_clip(
        &mut self,
        id: ClipId,
        handle: ClipDataHandle,
    ) {
        self.clip_map.insert(id, handle);
    }

    /// Define a clip-chain
    pub fn define_clip_chain<I: Iterator<Item = ClipId>>(
        &mut self,
        id: ClipChainId,
        parent: Option<ClipChainId>,
        clips: I,
    ) {
        let parent = parent.map(|ref id| self.clip_chain_map[id]);
        let index = self.clip_chains.len();
        let clips = clips.map(|clip_id| {
            self.clip_map[&clip_id]
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
    ) {
        let (mut clip_node_id, mut seen_clips) = {
            let prev = self.clip_stack.last().unwrap();
            (prev.clip_node_id, prev.seen_clips.clone())
        };

        if let Some(clip_chain_id) = clip_chain_id {
            if clip_chain_id != ClipChainId::INVALID {
                self.clip_handles_buffer.clear();

                let clip_chain_index = self.clip_chain_map[&clip_chain_id];
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
        let clip_index = self.clip_map[&clip_id];

        if seen_clips.insert(clip_index) {
            self.clip_handles_buffer.push(clip_index);
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
        seen_clips: &mut FastHashSet<ClipDataHandle>,
        output: &mut Vec<ClipDataHandle>,
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

        for clip_index in clip_chain.clips.iter().rev() {
            if seen_clips.insert(*clip_index) {
                output.push(*clip_index);
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

            let clip_chain_index = self.clip_chain_map[&clip_chain_id];

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
            for handle in &self.clip_handles_buffer {
                clip_stack.seen_clips.remove(handle);
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
        interners: &Interners,
    ) -> bool {
        let clip_chain = &self.clip_chains[clip_chain_index];

        for clip_handle in &clip_chain.clips {
            let clip_info = &interners.clip[*clip_handle];

            if let ClipNodeKind::Complex = clip_info.key.kind.node_kind() {
                return true;
            }
        }

        match clip_chain.parent {
            Some(parent) => self.has_complex_clips_impl(parent, interners),
            None => false,
        }
    }

    /// Check if a clip-chain has complex (non-rectangular) clips
    pub fn clip_chain_has_complex_clips(
        &self,
        clip_chain_id: ClipChainId,
        interners: &Interners,
    ) -> bool {
        let clip_chain_index = self.clip_chain_map[&clip_chain_id];
        self.has_complex_clips_impl(clip_chain_index, interners)
    }

    /// Check if a clip-node has complex (non-rectangular) clips
    pub fn clip_node_has_complex_clips(
        &self,
        clip_node_id: ClipNodeId,
        interners: &Interners,
    ) -> bool {
        let mut current = clip_node_id;

        while current != ClipNodeId::NONE {
            let node = &self.tree.nodes[current.0 as usize];
            let clip_info = &interners.clip[node.handle];

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
            leaves: Vec::new(),
            clip_root_stack: Vec::new(),
        })
    }

    /// Get a clip node by id
    pub fn get_node(&self, id: ClipNodeId) -> &ClipTreeNode {
        assert!(id != ClipNodeId::NONE);

        &self.tree.nodes[id.0 as usize]
    }

    /// Get a clip leaf by id
    pub fn get_leaf(&self, id: ClipLeafId) -> &ClipTreeLeaf {
        &self.tree.leaves[id.0 as usize]
    }

    /// Build a clip-leaf for a tile-cache
    pub fn build_for_tile_cache(
        &mut self,
        clip_node_id: ClipNodeId,
        extra_clips: &[ClipId],
    ) -> ClipLeafId {
        self.clip_handles_buffer.clear();

        for clip_id in extra_clips {
            let handle = self.clip_map[clip_id];
            self.clip_handles_buffer.push(handle);
        }

        let node_id = self.tree.add(
            clip_node_id,
            &self.clip_handles_buffer,
        );

        let clip_leaf_id = ClipLeafId(self.tree.leaves.len() as u32);

        self.tree.leaves.push(ClipTreeLeaf {
            node_id,
            local_clip_rect: LayoutRect::max_rect(),
        });

        clip_leaf_id
    }

    /// Build a clip-leaf for a picture
    pub fn build_for_picture(
        &mut self,
        clip_node_id: ClipNodeId,
    ) -> ClipLeafId {
        let node_id = self.tree.add(
            clip_node_id,
            &[],
        );

        let clip_leaf_id = ClipLeafId(self.tree.leaves.len() as u32);

        self.tree.leaves.push(ClipTreeLeaf {
            node_id,
            local_clip_rect: LayoutRect::max_rect(),
        });

        clip_leaf_id
    }

    /// Build a clip-leaf for a normal primitive
    pub fn build_for_prim(
        &mut self,
        clip_node_id: ClipNodeId,
        info: &LayoutPrimitiveInfo,
        extra_clips: &[ClipItemKey],
        interners: &mut Interners,
    ) -> ClipLeafId {

        let node_id = if extra_clips.is_empty() {
            clip_node_id
        } else {
            // TODO(gw): Cache the previous build of clip-node / clip-leaf to handle cases where we get a
            //           lot of primitives referencing the same clip set (e.g. dl_mutate and similar tests)
            self.clip_handles_buffer.clear();

            for item in extra_clips {
                // Intern this clip item, and store the handle
                // in the clip chain node.
                let handle = interners.clip.intern(item, || {
                    ClipInternData {
                        key: item.clone(),
                    }
                });

                self.clip_handles_buffer.push(handle);
            }

            self.tree.add(
                clip_node_id,
                &self.clip_handles_buffer,
            )
        };

        let clip_leaf_id = ClipLeafId(self.tree.leaves.len() as u32);

        self.tree.leaves.push(ClipTreeLeaf {
            node_id,
            local_clip_rect: info.clip_rect,
        });

        clip_leaf_id
    }

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
            ClipItemKeyKind::Rectangle(rect, mode) => {
                ClipItemKind::Rectangle { rect: rect.into(), mode }
            }
            ClipItemKeyKind::RoundedRectangle(rect, radius, mode) => {
                ClipItemKind::RoundedRectangle {
                    rect: rect.into(),
                    radius: radius.into(),
                    mode,
                }
            }
            ClipItemKeyKind::ImageMask(rect, image, polygon_handle) => {
                ClipItemKind::Image {
                    image,
                    rect: rect.into(),
                    polygon_handle,
                }
            }
            ClipItemKeyKind::BoxShadow(shadow_rect_fract_offset, shadow_rect_size, shadow_radius, prim_shadow_rect, blur_radius, clip_mode) => {
                ClipItemKind::new_box_shadow(
                    shadow_rect_fract_offset.into(),
                    shadow_rect_size.into(),
                    shadow_radius.into(),
                    prim_shadow_rect.into(),
                    blur_radius.to_f32_px(),
                    clip_mode,
                )
            }
        };

        ClipNode {
            item: ClipItem {
                kind,
                spatial_node_index: item.spatial_node_index,
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
        const USE_FAST_PATH = 0x4;
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
    pub flags: ClipNodeFlags,
    pub visible_tiles: Option<ops::Range<usize>>,
}

impl ClipNodeInstance {
    pub fn has_visible_tiles(&self) -> bool {
        self.visible_tiles.is_some()
    }
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
// todo(gw): optimize:
//  separate arrays for matrices
//  cache and only build as needed.
//TODO: merge with `CoordinateSpaceMapping`?
#[derive(Debug, MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub enum ClipSpaceConversion {
    Local,
    ScaleOffset(ScaleOffset),
    Transform(LayoutToVisTransform),
}

impl ClipSpaceConversion {
    /// Construct a new clip space converter between two spatial nodes.
    pub fn new(
        prim_spatial_node_index: SpatialNodeIndex,
        clip_spatial_node_index: SpatialNodeIndex,
        visibility_spatial_node_index: SpatialNodeIndex,
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
        } else {
            ClipSpaceConversion::Transform(
                spatial_tree.get_relative_transform(
                    clip_spatial_node_index,
                    visibility_spatial_node_index,
                ).into_transform().cast_unit()
            )
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
            ClipSpaceConversion::Transform(..) => {
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
}

impl ClipNodeInfo {
    fn create_instance(
        &self,
        node: &ClipNode,
        clipped_rect: &LayoutRect,
        gpu_cache: &mut GpuCache,
        resource_cache: &mut ResourceCache,
        mask_tiles: &mut Vec<VisibleMaskImageTile>,
        spatial_tree: &SpatialTree,
        rg_builder: &mut RenderTaskGraphBuilder,
        request_resources: bool,
    ) -> Option<ClipNodeInstance> {
        // Calculate some flags that are required for the segment
        // building logic.
        let mut flags = self.conversion.to_flags();

        // Some clip shaders support a fast path mode for simple clips.
        // TODO(gw): We could also apply fast path when segments are created, since we only write
        //           the mask for a single corner at a time then, so can always consider radii uniform.
        let is_raster_2d =
            flags.contains(ClipNodeFlags::SAME_COORD_SYSTEM) ||
            spatial_tree
                .get_world_viewport_transform(node.item.spatial_node_index)
                .is_2d_axis_aligned();
        if is_raster_2d && node.item.kind.supports_fast_path_rendering() {
            flags |= ClipNodeFlags::USE_FAST_PATH;
        }

        let mut visible_tiles = None;

        if let ClipItemKind::Image { rect, image, .. } = node.item.kind {
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
                        &visible_rect,
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
                                    gpu_cache,
                                );
                            }

                            let task_id = rg_builder.add().init(
                                RenderTask::new_image(props.descriptor.size, req)
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
                        resource_cache.request_image(request, gpu_cache);
                    }

                    let tile_range_start = mask_tiles.len();

                    let task_id = rg_builder.add().init(
                        RenderTask::new_image(props.descriptor.size, request)
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
            flags,
            visible_tiles,
        })
    }
}

impl ClipNode {
    pub fn update(
        &mut self,
        device_pixel_scale: DevicePixelScale,
    ) {
        match self.item.kind {
            ClipItemKind::Image { .. } |
            ClipItemKind::Rectangle { .. } |
            ClipItemKind::RoundedRectangle { .. } => {}

            ClipItemKind::BoxShadow { ref mut source } => {
                // Quote from https://drafts.csswg.org/css-backgrounds-3/#shadow-blur
                // "the image that would be generated by applying to the shadow a
                // Gaussian blur with a standard deviation equal to half the blur radius."
                let blur_radius_dp = source.blur_radius * 0.5;

                // Create scaling from requested size to cache size.
                let mut content_scale = LayoutToWorldScale::new(1.0) * device_pixel_scale;
                content_scale.0 = clamp_to_scale_factor(content_scale.0, false);

                // Create the cache key for this box-shadow render task.
                let cache_size = to_cache_size(source.shadow_rect_alloc_size, &mut content_scale);

                let bs_cache_key = BoxShadowCacheKey {
                    blur_radius_dp: (blur_radius_dp * content_scale.0).round() as i32,
                    clip_mode: source.clip_mode,
                    original_alloc_size: (source.original_alloc_size * content_scale).round().to_i32(),
                    br_top_left: (source.shadow_radius.top_left * content_scale).round().to_i32(),
                    br_top_right: (source.shadow_radius.top_right * content_scale).round().to_i32(),
                    br_bottom_right: (source.shadow_radius.bottom_right * content_scale).round().to_i32(),
                    br_bottom_left: (source.shadow_radius.bottom_left * content_scale).round().to_i32(),
                    device_pixel_scale: Au::from_f32_px(content_scale.0),
                };

                source.cache_key = Some((cache_size, bs_cache_key));
            }
        }
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
}

// A clip chain instance is what gets built for a given clip
// chain id + local primitive region + positioning node.
#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct ClipChainInstance {
    pub clips_range: ClipNodeRange,
    // Combined clip rect for clips that are in the
    // same coordinate system as the primitive.
    pub local_clip_rect: LayoutRect,
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

    /// Setup the active clip chains for building a clip chain instance.
    pub fn set_active_clips(
        &mut self,
        prim_spatial_node_index: SpatialNodeIndex,
        pic_spatial_node_index: SpatialNodeIndex,
        visibility_spatial_node_index: SpatialNodeIndex,
        clip_leaf_id: ClipLeafId,
        spatial_tree: &SpatialTree,
        clip_data_store: &ClipDataStore,
        clip_tree: &ClipTree,
    ) {
        self.active_clip_node_info.clear();
        self.active_local_clip_rect = None;
        self.active_pic_coverage_rect = PictureRect::max_rect();

        let clip_root = clip_tree.current_clip_root();
        let clip_leaf = clip_tree.get_leaf(clip_leaf_id);

        let mut local_clip_rect = clip_leaf.local_clip_rect;
        let mut current = clip_leaf.node_id;

        while current != clip_root {
            let node = clip_tree.get_node(current);

            if !add_clip_node_to_current_chain(
                node.handle,
                prim_spatial_node_index,
                pic_spatial_node_index,
                visibility_spatial_node_index,
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

    /// Setup the active clip chains, based on an existing primitive clip chain instance.
    pub fn set_active_clips_from_clip_chain(
        &mut self,
        prim_clip_chain: &ClipChainInstance,
        prim_spatial_node_index: SpatialNodeIndex,
        visibility_spatial_node_index: SpatialNodeIndex,
        spatial_tree: &SpatialTree,
        clip_data_store: &ClipDataStore,
    ) {
        // TODO(gw): Although this does less work than set_active_clips(), it does
        //           still do some unnecessary work (such as the clip space conversion).
        //           We could consider optimizing this if it ever shows up in a profile.

        self.active_clip_node_info.clear();
        self.active_local_clip_rect = Some(prim_clip_chain.local_clip_rect);
        self.active_pic_coverage_rect = prim_clip_chain.pic_coverage_rect;

        let clip_instances = &self
            .clip_node_instances[prim_clip_chain.clips_range.to_range()];
        for clip_instance in clip_instances {
            let clip = &clip_data_store[clip_instance.handle];
            let conversion = ClipSpaceConversion::new(
                prim_spatial_node_index,
                clip.item.spatial_node_index,
                visibility_spatial_node_index,
                spatial_tree,
            );
            self.active_clip_node_info.push(ClipNodeInfo {
                handle: clip_instance.handle,
                conversion,
            });
        }
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
                ClipItemKind::Image { .. } |
                ClipItemKind::BoxShadow { .. } |
                ClipItemKind::RoundedRectangle { mode: ClipMode::ClipOut, .. } => {
                    return None;
                }
                // Normal Clip rects are already handled by the clip-chain pic_coverage_rect,
                // no need to do anything here
                ClipItemKind::Rectangle { mode: ClipMode::Clip, .. } => {}
                ClipItemKind::RoundedRectangle { mode: ClipMode::Clip, rect, radius } => {
                    // Get an inner rect for the rounded-rect clip
                    let local_inner_rect = match extract_inner_rect_safe(&rect, &radius) {
                        Some(rect) => rect,
                        None => return None,
                    };

                    // Map it from local -> picture space
                    let mapper = SpaceMapper::new_with_target(
                        clip_chain.pic_spatial_node_index,
                        clip_node.item.spatial_node_index,
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

    // Directly construct a clip node range, ready for rendering, from an interned clip handle.
    // Typically useful for drawing specific clips on custom pattern / child render tasks that
    // aren't primitives.
    // TODO(gw): For now, we assume they are local clips only - in future we might want to support
    //           non-local clips.
    pub fn push_clip_instance(
        &mut self,
        handle: ClipDataHandle,
    ) -> ClipNodeRange {
        let first = self.clip_node_instances.len() as u32;

        self.clip_node_instances.push(ClipNodeInstance {
            handle,
            flags: ClipNodeFlags::SAME_COORD_SYSTEM | ClipNodeFlags::SAME_SPATIAL_NODE,
            visible_tiles: None,
        });

        ClipNodeRange {
            first,
            count: 1,
        }
    }

    /// The main interface external code uses. Given a local primitive, positioning
    /// information, and a clip chain id, build an optimized clip chain instance.
    pub fn build_clip_chain_instance(
        &mut self,
        local_prim_rect: LayoutRect,
        prim_to_pic_mapper: &SpaceMapper<LayoutPixel, PicturePixel>,
        pic_to_vis_mapper: &SpaceMapper<PicturePixel, VisPixel>,
        spatial_tree: &SpatialTree,
        gpu_cache: &mut GpuCache,
        resource_cache: &mut ResourceCache,
        device_pixel_scale: DevicePixelScale,
        culling_rect: &VisRect,
        clip_data_store: &mut ClipDataStore,
        rg_builder: &mut RenderTaskGraphBuilder,
        request_resources: bool,
    ) -> Option<ClipChainInstance> {
        let local_clip_rect = match self.active_local_clip_rect {
            Some(rect) => rect,
            None => return None,
        };
        profile_scope!("build_clip_chain_instance");

        let local_bounding_rect = local_prim_rect.intersection(&local_clip_rect)?;
        let mut pic_coverage_rect = prim_to_pic_mapper.map(&local_bounding_rect)?;
        let vis_clip_rect = pic_to_vis_mapper.map(&pic_coverage_rect)?;

        // Now, we've collected all the clip nodes that *potentially* affect this
        // primitive region, and reduced the size of the prim region as much as possible.

        // Run through the clip nodes, and see which ones affect this prim region.

        let first_clip_node_index = self.clip_node_instances.len() as u32;
        let mut has_non_local_clips = false;
        let mut needs_mask = false;

        // For each potential clip node
        for node_info in self.active_clip_node_info.drain(..) {
            let node = &mut clip_data_store[node_info.handle];

            // See how this clip affects the prim region.
            let clip_result = match node_info.conversion {
                ClipSpaceConversion::Local => {
                    node.item.kind.get_clip_result(&local_bounding_rect)
                }
                ClipSpaceConversion::ScaleOffset(ref scale_offset) => {
                    has_non_local_clips = true;
                    node.item.kind.get_clip_result(&scale_offset.unmap_rect(&local_bounding_rect))
                }
                ClipSpaceConversion::Transform(ref transform) => {
                    has_non_local_clips = true;
                    node.item.kind.get_clip_result_complex(
                        transform,
                        &vis_clip_rect,
                        culling_rect,
                    )
                }
            };

            match clip_result {
                ClipResult::Accept => {
                    // Doesn't affect the primitive at all, so skip adding to list
                }
                ClipResult::Reject => {
                    // Completely clips the supplied prim rect
                    return None;
                }
                ClipResult::Partial => {
                    // Needs a mask -> add to clip node indices

                    // TODO(gw): Ensure this only runs once on each node per frame?
                    node.update(device_pixel_scale);

                    // Create the clip node instance for this clip node
                    if let Some(instance) = node_info.create_instance(
                        node,
                        &local_bounding_rect,
                        gpu_cache,
                        resource_cache,
                        &mut self.mask_tiles,
                        spatial_tree,
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
                            ClipItemKind::Image { .. } |
                            ClipItemKind::BoxShadow { .. } => {
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

// The ClipItemKey is a hashable representation of the contents
// of a clip item. It is used during interning to de-duplicate
// clip nodes between frames and display lists. This allows quick
// comparison of clip node equality by handle, and also allows
// the uploaded GPU cache handle to be retained between display lists.
// TODO(gw): Maybe we should consider constructing these directly
//           in the DL builder?
#[derive(Copy, Debug, Clone, Eq, MallocSizeOf, PartialEq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum ClipItemKeyKind {
    Rectangle(RectangleKey, ClipMode),
    RoundedRectangle(RectangleKey, BorderRadiusAu, ClipMode),
    ImageMask(RectangleKey, ImageKey, Option<PolygonDataHandle>),
    BoxShadow(PointKey, SizeKey, BorderRadiusAu, RectangleKey, Au, BoxShadowClipMode),
}

impl ClipItemKeyKind {
    pub fn rectangle(rect: LayoutRect, mode: ClipMode) -> Self {
        ClipItemKeyKind::Rectangle(rect.into(), mode)
    }

    pub fn rounded_rect(rect: LayoutRect, mut radii: BorderRadius, mode: ClipMode) -> Self {
        if radii.is_zero() {
            ClipItemKeyKind::rectangle(rect, mode)
        } else {
            ensure_no_corner_overlap(&mut radii, rect.size());
            ClipItemKeyKind::RoundedRectangle(
                rect.into(),
                radii.into(),
                mode,
            )
        }
    }

    pub fn image_mask(image_mask: &ImageMask, mask_rect: LayoutRect,
                      polygon_handle: Option<PolygonDataHandle>) -> Self {
        ClipItemKeyKind::ImageMask(
            mask_rect.into(),
            image_mask.image,
            polygon_handle,
        )
    }

    pub fn box_shadow(
        shadow_rect: LayoutRect,
        shadow_radius: BorderRadius,
        prim_shadow_rect: LayoutRect,
        blur_radius: f32,
        clip_mode: BoxShadowClipMode,
    ) -> Self {
        // Get the fractional offsets required to match the
        // source rect with a minimal rect.
        let fract_offset = LayoutPoint::new(
            shadow_rect.min.x.fract().abs(),
            shadow_rect.min.y.fract().abs(),
        );

        ClipItemKeyKind::BoxShadow(
            fract_offset.into(),
            shadow_rect.size().into(),
            shadow_radius.into(),
            prim_shadow_rect.into(),
            Au::from_f32_px(blur_radius),
            clip_mode,
        )
    }

    pub fn node_kind(&self) -> ClipNodeKind {
        match *self {
            ClipItemKeyKind::Rectangle(_, ClipMode::Clip) => ClipNodeKind::Rectangle,

            ClipItemKeyKind::Rectangle(_, ClipMode::ClipOut) |
            ClipItemKeyKind::RoundedRectangle(..) |
            ClipItemKeyKind::ImageMask(..) |
            ClipItemKeyKind::BoxShadow(..) => ClipNodeKind::Complex,
        }
    }
}

#[derive(Debug, Copy, Clone, Eq, MallocSizeOf, PartialEq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipItemKey {
    pub kind: ClipItemKeyKind,
    pub spatial_node_index: SpatialNodeIndex,
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
    Rectangle {
        rect: LayoutRect,
        mode: ClipMode,
    },
    RoundedRectangle {
        rect: LayoutRect,
        radius: BorderRadius,
        mode: ClipMode,
    },
    Image {
        image: ImageKey,
        rect: LayoutRect,
        polygon_handle: Option<PolygonDataHandle>,
    },
    BoxShadow {
        source: BoxShadowClipSource,
    },
}

#[derive(Debug, MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ClipItem {
    pub kind: ClipItemKind,
    pub spatial_node_index: SpatialNodeIndex,
}

fn compute_box_shadow_parameters(
    shadow_rect_fract_offset: LayoutPoint,
    shadow_rect_size: LayoutSize,
    mut shadow_radius: BorderRadius,
    prim_shadow_rect: LayoutRect,
    blur_radius: f32,
    clip_mode: BoxShadowClipMode,
) -> BoxShadowClipSource {
    // Make sure corners don't overlap.
    ensure_no_corner_overlap(&mut shadow_radius, shadow_rect_size);

    let fract_size = LayoutSize::new(
        shadow_rect_size.width.fract().abs(),
        shadow_rect_size.height.fract().abs(),
    );

    // Create a minimal size primitive mask to blur. In this
    // case, we ensure the size of each corner is the same,
    // to simplify the shader logic that stretches the blurred
    // result across the primitive.
    let max_corner_width = shadow_radius.top_left.width
                                .max(shadow_radius.bottom_left.width)
                                .max(shadow_radius.top_right.width)
                                .max(shadow_radius.bottom_right.width);
    let max_corner_height = shadow_radius.top_left.height
                                .max(shadow_radius.bottom_left.height)
                                .max(shadow_radius.top_right.height)
                                .max(shadow_radius.bottom_right.height);

    // Get maximum distance that can be affected by given blur radius.
    let blur_region = (BLUR_SAMPLE_SCALE * blur_radius).ceil();

    // If the largest corner is smaller than the blur radius, we need to ensure
    // that it's big enough that the corners don't affect the middle segments.
    let used_corner_width = max_corner_width.max(blur_region);
    let used_corner_height = max_corner_height.max(blur_region);

    // Minimal nine-patch size, corner + internal + corner.
    let min_shadow_rect_size = LayoutSize::new(
        2.0 * used_corner_width + blur_region,
        2.0 * used_corner_height + blur_region,
    );

    // The minimal rect to blur.
    let mut minimal_shadow_rect = LayoutRect::from_origin_and_size(
        LayoutPoint::new(
            blur_region + shadow_rect_fract_offset.x,
            blur_region + shadow_rect_fract_offset.y,
        ),
        LayoutSize::new(
            min_shadow_rect_size.width + fract_size.width,
            min_shadow_rect_size.height + fract_size.height,
        ),
    );

    // If the width or height ends up being bigger than the original
    // primitive shadow rect, just blur the entire rect along that
    // axis and draw that as a simple blit. This is necessary for
    // correctness, since the blur of one corner may affect the blur
    // in another corner.
    let mut stretch_mode_x = BoxShadowStretchMode::Stretch;
    if shadow_rect_size.width < minimal_shadow_rect.width() {
        minimal_shadow_rect.max.x = minimal_shadow_rect.min.x + shadow_rect_size.width;
        stretch_mode_x = BoxShadowStretchMode::Simple;
    }

    let mut stretch_mode_y = BoxShadowStretchMode::Stretch;
    if shadow_rect_size.height < minimal_shadow_rect.height() {
        minimal_shadow_rect.max.y = minimal_shadow_rect.min.y + shadow_rect_size.height;
        stretch_mode_y = BoxShadowStretchMode::Simple;
    }

    // Expand the shadow rect by enough room for the blur to take effect.
    let shadow_rect_alloc_size = LayoutSize::new(
        2.0 * blur_region + minimal_shadow_rect.width().ceil(),
        2.0 * blur_region + minimal_shadow_rect.height().ceil(),
    );

    BoxShadowClipSource {
        original_alloc_size: shadow_rect_alloc_size,
        shadow_rect_alloc_size,
        shadow_radius,
        prim_shadow_rect,
        blur_radius,
        clip_mode,
        stretch_mode_x,
        stretch_mode_y,
        render_task: None,
        cache_key: None,
        minimal_shadow_rect,
    }
}

impl ClipItemKind {
    pub fn new_box_shadow(
        shadow_rect_fract_offset: LayoutPoint,
        shadow_rect_size: LayoutSize,
        mut shadow_radius: BorderRadius,
        prim_shadow_rect: LayoutRect,
        blur_radius: f32,
        clip_mode: BoxShadowClipMode,
    ) -> Self {
        let mut source = compute_box_shadow_parameters(
            shadow_rect_fract_offset,
            shadow_rect_size,
            shadow_radius,
            prim_shadow_rect,
            blur_radius,
            clip_mode,
        );

        fn needed_downscaling(source: &BoxShadowClipSource) -> Option<f32> {
            // This size is fairly arbitrary, but it's the same as the size that
            // we use to avoid caching big blurred stacking contexts.
            //
            // If you change it, ensure that the reftests
            // box-shadow-large-blur-radius-* still hit the downscaling path,
            // and that they render correctly.
            const MAX_SIZE: f32 = 2048.;

            let max_dimension =
                source.shadow_rect_alloc_size.width.max(source.shadow_rect_alloc_size.height);

            if max_dimension > MAX_SIZE {
                Some(MAX_SIZE / max_dimension)
            } else {
                None
            }
        }

        if let Some(downscale) = needed_downscaling(&source) {
            shadow_radius.bottom_left.height *= downscale;
            shadow_radius.bottom_left.width *= downscale;
            shadow_radius.bottom_right.height *= downscale;
            shadow_radius.bottom_right.width *= downscale;
            shadow_radius.top_left.height *= downscale;
            shadow_radius.top_left.width *= downscale;
            shadow_radius.top_right.height *= downscale;
            shadow_radius.top_right.width *= downscale;

            let original_alloc_size = source.shadow_rect_alloc_size;

            source = compute_box_shadow_parameters(
                shadow_rect_fract_offset * downscale,
                shadow_rect_size * downscale,
                shadow_radius,
                prim_shadow_rect,
                blur_radius * downscale,
                clip_mode,
            );
            source.original_alloc_size = original_alloc_size;
        }
        ClipItemKind::BoxShadow { source }
    }

    /// Returns true if this clip mask can run through the fast path
    /// for the given clip item type.
    ///
    /// Note: this logic has to match `ClipBatcher::add` behavior.
    fn supports_fast_path_rendering(&self) -> bool {
        match *self {
            ClipItemKind::Rectangle { .. } |
            ClipItemKind::Image { .. } |
            ClipItemKind::BoxShadow { .. } => {
                false
            }
            ClipItemKind::RoundedRectangle { ref rect, ref radius, .. } => {
                radius.can_use_fast_path_in(rect)
            }
        }
    }

    // Get an optional clip rect that a clip source can provide to
    // reduce the size of a primitive region. This is typically
    // used to eliminate redundant clips, and reduce the size of
    // any clip mask that eventually gets drawn.
    pub fn get_local_clip_rect(&self) -> Option<LayoutRect> {
        match *self {
            ClipItemKind::Rectangle { rect, mode: ClipMode::Clip } => Some(rect),
            ClipItemKind::Rectangle { mode: ClipMode::ClipOut, .. } => None,
            ClipItemKind::RoundedRectangle { rect, mode: ClipMode::Clip, .. } => Some(rect),
            ClipItemKind::RoundedRectangle { mode: ClipMode::ClipOut, .. } => None,
            ClipItemKind::Image { rect, .. } => {
                Some(rect)
            }
            ClipItemKind::BoxShadow { .. } => None,
        }
    }

    fn get_clip_result_complex(
        &self,
        transform: &LayoutToVisTransform,
        prim_rect: &VisRect,
        culling_rect: &VisRect,
    ) -> ClipResult {
        let visible_rect = match prim_rect.intersection(culling_rect) {
            Some(rect) => rect,
            None => return ClipResult::Reject,
        };

        let (clip_rect, inner_rect, mode) = match *self {
            ClipItemKind::Rectangle { rect, mode } => {
                (rect, Some(rect), mode)
            }
            ClipItemKind::RoundedRectangle { rect, ref radius, mode } => {
                let inner_clip_rect = extract_inner_rect_safe(&rect, radius);
                (rect, inner_clip_rect, mode)
            }
            ClipItemKind::Image { rect, .. } => {
                (rect, None, ClipMode::Clip)
            }
            ClipItemKind::BoxShadow { .. } => {
                return ClipResult::Partial;
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
                    None => return ClipResult::Partial,
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
    ) -> ClipResult {
        match *self {
            ClipItemKind::Rectangle { rect, mode: ClipMode::Clip } => {
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
            ClipItemKind::Rectangle { rect, mode: ClipMode::ClipOut } => {
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
            ClipItemKind::RoundedRectangle { rect, ref radius, mode: ClipMode::Clip } => {
                // TODO(gw): Consider caching this in the ClipNode
                //           if it ever shows in profiles.
                if rounded_rectangle_contains_box_quick(&rect, radius, &prim_rect) {
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
            ClipItemKind::RoundedRectangle { rect, ref radius, mode: ClipMode::ClipOut } => {
                // TODO(gw): Consider caching this in the ClipNode
                //           if it ever shows in profiles.
                if rounded_rectangle_contains_box_quick(&rect, radius, &prim_rect) {
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
            ClipItemKind::Image { rect, .. } => {
                match rect.intersection(prim_rect) {
                    Some(..) => {
                        ClipResult::Partial
                    }
                    None => {
                        ClipResult::Reject
                    }
                }
            }
            ClipItemKind::BoxShadow { .. } => {
                ClipResult::Partial
            }
        }
    }
}

/// Represents a local rect and a device space
/// rectangles that are either outside or inside bounds.
#[derive(Clone, Debug, PartialEq)]
pub struct Geometry {
    pub local_rect: LayoutRect,
    pub device_rect: DeviceIntRect,
}

impl From<LayoutRect> for Geometry {
    fn from(local_rect: LayoutRect) -> Self {
        Geometry {
            local_rect,
            device_rect: DeviceIntRect::zero(),
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

/// Test where point p is relative to the infinite line that passes through the segment
/// defined by p0 and p1. Point p is on the "left" of the line if the triangle (p0, p1, p)
/// forms a counter-clockwise triangle.
/// > 0 is left of the line
/// < 0 is right of the line
/// == 0 is on the line
pub fn is_left_of_line(
    p_x: f32,
    p_y: f32,
    p0_x: f32,
    p0_y: f32,
    p1_x: f32,
    p1_y: f32,
) -> f32 {
    (p1_x - p0_x) * (p_y - p0_y) - (p_x - p0_x) * (p1_y - p0_y)
}

pub fn polygon_contains_point(
    point: &LayoutPoint,
    rect: &LayoutRect,
    polygon: &PolygonKey,
) -> bool {
    if !rect.contains(*point) {
        return false;
    }

    // p is a LayoutPoint that we'll be comparing to dimensionless PointKeys,
    // which were created from LayoutPoints, so it all works out.
    let p = LayoutPoint::new(point.x - rect.min.x, point.y - rect.min.y);

    // Calculate a winding number for this point.
    let mut winding_number: i32 = 0;

    let count = polygon.point_count as usize;

    for i in 0..count {
        let p0 = polygon.points[i];
        let p1 = polygon.points[(i + 1) % count];

        if p0.y <= p.y {
            if p1.y > p.y {
                if is_left_of_line(p.x, p.y, p0.x, p0.y, p1.x, p1.y) > 0.0 {
                    winding_number = winding_number + 1;
                }
            }
        } else if p1.y <= p.y {
            if is_left_of_line(p.x, p.y, p0.x, p0.y, p1.x, p1.y) < 0.0 {
                winding_number = winding_number - 1;
            }
        }
    }

    match polygon.fill_rule {
        FillRule::Nonzero => winding_number != 0,
        FillRule::Evenodd => winding_number.abs() % 2 == 1,
    }
}

pub fn projected_rect_contains(
    source_rect: &LayoutRect,
    transform: &LayoutToVisTransform,
    target_rect: &VisRect,
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
    prim_spatial_node_index: SpatialNodeIndex,
    pic_spatial_node_index: SpatialNodeIndex,
    visibility_spatial_node_index: SpatialNodeIndex,
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
        clip_node.item.spatial_node_index,
        visibility_spatial_node_index,
        spatial_tree,
    );

    // If we can convert spaces, try to reduce the size of the region
    // requested, and cache the conversion information for the next step.
    if let Some(clip_rect) = clip_node.item.kind.get_local_clip_rect() {
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
            ClipSpaceConversion::Transform(..) => {
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
                    .get_spatial_node(clip_node.item.spatial_node_index)
                    .coordinate_system_id;

                if pic_coord_system == clip_coord_system {
                    let mapper = SpaceMapper::new_with_target(
                        pic_spatial_node_index,
                        clip_node.item.spatial_node_index,
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
    });

    true
}

#[cfg(test)]
mod tests {
    use super::projected_rect_contains;
    use euclid::{Transform3D, rect};

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
