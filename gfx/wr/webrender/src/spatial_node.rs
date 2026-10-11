
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{ExternalScrollId, PipelineId, PropertyBinding, PropertyBindingId, ReferenceFrameKind};
use api::{APZScrollGeneration, HasScrollLinkedEffect, SampledScrollOffset};
use api::{TransformStyle, StickyOffsetBounds};
use api::units::*;
use crate::spatial_tree::{CoordinateSystem, SpatialNodeIndex, TransformUpdateState};
use crate::spatial_tree::CoordinateSystemId;
use euclid::{Vector2D, SideOffsets2D};
use crate::scene::SceneProperties;
use crate::util::{LayoutFastTransform, MatrixHelpers, ScaleOffset, TransformedRectKind};
use crate::util::VectorHelpers;

/// Defines the content of a spatial node. If the values in the descriptor don't
/// change, that means the rest of the fields in a spatial node will end up with
/// the same result
#[derive(Clone, PartialEq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct SpatialNodeDescriptor {
    /// The type of this node and any data associated with that node type.
    pub node_type: SpatialNodeType,

    /// Pipeline that this layer belongs to
    pub pipeline_id: PipelineId,
}

#[derive(Clone, PartialEq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum SpatialNodeType {
    /// A special kind of node that adjusts its position based on the position
    /// of its parent node and a given set of sticky positioning offset bounds.
    /// Sticky positioned is described in the CSS Positioned Layout Module Level 3 here:
    /// https://www.w3.org/TR/css-position-3/#sticky-pos
    StickyFrame(StickyFrameInfo),

    /// Transforms it's content, but doesn't clip it. Can also be adjusted
    /// by scroll events or setting scroll offsets.
    ScrollFrame(ScrollFrameInfo),

    /// A reference frame establishes a new coordinate space in the tree.
    ReferenceFrame(ReferenceFrameInfo),
}

/// Information about a spatial node that can be queried during either scene of
/// frame building.
pub struct SpatialNodeInfo<'a> {
    /// The type of this node and any data associated with that node type.
    pub node_type: &'a SpatialNodeType,

    /// Parent spatial node. If this is None, we are the root node.
    pub parent: Option<SpatialNodeIndex>,
}

/// Scene building specific representation of a spatial node, which is a much
/// lighter subset of a full spatial node constructed and used for frame building
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(PartialEq)]
pub struct SceneSpatialNode {
    /// Parent spatial node. If this is None, we are the root node.
    pub parent: Option<SpatialNodeIndex>,

    /// Descriptor describing how this spatial node behaves
    pub descriptor: SpatialNodeDescriptor,

    /// If true, this spatial node is known to exist in the root coordinate
    /// system in all cases (it has no animated or complex transforms)
    pub is_root_coord_system: bool,
}

impl SceneSpatialNode {
    pub fn new_reference_frame(
        parent_index: Option<SpatialNodeIndex>,
        transform_style: TransformStyle,
        source_transform: PropertyBinding<LayoutTransform>,
        kind: ReferenceFrameKind,
        origin_in_parent_reference_frame: LayoutVector2D,
        pipeline_id: PipelineId,
        is_root_coord_system: bool,
        is_pipeline_root: bool,
    ) -> Self {
        let info = ReferenceFrameInfo {
            transform_style,
            source_transform,
            kind,
            origin_in_parent_reference_frame,
            is_pipeline_root,
        };
        Self::new(
            pipeline_id,
            parent_index,
            SpatialNodeType::ReferenceFrame(info),
            is_root_coord_system,
        )
    }

    pub fn new_scroll_frame(
        pipeline_id: PipelineId,
        parent_index: SpatialNodeIndex,
        external_id: ExternalScrollId,
        frame_rect: &LayoutRect,
        content_size: &LayoutSize,
        frame_kind: ScrollFrameKind,
        external_scroll_offset: LayoutVector2D,
        offset_generation: APZScrollGeneration,
        has_scroll_linked_effect: HasScrollLinkedEffect,
        is_root_coord_system: bool,
    ) -> Self {
        let node_type = SpatialNodeType::ScrollFrame(ScrollFrameInfo::new(
                *frame_rect,
                LayoutSize::new(
                    (content_size.width - frame_rect.width()).max(0.0),
                    (content_size.height - frame_rect.height()).max(0.0)
                ),
                external_id,
                frame_kind,
                external_scroll_offset,
                offset_generation,
                has_scroll_linked_effect,
            )
        );

        Self::new(
            pipeline_id,
            Some(parent_index),
            node_type,
            is_root_coord_system,
        )
    }

    pub fn new_sticky_frame(
        parent_index: SpatialNodeIndex,
        sticky_frame_info: StickyFrameInfo,
        pipeline_id: PipelineId,
        is_root_coord_system: bool,
    ) -> Self {
        Self::new(
            pipeline_id,
            Some(parent_index),
            SpatialNodeType::StickyFrame(sticky_frame_info),
            is_root_coord_system,
        )
    }

    fn new(
        pipeline_id: PipelineId,
        parent_index: Option<SpatialNodeIndex>,
        node_type: SpatialNodeType,
        is_root_coord_system: bool,
    ) -> Self {
        SceneSpatialNode {
            parent: parent_index,
            descriptor: SpatialNodeDescriptor {
                pipeline_id,
                node_type,
            },
            is_root_coord_system,
        }
    }
}

