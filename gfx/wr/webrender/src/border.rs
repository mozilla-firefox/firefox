/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use euclid::vec2;
use api::{BorderSide, BorderStyle, ColorF};
use api::{NormalBorder as ApiNormalBorder, RepeatMode};
use api::units::*;
use crate::clip::ClipNodeId;
use crate::ellipse::Ellipse;
use crate::renderer::GpuBufferBuilderF;
use crate::render_task::{RenderTask, RenderTaskKind};
use crate::scene_building::SceneBuilder;
use crate::spatial_tree::SpatialNodeIndex;
use crate::gpu_types::{BorderInstance, BorderInstanceGpuData, BorderSegment};
use crate::prim_store::NinePatchDescriptor;
use crate::prim_store::borders::NormalBorderPrim;
use crate::util::{lerp, RectHelpers};
use crate::internal_types::LayoutPrimitiveInfo;
use crate::segment::EdgeMask;

// Using 2048 as the maximum radius in device space before which we
// start stretching is up for debate.
// the value must be chosen so that the corners will not use an
// unreasonable amount of memory but should allow crisp corners in the
// common cases.

/// Maximum resolution in device pixels at which borders are rasterized.
pub const MAX_BORDER_RESOLUTION: u32 = 2048;
/// Maximum number of dots or dashes per segment to avoid freezing and filling up
/// memory with unreasonable inputs. It would be better to address this by not building
/// a list of per-dot information in the first place.
pub const MAX_DASH_COUNT: u32 = 2048;

/// Dotted edges at most this many device pixels wide are drawn with square
/// dots.
const MAX_SQUARE_DOT_WIDTH: f32 = 2.0;

// TODO(gw): Perhaps there is a better way to store
//           the border cache key than duplicating
//           all the border structs with hashable
//           variants...

// `BorderRadiusAu` now lives in `webrender_api` so builder-side interning keys
// can reference it. Re-exported here to keep existing references working.
pub use api::key_types::BorderRadiusAu;

// `BorderSideAu` (with its `From` conversions to/from the api border types) now
// lives in `webrender_api::key_types` so builder-side interning keys can
// reference it. Re-exported to keep existing references working. (`NormalBorderAu`
// also lives there but is now referenced only through `interned_prims`.)
pub use api::key_types::BorderSideAu;

/// Cache key that uniquely identifies a border
/// segment in the render task cache.
#[derive(Clone, Debug, Hash, MallocSizeOf, PartialEq, Eq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BorderSegmentCacheKey {
    pub size: LayoutSizeAu,
    pub radius: LayoutSizeAu,
    pub shape: u32,
    pub shape_offset: LayoutSizeAu,
    pub inset: LayoutSizeAu,
    pub side0: BorderSideAu,
    pub side1: BorderSideAu,
    pub segment: BorderSegment,
    pub do_aa: bool,
    pub h_adjacent_corner_outer: LayoutPointAu,
    pub h_adjacent_corner_radius: LayoutSizeAu,
    pub v_adjacent_corner_outer: LayoutPointAu,
    pub v_adjacent_corner_radius: LayoutSizeAu,
}

// `ensure_no_corner_overlap` now lives in `webrender_api::key_types` so
// builder-side interning can build a stable normal-border key. Re-exported to
// keep existing references working.
pub use api::key_types::ensure_no_corner_overlap;

impl<'a> SceneBuilder<'a> {
    pub fn add_normal_border(
        &mut self,
        info: &LayoutPrimitiveInfo,
        border: &ApiNormalBorder,
        widths: LayoutSideOffsets,
        spatial_node_index: SpatialNodeIndex,
        clip_node_id: ClipNodeId,
    ) {
        self.add_primitive(
            spatial_node_index,
            clip_node_id,
            info,
            NormalBorderPrim {
                border: (*border).into(),
                widths: widths.to_au(),
            },
        );
    }
}

pub trait BorderSideHelpers {
    fn border_color(&self, is_inner_border: bool) -> ColorF;
}

impl BorderSideHelpers for BorderSide {
    fn border_color(&self, is_inner_border: bool) -> ColorF {
        let lighter = match self.style {
            BorderStyle::Inset => is_inner_border,
            BorderStyle::Outset => !is_inner_border,
            _ => return self.color,
        };

        // The modulate colors below are not part of the specification. They are
        // derived from the Gecko source code and experimentation, and used to
        // modulate the colors in order to generate colors for the inset/outset
        // and groove/ridge border styles.
        //
        // NOTE(emilio): Gecko at least takes the background color into
        // account, should we do the same? Looks a bit annoying for this.
        //
        // NOTE(emilio): If you change this algorithm, do the same change on
        // get_colors_for_side in cs_border_segment.glsl.
        if self.color.r != 0.0 || self.color.g != 0.0 || self.color.b != 0.0 {
            let scale = if lighter { 1.0 } else { 2.0 / 3.0 };
            return self.color.scale_rgb(scale)
        }

        let black = if lighter { 0.7 } else { 0.3 };
        ColorF::new(black, black, black, self.color.a)
    }
}

/// The kind of border corner clip.
#[repr(C)]
#[derive(Copy, Debug, Clone, PartialEq)]
pub enum BorderClipKind {
    DashCorner = 1,
    DashEdge = 2,
    Dot = 3,
}

fn compute_outer_and_clip_sign(
    corner_segment: BorderSegment,
    radius: DeviceSize,
) -> (DevicePoint, DeviceVector2D) {
    let outer_scale = match corner_segment {
        BorderSegment::TopLeft => DeviceVector2D::new(0.0, 0.0),
        BorderSegment::TopRight => DeviceVector2D::new(1.0, 0.0),
        BorderSegment::BottomRight => DeviceVector2D::new(1.0, 1.0),
        BorderSegment::BottomLeft => DeviceVector2D::new(0.0, 1.0),
        _ => panic!("bug: expected a corner segment"),
    };
    let outer = DevicePoint::new(
        outer_scale.x * radius.width,
        outer_scale.y * radius.height,
    );

    let clip_sign = DeviceVector2D::new(
        1.0 - 2.0 * outer_scale.x,
        1.0 - 2.0 * outer_scale.y,
    );

    (outer, clip_sign)
}

