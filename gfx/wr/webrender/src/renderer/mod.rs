/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! The high-level module responsible for interfacing with the GPU.
//!
//! Much of WebRender's design is driven by separating work into different
//! threads. To avoid the complexities of multi-threaded GPU access, we restrict
//! all communication with the GPU to one thread, the render thread. But since
//! issuing GPU commands is often a bottleneck, we move everything else (i.e.
//! the computation of what commands to issue) to another thread, the
//! RenderBackend thread. The RenderBackend, in turn, may delegate work to other
//! thread (like the SceneBuilder threads or Rayon workers), but the
//! Render-vs-RenderBackend distinction is the most important.
//!
//! The consumer is responsible for initializing the render thread before
//! calling into WebRender, which means that this module also serves as the
//! initial entry point into WebRender, and is responsible for spawning the
//! various other threads discussed above. That said, WebRender initialization
//! returns both the `Renderer` instance as well as a channel for communicating
//! directly with the `RenderBackend`. Aside from a few high-level operations
//! like 'render now', most of interesting commands from the consumer go over
//! that channel and operate on the `RenderBackend`.
//!
//! ## Space conversion guidelines
//! At this stage, we shuld be operating with `DevicePixel` and `FramebufferPixel` only.
//! "Framebuffer" space represents the final destination of our rendeing,
//! and it happens to be Y-flipped on OpenGL. The conversion is done as follows:
//!   - for rasterized primitives, the orthographics projection transforms
//! the content rectangle to -1 to 1
//!   - the viewport transformation is setup to map the whole range to
//! the framebuffer rectangle provided by the document view, stored in `DrawTarget`
//!   - all the direct framebuffer operations, like blitting, reading pixels, and setting
//! up the scissor, are accepting already transformed coordinates, which we can get by
//! calling `DrawTarget::to_framebuffer_rect`

use api::{ColorF, MixBlendMode, TextureCacheCategory};
use api::{DocumentId, Epoch, ExternalImageHandler, RenderReasons};
use api::{PipelineId, Checkpoint, NotificationRequest, ImageBufferKind};
use api::{FramePublishId, ImageFormat, RenderBackendId, Parameter, IntParameter};
#[cfg(any(feature = "capture", feature = "replay"))]
use api::{ExternalImageSource, ExternalImageType, ExternalTextureHandle};
#[cfg(feature = "replay")]
use api::{ExternalImage, ExternalImageId};
use api::units::*;
use api::channel::{Sender, Receiver};
pub use api::DebugFlags;
use core::time::Duration;

use crate::pattern::PatternKind;
use crate::render_api::{DebugCommand, ApiMsg, MemoryReport};
use crate::batch::{AlphaBatchContainer, BatchKind, BatchFeatures, BatchTextures, TextureSet};
use crate::batch::ClipMaskInstanceList;
#[cfg(any(feature = "capture", feature = "replay"))]
use crate::capture::{CaptureConfig, ExternalCaptureImage, PlainExternalImage};
use crate::composite::{CompositeState, CompositeTileSurface};
use crate::composite::{CompositorKind, Compositor, NativeTileId};
use crate::composite::{CompositorConfig, NativeSurfaceOperationDetails, NativeSurfaceId, NativeSurfaceOperation};
#[cfg(feature = "debugger")]
use api::debugger::{CompositorDebugInfo, DebuggerTextureContent};
#[cfg(feature = "debugger")]
use api::debugger::{ShaderDiagnostic, ShaderFileInfo, ShaderListReply, ShaderReloadReply};
#[cfg(feature = "debugger")]
use api::debugger::{ShaderSourceReply, ShaderStage, ShaderVariantInfo};
use crate::debug_colors;
use crate::device::{DepthFunction, Device, DrawTarget, ExternalTexture, GpuFrameId, GraphicsApiInfo, UploadBufferPool};
use crate::device::{LoadOp, ReadTarget, RenderPassDescriptor, ShaderError, StoreOp, Texture, TextureFilter, TextureFlags, TextureSlot, Texel};
use crate::device::query::{GpuSampler, GpuTimer};
use crate::debug_item::DebugItem;
use crate::frame_builder::Frame;
use glyph_rasterizer::GlyphFormat;
use crate::gpu_types::{ScalingInstance, SVGFEFilterInstance, CopyInstance, PrimitiveInstanceData};
use crate::gpu_types::{BlurInstance, ClearInstance};
use crate::internal_types::{TextureSource, TextureSourceExternal, FrameVec};
#[cfg(any(feature = "capture", feature = "replay"))]
use crate::internal_types::DebugOutput;
use crate::internal_types::{CacheTextureId, FastHashMap, FastHashSet, RenderedDocument, ResultMsg};
use crate::internal_types::{TextureCacheAllocInfo, TextureCacheAllocationKind, TextureUpdateList};
use crate::internal_types::{RenderTargetInfo, Swizzle, DeferredResolveIndex};
use crate::picture::ResolvedSurfaceTexture;
use crate::profiler::{self, RenderCommandLog, GpuProfileTag, TransactionProfile};
use crate::profiler::{Profiler, ProfileCounterValue, add_event_marker, add_text_marker, thread_is_being_profiled};
use crate::device::query::GpuProfiler;
use crate::render_task_graph::RenderTaskGraph;
use crate::render_task::{RenderTask, RenderTaskKind, ReadbackTask};
use crate::screen_capture::AsyncScreenshotGrabber;
use crate::render_target::{RenderTarget, PictureCacheTarget, PictureCacheTargetKind};
use crate::render_target::{RenderTargetKind, BlitJob};
use crate::telemetry::Telemetry;
use crate::tile_cache::PictureCacheDebugInfo;
use crate::util::drain_filter;
#[cfg(feature = "debugger")]
use crate::debugger::{Debugger, DebugQueryKind};
use upload::{upload_to_texture_cache, UploadTexturePool};
use init::*;

use euclid::{Transform3D, Scale, default};
use malloc_size_of::MallocSizeOfOps;

use std::sync::Arc;

use std::{
    cell::RefCell,
    collections::VecDeque,
    f32,
    ffi::c_void,
    mem,
    num::NonZeroUsize,
    path::PathBuf,
    rc::Rc,
};
#[cfg(any(feature = "capture", feature = "replay"))]
use std::collections::hash_map::Entry;

mod composite;
mod debug;
mod external_image;
mod gpu_buffer;
mod layer_tree;
mod shade;
mod vertex;
mod upload;
pub(crate) mod init;

use composite::LayerCompositorFrameState;
use debug::DebugOverlayState;

pub use debug::DebugRenderer;
pub use shade::{PendingShadersToPrecache, Shaders, SharedShaders};
pub use vertex::{desc, VertexArrayKind, MAX_VERTEX_TEXTURE_WIDTH};
pub use gpu_buffer::{GpuBuffer, GpuBufferF, GpuBufferBuilderF, GpuBufferI, GpuBufferBuilderI};
pub use gpu_buffer::{GpuBufferHandle, GpuBufferAddress, GpuBufferBuilder, GpuBufferWriterF};
pub use gpu_buffer::{GpuBufferDataF, GpuBufferDataI, GpuBufferWriterI};
use gpu_buffer::{GpuBufferBlockF, GpuBufferBlockI};
pub use gpu_buffer::{GpuBufferBlockEdge, GpuBufferEdges, PathEdgeBufferBuilder};

/// The size of the array of each type of vertex data texture that
/// is round-robin-ed each frame during bind_frame_data. Doing this
/// helps avoid driver stalls while updating the texture in some
/// drivers. The size of these textures are typically very small
/// (e.g. < 16 kB) so it's not a huge waste of memory. Despite that,
/// this is a short-term solution - we want to find a better way
/// to provide this frame data, which will likely involve some
/// combination of UBO/SSBO usage. Although this only affects some
/// platforms, it's enabled on all platforms to reduce testing
/// differences between platforms.
pub const VERTEX_DATA_TEXTURE_COUNT: usize = 3;

/// Number of GPU blocks per UV rectangle provided for an image.
pub const BLOCKS_PER_UV_RECT: usize = 2;

const GPU_TAG_BRUSH_YUV_IMAGE: GpuProfileTag = GpuProfileTag {
    label: "B_YuvImage",
    color: debug_colors::DARKGREEN,
};
const GPU_TAG_MIXBLEND: GpuProfileTag = GpuProfileTag {
    label: "B_MixBlend",
    color: debug_colors::MAGENTA,
};
const GPU_TAG_CACHE_CLIP: GpuProfileTag = GpuProfileTag {
    label: "C_Clip",
    color: debug_colors::PURPLE,
};
const GPU_TAG_CACHE_BORDER: GpuProfileTag = GpuProfileTag {
    label: "C_Border",
    color: debug_colors::CORNSILK,
};
const GPU_TAG_CACHE_LINE_DECORATION: GpuProfileTag = GpuProfileTag {
    label: "C_LineDecoration",
    color: debug_colors::YELLOWGREEN,
};
const GPU_TAG_GRADIENT: GpuProfileTag = GpuProfileTag {
    label: "C_Gradient",
    color: debug_colors::BROWN,
};
const GPU_TAG_REPEAT: GpuProfileTag = GpuProfileTag {
    label: "Repeat",
    color: debug_colors::CHARTREUSE,
};
const GPU_TAG_SETUP_TARGET: GpuProfileTag = GpuProfileTag {
    label: "target init",
    color: debug_colors::SLATEGREY,
};
const GPU_TAG_SETUP_DATA: GpuProfileTag = GpuProfileTag {
    label: "data init",
    color: debug_colors::LIGHTGREY,
};
const GPU_TAG_PRIM_SPLIT_COMPOSITE: GpuProfileTag = GpuProfileTag {
    label: "SplitComposite",
    color: debug_colors::DARKBLUE,
};
const GPU_TAG_PRIM_TEXT_RUN: GpuProfileTag = GpuProfileTag {
    label: "TextRun",
    color: debug_colors::BLUE,
};
const GPU_TAG_PRIMITIVE: GpuProfileTag = GpuProfileTag {
    label: "Primitive",
    color: debug_colors::RED,
};
const GPU_TAG_INDIRECT_PRIM: GpuProfileTag = GpuProfileTag {
    label: "Primitive (indirect)",
    color: debug_colors::YELLOWGREEN,
};
const GPU_TAG_INDIRECT_MASK: GpuProfileTag = GpuProfileTag {
    label: "Mask (indirect)",
    color: debug_colors::IVORY,
};
const GPU_TAG_BLUR: GpuProfileTag = GpuProfileTag {
    label: "Blur",
    color: debug_colors::VIOLET,
};
const GPU_TAG_BLIT: GpuProfileTag = GpuProfileTag {
    label: "Blit",
    color: debug_colors::LIME,
};
const GPU_TAG_SCALE: GpuProfileTag = GpuProfileTag {
    label: "Scale",
    color: debug_colors::GHOSTWHITE,
};
const GPU_SAMPLER_TAG_ALPHA: GpuProfileTag = GpuProfileTag {
    label: "Alpha targets",
    color: debug_colors::BLACK,
};
pub const GPU_SAMPLER_TAG_OPAQUE: GpuProfileTag = GpuProfileTag {
    label: "Opaque pass",
    color: debug_colors::BLACK,
};
pub const GPU_SAMPLER_TAG_TRANSPARENT: GpuProfileTag = GpuProfileTag {
    label: "Transparent pass",
    color: debug_colors::BLACK,
};
const GPU_TAG_SVG_FILTER_NODES: GpuProfileTag = GpuProfileTag {
    label: "SvgFilterNodes",
    color: debug_colors::LEMONCHIFFON,
};
pub const GPU_TAG_COMPOSITE: GpuProfileTag = GpuProfileTag {
    label: "Composite",
    color: debug_colors::TOMATO,
};


/// The clear color used for the texture cache when the debug display is enabled.
/// We use a shade of blue so that we can still identify completely blue items in
/// the texture cache.
pub const TEXTURE_CACHE_DBG_CLEAR_COLOR: [f32; 4] = [0.0, 0.0, 0.8, 1.0];

impl BatchKind {
    fn sampler_tag(&self) -> GpuProfileTag {
        match *self {
            BatchKind::SplitComposite => GPU_TAG_PRIM_SPLIT_COMPOSITE,
            BatchKind::TextRun(_) => GPU_TAG_PRIM_TEXT_RUN,
            BatchKind::Quad(PatternKind::ColorOrTexture) => GPU_TAG_PRIMITIVE,
            BatchKind::Quad(PatternKind::TextureExternal) => GPU_TAG_PRIMITIVE,
            BatchKind::Quad(PatternKind::TextureExternalBT709) => GPU_TAG_PRIMITIVE,
            BatchKind::Quad(PatternKind::TextureRect) => GPU_TAG_PRIMITIVE,
            BatchKind::Quad(PatternKind::Gradient) => GPU_TAG_GRADIENT,
            BatchKind::Quad(PatternKind::Repeat) => GPU_TAG_REPEAT,
            BatchKind::Quad(PatternKind::BoxShadow) => GPU_TAG_PRIMITIVE,
            BatchKind::Quad(PatternKind::BoxShadowSuperellipse) => GPU_TAG_PRIMITIVE,
            BatchKind::Quad(PatternKind::Yuv) => GPU_TAG_BRUSH_YUV_IMAGE,
            BatchKind::Quad(PatternKind::YuvTextureExternal) => GPU_TAG_BRUSH_YUV_IMAGE,
            BatchKind::Quad(PatternKind::YuvTextureExternalBT709) => GPU_TAG_BRUSH_YUV_IMAGE,
            BatchKind::Quad(PatternKind::YuvTextureRect) => GPU_TAG_BRUSH_YUV_IMAGE,
            BatchKind::Quad(PatternKind::Backdrop) => GPU_TAG_PRIMITIVE,
            BatchKind::Quad(PatternKind::Blend) => GPU_TAG_PRIMITIVE,
            BatchKind::Quad(PatternKind::MixBlend) => GPU_TAG_MIXBLEND,
            BatchKind::Quad(PatternKind::Mask) => GPU_TAG_INDIRECT_MASK,
        }
    }
}

fn flag_changed(before: DebugFlags, after: DebugFlags, select: DebugFlags) -> Option<bool> {
    if before & select != after & select {
        Some(after.contains(select))
    } else {
        None
    }
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub enum ShaderColorMode {
    Alpha = 0,
    SubpixelDualSource = 1,
    BitmapShadow = 2,
    ColorBitmap = 3,
}

impl From<GlyphFormat> for ShaderColorMode {
    fn from(format: GlyphFormat) -> ShaderColorMode {
        match format {
            GlyphFormat::Alpha |
            GlyphFormat::TransformedAlpha |
            GlyphFormat::Bitmap => ShaderColorMode::Alpha,
            GlyphFormat::Subpixel | GlyphFormat::TransformedSubpixel => {
                panic!("Subpixel glyph formats must be handled separately.");
            }
            GlyphFormat::ColorBitmap => ShaderColorMode::ColorBitmap,
        }
    }
}

/// Enumeration of the texture samplers used across the various WebRender shaders.
///
/// Each variant corresponds to a uniform declared in shader source. We only bind
/// the variants we need for a given shader, so not every variant is bound for every
/// batch.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub(crate) enum TextureSampler {
    Color0,
    Color1,
    Color2,
    TransformPalette,
    RenderTasks,
    Dither,
    PrimitiveHeadersF,
    PrimitiveHeadersI,
    ClipMask,
    GpuBufferF,
    GpuBufferI,
    PathEdges,
}

impl TextureSampler {
    pub(crate) fn color(n: usize) -> TextureSampler {
        match n {
            0 => TextureSampler::Color0,
            1 => TextureSampler::Color1,
            2 => TextureSampler::Color2,
            _ => {
                panic!("There are only 3 color samplers.");
            }
        }
    }
}

impl Into<TextureSlot> for TextureSampler {
    fn into(self) -> TextureSlot {
        match self {
            TextureSampler::Color0 => TextureSlot(0),
            TextureSampler::Color1 => TextureSlot(1),
            TextureSampler::Color2 => TextureSlot(2),
            TextureSampler::TransformPalette => TextureSlot(3),
            TextureSampler::RenderTasks => TextureSlot(4),
            TextureSampler::Dither => TextureSlot(5),
            TextureSampler::PrimitiveHeadersF => TextureSlot(6),
            TextureSampler::PrimitiveHeadersI => TextureSlot(7),
            TextureSampler::ClipMask => TextureSlot(8),
            TextureSampler::GpuBufferF => TextureSlot(9),
            TextureSampler::GpuBufferI => TextureSlot(10),
            TextureSampler::PathEdges => TextureSlot(11),
        }
    }
}

#[derive(Debug)]
pub struct GpuProfile {
    pub frame_id: GpuFrameId,
    pub paint_time_ns: u64,
}

impl GpuProfile {
    fn new(frame_id: GpuFrameId, timers: &[GpuTimer]) -> GpuProfile {
        let mut paint_time_ns = 0;
        for timer in timers {
            paint_time_ns += timer.time_ns;
        }
        GpuProfile {
            frame_id,
            paint_time_ns,
        }
    }
}

#[derive(Debug)]
pub struct CpuProfile {
    pub frame_id: GpuFrameId,
    pub backend_time_ns: u64,
    pub composite_time_ns: u64,
    pub draw_calls: usize,
}

impl CpuProfile {
    fn new(
        frame_id: GpuFrameId,
        backend_time_ns: u64,
        composite_time_ns: u64,
        draw_calls: usize,
    ) -> CpuProfile {
        CpuProfile {
            frame_id,
            backend_time_ns,
            composite_time_ns,
            draw_calls,
        }
    }
}

/// The selected partial present mode for a given frame.
#[derive(Debug, Copy, Clone)]
pub(super) enum PartialPresentMode {
    /// The device supports fewer dirty rects than the number of dirty rects
    /// that WR produced. In this case, the WR dirty rects are union'ed into
    /// a single dirty rect, that is provided to the caller.
    Single {
        dirty_rect: DeviceRect,
    },
}

struct CacheTexture {
    texture: Texture,
    category: TextureCacheCategory,
}

/// Helper struct for resolving device Textures for use during rendering passes.
///
/// Manages the mapping between the at-a-distance texture handles used by the
/// `RenderBackend` (which does not directly interface with the GPU) and actual
/// device texture handles.
struct TextureResolver {
    /// A map to resolve texture cache IDs to native textures.
    texture_cache_map: FastHashMap<CacheTextureId, CacheTexture>,

    /// Map of external image IDs to native textures.
    external_images: FastHashMap<DeferredResolveIndex, ExternalTexture>,

    /// A special 1x1 dummy texture used for shaders that expect to work with
    /// the output of the previous pass but are actually running in the first
    /// pass.
    dummy_cache_texture: Texture,
}

impl TextureResolver {
    fn new(device: &mut Device) -> TextureResolver {
        let dummy_cache_texture = device
            .create_texture(
                ImageBufferKind::Texture2D,
                ImageFormat::RGBA8,
                1,
                1,
                TextureFilter::Linear,
                None,
            );
        device.upload_texture_immediate(
            &dummy_cache_texture,
            &[0xff, 0xff, 0xff, 0xff],
        );

        TextureResolver {
            texture_cache_map: FastHashMap::default(),
            external_images: FastHashMap::default(),
            dummy_cache_texture,
        }
    }

    fn deinit(self, device: &mut Device) {
        device.delete_texture(self.dummy_cache_texture);

        for (_id, item) in self.texture_cache_map {
            device.delete_texture(item.texture);
        }
    }

    fn begin_frame(&mut self) {
    }

    fn end_pass(
        &mut self,
        device: &mut Device,
        textures_to_invalidate: &[CacheTextureId],
    ) {
        // For any texture that is no longer needed, immediately
        // invalidate it so that tiled GPUs don't need to resolve it
        // back to memory.
        for texture_id in textures_to_invalidate {
            let render_target = &self.texture_cache_map[texture_id].texture;
            device.invalidate_render_target(render_target);
        }
    }

    // Bind a source texture to the device.
    fn bind(&self, texture_id: &TextureSource, sampler: TextureSampler, device: &mut Device) -> Swizzle {
        match *texture_id {
            TextureSource::Invalid => {
                Swizzle::default()
            }
            TextureSource::Dummy => {
                let swizzle = Swizzle::default();
                device.bind_texture(sampler, &self.dummy_cache_texture, swizzle);
                swizzle
            }
            TextureSource::External(TextureSourceExternal { ref index, .. }) => {
                let texture = self.external_images
                    .get(index)
                    .expect("BUG: External image should be resolved by now");
                device.bind_external_texture(sampler, texture);
                Swizzle::default()
            }
            TextureSource::TextureCache(index, swizzle) => {
                let texture = &self.texture_cache_map[&index].texture;
                device.bind_texture(sampler, texture, swizzle);
                swizzle
            }
        }
    }

