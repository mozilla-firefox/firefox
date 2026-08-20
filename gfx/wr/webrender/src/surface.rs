/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Contains functionality to help building the render task graph from a series of off-screen
//! surfaces that are created during the prepare pass, and other surface related types and
//! helpers.

use api::units::*;
use crate::box_shadow::BLUR_SAMPLE_SCALE;
use crate::command_buffer::{CommandBufferBuilderKind, CommandBufferList, CommandBufferBuilder, CommandBufferIndex};
use crate::internal_types::{FastHashMap, Filter};
use crate::picture_composite_mode::PictureCompositeMode;
use crate::tile_cache::{TileKey, SubSliceIndex, MAX_COMPOSITOR_SURFACES};
use crate::prim_store::PictureIndex;
use crate::render_task_graph::{RenderTaskId, RenderTaskGraphBuilder};
use crate::render_target::ResolveOp;
use crate::render_task::{RenderTask, RenderTaskKind, RenderTaskLocation};
use crate::space::SpaceMapper;
use crate::spatial_tree::{CoordinateSpaceMapping, SpatialTree, SpatialNodeIndex};
use crate::util::{MaxRect, ScaleOffset};
use crate::visibility::{DrawState, PrimitiveDrawHeader, FrameVisibilityContext};
pub use crate::picture_composite_mode::get_surface_rects;

/// Walk the filter chain rooted at `task_id` and make every task in it that
/// samples `src_task_id` depend on `dep_task_id` as well.
///
/// The tasks that sample the chain's source sit at the *start* of the chain, not
/// at the root (which is its output), so making the root alone depend on
/// `dep_task_id` is not enough. How many there are depends on the chain:
///  - Blur: one, the vertical blur - or the first downscale, for a blur large
///    enough that `new_blur` scales it down first.
///  - Drop-shadow: one. Every shadow blurs from the same source task, but only
///    the last chain is reachable from the root, and all of the shadow quads
///    sample that one task.
///  - SVG filter graph: potentially several, since any node in the graph may
///    take SourceGraphic as an input.
///
/// Filter chains are small and acyclic, so a plain recursive walk is enough.
fn order_readers_after(
    rg_builder: &mut RenderTaskGraphBuilder,
    task_id: RenderTaskId,
    src_task_id: RenderTaskId,
    dep_task_id: RenderTaskId,
) {
    let children = rg_builder.get_task(task_id).children.clone();

    if children.contains(&src_task_id) {
        rg_builder.add_dependency(task_id, dep_task_id);
    }

    for child_id in children {
        if child_id != src_task_id {
            order_readers_after(rg_builder, child_id, src_task_id, dep_task_id);
        }
    }
}

/// Fetch the raster spatial node of a picture render task (used to relate the
/// raster spaces of a resolve target and the surface(s) it reads back from).
fn raster_spatial_node(
    rg_builder: &RenderTaskGraphBuilder,
    task_id: RenderTaskId,
) -> SpatialNodeIndex {
    match rg_builder.get_task(task_id).kind {
        RenderTaskKind::Picture(ref info) => info.raster_spatial_node_index,
        _ => unreachable!("bug: resolve src/dest task is not a picture"),
    }
}

/// Compute the mapping from a resolve target's raster space into the raster
/// space of the surface(s) it reads back from, for use by `handle_resolve`.
///
/// A resolve target (backdrop-filter sub-graph) and the surface it captures
/// always share a surface spatial node: `finalize_picture` resolves the filter
/// picture's spatial node to its backdrop root. They differ only in their raster
/// root, and only when the resolve target promotes to a root-snapping raster
/// root (the root reference frame) while the parent rasterizes against its own
/// node (e.g. a scrolling tile cache). Both nodes are then in the root
/// coordinate system, so the relationship is always a `ScaleOffset` (the
/// identity when the raster roots coincide); it can never be a non-axis-aligned
/// `Transform`, because a resolve target under a non-root coordinate system does
/// not promote and shares its parent's raster node.
fn resolve_dest_to_src_raster(
    rg_builder: &RenderTaskGraphBuilder,
    spatial_tree: &SpatialTree,
    dest_task_id: RenderTaskId,
    src_task_ids: &[RenderTaskId],
) -> ScaleOffset {
    // All src tasks are tiles of the same parent surface, so they share a raster
    // node; the first is representative.
    let Some(&first_src) = src_task_ids.first() else {
        return ScaleOffset::identity();
    };

    let dest_raster = raster_spatial_node(rg_builder, dest_task_id);
    let src_raster = raster_spatial_node(rg_builder, first_src);

    if src_raster == dest_raster {
        return ScaleOffset::identity();
    }

    match spatial_tree.get_relative_transform(dest_raster, src_raster) {
        CoordinateSpaceMapping::ScaleOffset(scale_offset) => scale_offset,
        // Distinct nodes with an identity relationship: no correction needed.
        CoordinateSpaceMapping::Local => ScaleOffset::identity(),
        CoordinateSpaceMapping::Transform(..) => {
            // Unreachable given the shared-coordinate-system invariant above; a
            // rect-to-rect copy can't express a rotation, so degrade to the old
            // (uncorrected) behaviour rather than crash a release build.
            debug_assert!(
                false,
                "resolve target and its backdrop source must share the root coordinate system",
            );
            ScaleOffset::identity()
        }
    }
}