fn write_dashed_corner_instances(
    corner_radius: DeviceSize,
    widths: DeviceSize,
    segment: BorderSegment,
    base_instance: &BorderInstance,
    instances: &mut Vec<BorderInstance>,
) -> Result<(), ()> {
    let ellipse = Ellipse::new(corner_radius);

    let average_border_width = 0.5 * (widths.width + widths.height);

    let (_half_dash, num_half_dashes) =
        compute_half_dash(average_border_width, ellipse.total_arc_length);

    if num_half_dashes == 0 {
        return Err(());
    }

    let num_half_dashes = num_half_dashes.min(MAX_DASH_COUNT);

    let (outer, clip_sign) = compute_outer_and_clip_sign(segment, corner_radius);

    let instance_count = num_half_dashes / 4 + 1;
    instances.reserve(instance_count as usize);

    let half_dash_arc_length =
        ellipse.total_arc_length / num_half_dashes as f32;
    let dash_length = 2. * half_dash_arc_length;

    let mut current_length = 0.;
    for i in 0..instance_count {
        let arc_length0 = current_length;
        current_length += if i == 0 {
            half_dash_arc_length
        } else {
            dash_length
        };

        let arc_length1 = current_length;
        current_length += dash_length;

        let alpha = ellipse.find_angle_for_arc_length(arc_length0);
        let beta = ellipse.find_angle_for_arc_length(arc_length1);

        let (point0, tangent0) = ellipse.get_point_and_tangent(alpha);
        let (point1, tangent1) = ellipse.get_point_and_tangent(beta);

        let point0 = DevicePoint::new(
            outer.x + clip_sign.x * (corner_radius.width - point0.x),
            outer.y + clip_sign.y * (corner_radius.height - point0.y),
        );

        let tangent0 = DeviceVector2D::new(
            -tangent0.x * clip_sign.x,
            -tangent0.y * clip_sign.y,
        );

        let point1 = DevicePoint::new(
            outer.x + clip_sign.x * (corner_radius.width - point1.x),
            outer.y + clip_sign.y * (corner_radius.height - point1.y),
        );

        let tangent1 = DeviceVector2D::new(
            -tangent1.x * clip_sign.x,
            -tangent1.y * clip_sign.y,
        );

        instances.push(BorderInstance {
            flags: base_instance.flags | ((BorderClipKind::DashCorner as i32) << 24),
            clip_params: [
                point0.x,
                point0.y,
                tangent0.x,
                tangent0.y,
                point1.x,
                point1.y,
                tangent1.x,
                tangent1.y,
            ],
            .. *base_instance
        });
    }

    Ok(())
}

fn write_dotted_corner_instances(
    corner_radius: DeviceSize,
    widths: DeviceSize,
    segment: BorderSegment,
    base_instance: &BorderInstance,
    instances: &mut Vec<BorderInstance>,
) -> Result<(), ()> {
    let mut corner_radius = corner_radius;
    if corner_radius.width < (widths.width / 2.0) {
        corner_radius.width = 0.0;
    }
    if corner_radius.height < (widths.height / 2.0) {
        corner_radius.height = 0.0;
    }

    let (ellipse, max_dot_count) =
        if corner_radius.width == 0. && corner_radius.height == 0. {
            (Ellipse::new(corner_radius), 1)
        } else {
            // The centers of dots follow an ellipse along the middle of the
            // border radius.
            let inner_radius = (corner_radius - widths * 0.5).abs();
            let ellipse = Ellipse::new(inner_radius);

            // Allocate a "worst case" number of dot clips. This can be
            // calculated by taking the minimum edge radius, since that
            // will result in the maximum number of dots along the path.
            let min_diameter = widths.width.min(widths.height);

            // Get the number of circles (assuming spacing of one diameter
            // between dots).
            let max_dot_count = 0.5 * ellipse.total_arc_length / min_diameter;

            // Add space for one extra dot since they are centered at the
            // start of the arc.
            (ellipse, max_dot_count.ceil() as usize)
        };

    if max_dot_count == 0 {
        return Err(());
    }

    if max_dot_count == 1 {
        let dot_diameter = lerp(widths.width, widths.height, 0.5);
        let is_square = widths.width == widths.height && dot_diameter <= MAX_SQUARE_DOT_WIDTH;
        instances.push(BorderInstance {
            flags: base_instance.flags | ((BorderClipKind::Dot as i32) << 24),
            clip_params: [
                widths.width / 2.0, widths.height / 2.0, 0.5 * dot_diameter, is_square as i32 as f32,
                0., 0., 0., 0.,
            ],
            .. *base_instance
        });
        return Ok(());
    }

    let max_dot_count = max_dot_count.min(MAX_DASH_COUNT as usize);

    // FIXME(emilio): Should probably use SmallVec.
    let mut forward_dots = Vec::with_capacity(max_dot_count / 2 + 1);
    let mut back_dots = Vec::with_capacity(max_dot_count / 2 + 1);
    let mut leftover_arc_length = 0.0;

    // Alternate between adding dots at the start and end of the
    // ellipse arc. This ensures that we always end up with an exact
    // half dot at each end of the arc, to match up with the edges.
    forward_dots.push(DotInfo::new(widths.width, widths.width));
    back_dots.push(DotInfo::new(
        ellipse.total_arc_length - widths.height,
        widths.height,
    ));

    let (outer, clip_sign) = compute_outer_and_clip_sign(segment, corner_radius);
    for dot_index in 0 .. max_dot_count {
        let prev_forward_pos = *forward_dots.last().unwrap();
        let prev_back_pos = *back_dots.last().unwrap();

        // Select which end of the arc to place a dot from.
        // This just alternates between the start and end of
        // the arc, which ensures that there is always an
        // exact half-dot at each end of the ellipse.
        let going_forward = dot_index & 1 == 0;

        let (next_dot_pos, leftover) = if going_forward {
            let next_dot_pos =
                prev_forward_pos.arc_pos + 2.0 * prev_forward_pos.diameter;
            (next_dot_pos, prev_back_pos.arc_pos - next_dot_pos)
        } else {
            let next_dot_pos = prev_back_pos.arc_pos - 2.0 * prev_back_pos.diameter;
            (next_dot_pos, next_dot_pos - prev_forward_pos.arc_pos)
        };

        // Use a lerp between each edge's dot
        // diameter, based on the linear distance
        // along the arc to get the diameter of the
        // dot at this arc position.
        let t = next_dot_pos / ellipse.total_arc_length;
        let dot_diameter = lerp(widths.width, widths.height, t);

        // If we can't fit a dot, bail out.
        if leftover < dot_diameter {
            leftover_arc_length = leftover;
            break;
        }

        // We can place a dot!
        let dot = DotInfo::new(next_dot_pos, dot_diameter);
        if going_forward {
            forward_dots.push(dot);
        } else {
            back_dots.push(dot);
        }
    }

    // Now step through the dots, and distribute any extra
    // leftover space on the arc between them evenly. Once
    // the final arc position is determined, generate the correct
    // arc positions and angles that get passed to the clip shader.
    let number_of_dots = forward_dots.len() + back_dots.len();
    let extra_space_per_dot = leftover_arc_length / (number_of_dots - 1) as f32;

    let create_dot_data = |arc_length: f32, dot_radius: f32| -> [f32; 8] {
        // Represents the GPU data for drawing a single dot to a clip mask. The order
        // these are specified must stay in sync with the way this data is read in the
        // dot clip shader.
        let theta = ellipse.find_angle_for_arc_length(arc_length);
        let (center, _) = ellipse.get_point_and_tangent(theta);

        let center = DevicePoint::new(
            outer.x + clip_sign.x * (corner_radius.width - center.x),
            outer.y + clip_sign.y * (corner_radius.height - center.y),
        );

        [center.x, center.y, dot_radius, 0.0, 0.0, 0.0, 0.0, 0.0]
    };

    instances.reserve(number_of_dots);
    for (i, dot) in forward_dots.iter().enumerate() {
        let extra_dist = i as f32 * extra_space_per_dot;
        instances.push(BorderInstance {
            flags: base_instance.flags | ((BorderClipKind::Dot as i32) << 24),
            clip_params: create_dot_data(dot.arc_pos + extra_dist, 0.5 * dot.diameter),
            .. *base_instance
        });
    }

    for (i, dot) in back_dots.iter().enumerate() {
        let extra_dist = i as f32 * extra_space_per_dot;
        instances.push(BorderInstance {
            flags: base_instance.flags | ((BorderClipKind::Dot as i32) << 24),
            clip_params: create_dot_data(dot.arc_pos - extra_dist, 0.5 * dot.diameter),
            .. *base_instance
        });
    }

    Ok(())
}

