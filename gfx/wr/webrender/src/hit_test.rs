/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{BorderRadius, ClipMode, HitTestResultItem, HitTestResult, ItemTag, PrimitiveFlags};
use api::{PipelineId, ApiHitTester, FillRule, Path, PathEvent};
use api::units::*;
use crate::bezier;
use crate::clip::{rounded_rectangle_contains_point, ClipNodeId, ClipTreeBuilder};
use crate::clip::{ClipItemKey, ClipItemKeyKind};
use crate::prim_store::PolygonKey;
use crate::scene_builder_thread::{Interners, SceneDlStores};
use crate::spatial_tree::{SpatialNodeIndex, SpatialTree};
use crate::internal_types::{FastHashMap, LayoutPrimitiveInfo};
use std::sync::{Arc, Mutex};
use crate::util::{FastTransform, LayoutToWorldFastTransform};

pub struct SharedHitTester {
    // We don't really need a mutex here. We could do with some sort of
    // atomic-atomic-ref-counted pointer (an Arc which would let the pointer
    // be swapped atomically like an AtomicPtr).
    // In practive this shouldn't cause performance issues, though.
    hit_tester: Mutex<Arc<HitTester>>,
}

impl SharedHitTester {
    pub fn new() -> Self {
        SharedHitTester {
            hit_tester: Mutex::new(Arc::new(HitTester::empty())),
        }
    }

    pub fn get_ref(&self) -> Arc<HitTester> {
        let guard = self.hit_tester.lock().unwrap();
        Arc::clone(&*guard)
    }

    pub(crate) fn update(&self, new_hit_tester: Arc<HitTester>) {
        let mut guard = self.hit_tester.lock().unwrap();
        *guard = new_hit_tester;
    }
}

impl ApiHitTester for SharedHitTester {
    fn hit_test(&self,
        point: WorldPoint,
    ) -> HitTestResult {
        self.get_ref().hit_test(HitTest::new(point))
    }
}

/// A copy of important spatial node data to use during hit testing. This a copy of
/// data from the SpatialTree that will persist as a new frame is under construction,
/// allowing hit tests consistent with the currently rendered frame.
#[derive(MallocSizeOf)]
struct HitTestSpatialNode {
    /// The pipeline id of this node.
    pipeline_id: PipelineId,

    /// World transform for content transformed by this node.
    world_content_transform: LayoutToWorldFastTransform,
}

#[derive(MallocSizeOf)]
struct HitTestClipNode {
    /// A particular point must be inside all of these regions to be considered clipped in
    /// for the purposes of a hit test.
    region: HitTestRegion,
    /// The positioning node for this clip
    spatial_node_index: SpatialNodeIndex,
    /// Parent clip node
    parent: ClipNodeId,
}

impl HitTestClipNode {
    fn new(
        item: &ClipItemKey,
        clip_rect: LayoutRect,
        interners: &Interners,
        dl_stores: &SceneDlStores,
        parent: ClipNodeId,
        spatial_node_index: SpatialNodeIndex,
    ) -> Self {
        let region = match item.kind {
            ClipItemKeyKind::Rectangle(mode) => {
                HitTestRegion::Rectangle(clip_rect, mode)
            }
            ClipItemKeyKind::Border(_) => {
                HitTestRegion::Rectangle(clip_rect, ClipMode::Clip)
            }
            ClipItemKeyKind::RoundedRectangle(radius, _, mode) => {
                // TODO(wsmind): implement hit-testing for corner-shape
                HitTestRegion::RoundedRectangle(clip_rect, radius.into(), mode)
            }
            ClipItemKeyKind::ImageMask(_, polygon_handle) => {
                if let Some(handle) = polygon_handle {
                    // Retrieve the polygon data from the interner.
                    let polygon = &interners.polygon[handle];
                    HitTestRegion::Polygon(clip_rect, *polygon)
                } else {
                    HitTestRegion::Rectangle(clip_rect, ClipMode::Clip)
                }
            }
            ClipItemKeyKind::Path(path, _, fill_rule, _) => {
                HitTestRegion::Path(clip_rect, dl_stores.path[path].clone(), fill_rule)
            }
        };

        HitTestClipNode {
            region,
            spatial_node_index,
            parent,
        }
    }
}

#[derive(Clone, MallocSizeOf)]
struct HitTestingItem {
    rect: LayoutRect,
    tag: ItemTag,
    animation_id: u64,
    is_backface_visible: bool,
    spatial_node_index: SpatialNodeIndex,
    clip_node_id: ClipNodeId,
}

