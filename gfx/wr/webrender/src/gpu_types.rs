/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{PremultipliedColorF, YuvFormat, YuvRangedColorSpace};
use api::units::*;
use euclid::HomogeneousVector;
use crate::composite::{CompositeFeatures, CompositorClip};
use crate::pattern::PatternShaderInput;
use crate::quad::LayoutOrDeviceRect;
use crate::transform::GpuTransformId;
use crate::internal_types::{FrameVec, FrameMemory};
use crate::render_task::RenderTaskAddress;
use crate::render_task_graph::RenderTaskId;
use crate::renderer::{GpuBufferAddress, GpuBufferBuilderF, GpuBufferHandle, GpuBufferWriterF, GpuBufferDataF, GpuBufferDataI, GpuBufferWriterI, ShaderColorMode};
use crate::util::ScaleOffset;
use glyph_rasterizer::SubpixelDirection;
use crate::util::pack_as_float;
use crate::path_tiler::PathTileInstance;


// Contains type that must exactly match the same structures declared in GLSL.

pub const VECS_PER_TRANSFORM: usize = 8;

#[derive(Copy, Clone, PartialEq)]
#[repr(C)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ZBufferId(pub i32);

impl ZBufferId {
    pub fn invalid() -> Self {
        ZBufferId(i32::MAX)
    }
}

impl std::fmt::Debug for ZBufferId {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        if *self == Self::invalid() {
            write!(f, "<invalid>")
        } else {
            write!(f, "#{}", self.0)
        }
    }
}

#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ZBufferIdGenerator {
    next: i32,
    max_depth_ids: i32,
}

impl ZBufferIdGenerator {
    pub fn new(max_depth_ids: i32) -> Self {
        ZBufferIdGenerator {
            next: 0,
            max_depth_ids,
        }
    }

    pub fn next(&mut self) -> ZBufferId {
        debug_assert!(self.next < self.max_depth_ids);
        let id = ZBufferId(self.next);
        self.next += 1;
        id
    }
}

#[derive(Clone, Debug)]
#[repr(C)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct CopyInstance {
    pub src_rect: DeviceRect,
    pub dst_rect: DeviceRect,
    pub dst_texture_size: DeviceSize,
}

#[repr(i32)]
#[derive(Debug, Copy, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum BlurDirection {
    Horizontal = 0,
    Vertical,
}

impl BlurDirection {
    pub fn as_int(self) -> i32 {
        match self {
            BlurDirection::Horizontal => 0,
            BlurDirection::Vertical => 1,
        }
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash, MallocSizeOf)]
#[repr(C)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum BlurEdgeMode {
    Duplicate = 0,
    Mirror,
}

impl BlurEdgeMode {
    pub fn as_int(self) -> i32 {
        match self {
            BlurEdgeMode::Duplicate => 0,
            BlurEdgeMode::Mirror => 1,
        }
    }
}

#[derive(Clone, Debug)]
#[repr(C)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BlurInstance {
    pub task_address: RenderTaskAddress,
    pub src_task_address: RenderTaskAddress,
    pub blur_direction: i32,
    pub edge_mode: i32,
    pub blur_std_deviation: f32,
    pub blur_region: DeviceSize,
}

#[derive(Clone, Debug)]
#[repr(C)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ScalingInstance {
    pub target_rect: DeviceRect,
    pub source_rect: DeviceRect,
    source_rect_type: f32,
}

impl ScalingInstance {
    pub fn new(target_rect: DeviceRect, source_rect: DeviceRect, source_rect_normalized: bool) -> Self {
        let source_rect_type = match source_rect_normalized {
            true => UV_TYPE_NORMALIZED,
            false => UV_TYPE_UNNORMALIZED,
        };
        Self {
            target_rect,
            source_rect,
            source_rect_type: pack_as_float(source_rect_type),
        }
    }
}

#[derive(Clone, Debug)]
#[repr(C)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct SVGFEFilterInstance {
    pub target_rect: DeviceRect,
    pub input_1_content_scale_and_offset: [f32; 4],
    pub input_2_content_scale_and_offset: [f32; 4],
    pub input_1_task_address: RenderTaskAddress,
    pub input_2_task_address: RenderTaskAddress,
    pub kind: u16,
    pub input_count: u16,
    pub extra_data_address: i32,
}