    // Get the real (OpenGL) texture ID for a given source texture.
    // For a texture cache texture, the IDs are stored in a vector
    // map for fast access.
    fn resolve(&self, texture_id: &TextureSource) -> Option<(&Texture, Swizzle)> {
        match *texture_id {
            TextureSource::Invalid => None,
            TextureSource::Dummy => {
                Some((&self.dummy_cache_texture, Swizzle::default()))
            }
            TextureSource::External(..) => {
                panic!("BUG: External textures cannot be resolved, they can only be bound.");
            }
            TextureSource::TextureCache(index, swizzle) => {
                Some((&self.texture_cache_map[&index].texture, swizzle))
            }
        }
    }

    // Retrieve the deferred / resolved UV rect if an external texture, otherwise
    // return the default supplied UV rect.
    fn get_uv_rect(
        &self,
        source: &TextureSource,
        default_value: TexelRect,
    ) -> TexelRect {
        match source {
            TextureSource::External(TextureSourceExternal { ref index, .. }) => {
                let texture = self.external_images
                    .get(index)
                    .expect("BUG: External image should be resolved by now");
                texture.get_uv_rect()
            }
            _ => {
                default_value
            }
        }
    }

    /// Returns the size of the texture in pixels
    fn get_texture_size(&self, texture: &TextureSource) -> DeviceIntSize {
        match *texture {
            TextureSource::Invalid => DeviceIntSize::zero(),
            TextureSource::TextureCache(id, _) => {
                self.texture_cache_map[&id].texture.get_dimensions()
            },
            TextureSource::External(TextureSourceExternal { index, .. }) => {
                // If UV coords are normalized then this value will be incorrect. However, the
                // texture size is currently only used to set the uTextureSize uniform, so that
                // shaders without access to textureSize() can normalize unnormalized UVs. Which
                // means this is not a problem.
                let uv_rect = self.external_images[&index].get_uv_rect();
                (uv_rect.uv1 - uv_rect.uv0).abs().to_size().to_i32()
            },
            TextureSource::Dummy => DeviceIntSize::new(1, 1),
        }
    }

    fn report_memory(&self) -> MemoryReport {
        let mut report = MemoryReport::default();

        // We're reporting GPU memory rather than heap-allocations, so we don't
        // use size_of_op.
        for item in self.texture_cache_map.values() {
            let counter = match item.category {
                TextureCacheCategory::Atlas => &mut report.atlas_textures,
                TextureCacheCategory::Standalone => &mut report.standalone_textures,
                TextureCacheCategory::PictureTile => &mut report.picture_tile_textures,
                TextureCacheCategory::RenderTarget => &mut report.render_target_textures,
            };
            *counter += item.texture.size_in_bytes();
        }

        report
    }

    fn update_profile(&self, profile: &mut TransactionProfile) {
        let mut external_image_bytes = 0;
        for img in self.external_images.values() {
            let uv_rect = img.get_uv_rect();
            // If UV coords are normalized then this value will be incorrect. This is unfortunate
            // but doesn't impact end users at all.
            let size = (uv_rect.uv1 - uv_rect.uv0).abs().to_size().to_i32();

            // Assume 4 bytes per pixels which is true most of the time but
            // not always.
            let bpp = 4;
            external_image_bytes += size.area() as usize * bpp;
        }

        profile.set(profiler::EXTERNAL_IMAGE_BYTES, profiler::bytes_to_mb(external_image_bytes));
    }

    fn get_cache_texture_mut(&mut self, id: &CacheTextureId) -> &mut Texture {
        &mut self.texture_cache_map
            .get_mut(id)
            .expect("bug: texture not allocated")
            .texture
    }
}

pub use crate::device::BlendMode;

impl BlendMode {
    /// Decides when a given mix-blend-mode can be implemented in terms of
    /// simple blending, dual-source blending, advanced blending, or not at
    /// all based on available capabilities.
    pub fn from_mix_blend_mode(
        mode: MixBlendMode,
        advanced_blend: bool,
        coherent: bool,
    ) -> Option<BlendMode> {
        // If we emulate a mix-blend-mode via simple or dual-source blending,
        // care must be taken to output alpha As + Ad*(1-As) regardless of what
        // the RGB output is to comply with the mix-blend-mode spec.
        Some(match mode {
            // If we have coherent advanced blend, just use that.
            _ if advanced_blend && coherent => BlendMode::Advanced(mode),
            // Screen can be implemented as Cs + Cd - Cs*Cd => Cs + Cd*(1-Cs)
            MixBlendMode::Screen => BlendMode::Screen,
            // Exclusion can be implemented as Cs + Cd - 2*Cs*Cd => Cs*(1-Cd) + Cd*(1-Cs)
            MixBlendMode::Exclusion => BlendMode::Exclusion,
            // PlusLighter is basically a clamped add.
            MixBlendMode::PlusLighter => BlendMode::PlusLighter,
            // Otherwise, use advanced blend without coherency if available.
            _ if advanced_blend => BlendMode::Advanced(mode),
            // If advanced blend is not available, then we have to use mix_blend.
            _ => return None,
        })
    }
}

/// Tracks buffer damage rects over a series of frames.
#[derive(Debug, Default)]
pub(crate) struct BufferDamageTracker {
    damage_rects: [DeviceRect; 4],
    current_offset: usize,
}

impl BufferDamageTracker {
    /// Sets the damage rect for the current frame. Should only be called *after*
    /// get_damage_rect() has been called to get the current backbuffer's damage rect.
    fn push_dirty_rect(&mut self, rect: &DeviceRect) {
        self.damage_rects[self.current_offset] = rect.clone();
        self.current_offset = match self.current_offset {
            0 => self.damage_rects.len() - 1,
            n => n - 1,
        }
    }

    /// Gets the damage rect for the current backbuffer, given the backbuffer's age.
    /// (The number of frames since it was previously the backbuffer.)
    /// Returns an empty rect if the buffer is valid, and None if the entire buffer is invalid.
    fn get_damage_rect(&self, buffer_age: usize) -> Option<DeviceRect> {
        match buffer_age {
            // 0 means this is a new buffer, so is completely invalid.
            0 => None,
            // 1 means this backbuffer was also the previous frame's backbuffer
            // (so must have been copied to the frontbuffer). It is therefore entirely valid.
            1 => Some(DeviceRect::zero()),
            // We must calculate the union of the damage rects since this buffer was previously
            // the backbuffer.
            n if n <= self.damage_rects.len() + 1 => {
                Some(
                    self.damage_rects.iter()
                        .cycle()
                        .skip(self.current_offset + 1)
                        .take(n - 1)
                        .fold(DeviceRect::zero(), |acc, r| acc.union(r))
                )
            }
            // The backbuffer is older than the number of frames for which we track,
            // so we treat it as entirely invalid.
            _ => None,
        }
    }
}

/// Upper bound on the number of GPU buffer blocks we keep a copy of in order to
/// detect unchanged uploads. The blocks are 16 bytes and the textures are
/// `MAX_VERTEX_TEXTURE_WIDTH` wide, so this caps the retained data at one texture
/// row, or 16kB per buffer. Anything larger is treated as always dirty, which
/// bounds both the retained copy and the comparison.
const MAX_RETAINED_GPU_BUFFER_BLOCKS: usize = MAX_VERTEX_TEXTURE_WIDTH;

fn preferred_gpu_buffer_texture_height(required_height: i32) -> i32 {
    ((required_height + 7) & !7).max(8)
}

/// How a color or alpha render target is cleared: what its render pass loads,
/// and what clearing is left to do once the pass has begun.
struct RenderTargetClear {
    color_load: LoadOp<[f32; 4]>,
    depth_load: LoadOp<f32>,
    /// A clear of part of the target, with its color and depth values.
    rect: Option<(FramebufferIntRect, Option<[f32; 4]>, Option<f32>)>,
    /// Only the target's clear rects are cleared, each to its own color.
    precise: bool,
    /// The clear rects are drawn with a shader rather than cleared.
    with_quads: bool,
}

/// The renderer is responsible for submitting to the GPU the work prepared by the
/// RenderBackend.
///
/// We have a separate `Renderer` instance for each instance of WebRender (generally
/// one per OS window), and all instances share the same thread.
pub struct Renderer {
    result_rx: Receiver<ResultMsg>,
    api_tx: Sender<ApiMsg>,
    /// Identifies this renderer's window on a potentially shared backend.
    backend_id: RenderBackendId,
    /// Keep the `RenderBackendPool` alive for the lifetime of this
    /// `Renderer`. For the private-pool case (pref=0) this is the only
    /// owner, so dropping the renderer drops the pool and triggers its
    /// `Drop` impl, which signals the backend thread to exit cleanly.
    /// For the shared-pool case (pref>=1) the C++ side also holds an
    /// `Arc`, so the pool only drops at process shutdown.
    _render_backend_pool: Arc<crate::render_backend_pool::RenderBackendPool>,
    pub device: Device,
    pending_texture_updates: Vec<TextureUpdateList>,
    /// True if there are any TextureCacheUpdate pending.
    pending_texture_cache_updates: bool,
    pending_native_surface_updates: Vec<NativeSurfaceOperation>,
    pending_shader_updates: Vec<PathBuf>,
    active_documents: FastHashMap<DocumentId, RenderedDocument>,

    shaders: Rc<RefCell<Shaders>>,

    max_recorded_profiles: usize,

    clear_color: ColorF,
    enable_clear_scissor: bool,
    enable_advanced_blend_barriers: bool,
    clear_caches_with_quads: bool,
    clear_alpha_targets_with_quads: bool,

    debug: debug::LazyInitializedDebugRenderer,
    debug_flags: DebugFlags,
    /// See IntParameter::PathAaSamples.
    path_aa_samples: i32,
    profile: TransactionProfile,
    frame_counter: u64,
    resource_upload_time: f64,
    profiler: Profiler,
    command_log: Option<RenderCommandLog>,
    #[cfg(feature = "debugger")]
    debugger: Debugger,

    last_time: u64,

    pub gpu_profiler: GpuProfiler,
    vaos: vertex::RendererVAOs,

    gpu_buffer_texture_f: Option<Texture>,
    gpu_buffer_texture_f_too_large: i32,
    gpu_buffer_last_data_f: Vec<GpuBufferBlockF>,
    gpu_buffer_texture_i: Option<Texture>,
    gpu_buffer_texture_i_too_large: i32,
    gpu_buffer_last_data_i: Vec<GpuBufferBlockI>,
    gpu_buffer_texture_edges: Option<Texture>,
    gpu_buffer_texture_edges_too_large: i32,
    gpu_buffer_last_data_edges: Vec<GpuBufferBlockEdge>,
    vertex_data: vertex::VertexDataRing,

    pipeline_info: PipelineInfo,

    // Manages and resolves source textures IDs to real texture IDs.
    texture_resolver: TextureResolver,

    texture_upload_buffer_pool: UploadBufferPool,
    staging_texture_pool: UploadTexturePool,

    dither_matrix_texture: Option<Texture>,

    /// Optional trait object that allows the client
    /// application to provide external buffers for image data.
    external_image_handler: Option<Box<dyn ExternalImageHandler>>,

    /// Optional function pointers for measuring memory used by a given
    /// heap-allocated pointer.
    size_of_ops: Option<MallocSizeOfOps>,

    pub renderer_errors: Vec<RendererError>,

    pub(in crate) async_frame_recorder: Option<AsyncScreenshotGrabber>,
    pub(in crate) async_screenshots: Option<AsyncScreenshotGrabber>,

    /// List of profile results from previous frames. Can be retrieved
    /// via get_frame_profiles().
    cpu_profiles: VecDeque<CpuProfile>,
    gpu_profiles: VecDeque<GpuProfile>,
    /// Profiler counters of the frames built by the render backend, in the
    /// order they were published. Can be retrieved via take_frame_build_profiles().
    frame_build_profiles: VecDeque<Vec<ProfileCounterValue>>,

    /// Notification requests to be fulfilled after rendering.
    notifications: Vec<NotificationRequest>,

    device_size: Option<DeviceIntSize>,

    /// A lazily created texture for the zoom debugging widget.
    zoom_debug_texture: Option<Texture>,

    /// The current mouse position. This is used for debugging
    /// functionality only, such as the debug zoom widget.
    cursor_position: DeviceIntPoint,

    /// Guards to check if we might be rendering a frame with expired texture
    /// cache entries.
    shared_texture_cache_cleared: bool,

    /// The set of documents which we've seen a publish for since last render.
    documents_seen: FastHashSet<DocumentId>,

    #[cfg(feature = "replay")]
    owned_external_images: FastHashMap<(ExternalImageId, u8), ExternalTexture>,

    /// The compositing config, affecting how WR composites into the final scene.
    compositor_config: CompositorConfig,
    /// Swapchain surfaces used by the Layer compositor.
    layer_tree: layer_tree::LayerTree,
    current_compositor_kind: CompositorKind,
    /// Limit external compositing to one visible SDR YUV tile per frame.
    limit_sdr_yuv_external_composites: bool,

    /// Maintains a set of allocated native composite surfaces. This allows any
    /// currently allocated surfaces to be cleaned up as soon as deinit() is
    /// called (the normal bookkeeping for native surfaces exists in the
    /// render backend thread).
    allocated_native_surfaces: FastHashSet<NativeSurfaceId>,

    /// If true, partial present state has been reset and everything needs to
    /// be drawn on the next render.
    force_redraw: bool,

    /// State related to the debug / profiling overlays
    debug_overlay_state: DebugOverlayState,

    /// Tracks the dirty rectangles from previous frames, to find the region
    /// of the back buffer that must be redrawn given its buffer age.
    buffer_damage_tracker: BufferDamageTracker,

    max_primitive_instance_count: usize,
    enable_instancing: bool,
    /// If true, stream instance data into large buffers shared by multiple
    /// draws, rather than allocating a new instance buffer for each draw.
    use_shared_instance_buffer: bool,

    /// Count consecutive oom frames to detectif we are stuck unable to render
    /// in a loop.
    consecutive_oom_frames: u32,

    /// update() defers processing of ResultMsg, if frame_publish_id of
    /// ResultMsg::PublishDocument exceeds target_frame_publish_id.
    target_frame_publish_id: Option<FramePublishId>,

    /// Hold a next ResultMsg that will be handled by update().
    pending_result_msg: Option<ResultMsg>,

    /// Hold previous frame compositing state with layer compositor.
    layer_compositor_frame_state_in_prev_frame: Option<LayerCompositorFrameState>,

    /// Hold DebugItems of DebugFlags::EXTERNAL_COMPOSITE_BORDERS for debug overlay
    external_composite_debug_items: Vec<DebugItem>,

    /// On-demand RenderDoc frame capture, driven from the debugger / wrshell.
    #[cfg(feature = "debugger")]
    renderdoc: crate::renderdoc::RenderDocCapture,
    /// Pending reply channel for an in-flight RenderDoc capture request; sent the
    /// written .rdc path (or an error) once the next frame has been captured.
    #[cfg(feature = "debugger")]
    renderdoc_capture_reply: Option<crate::api::channel::Sender<crate::api::debugger::RenderDocReply>>,
}

#[derive(Debug)]
pub enum RendererError {
    Shader(ShaderError),
    Thread(std::io::Error),
    MaxTextureSize,
    SoftwareRasterizer,
    OutOfMemory,
}

/// Flatten a shader build failure into the per-line diagnostics the debugger
/// client reports. A log that no driver pattern matched yields a single
/// location-less diagnostic carrying the log itself.
#[cfg(feature = "debugger")]
fn shader_diagnostics(error: &ShaderError) -> Vec<ShaderDiagnostic> {
    let stage = match error {
        ShaderError::Compilation(..) => ShaderStage::Compile,
        ShaderError::Link(..) => ShaderStage::Link,
    };
    let variant = error.name().to_string();

    if error.diagnostics().is_empty() {
        return vec![ShaderDiagnostic {
            variant,
            stage,
            file: None,
            line: None,
            column: None,
            message: error.log().to_string(),
        }];
    }

    error
        .diagnostics()
        .iter()
        .map(|entry| ShaderDiagnostic {
            variant: variant.clone(),
            stage,
            file: entry.file.clone(),
            line: entry.line,
            column: entry.column,
            message: entry.message.clone(),
        })
        .collect()
}

impl From<ShaderError> for RendererError {
    fn from(err: ShaderError) -> Self {
        RendererError::Shader(err)
    }
}

impl From<std::io::Error> for RendererError {
    fn from(err: std::io::Error) -> Self {
        RendererError::Thread(err)
    }
}

impl Renderer {
    pub fn device_size(&self) -> Option<DeviceIntSize> {
        self.device_size
    }

    /// Update the current position of the debug cursor.
    pub fn set_cursor_position(
        &mut self,
        position: DeviceIntPoint,
    ) {
        self.cursor_position = position;
    }

    pub fn get_max_texture_size(&self) -> i32 {
        self.device.max_texture_size()
    }

    pub fn get_graphics_api_info(&self) -> GraphicsApiInfo {
        self.device.api_info()
    }

    pub fn preferred_color_format(&self) -> ImageFormat {
        self.device.preferred_color_formats().external
    }

    pub fn required_texture_stride_alignment(&self, format: ImageFormat) -> usize {
        self.device.required_transfer_stride().num_bytes(format).get()
    }

    pub fn set_clear_color(&mut self, color: ColorF) {
        self.clear_color = color;
    }

    pub fn flush_pipeline_info(&mut self) -> PipelineInfo {
        mem::replace(&mut self.pipeline_info, PipelineInfo::default())
    }

    /// Returns the Epoch of the current frame in a pipeline.
    pub fn current_epoch(&self, document_id: DocumentId, pipeline_id: PipelineId) -> Option<Epoch> {
        self.pipeline_info.epochs.get(&(pipeline_id, document_id)).cloned()
    }

    fn get_next_result_msg(&mut self) -> Option<ResultMsg> {
        if self.pending_result_msg.is_none() {
            if let Ok(msg) = self.result_rx.try_recv() {
                self.pending_result_msg = Some(msg);
            }
        }

        match (&self.pending_result_msg, &self.target_frame_publish_id) {
          (Some(ResultMsg::PublishDocument(frame_publish_id, _, _, _)), Some(target_id)) => {
            if frame_publish_id > target_id {
              return None;
            }
          }
          _ => {}
        }

        self.pending_result_msg.take()
    }