#[derive(Copy, Clone, Debug)]
struct DotInfo {
    arc_pos: f32,
    diameter: f32,
}

impl DotInfo {
    fn new(arc_pos: f32, diameter: f32) -> DotInfo {
        DotInfo { arc_pos, diameter }
    }
}

/// Information needed to place and draw a border edge.
#[derive(Debug)]
struct EdgeInfo {
    /// Offset in local space to place the edge from origin.
    local_offset: f32,
    /// Size of the edge in local space.
    local_size: f32,
    /// Length of the render task along the edge, in local space. For dashed
    /// and dotted edges this is the size of the repeated pattern; for the
    /// other styles it is a short slice that gets stretched over `local_size`.
    stretch_size: f32,
}

/// Length along the edge of the render task used for styles that don't vary
/// along the edge. Anything above one device pixel would do; a few pixels
/// keeps some slack for filtering at the ends.
const UNIFORM_EDGE_TASK_LENGTH: f32 = 8.0;

impl EdgeInfo {
    fn new(
        local_offset: f32,
        local_size: f32,
        stretch_size: f32,
    ) -> Self {
        Self {
            local_offset,
            local_size,
            stretch_size,
        }
    }
}

// Given a side width and the available space, compute the half-dash (half of
// the 'on' segment) and the count of them for a given segment.
fn compute_half_dash(side_width: f32, total_size: f32) -> (f32, u32) {
    let half_dash = side_width * 1.5;
    // 16k dashes should be enough for anyone
    let num_half_dashes = (total_size / half_dash).ceil().min(16.0 * 1024.0) as u32;

    if num_half_dashes == 0 {
        return (0., 0);
    }

    // TODO(emilio): Gecko has some other heuristics here to start with a full
    // dash when the border side is zero, for example. We might consider those
    // in the future.
    let num_half_dashes = if num_half_dashes % 4 != 0 {
        num_half_dashes + 4 - num_half_dashes % 4
    } else {
        num_half_dashes
    };

    let half_dash = total_size / num_half_dashes as f32;
    (half_dash, num_half_dashes)
}


// Get the needed size in device pixels for an edge,
// based on the border style of that edge. This is used
// to determine how big the render task should be.
fn get_edge_info(
    style: BorderStyle,
    side_width: f32,
    avail_size: f32,
) -> EdgeInfo {
    // To avoid division by zero below.
    if side_width <= 0.0 || avail_size <= 0.0 {
        return EdgeInfo::new(0.0, 0.0, 0.0);
    }

    match style {
        BorderStyle::Dashed => {
            // Basically, two times the dash size.
            let (half_dash, _num_half_dashes) =
                compute_half_dash(side_width, avail_size);
            let stretch_size = 2.0 * 2.0 * half_dash;
            EdgeInfo::new(0., avail_size, stretch_size)
        }
        BorderStyle::Dotted => {
            let dot_and_space_size = 2.0 * side_width;
            if avail_size < dot_and_space_size * 0.75 {
                return EdgeInfo::new(0.0, 0.0, 0.0);
            }
            let approx_dot_count = avail_size / dot_and_space_size;
            let dot_count = approx_dot_count.floor().max(1.0);
            let used_size = dot_count * dot_and_space_size;
            let extra_space = avail_size - used_size;
            let stretch_size = dot_and_space_size;
            let offset = (extra_space * 0.5).round();
            EdgeInfo::new(offset, used_size, stretch_size)
        }
        _ => {
            // These styles don't vary along the edge, so rasterizing a short
            // slice and stretching it produces the same result as rasterizing
            // the whole edge, without allocating a render task as long as the
            // border.
            EdgeInfo::new(0.0, avail_size, avail_size.min(UNIFORM_EDGE_TASK_LENGTH))
        }
    }
}

/// Whether a style's appearance is constant along the length of an edge, in
/// which case the edge only needs a short render task stretched over its
/// length instead of one covering the whole edge.
fn is_uniform_along_edge(style: BorderStyle) -> bool {
    !matches!(style, BorderStyle::Dashed | BorderStyle::Dotted)
}

#[derive(Clone)]
pub struct NormalBorderSegment {
    /// The rect the cached segment texture is drawn into at its natural size.
    /// For corners this is the full corner image rect, which may extend past
    /// the visible area when an adjacent corner overlaps it; for edges it is
    /// the edge rect.
    pub pattern_rect: LayoutRect,
    /// Sub-rect of `pattern_rect` the drawn texture is clipped to. `Some` for
    /// corners (the visible, non-overlapping part); `None` for edges, which
    /// need no per-segment clip.
    pub clip_rect: Option<LayoutRect>,
    pub repeat_x: RepeatMode,
    pub repeat_y: RepeatMode,
    pub edge_flags: EdgeMask,
    pub task_size: LayoutSize,
    pub cache_key: BorderSegmentCacheKey,
    pub is_solid: Option<ColorF>,
}

impl NormalBorderSegment {
    pub fn bounds(&self, clip_rect: &LayoutRect) -> LayoutRect {
        let mut bounds = self.pattern_rect.intersection_unchecked(clip_rect);
        if let Some(clip_rect) = self.clip_rect {
            bounds = bounds.intersection_unchecked(&clip_rect);
        }
        bounds
    }

    pub fn render_task(
        &self,
        cache_size: DeviceIntSize,
        border: &ApiNormalBorder,
        scale: LayoutToDeviceScale,
        gpu_buffer: &mut GpuBufferBuilderF,
    ) -> RenderTask {
        RenderTask::new_dynamic(
            cache_size,
            RenderTaskKind::new_border_segment(build_border_instances(
                &self.cache_key, cache_size, border, scale, gpu_buffer,
            )),
        )
    }

    pub fn repetition(&self, cache_size: DeviceIntSize) -> (LayoutSize, LayoutSize) {
        let mut stretch_size = self.pattern_rect.size();
        let mut spacing = LayoutSize::zero();
        let mut _repeat_offset = LayoutVector2D::zero();
        crate::border::compute_border_repetition(
            self.pattern_rect.size(),
            cache_size.to_f32(),
            self.repeat_x,
            self.repeat_y,
            &mut stretch_size,
            &mut spacing,
            &mut _repeat_offset,
        );

        if self.repeat_x == RepeatMode::Repeat {
            let w = self.pattern_rect.width();
            let sw = stretch_size.width;
            let scale = w / ((w / sw).round() * sw);

            stretch_size.width *= scale;
        }

        if self.repeat_y == RepeatMode::Repeat {
            let h = self.pattern_rect.height();
            let sh = stretch_size.height;
            let scale = h / ((h / sh).round() * sh);

            stretch_size.height *= scale;
        }

        (stretch_size, spacing)
    }
}