#[derive(Copy, Clone, Debug, Hash, MallocSizeOf, PartialEq, Eq)]
#[repr(C)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum BorderSegment {
    TopLeft,
    TopRight,
    BottomRight,
    BottomLeft,
    Left,
    Top,
    Right,
    Bottom,
}

pub struct BorderInstanceGpuData {
    pub local_rect: DeviceRect,
    pub color0: PremultipliedColorF,
    pub color1: PremultipliedColorF,
    pub widths: DeviceSize,
    pub radius: DeviceSize,
    pub shape: f32,
    pub shape_offset: DeviceSize,
    pub inset: DeviceSize,
}

impl BorderInstanceGpuData {
    pub fn write(&self, superellipse: bool, gpu_buffer_builder: &mut GpuBufferBuilderF) -> GpuBufferAddress {
        let block_count = if superellipse { 6 } else { 4 };
        let mut writer = gpu_buffer_builder.write_blocks(block_count);
        writer.push_one(self.local_rect);
        writer.push_one(self.color0);
        writer.push_one(self.color1);
        writer.push_one([self.widths.width, self.widths.height, self.radius.width, self.radius.height]);
        if superellipse {
            writer.push_one([self.shape, self.shape_offset.width, self.shape_offset.height, 0.0]);
            writer.push_one([self.inset.width, self.inset.height, 0.0, 0.0]);
        }

        writer.finish()
    }
}

#[derive(Debug, Clone)]
#[repr(C)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BorderInstance {
    pub task_origin: DevicePoint,
    pub flags: i32,
    pub gpu_data_address: GpuBufferAddress,
    pub clip_params: [f32; 8],
}

// 16 bytes per instance should be enough for anyone!
#[repr(C)]
#[derive(Debug, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PrimitiveInstanceData {
    data: [i32; 4],
}

// Keep these in sync with the corresponding #defines in shared.glsl
/// Specifies that an RGB CompositeInstance or ScalingInstance's UV coordinates are normalized.
const UV_TYPE_NORMALIZED: u32 = 0;
/// Specifies that an RGB CompositeInstance or ScalingInstance's UV coordinates are not normalized.
const UV_TYPE_UNNORMALIZED: u32 = 1;

/// Vertex format for picture cache composite shader.
/// When editing the members, update desc::COMPOSITE
/// so its list of instance_attributes matches:
#[derive(Clone, Debug)]
#[repr(C)]
pub struct CompositeInstance {
    // Device space destination rectangle of surface
    rect: DeviceRect,
    // Device space destination clip rect for this surface
    clip_rect: DeviceRect,
    // Color for solid color tiles, white otherwise
    color: PremultipliedColorF,

    // Packed into a single vec4 (aParams)
    _padding: f32,
    color_space_or_uv_type: f32, // YuvColorSpace for YUV;
                                 // UV coordinate space for RGB
    yuv_format: f32,            // YuvFormat
    yuv_channel_bit_depth: f32,

    // UV rectangles (pixel space) for color / yuv texture planes
    uv_rects: [TexelRect; 3],

    // Whether to flip the x and y axis respectively, where 0.0 is no-flip and 1.0 is flip.
    flip: (f32, f32),

    // Optional rounded rect clip to apply during compositing
    rounded_clip_rect: DeviceRect,
    rounded_clip_radii: [f32; 4],
}

impl CompositeInstance {
    pub fn new(
        rect: DeviceRect,
        clip_rect: DeviceRect,
        color: PremultipliedColorF,
        flip: (bool, bool),
        clip: Option<&CompositorClip>,
    ) -> Self {
        let uv = TexelRect::new(0.0, 0.0, 1.0, 1.0);

        let (rounded_clip_rect, rounded_clip_radii) = Self::vertex_clip_params(clip, rect);

        CompositeInstance {
            rect,
            clip_rect,
            color,
            _padding: 0.0,
            color_space_or_uv_type: pack_as_float(UV_TYPE_NORMALIZED),
            yuv_format: 0.0,
            yuv_channel_bit_depth: 0.0,
            uv_rects: [uv, uv, uv],
            flip: (flip.0.into(), flip.1.into()),
            rounded_clip_rect,
            rounded_clip_radii,
        }
    }