impl HitTestingItem {
    fn new(
        tag: ItemTag,
        animation_id: u64,
        info: &LayoutPrimitiveInfo,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
    ) -> HitTestingItem {
        HitTestingItem {
            rect: info.rect,
            tag,
            animation_id,
            is_backface_visible: info.flags.contains(PrimitiveFlags::IS_BACKFACE_VISIBLE),
            spatial_node_index,
            clip_node_id,
        }
    }
}

/// Statistics about allocation sizes of current hit tester,
/// used to pre-allocate size of the next hit tester.
pub struct HitTestingSceneStats {
    pub clip_nodes_count: usize,
    pub items_count: usize,
}

impl HitTestingSceneStats {
    pub fn empty() -> Self {
        HitTestingSceneStats {
            clip_nodes_count: 0,
            items_count: 0,
        }
    }
}

/// Defines the immutable part of a hit tester for a given scene.
/// The hit tester is recreated each time a frame is built, since
/// it relies on the current values of the spatial tree.
/// However, the clip chain and item definitions don't change,
/// so they are created once per scene, and shared between
/// hit tester instances via Arc.
#[derive(MallocSizeOf)]
pub struct HitTestingScene {
    clip_nodes: FastHashMap<ClipNodeId, HitTestClipNode>,

    /// List of hit testing primitives.
    items: Vec<HitTestingItem>,
}

impl HitTestingScene {
    /// Construct a new hit testing scene, pre-allocating to size
    /// provided by previous scene stats.
    pub fn new(stats: &HitTestingSceneStats) -> Self {
        HitTestingScene {
            clip_nodes: FastHashMap::default(),
            items: Vec::with_capacity(stats.items_count),
        }
    }

    pub fn reset(&mut self) {
        self.clip_nodes.clear();
        self.items.clear();
    }

    /// Get stats about the current scene allocation sizes.
    pub fn get_stats(&self) -> HitTestingSceneStats {
        HitTestingSceneStats {
            clip_nodes_count: 0,
            items_count: self.items.len(),
        }
    }

    fn add_clip_node(
        &mut self,
        clip_node_id: ClipNodeId,
        clip_tree_builder: &ClipTreeBuilder,
        interners: &Interners,
        dl_stores: &SceneDlStores,
    ) {
        if clip_node_id == ClipNodeId::NONE {
            return;
        }

        if !self.clip_nodes.contains_key(&clip_node_id) {
            let src_clip_node = clip_tree_builder.get_node(clip_node_id);
            let clip_item = &interners.clip[src_clip_node.handle];

            // SNAPTODO: Scene-build hit-test scene captures the unsnapped
            // clip rect. Snapping happens against frame-time spatial state
            // which isn't available here; audit hit-test consumers to
            // confirm using the unsnapped value is correct for hit
            // semantics, or apply a frame-time snap before testing.
            let clip_node = HitTestClipNode::new(
                &clip_item.key,
                src_clip_node.unsnapped_clip_rect,
                interners,
                dl_stores,
                src_clip_node.parent,
                src_clip_node.spatial_node_index,
            );

            self.clip_nodes.insert(clip_node_id, clip_node);

            self.add_clip_node(
                src_clip_node.parent,
                clip_tree_builder,
                interners,
                dl_stores,
            );
        }
    }

    /// Add a hit testing primitive.
    pub fn add_item(
        &mut self,
        tag: ItemTag,
        anim_id: u64,
        info: &LayoutPrimitiveInfo,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
        clip_tree_builder: &ClipTreeBuilder,
        interners: &Interners,
        dl_stores: &SceneDlStores,
    ) {
        self.add_clip_node(
            clip_node_id,
            clip_tree_builder,
            interners,
            dl_stores,
        );

        let item = HitTestingItem::new(
            tag,
            anim_id,
            info,
            spatial_node_index,
            clip_node_id,
        );

        self.items.push(item);
    }
}

#[derive(MallocSizeOf)]
enum HitTestRegion {
    Rectangle(LayoutRect, ClipMode),
    RoundedRectangle(LayoutRect, BorderRadius, ClipMode),
    Polygon(LayoutRect, PolygonKey),
    Path(LayoutRect, Path, FillRule),
}