/// Maximum blur radius for blur filter
const MAX_BLUR_RADIUS: f32 = 100.;

/// An index into the surface array
#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct SurfaceIndex(pub usize);

/// Specify whether a surface allows subpixel AA text rendering.
#[derive(Debug, Copy, Clone)]
pub enum SubpixelMode {
    /// This surface allows subpixel AA text
    Allow,
    /// Subpixel AA text cannot be drawn on this surface
    Deny,
    /// Subpixel AA can be drawn on this surface, if not intersecting
    /// with the excluded regions, and inside the allowed rect.
    Conditional {
        allowed_rect: PictureRect,
        prohibited_rect: PictureRect,
    },
}

/// Information about an offscreen surface. For now,
/// it contains information about the size and coordinate
/// system of the surface. In the future, it will contain
/// information about the contents of the surface, which
/// will allow surfaces to be cached / retained between
/// frames and display lists.
pub struct SurfaceInfo {
    /// A local rect defining the size of this surface, in the
    /// coordinate system of the parent surface. This contains
    /// the unclipped bounding rect of child primitives.
    ///
    /// SNAPTODO: This rect is built by mapping per-cluster bounding
    /// rects (and child-surface coverage rects) into this surface's
    /// picture space via `map_local_to_picture`. Even once the source
    /// cluster bound is a true union of per-prim *snapped* local
    /// rects, the resulting `unclipped_local_rect` is not guaranteed
    /// to be snapped: any 2D transform that isn't an axis-aligned,
    /// integer-pixel translation between the cluster/child-surface
    /// spatial node and this surface's spatial node will produce
    /// sub-pixel edges in picture space. Float blur-margin inflation
    /// inside `composite_mode.get_coverage` can also break the snap.
    /// Consumers that need a snapped value will either need to
    /// re-snap in surface space or restrict the snap path to surfaces
    /// where the cross-space mapping preserves grid alignment (see
    /// `SurfaceInfo.allow_snapping`).
    pub unclipped_local_rect: PictureRect,
    /// The local space coverage of child primitives after they are
    /// are clipped to their owning clip-chain.
    pub clipped_local_rect: PictureRect,
    /// The (conservative) valid part of this surface rect. Used
    /// to reduce the size of render target allocation.
    pub clipping_rect: PictureRect,
    /// The rectangle to use for culling and clipping.
    pub culling_rect: VisRect,
    /// Helper structs for mapping local rects in different
    /// coordinate systems into the picture coordinates.
    pub map_local_to_picture: SpaceMapper<LayoutPixel, PicturePixel>,
    /// The positioning node for the surface itself,
    pub surface_spatial_node_index: SpatialNodeIndex,
    /// The rasterization root for this surface.
    pub raster_spatial_node_index: SpatialNodeIndex,
    /// The spatial node for culling and clipping (anything using VisPixel).
    /// TODO: Replace with the raster spatial node.
    pub visibility_spatial_node_index: SpatialNodeIndex,
    /// The device pixel ratio specific to this surface.
    pub device_pixel_scale: DevicePixelScale,
    /// The scale factors of the surface to world transform.
    pub world_scale_factors: (f32, f32),
    /// Local scale factors surface to raster transform
    pub local_scale: (f32, f32),
    /// If true, we know this surface is completely opaque.
    pub is_opaque: bool,
    /// Whether content rasterized into this surface is snapped to the device
    /// pixel grid at frame time. True for tile caches (snapped against the
    /// scroll-stable cache node) and root-snapping surfaces (raster node is
    /// root). False for a non-snapping raster root (preserve-3d / perspective /
    /// huge-scale, `enable_snapping == false`): snapping against its own scaled
    /// node would use only the tiny local scale and collapse content to zero,
    /// so content is left unsnapped there instead.
    pub allow_snapping: bool,
    /// If true, the scissor rect must be set when drawing this surface
    pub force_scissor_rect: bool,
    /// For an SVGFEGraph surface, the mapping from the space the filter
    /// subregions are authored in (the filtered element's spatial node) to this
    /// surface's spatial node. Non-identity for backdrop filters, whose graph
    /// composites in backdrop-root space; it is a full scale+offset because an
    /// intervening reference frame may scale (e.g. pdf.js scales its text
    /// spans), so a translation alone is not enough. All SVGFE coverage paths
    /// map the subregions through this so they line up with the geometry.
    pub svgfe_source_map: ScaleOffset,
}