    pub fn new_rgb(
        rect: DeviceRect,
        clip_rect: DeviceRect,
        color: PremultipliedColorF,
        uv_rect: TexelRect,
        normalized_uvs: bool,
        flip: (bool, bool),
        clip: Option<&CompositorClip>,
    ) -> Self {
        let (rounded_clip_rect, rounded_clip_radii) = Self::vertex_clip_params(clip, rect);

        let uv_type = match normalized_uvs {
            true => UV_TYPE_NORMALIZED,
            false => UV_TYPE_UNNORMALIZED,
        };
        CompositeInstance {
            rect,
            clip_rect,
            color,
            _padding: 0.0,
            color_space_or_uv_type: pack_as_float(uv_type),
            yuv_format: 0.0,
            yuv_channel_bit_depth: 0.0,
            uv_rects: [uv_rect, uv_rect, uv_rect],
            flip: (flip.0.into(), flip.1.into()),
            rounded_clip_rect,
            rounded_clip_radii,
        }
    }

    pub fn new_yuv(
        rect: DeviceRect,
        clip_rect: DeviceRect,
        yuv_color_space: YuvRangedColorSpace,
        yuv_format: YuvFormat,
        yuv_channel_bit_depth: u32,
        uv_rects: [TexelRect; 3],
        flip: (bool, bool),
        clip: Option<&CompositorClip>,
    ) -> Self {

        let (rounded_clip_rect, rounded_clip_radii) = Self::vertex_clip_params(clip, rect);

        CompositeInstance {
            rect,
            clip_rect,
            color: PremultipliedColorF::WHITE,
            _padding: 0.0,
            color_space_or_uv_type: pack_as_float(yuv_color_space as u32),
            yuv_format: pack_as_float(yuv_format as u32),
            yuv_channel_bit_depth: pack_as_float(yuv_channel_bit_depth),
            uv_rects,
            flip: (flip.0.into(), flip.1.into()),
            rounded_clip_rect,
            rounded_clip_radii,
        }
    }

    // Returns the CompositeFeatures that can be used to composite
    // this RGB instance.
    pub fn get_rgb_features(&self) -> CompositeFeatures {
        let mut features = CompositeFeatures::empty();

        // If the UV rect covers the entire texture then we can avoid UV clamping.
        // We should try harder to determine this for unnormalized UVs too.
        if self.color_space_or_uv_type == pack_as_float(UV_TYPE_NORMALIZED)
            && self.uv_rects[0] == TexelRect::new(0.0, 0.0, 1.0, 1.0)
        {
            features |= CompositeFeatures::NO_UV_CLAMP;
        }

        if self.color == PremultipliedColorF::WHITE {
            features |= CompositeFeatures::NO_COLOR_MODULATION
        }

        // If all the clip radii are <= 0.0, then we don't need clip masking
        if self.rounded_clip_radii.iter().all(|r| *r <= 0.0) {
            features |= CompositeFeatures::NO_CLIP_MASK
        }

        features
    }

    // Returns the CompositeFeatures that can be used to composite
    // this YUV instance.
    pub fn get_yuv_features(&self) -> CompositeFeatures {
        let mut features = CompositeFeatures::empty();

        // If all the clip radii are <= 0.0, then we don't need clip masking
        if self.rounded_clip_radii.iter().all(|r| *r <= 0.0) {
            features |= CompositeFeatures::NO_CLIP_MASK
        }

        features
    }

    fn vertex_clip_params(
        clip: Option<&CompositorClip>,
        default_rect: DeviceRect,
    ) -> (DeviceRect, [f32; 4]) {
        match clip {
            Some(clip) => {
                (
                    clip.rect.cast_unit(),
                    [
                        clip.radius.top_left.width,
                        clip.radius.bottom_left.width,
                        clip.radius.top_right.width,
                        clip.radius.bottom_right.width,
                    ],
                )
            }
            None => {
                (default_rect, [0.0; 4])
            }
        }
    }
}

