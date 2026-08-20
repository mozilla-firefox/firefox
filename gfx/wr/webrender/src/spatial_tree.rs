/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{ExternalScrollId, PropertyBinding, ReferenceFrameKind, TransformStyle, PropertyBindingId};
use api::{APZScrollGeneration, HasScrollLinkedEffect, PipelineId, SampledScrollOffset};
use api::units::*;
use euclid::Transform3D;
use crate::transform::TransformPalette;
use crate::internal_types::{FastHashMap, FrameMemory};
use crate::print_tree::{PrintableTree, PrintTree, PrintTreePrinter};
use crate::scene::SceneProperties;
use crate::spatial_node::{ReferenceFrameInfo, SpatialNode, SpatialNodeDescriptor, SpatialNodeType, StickyFrameInfo};
use crate::spatial_node::{ScrollFrameKind, SceneSpatialNode, SpatialNodeInfo};
use std::{ops, u32};
use crate::util::{FastTransform, LayoutToWorldFastTransform, MatrixHelpers, ScaleOffset, scale_factors};
use smallvec::SmallVec;
use crate::util::TransformedRectKind;
use peek_poke::PeekPoke;


/// An id that identifies coordinate systems in the SpatialTree. Each
/// coordinate system has an id and those ids will be shared when the coordinates
/// system are the same or are in the same axis-aligned space. This allows
/// for optimizing mask generation.
#[derive(Copy, Clone, PartialEq, PartialOrd)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct CoordinateSystemId(pub u32);

impl std::fmt::Debug for CoordinateSystemId {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "#{}", self.0)
    }
}

/// A node in the hierarchy of coordinate system
/// transforms.
#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct CoordinateSystem {
    pub transform: LayoutTransform,
    pub world_transform: LayoutToWorldTransform,
    pub should_flatten: bool,
    pub parent: Option<CoordinateSystemId>,
}

impl CoordinateSystem {
    fn root() -> Self {
        CoordinateSystem {
            transform: LayoutTransform::identity(),
            world_transform: LayoutToWorldTransform::identity(),
            should_flatten: false,
            parent: None,
        }
    }
}

#[derive(Copy, Clone, Eq, Hash, MallocSizeOf, PartialEq, PeekPoke, Default)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct SpatialNodeIndex(pub u32);

impl SpatialNodeIndex {
    pub const INVALID: SpatialNodeIndex = SpatialNodeIndex(u32::MAX);

    /// May be set on a cluster / picture during scene building if the spatial
    /// node is not known at this time. It must be set to a valid value before
    /// scene building is complete (by `finalize_picture`). In future, we could
    /// make this type-safe with a wrapper type to ensure we know when a spatial
    /// node index may have an unknown value.
    pub const UNKNOWN: SpatialNodeIndex = SpatialNodeIndex(u32::MAX - 1);
}

impl std::fmt::Debug for SpatialNodeIndex {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        if *self == Self::INVALID {
            write!(f, "<invalid>")
        } else if *self == Self::UNKNOWN {
            write!(f, "<unknown>")
        } else {
            write!(f, "#{}", self.0)
        }
    }
}

// In some cases, the conversion from CSS pixels to device pixels can result in small
// rounding errors when calculating the scrollable distance of a scroll frame. Apply
// a small epsilon so that we don't detect these frames as "real" scroll frames.
const MIN_SCROLLABLE_AMOUNT: f32 = 0.01;

// The minimum size for a scroll frame for it to be considered for a scroll root.
const MIN_SCROLL_ROOT_SIZE: f32 = 128.0;

impl SpatialNodeIndex {
    pub fn new(index: usize) -> Self {
        debug_assert!(index < ::std::u32::MAX as usize);
        SpatialNodeIndex(index as u32)
    }
}

impl CoordinateSystemId {
    pub fn root() -> Self {
        CoordinateSystemId(0)
    }
}

#[derive(Debug, Copy, Clone, PartialEq)]
pub enum VisibleFace {
    Front,
    Back,
}

impl Default for VisibleFace {
    fn default() -> Self {
        VisibleFace::Front
    }
}

impl ops::Not for VisibleFace {
    type Output = Self;
    fn not(self) -> Self {
        match self {
            VisibleFace::Front => VisibleFace::Back,
            VisibleFace::Back => VisibleFace::Front,
        }
    }
}

/// Allows functions and methods to retrieve common information about
/// a spatial node, whether during scene or frame building
pub trait SpatialNodeContainer {
    /// Get the common information for a given spatial node
    fn get_node_info(&self, index: SpatialNodeIndex) -> SpatialNodeInfo;
}

/// The representation of the spatial tree during scene building, which is
/// mostly write-only, with a small number of queries for snapping,
/// picture cache building.
///
/// Each `SceneBuilder::build` call calls `reset()` to start the tree fresh,
/// then emits a complete list of `SpatialTreeUpdate::Insert` ops that the
/// frame-side `SpatialTree::apply_updates` consumes verbatim.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct SceneSpatialTree {
    /// Nodes which determine the positions (offsets and transforms) for primitives
    /// and clips.
    spatial_nodes: Vec<SceneSpatialNode>,

    root_reference_frame_index: SpatialNodeIndex,

    updates: SpatialTreeUpdates,
}

impl SpatialNodeContainer for SceneSpatialTree {
    fn get_node_info(&self, index: SpatialNodeIndex) -> SpatialNodeInfo {
        let node = &self.spatial_nodes[index.0 as usize];

        SpatialNodeInfo {
            parent: node.parent,
            node_type: &node.descriptor.node_type,
        }
    }
}

impl SceneSpatialTree {
    pub fn new() -> Self {
        let mut tree = SceneSpatialTree {
            spatial_nodes: Vec::new(),
            root_reference_frame_index: SpatialNodeIndex(0),
            updates: SpatialTreeUpdates::new(),
        };

        tree.add_root_reference_frame();

        tree
    }

    /// Reset the tree to an empty state with just the root reference frame.
    /// Called at the start of each scene build.
    pub fn reset(&mut self) {
        self.spatial_nodes.clear();
        self.updates = SpatialTreeUpdates::new();
        self.add_root_reference_frame();
    }

    fn add_root_reference_frame(&mut self) {
        let node = SceneSpatialNode::new_reference_frame(
            None,
            TransformStyle::Flat,
            PropertyBinding::Value(LayoutTransform::identity()),
            ReferenceFrameKind::Transform {
                should_snap: true,
                is_2d_scale_translation: true,
                paired_with_perspective: false,
            },
            LayoutVector2D::zero(),
            PipelineId::dummy(),
            true,
            true,
        );

        let index = self.add_spatial_node(node);
        debug_assert_eq!(index, SpatialNodeIndex(0));
    }

    pub fn is_root_coord_system(&self, index: SpatialNodeIndex) -> bool {
        self.spatial_nodes[index.0 as usize].is_root_coord_system
    }

    /// Complete building this scene, return the updates to apply to the frame spatial tree
    pub fn end_frame_and_get_pending_updates(&mut self) -> SpatialTreeUpdates {
        self.updates.root_reference_frame_index = self.root_reference_frame_index;
        std::mem::replace(&mut self.updates, SpatialTreeUpdates::new())
    }

    /// Check if a given spatial node is an ancestor of another spatial node.
    pub fn is_ancestor(
        &self,
        maybe_parent: SpatialNodeIndex,
        maybe_child: SpatialNodeIndex,
    ) -> bool {
        // Early out if same node
        if maybe_parent == maybe_child {
            return false;
        }

        let mut current_node = maybe_child;

        while current_node != self.root_reference_frame_index {
            let node = self.get_node_info(current_node);
            current_node = node.parent.expect("bug: no parent");

            if current_node == maybe_parent {
                return true;
            }
        }

        false
    }