impl SurfaceInfo {
    pub fn new(
        surface_spatial_node_index: SpatialNodeIndex,
        raster_spatial_node_index: SpatialNodeIndex,
        global_culling_rect: DeviceRect,
        spatial_tree: &SpatialTree,
        device_pixel_scale: DevicePixelScale,
        world_scale_factors: (f32, f32),
        local_scale: (f32, f32),
        allow_snapping: bool,
        force_scissor_rect: bool,
    ) -> Self {
        let map_surface_to_root = SpaceMapper::new_with_target(
            spatial_tree.root_reference_frame_index(),
            surface_spatial_node_index,
            global_culling_rect,
            spatial_tree,
        );

        let pic_bounds = map_surface_to_root
            .unmap(&map_surface_to_root.bounds)
            .unwrap_or_else(PictureRect::max_rect);

        let map_local_to_picture = SpaceMapper::new(
            surface_spatial_node_index,
            pic_bounds,
        );

        // TODO: replace the root with raster space.
        let visibility_spatial_node_index = spatial_tree.root_reference_frame_index();

        SurfaceInfo {
            unclipped_local_rect: PictureRect::zero(),
            clipped_local_rect: PictureRect::zero(),
            is_opaque: false,
            clipping_rect: PictureRect::zero(),
            map_local_to_picture,
            raster_spatial_node_index,
            surface_spatial_node_index,
            visibility_spatial_node_index,
            device_pixel_scale,
            world_scale_factors,
            local_scale,
            allow_snapping,
            force_scissor_rect,
            svgfe_source_map: ScaleOffset::identity(),
            // TODO: At the moment all culling is done in the root device space but
            // but the plan is to move it to raster space.
            culling_rect: global_culling_rect.cast_unit(),
        }
    }

    /// Clamps the blur radius depending on scale factors.
    pub fn clamp_blur_radius(
        &self,
        x_blur_radius: f32,
        y_blur_radius: f32,
    ) -> (f32, f32) {
        // Clamping must occur after scale factors are applied, but scale factors are not applied
        // until later on. To clamp the blur radius, we first apply the scale factors and then clamp
        // and finally revert the scale factors.

        let sx_blur_radius = x_blur_radius * self.local_scale.0;
        let sy_blur_radius = y_blur_radius * self.local_scale.1;

        let largest_scaled_blur_radius = f32::max(
            sx_blur_radius * self.world_scale_factors.0,
            sy_blur_radius * self.world_scale_factors.1,
        );

        if largest_scaled_blur_radius > MAX_BLUR_RADIUS {
            let sf = MAX_BLUR_RADIUS / largest_scaled_blur_radius;
            (x_blur_radius * sf, y_blur_radius * sf)
        } else {
            // Return the original blur radius to avoid any rounding errors
            (x_blur_radius, y_blur_radius)
        }
    }

    pub fn update_culling_rect(
        &mut self,
        parent_culling_rect: VisRect,
        composite_mode: &PictureCompositeMode,
        frame_context: &FrameVisibilityContext,
    ) {
        // Set the default culling rect to be the parent, in case we fail
        // any mappings below due to weird perspective or invalid transforms.
        self.culling_rect = parent_culling_rect;

        if let PictureCompositeMode::Filter(Filter::Blur { width, height, should_inflate, .. }) = composite_mode {
            if *should_inflate {
                // Space mapping vis <-> picture space
                let map_surface_to_vis = SpaceMapper::new_with_target(
                    // TODO: switch from root to raster space.
                    frame_context.root_spatial_node_index,
                    self.surface_spatial_node_index,
                    parent_culling_rect,
                    frame_context.spatial_tree,
                );

                // Unmap the parent culling rect to surface space. Note that this may be
                // quite conservative in the case of a complex transform, especially perspective.
                if let Some(local_parent_culling_rect) = map_surface_to_vis.unmap(&parent_culling_rect) {
                    let (width_factor, height_factor) = self.clamp_blur_radius(*width, *height);

                    // Inflate by the local-space amount this surface extends.
                    let expanded_rect: PictureBox2D = local_parent_culling_rect.inflate(
                        width_factor.ceil() * BLUR_SAMPLE_SCALE,
                        height_factor.ceil() * BLUR_SAMPLE_SCALE,
                    );

                    // Map back to the expected vis-space culling rect
                    if let Some(rect) = map_surface_to_vis.map(&expanded_rect) {
                        self.culling_rect = rect;
                    }
                }
            }
        }
    }