/// Vertex format for issuing colored quads.
#[derive(Debug, Clone)]
#[repr(C)]
pub struct ClearInstance {
    pub rect: [f32; 4],
    pub color: [f32; 4],
}

#[derive(Debug, Copy, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PrimitiveHeaderIndex(pub i32);

#[derive(Debug)]
#[repr(C)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PrimitiveHeaders {
    // The integer-type headers for a primitive.
    pub headers_int: FrameVec<PrimitiveHeaderI>,
    // The float-type headers for a primitive.
    pub headers_float: FrameVec<PrimitiveHeaderF>,
}

impl PrimitiveHeaders {
    pub fn new(memory: &FrameMemory) -> PrimitiveHeaders {
        PrimitiveHeaders {
            headers_int: memory.new_vec(),
            headers_float: memory.new_vec(),
        }
    }

    // Add a new primitive header.
    pub fn push(
        &mut self,
        prim_header: &PrimitiveHeader,
    ) -> PrimitiveHeaderIndex {
        debug_assert_eq!(self.headers_int.len(), self.headers_float.len());
        let id = self.headers_float.len();

        self.headers_float.push(PrimitiveHeaderF {
            pattern_rect: prim_header.pattern_rect,
            bounds: prim_header.bounds,
        });

        self.headers_int.push(PrimitiveHeaderI {
            z: prim_header.z,
            render_task_address: prim_header.render_task_address,
            specific_prim_address: prim_header.specific_prim_address,
            transform_id: prim_header.transform_id,
            user_data: prim_header.user_data,
        });

        PrimitiveHeaderIndex(id as i32)
    }
}

// This is a convenience type used to make it easier to pass
// the common parts around during batching.
#[derive(Debug)]
pub struct PrimitiveHeader {
    /// Situates the primitive's content. For a text run this is the run
    /// anchor: only `.min` is read by the shader, the extent is unused.
    pub pattern_rect: LayoutRect,
    /// The primitive's coverage rect: the only rect that clips it.
    pub bounds: LayoutRect,
    pub specific_prim_address: i32,
    pub transform_id: GpuTransformId,
    pub z: ZBufferId,
    pub render_task_address: RenderTaskAddress,
    pub user_data: [i32; 4],
}

// f32 parts of a primitive header
#[derive(Debug)]
#[repr(C)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PrimitiveHeaderF {
    pub pattern_rect: LayoutRect,
    pub bounds: LayoutRect,
}

// i32 parts of a primitive header
// TODO(gw): Compress parts of these down to u16
#[derive(Debug)]
#[repr(C)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct PrimitiveHeaderI {
    pub z: ZBufferId,
    pub specific_prim_address: i32,
    pub transform_id: GpuTransformId,
    pub render_task_address: RenderTaskAddress,
    pub user_data: [i32; 4],
}

pub struct GlyphInstance {
    pub prim_header_index: PrimitiveHeaderIndex,
}

impl GlyphInstance {
    pub fn new(
        prim_header_index: PrimitiveHeaderIndex,
    ) -> Self {
        GlyphInstance {
            prim_header_index,
        }
    }