    /// Processes the result queue.
    ///
    /// Should be called before `render()`, as texture cache updates are done here.
    pub fn update(&mut self) {
        tracy_rs::profile_scope!("update");

        // Pull any pending results and return the most recent.
        while let Some(msg) = self.get_next_result_msg() {
            match msg {
                ResultMsg::PublishPipelineInfo(mut pipeline_info) => {
                    for ((pipeline_id, document_id), epoch) in pipeline_info.epochs {
                        self.pipeline_info.epochs.insert((pipeline_id, document_id), epoch);
                    }
                    self.pipeline_info.removed_pipelines.extend(pipeline_info.removed_pipelines.drain(..));
                }
                ResultMsg::PublishDocument(
                    _,
                    document_id,
                    mut doc,
                    resource_update_list,
                ) => {
                    if self.max_recorded_profiles > 0 {
                        while self.frame_build_profiles.len() >= self.max_recorded_profiles {
                            self.frame_build_profiles.pop_front();
                        }
                        self.frame_build_profiles.push_back(self.profiler.counter_values(&doc.profile));
                    }

                    // Add a new document to the active set

                    // If the document we are replacing must be drawn (in order to
                    // update the texture cache), issue a render just to
                    // off-screen targets, ie pass None to render_impl. We do this
                    // because a) we don't need to render to the main framebuffer
                    // so it is cheaper not to, and b) doing so without a
                    // subsequent present would break partial present.
                    let prev_frame_memory = if let Some(mut prev_doc) = self.active_documents.remove(&document_id) {
                        doc.profile.merge(&mut prev_doc.profile);

                        if self.frame_must_be_drawn(&prev_doc) {
                            prev_doc.render_reasons |= RenderReasons::TEXTURE_CACHE_FLUSH;
                            self.render_impl(
                                document_id,
                                &mut prev_doc,
                                None,
                                0,
                            ).ok();
                        }

                        Some(prev_doc.frame.allocator_memory)
                    } else {
                        None
                    };

                    if let Some(memory) = prev_frame_memory {
                        // We just dropped the frame a few lives above. There should be no
                        // live allocations left in the frame's memory.
                        memory.assert_memory_reusable();
                    }

                    self.active_documents.insert(document_id, doc);

                    // IMPORTANT: The pending texture cache updates must be applied
                    //            *after* the previous frame has been rendered above
                    //            (if neceessary for a texture cache update). For
                    //            an example of why this is required:
                    //            1) Previous frame contains a render task that
                    //               targets Texture X.
                    //            2) New frame contains a texture cache update which
                    //               frees Texture X.
                    //            3) bad stuff happens.

                    //TODO: associate `document_id` with target window
                    self.pending_texture_cache_updates |= !resource_update_list.texture_updates.updates.is_empty();
                    self.pending_texture_updates.push(resource_update_list.texture_updates);
                    self.pending_native_surface_updates.extend(resource_update_list.native_surface_updates);
                    self.documents_seen.insert(document_id);

                    // Nothing draws the frames, so the texture updates must not
                    // wait for a render to be applied, otherwise they accumulate
                    // while frames are only built.
                    if self.debug_flags.contains(DebugFlags::SKIP_RENDERING) {
                        self.apply_pending_resource_updates();
                    }
                }
                ResultMsg::UpdateResources {
                    resource_updates,
                    memory_pressure,
                    discard_active_documents,
                    trim_upload_buffers,
                } => {
                    if memory_pressure || discard_active_documents {
                        // Resource cleanup can arrive _after_ a new scene has been
                        // published that writes persistent targets (i.e. cached
                        // render tasks to the texture cache, or picture cache tiles)
                        // but _before_ the next update/render loop. Render any such
                        // document offscreen before discarding it so those persistent
                        // targets are updated while all referenced textures still
                        // exist. Paused-window trimming also has to discard already
                        // rendered documents because their frames may reference pooled
                        // render targets that are about to be freed; Resume() forces a
                        // freshly generated frame.
                        //
                        // UpdateResources and PublishDocument share the backend's FIFO
                        // result queue, so the replacement cannot overtake these frees.
                        let active_documents = mem::replace(
                            &mut self.active_documents,
                            FastHashMap::default(),
                        );
                        for (doc_id, mut doc) in active_documents {
                            if self.frame_must_be_drawn(&doc) {
                                // As this render will not be presented, we must pass None to
                                // render_impl. This avoids interfering with partial present
                                // logic, as well as being more efficient.
                                self.render_impl(
                                    doc_id,
                                    &mut doc,
                                    None,
                                    0,
                                ).ok();
                            }
                        }
                    }

                    self.pending_texture_cache_updates |= !resource_updates.texture_updates.updates.is_empty();
                    self.pending_texture_updates.push(resource_updates.texture_updates);
                    self.pending_native_surface_updates.extend(resource_updates.native_surface_updates);
                    self.device.begin_frame();

                    self.update_texture_cache();
                    self.update_native_surfaces();

                    // The device asserts if textures are deleted outside a frame.
                    if memory_pressure || trim_upload_buffers {
                        self.trim_upload_buffers();
                    }

                    self.device.end_frame();
                }
                ResultMsg::RenderDocumentOffscreen(document_id, mut offscreen_doc, resources) => {
                    // Flush pending operations if needed (See comment in the match arm for
                    // PublishPipelineInfo).

                    // Borrow-ck dance.
                    let prev_doc = self.active_documents.remove(&document_id);
                    if let Some(mut prev_doc) = prev_doc {
                        if self.frame_must_be_drawn(&prev_doc) {
                            prev_doc.render_reasons |= RenderReasons::TEXTURE_CACHE_FLUSH;
                            self.render_impl(
                                document_id,
                                &mut prev_doc,
                                None,
                                0,
                            ).ok();
                        }

                        self.active_documents.insert(document_id, prev_doc);
                    }

                    // Now update resources and render the offscreen frame.

                    self.pending_texture_cache_updates |= !resources.texture_updates.updates.is_empty();
                    self.pending_texture_updates.push(resources.texture_updates);
                    self.pending_native_surface_updates.extend(resources.native_surface_updates);

                    if self.debug_flags.contains(DebugFlags::SKIP_RENDERING) {
                        self.apply_pending_resource_updates();
                    } else {
                        self.render_impl(
                            document_id,
                            &mut offscreen_doc,
                            None,
                            0,
                        ).unwrap();
                    }
                }
                ResultMsg::AppendNotificationRequests(mut notifications) => {
                    // We need to know specifically if there are any pending
                    // TextureCacheUpdate updates in any of the entries in
                    // pending_texture_updates. They may simply be nops, which do not
                    // need to prevent issuing the notification, and if so, may not
                    // cause a timely frame render to occur to wake up any listeners.
                    if !self.pending_texture_cache_updates {
                        drain_filter(
                            &mut notifications,
                            |n| { n.when() == Checkpoint::FrameTexturesUpdated },
                            |n| { n.notify(); },
                        );
                    }
                    self.notifications.append(&mut notifications);
                }
                ResultMsg::ForceRedraw => {
                    self.force_redraw = true;
                }
                ResultMsg::RefreshShader(path) => {
                    self.pending_shader_updates.push(path);
                }
                ResultMsg::SetParameter(ref param) => {
                    if let Parameter::Int(IntParameter::PathAaSamples, samples) = *param {
                        self.path_aa_samples = samples;
                    }
                    self.device.set_parameter(param);
                    self.profiler.set_parameter(param);
                }
                ResultMsg::DebugOutput(output) => match output {
                    #[cfg(feature = "capture")]
                    DebugOutput::SaveCapture(config, deferred) => {
                        self.save_capture(config, deferred);
                    }
                    #[cfg(feature = "replay")]
                    DebugOutput::LoadCapture(config, plain_externals) => {
                        self.active_documents.clear();
                        self.load_capture(config, plain_externals);
                    }
                },
                ResultMsg::DebugCommand(command) => {
                    self.handle_debug_command(command);
                }
            }
        }
    }

    fn frame_must_be_drawn(&self, doc: &RenderedDocument) -> bool {
        doc.frame.must_be_drawn() && !self.debug_flags.contains(DebugFlags::SKIP_RENDERING)
    }

    fn apply_pending_resource_updates(&mut self) {
        self.device.begin_frame();
        self.update_texture_cache();
        self.update_native_surfaces();
        self.device.end_frame();
    }

    /// update() defers processing of ResultMsg, if frame_publish_id of
    /// ResultMsg::PublishDocument exceeds target_frame_publish_id.
    pub fn set_target_frame_publish_id(&mut self, publish_id: FramePublishId) {
        self.target_frame_publish_id = Some(publish_id);
    }

    /// Answer a `shader-source` debug query: the raw `.glsl` file when
    /// `features` is `None`, otherwise the preprocessed source of that variant.
    #[cfg(feature = "debugger")]
    fn query_shader_source(
        &self,
        name: &str,
        features: Option<&[String]>,
    ) -> ShaderSourceReply {
        if self.device.builtin_shader_source(name).is_none() {
            return ShaderSourceReply::Error(format!("No shader named \"{}\"", name));
        }

        let features = match features {
            None => {
                return ShaderSourceReply::Source {
                    name: name.to_string(),
                    source: self.device.get_shader_source(name).to_string(),
                    is_override: self.device.shader_source_override(name).is_some(),
                };
            }
            Some(features) => features,
        };

        // Expanding a variant needs the `'static` feature strings the shader
        // was registered with, so match the request against the registered
        // variants rather than trusting the names it sent.
        let mut requested: Vec<&str> = features.iter().map(String::as_str).collect();
        requested.sort_unstable();

        let shaders = self.shaders.borrow();
        let variant = shaders.variants().iter().find(|shader| {
            shader.name() == name && shader.features() == requested.as_slice()
        });

        match variant {
            Some(variant) => {
                let (vertex, fragment) = self
                    .device
                    .expanded_shader_source(variant.name(), variant.features());
                ShaderSourceReply::Expanded {
                    variant: variant.full_name(),
                    vertex,
                    fragment,
                }
            }
            None => ShaderSourceReply::Error(format!(
                "No variant of \"{}\" with features [{}]",
                name,
                requested.join(", "),
            )),
        }
    }

    /// Replace the source of one `.glsl` file, or drop the override when
    /// `source` is `None`, and rebuild the shaders it affects.
    ///
    /// On failure the override is rolled back, so the shaders still in use and
    /// the source the client will read next stay in agreement.
    #[cfg(feature = "debugger")]
    fn set_shader_source(&mut self, name: &str, source: Option<String>) -> ShaderReloadReply {
        if !self.device.supports_shader_source_override() {
            return ShaderReloadReply::Unsupported(
                "This instance renders with SWGL, whose shaders are transpiled to C++ \
                 at build time and cannot be recompiled from GLSL"
                    .into(),
            );
        }

        if self.device.builtin_shader_source(name).is_none() {
            return ShaderReloadReply::Error(format!("No shader named \"{}\"", name));
        }

        let previous = self.device.shader_source_override(name).map(str::to_string);

        let install = |device: &mut Device, source: Option<String>| match source {
            Some(source) => device.set_shader_source_override(name, source),
            None => {
                device.clear_shader_source_override(name);
            }
        };

        self.device.begin_frame();
        install(&mut self.device, source);

        let reply = match self.shaders.borrow_mut().reload(&mut self.device, name) {
            Ok(recompiled) => ShaderReloadReply::Ok { recompiled },
            Err(errors) => {
                install(&mut self.device, previous);
                let diagnostics = errors.iter().flat_map(shader_diagnostics).collect();
                ShaderReloadReply::Errors(diagnostics)
            }
        };
        self.device.end_frame();

        reply
    }

    fn handle_debug_command(&mut self, command: DebugCommand) {
        match command {
            DebugCommand::SetPictureTileSize(_) |
            DebugCommand::SetMaximumSurfaceSize(_) |
            DebugCommand::GenerateFrame => {
                panic!("Should be handled by render backend");
            }
            #[cfg(feature = "debugger")]
            DebugCommand::SetSceneDebugOverride(..) => {
                panic!("Should be handled by render backend");
            }
            #[cfg(feature = "debugger")]
            DebugCommand::Query(ref query) => {
                match query.kind {
                    DebugQueryKind::SpatialTree { .. } |
                    DebugQueryKind::Scene { .. } => {
                        panic!("Should be handled by render backend");
                    }
                    DebugQueryKind::CompositorConfig { .. } => {
                        let result = match self.active_documents.iter().last() {
                            Some((_, doc)) => {
                                doc.frame.composite_state.print_to_string()
                            }
                            None => {
                                "No active documents".into()
                            }
                        };
                        query.result.send(result).ok();
                    }
                    DebugQueryKind::CompositorView { .. } => {
                        let result = match self.active_documents.iter().last() {
                            Some((_, doc)) => {
                                let info = CompositorDebugInfo::from(&doc.frame.composite_state);
                                serde_json::to_string(&info).unwrap()
                            }
                            None => {
                                "No active documents".into()
                            }
                        };
                        query.result.send(result).ok();
                    }
                    DebugQueryKind::Shaders { .. } => {
                        let shaders = self.shaders.borrow();
                        let files = self
                            .device
                            .shader_file_names()
                            .iter()
                            .map(|name| ShaderFileInfo {
                                name: name.to_string(),
                                overridden: self.device.shader_source_override(name).is_some(),
                            })
                            .collect();
                        let variants = shaders
                            .variants()
                            .iter()
                            .map(|shader| ShaderVariantInfo {
                                base_filename: shader.name().to_string(),
                                features: shader
                                    .features()
                                    .iter()
                                    .map(|feature| feature.to_string())
                                    .collect(),
                                compiled: shader.is_compiled(),
                            })
                            .collect();

                        let reply = ShaderListReply {
                            supported: self.device.supports_shader_source_override(),
                            files,
                            variants,
                        };
                        query.result.send(serde_json::to_string(&reply).unwrap()).ok();
                    }
                    DebugQueryKind::ShaderSource { ref name, ref features } => {
                        let reply = self.query_shader_source(name, features.as_deref());
                        query.result.send(serde_json::to_string(&reply).unwrap()).ok();
                    }
                    DebugQueryKind::Textures { category } => {
                        let mut texture_list = Vec::new();

                        self.device.begin_frame();

                        for (id, item) in &self.texture_resolver.texture_cache_map {
                            if category.is_some() && category != Some(item.category) {
                                continue;
                            }

                            let size = item.texture.get_dimensions();
                            let format = item.texture.get_format();
                            let buffer_size = (size.area() * format.bytes_per_pixel()) as usize;
                            let mut data = vec![0u8; buffer_size];
                            self.device.read_texture(&item.texture, format, &mut data);

                            let category_str = match item.category {
                                TextureCacheCategory::Atlas => "atlas",
                                TextureCacheCategory::Standalone => "standalone",
                                TextureCacheCategory::PictureTile => "tile",
                                TextureCacheCategory::RenderTarget => "target",
                            };

                            let texture_msg = DebuggerTextureContent {
                                name: format!("{category_str}-{:02}", id.0),
                                category: item.category,
                                width: size.width as u32,
                                height: size.height as u32,
                                format,
                                data,
                            };
                            texture_list.push(texture_msg);
                        }
                        self.device.end_frame();

                        query.result.send(serde_json::to_string(&texture_list).unwrap()).ok();
                    }
                }
            }
            DebugCommand::SaveCapture(..) |
            DebugCommand::LoadCapture(..) |
            DebugCommand::StartCaptureSequence(..) |
            DebugCommand::StopCaptureSequence => {
                panic!("Capture commands are not welcome here! Did you build with 'capture' feature?")
            }
            DebugCommand::ClearCaches(_)
            | DebugCommand::SimulateLongSceneBuild(_)
            | DebugCommand::SetBatchingLookback(_) => {}
            DebugCommand::SetFlags(flags) => {
                self.set_debug_flags(flags);
            }
            DebugCommand::GetDebugFlags(tx) => {
                tx.send(self.debug_flags).unwrap();
            }
            DebugCommand::SetRenderCommandLog(enabled) => {
                if enabled && self.command_log.is_none() {
                    self.command_log = Some(RenderCommandLog::new());
                } else if !enabled {
                    self.command_log = None;
                }
            }
            #[cfg(feature = "debugger")]
            DebugCommand::SetShaderSource(name, source, tx) => {
                let reply = self.set_shader_source(&name, source);
                tx.send(reply).unwrap();
            }
            #[cfg(feature = "debugger")]
            DebugCommand::AddDebugClient(client) => {
                self.debugger.add_client(
                    client,
                    self.debug_flags,
                    &self.profiler,
                );
            }
            #[cfg(feature = "debugger")]
            DebugCommand::CaptureRenderDoc(reply) => {
                if self.renderdoc.is_available() {
                    self.renderdoc.arm();
                    self.renderdoc_capture_reply = Some(reply);
                } else {
                    let _ = reply.send(crate::api::debugger::RenderDocReply::Error(
                        "RenderDoc not available (launch the host with \
                         LD_PRELOAD=librenderdoc.so)".to_string(),
                    ));
                }
            }
        }
    }

    fn trim_upload_buffers(&mut self) {
        self.texture_upload_buffer_pool.on_memory_pressure(&mut self.device);
        self.staging_texture_pool.delete_textures(&mut self.device);
        if let Some(texture) = self.gpu_buffer_texture_f.take() {
            self.device.delete_texture(texture);
        }
        self.gpu_buffer_last_data_f = Vec::new();
        if let Some(texture) = self.gpu_buffer_texture_i.take() {
            self.device.delete_texture(texture);
        }
        self.gpu_buffer_last_data_i = Vec::new();
        if let Some(texture) = self.gpu_buffer_texture_edges.take() {
            self.device.delete_texture(texture);
        }
        self.gpu_buffer_last_data_edges = Vec::new();
    }

    /// Set a callback for handling external images.
    pub fn set_external_image_handler(&mut self, handler: Box<dyn ExternalImageHandler>) {
        self.external_image_handler = Some(handler);
    }

    /// Release transient resources while preserving persistent caches.
    pub fn trim_transient_resources(&self, trim_upload_buffers: bool) {
        let _ = self.api_tx.send(ApiMsg::TrimTransientResources {
            backend_id: self.backend_id,
            trim_upload_buffers,
        });
    }

    /// Retrieve (and clear) the current list of recorded frame profiles.
    pub fn get_frame_profiles(&mut self) -> (Vec<CpuProfile>, Vec<GpuProfile>) {
        let cpu_profiles = self.cpu_profiles.drain(..).collect();
        let gpu_profiles = self.gpu_profiles.drain(..).collect();
        (cpu_profiles, gpu_profiles)
    }

    /// Retrieve (and clear) the profiler counters of the frames published since
    /// the last call, oldest first. Only recorded if max_recorded_profiles is
    /// non-zero.
    pub fn take_frame_build_profiles(&mut self) -> Vec<Vec<ProfileCounterValue>> {
        self.frame_build_profiles.drain(..).collect()
    }

    /// Mark the active frames as not rendered, so that the next call to render()
    /// draws their picture cache tiles and texture cache targets again instead
    /// of only compositing them. Useful to benchmark rendering the same frame
    /// repeatedly.
    pub fn invalidate_rendered_frames(&mut self) {
        for doc in self.active_documents.values_mut() {
            doc.frame.has_been_rendered = false;
        }
    }

    /// Reset the current partial present state. This forces the entire framebuffer
    /// to be refreshed next time `render` is called.
    pub fn force_redraw(&mut self) {
        self.force_redraw = true;
    }

    /// Renders the current frame.
    ///
    /// A Frame is supplied by calling [`generate_frame()`][webrender_api::Transaction::generate_frame].
    /// buffer_age is the number of frames since the current backbuffer was last presented.
    /// 1 means that it holds the previous frame, and 0 means that its content is unknown,
    /// which forces a full redraw. It is only relevant if partial present is active,
    /// otherwise 0 should be passed here.
    pub fn render(
        &mut self,
        device_size: DeviceIntSize,
        buffer_age: usize,
    ) -> Result<RenderResults, Vec<RendererError>> {
        self.device_size = Some(device_size);

        // TODO(gw): We want to make the active document that is
        //           being rendered configurable via the public
        //           API in future. For now, just select the last
        //           added document as the active one to render
        //           (Gecko only ever creates a single document
        //           per renderer right now).
        let doc_id = self.active_documents.keys().last().cloned();

        let result = match doc_id {
            Some(_) if self.debug_flags.contains(DebugFlags::SKIP_RENDERING) => {
                self.apply_pending_resource_updates();
                self.last_time = zeitstempel::now();
                Ok(RenderResults::default())
            }
            Some(doc_id) => {
                // Remove the doc from the map to appease the borrow checker
                let mut doc = self.active_documents
                    .remove(&doc_id)
                    .unwrap();

                let size = if !device_size.is_empty() {
                    Some(device_size)
                } else {
                    None
                };

                #[cfg(feature = "debugger")]
                let capture = self.renderdoc.take_request();
                #[cfg(feature = "debugger")]
                if capture {
                    self.renderdoc.start();
                }

                let result = self.render_impl(
                    doc_id,
                    &mut doc,
                    size,
                    buffer_age,
                );

                #[cfg(feature = "debugger")]
                if capture {
                    let path = self.renderdoc.end();
                    if let Some(reply) = self.renderdoc_capture_reply.take() {
                        let result = match path {
                            Some(p) => crate::api::debugger::RenderDocReply::Path(
                                p.to_string_lossy().into_owned()
                            ),
                            None => crate::api::debugger::RenderDocReply::Error(
                                "RenderDoc capture failed (launch the host with \
                                 LD_PRELOAD=librenderdoc.so)".to_string()
                            ),
                        };
                        let _ = reply.send(result);
                    }
                }

                self.active_documents.insert(doc_id, doc);

                result
            }
            None => {
                self.last_time = zeitstempel::now();
                Ok(RenderResults::default())
            }
        };

        drain_filter(
            &mut self.notifications,
            |n| { n.when() == Checkpoint::FrameRendered },
            |n| { n.notify(); },
        );

        let mut oom = false;
        if let Err(ref errors) = result {
            for error in errors {
                if matches!(error, &RendererError::OutOfMemory) {
                    oom = true;
                    break;
                }
            }
        }

        if oom {
            let _ = self.api_tx.send(ApiMsg::MemoryPressure);
            // Ensure we don't get stuck in a loop.
            self.consecutive_oom_frames += 1;
            assert!(self.consecutive_oom_frames < 5, "Renderer out of memory");
        } else {
            self.consecutive_oom_frames = 0;
        }

        // This is the end of the rendering pipeline. If some notifications are is still there,
        // just clear them and they will autimatically fire the Checkpoint::TransactionDropped
        // event. Otherwise they would just pile up in this vector forever.
        self.notifications.clear();

        self.external_composite_debug_items = Vec::new();

        tracy_rs::tracy_frame_marker!();

        result
    }