    pub fn map_to_device_rect(
        &self,
        picture_rect: &PictureRect,
        spatial_tree: &SpatialTree,
    ) -> DeviceRect {
        let raster_rect = if self.raster_spatial_node_index != self.surface_spatial_node_index {
            // Currently, the surface's spatial node can be different from its raster node only
            // for surfaces in the root coordinate system for snapping reasons.
            // See `PictureInstance::assign_surface`.
            assert_eq!(self.device_pixel_scale.0, 1.0);
            assert_eq!(self.raster_spatial_node_index, spatial_tree.root_reference_frame_index());

            let pic_to_raster = SpaceMapper::new_with_target(
                self.raster_spatial_node_index,
                self.surface_spatial_node_index,
                WorldRect::max_rect(),
                spatial_tree,
            );

            pic_to_raster.map(&picture_rect).unwrap()
        } else {
            picture_rect.cast_unit()
        };

        raster_rect * self.device_pixel_scale
    }

    /// Clip and transform a local rect to a device rect suitable for allocating
    /// a child off-screen surface of this surface (e.g. for clip-masks)
    pub fn get_surface_rect(
        &self,
        local_rect: &PictureRect,
        spatial_tree: &SpatialTree,
    ) -> Option<DeviceIntRect> {
        let local_rect = match local_rect.intersection(&self.clipping_rect) {
            Some(rect) => rect,
            None => return None,
        };

        let raster_rect = if self.raster_spatial_node_index != self.surface_spatial_node_index {
            assert_eq!(self.device_pixel_scale.0, 1.0);

            let local_to_world = SpaceMapper::new_with_target(
                spatial_tree.root_reference_frame_index(),
                self.surface_spatial_node_index,
                WorldRect::max_rect(),
                spatial_tree,
            );

            local_to_world.map(&local_rect).unwrap()
        } else {
            // The content should have been culled out earlier.
            assert!(self.device_pixel_scale.0 > 0.0);

            local_rect.cast_unit()
        };

        let surface_rect = (raster_rect * self.device_pixel_scale).round_out().to_i32();
        if surface_rect.is_empty() {
            // The local_rect computed above may have non-empty size that is very
            // close to zero. Due to limited arithmetic precision, the SpaceMapper
            // might transform the near-zero-sized rect into a zero-sized one.
            return None;
        }

        Some(surface_rect)
    }
}

// Information about the render task(s) for a given tile
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct SurfaceTileDescriptor {
    /// Target render task for commands added to this tile. This is changed
    /// each time a sub-graph is encountered on this tile
    pub current_task_id: RenderTaskId,
    /// The compositing task for this tile, if required. This is only needed
    /// when a tile contains one or more sub-graphs.
    pub composite_task_id: Option<RenderTaskId>,
    /// Dirty rect for this tile
    pub dirty_rect: PictureRect,
}

// Details of how a surface is rendered
pub enum SurfaceDescriptorKind {
    // Picture cache tiles
    Tiled {
        tiles: FastHashMap<TileKey, SurfaceTileDescriptor>,
    },
    // A single surface (e.g. for an opacity filter)
    Simple {
        render_task_id: RenderTaskId,
        dirty_rect: PictureRect,
    },
    // A surface with 1+ intermediate tasks (e.g. blur)
    Chained {
        render_task_id: RenderTaskId,
        root_task_id: RenderTaskId,
        dirty_rect: PictureRect,
    },
}

// Describes how a surface is rendered
pub struct SurfaceDescriptor {
    kind: SurfaceDescriptorKind,
}

impl SurfaceDescriptor {
    // Create a picture cache tiled surface
    pub fn new_tiled(
        tiles: FastHashMap<TileKey, SurfaceTileDescriptor>,
    ) -> Self {
        SurfaceDescriptor {
            kind: SurfaceDescriptorKind::Tiled {
                tiles,
            },
        }
    }

    // Create a chained surface (e.g. blur)
    pub fn new_chained(
        render_task_id: RenderTaskId,
        root_task_id: RenderTaskId,
        dirty_rect: PictureRect,
    ) -> Self {
        SurfaceDescriptor {
            kind: SurfaceDescriptorKind::Chained {
                render_task_id,
                root_task_id,
                dirty_rect,
            },
        }
    }

    // Create a simple surface (e.g. opacity)
    pub fn new_simple(
        render_task_id: RenderTaskId,
        dirty_rect: PictureRect,
    ) -> Self {
        SurfaceDescriptor {
            kind: SurfaceDescriptorKind::Simple {
                render_task_id,
                dirty_rect,
            },
        }
    }
}

// Describes a list of command buffers that we are adding primitives to
// for a given surface. These are created from a command buffer builder
// as an optimization - skipping the indirection pic_task -> cmd_buffer_index
struct CommandBufferTargets {
    available_cmd_buffers: Vec<Vec<(PictureRect, CommandBufferIndex)>>,
}

