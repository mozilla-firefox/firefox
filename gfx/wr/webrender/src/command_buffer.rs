/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::euclid::{point2, Box2D};
use api::{MixBlendMode, units::{DeviceRect, LayoutRect, PictureRect}};
use crate::pattern::{PatternKind, PatternShaderInput};
use crate::renderer::BlendMode;
use crate::{spatial_tree::SpatialNodeIndex, render_task_graph::RenderTaskId, surface::{SurfaceExtraTarget, SurfaceTileDescriptor}, tile_cache::TileKey, renderer::GpuBufferAddress, FastHashMap};
use crate::gpu_types::QuadSegment;
use crate::prim_store::storage;
use crate::segment::EdgeMask;
use crate::transform::GpuTransformId;
use crate::prim_store::PrimitiveInstanceIndex;
use crate::visibility::{BackdropCaptureIndex, PrimitiveDrawHeader};

/// A tightly packed command stored in a command buffer
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, Copy, Clone)]
pub struct Command(u32);

impl Command {
    /// Draw a simple primitive that needs prim instance index only.
    const CMD_DRAW_SIMPLE_PRIM: u32 = 0x00000000;
    /// Change the current spatial node.
    const CMD_SET_SPATIAL_NODE: u32 = 0x10000000;
    /// Draw a primitive, that has a single GPU buffer addresses.
    const CMD_DRAW_INSTANCE: u32 = 0x30000000;
    /// Draw a generic quad primitive
    const CMD_DRAW_QUAD: u32 = 0x40000000;
    /// Set a list of variable-length segments
    const CMD_SET_SEGMENTS: u32 = 0x50000000;
    /// Draw a 3d-split-composite primitive.
    const CMD_DRAW_SPLIT_COMPOSITE: u32 = 0x60000000;
    /// Draw a path primitive, tiled during batching.
    const CMD_DRAW_PATH: u32 = 0x70000000;

    /// Bitmask for command bits of the command.
    const CMD_MASK: u32 = 0xf0000000;
    /// Bitmask for param bits of the command.
    const PARAM_MASK: u32 = 0x0fffffff;

    /// Encode drawing a simple primitive.
    fn draw_simple_prim(draw_index: storage::Index<PrimitiveDrawHeader>) -> Self {
        Command(Command::CMD_DRAW_SIMPLE_PRIM | draw_index.0)
    }

    /// Encode changing spatial node.
    fn set_spatial_node(spatial_node_index: SpatialNodeIndex) -> Self {
        Command(Command::CMD_SET_SPATIAL_NODE | spatial_node_index.0)
    }

    /// Encode a list of segments that follow
    fn set_segments(count: usize) -> Self {
        Command(Command::CMD_SET_SEGMENTS | count as u32)
    }

    fn draw_instance(draw_index: storage::Index<PrimitiveDrawHeader>) -> Self {
        Command(Command::CMD_DRAW_INSTANCE | draw_index.0)
    }

    /// Encode drawing a 3d-split-composite prim.
    fn draw_split_composite(draw_index: storage::Index<PrimitiveDrawHeader>) -> Self {
        Command(Command::CMD_DRAW_SPLIT_COMPOSITE | draw_index.0)
    }

    /// Encode arbitrary data word.
    fn data(data: u32) -> Self {
        Command(data)
    }

    fn draw_quad() -> Self {
        Command(Command::CMD_DRAW_QUAD)
    }

    fn draw_path(path_index: u32) -> Self {
        debug_assert!(path_index <= Command::PARAM_MASK);
        Command(Command::CMD_DRAW_PATH | path_index)
    }
}

bitflags! {
    /// Flags related to quad primitives
    #[repr(transparent)]
    #[cfg_attr(feature = "capture", derive(Serialize))]
    #[cfg_attr(feature = "replay", derive(Deserialize))]
    #[derive(Debug, Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash)]
    pub struct QuadFlags : u8 {
        const IS_OPAQUE = 1 << 0;

        /// If true, the prim is 2d and axis-aligned in device space. The render task rect can
        /// cheaply be used as a device-space clip in the vertex shader.
        const APPLY_RENDER_TASK_CLIP = 1 << 1;

        /// If true, use segments for drawing the AA edges, to allow inner section to be opaque
        const USE_AA_SEGMENTS = 1 << 3;

        /// If true, render as a mask. This ignores the blue, green and alpha channels and replaces
        /// them with the red channel in the fragment shader. Used with multiply blending, on top
        /// of premultiplied alpha content, it has the effect of applying a mask to the content under ir.
        const IS_MASK = 1 << 4;
    }
}