    // If device_size is None, don't render to the main frame buffer. This is useful to
    // update texture cache render tasks but avoid doing a full frame render. If the
    // render is not going to be presented, then this must be set to None, as performing a
    // composite without a present will confuse partial present.
    fn render_impl(
        &mut self,
        doc_id: DocumentId,
        active_doc: &mut RenderedDocument,
        mut device_size: Option<DeviceIntSize>,
        buffer_age: usize,
    ) -> Result<RenderResults, Vec<RendererError>> {
        tracy_rs::profile_scope!("render");
        let mut results = RenderResults::default();
        self.profile.end_time_if_started(profiler::FRAME_SEND_TIME);
        self.profile.start_time(profiler::RENDERER_TIME);

        if let Some(log) = &mut self.command_log {
            log.clear();
        }

        self.staging_texture_pool.begin_frame();

        // The texture resolver scope should be outside of any rendering, including
        // debug rendering. This ensures that when we return render targets to the
        // pool via glInvalidateFramebuffer, we don't do any debug rendering after
        // that point. Otherwise, the bind / invalidate / bind logic trips up the
        // render pass logic in tiled / mobile GPUs, resulting in an extra copy /
        // resolve step when the debug overlay is enabled.
        self.texture_resolver.begin_frame();

        if let Some(device_size) = device_size {
            self.update_gpu_profile(device_size);
        }

        let cpu_frame_id = {
            let _gm = self.gpu_profiler.start_marker("begin frame");
            let frame_id = self.device.begin_frame();
            self.gpu_profiler.begin_frame(frame_id);

            self.device.set_depth_test(None);
            self.set_blend_mode(BlendMode::None, FramebufferKind::Main);
            //self.update_shaders();

            self.update_texture_cache();
            self.update_native_surfaces();

            frame_id
        };

        if !active_doc.frame.present {
            // Setting device_size to None is what ensures compositing/presenting
            // the frame is skipped in the rest of this module.
            device_size = None;
        }

        if let Some(device_size) = device_size {
            // Inform the client that we are starting a composition transaction if native
            // compositing is enabled. This needs to be done early in the frame, so that
            // we can create debug overlays after drawing the main surfaces.
            if let CompositorKind::Native { .. } = self.current_compositor_kind {
                let compositor = self.compositor_config.compositor().unwrap();
                compositor.begin_frame();
            }

            // Update the state of the debug overlay surface, ensuring that
            // the compositor mode has a suitable surface to draw to, if required.
            debug::update_debug_overlay(
                &mut self.compositor_config,
                self.current_compositor_kind,
                &mut self.debug_overlay_state,
                self.debug_flags,
                device_size,
                !active_doc.frame.debug_items.is_empty(),
            );
        }

        let frame = &mut active_doc.frame;
        let profile = &mut active_doc.profile;
        assert!(self.current_compositor_kind == frame.composite_state.compositor_kind);

        if self.shared_texture_cache_cleared {
            assert!(self.documents_seen.contains(&doc_id),
                    "Cleared texture cache without sending new document frame.");
        }

        external_image::update_deferred_resolves(
            self.external_image_handler.as_mut(),
            &mut self.texture_resolver.external_images,
            &mut self.gpu_profiler,
            &mut self.device,
            &frame.deferred_resolves,
            &mut frame.gpu_buffer_f,
        );

        // Now that external images are resolved, copy their (potentially Y-flipped) uv
        // rects into the quad segment blocks that reference them.
        frame.gpu_buffer_f.apply_deferred_uv_copies();

        self.draw_frame(
            frame,
            device_size,
            buffer_age,
            &mut results,
        );

        // TODO(nical): do this automatically by selecting counters in the wr profiler
        // Profile marker for the number of invalidated picture cache
        if thread_is_being_profiled() {
            let duration = Duration::new(0,0);
            if let Some(n) = self.profile.get(profiler::RENDERED_PICTURE_TILES) {
                let message = (n as usize).to_string();
                add_text_marker("NumPictureCacheInvalidated", &message, duration);
            }
        }

        if device_size.is_some() {
            debug::draw_frame_debug_items(
                &mut self.device,
                &mut self.debug,
                &frame.debug_items,
            );
        }

        self.profile.merge(profile);

        external_image::unlock_external_images(
            self.external_image_handler.as_mut(),
            &mut self.texture_resolver.external_images,
            &frame.deferred_resolves,
        );

        let _gm = self.gpu_profiler.start_marker("end frame");
        self.gpu_profiler.end_frame();

        let t = self.profile.end_time(profiler::RENDERER_TIME);
        self.profile.end_time_if_started(profiler::TOTAL_FRAME_CPU_TIME);

        let current_time = zeitstempel::now();
        if device_size.is_some() {
            let time = profiler::ns_to_ms(current_time - self.last_time);
            self.profile.set(profiler::FRAME_TIME, time);
        }

        let debug_overlay = device_size.and_then(|device_size| {
            // Bind a surface to draw the debug / profiler information to.
            debug::bind_debug_overlay(
                &mut self.device,
                &mut self.compositor_config,
                &mut self.layer_tree,
                self.current_compositor_kind,
                &self.debug_overlay_state,
                device_size,
            ).map(|draw_target| {
                debug::draw_render_target_debug(
                    &mut self.device,
                    &mut self.debug,
                    self.debug_flags,
                    &self.texture_resolver,
                    &draw_target,
                );
                debug::draw_texture_cache_debug(
                    &mut self.device,
                    &mut self.debug,
                    self.debug_flags,
                    &self.texture_resolver,
                    &draw_target,
                );
                debug::draw_zoom_debug(
                    &mut self.device,
                    &mut self.debug,
                    self.debug_flags,
                    &mut self.zoom_debug_texture,
                    self.cursor_position,
                    device_size,
                );
                debug::draw_epoch_debug(
                    &mut self.device,
                    &mut self.debug,
                    self.debug_flags,
                    &self.pipeline_info,
                );
                debug::draw_window_visibility_debug(
                    &mut self.device,
                    &mut self.debug,
                    self.debug_flags,
                    &mut self.compositor_config,
                );
                debug::draw_external_composite_borders_debug(
                    &mut self.device,
                    &mut self.debug,
                    self.debug_flags,
                    &self.external_composite_debug_items,
                );
                draw_target
            })
        });

        Telemetry::record_renderer_time(Duration::from_micros((t * 1000.00) as u64));
        if self.profile.get(profiler::SHADER_BUILD_TIME).is_none() {
          Telemetry::record_renderer_time_no_sc(Duration::from_micros((t * 1000.00) as u64));
        }

        if self.max_recorded_profiles > 0 {
            while self.cpu_profiles.len() >= self.max_recorded_profiles {
                self.cpu_profiles.pop_front();
            }
            let cpu_profile = CpuProfile::new(
                cpu_frame_id,
                (self.profile.get_or(profiler::FRAME_BUILDING_TIME, 0.0) * 1000000.0) as u64,
                (self.profile.get_or(profiler::RENDERER_TIME, 0.0) * 1000000.0) as u64,
                self.profile.get_or(profiler::DRAW_CALLS, 0.0) as usize,
            );
            self.cpu_profiles.push_back(cpu_profile);
        }

        if thread_is_being_profiled() {
            let duration = Duration::new(0,0);
            let message = (self.profile.get_or(profiler::DRAW_CALLS, 0.0) as usize).to_string();
            add_text_marker("NumDrawCalls", &message, duration);
        }

        let report = self.texture_resolver.report_memory();
        self.profile.set(profiler::RENDER_TARGET_MEM, profiler::bytes_to_mb(report.render_target_textures));
        self.profile.set(profiler::PICTURE_TILES_MEM, profiler::bytes_to_mb(report.picture_tile_textures));
        self.profile.set(profiler::ATLAS_TEXTURES_MEM, profiler::bytes_to_mb(report.atlas_textures));
        self.profile.set(profiler::STANDALONE_TEXTURES_MEM, profiler::bytes_to_mb(report.standalone_textures));

        self.profile.set(profiler::DEPTH_TARGETS_MEM, profiler::bytes_to_mb(self.device.depth_targets_memory()));

        self.profile.set(profiler::TEXTURES_CREATED, self.device.textures_created());
        self.profile.set(profiler::TEXTURES_DELETED, self.device.textures_deleted());

        results.stats.texture_upload_mb = self.profile.get_or(profiler::TEXTURE_UPLOADS_MEM, 0.0);
        results.compositor_surface_overlays =
            self.profile.get_or(profiler::COMPOSITOR_SURFACE_OVERLAYS, 0.0) as usize;
        results.compositor_surface_underlays =
            self.profile.get_or(profiler::COMPOSITOR_SURFACE_UNDERLAYS, 0.0) as usize;
        self.frame_counter += 1;
        results.stats.resource_upload_time = self.resource_upload_time;
        self.resource_upload_time = 0.0;

        if let Some(stats) = active_doc.frame_stats.take() {
          // Copy the full frame stats to RendererStats
          results.stats.merge(&stats);

          self.profiler.update_frame_stats(stats);
        }

        // Turn the render reasons bitflags into something we can see in the profiler.
        // For now this is just a binary yes/no for each bit, which means that when looking
        // at "Render reasons" in the profiler HUD the average view indicates the proportion
        // of frames that had the bit set over a half second window whereas max shows whether
        // the bit as been set at least once during that time window.
        // We could implement better ways to visualize this information.
        let add_markers = thread_is_being_profiled();
        for i in 0..RenderReasons::NUM_BITS {
            let counter = profiler::RENDER_REASON_FIRST + i as usize;
            let mut val = 0.0;
            let reason_bit = RenderReasons::from_bits_truncate(1 << i);
            if active_doc.render_reasons.contains(reason_bit) {
                val = 1.0;
                if add_markers {
                    let event_str = format!("Render reason {:?}", reason_bit);
                    add_event_marker(&event_str);
                }
            }
            self.profile.set(counter, val);
        }
        active_doc.render_reasons = RenderReasons::empty();


        self.texture_resolver.update_profile(&mut self.profile);

        // Note: this clears the values in self.profile.
        self.profiler.set_counters(&mut self.profile);

        // If debugger is enabled, collect any profiler updates before value is overwritten
        // during update below.
        #[cfg(feature = "debugger")]
        {
            // Shader failures accumulated while drawing this frame. These are
            // the ones a source push could not report, because the variant
            // had not been compiled yet when the push happened.
            let shader_errors: Vec<ShaderDiagnostic> = self
                .renderer_errors
                .iter()
                .filter_map(|error| match error {
                    RendererError::Shader(error) => Some(error),
                    _ => None,
                })
                .flat_map(shader_diagnostics)
                .collect();

            self.debugger.update(
                self.debug_flags,
                &self.profiler,
                &self.command_log,
                &shader_errors,
            );
        }

        // Note: profile counters must be set before this or they will count for next frame.
        self.profiler.update();

        if self.debug_flags.intersects(DebugFlags::PROFILER_DBG | DebugFlags::PROFILER_CAPTURE) {
            if let Some(device_size) = device_size {
                //TODO: take device/pixel ratio into equation?
                if let Some(debug_renderer) = self.debug.get_mut(&mut self.device) {
                    self.profiler.draw_profile(
                        self.frame_counter,
                        debug_renderer,
                        device_size,
                    );
                }
            }
        }

        if self.debug_flags.contains(DebugFlags::ECHO_DRIVER_MESSAGES) {
            self.device.echo_driver_messages();
        }

        if let Some(debug_renderer) = self.debug.try_get_mut() {
            let small_screen = self.debug_flags.contains(DebugFlags::SMALL_SCREEN);
            let scale = if small_screen { 1.6 } else { 1.0 };
            // TODO(gw): Tidy this up so that compositor config integrates better
            //           with the (non-compositor) surface y-flip options.
            let surface_origin_is_top_left = match self.current_compositor_kind {
                CompositorKind::Native { .. } => true,
                CompositorKind::Draw { .. } | CompositorKind::Layer { .. } => self.device.surface_origin_is_top_left(),
            };
            // If there is a debug overlay, render it. Otherwise, just clear
            // the debug renderer.
            debug_renderer.render(
                &mut self.device,
                debug_overlay.and(device_size),
                scale,
                surface_origin_is_top_left,
            );
        }

        if debug_overlay.is_some() {
            self.device.end_render_pass(StoreOp::Store);
        }

        self.staging_texture_pool.end_frame(&mut self.device);
        self.texture_upload_buffer_pool.end_frame(&mut self.device);
        self.device.end_frame();

        if debug_overlay.is_some() {
            self.last_time = current_time;

            // Unbind the target for the debug overlay. No debug or profiler drawing
            // can occur afer this point.
            debug::unbind_debug_overlay(
                &mut self.compositor_config,
                &mut self.layer_tree,
                self.current_compositor_kind,
                &self.debug_overlay_state,
            );
        }

        if device_size.is_some() {
            // Inform the client that we are finished this composition transaction if native
            // compositing is enabled. This must be called after any debug / profiling compositor
            // surfaces have been drawn and added to the visual tree.
            match self.current_compositor_kind {
                CompositorKind::Native { .. } => {
                    tracy_rs::profile_scope!("compositor.end_frame");
                    let compositor = self.compositor_config.compositor().unwrap();
                    compositor.end_frame();
                }
                CompositorKind::Draw { .. } | CompositorKind::Layer { .. } => {
                    self.layer_tree.end_frame(&mut self.compositor_config);
                }
            }
        }

        self.documents_seen.clear();
        self.shared_texture_cache_cleared = false;

        self.check_device_errors();

        if self.renderer_errors.is_empty() {
            Ok(results)
        } else {
            Err(mem::replace(&mut self.renderer_errors, Vec::new()))
        }
    }

    fn update_gpu_profile(&mut self, device_size: DeviceIntSize) {
        let _gm = self.gpu_profiler.start_marker("build samples");
        // Block CPU waiting for last frame's GPU profiles to arrive.
        // In general this shouldn't block unless heavily GPU limited.
        let (gpu_frame_id, timers, samplers) = self.gpu_profiler.build_samples();

        if self.max_recorded_profiles > 0 {
            while self.gpu_profiles.len() >= self.max_recorded_profiles {
                self.gpu_profiles.pop_front();
            }

            self.gpu_profiles.push_back(GpuProfile::new(gpu_frame_id, &timers));
        }

        self.profiler.set_gpu_time_queries(timers);

        if !samplers.is_empty() {
            let screen_fraction = 1.0 / device_size.to_f32().area();

            fn accumulate_sampler_value(description: &str, samplers: &[GpuSampler]) -> f32 {
                let mut accum = 0.0;
                for sampler in samplers {
                    if sampler.tag.label != description {
                        continue;
                    }

                    accum += sampler.count as f32;
                }

                accum
            }

            let alpha_targets = accumulate_sampler_value(&"Alpha targets", &samplers) * screen_fraction;
            let transparent_pass = accumulate_sampler_value(&"Transparent pass", &samplers) * screen_fraction;
            let opaque_pass = accumulate_sampler_value(&"Opaque pass", &samplers) * screen_fraction;
            self.profile.set(profiler::ALPHA_TARGETS_SAMPLERS, alpha_targets);
            self.profile.set(profiler::TRANSPARENT_PASS_SAMPLERS, transparent_pass);
            self.profile.set(profiler::OPAQUE_PASS_SAMPLERS, opaque_pass);
            self.profile.set(profiler::TOTAL_SAMPLERS, alpha_targets + transparent_pass + opaque_pass);
        }
    }

    fn update_texture_cache(&mut self) {
        tracy_rs::profile_scope!("update_texture_cache");

        let _gm = self.gpu_profiler.start_marker("texture cache update");
        let mut pending_texture_updates = mem::replace(&mut self.pending_texture_updates, vec![]);
        self.pending_texture_cache_updates = false;

        self.profile.start_time(profiler::TEXTURE_CACHE_UPDATE_TIME);

        let mut create_cache_texture_time = 0;
        let mut delete_cache_texture_time = 0;

        for update_list in pending_texture_updates.drain(..) {
            // Handle copies from one texture to another.
            for ((src_tex, dst_tex), copies) in &update_list.copies {

                let dest_texture = &self.texture_resolver.texture_cache_map[&dst_tex].texture;
                let dst_texture_size = dest_texture.get_dimensions().to_f32();

                let mut copy_instances = Vec::new();
                for copy in copies {
                    copy_instances.push(CopyInstance {
                        src_rect: copy.src_rect.to_f32(),
                        dst_rect: copy.dst_rect.to_f32(),
                        dst_texture_size,
                    });
                }

                let draw_target = DrawTarget::from_texture(dest_texture, false);
                self.device.begin_render_pass(&RenderPassDescriptor {
                    target: draw_target,
                    render_area: None,
                    color_load: LoadOp::Load,
                    depth_load: LoadOp::DontCare,
                });

                self.shaders
                    .borrow_mut()
                    .ps_copy()
                    .bind(
                        &mut self.device,
                        &Transform3D::identity(),
                        None,
                        &mut self.renderer_errors,
                        &mut self.profile,
                        &mut self.command_log,
                    );

                self.draw_instanced_batch(
                    &copy_instances,
                    VertexArrayKind::Copy,
                    &BatchTextures::composite_rgb(
                        TextureSource::TextureCache(*src_tex, Swizzle::default())
                    ),
                    &mut RendererStats::default(),
                );

                self.device.end_render_pass(StoreOp::Store);
            }

            // Find any textures that will need to be deleted in this group of allocations.
            let mut pending_deletes = Vec::new();
            for allocation in &update_list.allocations {
                let old = self.texture_resolver.texture_cache_map.remove(&allocation.id);
                match allocation.kind {
                    TextureCacheAllocationKind::Alloc(_) => {
                        assert!(old.is_none(), "Renderer and backend disagree!");
                    }
                    TextureCacheAllocationKind::Reset(_) |
                    TextureCacheAllocationKind::Free => {
                        assert!(old.is_some(), "Renderer and backend disagree!");
                    }
                }
                if let Some(old) = old {

                    // Regenerate the cache allocation info so we can search through deletes for reuse.
                    let size = old.texture.get_dimensions();
                    let info = TextureCacheAllocInfo {
                        width: size.width,
                        height: size.height,
                        format: old.texture.get_format(),
                        filter: old.texture.get_filter(),
                        target: old.texture.get_target(),
                        is_shared_cache: old.texture.flags().contains(TextureFlags::IS_SHARED_TEXTURE_CACHE),
                        has_depth: old.texture.supports_depth(),
                        category: old.category,
                    };
                    pending_deletes.push((old.texture, info));
                }
            }
            // Look for any alloc or reset that has matching alloc info and save it from being deleted.
            let mut reused_textures = VecDeque::with_capacity(pending_deletes.len());
            for allocation in &update_list.allocations {
                match allocation.kind {
                    TextureCacheAllocationKind::Alloc(ref info) |
                    TextureCacheAllocationKind::Reset(ref info) => {
                        reused_textures.push_back(
                            pending_deletes.iter()
                                .position(|(_, old_info)| *old_info == *info)
                                .map(|index| pending_deletes.swap_remove(index).0)
                        );
                    }
                    TextureCacheAllocationKind::Free => {}
                }
            }

            // Now that we've saved as many deletions for reuse as we can, actually delete whatever is left.
            if !pending_deletes.is_empty() {
                let delete_texture_start = zeitstempel::now();
                for (texture, _) in pending_deletes {
                    add_event_marker("TextureCacheFree");
                    self.device.delete_texture(texture);
                }
                delete_cache_texture_time += zeitstempel::now() - delete_texture_start;
            }

            for allocation in update_list.allocations {
                match allocation.kind {
                    TextureCacheAllocationKind::Alloc(_) => add_event_marker("TextureCacheAlloc"),
                    TextureCacheAllocationKind::Reset(_) => add_event_marker("TextureCacheReset"),
                    TextureCacheAllocationKind::Free => {}
                };
                match allocation.kind {
                    TextureCacheAllocationKind::Alloc(ref info) |
                    TextureCacheAllocationKind::Reset(ref info) => {
                        let create_cache_texture_start = zeitstempel::now();
                        // Create a new native texture, as requested by the texture cache.
                        // If we managed to reuse a deleted texture, then prefer that instead.
                        //
                        // Ensure no PBO is bound when creating the texture storage,
                        // or GL will attempt to read data from there.
                        let mut texture = reused_textures.pop_front().unwrap_or(None).unwrap_or_else(|| {
                            self.device.create_texture(
                                info.target,
                                info.format,
                                info.width,
                                info.height,
                                info.filter,
                                // This needs to be a render target because some render
                                // tasks get rendered into the texture cache.
                                Some(RenderTargetInfo { has_depth: info.has_depth }),
                            )
                        });

                        if info.is_shared_cache {
                            texture.flags_mut()
                                .insert(TextureFlags::IS_SHARED_TEXTURE_CACHE);

                            // On Mali-Gxx devices we use batched texture uploads as it performs much better.
                            // However, due to another driver bug we must ensure the textures are fully cleared,
                            // otherwise we get visual artefacts when blitting to the texture cache.
                            if self.device.use_batched_texture_uploads() &&
                                !self.device.get_capabilities().supports_render_target_partial_update
                            {
                                self.clear_texture(&texture, [0.0; 4]);
                            }

                            // Textures in the cache generally don't need to be cleared,
                            // but we do so if the debug display is active to make it
                            // easier to identify unallocated regions.
                            if self.debug_flags.contains(DebugFlags::TEXTURE_CACHE_DBG) {
                                self.clear_texture(&texture, TEXTURE_CACHE_DBG_CLEAR_COLOR);
                            }
                        }

                        create_cache_texture_time += zeitstempel::now() - create_cache_texture_start;

                        self.texture_resolver.texture_cache_map.insert(allocation.id, CacheTexture {
                            texture,
                            category: info.category,
                        });
                    }
                    TextureCacheAllocationKind::Free => {}
                };
            }

            upload_to_texture_cache(self, update_list.updates);

            self.check_device_errors();
        }

        if create_cache_texture_time > 0 {
            self.profile.set(
                profiler::CREATE_CACHE_TEXTURE_TIME,
                profiler::ns_to_ms(create_cache_texture_time)
            );
        }
        if delete_cache_texture_time > 0 {
            self.profile.set(
                profiler::DELETE_CACHE_TEXTURE_TIME,
                profiler::ns_to_ms(delete_cache_texture_time)
            )
        }

        let t = self.profile.end_time(profiler::TEXTURE_CACHE_UPDATE_TIME);
        self.resource_upload_time += t;
        Telemetry::record_texture_cache_update_time(Duration::from_micros((t * 1000.00) as u64));

        drain_filter(
            &mut self.notifications,
            |n| { n.when() == Checkpoint::FrameTexturesUpdated },
            |n| { n.notify(); },
        );
    }