impl CommandBufferTargets {
    fn new() -> Self {
        CommandBufferTargets {
            available_cmd_buffers: vec![Vec::new(); MAX_COMPOSITOR_SURFACES+1],
        }
    }

    fn init(
        &mut self,
        cb: &CommandBufferBuilder,
        rg_builder: &RenderTaskGraphBuilder,
    ) {
        for available_cmd_buffers in &mut self.available_cmd_buffers {
            available_cmd_buffers.clear();
        }

        match cb.kind {
            CommandBufferBuilderKind::Tiled { ref tiles, .. } => {
                for (key, desc) in tiles {
                    let task = rg_builder.get_task(desc.current_task_id);
                    match task.kind {
                        RenderTaskKind::Picture(ref info) => {
                            let available_cmd_buffers = &mut self.available_cmd_buffers[key.sub_slice_index.as_usize()];
                            available_cmd_buffers.push((desc.dirty_rect, info.cmd_buffer_index));
                        }
                        _ => unreachable!("bug: not a picture"),
                    }
                }
            }
            CommandBufferBuilderKind::Simple { render_task_id, dirty_rect, .. } => {
                let task = rg_builder.get_task(render_task_id);
                match task.kind {
                    RenderTaskKind::Picture(ref info) => {
                        for sub_slice_buffer in &mut self.available_cmd_buffers {
                            sub_slice_buffer.push((dirty_rect, info.cmd_buffer_index));
                        }
                    }
                    _ => unreachable!("bug: not a picture"),
                }
            }
            CommandBufferBuilderKind::Invalid => {}
        };
    }

    /// For a given rect and sub-slice, get a list of command buffers to write commands to
    fn get_cmd_buffer_targets_for_rect(
        &mut self,
        rect: &PictureRect,
        sub_slice_index: SubSliceIndex,
        targets: &mut Vec<CommandBufferIndex>,
    ) -> bool {

        for (dirty_rect, cmd_buffer_index) in &self.available_cmd_buffers[sub_slice_index.as_usize()] {
            if dirty_rect.intersects(rect) {
                targets.push(*cmd_buffer_index);
            }
        }

        !targets.is_empty()
    }
}

// Main helper interface to build a graph of surfaces. In future patches this
// will support building sub-graphs.
pub struct SurfaceBuilder {
    // The currently set cmd buffer targets (updated during push/pop)
    current_cmd_buffers: CommandBufferTargets,
    // Stack of surfaces that are parents to the current targets
    builder_stack: Vec<CommandBufferBuilder>,
    // A map of the output render tasks from any sub-graphs that haven't
    // been consumed by BackdropRender prims yet
    pub sub_graph_output_map: FastHashMap<PictureIndex, RenderTaskId>,
}

impl SurfaceBuilder {
    pub fn new() -> Self {
        SurfaceBuilder {
            current_cmd_buffers: CommandBufferTargets::new(),
            builder_stack: Vec::new(),
            sub_graph_output_map: FastHashMap::default(),
        }
    }

    /// Register the current surface as the source of a resolve for the task sub-graph that
    /// is currently on the surface builder stack.
    pub fn register_resolve_source(
        &mut self,
    ) {
        let surface_task_id = match self.builder_stack.last().unwrap().kind {
            CommandBufferBuilderKind::Tiled { .. } | CommandBufferBuilderKind::Invalid => {
                panic!("bug: only supported for non-tiled surfaces");
            }
            CommandBufferBuilderKind::Simple { render_task_id, .. } => render_task_id,
        };

        for builder in self.builder_stack.iter_mut().rev() {
            if builder.establishes_sub_graph {
                assert_eq!(builder.resolve_source, None);
                builder.resolve_source = Some(surface_task_id);
                return;
            }
        }

        unreachable!("bug: resolve source with no sub-graph");
    }

    pub fn push_surface(
        &mut self,
        surface_index: SurfaceIndex,
        is_sub_graph: bool,
        clipping_rect: PictureRect,
        descriptor: Option<SurfaceDescriptor>,
        surfaces: &mut [SurfaceInfo],
        rg_builder: &RenderTaskGraphBuilder,
    ) {
        // Init the surface
        surfaces[surface_index.0].clipping_rect = clipping_rect;

        let builder = if let Some(descriptor) = descriptor {
            match descriptor.kind {
                SurfaceDescriptorKind::Tiled { tiles } => {
                    CommandBufferBuilder::new_tiled(
                        tiles,
                    )
                }
                SurfaceDescriptorKind::Simple { render_task_id, dirty_rect, .. } => {
                    CommandBufferBuilder::new_simple(
                        render_task_id,
                        is_sub_graph,
                        None,
                        dirty_rect,
                    )
                }
                SurfaceDescriptorKind::Chained { render_task_id, root_task_id, dirty_rect, .. } => {
                    CommandBufferBuilder::new_simple(
                        render_task_id,
                        is_sub_graph,
                        Some(root_task_id),
                        dirty_rect,
                    )
                }
            }
        } else {
            CommandBufferBuilder::empty()
        };

        self.current_cmd_buffers.init(&builder, rg_builder);
        self.builder_stack.push(builder);
    }