bitflags! {
    /// Defines the space that a quad primitive is drawn in
    #[repr(transparent)]
    #[cfg_attr(feature = "capture", derive(Serialize))]
    #[cfg_attr(feature = "replay", derive(Deserialize))]
    #[derive(Debug, Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash)]
    pub struct MaskFlags : i32 {
        const PRIM_SPACE = 1 << 0;
    }
}

/// The unpacked equivalent to a `Command`.
///
/// Each variant carries an `Index<PrimitiveDrawHeader>` identifying which
/// per-frame draw header to use. That is a draw index, not a primitive instance
/// index: reach the instance through the header's `prim_instance_index`.
#[cfg_attr(feature = "capture", derive(Serialize))]
pub enum PrimitiveCommand {
    Simple {
        draw_index: storage::Index<PrimitiveDrawHeader>,
        device_rect: DeviceRect,
    },
    SplitComposite {
        draw_index: storage::Index<PrimitiveDrawHeader>,
        device_rect: DeviceRect,
        polygons_address: GpuBufferAddress,
        // Maps the plane's local space to the destination surface's raster space.
        transform_id: GpuTransformId,
        src_task_id: RenderTaskId,
        // The picture's unclipped local rect, used to map plane positions to
        // texture coordinates.
        pattern_rect: LayoutRect,
    },
    Instance {
        draw_index: storage::Index<PrimitiveDrawHeader>,
        device_rect: DeviceRect,
        gpu_buffer_address: GpuBufferAddress,
    },
    Quad {
        pattern: PatternKind,
        pattern_input: PatternShaderInput,
        // Source textures sampled by the pattern. Most patterns only use slot 0;
        // multi-plane patterns such as YUV use the additional slots.
        src_color_task_ids: [RenderTaskId; 3],
        device_rect: DeviceRect,
        gpu_buffer_address: GpuBufferAddress,
        transform_id: GpuTransformId,
        quad_flags: QuadFlags,
        edge_flags: EdgeMask,
        blend_mode: BlendMode,
    },
    Path {
        /// Index of the `PathPrimitive` in the frame's path primitive storage.
        path_index: u32,
        device_rect: DeviceRect,
    },
}

impl PrimitiveCommand {
    pub fn simple(
        draw_index: storage::Index<PrimitiveDrawHeader>,
        device_rect: DeviceRect,
    ) -> Self {
        PrimitiveCommand::Simple {
            draw_index,
            device_rect,
        }
    }

    pub fn split_composite(
        draw_index: storage::Index<PrimitiveDrawHeader>,
        device_rect: DeviceRect,
        polygons_address: GpuBufferAddress,
        transform_id: GpuTransformId,
        src_task_id: RenderTaskId,
        pattern_rect: LayoutRect,
    ) -> Self {
        PrimitiveCommand::SplitComposite {
            draw_index,
            device_rect,
            polygons_address,
            transform_id,
            src_task_id,
            pattern_rect,
        }
    }

    pub fn quad(
        pattern: PatternKind,
        pattern_input: PatternShaderInput,
        src_color_task_ids: [RenderTaskId; 3],
        device_rect: DeviceRect,
        gpu_buffer_address: GpuBufferAddress,
        transform_id: GpuTransformId,
        quad_flags: QuadFlags,
        edge_flags: EdgeMask,
        blend_mode: BlendMode,
    ) -> Self {
        PrimitiveCommand::Quad {
            pattern,
            pattern_input,
            src_color_task_ids,
            device_rect,
            gpu_buffer_address,
            transform_id,
            quad_flags,
            edge_flags,
            blend_mode,
        }
    }