    fn check_device_errors(&mut self) {
        if self.device.take_out_of_memory_error() {
            self.renderer_errors.push(RendererError::OutOfMemory);
        }
    }

    fn bind_textures(&mut self, textures: &BatchTextures) {
        for i in 0 .. 3 {
            self.texture_resolver.bind(
                &textures.input.colors[i],
                TextureSampler::color(i),
                &mut self.device,
            );
        }

        self.texture_resolver.bind(
            &textures.clip_mask,
            TextureSampler::ClipMask,
            &mut self.device,
        );

        // TODO: this probably isn't the best place for this.
        if let Some(ref texture) = self.dither_matrix_texture {
            self.device.bind_texture(TextureSampler::Dither, texture, Swizzle::default());
        }
    }

    fn draw_instanced_batch<T: Clone>(
        &mut self,
        data: &[T],
        vertex_array_kind: VertexArrayKind,
        textures: &BatchTextures,
        stats: &mut RendererStats,
    ) {
        if let Some(history) = &mut self.command_log {
            history.draw(data.len() as u32);
        }

        self.bind_textures(textures);

        // If we end up with an empty draw call here, that means we have
        // probably introduced unnecessary batch breaks during frame
        // building - so we should be catching this earlier and removing
        // the batch.
        debug_assert!(!data.is_empty());

        let vao = &self.vaos[vertex_array_kind];
        self.device.bind_vertex_array(vao);
        let instance_stride = vao.instance_stride();

        let chunk_size = if self.debug_flags.contains(DebugFlags::DISABLE_BATCHING) {
            1
        } else if self.use_shared_instance_buffer {
            // Ensure each chunk will fit within the fixed size instance buffer.
            vertex::SHARED_INSTANCE_BUFFER_SIZE / instance_stride
        } else if vertex_array_kind == VertexArrayKind::Primitive {
            self.max_primitive_instance_count
        } else {
            data.len()
        };

        if self.use_shared_instance_buffer {
            for chunk in data.chunks(chunk_size) {
                let offset = self
                    .vaos
                    .shared_instance_buffer
                    .as_mut()
                    .expect("shared instance buffer mode enabled but no shared buffer")
                    .push_instances(&mut self.device, chunk);
                let base_instance = (offset / instance_stride) as u32;
                self.device.draw_indexed_triangles_instanced_base_instance_u16(
                    6,
                    chunk.len() as i32,
                    base_instance,
                );
                self.profile.inc(profiler::DRAW_CALLS);
                stats.total_draw_calls += 1;
            }
        } else {
            for chunk in data.chunks(chunk_size) {
                let instances = self.vaos.instance_buffer_mut(vertex_array_kind);
                if self.enable_instancing {
                    self.device
                        .write_buffer(instances, chunk, ONE_TIME_USAGE_HINT);
                    self.device
                        .draw_indexed_triangles_instanced_u16(6, chunk.len() as i32);
                } else {
                    self.device
                        .write_buffer_repeated(instances, chunk, NonZeroUsize::new(4).unwrap(), ONE_TIME_USAGE_HINT);
                    self.device
                        .draw_indexed_triangles(6 * chunk.len() as i32);
                }
                self.profile.inc(profiler::DRAW_CALLS);
                stats.total_draw_calls += 1;
            }
        }

        self.profile.add(profiler::VERTICES, 6 * data.len());
    }

    fn handle_readback_composite(
        &mut self,
        draw_target: DrawTarget,
        scissor_rect: Option<FramebufferIntRect>,
        backdrop: &RenderTask,
        readback: &RenderTask,
    ) {
        // Extract the rectangle in the backdrop surface's device space of where
        // we need to read from.
        let readback_origin = match readback.kind {
            RenderTaskKind::Readback(ReadbackTask { readback_origin: Some(o), .. }) => o,
            RenderTaskKind::Readback(ReadbackTask { readback_origin: None, .. }) => {
                // If this is a dummy readback, just early out. We know that the
                // clear of the target will ensure the task rect is already zero alpha,
                // so it won't affect the rendering output.
                return;
            }
            _ => unreachable!(),
        };

        self.device.set_scissor(None);

        let texture_source = TextureSource::TextureCache(
            readback.get_target_texture(),
            Swizzle::default(),
        );
        let (cache_texture, _) = self.texture_resolver
            .resolve(&texture_source).expect("bug: no source texture");

        // Before submitting the composite batch, do the
        // framebuffer readbacks that are needed for each
        // composite operation in this batch.
        let readback_rect = readback.get_target_rect();
        let backdrop_rect = backdrop.get_target_rect();
        let (backdrop_screen_origin, _) = match backdrop.kind {
            RenderTaskKind::Picture(ref task_info) => (task_info.content_origin, task_info.device_pixel_scale),
            _ => panic!("bug: composite on non-picture?"),
        };

        // Bind the FBO to blit the backdrop to.
        // Called per-instance in case the FBO changes. The device will skip
        // the GL call if the requested target is already bound.
        let cache_draw_target = DrawTarget::from_texture(
            cache_texture,
            false,
        );

        // Get the rect that we ideally want, in space of the parent surface
        let wanted_rect = DeviceRect::from_origin_and_size(
            readback_origin,
            readback_rect.size().to_f32(),
        );

        // Get the rect that is available on the parent surface. It may be smaller
        // than desired because this is a picture cache tile covering only part of
        // the wanted rect and/or because the parent surface was clipped.
        let avail_rect = DeviceRect::from_origin_and_size(
            backdrop_screen_origin,
            backdrop_rect.size().to_f32(),
        );

        if let Some(int_rect) = wanted_rect.intersection(&avail_rect) {
            // If there is a valid intersection, work out the correct origins and
            // sizes of the copy rects, and do the blit.
            let copy_size = int_rect.size().to_i32();

            let src_origin = backdrop_rect.min.to_f32() +
                int_rect.min.to_vector() -
                backdrop_screen_origin.to_vector();

            let src = DeviceIntRect::from_origin_and_size(
                src_origin.to_i32(),
                copy_size,
            );

            let dest_origin = readback_rect.min.to_f32() +
                int_rect.min.to_vector() -
                readback_origin.to_vector();

            let dest = DeviceIntRect::from_origin_and_size(
                dest_origin.to_i32(),
                copy_size,
            );

            // Should always be drawing to picture cache tiles or off-screen surface!
            debug_assert!(!draw_target.is_default());
            let device_to_framebuffer = Scale::new(1i32);

            self.device.blit_render_target(
                draw_target.into(),
                src * device_to_framebuffer,
                cache_draw_target,
                dest * device_to_framebuffer,
                TextureFilter::Linear,
            );
        }

        self.device.set_scissor(scissor_rect);
    }