/// Contains information common among all types of SpatialTree nodes.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct SpatialNode {
    /// The scale/offset of the viewport for this spatial node, relative to the
    /// coordinate system. Includes any accumulated scrolling offsets from nodes
    /// between our reference frame and this node.
    pub viewport_transform: ScaleOffset,

    /// Content scale/offset relative to the coordinate system.
    pub content_transform: ScaleOffset,

    /// The axis-aligned coordinate system id of this node.
    pub coordinate_system_id: CoordinateSystemId,

    /// The current transform kind of this node.
    pub transform_kind: TransformedRectKind,

    /// Pipeline that this layer belongs to
    pub pipeline_id: PipelineId,

    /// Parent layer. If this is None, we are the root node.
    pub parent: Option<SpatialNodeIndex>,

    /// Child layers
    pub children: Vec<SpatialNodeIndex>,

    /// The type of this node and any data associated with that node type.
    pub node_type: SpatialNodeType,

    /// True if this node is transformed by an invertible transform.  If not, display items
    /// transformed by this node will not be displayed and display items not transformed by this
    /// node will not be clipped by clips that are transformed by this node.
    pub invertible: bool,

    /// Whether this specific node is currently being async zoomed.
    /// Should be set when a SetIsTransformAsyncZooming FrameMsg is received.
    pub is_async_zooming: bool,

    /// Whether this node or any of its ancestors is being pinch zoomed.
    /// This is calculated in update(). This will be used to decide whether
    /// to override corresponding picture's raster space as an optimisation.
    pub is_ancestor_or_self_zooming: bool,

    /// Whether this node or any of its ancestors has an animated (property-bound)
    /// transform. Calculated in update(). Used to render text under an animated
    /// transform in local raster space instead of device-snapping it, so its
    /// glyphs don't jitter as the transform is re-sampled each frame - matching
    /// the tree's existing policy of not snapping animated transforms (bug
    /// 637852), which the device text-snap path (bug 2044211) otherwise misses.
    pub is_ancestor_or_self_animating: bool,

    /// True if this node is the spatial node of a picture cache slice.
    pub is_slice_root: bool,

    /// Adjustment that places a slice root scroll or sticky frame on the device
    /// pixel grid. Applied to this node's transforms and to the coordinate system
    /// offset handed to its descendants.
    pub slice_root_snap_offset: WorldVector2D,
}

/// Grid that a device-space offset is quantized to just before it is rounded
/// to a device pixel (`snap_offset`, and the `should_snap` round in
/// `update_transform`). The display-list builder reconstitutes a
/// reference-frame origin as a scrolled-space value plus a re-added (fractional)
/// external scroll offset; at large scroll magnitudes that f32 round-trip can
/// leave the origin slightly off its intended sub-pixel position (a few of the
/// smallest representable float increments, e.g. 201.49994 vs 201.5). That tiny
/// error sits on the device-pixel snap tie and flips snapped content +/-1px
/// frame-to-frame while scrolling. 1/128 px is far above that float noise yet far
/// below a device pixel (about half Gecko's app-unit granularity), so quantizing
/// removes the noise without disturbing genuine sub-pixel placement.
///
/// This is only ever applied to device-space values that are about to be
/// rounded. A reference frame's local origin is left exact: under a scaled
/// ancestor (an SVG viewBox, say) 1/128 of a local unit is a sizeable fraction
/// of a device pixel, and unsnapped (anti-aliased) content below the frame
/// would visibly shift by it.
const DEVICE_OFFSET_QUANTUM: f32 = 1.0 / 128.0;

fn quantize_device_offset(value: f32) -> f32 {
    (value / DEVICE_OFFSET_QUANTUM).round() * DEVICE_OFFSET_QUANTUM
}

/// Snap an offset to be incorporated into a transform, where the local space
/// may be considered the world space. We assume raster scale is 1.0, which
/// may not always be correct if there are intermediate surfaces used, however
/// those are either cases where snapping is not important (e.g. has perspective
/// or is not axis aligned), or an edge case (e.g. SVG filters) which we can accept
/// imperfection for now.
fn snap_offset<OffsetUnits, ScaleUnits>(
    offset: Vector2D<f32, OffsetUnits>,
    scale: Vector2D<f32, ScaleUnits>,
) -> Vector2D<f32, OffsetUnits> {
    // Snap the accumulated (composite) offset of a spatial node to the device
    // pixel grid. Per-prim rect snapping at frame time happens in each surface's
    // raster space (`SpaceSnapper`), which excludes a scroll/sticky slice's own
    // composite offset; that offset must still be snapped here so the slice
    // composites on an integer boundary. Scroll slices already land integer
    // (their sampled offset is pre-snapped); sticky slices rely on this.
    let snapped_x = quantize_device_offset(offset.x * scale.x).round();
    let snapped_y = quantize_device_offset(offset.y * scale.y).round();
    Vector2D::new(
        if scale.x != 0.0 { snapped_x / scale.x } else { offset.x },
        if scale.y != 0.0 { snapped_y / scale.y } else { offset.y },
    )
}

impl SpatialNode {
    pub fn add_child(&mut self, child: SpatialNodeIndex) {
        self.children.push(child);
    }

    pub fn set_scroll_offsets(&mut self, mut offsets: Vec<SampledScrollOffset>) -> bool {
        debug_assert!(offsets.len() > 0);

        let scrolling = match self.node_type {
            SpatialNodeType::ScrollFrame(ref mut scrolling) => scrolling,
            _ => {
                warn!("Tried to scroll a non-scroll node.");
                return false;
            }
        };

        for element in offsets.iter_mut() {
            element.offset = -element.offset - scrolling.external_scroll_offset;

            // Once the final scroll offset (APZ + content external offset) is
            // calculated, we need to snap it to a device pixel. We already snap
            // the final transforms in `update_transform`. However, we need to
            // ensure the offsets are also snapped so that if the offset is used
            // in a nested sticky frame, it is pre-snapped.
            element.offset = element.offset.snap();
        }

        if scrolling.offsets == offsets {
            return false;
        }

        scrolling.offsets = offsets;
        true
    }