/// Create the set of border segments and render task
/// cache keys for a given CSS border.
pub fn create_border_segments(
    rect: LayoutRect,
    border: &ApiNormalBorder,
    widths: &LayoutSideOffsets,
    segment_cb: &mut dyn FnMut(&NormalBorderSegment),
) {
    let overlap = LayoutSize::new(
        (widths.left + widths.right - rect.width()).max(0.0),
        (widths.top + widths.bottom - rect.height()).max(0.0),
    );
    let non_overlapping_widths = LayoutSideOffsets::new(
        widths.top - overlap.height / 2.0,
        widths.right - overlap.width / 2.0,
        widths.bottom - overlap.height / 2.0,
        widths.left - overlap.width / 2.0,
    );

    // Where the corner segments' clips meet in the middle when the widths
    // overlap. `min + non_overlapping.top` and `max - non_overlapping.bottom`
    // are the same number mathematically (the two non-overlapping widths sum to
    // the rect size) but not in f32, and a one-ULP disagreement is enough for
    // the two adjoining segments to round their shared, non-antialiased device
    // edge in opposite directions, leaving a 1px hole (bug 2059620). Evaluate
    // each split once so both sides see identical bits.
    let split_x = rect.min.x + non_overlapping_widths.left;
    let split_y = rect.min.y + non_overlapping_widths.top;
    let (clip_split_x_from_min, clip_split_x_from_max) = if overlap.width > 0.0 {
        (split_x, split_x)
    } else {
        (split_x, rect.max.x - non_overlapping_widths.right)
    };
    let (clip_split_y_from_min, clip_split_y_from_max) = if overlap.height > 0.0 {
        (split_y, split_y)
    } else {
        (split_y, rect.max.y - non_overlapping_widths.bottom)
    };

    let inset_tl = LayoutSize::new(border.inset.left, border.inset.top);
    let inset_tr = LayoutSize::new(border.inset.right, border.inset.top);
    let inset_br = LayoutSize::new(border.inset.right, border.inset.bottom);
    let inset_bl = LayoutSize::new(border.inset.left, border.inset.bottom);

    let max_shape_offsets = LayoutSideOffsets::new(
        rect.width() - border.radius.top_left.width - border.radius.top_right.width,
        rect.height() - border.radius.top_right.height - border.radius.bottom_right.height,
        rect.width() - border.radius.bottom_left.width - border.radius.bottom_right.width,
        rect.height() - border.radius.top_left.height - border.radius.bottom_left.height,
    );

    let shape_offset_tl = if border.radius.shape_top_left < 1.0 {
        LayoutSize::new(
            non_overlapping_widths.top + inset_tl.height.max(0.0),
            non_overlapping_widths.left + inset_tl.width.max(0.0),
        ).min(LayoutSize::new(
            max_shape_offsets.top,
            max_shape_offsets.left,
        ))
    } else {
        LayoutSize::zero()
    };
    let shape_offset_tr = if border.radius.shape_top_right < 1.0 {
        LayoutSize::new(
            non_overlapping_widths.top + inset_tr.height.max(0.0),
           non_overlapping_widths.right + inset_tr.width.max(0.0),
        ).min(LayoutSize::new(
            max_shape_offsets.top,
            max_shape_offsets.right,
        ))
    } else {
        LayoutSize::zero()
    };
    let shape_offset_br = if border.radius.shape_bottom_right < 1.0 {
        LayoutSize::new(
            non_overlapping_widths.bottom + inset_br.height.max(0.0),
           non_overlapping_widths.right + inset_br.width.max(0.0),
        ).min(LayoutSize::new(
            max_shape_offsets.bottom,
            max_shape_offsets.right,
        ))
    } else {
        LayoutSize::zero()
    };
    let shape_offset_bl = if border.radius.shape_bottom_left < 1.0 {
        LayoutSize::new(
            non_overlapping_widths.bottom + inset_bl.height.max(0.0),
           non_overlapping_widths.left + inset_bl.width.max(0.0),
        ).min(LayoutSize::new(
            max_shape_offsets.bottom,
            max_shape_offsets.left,
        ))
    } else {
        LayoutSize::zero()
    };

    let local_size_tl = LayoutSize::new(
        border.radius.top_left.width.max(widths.left),
        border.radius.top_left.height.max(widths.top),
    ) + shape_offset_tl;
    let local_size_tr = LayoutSize::new(
        border.radius.top_right.width.max(widths.right),
        border.radius.top_right.height.max(widths.top),
    ) + shape_offset_tr;
    let local_size_br = LayoutSize::new(
        border.radius.bottom_right.width.max(widths.right),
        border.radius.bottom_right.height.max(widths.bottom),
    ) + shape_offset_br;
    let local_size_bl = LayoutSize::new(
        border.radius.bottom_left.width.max(widths.left),
        border.radius.bottom_left.height.max(widths.bottom),
    ) + shape_offset_bl;

    let top_edge_info = get_edge_info(
        border.top.style,
        widths.top,
        rect.width() - local_size_tl.width - local_size_tr.width,
    );
    let bottom_edge_info = get_edge_info(
        border.bottom.style,
        widths.bottom,
        rect.width() - local_size_bl.width - local_size_br.width,
    );

    let left_edge_info = get_edge_info(
        border.left.style,
        widths.left,
        rect.height() - local_size_tl.height - local_size_bl.height,
    );
    let right_edge_info = get_edge_info(
        border.right.style,
        widths.right,
        rect.height() - local_size_tr.height - local_size_br.height,
    );

    add_edge_segment(
        LayoutRect::from_floats(
            rect.min.x,
            rect.min.y + local_size_tl.height + left_edge_info.local_offset,
            rect.min.x + non_overlapping_widths.left,
            rect.min.y + local_size_tl.height + left_edge_info.local_offset + left_edge_info.local_size,
        ),
        &left_edge_info,
        border.left,
        non_overlapping_widths.left,
        BorderSegment::Left,
        EdgeMask::LEFT | EdgeMask::RIGHT,
        border.do_aa,
        segment_cb,
    );
    add_edge_segment(
        LayoutRect::from_floats(
            rect.min.x + local_size_tl.width + top_edge_info.local_offset,
            rect.min.y,
            rect.min.x + local_size_tl.width + top_edge_info.local_offset + top_edge_info.local_size,
            rect.min.y + non_overlapping_widths.top,
        ),
        &top_edge_info,
        border.top,
        non_overlapping_widths.top,
        BorderSegment::Top,
        EdgeMask::TOP | EdgeMask::BOTTOM,
        border.do_aa,
        segment_cb,
    );
    add_edge_segment(
        LayoutRect::from_floats(
            rect.min.x + rect.width() - non_overlapping_widths.right,
            rect.min.y + local_size_tr.height + right_edge_info.local_offset,
            rect.min.x + rect.width(),
            rect.min.y + local_size_tr.height + right_edge_info.local_offset + right_edge_info.local_size,
        ),
        &right_edge_info,
        border.right,
        non_overlapping_widths.right,
        BorderSegment::Right,
        EdgeMask::RIGHT | EdgeMask::LEFT,
        border.do_aa,
        segment_cb,
    );
    add_edge_segment(
        LayoutRect::from_floats(
            rect.min.x + local_size_bl.width + bottom_edge_info.local_offset,
            rect.min.y + rect.height() - non_overlapping_widths.bottom,
            rect.min.x + local_size_bl.width + bottom_edge_info.local_offset + bottom_edge_info.local_size,
            rect.min.y + rect.height(),
        ),
        &bottom_edge_info,
        border.bottom,
        non_overlapping_widths.bottom,
        BorderSegment::Bottom,
        EdgeMask::BOTTOM | EdgeMask::TOP,
        border.do_aa,
        segment_cb,
    );

    add_corner_segment(
        LayoutRect::from_floats(
            rect.min.x,
            rect.min.y,
            rect.min.x + local_size_tl.width,
            rect.min.y + local_size_tl.height,
        ),
        LayoutRect::from_floats(
            rect.min.x,
            rect.min.y,
            clip_split_x_from_max,
            clip_split_y_from_max,
        ),
        border.left,
        border.top,
        LayoutSize::new(widths.left, widths.top),
        border.radius.top_left,
        border.radius.shape_top_left,
        shape_offset_tl,
        inset_tl,
        BorderSegment::TopLeft,
        EdgeMask::TOP | EdgeMask::LEFT,
        rect.top_right(),
        border.radius.top_right,
        rect.bottom_left(),
        border.radius.bottom_left,
        border.do_aa,
        segment_cb,
    );
    add_corner_segment(
        LayoutRect::from_floats(
            rect.min.x + rect.width() - local_size_tr.width,
            rect.min.y,
            rect.min.x + rect.width(),
            rect.min.y + local_size_tr.height,
        ),
        LayoutRect::from_floats(
            clip_split_x_from_min,
            rect.min.y,
            rect.max.x,
            clip_split_y_from_max,
        ),
        border.top,
        border.right,
        LayoutSize::new(widths.right, widths.top),
        border.radius.top_right,
        border.radius.shape_top_right,
        shape_offset_tr,
        inset_tr,
        BorderSegment::TopRight,
        EdgeMask::TOP | EdgeMask::RIGHT,
        rect.min,
        border.radius.top_left,
        rect.max,
        border.radius.bottom_right,
        border.do_aa,
        segment_cb,
    );
    add_corner_segment(
        LayoutRect::from_floats(
            rect.min.x + rect.width() - local_size_br.width,
            rect.min.y + rect.height() - local_size_br.height,
            rect.min.x + rect.width(),
            rect.min.y + rect.height(),
        ),
        LayoutRect::from_floats(
            clip_split_x_from_min,
            clip_split_y_from_min,
            rect.max.x,
            rect.max.y,
        ),
        border.right,
        border.bottom,
        LayoutSize::new(widths.right, widths.bottom),
        border.radius.bottom_right,
        border.radius.shape_bottom_right,
        shape_offset_br,
        inset_br,
        BorderSegment::BottomRight,
        EdgeMask::BOTTOM | EdgeMask::RIGHT,
        rect.bottom_left(),
        border.radius.bottom_left,
        rect.top_right(),
        border.radius.top_right,
        border.do_aa,
        segment_cb,
    );
    add_corner_segment(
        LayoutRect::from_floats(
            rect.min.x,
            rect.min.y + rect.height() - local_size_bl.height,
            rect.min.x + local_size_bl.width,
            rect.min.y + rect.height(),
        ),
        LayoutRect::from_floats(
            rect.min.x,
            clip_split_y_from_min,
            clip_split_x_from_max,
            rect.max.y,
        ),
        border.bottom,
        border.left,
        LayoutSize::new(widths.left, widths.bottom),
        border.radius.bottom_left,
        border.radius.shape_bottom_left,
        shape_offset_bl,
        inset_bl,
        BorderSegment::BottomLeft,
        EdgeMask::BOTTOM | EdgeMask::LEFT,
        rect.max,
        border.radius.bottom_right,
        rect.min,
        border.radius.top_left,
        border.do_aa,
        segment_cb,
    );
}