    fn handle_prims(
        &mut self,
        draw_target: &DrawTarget,
        prim_instances: &[FastHashMap<TextureSet, FrameVec<PrimitiveInstanceData>>],
        prim_instances_with_scissor: &FastHashMap<(DeviceIntRect, PatternKind), FastHashMap<TextureSet, FrameVec<PrimitiveInstanceData>>>,
        projection: &default::Transform3D<f32>,
        stats: &mut RendererStats,
    ) {
        self.device.set_depth_write(false);

        let has_prim_instances = prim_instances.iter().any(|map| !map.is_empty());
        if has_prim_instances || !prim_instances_with_scissor.is_empty() {
            let _timer = self.gpu_profiler.start_timer(GPU_TAG_INDIRECT_PRIM);

            self.set_blend_mode(BlendMode::None, FramebufferKind::Other);

            for (pattern_idx, prim_instances_map) in prim_instances.iter().enumerate() {
                if prim_instances_map.is_empty() {
                    continue;
                }
                let pattern = PatternKind::from_u32(pattern_idx as u32);

                self.shaders.borrow_mut().get_quad_shader(pattern).bind(
                    &mut self.device,
                    projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                for (texture_set, prim_instances) in prim_instances_map {
                    let texture_bindings = BatchTextures {
                        input: *texture_set,
                        clip_mask: TextureSource::Invalid,
                    };

                    self.draw_instanced_batch(
                        prim_instances,
                        VertexArrayKind::Primitive,
                        &texture_bindings,
                        stats,
                    );
                }
            }

            if !prim_instances_with_scissor.is_empty() {
                self.set_blend_mode(BlendMode::PremultipliedAlpha, FramebufferKind::Other);

                let mut prev_pattern = None;

                for ((scissor_rect, pattern), prim_instances_map) in prim_instances_with_scissor {
                    if prev_pattern != Some(*pattern) {
                        prev_pattern = Some(*pattern);
                        self.shaders.borrow_mut().get_quad_shader(*pattern).bind(
                            &mut self.device,
                            projection,
                            None,
                            &mut self.renderer_errors,
                            &mut self.profile,
                            &mut self.command_log,
                        );
                    }

                    self.device.set_scissor(Some(draw_target.to_framebuffer_rect(*scissor_rect)));

                    for (texture_set, prim_instances) in prim_instances_map {
                        let texture_bindings = BatchTextures {
                            input: *texture_set,
                            clip_mask: TextureSource::Invalid,
                        };

                        self.draw_instanced_batch(
                            prim_instances,
                            VertexArrayKind::Primitive,
                            &texture_bindings,
                            stats,
                        );
                    }
                }

                self.device.set_scissor(None);
            }
        }
    }

    fn handle_clips(
        &mut self,
        draw_target: &DrawTarget,
        target_kind: RenderTargetKind,
        masks: &ClipMaskInstanceList,
        projection: &default::Transform3D<f32>,
        stats: &mut RendererStats,
    ) {
        self.device.set_depth_write(false);

        {
            let _timer = self.gpu_profiler.start_timer(GPU_TAG_INDIRECT_MASK);

            self.set_blend_mode(BlendMode::Multiply, FramebufferKind::Other);

            if !masks.mask_instances_fast.is_empty() {
                self.shaders.borrow_mut().ps_mask_fast().bind(
                    &mut self.device,
                    projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                self.draw_instanced_batch(
                    &masks.mask_instances_fast,
                    VertexArrayKind::Mask,
                    &BatchTextures::empty(),
                    stats,
                );
            }

            if !masks.mask_instances_fast_with_scissor.is_empty() {
                self.shaders.borrow_mut().ps_mask_fast().bind(
                    &mut self.device,
                    projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                for (scissor_rect, instances) in &masks.mask_instances_fast_with_scissor {
                    self.device.set_scissor(Some(draw_target.to_framebuffer_rect(*scissor_rect)));

                    self.draw_instanced_batch(
                        instances,
                        VertexArrayKind::Mask,
                        &BatchTextures::empty(),
                        stats,
                    );
                }

                self.device.set_scissor(None);
            }

            if !masks.mask_instances_superellipse.is_empty() {
                self.shaders.borrow_mut().ps_mask_superellipse().bind(
                    &mut self.device,
                    projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                self.draw_instanced_batch(
                    &masks.mask_instances_superellipse,
                    VertexArrayKind::Mask,
                    &BatchTextures::empty(),
                    stats,
                );
            }

            if !masks.mask_instances_superellipse_with_scissor.is_empty() {
                self.shaders.borrow_mut().ps_mask_superellipse().bind(
                    &mut self.device,
                    projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                for (scissor_rect, instances) in &masks.mask_instances_superellipse_with_scissor {
                    self.device.set_scissor(Some(draw_target.to_framebuffer_rect(*scissor_rect)));

                    self.draw_instanced_batch(
                        instances,
                        VertexArrayKind::Mask,
                        &BatchTextures::empty(),
                        stats,
                    );
                }

                self.device.set_scissor(None);
            }

            if !masks.image_mask_instances.is_empty() {
                self.shaders.borrow_mut().ps_quad_textured().bind(
                    &mut self.device,
                    projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                for (texture, prim_instances) in &masks.image_mask_instances {
                    self.draw_instanced_batch(
                        prim_instances,
                        VertexArrayKind::Primitive,
                        &BatchTextures::composite_rgb(*texture),
                        stats,
                    );
                }
            }

            if !masks.image_mask_instances_with_scissor.is_empty() {
                self.shaders.borrow_mut().ps_quad_textured().bind(
                    &mut self.device,
                    projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                for ((scissor_rect, texture), prim_instances) in &masks.image_mask_instances_with_scissor {
                    self.device.set_scissor(Some(draw_target.to_framebuffer_rect(*scissor_rect)));

                    self.draw_instanced_batch(
                        prim_instances,
                        VertexArrayKind::Primitive,
                        &BatchTextures::composite_rgb(*texture),
                        stats,
                    );
                }

                self.device.set_scissor(None);
            }

            if !masks.mask_instances_slow.is_empty() {
                self.shaders.borrow_mut().ps_mask().bind(
                    &mut self.device,
                    projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                self.draw_instanced_batch(
                    &masks.mask_instances_slow,
                    VertexArrayKind::Mask,
                    &BatchTextures::empty(),
                    stats,
                );
            }

            if !masks.mask_instances_slow_with_scissor.is_empty() {
                self.shaders.borrow_mut().ps_mask().bind(
                    &mut self.device,
                    projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                for (scissor_rect, instances) in &masks.mask_instances_slow_with_scissor {
                    self.device.set_scissor(Some(draw_target.to_framebuffer_rect(*scissor_rect)));

                    self.draw_instanced_batch(
                        instances,
                        VertexArrayKind::Mask,
                        &BatchTextures::empty(),
                        stats,
                    );
                }

                self.device.set_scissor(None);
            }

            // Path tiles cover the outside of the path. Both blend modes
            // multiply the destination by the coverage of the path (see
            // cs_path_tile), so like the masks above, the order of the clips
            // does not matter. Alpha targets have no alpha channel to drive
            // dest-out with in SWGL, so they keep the multiply blend mode.
            if !masks.path_tiles.is_empty() {
                if target_kind == RenderTargetKind::Color {
                    self.set_blend_mode(BlendMode::PremultipliedDestOut, FramebufferKind::Other);
                }

                self.shaders.borrow_mut().cs_path_tile(self.path_aa_samples).bind(
                    &mut self.device,
                    projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                self.draw_instanced_batch(
                    &masks.path_tiles,
                    VertexArrayKind::PathTile,
                    &BatchTextures::empty(),
                    stats,
                );
            }
        }
    }

    fn handle_blits(
        &mut self,
        blits: &[BlitJob],
        render_tasks: &RenderTaskGraph,
        draw_target: DrawTarget,
    ) {
        if blits.is_empty() {
            return;
        }

        let _timer = self.gpu_profiler.start_timer(GPU_TAG_BLIT);

        // TODO(gw): For now, we don't bother batching these by source texture.
        //           If if ever shows up as an issue, we can easily batch them.
        for blit in blits {
            let (source, source_rect) = {
                // A blit from the child render task into this target.
                // TODO(gw): Support R8 format here once we start
                //           creating mips for alpha masks.
                let task = &render_tasks[blit.source];
                let source_rect = blit.source_rect.translate(task.get_target_rect().min.to_vector());
                let source_texture = task.get_texture_source();

                (source_texture, source_rect)
            };

            let (texture, swizzle) = self.texture_resolver
                .resolve(&source)
                .expect("BUG: invalid source texture");

            if swizzle != Swizzle::default() {
                error!("Swizzle {:?} can't be handled by a blit", swizzle);
            }

            let read_target = DrawTarget::from_texture(
                texture,
                false,
            );

            self.device.blit_render_target(
                read_target.into(),
                read_target.to_framebuffer_rect(source_rect),
                draw_target,
                draw_target.to_framebuffer_rect(blit.target_rect),
                TextureFilter::Linear,
            );
        }
    }

    fn handle_scaling(
        &mut self,
        scalings: &FastHashMap<TextureSource, FrameVec<ScalingInstance>>,
        projection: &default::Transform3D<f32>,
        stats: &mut RendererStats,
    ) {
        if scalings.is_empty() {
            return
        }

        let _timer = self.gpu_profiler.start_timer(GPU_TAG_SCALE);
        for (source, instances) in scalings {
            let buffer_kind = source.image_buffer_kind();

            // When the source texture is an external texture, the UV rect is not known
            // when the external surface descriptor is created, because external textures
            // are not resolved until the lock() callback is invoked at the start of the
            // frame render. We must therefore override the source rects now.
            let uv_override_instances;
            let instances = match source {
                TextureSource::External(..) => {
                    uv_override_instances = instances.iter().map(|instance| {
                        let mut new_instance = instance.clone();
                        let texel_rect: TexelRect = self.texture_resolver.get_uv_rect(
                            &source,
                            instance.source_rect.cast().into()
                        ).into();
                        new_instance.source_rect = DeviceRect::new(texel_rect.uv0, texel_rect.uv1);
                        new_instance
                    }).collect::<Vec<_>>();
                    uv_override_instances.as_slice()
                }
                _ => instances.as_slice()
            };

            self.shaders
                .borrow_mut()
                .get_scale_shader(buffer_kind)
                .bind(
                    &mut self.device,
                    &projection,
                    Some(self.texture_resolver.get_texture_size(source).to_f32()),
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

            self.draw_instanced_batch(
                instances,
                VertexArrayKind::Scale,
                &BatchTextures::composite_rgb(*source),
                stats,
            );
        }
    }

    fn handle_svg_nodes(
        &mut self,
        textures: &BatchTextures,
        svg_filters: &[SVGFEFilterInstance],
        projection: &default::Transform3D<f32>,
        stats: &mut RendererStats,
    ) {
        if svg_filters.is_empty() {
            return;
        }

        let _timer = self.gpu_profiler.start_timer(GPU_TAG_SVG_FILTER_NODES);

        self.shaders.borrow_mut().cs_svg_filter_node().bind(
            &mut self.device,
            &projection,
            None,
            &mut self.renderer_errors,
            &mut self.profile,
            &mut self.command_log,
        );

        self.draw_instanced_batch(
            &svg_filters,
            VertexArrayKind::SvgFilterNode,
            textures,
            stats,
        );
    }

    fn draw_picture_cache_target(
        &mut self,
        target: &PictureCacheTarget,
        draw_target: DrawTarget,
        projection: &default::Transform3D<f32>,
        render_tasks: &RenderTaskGraph,
        stats: &mut RendererStats,
    ) {
        tracy_rs::profile_scope!("draw_picture_cache_target");
        if let Some(history) = &mut self.command_log {
            history.begin_render_target("Picture tile", draw_target.dimensions());
        }

        self.profile.inc(profiler::RENDERED_PICTURE_TILES);
        let _gm = self.gpu_profiler.start_marker("picture cache target");
        let framebuffer_kind = FramebufferKind::Other;

        {
            let _timer = self.gpu_profiler.start_timer(GPU_TAG_SETUP_TARGET);

            let clear_color = target.clear_color.map(|c| c.to_array());
            let scissor_rect = if self.device.get_capabilities().supports_render_target_partial_update
                && (target.dirty_rect != target.valid_rect
                    || self.device.get_capabilities().prefers_clear_scissor)
            {
                Some(target.dirty_rect)
            } else {
                None
            };

            // The dirty rect is fully redrawn, so nothing needs loading. A clear
            // of the whole target is the pass's load op; one restricted to the
            // dirty rect is issued once the pass has begun.
            let full_clear = scissor_rect.is_none();
            self.device.begin_render_pass(&RenderPassDescriptor {
                target: draw_target,
                render_area: Some(target.dirty_rect),
                color_load: match clear_color {
                    Some(color) if full_clear => LoadOp::Clear(color),
                    _ => LoadOp::DontCare,
                },
                depth_load: if full_clear { LoadOp::Clear(1.0) } else { LoadOp::DontCare },
            });

            self.device.set_depth_write(true);
            self.set_blend_mode(BlendMode::None, framebuffer_kind);

            match scissor_rect {
                // If updating only a dirty rect within a picture cache target, the
                // clear must also be scissored to that dirty region.
                Some(r) if self.clear_caches_with_quads => {
                    self.device.set_depth_test(Some(DepthFunction::Always));
                    // Save the draw call count so that our reftests don't get confused...
                    let old_draw_call_count = stats.total_draw_calls;
                    if clear_color.is_none() {
                        self.device.set_color_write(false);
                    }
                    let instance = ClearInstance {
                        rect: [
                            r.min.x as f32, r.min.y as f32,
                            r.max.x as f32, r.max.y as f32,
                        ],
                        color: clear_color.unwrap_or([0.0; 4]),
                    };
                    self.shaders.borrow_mut().ps_clear().bind(
                        &mut self.device,
                        &projection,
                        None,
                        &mut self.renderer_errors,
                        &mut self.profile,
                        &mut self.command_log,
                    );
                    self.draw_instanced_batch(
                        &[instance],
                        VertexArrayKind::Clear,
                        &BatchTextures::empty(),
                        stats,
                    );
                    if clear_color.is_none() {
                        self.device.set_color_write(true);
                    }
                    stats.total_draw_calls = old_draw_call_count;
                    self.device.set_depth_test(None);
                }
                Some(r) => {
                    self.device.clear_rect(
                        draw_target.build_scissor_rect(Some(r)),
                        clear_color,
                        Some(1.0),
                    );
                }
                None => {}
            };
            self.device.set_depth_write(false);
        }

        match target.kind {
            PictureCacheTargetKind::Draw { ref alpha_batch_container } => {
                self.draw_alpha_batch_container(
                    alpha_batch_container,
                    draw_target,
                    framebuffer_kind,
                    projection,
                    render_tasks,
                    stats,
                );
            }
        }

        self.device.end_render_pass(StoreOp::Discard);
    }

    /// Draw an alpha batch container into a given draw target. This is used
    /// by both color and picture cache target kinds.
    fn draw_alpha_batch_container(
        &mut self,
        alpha_batch_container: &AlphaBatchContainer,
        draw_target: DrawTarget,
        framebuffer_kind: FramebufferKind,
        projection: &default::Transform3D<f32>,
        render_tasks: &RenderTaskGraph,
        stats: &mut RendererStats,
    ) {
        let scissor_rect = alpha_batch_container
            .task_scissor_rect
            .map(|rect| draw_target.build_scissor_rect(Some(rect)));
        self.device.set_scissor(scissor_rect);

        if !alpha_batch_container.opaque_batches.is_empty()
            && !self.debug_flags.contains(DebugFlags::DISABLE_OPAQUE_PASS) {
            let _gl = self.gpu_profiler.start_marker("opaque batches");
            let opaque_sampler = self.gpu_profiler.start_sampler(GPU_SAMPLER_TAG_OPAQUE);
            self.set_blend_mode(BlendMode::None, framebuffer_kind);
            //Note: depth equality is needed for split planes
            self.device.set_depth_test(Some(DepthFunction::LessEqual));
            self.device.set_depth_write(true);

            // Draw opaque batches front-to-back for maximum
            // z-buffer efficiency!
            for batch in alpha_batch_container
                .opaque_batches
                .iter()
                .rev()
                {
                    if should_skip_batch(&batch.key.kind, self.debug_flags) {
                        continue;
                    }

                    self.shaders.borrow_mut()
                        .get(&batch.key, batch.features, self.debug_flags)
                        .bind(
                            &mut self.device, projection, None,
                            &mut self.renderer_errors,
                            &mut self.profile,
                            &mut self.command_log,
                        );

                    let _timer = self.gpu_profiler.start_timer(batch.key.kind.sampler_tag());
                    self.draw_instanced_batch(
                        &batch.instances,
                        VertexArrayKind::Primitive,
                        &batch.key.textures,
                        stats
                    );
                }

            self.device.set_depth_write(false);
            self.gpu_profiler.finish_sampler(opaque_sampler);
        } else {
            self.device.set_depth_test(None);
        }

        if !alpha_batch_container.alpha_batches.is_empty()
            && !self.debug_flags.contains(DebugFlags::DISABLE_ALPHA_PASS) {
            let _gl = self.gpu_profiler.start_marker("alpha batches");
            let transparent_sampler = self.gpu_profiler.start_sampler(GPU_SAMPLER_TAG_TRANSPARENT);

            let mut prev_blend_mode = BlendMode::None;
            let shaders_rc = self.shaders.clone();

            for batch in &alpha_batch_container.alpha_batches {
                if should_skip_batch(&batch.key.kind, self.debug_flags) {
                    continue;
                }

                let mut shaders = shaders_rc.borrow_mut();
                let shader = shaders.get(
                    &batch.key,
                    batch.features | BatchFeatures::ALPHA_PASS,
                    self.debug_flags,
                );

                if batch.key.blend_mode != prev_blend_mode {
                    match batch.key.blend_mode {
                        BlendMode::None => {
                            unreachable!("bug: opaque blend in alpha pass");
                        }
                        BlendMode::Advanced(..) if self.enable_advanced_blend_barriers => {
                            self.device.blend_barrier();
                        }
                        _ => {}
                    }
                    self.set_blend_mode(batch.key.blend_mode, framebuffer_kind);
                    prev_blend_mode = batch.key.blend_mode;
                }

                if let Some(readback) = batch.readback {
                    debug_assert_eq!(batch.instances.len(), 1);
                    self.handle_readback_composite(
                        draw_target,
                        scissor_rect,
                        &render_tasks[readback.src_task_id],
                        &render_tasks[readback.readback_task_id],
                    );
                }

                let _timer = self.gpu_profiler.start_timer(batch.key.kind.sampler_tag());
                shader.bind(
                    &mut self.device,
                    projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                self.draw_instanced_batch(
                    &batch.instances,
                    VertexArrayKind::Primitive,
                    &batch.key.textures,
                    stats
                );
            }

            self.set_blend_mode(BlendMode::None, framebuffer_kind);
            self.gpu_profiler.finish_sampler(transparent_sampler);
        }

        self.device.set_depth_test(None);
        self.device.set_scissor(None);
    }

    /// Decides how a color or alpha target is cleared. Whole-target clears
    /// are the render pass's load ops; anything restricted to part of the
    /// target is issued by `clear_render_target` once the pass has begun.
    fn plan_render_target_clear(
        &self,
        target: &RenderTarget,
        draw_target: DrawTarget,
    ) -> RenderTargetClear {
        let is_alpha = target.target_kind == RenderTargetKind::Alpha;
        let require_precise_clear = target.cached;

        // On some Mali-T devices we have observed crashes in subsequent draw calls
        // immediately after clearing the alpha render target regions with glClear().
        // Using the shader to clear the regions avoids the crash. See bug 1638593.
        let clear_with_quads = (target.cached && self.clear_caches_with_quads)
            || (is_alpha && self.clear_alpha_targets_with_quads);

        let favor_partial_updates = self.device.get_capabilities().supports_render_target_partial_update
            && self.enable_clear_scissor;

        // On some Adreno 4xx devices we have seen render tasks to alpha targets have no
        // effect unless the target is fully cleared prior to rendering. See bug 1714227.
        let full_clears_on_adreno = is_alpha && self.device.get_capabilities().requires_alpha_target_full_clear;
        let require_full_clear = !require_precise_clear
            && (full_clears_on_adreno || !favor_partial_updates);

        let clear_color = target
            .clear_color
            .map(|color| color.to_array());
        let clear_depth = if target.needs_depth() {
            Some(1.0)
        } else {
            None
        };

        // A target with a clear color is fully overwritten by the clear,
        // so its previous contents need not be loaded.
        let color_load = match clear_color {
            Some(..) => LoadOp::DontCare,
            None => LoadOp::Load,
        };
        let depth_load = clear_depth.map_or(LoadOp::DontCare, LoadOp::Clear);

        if clear_with_quads || require_precise_clear {
            // Only specific rects will be cleared.
            return RenderTargetClear {
                color_load,
                depth_load,
                rect: None,
                precise: require_precise_clear,
                with_quads: clear_with_quads,
            };
        }

        // At this point we know we don't require precise clears for correctness.
        // We may still attempt to restrict the clear rect as an optimization on
        // some configurations.
        let clear_rect = if require_full_clear {
            None
        } else {
            match draw_target {
                DrawTarget::Default { rect, total_size, .. } => {
                    if rect.min == FramebufferIntPoint::zero() && rect.size() == total_size {
                        // Whole screen is covered, no need for scissor
                        None
                    } else {
                        Some(rect)
                    }
                }
                DrawTarget::Texture { .. } => {
                    // TODO(gw): Applying a scissor rect and minimal clear here
                    // is a very large performance win on the Intel and nVidia
                    // GPUs that I have tested with. It's possible it may be a
                    // performance penalty on other GPU types - we should test this
                    // and consider different code paths.
                    //
                    // Note: The above measurements were taken when render
                    // target slices were minimum 2048x2048. Now that we size
                    // them adaptively, this may be less of a win (except perhaps
                    // on a mostly-unused last slice of a large texture array).
                    target.used_rect.map(|rect| draw_target.to_framebuffer_rect(rect))
                }
                // Full clear.
                _ => None,
            }
        };

        match clear_rect {
            None => RenderTargetClear {
                color_load: clear_color.map_or(LoadOp::Load, LoadOp::Clear),
                depth_load,
                rect: None,
                precise: false,
                with_quads: false,
            },
            Some(rect) => RenderTargetClear {
                color_load,
                depth_load: LoadOp::DontCare,
                rect: Some((rect, clear_color, clear_depth)),
                precise: false,
                with_quads: false,
            },
        }
    }

    /// Issues the clears of `clear` that happen inside the render pass.
    fn clear_render_target(
        &mut self,
        target: &RenderTarget,
        draw_target: DrawTarget,
        framebuffer_kind: FramebufferKind,
        clear: &RenderTargetClear,
        projection: &default::Transform3D<f32>,
        stats: &mut RendererStats,
    ) {
        self.device.set_depth_test(None);
        self.set_blend_mode(BlendMode::None, framebuffer_kind);

        if let Some((rect, color, depth)) = clear.rect {
            self.device.clear_rect(rect, color, depth);
        }

        if clear.precise && !clear.with_quads {
            // Only clear specific rects
            for (rect, color) in &target.clears {
                self.device.clear_rect(
                    draw_target.to_framebuffer_rect(*rect),
                    Some(color.to_array()),
                    None,
                );
            }
        }

        // Finally, if we decided to clear with quads or if we need to clear
        // some areas with specific colors that don't match the global clear
        // color, clear more areas using a draw call.

        let mut clear_instances = Vec::with_capacity(target.clears.len());
        for (rect, color) in &target.clears {
            if clear.with_quads || (!clear.precise && target.clear_color != Some(*color)) {
                let rect = rect.to_f32();
                clear_instances.push(ClearInstance {
                    rect: [
                        rect.min.x, rect.min.y,
                        rect.max.x, rect.max.y,
                    ],
                    color: color.to_array(),
                })
            }
        }

        if !clear_instances.is_empty() {
            self.shaders.borrow_mut().ps_clear().bind(
                &mut self.device,
                &projection,
                None,
                &mut self.renderer_errors,
                &mut self.profile,
                &mut self.command_log,
            );
            self.draw_instanced_batch(
                &clear_instances,
                VertexArrayKind::Clear,
                &BatchTextures::empty(),
                stats,
            );
        }
    }

    fn draw_render_target(
        &mut self,
        texture_id: CacheTextureId,
        target: &RenderTarget,
        render_tasks: &RenderTaskGraph,
        stats: &mut RendererStats,
    ) {
        let needs_depth = target.needs_depth();

        let texture = self.texture_resolver.get_cache_texture_mut(&texture_id);

        if let Some(history) = &mut self.command_log {
            let label = match target.target_kind {
                RenderTargetKind::Color => "color",
                RenderTargetKind::Alpha => "alpha",
            };
            history.begin_render_target(label, texture.get_dimensions());
        }

        if needs_depth {
            self.device.reuse_render_target(
                texture,
                RenderTargetInfo { has_depth: needs_depth },
            );
        }

        let draw_target = DrawTarget::from_texture(
            texture,
            needs_depth,
        );

        let projection = Transform3D::ortho(
            0.0,
            draw_target.dimensions().width as f32,
            0.0,
            draw_target.dimensions().height as f32,
            self.device.ortho_near_plane(),
            self.device.ortho_far_plane(),
        );

        tracy_rs::profile_scope!("draw_render_target");
        let _gm = self.gpu_profiler.start_marker("render target");

        let counter = match target.target_kind {
            RenderTargetKind::Color => profiler::COLOR_PASSES,
            RenderTargetKind::Alpha => profiler::ALPHA_PASSES,
        };
        self.profile.inc(counter);

        let sampler_query = match target.target_kind {
            RenderTargetKind::Color => None,
            RenderTargetKind::Alpha => Some(self.gpu_profiler.start_sampler(GPU_SAMPLER_TAG_ALPHA)),
        };

        // sanity check for the depth buffer
        if let DrawTarget::Texture { with_depth, .. } = draw_target {
            assert!(with_depth >= target.needs_depth());
        }

        let framebuffer_kind = if draw_target.is_default() {
            FramebufferKind::Main
        } else {
            FramebufferKind::Other
        };

        let clear = self.plan_render_target_clear(target, draw_target);

        {
            let _timer = self.gpu_profiler.start_timer(GPU_TAG_SETUP_TARGET);

            self.device.begin_render_pass(&RenderPassDescriptor {
                target: draw_target,
                render_area: target.used_rect,
                color_load: clear.color_load,
                depth_load: clear.depth_load,
            });

            if needs_depth {
                self.device.set_depth_write(true);
            } else {
                self.device.set_depth_write(false);
            }

            self.clear_render_target(
                target,
                draw_target,
                framebuffer_kind,
                &clear,
                &projection,
                stats,
            );

            if needs_depth {
                self.device.set_depth_write(false);
            }
        }

        // Handle any blits from the texture cache to this target.
        self.handle_blits(
            &target.blits,
            render_tasks,
            draw_target,
        );

        // Draw any borders for this target.
        if !target.border_segments_solid.is_empty() ||
           !target.border_segments_complex.is_empty() ||
           !target.border_segments_solid_superellipse.is_empty() ||
           !target.border_segments_complex_superellipse.is_empty()
        {
            let _timer = self.gpu_profiler.start_timer(GPU_TAG_CACHE_BORDER);

            self.set_blend_mode(BlendMode::PremultipliedAlpha, FramebufferKind::Other);

            if !target.border_segments_solid.is_empty() {
                self.shaders.borrow_mut().cs_border_solid().bind(
                    &mut self.device,
                    &projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                self.draw_instanced_batch(
                    &target.border_segments_solid,
                    VertexArrayKind::Border,
                    &BatchTextures::empty(),
                    stats,
                );
            }

            if !target.border_segments_complex.is_empty() {
                self.shaders.borrow_mut().cs_border_segment().bind(
                    &mut self.device,
                    &projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                self.draw_instanced_batch(
                    &target.border_segments_complex,
                    VertexArrayKind::Border,
                    &BatchTextures::empty(),
                    stats,
                );
            }

            if !target.border_segments_solid_superellipse.is_empty() {
                self.shaders.borrow_mut().cs_border_solid_superellipse().bind(
                    &mut self.device,
                    &projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                self.draw_instanced_batch(
                    &target.border_segments_solid_superellipse,
                    VertexArrayKind::Border,
                    &BatchTextures::empty(),
                    stats,
                );
            }

            if !target.border_segments_complex_superellipse.is_empty() {
                self.shaders.borrow_mut().cs_border_segment_superellipse().bind(
                    &mut self.device,
                    &projection,
                    None,
                    &mut self.renderer_errors,
                    &mut self.profile,
                    &mut self.command_log,
                );

                self.draw_instanced_batch(
                    &target.border_segments_complex_superellipse,
                    VertexArrayKind::Border,
                    &BatchTextures::empty(),
                    stats,
                );
            }

            self.set_blend_mode(BlendMode::None, FramebufferKind::Other);
        }

        // Draw any line decorations for this target.
        if !target.line_decorations.is_empty() {
            let _timer = self.gpu_profiler.start_timer(GPU_TAG_CACHE_LINE_DECORATION);

            self.set_blend_mode(BlendMode::PremultipliedAlpha, FramebufferKind::Other);

            self.shaders.borrow_mut().cs_line_decoration().bind(
                &mut self.device,
                &projection,
                None,
                &mut self.renderer_errors,
                &mut self.profile,
                &mut self.command_log,
            );

            self.draw_instanced_batch(
                &target.line_decorations,
                VertexArrayKind::LineDecoration,
                &BatchTextures::empty(),
                stats,
            );

            self.set_blend_mode(BlendMode::None, FramebufferKind::Other);
        }


        // Draw any blurs for this target.
        // Blurs are rendered as a standard 2-pass
        // separable implementation.
        // TODO(gw): In the future, consider having
        //           fast path blur shaders for common
        //           blur radii with fixed weights.
        if !target.vertical_blurs.is_empty() || !target.horizontal_blurs.is_empty() {
            let _timer = self.gpu_profiler.start_timer(GPU_TAG_BLUR);

            self.set_blend_mode(BlendMode::None, framebuffer_kind);
            self.shaders.borrow_mut().cs_blur_rgba8().bind(
                &mut self.device,
                &projection,
                None,
                &mut self.renderer_errors,
                &mut self.profile,
                &mut self.command_log,
            );

            if !target.vertical_blurs.is_empty() {
                self.draw_blurs(
                    &target.vertical_blurs,
                    stats,
                );
            }

            if !target.horizontal_blurs.is_empty() {
                self.draw_blurs(
                    &target.horizontal_blurs,
                    stats,
                );
            }
        }

        self.handle_scaling(
            &target.scalings,
            &projection,
            stats,
        );

        for (ref textures, ref filters) in &target.svg_nodes {
            self.handle_svg_nodes(textures, filters, &projection, stats);
        }

        for alpha_batch_container in &target.alpha_batch_containers {
            self.draw_alpha_batch_container(
                alpha_batch_container,
                draw_target,
                framebuffer_kind,
                &projection,
                render_tasks,
                stats,
            );
        }

        self.handle_prims(
            &draw_target,
            &target.prim_instances,
            &target.prim_instances_with_scissor,
            &projection,
            stats,
        );

        // Draw the clip items into the tiled alpha mask.
        if !target.clip_masks.is_empty() && !self.debug_flags.contains(DebugFlags::DISABLE_CLIP_MASKS) {
            let _timer = self.gpu_profiler.start_timer(GPU_TAG_CACHE_CLIP);

            self.handle_clips(
                &draw_target,
                target.target_kind,
                &target.clip_masks,
                &projection,
                stats,
            );
        }

        self.device.end_render_pass(if needs_depth {
            StoreOp::Discard
        } else {
            StoreOp::Store
        });

        if let Some(sampler) = sampler_query {
            self.gpu_profiler.finish_sampler(sampler);
        }
    }

    fn draw_blurs(
        &mut self,
        blurs: &FastHashMap<TextureSource, FrameVec<BlurInstance>>,
        stats: &mut RendererStats,
    ) {
        for (texture, blurs) in blurs {
            let textures = BatchTextures::composite_rgb(
                *texture,
            );

            self.draw_instanced_batch(
                blurs,
                VertexArrayKind::Blur,
                &textures,
                stats,
            );
        }
    }

    /// Draw all the instances in a clip batcher list to the current target.
    fn bind_frame_data(&mut self, frame: &mut Frame, results: &mut RenderResults) {
        tracy_rs::profile_scope!("bind_frame_data");

        let _timer = self.gpu_profiler.start_timer(GPU_TAG_SETUP_DATA);

        if self.vertex_data.update(
            &mut self.device,
            &mut self.texture_upload_buffer_pool,
            frame,
        ) {
            results.vertex_data_uploads += 1;
        }

        if let Some(texture) = &self.gpu_buffer_texture_f {
            self.device.bind_texture(
                TextureSampler::GpuBufferF,
                &texture,
                Swizzle::default(),
            );
        }

        if let Some(texture) = &self.gpu_buffer_texture_i {
            self.device.bind_texture(
                TextureSampler::GpuBufferI,
                &texture,
                Swizzle::default(),
            );
        }

        if let Some(texture) = &self.gpu_buffer_texture_edges {
            self.device.bind_texture(
                TextureSampler::PathEdges,
                &texture,
                Swizzle::default(),
            );
        }
    }

    fn update_native_surfaces(&mut self) {
        tracy_rs::profile_scope!("update_native_surfaces");

        match self.compositor_config {
            CompositorConfig::Native { ref mut compositor, .. } => {
                for op in self.pending_native_surface_updates.drain(..) {
                    match op.details {
                        NativeSurfaceOperationDetails::CreateSurface { id, tile_size, is_opaque } => {
                            let _inserted = self.allocated_native_surfaces.insert(id);
                            debug_assert!(_inserted, "bug: creating existing surface");
                            compositor.create_surface(
                                id,
                                tile_size,
                                is_opaque,
                            );
                        }
                        NativeSurfaceOperationDetails::CreateExternalSurface { id, is_opaque } => {
                            let _inserted = self.allocated_native_surfaces.insert(id);
                            debug_assert!(_inserted, "bug: creating existing surface");
                            compositor.create_external_surface(id, is_opaque);
                        }
                        NativeSurfaceOperationDetails::CreateBackdropSurface { id, color } => {
                            let _inserted = self.allocated_native_surfaces.insert(id);
                            debug_assert!(_inserted, "bug: creating existing surface");
                            compositor.create_backdrop_surface(id, color);
                        }
                        NativeSurfaceOperationDetails::DestroySurface { id } => {
                            let _existed = self.allocated_native_surfaces.remove(&id);
                            debug_assert!(_existed, "bug: removing unknown surface");
                            compositor.destroy_surface(id);
                        }
                        NativeSurfaceOperationDetails::CreateTile { id } => {
                            compositor.create_tile(id);
                        }
                        NativeSurfaceOperationDetails::DestroyTile { id } => {
                            compositor.destroy_tile(id);
                        }
                        NativeSurfaceOperationDetails::AttachExternalImage { id, external_image } => {
                            compositor.attach_external_image(id, external_image);
                        }
                    }
                }
            }
            CompositorConfig::Draw { .. } | CompositorConfig::Layer { .. } => {
                // Ensure nothing is added in simple composite mode, since otherwise
                // memory will leak as this doesn't get drained
                debug_assert!(self.pending_native_surface_updates.is_empty());
            }
        }
    }

    fn update_gpu_buffer_texture<T: Texel + Copy + PartialEq>(
        device: &mut Device,
        buffer: &GpuBuffer<T>,
        dst_texture: &mut Option<Texture>,
        last_data: &mut Vec<T>,
        pbo_pool: &mut UploadBufferPool,
    ) {
        if buffer.is_empty() {
            return;
        }

        if let Some(texture) = dst_texture {
            assert!(texture.get_dimensions().width == buffer.size.width);
            if texture.get_dimensions().height < buffer.size.height {
                device.delete_texture(dst_texture.take().unwrap());
                last_data.clear();
            }
        }

        // Retaining the data is only worth it for small buffers, so anything larger
        // is treated as always dirty. Note that comparing floats means a buffer
        // containing NaN always counts as changed, which errs towards uploading.
        let retain = buffer.data.len() <= MAX_RETAINED_GPU_BUFFER_BLOCKS;

        if retain && dst_texture.is_some() && last_data.as_slice() == &buffer.data[..] {
            return;
        }

        last_data.clear();
        if retain {
            last_data.extend_from_slice(&buffer.data[..]);
        } else {
            last_data.shrink_to_fit();
        }

        if dst_texture.is_none() {
            let height = preferred_gpu_buffer_texture_height(buffer.size.height);
            assert!(height >= buffer.size.height);
            *dst_texture = Some(
                device.create_texture(
                    ImageBufferKind::Texture2D,
                    buffer.format,
                    buffer.size.width,
                    height,
                    TextureFilter::Nearest,
                    None,
                )
            );
        }

        let mut uploader = device.upload_texture(pbo_pool);

        uploader.upload(
            device,
            dst_texture.as_mut().unwrap(),
            DeviceIntRect {
                min: DeviceIntPoint::zero(),
                max: DeviceIntPoint::new(buffer.size.width, buffer.size.height),
            },
            None,
            None,
            buffer.data.as_ptr(),
            buffer.data.len(),
        );

        uploader.flush(device);
    }

    fn maybe_evict_gpu_buffer_texture<T>(
        device: &mut Device,
        gpu_buffer_height: i32,
        texture: &mut Option<Texture>,
        texture_too_large: &mut i32,
        last_data: &mut Vec<T>,
    ) {
        if let Some(tex) = texture {
            if tex.get_dimensions().height > gpu_buffer_height * 2
                && tex.get_dimensions().height
                    > preferred_gpu_buffer_texture_height(gpu_buffer_height) {
                *texture_too_large += 1;
            } else {
                *texture_too_large = 0;
            }
        }

        // Delete the texture if it has been too large for 10 frames
        // or more.
        if *texture_too_large > 10 {
            device.delete_texture(texture.take().unwrap());
            *texture_too_large = 0;
            *last_data = Vec::new();
        }
    }

    fn draw_frame(
        &mut self,
        frame: &mut Frame,
        device_size: Option<DeviceIntSize>,
        buffer_age: usize,
        results: &mut RenderResults,
    ) {
        tracy_rs::profile_scope!("draw_frame");

        // These markers seem to crash a lot on Android, see bug 1559834
        #[cfg(not(target_os = "android"))]
        let _gm = self.gpu_profiler.start_marker("draw frame");

        if frame.passes.is_empty() {
            frame.has_been_rendered = true;
            return;
        }

        {
            let _gm = self.gpu_profiler.start_marker("gpu buffer update");

            Self::update_gpu_buffer_texture(
                &mut self.device,
                &frame.gpu_buffer_f,
                &mut self.gpu_buffer_texture_f,
                &mut self.gpu_buffer_last_data_f,
                &mut self.texture_upload_buffer_pool,
            );
            Self::update_gpu_buffer_texture(
                &mut self.device,
                &frame.gpu_buffer_i,
                &mut self.gpu_buffer_texture_i,
                &mut self.gpu_buffer_last_data_i,
                &mut self.texture_upload_buffer_pool,
            );
            Self::update_gpu_buffer_texture(
                &mut self.device,
                &frame.gpu_buffer_edges,
                &mut self.gpu_buffer_texture_edges,
                &mut self.gpu_buffer_last_data_edges,
                &mut self.texture_upload_buffer_pool,
            );
        }

        self.device.set_depth_write(false);
        self.set_blend_mode(BlendMode::None, FramebufferKind::Other);

        self.bind_frame_data(frame, results);

        let bytes_to_mb = 1.0 / 1000000.0;
        let gpu_buffer_bytes_f = frame.gpu_buffer_f.size.to_f32().area() * 16.0;
        let gpu_buffer_bytes_i = frame.gpu_buffer_i.size.to_f32().area() * 16.0;
        let gpu_buffer_bytes_edges = frame.gpu_buffer_edges.size.to_f32().area() * 4.0;
        let gpu_buffer_mb = (gpu_buffer_bytes_f + gpu_buffer_bytes_i + gpu_buffer_bytes_edges) as f32 * bytes_to_mb;
        self.profile.set(profiler::GPU_BUFFER_MEM, gpu_buffer_mb);

        // Determine the present mode and dirty rects, if device_size
        // is Some(..). If it's None, no composite will occur and only
        // picture cache and texture cache targets will be updated.
        // TODO(gw): Split Frame so that it's clearer when a composite
        //           is occurring.
        let present_mode = device_size.and_then(|device_size| {
            self.calculate_dirty_rects(
                buffer_age,
                &frame.composite_state,
                device_size,
                results,
            )
        });

        // If we have a native OS compositor, then make use of that interface to
        // specify how to composite each of the picture cache surfaces. First, we
        // need to find each tile that may be bound and updated later in the frame
        // and invalidate it so that the native render compositor knows that these
        // tiles can't be composited early. Next, after all such tiles have been
        // invalidated, then we queue surfaces for native composition by the render
        // compositor before we actually update the tiles. This allows the render
        // compositor to start early composition while the tiles are updating.
        if let CompositorKind::Native { .. } = self.current_compositor_kind {
            let compositor = self.compositor_config.compositor().unwrap();
            // Invalidate any native surface tiles that might be updated by passes.
            if !frame.has_been_rendered {
                for tile in &frame.composite_state.tiles {
                    if !tile.local_dirty_rect.is_empty() {
                        if let CompositeTileSurface::Texture { surface: ResolvedSurfaceTexture::Native { id, .. } } = tile.surface {
                            let valid_rect = frame.composite_state.get_surface_rect(
                                &tile.local_valid_rect,
                                &tile.local_rect,
                                tile.transform_index,
                            ).to_i32();

                            compositor.invalidate_tile(id, valid_rect);
                        }
                    }
                }
            }
            // Ensure any external surfaces that might be used during early composition
            // are invalidated first so that the native compositor can properly schedule
            // composition to happen only when the external surface is updated.
            // See update_external_native_surfaces for more details.
            for surface in &frame.composite_state.external_surfaces {
                if let Some((native_surface_id, size)) = surface.update_params {
                    let surface_rect = size.into();
                    compositor.invalidate_tile(NativeTileId { surface_id: native_surface_id, x: 0, y: 0 }, surface_rect);
                }
            }
            // Finally queue native surfaces for early composition, if applicable. By now,
            // we have already invalidated any tiles that such surfaces may depend upon, so
            // the native render compositor can keep track of when to actually schedule
            // composition as surfaces are updated.
            if device_size.is_some() {
                frame.composite_state.composite_native(
                    self.clear_color,
                    &results.dirty_rects,
                    &mut **compositor,
                );
            }
        }

        for (_pass_index, pass) in frame.passes.iter_mut().enumerate() {
            #[cfg(not(target_os = "android"))]
            let _gm = self.gpu_profiler.start_marker(&format!("pass {}", _pass_index));

            tracy_rs::profile_scope!("offscreen target");

            // If this frame has already been drawn, then any texture
            // cache targets have already been updated and can be
            // skipped this time.
            if !frame.has_been_rendered {
                for (&texture_id, target) in &pass.texture_cache {
                    self.draw_render_target(
                        texture_id,
                        target,
                        &frame.render_tasks,
                        &mut results.stats,
                    );
                }

                if !pass.picture_cache.is_empty() {
                    self.profile.inc(profiler::COLOR_PASSES);
                }

                // Draw picture caching tiles for this pass.
                for picture_target in &pass.picture_cache {
                    results.stats.color_target_count += 1;

                    let draw_target = match picture_target.surface {
                        ResolvedSurfaceTexture::TextureCache { ref texture } => {
                            let (texture, _) = self.texture_resolver
                                .resolve(texture)
                                .expect("bug");

                            DrawTarget::from_texture(
                                texture,
                                true,
                            )
                        }
                        ResolvedSurfaceTexture::Native { id, size } => {
                            let surface_info = match self.current_compositor_kind {
                                CompositorKind::Native { .. } => {
                                    let compositor = self.compositor_config.compositor().unwrap();
                                    compositor.bind(
                                        id,
                                        picture_target.dirty_rect,
                                        picture_target.valid_rect,
                                    )
                                }
                                CompositorKind::Draw { .. } | CompositorKind::Layer { .. } => {
                                    unreachable!();
                                }
                            };

                            DrawTarget::NativeSurface {
                                offset: surface_info.origin,
                                handle: surface_info.handle,
                                dimensions: size,
                            }
                        }
                    };

                    let projection = Transform3D::ortho(
                        0.0,
                        draw_target.dimensions().width as f32,
                        0.0,
                        draw_target.dimensions().height as f32,
                        self.device.ortho_near_plane(),
                        self.device.ortho_far_plane(),
                    );

                    self.draw_picture_cache_target(
                        picture_target,
                        draw_target,
                        &projection,
                        &frame.render_tasks,
                        &mut results.stats,
                    );

                    // Native OS surfaces must be unbound at the end of drawing to them
                    if let ResolvedSurfaceTexture::Native { .. } = picture_target.surface {
                        match self.current_compositor_kind {
                            CompositorKind::Native { .. } => {
                                let compositor = self.compositor_config.compositor().unwrap();
                                compositor.unbind();
                            }
                            CompositorKind::Draw { .. } | CompositorKind::Layer { .. } => {
                                unreachable!();
                            }
                        }
                    }
                }
            }

            for target in &pass.alpha.targets {
                results.stats.alpha_target_count += 1;
                self.draw_render_target(
                    target.texture_id(),
                    target,
                    &frame.render_tasks,
                    &mut results.stats,
                );
            }

            for target in &pass.color.targets {
                results.stats.color_target_count += 1;
                self.draw_render_target(
                    target.texture_id(),
                    target,
                    &frame.render_tasks,
                    &mut results.stats,
                );
            }

            // Only end the pass here and invalidate previous textures for
            // off-screen targets. Deferring return of the inputs to the
            // frame buffer until the implicit end_pass in end_frame allows
            // debug draw overlays to be added without triggering a copy
            // resolve stage in mobile / tiled GPUs.
            self.texture_resolver.end_pass(
                &mut self.device,
                &pass.textures_to_invalidate,
            );
        }

        self.composite_frame(
            frame,
            device_size,
            results,
            present_mode,
        );

        frame.has_been_rendered = true;

        Self::maybe_evict_gpu_buffer_texture(
            &mut self.device,
            frame.gpu_buffer_f.size.height,
            &mut self.gpu_buffer_texture_f,
            &mut self.gpu_buffer_texture_f_too_large,
            &mut self.gpu_buffer_last_data_f,
        );

        Self::maybe_evict_gpu_buffer_texture(
            &mut self.device,
            frame.gpu_buffer_i.size.height,
            &mut self.gpu_buffer_texture_i,
            &mut self.gpu_buffer_texture_i_too_large,
            &mut self.gpu_buffer_last_data_i,
        );

        Self::maybe_evict_gpu_buffer_texture(
            &mut self.device,
            frame.gpu_buffer_edges.size.height,
            &mut self.gpu_buffer_texture_edges,
            &mut self.gpu_buffer_texture_edges_too_large,
            &mut self.gpu_buffer_last_data_edges,
        );
    }

    pub fn debug_renderer(&mut self) -> Option<&mut DebugRenderer> {
        self.debug.get_mut(&mut self.device)
    }

    pub fn get_debug_flags(&self) -> DebugFlags {
        self.debug_flags
    }

    pub fn set_debug_flags(&mut self, flags: DebugFlags) {
        if let Some(enabled) = flag_changed(self.debug_flags, flags, DebugFlags::GPU_TIME_QUERIES) {
            if enabled {
                self.gpu_profiler.enable_timers();
            } else {
                self.gpu_profiler.disable_timers();
            }
        }
        if let Some(enabled) = flag_changed(self.debug_flags, flags, DebugFlags::GPU_SAMPLE_QUERIES) {
            if enabled {
                self.gpu_profiler.enable_samplers();
            } else {
                self.gpu_profiler.disable_samplers();
            }
        }

        self.device.set_initialize_color_targets_with_pink(
            flags.contains(DebugFlags::COLOR_TARGET_INIT),
        );

        self.debug_flags = flags;
    }

    pub fn set_profiler_ui(&mut self, ui_str: &str) {
        self.profiler.set_ui(ui_str);
    }

    /// Reads back the presented frame; used by Gecko's WR bindings.
    pub fn read_pixels_into(&mut self, rect: FramebufferIntRect, format: ImageFormat, output: &mut [u8]) {
        self.device.read_pixels_into(ReadTarget::Default, rect, format, output);
    }

    pub fn read_pixels_rgba8(&mut self, rect: FramebufferIntRect) -> Vec<u8> {
        let mut pixels = vec![0; (rect.area() * 4) as usize];
        self.device.read_pixels_into(ReadTarget::Default, rect, ImageFormat::RGBA8, &mut pixels);
        pixels
    }

    // De-initialize the Renderer safely, assuming the GL is still alive and active.
    pub fn deinit(mut self) {
        //Note: this is a fake frame, only needed because texture deletion is require to happen inside a frame
        self.device.begin_frame();
        // Destroy any swapchain surfaces owned by the Layer compositor. This must
        // happen before the native compositor is moved out of the config below.
        self.layer_tree.deinit(&mut self.compositor_config);
        // If we are using a native compositor, ensure that any remaining native
        // surfaces are freed.
        if let CompositorConfig::Native { mut compositor, .. } = self.compositor_config {
            for id in self.allocated_native_surfaces.drain() {
                compositor.destroy_surface(id);
            }
            // Destroy the debug overlay surface, if currently allocated.
            if self.debug_overlay_state.current_size.is_some() {
                compositor.destroy_surface(NativeSurfaceId::DEBUG_OVERLAY);
            }
            compositor.deinit();
        }
        if let Some(dither_matrix_texture) = self.dither_matrix_texture {
            self.device.delete_texture(dither_matrix_texture);
        }
        if let Some(zoom_debug_texture) = self.zoom_debug_texture {
            self.device.delete_texture(zoom_debug_texture);
        }
        if let Some(texture) = self.gpu_buffer_texture_f {
            self.device.delete_texture(texture);
        }
        if let Some(texture) = self.gpu_buffer_texture_i {
            self.device.delete_texture(texture);
        }
        if let Some(texture) = self.gpu_buffer_texture_edges {
            self.device.delete_texture(texture);
        }
        self.vertex_data.deinit(&mut self.device);
        self.texture_upload_buffer_pool.deinit(&mut self.device);
        self.staging_texture_pool.delete_textures(&mut self.device);
        self.texture_resolver.deinit(&mut self.device);
        self.vaos.deinit(&mut self.device);
        self.debug.deinit(&mut self.device);

        if let Ok(shaders) = Rc::try_unwrap(self.shaders) {
            shaders.into_inner().deinit(&mut self.device);
        }

        if let Some(async_screenshots) = self.async_screenshots.take() {
            async_screenshots.deinit(&mut self.device);
        }

        if let Some(async_frame_recorder) = self.async_frame_recorder.take() {
            async_frame_recorder.deinit(&mut self.device);
        }

        self.device.deinit();
        #[cfg(feature = "replay")]
        for (_, ext) in self.owned_external_images {
            self.device.delete_external_texture(ext);
        }
        self.device.end_frame();
    }

    /// Collects a memory report.
    pub fn report_memory(&self, swgl: *mut c_void) -> MemoryReport {
        let mut report = MemoryReport::default();

        self.staging_texture_pool.report_memory_to(&mut report, self.size_of_ops.as_ref().unwrap());

        // Render task CPU memory.
        for (_id, doc) in &self.active_documents {
            let frame_alloc_stats = doc.frame.allocator_memory.get_stats();
            report.frame_allocator += frame_alloc_stats.reserved_bytes;
            report.render_tasks += doc.frame.render_tasks.report_memory();
        }

        // Vertex data GPU memory.
        report.vertex_data_textures += self.vertex_data.gpu_size_in_bytes();

        // CPU-side copy of the last vertex data upload, used to skip redundant
        // uploads. Grouped with the other CPU-side upload staging memory.
        report.upload_staging_memory += self.vertex_data.cpu_size_in_bytes();

        // Texture cache and render target GPU memory.
        report += self.texture_resolver.report_memory();

        // Texture upload buffer memory.
        report += self.texture_upload_buffer_pool.report_memory();

        // Textures held internally within the device layer.
        report += self.device.report_memory();

        #[cfg(feature = "sw_compositor")]
        if !swgl.is_null() {
            let size_of_op = self.size_of_ops.as_ref().unwrap().size_of_op;
            report.swgl += swgl::Context::from(swgl).report_memory(size_of_op);
        }
        #[cfg(not(feature = "sw_compositor"))]
        let _ = swgl;

        report
    }

    // Sets the blend mode. Blend is unconditionally set if the "show overdraw" debugging mode is
    // enabled.
    fn set_blend_mode(&mut self, mut mode: BlendMode, framebuffer_kind: FramebufferKind) {
        if framebuffer_kind == FramebufferKind::Main &&
                self.debug_flags.contains(DebugFlags::SHOW_OVERDRAW) {
            mode = BlendMode::ShowOverdraw;
        }
        self.device.set_blend_mode(mode);
    }

    /// Clears the texture with a given color.
    fn clear_texture(&mut self, texture: &Texture, color: [f32; 4]) {
        self.device.begin_render_pass(&RenderPassDescriptor {
            target: DrawTarget::from_texture(&texture, false),
            render_area: None,
            color_load: LoadOp::Clear(color),
            depth_load: LoadOp::DontCare,
        });
        self.device.end_render_pass(StoreOp::Store);
    }
}

bitflags! {
    /// Flags that control how shaders are pre-cached, if at all.
    #[derive(Default, Debug, Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash)]
    pub struct ShaderPrecacheFlags: u32 {
        /// Needed for const initialization
        const EMPTY                 = 0;

        /// Only start async compile
        const ASYNC_COMPILE         = 1 << 2;

        /// Do a full compile/link during startup
        const FULL_COMPILE          = 1 << 3;
    }
}

/// The cumulative times spent in each painting phase to generate this frame.
#[derive(Debug, Default)]
pub struct FullFrameStats {
    pub full_display_list: bool,
    pub gecko_display_list_time: f64,
    pub wr_display_list_time: f64,
    pub scene_build_time: f64,
    pub frame_build_time: f64,
}

impl FullFrameStats {
    pub fn merge(&self, other: &FullFrameStats) -> Self {
        Self {
            full_display_list: self.full_display_list || other.full_display_list,
            gecko_display_list_time: self.gecko_display_list_time + other.gecko_display_list_time,
            wr_display_list_time: self.wr_display_list_time + other.wr_display_list_time,
            scene_build_time: self.scene_build_time + other.scene_build_time,
            frame_build_time: self.frame_build_time + other.frame_build_time
        }
    }

    pub fn total(&self) -> f64 {
      self.gecko_display_list_time + self.wr_display_list_time + self.scene_build_time + self.frame_build_time
    }
}

/// Some basic statistics about the rendered scene, used in Gecko, as
/// well as in wrench reftests to ensure that tests are batching and/or
/// allocating on render targets as we expect them to.
#[repr(C)]
#[derive(Debug, Default)]
pub struct RendererStats {
    pub total_draw_calls: usize,
    pub alpha_target_count: usize,
    pub color_target_count: usize,
    pub texture_upload_mb: f64,
    pub resource_upload_time: f64,
    pub gecko_display_list_time: f64,
    pub wr_display_list_time: f64,
    pub scene_build_time: f64,
    pub frame_build_time: f64,
    pub full_display_list: bool,
    pub full_paint: bool,
}

impl RendererStats {
    pub fn merge(&mut self, stats: &FullFrameStats) {
        self.gecko_display_list_time = stats.gecko_display_list_time;
        self.wr_display_list_time = stats.wr_display_list_time;
        self.scene_build_time = stats.scene_build_time;
        self.frame_build_time = stats.frame_build_time;
        self.full_display_list = stats.full_display_list;
        self.full_paint = true;
    }
}

/// Return type from render(), which contains some repr(C) statistics as well as
/// some non-repr(C) data.
#[derive(Debug, Default)]
pub struct RenderResults {
    /// Statistics about the frame that was rendered.
    pub stats: RendererStats,

    /// A list of the device dirty rects that were updated
    /// this frame.
    /// TODO(gw): This is an initial interface, likely to change in future.
    /// TODO(gw): The dirty rects here are currently only useful when scrolling
    ///           is not occurring. They are still correct in the case of
    ///           scrolling, but will be very large (until we expose proper
    ///           OS compositor support where the dirty rects apply to a
    ///           specific picture cache slice / OS compositor surface).
    pub dirty_rects: Vec<DeviceIntRect>,

    /// Information about the state of picture cache tiles. This is only
    /// allocated and stored if config.testing is true (such as wrench)
    pub picture_cache_debug: PictureCacheDebugInfo,

    /// Whether any tile was rasterized (had is_valid = false)
    pub did_rasterize_any_tile: bool,

    /// Number of primitives promoted to overlay compositor surfaces during the
    /// frame.
    pub compositor_surface_overlays: usize,

    /// Number of primitives promoted to underlay compositor surfaces during the
    /// frame. Underlays cancelled later in the frame are still counted here.
    pub compositor_surface_underlays: usize,

    /// Number of times the vertex data textures were uploaded during the frame.
    /// Zero when the frame produced data identical to the previous upload.
    pub vertex_data_uploads: usize,
}

#[cfg(any(feature = "capture", feature = "replay"))]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
struct PlainTexture {
    data: String,
    size: DeviceIntSize,
    format: ImageFormat,
    filter: TextureFilter,
    has_depth: bool,
    category: Option<TextureCacheCategory>,
}


#[cfg(any(feature = "capture", feature = "replay"))]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
struct PlainRenderer {
    device_size: Option<DeviceIntSize>,
    textures: FastHashMap<CacheTextureId, PlainTexture>,
}

#[cfg(any(feature = "capture", feature = "replay"))]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
struct PlainExternalResources {
    images: Vec<ExternalCaptureImage>
}

#[cfg(feature = "replay")]
enum CapturedExternalImageData {
    NativeTexture(ExternalTextureHandle),
    Buffer(Arc<Vec<u8>>),
}

#[cfg(feature = "replay")]
struct DummyExternalImageHandler {
    data: FastHashMap<(ExternalImageId, u8), (CapturedExternalImageData, TexelRect)>,
}

#[cfg(feature = "replay")]
impl ExternalImageHandler for DummyExternalImageHandler {
    fn lock(&mut self, key: ExternalImageId, channel_index: u8, _is_composited: bool) -> ExternalImage {
        let (ref captured_data, ref uv) = self.data[&(key, channel_index)];
        ExternalImage {
            uv: *uv,
            source: match *captured_data {
                CapturedExternalImageData::NativeTexture(tid) => ExternalImageSource::NativeTexture(tid),
                CapturedExternalImageData::Buffer(ref arc) => ExternalImageSource::RawData(&*arc),
            }
        }
    }
    fn unlock(&mut self, _key: ExternalImageId, _channel_index: u8) {}
}

#[derive(Default)]
pub struct PipelineInfo {
    pub epochs: FastHashMap<(PipelineId, DocumentId), Epoch>,
    pub removed_pipelines: Vec<(PipelineId, DocumentId)>,
}

impl Renderer {
    #[cfg(feature = "capture")]
    fn save_texture(
        texture: &Texture, category: Option<TextureCacheCategory>, name: &str, root: &PathBuf, device: &mut Device
    ) -> PlainTexture {
        use std::fs;
        use std::io::Write;

        let short_path = format!("textures/{}.raw", name);

        let bytes_per_pixel = texture.get_format().bytes_per_pixel();
        let read_format = texture.get_format();
        let rect_size = texture.get_dimensions();

        let mut file = fs::File::create(root.join(&short_path))
            .expect(&format!("Unable to create {}", short_path));
        let bytes_per_texture = (rect_size.width * rect_size.height * bytes_per_pixel) as usize;
        let mut data = vec![0; bytes_per_texture];

        #[cfg(feature = "png")]
        {
            let mut png_data;
            let (data_ref, format) = match texture.get_format() {
                ImageFormat::RGBAF32 => {
                    png_data = vec![0; (rect_size.width * rect_size.height * 4) as usize];
                    device.read_texture(texture, ImageFormat::RGBA8, &mut png_data);
                    (&png_data, ImageFormat::RGBA8)
                }
                fm => (&data, fm),
            };
            CaptureConfig::save_png(
                root.join(format!("textures/{}-{}.png", name, 0)),
                rect_size, format,
                None,
                data_ref,
            );
        }
        device.read_texture(texture, read_format, &mut data);
        file.write_all(&data)
            .unwrap();

        PlainTexture {
            data: short_path,
            size: rect_size,
            format: texture.get_format(),
            filter: texture.get_filter(),
            has_depth: texture.supports_depth(),
            category,
        }
    }

    #[cfg(feature = "replay")]
    fn load_texture(
        target: ImageBufferKind,
        plain: &PlainTexture,
        rt_info: Option<RenderTargetInfo>,
        root: &PathBuf,
        device: &mut Device
    ) -> (Texture, Vec<u8>)
    {
        use std::fs::File;
        use std::io::Read;

        let mut texels = Vec::new();
        File::open(root.join(&plain.data))
            .expect(&format!("Unable to open texture at {}", plain.data))
            .read_to_end(&mut texels)
            .unwrap();

        let texture = device.create_texture(
            target,
            plain.format,
            plain.size.width,
            plain.size.height,
            plain.filter,
            rt_info,
        );
        device.upload_texture_immediate(&texture, &texels);

        (texture, texels)
    }

    #[cfg(feature = "capture")]
    fn save_capture(
        &mut self,
        config: CaptureConfig,
        deferred_images: Vec<ExternalCaptureImage>,
    ) {
        use std::fs;
        use std::io::Write;
        use api::ExternalImageData;
        use crate::render_api::CaptureBits;

        let root = config.resource_root();

        self.device.begin_frame();
        let _gm = self.gpu_profiler.start_marker("read GPU data");

        if config.bits.contains(CaptureBits::EXTERNAL_RESOURCES) && !deferred_images.is_empty() {
            info!("saving external images");
            let mut arc_map = FastHashMap::<*const u8, String>::default();
            let mut tex_map = FastHashMap::<ExternalTextureHandle, String>::default();
            let handler = self.external_image_handler
                .as_mut()
                .expect("Unable to lock the external image handler!");
            for def in &deferred_images {
                info!("\t{}", def.short_path);
                let ExternalImageData { id, channel_index, image_type, .. } = def.external;
                // The image rendering parameter is irrelevant because no filtering happens during capturing.
                let ext_image = handler.lock(id, channel_index, false);
                let (data, short_path) = match ext_image.source {
                    ExternalImageSource::RawData(data) => {
                        let arc_id = arc_map.len() + 1;
                        match arc_map.entry(data.as_ptr()) {
                            Entry::Occupied(e) => {
                                (None, e.get().clone())
                            }
                            Entry::Vacant(e) => {
                                let short_path = format!("externals/d{}.raw", arc_id);
                                (Some(data.to_vec()), e.insert(short_path).clone())
                            }
                        }
                    }
                    ExternalImageSource::NativeTexture(handle) => {
                        let tex_id = tex_map.len() + 1;
                        match tex_map.entry(handle) {
                            Entry::Occupied(e) => {
                                (None, e.get().clone())
                            }
                            Entry::Vacant(e) => {
                                let target = match image_type {
                                    ExternalImageType::TextureHandle(target) => target,
                                    ExternalImageType::Buffer => unreachable!(),
                                };
                                info!("\t\tnative texture of target {:?}", target);
                                let data = self.device.read_external_texture(handle, target, &def.descriptor);
                                let short_path = format!("externals/t{}.raw", tex_id);
                                (Some(data), e.insert(short_path).clone())
                            }
                        }
                    }
                    ExternalImageSource::Invalid => {
                        info!("\t\tinvalid source!");
                        (None, String::new())
                    }
                };
                if let Some(bytes) = data {
                    fs::File::create(root.join(&short_path))
                        .expect(&format!("Unable to create {}", short_path))
                        .write_all(&bytes)
                        .unwrap();
                    #[cfg(feature = "png")]
                    CaptureConfig::save_png(
                        root.join(&short_path).with_extension("png"),
                        def.descriptor.size,
                        def.descriptor.format,
                        def.descriptor.stride,
                        &bytes,
                    );
                }
                let plain = PlainExternalImage {
                    data: short_path,
                    external: def.external,
                    uv: ext_image.uv,
                };
                config.serialize_for_resource(&plain, &def.short_path);
            }
            for def in &deferred_images {
                handler.unlock(def.external.id, def.external.channel_index);
            }
            let plain_external = PlainExternalResources {
                images: deferred_images,
            };
            config.serialize_for_resource(&plain_external, "external_resources");
        }

        if config.bits.contains(CaptureBits::FRAME) {
            let path_textures = root.join("textures");
            if !path_textures.is_dir() {
                fs::create_dir(&path_textures).unwrap();
            }

            let mut plain_self = PlainRenderer {
                device_size: self.device_size,
                textures: FastHashMap::default(),
            };

            info!("saving cached textures");
            for (id, item) in &self.texture_resolver.texture_cache_map {
                let file_name = format!("cache-{}", plain_self.textures.len() + 1);
                info!("\t{}", file_name);
                let plain = Self::save_texture(&item.texture, Some(item.category), &file_name, &root, &mut self.device);
                plain_self.textures.insert(*id, plain);
            }

            config.serialize_for_resource(&plain_self, "renderer");
        }

        self.device.end_frame();

        let mut stats_file = fs::File::create(config.root.join("profiler-stats.txt"))
            .expect(&format!("Unable to create profiler-stats.txt"));
        if self.debug_flags.intersects(DebugFlags::PROFILER_DBG | DebugFlags::PROFILER_CAPTURE) {
            self.profiler.dump_stats(&mut stats_file).unwrap();
        } else {
            writeln!(stats_file, "Turn on PROFILER_DBG or PROFILER_CAPTURE to get stats here!").unwrap();
        }

        info!("done.");
    }

    #[cfg(feature = "replay")]
    fn load_capture(
        &mut self,
        config: CaptureConfig,
        plain_externals: Vec<PlainExternalImage>,
    ) {
        use std::{fs::File, io::Read};

        info!("loading external buffer-backed images");
        assert!(self.texture_resolver.external_images.is_empty());
        let mut raw_map = FastHashMap::<String, Arc<Vec<u8>>>::default();
        let mut image_handler = DummyExternalImageHandler {
            data: FastHashMap::default(),
        };

        let root = config.resource_root();

        // Note: this is a `SCENE` level population of the external image handlers
        // It would put both external buffers and texture into the map.
        // But latter are going to be overwritten later in this function
        // if we are in the `FRAME` level.
        for plain_ext in plain_externals {
            let data = match raw_map.entry(plain_ext.data) {
                Entry::Occupied(e) => e.get().clone(),
                Entry::Vacant(e) => {
                    let mut buffer = Vec::new();
                    File::open(root.join(e.key()))
                        .expect(&format!("Unable to open {}", e.key()))
                        .read_to_end(&mut buffer)
                        .unwrap();
                    e.insert(Arc::new(buffer)).clone()
                }
            };
            let ext = plain_ext.external;
            let value = (CapturedExternalImageData::Buffer(data), plain_ext.uv);
            image_handler.data.insert((ext.id, ext.channel_index), value);
        }

        self.device.begin_frame();

        if let Some(external_resources) = config.deserialize_for_resource::<PlainExternalResources, _>("external_resources") {
            info!("loading external texture-backed images");
            let mut native_map = FastHashMap::<String, ExternalTextureHandle>::default();
            for ExternalCaptureImage { short_path, external, descriptor } in external_resources.images {
                let target = match external.image_type {
                    ExternalImageType::TextureHandle(target) => target,
                    ExternalImageType::Buffer => continue,
                };
                let plain_ext = config.deserialize_for_resource::<PlainExternalImage, _>(&short_path)
                    .expect(&format!("Unable to read {}.ron", short_path));
                let key = (external.id, external.channel_index);

                let tid = match native_map.entry(plain_ext.data) {
                    Entry::Occupied(e) => e.get().clone(),
                    Entry::Vacant(e) => {
                        let plain_tex = PlainTexture {
                            data: e.key().clone(),
                            size: descriptor.size,
                            format: descriptor.format,
                            filter: TextureFilter::Linear,
                            has_depth: false,
                            category: None,
                        };
                        let t = Self::load_texture(
                            target,
                            &plain_tex,
                            None,
                            &root,
                            &mut self.device
                        );
                        let extex = t.0.into_external();
                        self.owned_external_images.insert(key, extex.clone());
                        e.insert(extex.handle()).clone()
                    }
                };

                let value = (CapturedExternalImageData::NativeTexture(tid), plain_ext.uv);
                image_handler.data.insert(key, value);
            }
        }

        if let Some(renderer) = config.deserialize_for_resource::<PlainRenderer, _>("renderer") {
            info!("loading cached textures");
            self.device_size = renderer.device_size;

            for (_id, item) in self.texture_resolver.texture_cache_map.drain() {
                self.device.delete_texture(item.texture);
            }
            for (id, texture) in renderer.textures {
                info!("\t{}", texture.data);
                let target = ImageBufferKind::Texture2D;
                let t = Self::load_texture(
                    target,
                    &texture,
                    Some(RenderTargetInfo { has_depth: texture.has_depth }),
                    &root,
                    &mut self.device
                );
                self.texture_resolver.texture_cache_map.insert(id, CacheTexture {
                    texture: t.0,
                    category: texture.category.unwrap_or(TextureCacheCategory::Standalone),
                });
            }
        } else {
            info!("loading cached textures");
            self.device.begin_frame();
            for (_id, item) in self.texture_resolver.texture_cache_map.drain() {
                self.device.delete_texture(item.texture);
            }
        }
        self.device.end_frame();

        self.external_image_handler = Some(Box::new(image_handler) as Box<_>);
        info!("done.");
    }
}

#[derive(Clone, Copy, PartialEq)]
enum FramebufferKind {
    Main,
    Other,
}

fn should_skip_batch(kind: &BatchKind, flags: DebugFlags) -> bool {
    match kind {
        BatchKind::TextRun(_) => {
            flags.contains(DebugFlags::DISABLE_TEXT_PRIMS)
        }
        BatchKind::Quad(PatternKind::Gradient) => {
            flags.contains(DebugFlags::DISABLE_GRADIENT_PRIMS)
        }
        _ => false,
    }
}

impl CompositeState {
    /// Use the client provided native compositor interface to add all picture
    /// cache tiles to the OS compositor
    fn composite_native(
        &self,
        clear_color: ColorF,
        dirty_rects: &[DeviceIntRect],
        compositor: &mut dyn Compositor,
    ) {
        // Add each surface to the visual tree. z-order is implicit based on
        // order added. Offset and clip rect apply to all tiles within this
        // surface.
        for surface in &self.descriptor.surfaces {
            compositor.add_surface(
                surface.surface_id.expect("bug: no native surface allocated"),
                surface.transform,
                surface.clip_rect.to_i32(),
                surface.image_rendering,
                surface.rounded_clip_rect.to_i32(),
                surface.rounded_clip_radii,
            );
        }
        compositor.start_compositing(clear_color, dirty_rects, &[]);
    }
}

mod tests {
    #[test]
    fn test_buffer_damage_tracker() {
        use super::BufferDamageTracker;
        use api::units::{DevicePoint, DeviceRect, DeviceSize};

        let mut tracker = BufferDamageTracker::default();
        assert_eq!(tracker.get_damage_rect(0), None);
        assert_eq!(tracker.get_damage_rect(1), Some(DeviceRect::zero()));
        assert_eq!(tracker.get_damage_rect(2), Some(DeviceRect::zero()));
        assert_eq!(tracker.get_damage_rect(3), Some(DeviceRect::zero()));

        let damage1 = DeviceRect::from_origin_and_size(DevicePoint::new(10.0, 10.0), DeviceSize::new(10.0, 10.0));
        let damage2 = DeviceRect::from_origin_and_size(DevicePoint::new(20.0, 20.0), DeviceSize::new(10.0, 10.0));
        let combined = damage1.union(&damage2);

        tracker.push_dirty_rect(&damage1);
        assert_eq!(tracker.get_damage_rect(0), None);
        assert_eq!(tracker.get_damage_rect(1), Some(DeviceRect::zero()));
        assert_eq!(tracker.get_damage_rect(2), Some(damage1));
        assert_eq!(tracker.get_damage_rect(3), Some(damage1));

        tracker.push_dirty_rect(&damage2);
        assert_eq!(tracker.get_damage_rect(0), None);
        assert_eq!(tracker.get_damage_rect(1), Some(DeviceRect::zero()));
        assert_eq!(tracker.get_damage_rect(2), Some(damage2));
        assert_eq!(tracker.get_damage_rect(3), Some(combined));
    }
}