    pub fn path(path_index: u32, device_rect: DeviceRect) -> Self {
        PrimitiveCommand::Path { path_index, device_rect }
    }

    pub fn instance(
        draw_index: storage::Index<PrimitiveDrawHeader>,
        device_rect: DeviceRect,
        gpu_buffer_address: GpuBufferAddress,
    ) -> Self {
        PrimitiveCommand::Instance {
            draw_index,
            device_rect,
            gpu_buffer_address,
        }
    }
}


// Non-Advanced variants map to 0..=8; Advanced(mode) maps to 9 + mode as u32.
// MixBlendMode is repr(u8) with values 0..=16, so Advanced covers 9..=25.

fn encode_blend_mode(blend_mode: BlendMode) -> u32 {
    match blend_mode {
        BlendMode::None => 0,
        BlendMode::Alpha => 1,
        BlendMode::PremultipliedAlpha => 2,
        BlendMode::PremultipliedDestOut => 3,
        BlendMode::SubpixelDualSource => 4,
        BlendMode::Screen => 5,
        BlendMode::Exclusion => 6,
        BlendMode::PlusLighter => 7,
        BlendMode::Advanced(mode) => 8 + mode as u32,
        BlendMode::Multiply | BlendMode::ShowOverdraw => {
            unreachable!("blend mode {:?} is set by the renderer, not by batches", blend_mode);
        }
    }
}

fn decode_blend_mode(val: u32) -> BlendMode {
    match val {
        0 => BlendMode::None,
        1 => BlendMode::Alpha,
        2 => BlendMode::PremultipliedAlpha,
        3 => BlendMode::PremultipliedDestOut,
        4 => BlendMode::SubpixelDualSource,
        5 => BlendMode::Screen,
        6 => BlendMode::Exclusion,
        7 => BlendMode::PlusLighter,
        _ => BlendMode::Advanced(unsafe { std::mem::transmute::<u8, MixBlendMode>((val - 8) as u8) }),
    }
}

/// Encode a rect as four data words.
fn push_rect<U>(commands: &mut Vec<Command>, rect: &Box2D<f32, U>) {
    commands.push(Command::data(rect.min.x.to_bits()));
    commands.push(Command::data(rect.min.y.to_bits()));
    commands.push(Command::data(rect.max.x.to_bits()));
    commands.push(Command::data(rect.max.y.to_bits()));
}

/// Read back a rect written by `push_rect`.
fn read_rect<'a, U>(cmd_iter: &mut impl Iterator<Item = &'a Command>) -> Box2D<f32, U> {
    Box2D {
        min: point2(
            f32::from_bits(cmd_iter.next().unwrap().0),
            f32::from_bits(cmd_iter.next().unwrap().0),
        ),
        max: point2(
            f32::from_bits(cmd_iter.next().unwrap().0),
            f32::from_bits(cmd_iter.next().unwrap().0),
        ),
    }
}

/// A list of commands describing how to draw a primitive list.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct CommandBuffer {
    /// The encoded drawing commands.
    commands: Vec<Command>,
    /// Cached current spatial node.
    current_spatial_node_index: SpatialNodeIndex,
}

impl CommandBuffer {
    /// Construct a new cmd buffer.
    pub fn new() -> Self {
        CommandBuffer {
            commands: Vec::new(),
            current_spatial_node_index: SpatialNodeIndex::INVALID,
        }
    }

    /// Push a list of segments in to the cmd buffer
    pub fn set_segments(
        &mut self,
        segments: &[QuadSegment],
    ) {
        self.commands.push(Command::set_segments(segments.len()));
        for segment in segments {
            self.commands.push(Command::data(segment.task_id.index));
            self.commands.push(Command::data(segment.task_id.sub_rect_index as u32));
            push_rect(&mut self.commands, &segment.rect);
        }
    }

    /// Add a primitive to the command buffer.
    pub fn add_prim(
        &mut self,
        prim_cmd: &PrimitiveCommand,
        spatial_node_index: SpatialNodeIndex,
    ) {
        if self.current_spatial_node_index != spatial_node_index {
            self.commands.push(Command::set_spatial_node(spatial_node_index));
            self.current_spatial_node_index = spatial_node_index;
        }

        self.add_cmd(prim_cmd);
    }