    pub fn mark_uninvertible(
        &mut self,
        state: &TransformUpdateState,
    ) {
        self.invertible = false;
        self.viewport_transform = ScaleOffset::identity();
        self.content_transform = ScaleOffset::identity();
        self.coordinate_system_id = state.current_coordinate_system_id;
    }

    pub fn update(
        &mut self,
        state_stack: &[TransformUpdateState],
        coord_systems: &mut Vec<CoordinateSystem>,
        scene_properties: &SceneProperties,
    ) {
        let state = state_stack.last().unwrap();

        self.is_ancestor_or_self_zooming = self.is_async_zooming | state.is_ancestor_or_self_zooming;

        // A reference frame whose transform is bound to a property is re-sampled
        // every frame while the display list stays fixed. That covers a
        // compositor-driven CSS transform animation (which we do want to treat as
        // animating), but also APZ's async pinch-zoom container and fixed-position
        // reference frames, whose transforms are bound so the compositor can
        // update them without a new display list. Those APZ frames are marked
        // `is_2d_scale_translation` (see nsDisplayOwnLayer), which a CSS transform
        // reference frame never is, so exclude them - otherwise the async-zoom
        // container (present on essentially every page) would flag all content as
        // animating and push all text to local raster. Active pinch-zoom itself is
        // already handled by `is_ancestor_or_self_zooming`.
        let self_has_animated_transform = match self.node_type {
            SpatialNodeType::ReferenceFrame(ref info) => match info.source_transform {
                // A bound transform counts as animating only once it has been
                // observed to actually move (bug 2051166): a bound-but-static
                // value - e.g. a CSS animation holding a constant transform -
                // stays on the crisp device text path instead of local raster.
                // When the animation ends the binding is removed (the transform
                // becomes a static Value), so this returns false again.
                PropertyBinding::Binding(ref key, _) => {
                    !matches!(
                        info.kind,
                        ReferenceFrameKind::Transform { is_2d_scale_translation: true, .. }
                    ) && scene_properties.transform_binding_has_moved(key.id)
                }
                PropertyBinding::Value(..) => false,
            },
            _ => false,
        };
        self.is_ancestor_or_self_animating =
            self_has_animated_transform | state.is_ancestor_or_self_animating;

        // If any of our parents was not rendered, we are not rendered either and can just
        // quit here.
        if !state.invertible {
            self.mark_uninvertible(state);
            return;
        }

        self.update_transform(
            state_stack,
            coord_systems,
            scene_properties,
        );

        if !self.invertible {
            self.mark_uninvertible(state);
        }
    }