impl HitTestRegion {
    /// `transform` maps the region's space to world space.
    fn contains(&self, point: &LayoutPoint, transform: &LayoutToWorldFastTransform) -> bool {
        match *self {
            HitTestRegion::Rectangle(ref rectangle, ClipMode::Clip) =>
                rectangle.contains(*point),
            HitTestRegion::Rectangle(ref rectangle, ClipMode::ClipOut) =>
                !rectangle.contains(*point),
            HitTestRegion::RoundedRectangle(rect, radii, ClipMode::Clip) =>
                rounded_rectangle_contains_point(point, &rect, &radii),
            HitTestRegion::RoundedRectangle(rect, radii, ClipMode::ClipOut) =>
                !rounded_rectangle_contains_point(point, &rect, &radii),
            HitTestRegion::Polygon(rect, polygon) =>
                polygon_contains_point(point, &rect, &polygon),
            HitTestRegion::Path(ref rect, ref path, fill_rule) => {
                let tolerance = flattening_tolerance_for_transform(transform);
                path_contains_point(*point, rect, path, fill_rule, tolerance)
            }
        }
    }
}

#[derive(MallocSizeOf)]
pub struct HitTester {
    #[ignore_malloc_size_of = "Arc"]
    scene: Arc<HitTestingScene>,
    spatial_nodes: FastHashMap<SpatialNodeIndex, HitTestSpatialNode>,
}

impl HitTester {
    pub fn empty() -> Self {
        HitTester {
            scene: Arc::new(HitTestingScene::new(&HitTestingSceneStats::empty())),
            spatial_nodes: FastHashMap::default(),
        }
    }

    pub fn new(
        scene: Arc<HitTestingScene>,
        spatial_tree: &SpatialTree,
    ) -> HitTester {
        let mut hit_tester = HitTester {
            scene,
            spatial_nodes: FastHashMap::default(),
        };
        hit_tester.read_spatial_tree(spatial_tree);
        hit_tester
    }

    fn read_spatial_tree(
        &mut self,
        spatial_tree: &SpatialTree,
    ) {
        self.spatial_nodes.clear();
        self.spatial_nodes.reserve(spatial_tree.spatial_node_count());

        spatial_tree.visit_nodes(|index, node| {
            //TODO: avoid inverting more than necessary:
            //  - if the coordinate system is non-invertible, no need to try any of these concrete transforms
            //  - if there are other places where inversion is needed, let's not repeat the step

            self.spatial_nodes.insert(index, HitTestSpatialNode {
                pipeline_id: node.pipeline_id,
                world_content_transform: spatial_tree
                    .get_world_transform(index)
                    .into_fast_transform(),
            });
        });
    }

    pub fn hit_test(&self, test: HitTest) -> HitTestResult {
        let mut result = HitTestResult::default();

        let mut current_spatial_node_index = SpatialNodeIndex::INVALID;
        let mut point_in_layer = None;

        // For each hit test primitive
        for item in self.scene.items.iter().rev() {
            let scroll_node = &self.spatial_nodes[&item.spatial_node_index];
            let pipeline_id = scroll_node.pipeline_id;

            // Update the cached point in layer space, if the spatial node
            // changed since last primitive.
            if item.spatial_node_index != current_spatial_node_index {
                point_in_layer = scroll_node
                    .world_content_transform
                    .inverse()
                    .and_then(|inverted| inverted.project_point2d(test.point));
                current_spatial_node_index = item.spatial_node_index;
            }

            // Only consider hit tests on transformable layers.
            let point_in_layer = match point_in_layer {
                Some(p) => p,
                None => continue,
            };

            // If the item's rect or clip rect don't contain this point, it's
            // not a valid hit.
            if !item.rect.contains(point_in_layer) {
                continue;
            }

            // See if any of the clips for this primitive cull out the item.
            let mut current_clip_node_id = item.clip_node_id;
            let mut is_valid = true;

            while current_clip_node_id != ClipNodeId::NONE {
                let clip_node = &self.scene.clip_nodes[&current_clip_node_id];

                let transform = self
                    .spatial_nodes[&clip_node.spatial_node_index]
                    .world_content_transform;
                if let Some(transformed_point) = transform
                    .inverse()
                    .and_then(|inverted| inverted.project_point2d(test.point))
                {
                    if !clip_node.region.contains(&transformed_point, &transform) {
                        is_valid = false;
                        break;
                    }
                }

                current_clip_node_id = clip_node.parent;
            }

            if !is_valid {
                continue;
            }

            // Don't hit items with backface-visibility:hidden if they are facing the back.
            if !item.is_backface_visible && scroll_node.world_content_transform.is_backface_visible() {
                continue;
            }

            result.items.push(HitTestResultItem {
                pipeline: pipeline_id,
                tag: item.tag,
                animation_id: item.animation_id,
            });
        }

        result.items.dedup();
        result
    }
}