    // Add a child render task (e.g. a render task cache item, or a clip mask) as a
    // dependency of the current surface
    pub fn add_child_render_task(
        &mut self,
        child_task_id: RenderTaskId,
        rg_builder: &mut RenderTaskGraphBuilder,
    ) {
        let builder = self.builder_stack.last().unwrap();

        match builder.kind {
            CommandBufferBuilderKind::Tiled { ref tiles } => {
                for (_, descriptor) in tiles {
                    rg_builder.add_dependency(
                        descriptor.current_task_id,
                        child_task_id,
                    );
                }
            }
            CommandBufferBuilderKind::Simple { render_task_id, .. } => {
                rg_builder.add_dependency(
                    render_task_id,
                    child_task_id,
                );
            }
            CommandBufferBuilderKind::Invalid { .. } => {}
        }
    }

    // Add a picture render task as a dependency of the parent surface. This is a
    // special case with extra complexity as the root of the surface may change
    // when inside a sub-graph. It's currently only needed for drop-shadow effects.
    pub fn add_picture_render_task(
        &mut self,
        child_task_id: RenderTaskId,
    ) {
        self.builder_stack
            .last_mut()
            .unwrap()
            .extra_dependencies
            .push(child_task_id);
    }

    // Get a list of command buffer indices that primitives should be pushed
    // to for a given current visbility / dirty state
    pub fn get_cmd_buffer_targets_for_prim(
        &mut self,
        vis: &PrimitiveDrawHeader,
        targets: &mut Vec<CommandBufferIndex>,
    ) -> bool {
        targets.clear();

        match vis.state {
            DrawState::Unset => {
                panic!("bug: invalid vis state");
            }
            DrawState::Culled => {
                false
            }
            DrawState::Visible { sub_slice_index, .. } => {
                self.current_cmd_buffers.get_cmd_buffer_targets_for_rect(
                    &vis.clip_chain.pic_coverage_rect,
                    sub_slice_index,
                    targets,
                )
            }
            DrawState::PassThrough => {
                true
            }
        }
    }

    pub fn pop_empty_surface(&mut self) {
        let builder = self.builder_stack.pop().unwrap();
        assert!(!builder.establishes_sub_graph);
    }