    pub fn update_transform(
        &mut self,
        state_stack: &[TransformUpdateState],
        coord_systems: &mut Vec<CoordinateSystem>,
        scene_properties: &SceneProperties,
    ) {
        let state = state_stack.last().unwrap();

        // Start by assuming we're invertible
        self.invertible = true;

        match self.node_type {
            SpatialNodeType::ReferenceFrame(ref mut info) => {
                let mut cs_scale_offset = ScaleOffset::identity();
                let mut coordinate_system_id = state.current_coordinate_system_id;

                // Resolve the transform against any property bindings.
                let source_transform = {
                    let source_transform = scene_properties.resolve_layout_transform(&info.source_transform);
                    if let ReferenceFrameKind::Transform { is_2d_scale_translation: true, .. } = info.kind {
                        assert!(source_transform.is_2d_scale_translation(), "Reference frame was marked as only having 2d scale or translation");
                    }

                    LayoutFastTransform::from(source_transform)
                };

                // Do a change-basis operation on the perspective matrix using
                // the scroll offset.
                let source_transform = match info.kind {
                    ReferenceFrameKind::Perspective { scrolling_relative_to: Some(external_id) } => {
                        let mut scroll_offset = LayoutVector2D::zero();

                        for parent_state in state_stack.iter().rev() {
                            if let Some(parent_external_id) = parent_state.external_id {
                                if parent_external_id == external_id {
                                    break;
                                }
                            }

                            scroll_offset += parent_state.scroll_offset;
                        }

                        // Do a change-basis operation on the
                        // perspective matrix using the scroll offset.
                        source_transform
                            .pre_translate(scroll_offset)
                            .then_translate(-scroll_offset)
                    }
                    ReferenceFrameKind::Perspective { scrolling_relative_to: None } |
                    ReferenceFrameKind::Transform { .. } => source_transform,
                };

                // An axis-aligned reference frame composes into a `ScaleOffset`,
                // so its accumulated device offset is snapped below (the
                // `should_snap` round on `cs_scale_offset`); the origin is used
                // as-is here. A non-axis-aligned frame (skew / rotation /
                // perspective) doesn't compose into a `ScaleOffset`, so that
                // path can't reach it, and the frame-time rect pass can't either
                // (`SpaceSnapper` won't snap across a non-axis-aligned frame), so
                // snap the origin's device position here instead - otherwise a
                // fractional origin shifts all content below it.
                let parent_origin = match info.source_transform {
                    PropertyBinding::Value(ref value)
                        if ScaleOffset::from_transform(value).is_none() =>
                    {
                        snap_offset(
                            info.origin_in_parent_reference_frame,
                            state.coordinate_system_relative_scale_offset.scale,
                        )
                    }
                    _ => info.origin_in_parent_reference_frame,
                };

                let resolved_transform =
                    LayoutFastTransform::with_vector(parent_origin)
                        .pre_transform(&source_transform);

                // The transformation for this viewport in world coordinates is the transformation for
                // our parent reference frame, plus any accumulated scrolling offsets from nodes
                // between our reference frame and this node. Finally, we also include
                // whatever local transformation this reference frame provides.
                let relative_transform = resolved_transform
                    .then_translate(snap_offset(state.parent_accumulated_scroll_offset, state.coordinate_system_relative_scale_offset.scale))
                    .to_transform()
                    .with_destination::<LayoutPixel>();

                // Whether 3D content is already flattened into the plane of the
                // enclosing coordinate system. The root system is flat, but
                // doesn't set the flag.
                let parent_flattens = {
                    let parent = &coord_systems[state.current_coordinate_system_id.0 as usize];
                    parent.should_flatten || parent.parent.is_none()
                };
                let mut reset_cs_id = match info.transform_style {
                    TransformStyle::Preserve3D => !state.preserves_3d,
                    // A flat transform flattens its 3D descendants (it sets
                    // `should_flatten` below), so it needs its own coordinate system
                    // when the enclosing one doesn't flatten - under a perspective,
                    // say - even if its own matrix is a plain 2D scale/offset that
                    // could otherwise share the parent system.
                    TransformStyle::Flat => {
                        state.preserves_3d ||
                        (matches!(info.kind, ReferenceFrameKind::Transform { .. }) && !parent_flattens)
                    }
                };

                // We reset the coordinate system upon either crossing the preserve-3d context boundary,
                // or simply a 3D transformation.
                if !reset_cs_id {
                    // Try to update our compatible coordinate system transform. If we cannot, start a new
                    // incompatible coordinate system.
                    match ScaleOffset::from_transform(&relative_transform) {
                        Some(ref scale_offset) => {
                            // Compose with the accumulated parent transform first,
                            // then snap the *accumulated* device offset for
                            // `should_snap` frames. Snapping the local offset before
                            // composing (as this used to) ignores a fractional
                            // ancestor transform and leaves the frame on a sub-pixel
                            // boundary; see bug 1580534. The composed offset is already
                            // in device space, so round it directly — `snap_offset`
                            // would re-apply the accumulated scale and mis-round under a
                            // non-unit-scale ancestor (e.g. `transform: scale(0.5)`,
                            // bug 637852). We generally do not snap animated transforms
                            // as it causes jitter, but we do want to snap the visual
                            // viewport offset when scrolling.
                            cs_scale_offset = scale_offset.then(&state.coordinate_system_relative_scale_offset);
                            if let ReferenceFrameKind::Transform { should_snap: true, .. } = info.kind {
                                cs_scale_offset.offset = Vector2D::new(
                                    quantize_device_offset(cs_scale_offset.offset.x).round(),
                                    quantize_device_offset(cs_scale_offset.offset.y).round(),
                                );
                            }
                        }
                        None => reset_cs_id = true,
                    }
                }
                if reset_cs_id {
                    // If we break 2D axis alignment or have a perspective component, we need to start a
                    // new incompatible coordinate system with which we cannot share clips without masking.
                    let transform = relative_transform.then(
                        &state.coordinate_system_relative_scale_offset.to_transform()
                    );

                    // Push that new coordinate system and record the new id.
                    let coord_system = {
                        let parent_system = &coord_systems[state.current_coordinate_system_id.0 as usize];
                        let mut cur_transform = transform;
                        if parent_system.should_flatten {
                            cur_transform.flatten_z_output();
                        }
                        let world_transform = cur_transform.then(&parent_system.world_transform);
                        let determinant = world_transform.determinant();
                        self.invertible = determinant != 0.0 && !determinant.is_nan();

                        CoordinateSystem {
                            transform,
                            world_transform,
                            should_flatten: match (info.transform_style, info.kind) {
                                (TransformStyle::Flat, ReferenceFrameKind::Transform { .. }) => true,
                                (_, _) => false,
                            },
                            parent: Some(state.current_coordinate_system_id),
                        }
                    };
                    coordinate_system_id = CoordinateSystemId(coord_systems.len() as u32);
                    coord_systems.push(coord_system);
                }

                // Ensure that the current coordinate system ID is propagated to child
                // nodes, even if we encounter a node that is not invertible. This ensures
                // that the invariant in get_relative_transform is not violated.
                self.coordinate_system_id = coordinate_system_id;
                self.viewport_transform = cs_scale_offset;
                self.content_transform = cs_scale_offset;
            }
            SpatialNodeType::StickyFrame(ref mut info) => {
                let animated_offset = if let Some(transform_binding) = info.transform {
                    let transform = scene_properties.resolve_layout_transform(&transform_binding);
                    match ScaleOffset::from_transform(&transform) {
                        Some(ref scale_offset) => {
                            debug_assert!(scale_offset.scale == Vector2D::new(1.0, 1.0),
                                          "Can only animate a translation on sticky elements");
                            LayoutVector2D::from_untyped(scale_offset.offset)
                        }
                        None => {
                            debug_assert!(false, "Can only animate a translation on sticky elements");
                            LayoutVector2D::zero()
                        }
                    }
                } else {
                    LayoutVector2D::zero()
                };

                let sticky_offset = Self::calculate_sticky_offset(
                    &state.nearest_scrolling_ancestor_offset,
                    &state.nearest_scrolling_ancestor_viewport,
                    info,
                );

                // The transformation for the bounds of our viewport is the parent reference frame
                // transform, plus any accumulated scroll offset from our parents, plus any offset
                // provided by our own sticky positioning.
                let accumulated_offset = state.parent_accumulated_scroll_offset + sticky_offset + animated_offset;
                self.viewport_transform = state.coordinate_system_relative_scale_offset
                    .pre_offset(snap_offset(accumulated_offset, state.coordinate_system_relative_scale_offset.scale).to_untyped());
                self.content_transform = self.viewport_transform;

                info.current_offset = sticky_offset + animated_offset;

                self.coordinate_system_id = state.current_coordinate_system_id;
            }
            SpatialNodeType::ScrollFrame(_) => {
                // The transformation for the bounds of our viewport is the parent reference frame
                // transform, plus any accumulated scroll offset from our parents.
                let accumulated_offset = state.parent_accumulated_scroll_offset;
                self.viewport_transform = state.coordinate_system_relative_scale_offset
                    .pre_offset(snap_offset(accumulated_offset, state.coordinate_system_relative_scale_offset.scale).to_untyped());

                // The transformation for any content inside of us is the viewport transformation, plus
                // whatever scrolling offset we supply as well.
                let added_offset = accumulated_offset + self.scroll_offset();
                self.content_transform = state.coordinate_system_relative_scale_offset
                    .pre_offset(snap_offset(added_offset, state.coordinate_system_relative_scale_offset.scale).to_untyped());

                self.coordinate_system_id = state.current_coordinate_system_id;
            }
        }

        // A slice is rasterized relative to its spatial node and composited at a
        // whole device pixel offset. If an unsnapped reference frame leaves the
        // node at a fractional offset, surfaces drawn into the slice that rasterize
        // relative to root end up off its pixel grid and get resampled when
        // composited (bug 2077649). Snap the node so the spatial tree agrees with
        // where the slice is composited. Slice content doesn't move, since it is
        // already composited at the rounded offset.
        self.slice_root_snap_offset = WorldVector2D::zero();
        if self.is_slice_root {
            if let SpatialNodeType::ScrollFrame(..) | SpatialNodeType::StickyFrame(..) = self.node_type {
                debug_assert_eq!(self.coordinate_system_id, CoordinateSystemId::root());
                let offset = self.content_transform.offset;
                let snapped = Vector2D::new(
                    quantize_device_offset(offset.x).round(),
                    quantize_device_offset(offset.y).round(),
                );
                self.slice_root_snap_offset = WorldVector2D::from_untyped(snapped - offset);
                self.content_transform.offset = snapped;
                self.viewport_transform.offset += self.slice_root_snap_offset.to_untyped();
            }
        }

        //TODO: remove the field entirely?
        self.transform_kind = if self.coordinate_system_id.0 == 0 {
            TransformedRectKind::AxisAligned
        } else {
            TransformedRectKind::Complex
        };
    }