fn add_segment(
    task_rect: DeviceRect,
    style0: BorderStyle,
    style1: BorderStyle,
    color0: ColorF,
    color1: ColorF,
    segment: BorderSegment,
    instances: &mut Vec<BorderInstance>,
    widths: DeviceSize,
    radius: DeviceSize,
    shape: f32,
    shape_offset: DeviceSize,
    inset: DeviceSize,
    do_aa: bool,
    h_adjacent_corner_outer: DevicePoint,
    h_adjacent_corner_radius: DeviceSize,
    v_adjacent_corner_outer: DevicePoint,
    v_adjacent_corner_radius: DeviceSize,
    gpu_buffer_builder: &mut GpuBufferBuilderF,
) {
    let superellipse = shape != 1.0;

    let base_flags = (segment as i32) |
                     ((style0 as i32) << 8) |
                     ((style1 as i32) << 16) |
                     ((do_aa as i32) << 28) |
                     ((superellipse as i32) << 29);

    let instance_gpu_data = BorderInstanceGpuData {
        local_rect: task_rect,
        color0: color0.premultiplied(),
        color1: color1.premultiplied(),
        widths,
        radius,
        shape,
        shape_offset,
        inset,
    };

    let base_instance = BorderInstance {
        task_origin: DevicePoint::zero(),
        flags: base_flags,
        clip_params: [0.0; 8],
        gpu_data_address: instance_gpu_data.write(superellipse, gpu_buffer_builder)
    };

    match segment {
        BorderSegment::TopLeft |
        BorderSegment::TopRight |
        BorderSegment::BottomLeft |
        BorderSegment::BottomRight => {
            // TODO(gw): Similarly to the old border code, we don't correctly handle a a corner
            //           that is dashed on one edge, and dotted on another. We can handle this
            //           in the future by submitting two instances, each one with one side
            //           color set to have an alpha of 0.
            if (style0 == BorderStyle::Dotted && style1 == BorderStyle::Dashed) ||
               (style0 == BorderStyle::Dashed && style0 == BorderStyle::Dotted) {
                warn!("TODO: Handle a corner with dotted / dashed transition.");
            }

            let dashed_or_dotted_corner = match style0 {
                BorderStyle::Dashed => {
                    write_dashed_corner_instances(
                        radius,
                        widths,
                        segment,
                        &base_instance,
                        instances,
                    )
                }
                BorderStyle::Dotted => {
                    write_dotted_corner_instances(
                        radius,
                        widths,
                        segment,
                        &base_instance,
                        instances,
                    )
                }
                _ => Err(()),
            };

            if dashed_or_dotted_corner.is_err() {
                let clip_params = [
                    h_adjacent_corner_outer.x,
                    h_adjacent_corner_outer.y,
                    h_adjacent_corner_radius.width,
                    h_adjacent_corner_radius.height,
                    v_adjacent_corner_outer.x,
                    v_adjacent_corner_outer.y,
                    v_adjacent_corner_radius.width,
                    v_adjacent_corner_radius.height,
                ];

                instances.push(BorderInstance {
                    clip_params,
                    ..base_instance
                });
            }
        }
        BorderSegment::Top |
        BorderSegment::Bottom |
        BorderSegment::Right |
        BorderSegment::Left => {
            let is_vertical = segment == BorderSegment::Left ||
                              segment == BorderSegment::Right;

            match style0 {
                BorderStyle::Dashed => {
                    let (x, y) = if is_vertical {
                        let half_dash_size = task_rect.height() * 0.25;
                        (0., half_dash_size)
                    } else {
                        let half_dash_size = task_rect.width() * 0.25;
                        (half_dash_size, 0.)
                    };

                    instances.push(BorderInstance {
                        flags: base_flags | ((BorderClipKind::DashEdge as i32) << 24),
                        clip_params: [
                            x, y, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                        ],
                        ..base_instance
                    });
                }
                BorderStyle::Dotted => {
                    let width = if is_vertical { widths.width } else { widths.height };
                    let r = width * 0.5;

                    // Like Gecko, draw dots no wider than two device pixels as
                    // squares, placed on the pixel grid so that each one is
                    // fully covered rather than an antialiased circle.
                    let is_square = width <= MAX_SQUARE_DOT_WIDTH;
                    let along = if is_square {
                        (width * 0.5).round() + r
                    } else {
                        width
                    };

                    let (x, y) = if is_vertical {
                        (r, along)
                    } else {
                        (along, r)
                    };

                    instances.push(BorderInstance {
                        flags: base_flags | ((BorderClipKind::Dot as i32) << 24),
                        clip_params: [
                            x, y, r, is_square as i32 as f32, 0.0, 0.0, 0.0, 0.0,
                        ],
                        ..base_instance
                    });
                }
                _ => {
                    instances.push(base_instance);
                }
            }
        }
    }
}