    // TODO(gw): Some of these fields can be moved to the primitive
    //           header since they are constant, and some can be
    //           compressed to a smaller size.
    pub fn build(&self,
        clip_task: RenderTaskAddress,
        subpx_dir: SubpixelDirection,
        glyph_index_in_text_run: i32,
        glyph_uv_rect: GpuBufferAddress,
        color_mode: ShaderColorMode,
        subpx_offset_x: u8,
        subpx_offset_y: u8,
        is_packed_glyph: bool,
        is_bitmap_strike: bool,
    ) -> PrimitiveInstanceData {
        // Pack subpixel offsets and multi-variant flag into upper 16 bits of data[2]
        // After instance.flags extraction (>> 16), shader sees:
        // bits 0-3: color_mode, bits 4-5: subpx_offset_x, bits 6-7: subpx_offset_y,
        // bits 8-9: subpx_dir, bit 10: is_packed_glyph, bit 11: is_bitmap_strike
        let packed_flags = (((is_bitmap_strike as u32) & 0x1) << 27)
            | (((is_packed_glyph as u32) & 0x1) << 26)
            | (((subpx_dir as u32) & 0x3) << 24)
            | (((subpx_offset_y as u32) & 0x3) << 22)
            | (((subpx_offset_x as u32) & 0x3) << 20)
            | (((color_mode as u32) & 0xF) << 16);

        PrimitiveInstanceData {
            data: [
                self.prim_header_index.0 as i32,
                clip_task.0 as i32,
                packed_flags as i32 | glyph_index_in_text_run,
                glyph_uv_rect.as_int(),
            ],
        }
    }
}

pub struct SplitCompositeInstance {
    pub prim_header_index: PrimitiveHeaderIndex,
    pub polygons_address: i32,
    pub z: ZBufferId,
    pub render_task_address: RenderTaskAddress,
}

impl From<SplitCompositeInstance> for PrimitiveInstanceData {
    fn from(instance: SplitCompositeInstance) -> Self {
        PrimitiveInstanceData {
            data: [
                instance.prim_header_index.0,
                instance.polygons_address,
                instance.z.0,
                instance.render_task_address.0,
            ],
        }
    }
}

#[derive(Copy, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct QuadInstance {
    pub dst_task_address: RenderTaskAddress,
    pub prim_address_i: i32,
    pub prim_address_f: i32,
    pub quad_flags: u8,
    pub edge_flags: u8,
    pub part_index: u8,
    pub segment_index: u8,
}

impl From<PathTileInstance> for PrimitiveInstanceData {
    fn from(instance: PathTileInstance) -> Self {
        PrimitiveInstanceData {
            data: instance.data.map(|v| v as i32),
        }
    }
}

impl From<QuadInstance> for PrimitiveInstanceData {
    fn from(instance: QuadInstance) -> Self {
        /*
            [32 prim address_i]
            [32 prim address_f]
            [8888 qf ef pi si]
            [32 render task address]
        */

        PrimitiveInstanceData {
            data: [
                instance.prim_address_i,
                instance.prim_address_f,

                ((instance.quad_flags as i32)    << 24) |
                ((instance.edge_flags as i32)    << 16) |
                ((instance.part_index as i32)    <<  8) |
                ((instance.segment_index as i32) <<  0),

                instance.dst_task_address.0,
            ],
        }
    }
}

/// Matches QuadHeader in ps_quad.glsl
pub struct QuadHeader {
    pub transform_id: GpuTransformId,
    pub z_id: ZBufferId,
    pub pattern_input: PatternShaderInput,
}

impl GpuBufferDataI for QuadHeader {
    const NUM_BLOCKS: usize = 1;
    fn write(&self, writer: &mut GpuBufferWriterI) {
        writer.push_one([
            self.transform_id.0 as i32,
            self.z_id.0,
            self.pattern_input.0,
            self.pattern_input.1,
        ]);
    }
}

/// Matches QuadPrimitive in ps_quad.glsl
pub struct QuadPrimitive {
    /// The (clipped) coverage rect: the local rect intersected with the local
    /// clip rect. There is no separate clip rect; it is folded in here.
    pub bounds: LayoutOrDeviceRect,
    /// The rect that situates the source pattern.
    pub pattern_rect: LayoutOrDeviceRect,
    // TODO: This gets translated into a Rect just before upload.
    // It would be better to send the gpu buffer address to the shader.
    pub input_task: RenderTaskId,
    pub pattern_scale_offset: ScaleOffset,
    /// Base color of the pattern.
    pub color: PremultipliedColorF,
}

impl GpuBufferDataF for QuadPrimitive {
    const NUM_BLOCKS: usize = 5;
    fn write(&self, writer: &mut GpuBufferWriterF) {
        writer.push_one(self.bounds);
        writer.push_one(self.pattern_rect);
        writer.push_render_task(self.input_task);
        writer.push_one(self.pattern_scale_offset);
        writer.push_one(self.color);
    }
}