    fn calculate_sticky_offset(
        viewport_scroll_offset: &LayoutVector2D,
        viewport_rect: &LayoutRect,
        info: &StickyFrameInfo
    ) -> LayoutVector2D {
        if info.margins.top.is_none() && info.margins.bottom.is_none() &&
            info.margins.left.is_none() && info.margins.right.is_none() {
            return LayoutVector2D::zero();
        }

        // The viewport and margins of the item establishes the maximum amount that it can
        // be offset in order to keep it on screen. Since we care about the relationship
        // between the scrolled content and unscrolled viewport we adjust the viewport's
        // position by the scroll offset in order to work with their relative positions on the
        // page. `frame_rect` is the item's natural (unstuck) position: the display-list
        // builder removed the offset layout had already applied, so we compute the full
        // sticky offset here rather than a delta on top of a pre-applied amount.
        let mut sticky_rect = info.frame_rect.translate(*viewport_scroll_offset);

        let mut sticky_offset = LayoutVector2D::zero();
        if let Some(margin) = info.margins.top {
            let top_viewport_edge = viewport_rect.min.y + margin;
            if sticky_rect.min.y < top_viewport_edge {
                // If the sticky rect is positioned above the top edge of the viewport (plus margin)
                // we move it down so that it is fully inside the viewport.
                sticky_offset.y = top_viewport_edge - sticky_rect.min.y;
            }
        }

        // If we don't have a sticky-top offset, check for handling the bottom margin case.
        if sticky_offset.y <= 0.0 {
            if let Some(margin) = info.margins.bottom {
                // If sticky_offset.y is nonzero that means we must have set it
                // in the sticky-top handling code above, so this item must have
                // both top and bottom sticky margins. We adjust the item's rect
                // by the top-sticky offset, and then combine any offset from
                // the bottom-sticky calculation into sticky_offset below.
                sticky_rect.min.y += sticky_offset.y;
                sticky_rect.max.y += sticky_offset.y;

                // Same as the above case, but inverted for bottom-sticky items. Here
                // we adjust items upwards, resulting in a negative sticky_offset.y.
                let bottom_viewport_edge = viewport_rect.max.y - margin;
                if sticky_rect.max.y > bottom_viewport_edge {
                    sticky_offset.y += bottom_viewport_edge - sticky_rect.max.y;
                }
            }
        }

        // Same as above, but for the x-axis.
        if let Some(margin) = info.margins.left {
            let left_viewport_edge = viewport_rect.min.x + margin;
            if sticky_rect.min.x < left_viewport_edge {
                sticky_offset.x = left_viewport_edge - sticky_rect.min.x;
            }
        }

        if sticky_offset.x <= 0.0 {
            if let Some(margin) = info.margins.right {
                sticky_rect.min.x += sticky_offset.x;
                sticky_rect.max.x += sticky_offset.x;
                let right_viewport_edge = viewport_rect.max.x - margin;
                if sticky_rect.max.x > right_viewport_edge {
                    sticky_offset.x += right_viewport_edge - sticky_rect.max.x;
                }
            }
        }

        // Clamp the sticky offset to the provided bounds, which describe how far
        // the item can travel from its natural position.
        let clamp = |value: f32, bounds: &StickyOffsetBounds| {
            value.max(bounds.min).min(bounds.max)
        };
        sticky_offset.y = clamp(sticky_offset.y, &info.vertical_offset_bounds);
        sticky_offset.x = clamp(sticky_offset.x, &info.horizontal_offset_bounds);

        sticky_offset
    }