/// Add a corner segment (if valid) to the list of
/// border segments for this primitive.
fn add_corner_segment(
    image_rect: LayoutRect,
    non_overlapping_rect: LayoutRect,
    side0: BorderSide,
    side1: BorderSide,
    widths: LayoutSize,
    radius: LayoutSize,
    shape: f32,
    shape_offset: LayoutSize,
    inset: LayoutSize,
    segment: BorderSegment,
    edge_flags: EdgeMask,
    h_adjacent_corner_outer: LayoutPoint,
    h_adjacent_corner_radius: LayoutSize,
    v_adjacent_corner_outer: LayoutPoint,
    v_adjacent_corner_radius: LayoutSize,
    do_aa: bool,
    segment_cb: &mut dyn FnMut(&NormalBorderSegment),
) {
    if side0.color.a <= 0.0 && side1.color.a <= 0.0 {
        return;
    }

    if widths.width <= 0.0 && widths.height <= 0.0 {
        return;
    }

    if side0.style.is_hidden() && side1.style.is_hidden() {
        return;
    }

    let segment_rect = match image_rect.intersection(&non_overlapping_rect) {
        Some(rect) => rect,
        None => {
            return;
        }
    };

    // If the radii of the adjacent corners do not overlap with this segment,
    // then set the outer position to this segment's corner and the radii to zero.
    // That way the cache key is unaffected by non-overlapping corners, resulting
    // in fewer misses.
    let (h_corner_outer, h_corner_radius) = match segment {
        BorderSegment::TopLeft => {
            if h_adjacent_corner_outer.x - h_adjacent_corner_radius.width < image_rect.max.x {
                (h_adjacent_corner_outer, h_adjacent_corner_radius)
            } else {
                (LayoutPoint::new(image_rect.max.x, image_rect.min.y), LayoutSize::zero())
            }
        }
        BorderSegment::TopRight => {
            if h_adjacent_corner_outer.x + h_adjacent_corner_radius.width > image_rect.min.x {
                (h_adjacent_corner_outer, h_adjacent_corner_radius)
            } else {
                (LayoutPoint::new(image_rect.min.x, image_rect.min.y), LayoutSize::zero())
            }
        }
        BorderSegment::BottomRight => {
            if h_adjacent_corner_outer.x + h_adjacent_corner_radius.width > image_rect.min.x {
                (h_adjacent_corner_outer, h_adjacent_corner_radius)
            } else {
                (LayoutPoint::new(image_rect.min.x, image_rect.max.y), LayoutSize::zero())
            }
        }
        BorderSegment::BottomLeft => {
            if h_adjacent_corner_outer.x - h_adjacent_corner_radius.width < image_rect.max.x {
                (h_adjacent_corner_outer, h_adjacent_corner_radius)
            } else {
                (image_rect.max, LayoutSize::zero())
            }
        }
        _ => unreachable!()
    };

    let (v_corner_outer, v_corner_radius) = match segment {
        BorderSegment::TopLeft => {
            if v_adjacent_corner_outer.y - v_adjacent_corner_radius.height < image_rect.max.y {
                (v_adjacent_corner_outer, v_adjacent_corner_radius)
            } else {
                (LayoutPoint::new(image_rect.min.x, image_rect.max.y), LayoutSize::zero())
            }
        }
        BorderSegment::TopRight => {
            if v_adjacent_corner_outer.y - v_adjacent_corner_radius.height < image_rect.max.y {
                (v_adjacent_corner_outer, v_adjacent_corner_radius)
            } else {
                (image_rect.max, LayoutSize::zero())
            }
        }
        BorderSegment::BottomRight => {
            if v_adjacent_corner_outer.y + v_adjacent_corner_radius.height > image_rect.min.y {
                (v_adjacent_corner_outer, v_adjacent_corner_radius)
            } else {
                (LayoutPoint::new(image_rect.max.x, image_rect.min.y), LayoutSize::zero())
            }
        }
        BorderSegment::BottomLeft => {
            if v_adjacent_corner_outer.y + v_adjacent_corner_radius.height > image_rect.min.y {
                (v_adjacent_corner_outer, v_adjacent_corner_radius)
            } else {
                (LayoutPoint::new(image_rect.min.x, image_rect.min.y), LayoutSize::zero())
            }
        }
        _ => unreachable!()
    };

    segment_cb(&NormalBorderSegment {
        pattern_rect: image_rect,
        clip_rect: Some(segment_rect),
        repeat_x: RepeatMode::Stretch,
        repeat_y: RepeatMode::Stretch,
        edge_flags,
        task_size: image_rect.size(),
        cache_key: BorderSegmentCacheKey {
            do_aa,
            side0: side0.into(),
            side1: side1.into(),
            segment,
            radius: radius.to_au(),
            shape: shape.to_bits(),
            shape_offset: shape_offset.to_au(),
            inset: inset.to_au(),
            size: widths.to_au(),
            h_adjacent_corner_outer: (h_corner_outer - image_rect.min).to_point().to_au(),
            h_adjacent_corner_radius: h_corner_radius.to_au(),
            v_adjacent_corner_outer: (v_corner_outer - image_rect.min).to_point().to_au(),
            v_adjacent_corner_radius: v_corner_radius.to_au(),
        },
        is_solid: None,
    });
}