    /// Add a cmd to the command buffer.
    pub fn add_cmd(
        &mut self,
        prim_cmd: &PrimitiveCommand,
    ) {
        match *prim_cmd {
            PrimitiveCommand::Simple { draw_index, device_rect } => {
                self.commands.push(Command::draw_simple_prim(draw_index));
                push_rect(&mut self.commands, &device_rect);
            }
            PrimitiveCommand::SplitComposite { draw_index, device_rect, polygons_address, transform_id, src_task_id, pattern_rect } => {
                self.commands.push(Command::draw_split_composite(draw_index));
                push_rect(&mut self.commands, &device_rect);
                self.commands.push(Command::data(polygons_address.as_u32()));
                self.commands.push(Command::data(transform_id.0));
                self.commands.push(Command::data(src_task_id.index));
                self.commands.push(Command::data(src_task_id.sub_rect_index as u32));
                push_rect(&mut self.commands, &pattern_rect);
            }
            PrimitiveCommand::Instance { draw_index, device_rect, gpu_buffer_address } => {
                self.commands.push(Command::draw_instance(draw_index));
                push_rect(&mut self.commands, &device_rect);
                self.commands.push(Command::data(gpu_buffer_address.as_u32()));
            }
            PrimitiveCommand::Quad { pattern, pattern_input, device_rect, gpu_buffer_address, transform_id, quad_flags, edge_flags, src_color_task_ids, blend_mode } => {
                self.commands.push(Command::draw_quad());
                push_rect(&mut self.commands, &device_rect);
                self.commands.push(Command::data(pattern as u32));
                self.commands.push(Command::data(pattern_input.0 as u32));
                self.commands.push(Command::data(pattern_input.1 as u32));
                self.commands.push(Command::data(gpu_buffer_address.as_u32()));
                self.commands.push(Command::data(transform_id.0));
                self.commands.push(Command::data((quad_flags.bits() as u32) << 16 | edge_flags.bits() as u32));
                self.commands.push(Command::data(encode_blend_mode(blend_mode)));
                for i in 0..pattern.num_src_textures() {
                    self.commands.push(Command::data(src_color_task_ids[i].index));
                    self.commands.push(Command::data(src_color_task_ids[i].sub_rect_index as u32));
                }
            }
            PrimitiveCommand::Path { path_index, device_rect } => {
                self.commands.push(Command::draw_path(path_index));
                push_rect(&mut self.commands, &device_rect);
            }
        }
    }