pub const VECS_PER_QUAD_SEGMENT: usize = 2;

/// Matches QuadSegment in ps_quad.glsl
#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct QuadSegment {
    pub rect: LayoutOrDeviceRect,
    // TODO: This gets translated into a Rect just before upload.
    // It would be better to send the gpu buffer address to the shader.
    pub task_id: RenderTaskId,
}

impl GpuBufferDataF for QuadSegment {
    const NUM_BLOCKS: usize = VECS_PER_QUAD_SEGMENT;
    fn write(&self, writer: &mut GpuBufferWriterF) {
        writer.push_one(self.rect);
        writer.push_render_task(self.task_id)
    }
}


/// The coordinate space that the clip geometry (the quad rect) is relative to.
///
/// Not to confuse with the coordinate space of the primitive's pattern, for example
/// the rounded rect, which is already relative to clip's spatial node.
#[derive(Copy, Debug, Clone, PartialEq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[repr(u32)]
pub enum ClipSpace {
    Device = 0,
    Primitive = 1,
}

impl ClipSpace {
    pub fn as_int(self) -> u32 {
        match self {
            ClipSpace::Device => 0,
            ClipSpace::Primitive => 1,
        }
    }
}

#[repr(C)]
#[derive(Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct MaskInstance {
    pub prim: PrimitiveInstanceData,
    pub clip_transform_id: GpuTransformId,
    pub clip_address: i32,
    pub clip_space: u32,
    pub unused: i32,
}


// Texture cache resources can be either a simple rect, or define
// a polygon within a rect by specifying a UV coordinate for each
// corner. This is useful for rendering screen-space rasterized
// off-screen surfaces.
#[derive(Debug, Copy, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum UvRectKind {
    // The 2d bounds of the texture cache entry define the
    // valid UV space for this texture cache entry.
    Rect,
    // The four vertices below define a quad within
    // the texture cache entry rect. The shader can
    // use a bilerp() to correctly interpolate a
    // UV coord in the vertex shader.
    Quad {
        top_left: DeviceHomogeneousVector,
        top_right: DeviceHomogeneousVector,
        bottom_left: DeviceHomogeneousVector,
        bottom_right: DeviceHomogeneousVector,
    },
}

#[derive(Debug, Copy, Clone)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct ImageSource {
    pub p0: DevicePoint,
    pub p1: DevicePoint,
    // TODO: It appears that only glyphs make use of user_data (to store glyph offset
    // and scale).
    // Perhaps we should separate the two so we don't have to push an empty unused vec4
    // for all image sources.
    pub user_data: [f32; 4],
    pub uv_rect_kind: UvRectKind,
}

impl ImageSource {
    pub fn write_gpu_blocks(&self, gpu_buffer: &mut GpuBufferBuilderF) -> GpuBufferHandle {
        let mut writer = gpu_buffer.write_blocks(6);
        self.push_gpu_blocks(&mut writer);
        writer.finish_with_handle()
    }

    pub fn push_gpu_blocks(&self, writer: &mut GpuBufferWriterF) {
        // see fetch_image_resource in GLSL
        // has to be VECS_PER_IMAGE_RESOURCE vectors
        writer.push_one([
            self.p0.x,
            self.p0.y,
            self.p1.x,
            self.p1.y,
        ]);
        writer.push_one(self.user_data);

        // If this is a polygon uv kind, then upload the four vertices.
        if let UvRectKind::Quad { top_left, top_right, bottom_left, bottom_right } = self.uv_rect_kind {
            // see fetch_image_resource_extra in GLSL
            //Note: we really need only 3 components per point here: X, Y, and W
            fn to_array(v: HomogeneousVector<f32, DevicePixel>) -> [f32; 4] {
                [v.x, v.y, v.z, v.w]
            }
            writer.push_one(to_array(top_left));
            writer.push_one(to_array(top_right));
            writer.push_one(to_array(bottom_left));
            writer.push_one(to_array(bottom_right));
        }
    }
}