/// Add an edge segment (if valid) to the list of
/// border segments for this primitive.
fn add_edge_segment(
    image_rect: LayoutRect,
    edge_info: &EdgeInfo,
    side: BorderSide,
    width: f32,
    segment: BorderSegment,
    edge_flags: EdgeMask,
    do_aa: bool,
    segment_cb: &mut dyn FnMut(&NormalBorderSegment),
) {
    if side.color.a <= 0.0 {
        return;
    }

    if side.style.is_hidden() {
        return;
    }

    let along_edge = if is_uniform_along_edge(side.style) {
        RepeatMode::Stretch
    } else {
        RepeatMode::Repeat
    };

    let (size, repeat_x, repeat_y) = match segment {
        BorderSegment::Left | BorderSegment::Right => {
            (LayoutSize::new(width, edge_info.stretch_size), RepeatMode::Stretch, along_edge)
        }
        BorderSegment::Top | BorderSegment::Bottom => {
            (LayoutSize::new(edge_info.stretch_size, width), along_edge, RepeatMode::Stretch)
        }
        _ => {
            unreachable!();
        }
    };

    if image_rect.width() <= 0. || image_rect.height() <= 0. {
        return;
    }

    let is_solid = if side.style == BorderStyle::Solid {
        Some(side.color)
    } else {
        None
    };

    segment_cb(&NormalBorderSegment {
        pattern_rect: image_rect,
        clip_rect: None,
        repeat_x,
        repeat_y,
        edge_flags,
        task_size: size, // TODO: double-check
        cache_key: BorderSegmentCacheKey {
            do_aa,
            side0: side.into(),
            side1: side.into(),
            radius: LayoutSizeAu::zero(),
            shape: 1.0f32.to_bits(),
            shape_offset: LayoutSizeAu::zero(),
            inset: LayoutSizeAu::zero(),
            size: size.to_au(),
            segment,
            h_adjacent_corner_outer: LayoutPointAu::zero(),
            h_adjacent_corner_radius: LayoutSizeAu::zero(),
            v_adjacent_corner_outer: LayoutPointAu::zero(),
            v_adjacent_corner_radius: LayoutSizeAu::zero(),
        },
        is_solid,
    });
}

/// Build the set of border instances needed to draw a border
/// segment into the render task cache.
pub fn build_border_instances(
    cache_key: &BorderSegmentCacheKey,
    cache_size: DeviceIntSize,
    border: &ApiNormalBorder,
    scale: LayoutToDeviceScale,
    gpu_buffer_builder: &mut GpuBufferBuilderF,
) -> Vec<BorderInstance> {
    let mut instances = Vec::new();

    let (side0, side1, flip0, flip1) = match cache_key.segment {
        BorderSegment::Left => (&border.left, &border.left, false, false),
        BorderSegment::Top => (&border.top, &border.top, false, false),
        BorderSegment::Right => (&border.right, &border.right, true, true),
        BorderSegment::Bottom => (&border.bottom, &border.bottom, true, true),
        BorderSegment::TopLeft => (&border.left, &border.top, false, false),
        BorderSegment::TopRight => (&border.top, &border.right, false, true),
        BorderSegment::BottomRight => (&border.right, &border.bottom, true, true),
        BorderSegment::BottomLeft => (&border.bottom, &border.left, true, false),
    };

    let style0 = if side0.style.is_hidden() {
        side1.style
    } else {
        side0.style
    };
    let style1 = if side1.style.is_hidden() {
        side0.style
    } else {
        side1.style
    };

    let color0 = side0.border_color(flip0);
    let color1 = side1.border_color(flip1);

    // The corner box is a whole number of device pixels (see `snap_radius` in
    // `prim_store::borders`), so the geometry below is representable in the task
    // exactly. Rounding it up here instead would draw the arc with a radius and
    // thickness of up to a whole pixel more than was asked for. A hairline side
    // still gets a texel of its own so it can't drop out of the cached arc
    // entirely, but a side with no width keeps none.
    let side_texels = |v: f32| if v > 0.0 { (v * scale.0).max(1.0) } else { 0.0 };
    let size = LayoutSize::from_au(cache_key.size);
    let widths = DeviceSize::new(side_texels(size.width), side_texels(size.height));
    let radius = LayoutSize::from_au(cache_key.radius) * scale;
    let shape = f32::from_bits(cache_key.shape);
    let shape_offset = LayoutSize::from_au(cache_key.shape_offset) * scale;
    let inset = LayoutSize::from_au(cache_key.inset) * scale;

    let h_corner_outer = (LayoutPoint::from_au(cache_key.h_adjacent_corner_outer) * scale).round();
    let h_corner_radius = (LayoutSize::from_au(cache_key.h_adjacent_corner_radius) * scale).ceil();
    let v_corner_outer = (LayoutPoint::from_au(cache_key.v_adjacent_corner_outer) * scale).round();
    let v_corner_radius = (LayoutSize::from_au(cache_key.v_adjacent_corner_radius) * scale).ceil();

    let shape_offset = if shape < 1.0 {
        radius.max(widths) + shape_offset
    } else {
        DeviceSize::zero()
    };

    add_segment(
        DeviceRect::from_size(cache_size.to_f32()),
        style0,
        style1,
        color0,
        color1,
        cache_key.segment,
        &mut instances,
        widths,
        radius,
        shape,
        shape_offset,
        inset,
        border.do_aa,
        h_corner_outer,
        h_corner_radius,
        v_corner_outer,
        v_corner_radius,
        gpu_buffer_builder,
    );

    instances
}

/// WebRender-side behavior for the (api-resident) `NinePatchDescriptor`. The
/// data lives in `webrender_api` so interning keys can reference it; the
/// nine-patch segmentation logic stays here as an extension trait.
pub trait NinePatchDescriptorExt {
    fn for_each_segment(
        &self,
        rect: &LayoutRect,
        add_segment: &mut dyn FnMut(&LayoutRect, &TexelRect, EdgeMask, RepeatMode, RepeatMode),
    );
}