    pub fn prepare_state_for_children(&self, state: &mut TransformUpdateState) {
        state.current_coordinate_system_id = self.coordinate_system_id;
        state.is_ancestor_or_self_zooming = self.is_ancestor_or_self_zooming;
        state.is_ancestor_or_self_animating = self.is_ancestor_or_self_animating;
        state.invertible &= self.invertible;

        // The transformation we are passing is the transformation of the parent
        // reference frame and the offset is the accumulated offset of all the nodes
        // between us and the parent reference frame. If we are a reference frame,
        // we need to reset both these values.
        state.coordinate_system_relative_scale_offset.offset += self.slice_root_snap_offset.to_untyped();
        match self.node_type {
            SpatialNodeType::StickyFrame(ref info) => {
                // We don't translate the combined rect by the sticky offset, because sticky
                // offsets actually adjust the node position itself, whereas scroll offsets
                // only apply to contents inside the node.
                state.parent_accumulated_scroll_offset += info.current_offset;
                // We want nested sticky items to take into account the shift
                // we applied as well.
                state.nearest_scrolling_ancestor_offset += info.current_offset;
                state.preserves_3d = false;
                state.external_id = None;
                // A sticky offset translates the entire subtree uniformly (its
                // perspective origin moves with the element), so it must not
                // contribute to the perspective change-of-basis. Only genuine
                // scroll offsets do. `scroll_offset` is read solely by that
                // perspective path (see ReferenceFrameKind::Perspective).
                state.scroll_offset = LayoutVector2D::zero();
            }
            SpatialNodeType::ScrollFrame(ref scrolling) => {
                state.parent_accumulated_scroll_offset += scrolling.offset();
                state.nearest_scrolling_ancestor_offset = scrolling.offset();
                state.nearest_scrolling_ancestor_viewport = scrolling.viewport_rect;
                state.preserves_3d = false;
                state.external_id = Some(scrolling.external_id);
                state.scroll_offset = scrolling.offset() + scrolling.external_scroll_offset;
            }
            SpatialNodeType::ReferenceFrame(ref info) => {
                state.external_id = None;
                state.scroll_offset = LayoutVector2D::zero();
                state.preserves_3d = info.transform_style == TransformStyle::Preserve3D;
                state.parent_accumulated_scroll_offset = LayoutVector2D::zero();
                state.coordinate_system_relative_scale_offset = self.content_transform;
                let translation = -info.origin_in_parent_reference_frame;
                state.nearest_scrolling_ancestor_viewport =
                    state.nearest_scrolling_ancestor_viewport
                       .translate(translation);
            }
        }
    }

    pub fn scroll_offset(&self) -> LayoutVector2D {
        match self.node_type {
            SpatialNodeType::ScrollFrame(ref scrolling) => scrolling.offset(),
            _ => LayoutVector2D::zero(),
        }
    }

    pub fn matches_external_id(&self, external_id: ExternalScrollId) -> bool {
        match self.node_type {
            SpatialNodeType::ScrollFrame(ref info) if info.external_id == external_id => true,
            _ => false,
        }
    }

    /// Returns true for ReferenceFrames whose source_transform is
    /// bound to the property binding id.
    pub fn is_transform_bound_to_property(&self, id: PropertyBindingId) -> bool {
        if let SpatialNodeType::ReferenceFrame(ref info) = self.node_type {
            if let PropertyBinding::Binding(key, _) = info.source_transform {
                id == key.id
            } else {
                false
            }
        } else {
            false
        }
    }
}

/// Defines whether we have an implicit scroll frame for a pipeline root,
/// or an explicitly defined scroll frame from the display list.
#[derive(Copy, Clone, Debug, PartialEq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum ScrollFrameKind {
    PipelineRoot {
        is_root_pipeline: bool,
    },
    Explicit,
}