    /// Iterate the command list, calling a provided closure for each primitive draw command.
    pub fn iter_prims<F>(
        &self,
        f: &mut F,
    ) where F: FnMut(&PrimitiveCommand, SpatialNodeIndex, &[QuadSegment]) {
        let mut current_spatial_node_index = SpatialNodeIndex::INVALID;
        let mut cmd_iter = self.commands.iter();
        // TODO(gw): Consider pre-allocating this / Smallvec if it shows up in profiles.
        let mut segments = Vec::new();

        while let Some(cmd) = cmd_iter.next() {
            let command = cmd.0 & Command::CMD_MASK;
            let param = cmd.0 & Command::PARAM_MASK;

            match command {
                Command::CMD_DRAW_SIMPLE_PRIM => {
                    let draw_index = storage::Index::from_u32(param);
                    let device_rect = read_rect(&mut cmd_iter);
                    let cmd = PrimitiveCommand::simple(draw_index, device_rect);
                    f(&cmd, current_spatial_node_index, &[]);
                }
                Command::CMD_SET_SPATIAL_NODE => {
                    current_spatial_node_index = SpatialNodeIndex(param);
                }
                Command::CMD_DRAW_SPLIT_COMPOSITE => {
                    let draw_index = storage::Index::from_u32(param);
                    let device_rect = read_rect(&mut cmd_iter);
                    let polygons_address = GpuBufferAddress::from_u32(cmd_iter.next().unwrap().0);
                    let transform_id = GpuTransformId(cmd_iter.next().unwrap().0);
                    let src_task_id = RenderTaskId {
                        index: cmd_iter.next().unwrap().0,
                        sub_rect_index: cmd_iter.next().unwrap().0 as u16,
                    };
                    let pattern_rect = read_rect(&mut cmd_iter);
                    let cmd = PrimitiveCommand::split_composite(
                        draw_index,
                        device_rect,
                        polygons_address,
                        transform_id,
                        src_task_id,
                        pattern_rect,
                    );
                    f(&cmd, current_spatial_node_index, &[]);
                }
                Command::CMD_DRAW_QUAD => {
                    let device_rect = read_rect(&mut cmd_iter);
                    let pattern = PatternKind::from_u32(cmd_iter.next().unwrap().0);
                    let pattern_input = PatternShaderInput(
                        cmd_iter.next().unwrap().0 as i32,
                        cmd_iter.next().unwrap().0 as i32,
                    );
                    let data = cmd_iter.next().unwrap();
                    let transform_id = GpuTransformId(cmd_iter.next().unwrap().0);
                    let bits = cmd_iter.next().unwrap().0;
                    let quad_flags = QuadFlags::from_bits((bits >> 16) as u8).unwrap();
                    let edge_flags = EdgeMask::from_bits((bits & 0xff) as u8).unwrap();
                    let blend_mode = decode_blend_mode(cmd_iter.next().unwrap().0);
                    let gpu_buffer_address = GpuBufferAddress::from_u32(data.0);

                    let mut src_color_task_ids = [RenderTaskId::INVALID; 3];
                    for i in 0..pattern.num_src_textures() {
                        src_color_task_ids[i] = RenderTaskId {
                            index: cmd_iter.next().unwrap().0,
                            sub_rect_index: cmd_iter.next().unwrap().0 as u16
                        };
                    }

                    let cmd = PrimitiveCommand::quad(
                        pattern,
                        pattern_input,
                        src_color_task_ids,
                        device_rect,
                        gpu_buffer_address,
                        transform_id,
                        quad_flags,
                        edge_flags,
                        blend_mode,
                    );
                    f(&cmd, current_spatial_node_index, &segments);
                    segments.clear()
                }
                Command::CMD_DRAW_INSTANCE => {
                    let draw_index = storage::Index::from_u32(param);
                    let device_rect = read_rect(&mut cmd_iter);
                    let data = cmd_iter.next().unwrap();
                    let gpu_buffer_address = GpuBufferAddress::from_u32(data.0);
                    let cmd = PrimitiveCommand::instance(
                        draw_index,
                        device_rect,
                        gpu_buffer_address,
                    );
                    f(&cmd, current_spatial_node_index, &[]);
                }
                Command::CMD_DRAW_PATH => {
                    let device_rect = read_rect(&mut cmd_iter);
                    let cmd = PrimitiveCommand::path(param, device_rect);
                    f(&cmd, current_spatial_node_index, &[]);
                }
                Command::CMD_SET_SEGMENTS => {
                    let count = param;
                    for _ in 0 .. count {
                        segments.push(QuadSegment {
                            task_id: RenderTaskId {
                                index: cmd_iter.next().unwrap().0,
                                sub_rect_index: cmd_iter.next().unwrap().0 as u16,
                            },
                            rect: read_rect(&mut cmd_iter),
                        });
                    }
                }
                _ => {
                    unreachable!();
                }
            }
        }
    }
}

/// Abstracts whether a command buffer is being built for a tiled (picture cache)
/// or simple (child surface).
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum CommandBufferBuilderKind {
    Tiled {
        // TODO(gw): It might be worth storing this as a 2d-array instead
        //           of a hash map if it ever shows up in profiles. This is
        //           slightly complicated by the sub_slice_index in the
        //           TileKey structure - could have a 2 level array?
        tiles: FastHashMap<TileKey, SurfaceTileDescriptor>,
        /// Tasks besides the tiles that this surface's primitives are drawn
        /// into. Primitives of every sub-slice in a target's rect are drawn
        /// into it.
        extra_targets: Vec<SurfaceExtraTarget>,
    },
    Simple {
        render_task_id: RenderTaskId,
        root_task_id: Option<RenderTaskId>,
        dirty_rect: PictureRect,
    },
    Invalid,
}