impl NinePatchDescriptorExt for NinePatchDescriptor {
    fn for_each_segment(
        &self,
        rect: &LayoutRect,
        add_segment: &mut dyn FnMut(
            &LayoutRect, // dst rect
            &TexelRect,  // src rect
            EdgeMask,    // segment side
            RepeatMode,  // horizontal
            RepeatMode,  // vertical
        ),
    ) {
        // Calculate the local texel coords of the slices.
        let px0 = 0.0;
        let px1 = self.slice.left as f32 / self.width as f32;
        let px2 = (self.width as f32 - self.slice.right as f32) / self.width as f32;
        let px3 = 1.0;

        let py0 = 0.0;
        let py1 = self.slice.top as f32 / self.height as f32;
        let py2 = (self.height as f32 - self.slice.bottom as f32) / self.height as f32;
        let py3 = 1.0;

        let tl_outer = LayoutPoint::new(rect.min.x, rect.min.y);
        let tl_inner = tl_outer + vec2(self.widths.left, self.widths.top);

        let tr_outer = LayoutPoint::new(rect.min.x + rect.width(), rect.min.y);
        let tr_inner = tr_outer + vec2(-self.widths.right, self.widths.top);

        let bl_outer = LayoutPoint::new(rect.min.x, rect.min.y + rect.height());
        let bl_inner = bl_outer + vec2(self.widths.left, -self.widths.bottom);

        let br_outer = rect.max;

        let br_inner = br_outer - vec2(self.widths.right, self.widths.bottom);

        // Top left
        let top_left_src = TexelRect::new(px0, py0, px1, py1);
        if !top_left_src.is_empty() {
            add_segment(
                &LayoutRect::from_floats(tl_outer.x, tl_outer.y, tl_inner.x, tl_inner.y),
                &top_left_src,
                EdgeMask::TOP | EdgeMask::LEFT,
                RepeatMode::Stretch,
                RepeatMode::Stretch,
            );
        }

        // Top right
        let top_right_src = TexelRect::new(px2, py0, px3, py1);
        if !top_right_src.is_empty() {
            add_segment(
                &LayoutRect::from_floats(tr_inner.x, tr_outer.y, tr_outer.x, tr_inner.y),
                &top_right_src,
                EdgeMask::TOP | EdgeMask::RIGHT,
                RepeatMode::Stretch,
                RepeatMode::Stretch,
            );
        }

        // Bottom right
        let bottom_right_src = TexelRect::new(px2, py2, px3, py3);
        if !bottom_right_src.is_empty() {
            add_segment(
                &LayoutRect::from_floats(br_inner.x, br_inner.y, br_outer.x, br_outer.y),
                &bottom_right_src,
                EdgeMask::BOTTOM | EdgeMask::RIGHT,
                RepeatMode::Stretch,
                RepeatMode::Stretch,
            );
        }

        // Bottom left
        let bottom_left_src = TexelRect::new(px0, py2, px1, py3);
        if !bottom_left_src.is_empty() {
            add_segment(
                &LayoutRect::from_floats(bl_outer.x, bl_inner.y, bl_inner.x, bl_outer.y),
                &bottom_left_src,
                EdgeMask::BOTTOM | EdgeMask::LEFT,
                RepeatMode::Stretch,
                RepeatMode::Stretch,
            );
        }

        // Center
        let center_src = TexelRect::new(px1, py1, px2, py2);
        if self.fill && !center_src.is_empty() {
            add_segment(
                &LayoutRect::from_floats(tl_inner.x, tl_inner.y, tr_inner.x, bl_inner.y),
                &center_src,
                EdgeMask::empty(),
                self.repeat_horizontal,
                self.repeat_vertical,
            );
        }

        // Add edge segments.

        // Top
        let top_src = TexelRect::new(px1, py0, px2, py1);
        if !top_src.is_empty() {
            add_segment(
                &LayoutRect::from_floats(tl_inner.x, tl_outer.y, tr_inner.x, tl_inner.y),
                &top_src,
                EdgeMask::TOP,
                self.repeat_horizontal,
                RepeatMode::Stretch,
            );
        }

        // Bottom
        let bottom_src = TexelRect::new(px1, py2, px2, py3);
        if !bottom_src.is_empty() {
            add_segment(
                &LayoutRect::from_floats(bl_inner.x, bl_inner.y, br_inner.x, bl_outer.y),
                &bottom_src,
                EdgeMask::BOTTOM,
                self.repeat_horizontal,
                RepeatMode::Stretch,
            );
        }

        // Left
        let left_src = TexelRect::new(px0, py1, px1, py2);
        if !left_src.is_empty() {
            add_segment(
                &LayoutRect::from_floats(tl_outer.x, tl_inner.y, tl_inner.x, bl_inner.y),
                &left_src,
                EdgeMask::LEFT,
                RepeatMode::Stretch,
                self.repeat_vertical,
            );
        }

        // Right
        let right_src = TexelRect::new(px2, py1, px3, py2);
        if !right_src.is_empty() {
            add_segment(
                &LayoutRect::from_floats(tr_inner.x, tr_inner.y, br_outer.x, br_inner.y),
                &right_src,
                EdgeMask::RIGHT,
                RepeatMode::Stretch,
                self.repeat_vertical,
            );
        }
    }
}

// Computes the stretch-size of a repeated pattern along a border sgment,
// given the segment size and the size of the source pattern.
pub fn compute_border_repetition(
    segment_size: LayoutSize,
    src_size: DeviceSize,
    repeat_x: RepeatMode,
    repeat_y: RepeatMode,
    stretch_size: &mut LayoutSize,
    spacing: &mut LayoutSize,
    offset: &mut LayoutVector2D,
) {
    use euclid::size2;

    compute_border_repetition_1d(
        segment_size,
        src_size,
        repeat_x,
        &mut stretch_size.width,
        &mut spacing.width,
        &mut offset.x,
    );

    compute_border_repetition_1d(
        size2(segment_size.height, segment_size.width),
        size2(src_size.height, src_size.width),
        repeat_y,
        &mut stretch_size.height,
        &mut spacing.height,
        &mut offset.y,
    );
}

pub fn compute_border_repetition_1d(
    segment_size: LayoutSize,
    src_size: DeviceSize,
    repeat_mode: RepeatMode,
    out_stretch_size: &mut f32,
    out_spacing: &mut f32,
    out_offset: &mut f32,
) {
    *out_spacing = 0.0;
    *out_offset = 0.0;

    let mut stretch_size;
    if repeat_mode == RepeatMode::Stretch {
        stretch_size = segment_size.width;
    } else {
        let xy_ratio = src_size.width / src_size.height;
        // Maintain the aspect ratio of the source pattern.
        stretch_size = segment_size.height * xy_ratio;

        let repetitions = (segment_size.width / stretch_size).floor().max(1.0);
        let remaining_space = (segment_size.width - stretch_size * repetitions).max(0.0);

        if repeat_mode == RepeatMode::Round {
            // Rescale the pattern so that the nearest whole number of
            // repetitions fills the segment exactly. Unlike Repeat and Space,
            // Round rounds to the closest integer rather than truncating.
            let round_repetitions = (segment_size.width / stretch_size).round().max(1.0);
            stretch_size = segment_size.width / round_repetitions
        }

        if repeat_mode == RepeatMode::Space {
            // Maintain an integer number of repetitions using some space
            // between them.
            *out_spacing = remaining_space / (repetitions - 1.0).max(1.0);
        }


        if repeat_mode == RepeatMode::Repeat {
            // Center the tiled pattern so that a tile is centered on the
            // segment's midpoint (matching the CSS "repeat" behavior). We move
            // the tiling origin back to the tile boundary at or before the
            // start of the segment; the caller enlarges the local rect to
            // include that partial repetition and clips it back to the segment.
            // Reducing modulo stretch_size keeps the offset in
            // (-stretch_size, 0] so we don't emit unnecessary repetitions.
            let half_overflow = (segment_size.width - stretch_size) * 0.5;
            *out_offset = half_overflow - (half_overflow / stretch_size).ceil() * stretch_size;
        }
    }

    *out_stretch_size = stretch_size;
}