    // Finish adding primitives and child tasks to a surface and pop it off the stack
    pub fn pop_surface(
        &mut self,
        pic_index: PictureIndex,
        rg_builder: &mut RenderTaskGraphBuilder,
        cmd_buffers: &mut CommandBufferList,
        spatial_tree: &SpatialTree,
    ) {
        let builder = self.builder_stack.pop().unwrap();

        if builder.establishes_sub_graph {
            // If we are popping a sub-graph off the stack the dependency setup is rather more complex...
            match builder.kind {
                CommandBufferBuilderKind::Tiled { .. } | CommandBufferBuilderKind::Invalid => {
                    unreachable!("bug: sub-graphs can only be simple surfaces");
                }
                CommandBufferBuilderKind::Simple { render_task_id: child_render_task_id, root_task_id: child_root_task_id, .. } => {
                    // Get info about the resolve operation to copy from parent surface or tiles to the picture cache task
                    if let Some(resolve_task_id) = builder.resolve_source {
                        let mut src_task_ids = Vec::new();

                        // Make the output of the sub-graph a dependency of the new replacement tile task
                        let _old = self.sub_graph_output_map.insert(
                            pic_index,
                            child_root_task_id.unwrap_or(child_render_task_id),
                        );
                        debug_assert!(_old.is_none());

                        // Set up dependencies for the sub-graph. The basic concepts below are the same, but for
                        // tiled surfaces are a little more complex as there are multiple tasks to set up.
                        //  (a) Set up new task(s) on parent surface that write to the same location
                        //  (b) Set up a resolve target to copy from parent surface tasks(s) to the resolve target
                        //  (c) Make the old parent surface tasks input dependencies of the resolve target
                        //  (d) Make the sub-graph output an input dependency of the new task(s).

                        match self.builder_stack.last_mut().unwrap().kind {
                            CommandBufferBuilderKind::Tiled { ref mut tiles } => {
                                let keys: Vec<TileKey> = tiles.keys().cloned().collect();

                                // For each tile in parent surface
                                for key in keys {
                                    let descriptor = tiles.remove(&key).unwrap();
                                    let parent_task_id = descriptor.current_task_id;
                                    let parent_task = rg_builder.get_task_mut(parent_task_id);

                                    match parent_task.location {
                                        RenderTaskLocation::Unallocated { .. } | RenderTaskLocation::Existing { .. } => {
                                            // Get info about the parent tile task location and params
                                            let location = RenderTaskLocation::Existing {
                                                parent_task_id,
                                                size: parent_task.location.size(),
                                            };

                                            let pic_task = match parent_task.kind {
                                                RenderTaskKind::Picture(ref mut pic_task) => {
                                                    let cmd_buffer_index = cmd_buffers.create_cmd_buffer();
                                                    let new_pic_task = pic_task.duplicate(cmd_buffer_index);

                                                    // Add the resolve src to copy from tile -> picture input task
                                                    src_task_ids.push(parent_task_id);

                                                    new_pic_task
                                                }
                                                _ => panic!("bug: not a picture"),
                                            };

                                            // Make the existing tile an input dependency of the resolve target
                                            rg_builder.add_dependency(
                                                resolve_task_id,
                                                parent_task_id,
                                            );

                                            // Create the new task to replace the tile task
                                            let new_task_id = rg_builder.add().init(
                                                RenderTask::new(
                                                    location,          // draw to same place
                                                    RenderTaskKind::Picture(pic_task),
                                                ),
                                            );

                                            // Ensure that the parent task will get scheduled earlier during
                                            // pass assignment since we are reusing the existing surface,
                                            // even though it's not technically needed for rendering order.
                                            rg_builder.add_dependency(
                                                new_task_id,
                                                parent_task_id,
                                            );

                                            // Update the surface builder with the now current target for future primitives
                                            tiles.insert(
                                                key,
                                                SurfaceTileDescriptor {
                                                    current_task_id: new_task_id,
                                                    ..descriptor
                                                },
                                            );
                                        }
                                        RenderTaskLocation::Static { .. } => {
                                            // Update the surface builder with the now current target for future primitives
                                            tiles.insert(
                                                key,
                                                descriptor,
                                            );
                                        }
                                        _ => {
                                            panic!("bug: unexpected task location");
                                        }
                                    }
                                }
                            }
                            CommandBufferBuilderKind::Simple { render_task_id: ref mut parent_task_id, root_task_id: ref parent_root_task_id, .. } => {
                                let parent_task = rg_builder.get_task_mut(*parent_task_id);

                                // Get info about the parent tile task location and params
                                let location = RenderTaskLocation::Existing {
                                    parent_task_id: *parent_task_id,
                                    size: parent_task.location.size(),
                                };
                                let pic_task = match parent_task.kind {
                                    RenderTaskKind::Picture(ref mut pic_task) => {
                                        let cmd_buffer_index = cmd_buffers.create_cmd_buffer();

                                        let new_pic_task = pic_task.duplicate(cmd_buffer_index);

                                        // Add the resolve src to copy from tile -> picture input task
                                        src_task_ids.push(*parent_task_id);

                                        new_pic_task
                                    }
                                    _ => panic!("bug: not a picture"),
                                };

                                // Make the existing surface an input dependency of the resolve target
                                rg_builder.add_dependency(
                                    resolve_task_id,
                                    *parent_task_id,
                                );

                                // Create the new task to replace the parent surface task
                                let new_task_id = rg_builder.add().init(
                                    RenderTask::new(
                                        location,          // draw to same place
                                        RenderTaskKind::Picture(pic_task),
                                    ),
                                );

                                // Ensure that the parent task will get scheduled earlier during
                                // pass assignment since we are reusing the existing surface,
                                // even though it's not technically needed for rendering order.
                                rg_builder.add_dependency(
                                    new_task_id,
                                    *parent_task_id,
                                );

                                // If the parent is a chained surface (e.g. a CSS blur or drop-shadow
                                // filter), the tasks in that chain sample the same texture that
                                // new_task_id draws the post-backdrop-capture content into. They must
                                // run after new_task_id, otherwise those primitives are missing from
                                // the filter output.
                                if let Some(root_task_id) = *parent_root_task_id {
                                    order_readers_after(
                                        rg_builder,
                                        root_task_id,
                                        *parent_task_id,
                                        new_task_id,
                                    );
                                }

                                // Update the surface builder with the now current target for future primitives
                                *parent_task_id = new_task_id;
                            }
                            CommandBufferBuilderKind::Invalid => {
                                unreachable!();
                            }
                        }

                        // The resolve target may establish a different raster
                        // root than the parent surface(s) it reads back from (for
                        // example a backdrop-filter that promoted to a
                        // root-snapping raster root inside a scrolled subtree). The
                        // copy rects computed in `handle_resolve` then live in two
                        // different raster spaces, so pre-compute the mapping
                        // between them here (identity in the common case).
                        let dest_to_src_raster = resolve_dest_to_src_raster(
                            rg_builder,
                            spatial_tree,
                            resolve_task_id,
                            &src_task_ids,
                        );

                        let dest_task = rg_builder.get_task_mut(resolve_task_id);

                        match dest_task.kind {
                            RenderTaskKind::Picture(ref mut dest_task_info) => {
                                assert!(dest_task_info.resolve_op.is_none());
                                dest_task_info.resolve_op = Some(ResolveOp {
                                    src_task_ids,
                                    dest_task_id: resolve_task_id,
                                    dest_to_src_raster,
                                })
                            }
                            _ => {
                                unreachable!("bug: not a picture");
                            }
                        }
                    }

                    // This can occur if there is an edge case where the resolve target is found
                    // not visible even though the filter chain was (for example, in the case of
                    // an extreme scale causing floating point inaccuracies). Adding a dependency
                    // here is also a safety in case for some reason the backdrop render primitive
                    // doesn't pick up the dependency, ensuring that it gets scheduled and freed
                    // as early as possible.
                    match self.builder_stack.last().unwrap().kind {
                        CommandBufferBuilderKind::Tiled { ref tiles } => {
                            // For a tiled render task, add as a dependency to every tile.
                            for (_, descriptor) in tiles {
                                rg_builder.add_dependency(
                                    descriptor.current_task_id,
                                    child_root_task_id.unwrap_or(child_render_task_id),
                                );
                            }
                        }
                        CommandBufferBuilderKind::Simple { render_task_id: parent_task_id, .. } => {
                            rg_builder.add_dependency(
                                parent_task_id,
                                child_root_task_id.unwrap_or(child_render_task_id),
                            );
                        }
                        CommandBufferBuilderKind::Invalid => {
                            unreachable!();
                        }
                    }
                }
            }
        } else {
            match builder.kind {
                CommandBufferBuilderKind::Tiled { ref tiles } => {
                    for (_, descriptor) in tiles {
                        if let Some(composite_task_id) = descriptor.composite_task_id {
                            rg_builder.add_dependency(
                                composite_task_id,
                                descriptor.current_task_id,
                            );

                            let composite_task = rg_builder.get_task_mut(composite_task_id);
                            match composite_task.kind {
                                RenderTaskKind::TileComposite(ref mut info) => {
                                    info.task_id = Some(descriptor.current_task_id);
                                }
                                _ => unreachable!("bug: not a tile composite"),
                            }
                        }
                    }
                }
                CommandBufferBuilderKind::Simple { render_task_id: child_task_id, root_task_id: child_root_task_id, .. } => {
                    match self.builder_stack.last().unwrap().kind {
                        CommandBufferBuilderKind::Tiled { ref tiles } => {
                            // For a tiled render task, add as a dependency to every tile.
                            for (_, descriptor) in tiles {
                                rg_builder.add_dependency(
                                    descriptor.current_task_id,
                                    child_root_task_id.unwrap_or(child_task_id),
                                );
                            }
                        }
                        CommandBufferBuilderKind::Simple { render_task_id: parent_task_id, .. } => {
                            rg_builder.add_dependency(
                                parent_task_id,
                                child_root_task_id.unwrap_or(child_task_id),
                            );
                        }
                        CommandBufferBuilderKind::Invalid => {
                        }
                    }
                }
                CommandBufferBuilderKind::Invalid => {
                }
            }
        }

        // Step through the dependencies for this builder and add them to the finalized
        // render task root(s) for this surface
        match builder.kind {
            CommandBufferBuilderKind::Tiled { ref tiles } => {
                for (_, descriptor) in tiles {
                    for task_id in &builder.extra_dependencies {
                        rg_builder.add_dependency(
                            descriptor.current_task_id,
                            *task_id,
                        );
                    }
                }
            }
            CommandBufferBuilderKind::Simple { render_task_id, .. } => {
                for task_id in &builder.extra_dependencies {
                    rg_builder.add_dependency(
                        render_task_id,
                        *task_id,
                    );
                }
            }
            CommandBufferBuilderKind::Invalid { .. } => {}
        }

        // Set up the cmd-buffer targets to write prims into the popped surface
        self.current_cmd_buffers.init(
            self.builder_stack.last().unwrap_or(&CommandBufferBuilder::empty()), rg_builder
        );
    }

    pub fn finalize(self) {
        assert!(self.builder_stack.is_empty());
    }
}


pub fn calculate_screen_uv(
    p: DevicePoint,
    clipped: DeviceRect,
) -> DeviceHomogeneousVector {
    // TODO(gw): Switch to a simple mix, no bilerp / homogeneous vec needed anymore
    DeviceHomogeneousVector::new(
        (p.x - clipped.min.x) / (clipped.max.x - clipped.min.x),
        (p.y - clipped.min.y) / (clipped.max.y - clipped.min.y),
        0.0,
        1.0,
    )
}