#[derive(MallocSizeOf)]
pub struct HitTest {
    point: WorldPoint,
}

impl HitTest {
    pub fn new(
        point: WorldPoint,
    ) -> HitTest {
        HitTest {
            point,
        }
    }
}

fn polygon_contains_point(
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

/// Test where point p is relative to the infinite line that passes through the segment
/// defined by p0 and p1. Point p is on the "left" of the line if the triangle (p0, p1, p)
/// forms a counter-clockwise triangle.
/// > 0 is left of the line
/// < 0 is right of the line
/// == 0 is on the line
fn is_left_of_line(
    p_x: f32,
    p_y: f32,
    p0_x: f32,
    p0_y: f32,
    p1_x: f32,
    p1_y: f32,
) -> f32 {
    (p1_x - p0_x) * (p_y - p0_y) - (p_x - p0_x) * (p1_y - p0_y)
}

fn flattening_tolerance_for_transform(transform: &LayoutToWorldFastTransform) -> f32 {
    let m = match *transform {
        FastTransform::Offset(..) => return 1.0,
        FastTransform::Transform { ref transform, .. } => transform,
    };

    let scales = crate::util::scale_factors(m);
    let mut scale = scales.0.max(scales.1).abs();

    if !scale.is_finite() || scale == 0.0 {
        scale = 1.0;
    }

    // Keep the approximations within roughly half a pixel of the real curves.
    const DEVICE_SPACE_TOLERANCE: f32 = 0.5;

    DEVICE_SPACE_TOLERANCE / scale
}

/// `tolerance` is in the path's space.
fn path_contains_point(
    point: LayoutPoint,
    rect: &LayoutRect,
    path: &Path,
    fill_rule: FillRule,
    tolerance: f32,
) -> bool {
    if !rect.contains(point) {
        return false;
    }

    // Path coordinates are relative to the clip rect's origin.
    let winding = path_winding_number_at_position(point - rect.min.to_vector(), path, tolerance);

    match fill_rule {
        FillRule::Nonzero => winding != 0,
        FillRule::Evenodd => winding.abs() % 2 == 1,
    }
}

fn path_winding_number_at_position(
    point: LayoutPoint,
    path: &Path,
    tolerance: f32,
) -> i32 {
    let mut winding = 0;
    for evt in path.iter() {
        match evt {
            PathEvent::Begin { .. } => {}
            PathEvent::Line { from, to } => {
                test_segment(point, from, to, &mut winding);
            }
            PathEvent::End { last, first, .. } => {
                test_segment(point, last, first, &mut winding);
            }
            PathEvent::Quadratic { from, ctrl, to } => {
                let min_y = from.y.min(ctrl.y).min(to.y);
                let max_y = from.y.max(ctrl.y).max(to.y);

                if min_y > point.y || max_y < point.y {
                    continue;
                }

                let mut current = from;
                bezier::flatten_quadratic(from, ctrl, to, tolerance, &mut |next| {
                    test_segment(point, current, next, &mut winding);
                    current = next;
                });
            }
            PathEvent::Cubic { from, ctrl1, ctrl2, to } => {
                let min_y = from.y.min(ctrl1.y).min(ctrl2.y).min(to.y);
                let max_y = from.y.max(ctrl1.y).max(ctrl2.y).max(to.y);

                if min_y > point.y || max_y < point.y {
                    continue;
                }

                let mut current = from;
                bezier::flatten_cubic(from, ctrl1, ctrl2, to, tolerance, &mut |next| {
                    test_segment(point, current, next, &mut winding);
                    current = next;
                });
            }
        }
    }

    winding
}

fn test_segment(
    point: LayoutPoint,
    from: LayoutPoint,
    to: LayoutPoint,
    winding: &mut i32,
) {
    let y0 = from.y;
    let y1 = to.y;
    let min_y = f32::min(y0, y1);
    let max_y = f32::max(y0, y1);

    if min_y > point.y
        || max_y <= point.y
        || f32::min(from.x, to.x) > point.x
        || y0 == y1
    {
        return;
    }

    let side = is_left_of_line(point.x, point.y, from.x, from.y, to.x, to.y);

    let positive_winding = y1 - y0 >= 0.0;

    let is_before = if positive_winding {
        side > 0.0
    } else {
        side < 0.0
    };

    if is_before {
        return;
    }

    *winding += if positive_winding { 1 } else { -1 };
}


#[test]
fn polygon_clip_is_left_of_point() {
    // Define points of a line through (1, -3) and (-2, 6) to test against.
    // If the triplet consisting of these two points and the test point
    // form a counter-clockwise triangle, then the test point is on the
    // left. The easiest way to visualize this is with an "ascending"
    // line from low-Y to high-Y.
    let p0_x = 1.0;
    let p0_y = -3.0;
    let p1_x = -2.0;
    let p1_y = 6.0;

    // Test some points to the left of the line.
    assert!(is_left_of_line(-9.0, 0.0, p0_x, p0_y, p1_x, p1_y) > 0.0);
    assert!(is_left_of_line(-1.0, 1.0, p0_x, p0_y, p1_x, p1_y) > 0.0);
    assert!(is_left_of_line(1.0, -4.0, p0_x, p0_y, p1_x, p1_y) > 0.0);

    // Test some points on the line.
    assert!(is_left_of_line(-3.0, 9.0, p0_x, p0_y, p1_x, p1_y) == 0.0);
    assert!(is_left_of_line(0.0, 0.0, p0_x, p0_y, p1_x, p1_y) == 0.0);
    assert!(is_left_of_line(100.0, -300.0, p0_x, p0_y, p1_x, p1_y) == 0.0);

    // Test some points to the right of the line.
    assert!(is_left_of_line(0.0, 1.0, p0_x, p0_y, p1_x, p1_y) < 0.0);
    assert!(is_left_of_line(-4.0, 13.0, p0_x, p0_y, p1_x, p1_y) < 0.0);
    assert!(is_left_of_line(5.0, -12.0, p0_x, p0_y, p1_x, p1_y) < 0.0);
}

#[test]
fn polygon_clip_contains_point() {
    // We define the points of a self-overlapping polygon, which we will
    // use to create polygons with different windings and fill rules.
    let p0 = LayoutPoint::new(4.0, 4.0);
    let p1 = LayoutPoint::new(6.0, 4.0);
    let p2 = LayoutPoint::new(4.0, 7.0);
    let p3 = LayoutPoint::new(2.0, 1.0);
    let p4 = LayoutPoint::new(8.0, 1.0);
    let p5 = LayoutPoint::new(6.0, 7.0);

    let poly_clockwise_nonzero = PolygonKey::new(
        &[p5, p4, p3, p2, p1, p0].to_vec(), FillRule::Nonzero
    );
    let poly_clockwise_evenodd = PolygonKey::new(
        &[p5, p4, p3, p2, p1, p0].to_vec(), FillRule::Evenodd
    );
    let poly_counter_clockwise_nonzero = PolygonKey::new(
        &[p0, p1, p2, p3, p4, p5].to_vec(), FillRule::Nonzero
    );
    let poly_counter_clockwise_evenodd = PolygonKey::new(
        &[p0, p1, p2, p3, p4, p5].to_vec(), FillRule::Evenodd
    );

    // We define a rect that provides a bounding clip area of
    // the polygon.
    let rect = LayoutRect::from_size(LayoutSize::new(10.0, 10.0));

    // And we'll test three points of interest.
    let p_inside_once = LayoutPoint::new(5.0, 3.0);
    let p_inside_twice = LayoutPoint::new(5.0, 5.0);
    let p_outside = LayoutPoint::new(9.0, 9.0);

    // We should get the same results for both clockwise and
    // counter-clockwise polygons.
    // For nonzero polygons, the inside twice point is considered inside.
    for poly_nonzero in vec![poly_clockwise_nonzero, poly_counter_clockwise_nonzero].iter() {
        assert_eq!(polygon_contains_point(&p_inside_once, &rect, &poly_nonzero), true);
        assert_eq!(polygon_contains_point(&p_inside_twice, &rect, &poly_nonzero), true);
        assert_eq!(polygon_contains_point(&p_outside, &rect, &poly_nonzero), false);
    }
    // For evenodd polygons, the inside twice point is considered outside.
    for poly_evenodd in vec![poly_clockwise_evenodd, poly_counter_clockwise_evenodd].iter() {
        assert_eq!(polygon_contains_point(&p_inside_once, &rect, &poly_evenodd), true);
        assert_eq!(polygon_contains_point(&p_inside_twice, &rect, &poly_evenodd), false);
        assert_eq!(polygon_contains_point(&p_outside, &rect, &poly_evenodd), false);
    }
}