    /// Find the spatial node that is the scroll root for a given spatial node.
    /// A scroll root is the first spatial node when found travelling up the
    /// spatial node tree that is an explicit scroll frame.
    pub fn find_scroll_root(
        &self,
        spatial_node_index: SpatialNodeIndex,
        allow_sticky_frames: bool,
    ) -> SpatialNodeIndex {
        let mut real_scroll_root = self.root_reference_frame_index;
        let mut outermost_scroll_root = self.root_reference_frame_index;
        let mut current_scroll_root_is_sticky = false;
        let mut node_index = spatial_node_index;

        while node_index != self.root_reference_frame_index {
            let node = self.get_node_info(node_index);
            match node.node_type {
                SpatialNodeType::ReferenceFrame(ref info) => {
                    match info.kind {
                        ReferenceFrameKind::Transform { is_2d_scale_translation: true, .. } => {
                            // We can handle scroll nodes that pass through a 2d scale/translation node
                        }
                        ReferenceFrameKind::Transform { is_2d_scale_translation: false, .. } |
                        ReferenceFrameKind::Perspective { .. } => {
                            // When a reference frame is encountered, forget any scroll roots
                            // we have encountered, as they may end up with a non-axis-aligned transform.
                            real_scroll_root = self.root_reference_frame_index;
                            outermost_scroll_root = self.root_reference_frame_index;
                            current_scroll_root_is_sticky = false;
                        }
                    }
                }
                SpatialNodeType::StickyFrame(..) => {
                    // Though not a scroll frame, we optionally treat sticky frames as scroll roots
                    // to ensure they are given a separate picture cache slice.
                    if allow_sticky_frames {
                        outermost_scroll_root = node_index;
                        real_scroll_root = node_index;
                        // Set this true so that we don't select an ancestor scroll frame as the scroll root
                        // on a subsequent iteration.
                        current_scroll_root_is_sticky = true;
                    }
                }
                SpatialNodeType::ScrollFrame(ref info) => {
                    match info.frame_kind {
                        ScrollFrameKind::PipelineRoot { is_root_pipeline } => {
                            // Once we encounter a pipeline root, there is no need to look further
                            if is_root_pipeline {
                                break;
                            }
                        }
                        ScrollFrameKind::Explicit => {
                            // Store the closest scroll root we find to the root, for use
                            // later on, even if it's not actually scrollable.
                            outermost_scroll_root = node_index;

                            // If the previously identified scroll root is sticky then we don't
                            // want to choose an ancestor scroll root, as we want the sticky item
                            // to have its own picture cache slice.
                            if !current_scroll_root_is_sticky {
                                // If the scroll root has no scrollable area, we don't want to
                                // consider it. This helps pages that have a nested scroll root
                                // within a redundant scroll root to avoid selecting the wrong
                                // reference spatial node for a picture cache.
                                if info.scrollable_size.width > MIN_SCROLLABLE_AMOUNT ||
                                   info.scrollable_size.height > MIN_SCROLLABLE_AMOUNT {
                                    // Since we are skipping redundant scroll roots, we may end up
                                    // selecting inner scroll roots that are very small. There is
                                    // no performance benefit to creating a slice for these roots,
                                    // as they are cheap to rasterize. The size comparison is in
                                    // local-space, but makes for a reasonable estimate. The value
                                    // is arbitrary, but is generally small enough to ignore things
                                    // like scroll roots around text input elements.
                                    if info.viewport_rect.width() > MIN_SCROLL_ROOT_SIZE &&
                                       info.viewport_rect.height() > MIN_SCROLL_ROOT_SIZE {
                                        // If we've found a root that is scrollable, and a reasonable
                                        // size, select that as the current root for this node
                                        real_scroll_root = node_index;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            node_index = node.parent.expect("unable to find parent node");
        }

        // If we didn't find any real (scrollable) frames, then return the outermost
        // redundant scroll frame. This is important so that we can correctly find
        // the clips defined on the content which should be handled when drawing the
        // picture cache tiles (by definition these clips are ancestors of the
        // scroll root selected for the picture cache).
        if real_scroll_root == self.root_reference_frame_index {
            outermost_scroll_root
        } else {
            real_scroll_root
        }
    }

    /// The root reference frame, which is the true root of the SpatialTree.
    pub fn root_reference_frame_index(&self) -> SpatialNodeIndex {
        self.root_reference_frame_index
    }

    fn add_spatial_node(
        &mut self,
        node: SceneSpatialNode,
    ) -> SpatialNodeIndex {
        let descriptor = node.descriptor.clone();
        let parent = node.parent;

        let index = self.spatial_nodes.len();
        self.spatial_nodes.push(node);

        self.updates.updates.push(SpatialTreeUpdate {
            index,
            descriptor,
            parent,
        });

        SpatialNodeIndex(index as u32)
    }

    pub fn add_reference_frame(
        &mut self,
        parent_index: SpatialNodeIndex,
        transform_style: TransformStyle,
        source_transform: PropertyBinding<LayoutTransform>,
        kind: ReferenceFrameKind,
        origin_in_parent_reference_frame: LayoutVector2D,
        pipeline_id: PipelineId,
        is_pipeline_root: bool,
    ) -> SpatialNodeIndex {
        // Determine if this reference frame creates a new static coordinate system
        let new_static_coord_system = match kind {
            ReferenceFrameKind::Transform { is_2d_scale_translation: true, .. } => {
                // Client has guaranteed this transform will only be axis-aligned
                false
            }
            ReferenceFrameKind::Transform { is_2d_scale_translation: false, .. } | ReferenceFrameKind::Perspective { .. } => {
                // Even if client hasn't promised it's an axis-aligned transform, we can still
                // check this so long as the transform isn't animated (and thus could change to
                // anything by APZ during frame building)
                match source_transform {
                    PropertyBinding::Value(m) => {
                        !m.is_2d_scale_translation()
                    }
                    PropertyBinding::Binding(..) => {
                        // Animated, so assume it may introduce a complex transform
                        true
                    }
                }
            }
        };

        let is_root_coord_system = !new_static_coord_system &&
            self.spatial_nodes[parent_index.0 as usize].is_root_coord_system;

        let node = SceneSpatialNode::new_reference_frame(
            Some(parent_index),
            transform_style,
            source_transform,
            kind,
            origin_in_parent_reference_frame,
            pipeline_id,
            is_root_coord_system,
            is_pipeline_root,
        );
        self.add_spatial_node(node)
    }

    pub fn add_scroll_frame(
        &mut self,
        parent_index: SpatialNodeIndex,
        external_id: ExternalScrollId,
        pipeline_id: PipelineId,
        frame_rect: &LayoutRect,
        content_size: &LayoutSize,
        frame_kind: ScrollFrameKind,
        external_scroll_offset: LayoutVector2D,
        scroll_offset_generation: APZScrollGeneration,
        has_scroll_linked_effect: HasScrollLinkedEffect,
    ) -> SpatialNodeIndex {
        // Scroll frames are only 2d translations - they can't introduce a new static coord system
        let is_root_coord_system = self.spatial_nodes[parent_index.0 as usize].is_root_coord_system;

        let node = SceneSpatialNode::new_scroll_frame(
            pipeline_id,
            parent_index,
            external_id,
            frame_rect,
            content_size,
            frame_kind,
            external_scroll_offset,
            scroll_offset_generation,
            has_scroll_linked_effect,
            is_root_coord_system,
        );
        self.add_spatial_node(node)
    }

    pub fn add_sticky_frame(
        &mut self,
        parent_index: SpatialNodeIndex,
        sticky_frame_info: StickyFrameInfo,
        pipeline_id: PipelineId,
    ) -> SpatialNodeIndex {
        // Sticky frames are only 2d translations - they can't introduce a new static coord system
        let is_root_coord_system = self.spatial_nodes[parent_index.0 as usize].is_root_coord_system;

        let node = SceneSpatialNode::new_sticky_frame(
            parent_index,
            sticky_frame_info,
            pipeline_id,
            is_root_coord_system,
        );
        self.add_spatial_node(node)
    }
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct SpatialTreeUpdate {
    pub index: usize,
    pub parent: Option<SpatialNodeIndex>,
    pub descriptor: SpatialNodeDescriptor,
}

/// The full set of spatial nodes for the scene that just finished building.
/// `apply_updates` consumes this by replacing the frame-side tree wholesale.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct SpatialTreeUpdates {
    root_reference_frame_index: SpatialNodeIndex,
    updates: Vec<SpatialTreeUpdate>,
}

impl SpatialTreeUpdates {
    fn new() -> Self {
        SpatialTreeUpdates {
            root_reference_frame_index: SpatialNodeIndex::INVALID,
            updates: Vec::new(),
        }
    }
}

/// Represents the spatial tree during frame building, which is mostly
/// read-only, apart from the tree update at the start of the frame
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct SpatialTree {
    /// Nodes which determine the positions (offsets and transforms) for primitives
    /// and clips.
    spatial_nodes: Vec<SpatialNode>,

    /// A list of transforms that establish new coordinate systems.
    /// Spatial nodes only establish a new coordinate system when
    /// they have a transform that is not a simple 2d translation.
    coord_systems: Vec<CoordinateSystem>,

    root_reference_frame_index: SpatialNodeIndex,

    /// Stack of current state for each parent node while traversing and updating tree
    update_state_stack: Vec<TransformUpdateState>,
}

#[derive(Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct TransformUpdateState {
    pub parent_reference_frame_transform: LayoutToWorldFastTransform,
    pub parent_accumulated_scroll_offset: LayoutVector2D,
    pub nearest_scrolling_ancestor_offset: LayoutVector2D,
    pub nearest_scrolling_ancestor_viewport: LayoutRect,

    /// An id for keeping track of the axis-aligned space of this node. This is used in
    /// order to to track what kinds of clip optimizations can be done for a particular
    /// display list item, since optimizations can usually only be done among
    /// coordinate systems which are relatively axis aligned.
    pub current_coordinate_system_id: CoordinateSystemId,

    /// Scale and offset from the coordinate system that started this compatible coordinate system.
    pub coordinate_system_relative_scale_offset: ScaleOffset,

    /// True if this node is transformed by an invertible transform.  If not, display items
    /// transformed by this node will not be displayed and display items not transformed by this
    /// node will not be clipped by clips that are transformed by this node.
    pub invertible: bool,

    /// True if this node is a part of Preserve3D hierarchy.
    pub preserves_3d: bool,

    /// True if the any parent nodes are currently zooming
    pub is_ancestor_or_self_zooming: bool,

    /// True if this node or any parent node has an animated (property-bound) transform
    pub is_ancestor_or_self_animating: bool,

    /// Set to true if this state represents a scroll node with external id
    pub external_id: Option<ExternalScrollId>,

    /// The node scroll offset if this state is a scroll/sticky node. Zero if a reference frame.
    pub scroll_offset: LayoutVector2D,
}

/// Transformation between two nodes in the spatial tree that can sometimes be
/// encoded more efficiently than with a full matrix.
#[derive(Debug, Clone)]
pub enum CoordinateSpaceMapping<Src, Dst> {
    Local,
    ScaleOffset(ScaleOffset),
    Transform(Transform3D<f32, Src, Dst>),
}

impl<Src, Dst> CoordinateSpaceMapping<Src, Dst> {
    pub fn into_transform(self) -> Transform3D<f32, Src, Dst> {
        match self {
            CoordinateSpaceMapping::Local => Transform3D::identity(),
            CoordinateSpaceMapping::ScaleOffset(scale_offset) => scale_offset.to_transform(),
            CoordinateSpaceMapping::Transform(transform) => transform,
        }
    }

    pub fn into_fast_transform(self) -> FastTransform<Src, Dst> {
        match self {
            CoordinateSpaceMapping::Local => FastTransform::identity(),
            CoordinateSpaceMapping::ScaleOffset(scale_offset) => FastTransform::with_scale_offset(scale_offset),
            CoordinateSpaceMapping::Transform(transform) => FastTransform::with_transform(transform),
        }
    }

    pub fn is_perspective(&self) -> bool {
        match *self {
            CoordinateSpaceMapping::Local |
            CoordinateSpaceMapping::ScaleOffset(_) => false,
            CoordinateSpaceMapping::Transform(ref transform) => transform.has_perspective_component(),
        }
    }

    pub fn is_2d_axis_aligned(&self) -> bool {
        match *self {
            CoordinateSpaceMapping::Local |
            CoordinateSpaceMapping::ScaleOffset(_) => true,
            CoordinateSpaceMapping::Transform(ref transform) => transform.preserves_2d_axis_alignment(),
        }
    }

    pub fn is_2d_scale_translation(&self) -> bool {
        match *self {
            CoordinateSpaceMapping::Local |
            CoordinateSpaceMapping::ScaleOffset(_) => true,
            CoordinateSpaceMapping::Transform(ref transform) => transform.is_2d_scale_translation(),
        }
    }

    pub fn scale_factors(&self) -> (f32, f32) {
        match *self {
            CoordinateSpaceMapping::Local => (1.0, 1.0),
            CoordinateSpaceMapping::ScaleOffset(ref scale_offset) => (scale_offset.scale.x.abs(), scale_offset.scale.y.abs()),
            CoordinateSpaceMapping::Transform(ref transform) => scale_factors(transform),
        }
    }

    pub fn coplanar_scale_factors(&self) -> Option<(f32, f32)> {
        match *self {
            CoordinateSpaceMapping::Local => Some((1.0, 1.0)),
            CoordinateSpaceMapping::ScaleOffset(ref scale_offset) => Some((scale_offset.scale.x.abs(), scale_offset.scale.y.abs())),
            CoordinateSpaceMapping::Transform(ref transform) => transform.coplanar_scale_factors(),
        }
    }

    pub fn inverse(&self) -> Option<CoordinateSpaceMapping<Dst, Src>> {
        match *self {
            CoordinateSpaceMapping::Local => Some(CoordinateSpaceMapping::Local),
            CoordinateSpaceMapping::ScaleOffset(ref scale_offset) => {
                Some(CoordinateSpaceMapping::ScaleOffset(scale_offset.inverse()))
            }
            CoordinateSpaceMapping::Transform(ref transform) => {
                transform.inverse().map(CoordinateSpaceMapping::Transform)
            }
        }
    }

    pub fn as_2d_scale_offset(&self) -> Option<ScaleOffset> {
        Some(match *self {
            CoordinateSpaceMapping::Local => ScaleOffset::identity(),
            CoordinateSpaceMapping::ScaleOffset(transfrom) => transfrom,
            CoordinateSpaceMapping::Transform(ref transform) => {
                if !transform.is_2d_scale_translation() {
                    return None
                }
                ScaleOffset::new(transform.m11, transform.m22, transform.m41, transform.m42)
            }
        })
    }
}

enum TransformScroll {
    Scrolled,
    Unscrolled,
}

impl SpatialNodeContainer for SpatialTree {
    fn get_node_info(&self, index: SpatialNodeIndex) -> SpatialNodeInfo {
        let node = self.get_spatial_node(index);

        SpatialNodeInfo {
            parent: node.parent,
            node_type: &node.node_type,
        }
    }
}

impl SpatialTree {
    pub fn new() -> Self {
        SpatialTree {
            spatial_nodes: Vec::new(),
            coord_systems: Vec::new(),
            root_reference_frame_index: SpatialNodeIndex::INVALID,
            update_state_stack: Vec::new(),
        }
    }

    fn visit_node_impl_mut<F>(
        &mut self,
        index: SpatialNodeIndex,
        f: &mut F,
    ) where F: FnMut(SpatialNodeIndex, &mut SpatialNode) {
        let mut child_indices: SmallVec<[SpatialNodeIndex; 8]> = SmallVec::new();

        let node = self.get_spatial_node_mut(index);
        f(index, node);
        child_indices.extend_from_slice(&node.children);

        for child_index in child_indices {
            self.visit_node_impl_mut(child_index, f);
        }
    }

    fn visit_node_impl<F>(
        &self,
        index: SpatialNodeIndex,
        f: &mut F,
    ) where F: FnMut(SpatialNodeIndex, &SpatialNode) {
        let node = self.get_spatial_node(index);

        f(index, node);

        for child_index in &node.children {
            self.visit_node_impl(*child_index, f);
        }
    }

    /// Visit all nodes from the root of the tree, invoking a closure on each one
    pub fn visit_nodes<F>(&self, mut f: F) where F: FnMut(SpatialNodeIndex, &SpatialNode) {
        if self.root_reference_frame_index == SpatialNodeIndex::INVALID {
            return;
        }

        self.visit_node_impl(self.root_reference_frame_index, &mut f);
    }

    /// Visit all nodes from the root of the tree, invoking a closure on each one
    pub fn visit_nodes_mut<F>(&mut self, mut f: F) where F: FnMut(SpatialNodeIndex, &mut SpatialNode) {
        if self.root_reference_frame_index == SpatialNodeIndex::INVALID {
            return;
        }

        self.visit_node_impl_mut(self.root_reference_frame_index, &mut f);
    }

    /// Replace this tree with the contents of a freshly-built scene.
    pub fn apply_updates(
        &mut self,
        updates: SpatialTreeUpdates,
    ) {
        self.root_reference_frame_index = updates.root_reference_frame_index;
        self.spatial_nodes.clear();

        for SpatialTreeUpdate { index, parent, descriptor } in updates.updates {
            debug_assert_eq!(index, self.spatial_nodes.len());

            if let Some(parent) = parent {
                self.get_spatial_node_mut(parent).add_child(SpatialNodeIndex(index as u32));
            }

            self.spatial_nodes.push(SpatialNode {
                viewport_transform: ScaleOffset::identity(),
                content_transform: ScaleOffset::identity(),
                coordinate_system_id: CoordinateSystemId(0),
                transform_kind: TransformedRectKind::AxisAligned,
                parent,
                children: Vec::new(),
                pipeline_id: descriptor.pipeline_id,
                node_type: descriptor.node_type,
                invertible: true,
                is_async_zooming: false,
                is_ancestor_or_self_zooming: false,
                is_ancestor_or_self_animating: false,
            });
        }

        self.visit_nodes_mut(|_, node| {
            match node.node_type {
                SpatialNodeType::ScrollFrame(ref mut info) => {
                    info.offsets = vec![SampledScrollOffset{
                        offset: -info.external_scroll_offset,
                        generation: info.offset_generation,
                    }];
                }
                SpatialNodeType::StickyFrame(ref mut info) => {
                    info.current_offset = LayoutVector2D::zero();
                }
                SpatialNodeType::ReferenceFrame(..) => {}
            }
        });
    }

    pub fn get_last_sampled_scroll_offsets(
        &self,
    ) -> FastHashMap<ExternalScrollId, Vec<SampledScrollOffset>> {
        let mut result = FastHashMap::default();
        self.visit_nodes(|_, node| {
            if let SpatialNodeType::ScrollFrame(ref scrolling) = node.node_type {
                result.insert(scrolling.external_id, scrolling.offsets.clone());
            }
        });
        result
    }

    pub fn apply_last_sampled_scroll_offsets(
        &mut self,
        last_sampled_offsets: FastHashMap<ExternalScrollId, Vec<SampledScrollOffset>>,
    ) {
        self.visit_nodes_mut(|_, node| {
            if let SpatialNodeType::ScrollFrame(ref mut scrolling) = node.node_type {
                if let Some(offsets) = last_sampled_offsets.get(&scrolling.external_id) {
                    scrolling.offsets = offsets.clone();
                }
            }
        });
    }

    pub fn get_spatial_node(&self, index: SpatialNodeIndex) -> &SpatialNode {
        &self.spatial_nodes[index.0 as usize]
    }

    pub fn get_spatial_node_mut(&mut self, index: SpatialNodeIndex) -> &mut SpatialNode {
        &mut self.spatial_nodes[index.0 as usize]
    }

    /// Get total number of spatial nodes
    pub fn spatial_node_count(&self) -> usize {
        self.spatial_nodes.len()
    }

    pub fn find_spatial_node_by_anim_id(
        &self,
        id: PropertyBindingId,
    ) -> Option<SpatialNodeIndex> {
        let mut node_index = None;

        self.visit_nodes(|index, node| {
            if node.is_transform_bound_to_property(id) {
                debug_assert!(node_index.is_none());        // Multiple nodes with same anim id
                node_index = Some(index);
            }
        });

        node_index
    }

    /// Calculate the relative transform from `child_index` to `parent_index`.
    /// This method will panic if the nodes are not connected!
    pub fn get_relative_transform(
        &self,
        child_index: SpatialNodeIndex,
        parent_index: SpatialNodeIndex,
    ) -> CoordinateSpaceMapping<LayoutPixel, LayoutPixel> {
        self.get_relative_transform_with_face(child_index, parent_index, None)
    }

    /// Calculate the relative transform from `child_index` to `parent_index`.
    /// This method will panic if the nodes are not connected!
    /// Also, switch the visible face to `Back` if at any stage where the
    /// combined transform is flattened, we see the back face.
    pub fn get_relative_transform_with_face(
        &self,
        child_index: SpatialNodeIndex,
        parent_index: SpatialNodeIndex,
        mut visible_face: Option<&mut VisibleFace>,
    ) -> CoordinateSpaceMapping<LayoutPixel, LayoutPixel> {
        if child_index == parent_index {
            return CoordinateSpaceMapping::Local;
        }

        let child = self.get_spatial_node(child_index);
        let parent = self.get_spatial_node(parent_index);

        // TODO(gw): We expect this never to fail, but it's possible that it might due to
        //           either (a) a bug in WR / Gecko, or (b) some obscure real-world content
        //           that we're unaware of. If we ever hit this, please open a bug with any
        //           repro steps!
        assert!(
            child.coordinate_system_id.0 >= parent.coordinate_system_id.0,
            "bug: this is an unexpected case - please open a bug and talk to #gfx team!",
        );

        if child.coordinate_system_id == parent.coordinate_system_id {
            let scale_offset = child.content_transform.then(&parent.content_transform.inverse());

            // Optimization - detect identity scale-offsets and treat them as
            // local to skip following math
            if scale_offset.is_identity() {
                return CoordinateSpaceMapping::Local;
            }

            return CoordinateSpaceMapping::ScaleOffset(scale_offset);
        }

        let mut coordinate_system_id = child.coordinate_system_id;
        let mut transform = child.content_transform.to_transform();

        // we need to update the associated parameters of a transform in two cases:
        // 1) when the flattening happens, so that we don't lose that original 3D aspects
        // 2) when we reach the end of iteration, so that our result is up to date

        while coordinate_system_id != parent.coordinate_system_id {
            let coord_system = &self.coord_systems[coordinate_system_id.0 as usize];

            if coord_system.should_flatten {
                if let Some(ref mut face) = visible_face {
                    if transform.is_backface_visible() {
                        **face = VisibleFace::Back;
                    }
                }
                transform.flatten_z_output();
            }

            coordinate_system_id = coord_system.parent.expect("invalid parent!");
            transform = transform.then(&coord_system.transform);
        }

        transform = transform.then(
            &parent.content_transform
                .inverse()
                .to_transform(),
        );
        if let Some(face) = visible_face {
            if transform.is_backface_visible() {
                *face = VisibleFace::Back;
            }
        }

        CoordinateSpaceMapping::Transform(transform)
    }

    /// Returns true if both supplied spatial nodes are in the same coordinate system
    /// (implies the relative transform produce axis-aligned rects).
    pub fn is_matching_coord_system(
        &self,
        index0: SpatialNodeIndex,
        index1: SpatialNodeIndex,
    ) -> bool {
        let node0 = self.get_spatial_node(index0);
        let node1 = self.get_spatial_node(index1);

        node0.coordinate_system_id == node1.coordinate_system_id
    }

    fn get_world_transform_impl(
        &self,
        index: SpatialNodeIndex,
        scroll: TransformScroll,
    ) -> CoordinateSpaceMapping<LayoutPixel, WorldPixel> {
        let child = self.get_spatial_node(index);

        if child.coordinate_system_id.0 == 0 {
            if index == self.root_reference_frame_index {
                CoordinateSpaceMapping::Local
            } else {
                match scroll {
                    TransformScroll::Scrolled => CoordinateSpaceMapping::ScaleOffset(child.content_transform),
                    TransformScroll::Unscrolled => CoordinateSpaceMapping::ScaleOffset(child.viewport_transform),
                }
            }
        } else {
            let system = &self.coord_systems[child.coordinate_system_id.0 as usize];
            let scale_offset = match scroll {
                TransformScroll::Scrolled => &child.content_transform,
                TransformScroll::Unscrolled => &child.viewport_transform,
            };
            let transform = scale_offset
                .to_transform()
                .then(&system.world_transform);

            CoordinateSpaceMapping::Transform(transform)
        }
    }

    /// Calculate the relative transform from `index` to the root.
    pub fn get_world_transform(
        &self,
        index: SpatialNodeIndex,
    ) -> CoordinateSpaceMapping<LayoutPixel, WorldPixel> {
        self.get_world_transform_impl(index, TransformScroll::Scrolled)
    }

    /// Calculate the relative transform from `index` to the root.
    /// Unlike `get_world_transform`, this variant doesn't account for the local scroll offset.
    pub fn get_world_viewport_transform(
        &self,
        index: SpatialNodeIndex,
    ) -> CoordinateSpaceMapping<LayoutPixel, WorldPixel> {
        self.get_world_transform_impl(index, TransformScroll::Unscrolled)
    }

    /// The root reference frame, which is the true root of the SpatialTree.
    pub fn root_reference_frame_index(&self) -> SpatialNodeIndex {
        self.root_reference_frame_index
    }

    pub fn set_scroll_offsets(
        &mut self,
        id: ExternalScrollId,
        offsets: Vec<SampledScrollOffset>,
    ) -> bool {
        let mut did_change = false;

        self.visit_nodes_mut(|_, node| {
            if node.matches_external_id(id) {
                did_change |= node.set_scroll_offsets(offsets.clone());
            }
        });

        did_change
    }

    pub fn update_tree(
        &mut self,
        scene_properties: &SceneProperties,
    ) {
        if self.root_reference_frame_index == SpatialNodeIndex::INVALID {
            return;
        }

        tracy_rs::profile_scope!("update_tree");
        self.coord_systems.clear();
        self.coord_systems.push(CoordinateSystem::root());

        let root_node_index = self.root_reference_frame_index();
        assert!(self.update_state_stack.is_empty());

        let state = TransformUpdateState {
            parent_reference_frame_transform: LayoutVector2D::zero().into(),
            parent_accumulated_scroll_offset: LayoutVector2D::zero(),
            nearest_scrolling_ancestor_offset: LayoutVector2D::zero(),
            nearest_scrolling_ancestor_viewport: LayoutRect::zero(),
            current_coordinate_system_id: CoordinateSystemId::root(),
            coordinate_system_relative_scale_offset: ScaleOffset::identity(),
            invertible: true,
            preserves_3d: false,
            is_ancestor_or_self_zooming: false,
            is_ancestor_or_self_animating: false,
            external_id: None,
            scroll_offset: LayoutVector2D::zero(),
        };
        self.update_state_stack.push(state);

        self.update_node(
            root_node_index,
            scene_properties,
        );

        self.update_state_stack.pop().unwrap();
    }

    fn update_node(
        &mut self,
        node_index: SpatialNodeIndex,
        scene_properties: &SceneProperties,
    ) {
        let node = &mut self.spatial_nodes[node_index.0 as usize];

        node.update(
            &self.update_state_stack,
            &mut self.coord_systems,
            scene_properties,
        );

        if !node.children.is_empty() {
            let mut child_state = self.update_state_stack.last().unwrap().clone();
            node.prepare_state_for_children(&mut child_state);
            self.update_state_stack.push(child_state);

            let mut child_indices: SmallVec<[SpatialNodeIndex; 8]> = SmallVec::new();
            child_indices.extend_from_slice(&node.children);

            for child_index in child_indices {
                self.update_node(
                    child_index,
                    scene_properties,
                );
            }

            self.update_state_stack.pop().unwrap();
        }
    }

    pub fn build_transform_palette(&self, memory: &FrameMemory) -> TransformPalette {
        tracy_rs::profile_scope!("build_transform_palette");
        TransformPalette::new(self.spatial_nodes.len(), memory)
    }

    fn print_node<T: PrintTreePrinter>(
        &self,
        index: SpatialNodeIndex,
        pt: &mut T,
    ) {
        let node = self.get_spatial_node(index);
        match node.node_type {
            SpatialNodeType::StickyFrame(ref sticky_frame_info) => {
                pt.new_level(format!("StickyFrame"));
                pt.add_item(format!("sticky info: {:?}", sticky_frame_info));
            }
            SpatialNodeType::ScrollFrame(ref scrolling_info) => {
                pt.new_level(format!("ScrollFrame"));
                pt.add_item(format!("viewport: {:?}", scrolling_info.viewport_rect));
                pt.add_item(format!("scrollable_size: {:?}", scrolling_info.scrollable_size));
                pt.add_item(format!("scroll offset: {:?}", scrolling_info.offset()));
                pt.add_item(format!("external_scroll_offset: {:?}", scrolling_info.external_scroll_offset));
                pt.add_item(format!("offset generation: {:?}", scrolling_info.offset_generation));
                if scrolling_info.has_scroll_linked_effect == HasScrollLinkedEffect::Yes {
                    pt.add_item("has scroll-linked effect".to_string());
                }
                pt.add_item(format!("kind: {:?}", scrolling_info.frame_kind));
            }
            SpatialNodeType::ReferenceFrame(ref info) => {
                pt.new_level(format!("ReferenceFrame"));
                pt.add_item(format!("kind: {:?}", info.kind));
                pt.add_item(format!("transform_style: {:?}", info.transform_style));
                pt.add_item(format!("source_transform: {:?}", info.source_transform));
                pt.add_item(format!("origin_in_parent_reference_frame: {:?}", info.origin_in_parent_reference_frame));
            }
        }

        pt.add_item(format!("index: {:?}", index));
        pt.add_item(format!("content_transform: {:?}", node.content_transform));
        pt.add_item(format!("viewport_transform: {:?}", node.viewport_transform));
        pt.add_item(format!("coordinate_system_id: {:?}", node.coordinate_system_id));

        for child_index in &node.children {
            self.print_node(*child_index, pt);
        }

        pt.end_level();
    }

    /// Get the visible face of the transfrom from the specified node to its parent.
    pub fn get_local_visible_face(&self, node_index: SpatialNodeIndex) -> VisibleFace {
        let node = self.get_spatial_node(node_index);
        let mut face = VisibleFace::Front;
        if let Some(mut parent_index) = node.parent {
            // Check if the parent is perspective. In CSS, a stacking context may
            // have both perspective and a regular transformation. Gecko translates the
            // perspective into a different `nsDisplayPerspective` and `nsDisplayTransform` items.
            // On WebRender side, we end up with 2 different reference frames:
            // one has kind of "transform", and it's parented to another of "perspective":
            // https://searchfox.org/mozilla-central/rev/72c7cef167829b6f1e24cae216fa261934c455fc/layout/generic/nsIFrame.cpp#3716
            if let SpatialNodeType::ReferenceFrame(ReferenceFrameInfo { kind: ReferenceFrameKind::Transform {
                paired_with_perspective: true,
                ..
            }, .. }) = node.node_type {
                let parent = self.get_spatial_node(parent_index);
                match parent.node_type {
                    SpatialNodeType::ReferenceFrame(ReferenceFrameInfo {
                        kind: ReferenceFrameKind::Perspective { .. },
                        ..
                    }) => {
                        parent_index = parent.parent.unwrap();
                    }
                    _ => {
                        log::error!("Unexpected parent {:?} is not perspective", parent_index);
                    }
                }
            }

            self.get_relative_transform_with_face(node_index, parent_index, Some(&mut face));
        }
        face
    }

    #[allow(dead_code)]
    pub fn print_to_string(&self) -> String {
        let mut result = String::new();

        if self.root_reference_frame_index != SpatialNodeIndex::INVALID {
            let mut buf = Vec::<u8>::new();
            {
                let mut pt = PrintTree::new_with_sink("spatial tree", &mut buf);
                self.print_with(&mut pt);
            }
            result = std::str::from_utf8(&buf).unwrap_or("(Tree printer emitted non-utf8)").to_string();
        }

        result
    }

    #[allow(dead_code)]
    pub fn print(&self) {
        let result = self.print_to_string();
        // If running in Gecko, set RUST_LOG=webrender::spatial_tree=debug
        // to get this logging to be emitted to stderr/logcat.
        debug!("{}", result);
    }
}

impl PrintableTree for SpatialTree {
    fn print_with<T: PrintTreePrinter>(&self, pt: &mut T) {
        if self.root_reference_frame_index != SpatialNodeIndex::INVALID {
            self.print_node(self.root_reference_frame_index(), pt);
        }
    }
}

#[cfg(test)]
fn add_reference_frame(
    cst: &mut SceneSpatialTree,
    parent: SpatialNodeIndex,
    transform: LayoutTransform,
    origin_in_parent_reference_frame: LayoutVector2D,
) -> SpatialNodeIndex {
    cst.add_reference_frame(
        parent,
        TransformStyle::Preserve3D,
        PropertyBinding::Value(transform),
        ReferenceFrameKind::Transform {
            is_2d_scale_translation: false,
            should_snap: false,
            paired_with_perspective: false,
        },
        origin_in_parent_reference_frame,
        PipelineId::dummy(),
        false,
    )
}

#[cfg(test)]
fn test_pt(
    px: f32,
    py: f32,
    cst: &SpatialTree,
    child: SpatialNodeIndex,
    parent: SpatialNodeIndex,
    expected_x: f32,
    expected_y: f32,
) {
    use euclid::approxeq::ApproxEq;
    const EPSILON: f32 = 0.0001;

    let p = LayoutPoint::new(px, py);
    let m = cst.get_relative_transform(child, parent).into_transform();
    let pt = m.transform_point2d(p).unwrap();
    assert!(pt.x.approx_eq_eps(&expected_x, &EPSILON) &&
            pt.y.approx_eq_eps(&expected_y, &EPSILON),
            "p: {:?} -> {:?}\nm={:?}",
            p, pt, m,
            );
}

#[test]
fn test_cst_simple_translation() {
    // Basic translations only

    let mut cst = SceneSpatialTree::new();
    let root_reference_frame_index = cst.root_reference_frame_index();

    let root = add_reference_frame(
        &mut cst,
        root_reference_frame_index,
        LayoutTransform::identity(),
        LayoutVector2D::zero(),
    );

    let child1 = add_reference_frame(
        &mut cst,
        root,
        LayoutTransform::translation(100.0, 0.0, 0.0),
        LayoutVector2D::zero(),
    );

    let child2 = add_reference_frame(
        &mut cst,
        child1,
        LayoutTransform::translation(0.0, 50.0, 0.0),
        LayoutVector2D::zero(),
    );

    let child3 = add_reference_frame(
        &mut cst,
        child2,
        LayoutTransform::translation(200.0, 200.0, 0.0),
        LayoutVector2D::zero(),
    );

    let mut st = SpatialTree::new();
    st.apply_updates(cst.end_frame_and_get_pending_updates());
    st.update_tree(&SceneProperties::new());

    test_pt(100.0, 100.0, &st, child1, root, 200.0, 100.0);
    test_pt(100.0, 100.0, &st, child2, root, 200.0, 150.0);
    test_pt(100.0, 100.0, &st, child2, child1, 100.0, 150.0);
    test_pt(100.0, 100.0, &st, child3, root, 400.0, 350.0);
}

#[test]
fn test_cst_simple_scale() {
    // Basic scale only

    let mut cst = SceneSpatialTree::new();
    let root_reference_frame_index = cst.root_reference_frame_index();

    let root = add_reference_frame(
        &mut cst,
        root_reference_frame_index,
        LayoutTransform::identity(),
        LayoutVector2D::zero(),
    );

    let child1 = add_reference_frame(
        &mut cst,
        root,
        LayoutTransform::scale(4.0, 1.0, 1.0),
        LayoutVector2D::zero(),
    );

    let child2 = add_reference_frame(
        &mut cst,
        child1,
        LayoutTransform::scale(1.0, 2.0, 1.0),
        LayoutVector2D::zero(),
    );

    let child3 = add_reference_frame(
        &mut cst,
        child2,
        LayoutTransform::scale(2.0, 2.0, 1.0),
        LayoutVector2D::zero(),
    );

    let mut st = SpatialTree::new();
    st.apply_updates(cst.end_frame_and_get_pending_updates());
    st.update_tree(&SceneProperties::new());

    test_pt(100.0, 100.0, &st, child1, root, 400.0, 100.0);
    test_pt(100.0, 100.0, &st, child2, root, 400.0, 200.0);
    test_pt(100.0, 100.0, &st, child3, root, 800.0, 400.0);
    test_pt(100.0, 100.0, &st, child2, child1, 100.0, 200.0);
    test_pt(100.0, 100.0, &st, child3, child1, 200.0, 400.0);
}

#[test]
fn test_cst_scale_translation() {
    // Scale + translation

    let mut cst = SceneSpatialTree::new();
    let root_reference_frame_index = cst.root_reference_frame_index();

    let root = add_reference_frame(
        &mut cst,
        root_reference_frame_index,
        LayoutTransform::identity(),
        LayoutVector2D::zero(),
    );

    let child1 = add_reference_frame(
        &mut cst,
        root,
        LayoutTransform::translation(100.0, 50.0, 0.0),
        LayoutVector2D::zero(),
    );

    let child2 = add_reference_frame(
        &mut cst,
        child1,
        LayoutTransform::scale(2.0, 4.0, 1.0),
        LayoutVector2D::zero(),
    );

    let child3 = add_reference_frame(
        &mut cst,
        child2,
        LayoutTransform::translation(200.0, -100.0, 0.0),
        LayoutVector2D::zero(),
    );

    let child4 = add_reference_frame(
        &mut cst,
        child3,
        LayoutTransform::scale(3.0, 2.0, 1.0),
        LayoutVector2D::zero(),
    );

    let mut st = SpatialTree::new();
    st.apply_updates(cst.end_frame_and_get_pending_updates());
    st.update_tree(&SceneProperties::new());

    test_pt(100.0, 100.0, &st, child1, root, 200.0, 150.0);
    test_pt(100.0, 100.0, &st, child2, root, 300.0, 450.0);
    test_pt(100.0, 100.0, &st, child4, root, 1100.0, 450.0);

    test_pt(0.0, 0.0, &st, child4, child1, 400.0, -400.0);
    test_pt(100.0, 100.0, &st, child4, child1, 1000.0, 400.0);
    test_pt(100.0, 100.0, &st, child2, child1, 200.0, 400.0);

    test_pt(100.0, 100.0, &st, child3, child1, 600.0, 0.0);
}

#[test]
fn test_cst_translation_rotate() {
    // Rotation + translation
    use euclid::Angle;

    let mut cst = SceneSpatialTree::new();
    let root_reference_frame_index = cst.root_reference_frame_index();

    let root = add_reference_frame(
        &mut cst,
        root_reference_frame_index,
        LayoutTransform::identity(),
        LayoutVector2D::zero(),
    );

    let child1 = add_reference_frame(
        &mut cst,
        root,
        LayoutTransform::rotation(0.0, 0.0, 1.0, Angle::degrees(-90.0)),
        LayoutVector2D::zero(),
    );

    let mut st = SpatialTree::new();
    st.apply_updates(cst.end_frame_and_get_pending_updates());
    st.update_tree(&SceneProperties::new());

    test_pt(100.0, 0.0, &st, child1, root, 0.0, -100.0);
}

#[test]
fn test_is_ancestor1() {
    let mut st = SceneSpatialTree::new();
    let root_reference_frame_index = st.root_reference_frame_index();

    let root = add_reference_frame(
        &mut st,
        root_reference_frame_index,
        LayoutTransform::identity(),
        LayoutVector2D::zero(),
    );

    let child1_0 = add_reference_frame(
        &mut st,
        root,
        LayoutTransform::identity(),
        LayoutVector2D::zero(),
    );

    let child1_1 = add_reference_frame(
        &mut st,
        child1_0,
        LayoutTransform::identity(),
        LayoutVector2D::zero(),
    );

    let child2 = add_reference_frame(
        &mut st,
        root,
        LayoutTransform::identity(),
        LayoutVector2D::zero(),
    );

    assert!(!st.is_ancestor(root, root));
    assert!(!st.is_ancestor(child1_0, child1_0));
    assert!(!st.is_ancestor(child1_1, child1_1));
    assert!(!st.is_ancestor(child2, child2));

    assert!(st.is_ancestor(root, child1_0));
    assert!(st.is_ancestor(root, child1_1));
    assert!(st.is_ancestor(child1_0, child1_1));

    assert!(!st.is_ancestor(child1_0, root));
    assert!(!st.is_ancestor(child1_1, root));
    assert!(!st.is_ancestor(child1_1, child1_0));

    assert!(st.is_ancestor(root, child2));
    assert!(!st.is_ancestor(child2, root));

    assert!(!st.is_ancestor(child1_0, child2));
    assert!(!st.is_ancestor(child1_1, child2));
    assert!(!st.is_ancestor(child2, child1_0));
    assert!(!st.is_ancestor(child2, child1_1));
}

/// Tests that we select the correct scroll root in the simple case.
#[test]
fn test_find_scroll_root_simple() {
    let mut st = SceneSpatialTree::new();

    let root = st.add_reference_frame(
        st.root_reference_frame_index(),
        TransformStyle::Flat,
        PropertyBinding::Value(LayoutTransform::identity()),
        ReferenceFrameKind::Transform {
            is_2d_scale_translation: true,
            should_snap: true,
            paired_with_perspective: false,
        },
        LayoutVector2D::new(0.0, 0.0),
        PipelineId::dummy(),
        false,
    );

    let scroll = st.add_scroll_frame(
        root,
        ExternalScrollId(1, PipelineId::dummy()),
        PipelineId::dummy(),
        &LayoutRect::from_size(LayoutSize::new(400.0, 400.0)),
        &LayoutSize::new(800.0, 400.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 0.0),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    assert_eq!(st.find_scroll_root(scroll, true), scroll);
}

/// Tests that we select the root scroll frame rather than the subframe if both are scrollable.
#[test]
fn test_find_scroll_root_sub_scroll_frame() {
    let mut st = SceneSpatialTree::new();

    let root = st.add_reference_frame(
        st.root_reference_frame_index(),
        TransformStyle::Flat,
        PropertyBinding::Value(LayoutTransform::identity()),
        ReferenceFrameKind::Transform {
            is_2d_scale_translation: true,
            should_snap: true,
            paired_with_perspective: false,
        },
        LayoutVector2D::new(0.0, 0.0),
        PipelineId::dummy(),
        false,
    );

    let root_scroll = st.add_scroll_frame(
        root,
        ExternalScrollId(1, PipelineId::dummy()),
        PipelineId::dummy(),
        &LayoutRect::from_size(LayoutSize::new(400.0, 400.0)),
        &LayoutSize::new(800.0, 400.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 0.0),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    let sub_scroll = st.add_scroll_frame(
        root_scroll,
        ExternalScrollId(1, PipelineId::dummy()),
        PipelineId::dummy(),
        &LayoutRect::from_size(LayoutSize::new(400.0, 400.0)),
        &LayoutSize::new(800.0, 400.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 0.0),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    assert_eq!(st.find_scroll_root(sub_scroll, true), root_scroll);
}

/// Tests that we select the sub scroll frame when the root scroll frame is not scrollable.
#[test]
fn test_find_scroll_root_not_scrollable() {
    let mut st = SceneSpatialTree::new();

    let root = st.add_reference_frame(
        st.root_reference_frame_index(),
        TransformStyle::Flat,
        PropertyBinding::Value(LayoutTransform::identity()),
        ReferenceFrameKind::Transform {
            is_2d_scale_translation: true,
            should_snap: true,
            paired_with_perspective: false,
        },
        LayoutVector2D::new(0.0, 0.0),
        PipelineId::dummy(),
        false,
    );

    let root_scroll = st.add_scroll_frame(
        root,
        ExternalScrollId(1, PipelineId::dummy()),
        PipelineId::dummy(),
        &LayoutRect::from_size(LayoutSize::new(400.0, 400.0)),
        &LayoutSize::new(400.0, 400.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 0.0),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    let sub_scroll = st.add_scroll_frame(
        root_scroll,
        ExternalScrollId(1, PipelineId::dummy()),
        PipelineId::dummy(),
        &LayoutRect::from_size(LayoutSize::new(400.0, 400.0)),
        &LayoutSize::new(800.0, 400.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 0.0),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    assert_eq!(st.find_scroll_root(sub_scroll, true), sub_scroll);
}

/// Tests that we select the sub scroll frame when the root scroll frame is too small.
#[test]
fn test_find_scroll_root_too_small() {
    let mut st = SceneSpatialTree::new();

    let root = st.add_reference_frame(
        st.root_reference_frame_index(),
        TransformStyle::Flat,
        PropertyBinding::Value(LayoutTransform::identity()),
        ReferenceFrameKind::Transform {
            is_2d_scale_translation: true,
            should_snap: true,
            paired_with_perspective: false,
        },
        LayoutVector2D::new(0.0, 0.0),
        PipelineId::dummy(),
        false,
    );

    let root_scroll = st.add_scroll_frame(
        root,
        ExternalScrollId(1, PipelineId::dummy()),
        PipelineId::dummy(),
        &LayoutRect::from_size(LayoutSize::new(MIN_SCROLL_ROOT_SIZE, MIN_SCROLL_ROOT_SIZE)),
        &LayoutSize::new(1000.0, 1000.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 0.0),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    let sub_scroll = st.add_scroll_frame(
        root_scroll,
        ExternalScrollId(1, PipelineId::dummy()),
        PipelineId::dummy(),
        &LayoutRect::from_size(LayoutSize::new(400.0, 400.0)),
        &LayoutSize::new(800.0, 400.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 0.0),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    assert_eq!(st.find_scroll_root(sub_scroll, true), sub_scroll);
}

/// Tests that we select the root scroll node, even if it is not scrollable,
/// when encountering a non-axis-aligned transform.
#[test]
fn test_find_scroll_root_perspective() {
    let mut st = SceneSpatialTree::new();

    let root = st.add_reference_frame(
        st.root_reference_frame_index(),
        TransformStyle::Flat,
        PropertyBinding::Value(LayoutTransform::identity()),
        ReferenceFrameKind::Transform {
            is_2d_scale_translation: true,
            should_snap: true,
            paired_with_perspective: false,
        },
        LayoutVector2D::new(0.0, 0.0),
        PipelineId::dummy(),
        false,
    );

    let root_scroll = st.add_scroll_frame(
        root,
        ExternalScrollId(1, PipelineId::dummy()),
        PipelineId::dummy(),
        &LayoutRect::from_size(LayoutSize::new(400.0, 400.0)),
        &LayoutSize::new(400.0, 400.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 0.0),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    let perspective = st.add_reference_frame(
        root_scroll,
        TransformStyle::Flat,
        PropertyBinding::Value(LayoutTransform::identity()),
        ReferenceFrameKind::Perspective {
            scrolling_relative_to: None,
        },
        LayoutVector2D::new(0.0, 0.0),
        PipelineId::dummy(),
        false,
    );

    let sub_scroll = st.add_scroll_frame(
        perspective,
        ExternalScrollId(1, PipelineId::dummy()),
        PipelineId::dummy(),
        &LayoutRect::from_size(LayoutSize::new(400.0, 400.0)),
        &LayoutSize::new(800.0, 400.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 0.0),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    assert_eq!(st.find_scroll_root(sub_scroll, true), root_scroll);
}

/// Tests that encountering a 2D scale or translation transform does not prevent
/// us from selecting the sub scroll frame if the root scroll frame is unscrollable.
#[test]
fn test_find_scroll_root_2d_scale() {
    let mut st = SceneSpatialTree::new();

    let root = st.add_reference_frame(
        st.root_reference_frame_index(),
        TransformStyle::Flat,
        PropertyBinding::Value(LayoutTransform::identity()),
        ReferenceFrameKind::Transform {
            is_2d_scale_translation: true,
            should_snap: true,
            paired_with_perspective: false,
        },
        LayoutVector2D::new(0.0, 0.0),
        PipelineId::dummy(),
        false,
    );

    let root_scroll = st.add_scroll_frame(
        root,
        ExternalScrollId(1, PipelineId::dummy()),
        PipelineId::dummy(),
        &LayoutRect::from_size(LayoutSize::new(400.0, 400.0)),
        &LayoutSize::new(400.0, 400.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 0.0),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    let scale = st.add_reference_frame(
        root_scroll,
        TransformStyle::Flat,
        PropertyBinding::Value(LayoutTransform::identity()),
        ReferenceFrameKind::Transform {
            is_2d_scale_translation: true,
            should_snap: false,
            paired_with_perspective: false,
        },
        LayoutVector2D::new(0.0, 0.0),
        PipelineId::dummy(),
        false,
    );

    let sub_scroll = st.add_scroll_frame(
        scale,
        ExternalScrollId(1, PipelineId::dummy()),
        PipelineId::dummy(),
        &LayoutRect::from_size(LayoutSize::new(400.0, 400.0)),
        &LayoutSize::new(800.0, 400.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 0.0),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    assert_eq!(st.find_scroll_root(sub_scroll, true), sub_scroll);
}

/// Tests that a sticky spatial node is chosen as the scroll root rather than
/// its parent scroll frame
#[test]
fn test_find_scroll_root_sticky() {
    let mut st = SceneSpatialTree::new();

    let root = st.add_reference_frame(
        st.root_reference_frame_index(),
        TransformStyle::Flat,
        PropertyBinding::Value(LayoutTransform::identity()),
        ReferenceFrameKind::Transform {
            is_2d_scale_translation: true,
            should_snap: true,
            paired_with_perspective: false,
        },
        LayoutVector2D::new(0.0, 0.0),
        PipelineId::dummy(),
        false,
    );

    let scroll = st.add_scroll_frame(
        root,
        ExternalScrollId(1, PipelineId::dummy()),
        PipelineId::dummy(),
        &LayoutRect::from_size(LayoutSize::new(400.0, 400.0)),
        &LayoutSize::new(400.0, 800.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 0.0),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    let sticky = st.add_sticky_frame(
        scroll,
        StickyFrameInfo {
            frame_rect: LayoutRect::from_size(LayoutSize::new(400.0, 100.0)),
            margins: euclid::SideOffsets2D::new(Some(0.0), None, None, None),
            vertical_offset_bounds: api::StickyOffsetBounds::new(0.0, 0.0),
            horizontal_offset_bounds: api::StickyOffsetBounds::new(0.0, 0.0),
            current_offset: LayoutVector2D::zero(),
            transform: None
        },
        PipelineId::dummy(),
    );

    assert_eq!(st.find_scroll_root(sticky, true), sticky);
    assert_eq!(st.find_scroll_root(sticky, false), scroll);
}

#[test]
fn test_world_transforms() {
  // Create a spatial tree with a scroll frame node with scroll offset (0, 200).
  let mut cst = SceneSpatialTree::new();
  let scroll = cst.add_scroll_frame(
      cst.root_reference_frame_index(),
      ExternalScrollId(1, PipelineId::dummy()),
      PipelineId::dummy(),
      &LayoutRect::from_size(LayoutSize::new(400.0, 400.0)),
      &LayoutSize::new(400.0, 800.0),
      ScrollFrameKind::Explicit,
      LayoutVector2D::new(0.0, 200.0),
      APZScrollGeneration::default(),
      HasScrollLinkedEffect::No);

  let mut st = SpatialTree::new();
  st.apply_updates(cst.end_frame_and_get_pending_updates());
  st.update_tree(&SceneProperties::new());

  // The node's world transform should reflect the scroll offset,
  // e.g. here it should be (0, -200) to reflect that the content has been
  // scrolled up by 200px.
  assert_eq!(
      st.get_world_transform(scroll).into_transform(),
      LayoutToWorldTransform::translation(0.0, -200.0, 0.0));

  // The node's world viewport transform only reflects enclosing scrolling
  // or transforms. Here we don't have any, so it should be the identity.
  assert_eq!(
      st.get_world_viewport_transform(scroll).into_transform(),
      LayoutToWorldTransform::identity());
}

/// Tests that a spatial node that is async zooming and all of its descendants
/// are correctly marked as having themselves an ancestor that is zooming.
#[test]
fn test_is_ancestor_or_self_zooming() {
    let mut cst = SceneSpatialTree::new();
    let root_reference_frame_index = cst.root_reference_frame_index();

    let root = add_reference_frame(
        &mut cst,
        root_reference_frame_index,
        LayoutTransform::identity(),
        LayoutVector2D::zero(),
    );
    let child1 = add_reference_frame(
        &mut cst,
        root,
        LayoutTransform::identity(),
        LayoutVector2D::zero(),
    );
    let child2 = add_reference_frame(
        &mut cst,
        child1,
        LayoutTransform::identity(),
        LayoutVector2D::zero(),
    );

    let mut st = SpatialTree::new();
    st.apply_updates(cst.end_frame_and_get_pending_updates());

    // Mark the root node as async zooming
    st.get_spatial_node_mut(root).is_async_zooming = true;
    st.update_tree(&SceneProperties::new());

    // Ensure that the root node and all descendants are marked as having
    // themselves or an ancestor zooming
    assert!(st.get_spatial_node(root).is_ancestor_or_self_zooming);
    assert!(st.get_spatial_node(child1).is_ancestor_or_self_zooming);
    assert!(st.get_spatial_node(child2).is_ancestor_or_self_zooming);
}

/// Tests the `is_ancestor_or_self_animating` policy: a CSS-transform reference
/// frame counts as animating (and propagates that to its descendants) only once
/// its bound transform has been observed to actually move. A bound-but-static
/// transform, an APZ scale/translation frame, and static ancestors are not.
#[test]
fn test_is_ancestor_or_self_animating() {
    let mut cst = SceneSpatialTree::new();
    let root_reference_frame_index = cst.root_reference_frame_index();

    // A static reference frame ...
    let root = add_reference_frame(
        &mut cst,
        root_reference_frame_index,
        LayoutTransform::identity(),
        LayoutVector2D::zero(),
    );
    // ... a CSS-transform reference frame below it whose bound transform moves ...
    let animated = cst.add_reference_frame(
        root,
        TransformStyle::Flat,
        PropertyBinding::Binding(api::PropertyBindingKey::new(1), LayoutTransform::identity()),
        ReferenceFrameKind::Transform {
            is_2d_scale_translation: false,
            should_snap: false,
            paired_with_perspective: false,
        },
        LayoutVector2D::zero(),
        PipelineId::dummy(),
        false,
    );
    // ... and a static child of the animated frame.
    let child = add_reference_frame(
        &mut cst,
        animated,
        LayoutTransform::identity(),
        LayoutVector2D::zero(),
    );

    // A CSS-transform reference frame whose bound transform never changes value
    // (e.g. a `hold` animation): it must NOT be treated as animating, so its
    // text stays device-snapped (bug 2051166).
    let static_bound = cst.add_reference_frame(
        root,
        TransformStyle::Flat,
        PropertyBinding::Binding(api::PropertyBindingKey::new(3), LayoutTransform::identity()),
        ReferenceFrameKind::Transform {
            is_2d_scale_translation: false,
            should_snap: false,
            paired_with_perspective: false,
        },
        LayoutVector2D::zero(),
        PipelineId::dummy(),
        false,
    );

    // A bound reference frame marked `is_2d_scale_translation` is an APZ
    // async-zoom / fixed-position frame, not a CSS animation: it must NOT be
    // treated as animating (and must not propagate that to its children) even if
    // its bound transform moves.
    let apz = cst.add_reference_frame(
        root,
        TransformStyle::Flat,
        PropertyBinding::Binding(api::PropertyBindingKey::new(2), LayoutTransform::identity()),
        ReferenceFrameKind::Transform {
            is_2d_scale_translation: true,
            should_snap: true,
            paired_with_perspective: false,
        },
        LayoutVector2D::zero(),
        PipelineId::dummy(),
        false,
    );
    let apz_child = add_reference_frame(
        &mut cst,
        apz,
        LayoutTransform::identity(),
        LayoutVector2D::zero(),
    );

    let mut st = SpatialTree::new();
    st.apply_updates(cst.end_frame_and_get_pending_updates());

    // Feed two frames of dynamic properties: the id 1 (animated) and id 2 (APZ)
    // transforms move, while id 3 (static_bound) holds a constant value.
    let mut props = SceneProperties::new();
    let sample = |props: &mut SceneProperties, moving: LayoutTransform| {
        props.reset_properties();
        props.add_transforms(vec![
            api::PropertyValue { key: api::PropertyBindingKey::new(1), value: moving },
            api::PropertyValue { key: api::PropertyBindingKey::new(2), value: moving },
            api::PropertyValue { key: api::PropertyBindingKey::new(3), value: LayoutTransform::identity() },
        ]);
        props.flush_pending_updates();
    };
    sample(&mut props, LayoutTransform::identity());
    sample(&mut props, LayoutTransform::translation(10.0, 0.0, 0.0));
    st.update_tree(&props);

    // The static ancestor above the animated frame is unaffected.
    assert!(!st.get_spatial_node(root).is_ancestor_or_self_animating);
    // The moving CSS-transform frame and everything below it are marked.
    assert!(st.get_spatial_node(animated).is_ancestor_or_self_animating);
    assert!(st.get_spatial_node(child).is_ancestor_or_self_animating);
    // A bound-but-static transform is not animating.
    assert!(!st.get_spatial_node(static_bound).is_ancestor_or_self_animating);
    // The APZ (async-zoom / fixed) frame and its children are not, despite the
    // bound transform moving.
    assert!(!st.get_spatial_node(apz).is_ancestor_or_self_animating);
    assert!(!st.get_spatial_node(apz_child).is_ancestor_or_self_animating);

    // The latch is monotonic while the binding exists: once id 1 has moved it
    // stays animating even on a frame where its value is unchanged.
    sample(&mut props, LayoutTransform::translation(10.0, 0.0, 0.0));
    st.update_tree(&props);
    assert!(st.get_spatial_node(animated).is_ancestor_or_self_animating);
    assert!(!st.get_spatial_node(static_bound).is_ancestor_or_self_animating);
}