#[derive(Clone, Debug, PartialEq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ScrollFrameInfo {
    /// The rectangle of the viewport of this scroll frame. This is important for
    /// positioning of items inside child StickyFrames.
    pub viewport_rect: LayoutRect,

    /// Amount that this ScrollFrame can scroll in both directions.
    pub scrollable_size: LayoutSize,

    /// An external id to identify this scroll frame to API clients. This
    /// allows setting scroll positions via the API without relying on ClipsIds
    /// which may change between frames.
    pub external_id: ExternalScrollId,

    /// Stores whether this is a scroll frame added implicitly by WR when adding
    /// a pipeline (either the root or an iframe). We need to exclude these
    /// when searching for scroll roots we care about for picture caching.
    /// TODO(gw): I think we can actually completely remove the implicit
    ///           scroll frame being added by WR, and rely on the embedder
    ///           to define scroll frames. However, that involves API changes
    ///           so we will use this as a temporary hack!
    pub frame_kind: ScrollFrameKind,

    /// Amount that visual components attached to this scroll node have been
    /// pre-scrolled in their local coordinates.
    pub external_scroll_offset: LayoutVector2D,

    /// A set of a pair of negated scroll offset and scroll generation of this
    /// scroll node. The negated scroll offset is including the pre-scrolled
    /// amount. If, for example, a scroll node was pre-scrolled to y=10 (10
    /// pixels down from the initial unscrolled position), then
    /// `external_scroll_offset` would be (0,10), and this `offset` field would
    /// be (0,-10). If WebRender is then asked to change the scroll position by
    /// an additional 10 pixels (without changing the pre-scroll amount in the
    /// display list), `external_scroll_offset` would remain at (0,10) and
    /// `offset` would change to (0,-20).
    pub offsets: Vec<SampledScrollOffset>,

    /// The generation of the external_scroll_offset.
    /// This is used to pick up the most appropriate scroll offset sampled
    /// off the main thread.
    pub offset_generation: APZScrollGeneration,

    /// Whether the document containing this scroll frame has any scroll-linked
    /// effect or not.
    pub has_scroll_linked_effect: HasScrollLinkedEffect,
}

/// Manages scrolling offset.
impl ScrollFrameInfo {
    pub fn new(
        viewport_rect: LayoutRect,
        scrollable_size: LayoutSize,
        external_id: ExternalScrollId,
        frame_kind: ScrollFrameKind,
        external_scroll_offset: LayoutVector2D,
        offset_generation: APZScrollGeneration,
        has_scroll_linked_effect: HasScrollLinkedEffect,
    ) -> ScrollFrameInfo {
        ScrollFrameInfo {
            viewport_rect,
            scrollable_size,
            external_id,
            frame_kind,
            external_scroll_offset,
            offsets: vec![SampledScrollOffset{
                // If this scroll frame is a newly created one, using
                // `external_scroll_offset` and `offset_generation` is correct.
                // If this scroll frame is a result of updating an existing
                // scroll frame and if there have already been sampled async
                // scroll offsets by APZ, then these offsets will be replaced in
                // SpatialTree::set_scroll_offsets via a
                // RenderBackend::update_document call.
                offset: -external_scroll_offset,
                generation: offset_generation.clone(),
            }],
            offset_generation,
            has_scroll_linked_effect,
        }
    }

    pub fn offset(&self) -> LayoutVector2D {
        debug_assert!(self.offsets.len() > 0, "There should be at least one sampled offset!");

        if self.has_scroll_linked_effect == HasScrollLinkedEffect::No {
            // If there's no scroll-linked effect, use the one-frame delay offset.
            return self.offsets.first().map_or(LayoutVector2D::zero(), |sampled| sampled.offset);
        }

        match self.offsets.iter().find(|sampled| sampled.generation == self.offset_generation) {
            // If we found an offset having the same generation, use it.
            Some(sampled) => sampled.offset,
            // If we don't have any offset having the same generation, i.e.
            // the generation of this scroll frame is behind sampled offsets,
            // use the first queued sampled offset.
            _ => self.offsets.first().map_or(LayoutVector2D::zero(), |sampled| sampled.offset),
        }
    }
}

/// Contains information about reference frames.
#[derive(Copy, Clone, Debug, PartialEq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ReferenceFrameInfo {
    /// The source transform and perspective matrices provided by the stacking context
    /// that forms this reference frame. We maintain the property binding information
    /// here so that we can resolve the animated transform and update the tree each
    /// frame.
    pub source_transform: PropertyBinding<LayoutTransform>,
    pub transform_style: TransformStyle,
    pub kind: ReferenceFrameKind,

    /// The original, not including the transform and relative to the parent reference frame,
    /// origin of this reference frame. This is already rolled into the `transform' property, but
    /// we also store it here to properly transform the viewport for sticky positioning.
    pub origin_in_parent_reference_frame: LayoutVector2D,

    /// True if this is the root reference frame for a given pipeline. This is only used
    /// by the hit-test code, perhaps we can change the interface to not require this.
    pub is_pipeline_root: bool,
}

#[derive(Clone, Debug, PartialEq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct StickyFrameInfo {
  pub margins: SideOffsets2D<Option<f32>, LayoutPixel>,
  pub frame_rect: LayoutRect,
    pub vertical_offset_bounds: StickyOffsetBounds,
    pub horizontal_offset_bounds: StickyOffsetBounds,
    pub current_offset: LayoutVector2D,
    pub transform: Option<PropertyBinding<LayoutTransform>>,
}

impl StickyFrameInfo {
    pub fn new(
        frame_rect: LayoutRect,
        margins: SideOffsets2D<Option<f32>, LayoutPixel>,
        vertical_offset_bounds: StickyOffsetBounds,
        horizontal_offset_bounds: StickyOffsetBounds,
        transform: Option<PropertyBinding<LayoutTransform>>,
    ) -> StickyFrameInfo {
        StickyFrameInfo {
            frame_rect,
            margins,
            vertical_offset_bounds,
            horizontal_offset_bounds,
            current_offset: LayoutVector2D::zero(),
            transform,
        }
    }
}