impl CommandBufferBuilderKind {
    /// Call `f` with each task the surface's primitives are drawn into.
    pub fn for_each_task(&self, mut f: impl FnMut(RenderTaskId)) {
        match self {
            CommandBufferBuilderKind::Tiled { tiles, extra_targets } => {
                for descriptor in tiles.values() {
                    f(descriptor.current_task_id);
                }
                for target in extra_targets {
                    f(target.task_id);
                }
            }
            CommandBufferBuilderKind::Simple { render_task_id, .. } => f(*render_task_id),
            CommandBufferBuilderKind::Invalid => {}
        }
    }
}

/// A backdrop-filter capture open on a surface: each primitive prepared on the
/// surface that is behind the element is drawn into the capture too.
#[derive(Debug, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct OpenBackdropCapture {
    pub index: BackdropCaptureIndex,
    /// The area of the surface the capture's filters sample.
    pub region: PictureRect,
    /// The `BackdropCapture` primitive. Primitives painted before the element
    /// have lower instance indices.
    pub capture_instance_index: PrimitiveInstanceIndex,
    /// The command buffer the capture's task draws from.
    pub cmd_buffer_index: CommandBufferIndex,
    /// The render tasks the primitives drawn into the capture depend on, which
    /// its task has to depend on too.
    pub dependencies: Vec<RenderTaskId>,
    /// Whether the primitive being prepared on the surface is drawn into the
    /// capture, so that the render tasks it makes the surface depend on are
    /// added to its dependencies too.
    pub drawn_into: bool,
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct CommandBufferBuilder {
    pub kind: CommandBufferBuilderKind,

    /// List of render tasks that depend on the task that will be created for this builder.
    pub extra_dependencies: Vec<RenderTaskId>,

    /// The backdrop-filter captures open on this surface.
    pub backdrop_captures: Vec<OpenBackdropCapture>,
}

impl CommandBufferBuilder {
    pub fn empty() -> Self {
        CommandBufferBuilder {
            kind: CommandBufferBuilderKind::Invalid,
            extra_dependencies: Vec::new(),
            backdrop_captures: Vec::new(),
        }
    }

    /// Construct a tiled command buffer builder.
    pub fn new_tiled(
        tiles: FastHashMap<TileKey, SurfaceTileDescriptor>,
        extra_targets: Vec<SurfaceExtraTarget>,
    ) -> Self {
        CommandBufferBuilder {
            kind: CommandBufferBuilderKind::Tiled {
                tiles,
                extra_targets,
            },
            extra_dependencies: Vec::new(),
            backdrop_captures: Vec::new(),
        }
    }

    /// Construct a simple command buffer builder.
    pub fn new_simple(
        render_task_id: RenderTaskId,
        root_task_id: Option<RenderTaskId>,
        dirty_rect: PictureRect,
    ) -> Self {
        CommandBufferBuilder {
            kind: CommandBufferBuilderKind::Simple {
                render_task_id,
                root_task_id,
                dirty_rect,
            },
            extra_dependencies: Vec::new(),
            backdrop_captures: Vec::new(),
        }
    }
}

// Index into a command buffer stored in a `CommandBufferList`.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, Copy, Clone)]
pub struct CommandBufferIndex(pub u32);

// Container for a list of command buffers that are built for a frame.
pub struct CommandBufferList {
    cmd_buffers: Vec<CommandBuffer>,
}

impl CommandBufferList {
    pub fn new() -> Self {
        CommandBufferList {
            cmd_buffers: Vec::new(),
        }
    }

    pub fn create_cmd_buffer(
        &mut self,
    ) -> CommandBufferIndex {
        let index = CommandBufferIndex(self.cmd_buffers.len() as u32);
        self.cmd_buffers.push(CommandBuffer::new());
        index
    }

    pub fn get(&self, index: CommandBufferIndex) -> &CommandBuffer {
        &self.cmd_buffers[index.0 as usize]
    }

    pub fn get_mut(&mut self, index: CommandBufferIndex) -> &mut CommandBuffer {
        &mut self.cmd_buffers[index.0 as usize]
    }
}