#[test]
fn test_cst_perspective_relative_scroll() {
    // Verify that when computing the offset from a perspective transform
    // to a relative scroll node that any external scroll offset is
    // ignored. This is because external scroll offsets are not
    // propagated across reference frame boundaries.

    // It's not currently possible to verify this with a wrench reftest,
    // since wrench doesn't understand external scroll ids. When wrench
    // supports this, we could also verify with a reftest.

    use crate::spatial_tree::{SceneSpatialTree, SpatialTree};
    use euclid::Angle;

    let mut cst = SceneSpatialTree::new();
    let pipeline_id = PipelineId::dummy();
    let ext_scroll_id = ExternalScrollId(1, pipeline_id);
    let transform = LayoutTransform::rotation(0.0, 0.0, 1.0, Angle::degrees(45.0));

    let root = cst.add_reference_frame(
        cst.root_reference_frame_index(),
        TransformStyle::Flat,
        PropertyBinding::Value(LayoutTransform::identity()),
        ReferenceFrameKind::Transform {
            is_2d_scale_translation: false,
            should_snap: false,
            paired_with_perspective: false,
        },
        LayoutVector2D::zero(),
        pipeline_id,
        false,
    );

    let scroll_frame_1 = cst.add_scroll_frame(
        root,
        ext_scroll_id,
        pipeline_id,
        &LayoutRect::from_size(LayoutSize::new(100.0, 100.0)),
        &LayoutSize::new(100.0, 500.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::zero(),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    let scroll_frame_2 = cst.add_scroll_frame(
        scroll_frame_1,
        ExternalScrollId(2, pipeline_id),
        pipeline_id,
        &LayoutRect::from_size(LayoutSize::new(100.0, 100.0)),
        &LayoutSize::new(100.0, 500.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 50.0),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    let ref_frame = cst.add_reference_frame(
        scroll_frame_2,
        TransformStyle::Preserve3D,
        PropertyBinding::Value(transform),
        ReferenceFrameKind::Perspective {
            scrolling_relative_to: Some(ext_scroll_id),
        },
        LayoutVector2D::zero(),
        pipeline_id,
        false,
    );

    let mut st = SpatialTree::new();
    st.apply_updates(cst.end_frame_and_get_pending_updates());
    st.update_tree(&SceneProperties::new());

    let world_transform = st.get_world_transform(ref_frame).into_transform().cast_unit();
    let ref_transform = transform.then_translate(LayoutVector3D::new(0.0, -50.0, 0.0));
    assert!(world_transform.approx_eq(&ref_transform));
}

#[test]
fn test_cst_perspective_relative_sticky() {
    // Verify that a sticky offset applied to a node between a perspective
    // reference frame and its relative scroll node does NOT get folded into
    // the perspective change-of-basis. A sticky offset translates the whole
    // subtree uniformly (the perspective origin moves with the element), so it
    // must not conjugate the perspective matrix - otherwise the perspective
    // content tips/distorts as the sticky offset grows with scrolling
    // (bug 2053507).

    use crate::spatial_tree::{SceneSpatialTree, SpatialTree};

    let mut cst = SceneSpatialTree::new();
    let pipeline_id = PipelineId::dummy();
    let ext_scroll_id = ExternalScrollId(1, pipeline_id);
    let transform = LayoutTransform::perspective(1000.0);

    let root = cst.add_reference_frame(
        cst.root_reference_frame_index(),
        TransformStyle::Flat,
        PropertyBinding::Value(LayoutTransform::identity()),
        ReferenceFrameKind::Transform {
            is_2d_scale_translation: false,
            should_snap: false,
            paired_with_perspective: false,
        },
        LayoutVector2D::zero(),
        pipeline_id,
        false,
    );

    // Pre-scroll the scroll frame by 100px. The child sticky frame then sticks
    // to the top, producing a +100px sticky offset that exactly cancels the
    // scroll offset in the accumulated transform (a stuck element stays put).
    let scroll_frame = cst.add_scroll_frame(
        root,
        ext_scroll_id,
        pipeline_id,
        &LayoutRect::from_size(LayoutSize::new(100.0, 100.0)),
        &LayoutSize::new(100.0, 500.0),
        ScrollFrameKind::Explicit,
        LayoutVector2D::new(0.0, 100.0),
        APZScrollGeneration::default(),
        HasScrollLinkedEffect::No,
    );

    let sticky_frame = cst.add_sticky_frame(
        scroll_frame,
        StickyFrameInfo::new(
            LayoutRect::from_size(LayoutSize::new(100.0, 50.0)),
            SideOffsets2D::new(Some(0.0), None, None, None),
            StickyOffsetBounds::new(0.0, 1000.0),
            StickyOffsetBounds::new(0.0, 0.0),
            None,
        ),
        pipeline_id,
    );

    let ref_frame = cst.add_reference_frame(
        sticky_frame,
        TransformStyle::Preserve3D,
        PropertyBinding::Value(transform),
        ReferenceFrameKind::Perspective {
            scrolling_relative_to: Some(ext_scroll_id),
        },
        LayoutVector2D::zero(),
        pipeline_id,
        false,
    );

    let mut st = SpatialTree::new();
    st.apply_updates(cst.end_frame_and_get_pending_updates());
    st.update_tree(&SceneProperties::new());

    // The scroll (-100) and sticky (+100) offsets cancel, so the perspective
    // frame's world transform must be exactly the untouched perspective matrix.
    // If the sticky offset leaked into the change-of-basis it would be
    // conjugated and this would fail.
    let world_transform = st.get_world_transform(ref_frame).into_transform().cast_unit();
    assert!(world_transform.approx_eq(&transform));
}
