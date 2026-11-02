/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*!
A GPU based renderer for the web.

WebRender turns display lists into GPU draw calls. It is the rendering engine of
Firefox, and can also be used standalone.

# Api Structure

[`create_webrender_instance()`](crate::create_webrender_instance) returns a
[`Renderer`] plus a [`RenderApiSender`](crate::render_api::RenderApiSender). The
[`Renderer`] owns the GPU connection and draws frames; the sender is how you talk
to everything else.

[`create_api()`](crate::render_api::RenderApiSender::create_api) gives you a
[`RenderApi`](crate::render_api::RenderApi), which manages resources and
documents. Work is submitted as a
[`Transaction`](crate::render_api::Transaction) — most importantly
[`set_display_list()`](crate::render_api::Transaction::set_display_list), which
takes a [`BuiltDisplayList`](api::BuiltDisplayList) produced by finalizing a
[`DisplayListBuilder`](api::DisplayListBuilder). Display lists nest
[stacking contexts][stacking_contexts]. Completion is reported through the
[`RenderNotifier`](api::RenderNotifier) you passed at init.

# Threads

Work is split across threads, and knowing which thread a piece of code runs on
explains most of the structure here:

- **Scene builder thread** (`scene_builder_thread.rs`, `scene_building.rs`)
  turns display lists into a [`BuiltScene`](crate::scene::BuiltScene). Runs
  asynchronously; only re-runs when the display list changes.
- **Render backend thread** (`render_backend.rs`, `frame_builder.rs`) turns a
  scene plus the current scroll/animation state into a `Frame`. Runs every
  frame. This is where most of the interesting logic lives.
- **Render thread** (`renderer/`, `device/`) is the only thread that touches the
  GPU, and is also the initial entry point into the crate.

The `renderer` module documentation describes the render/render-backend split in
more detail. Rayon workers and the [`glyph_rasterizer`] are used for parallel
work underneath these.

# Anatomy of a frame

`FrameBuilder::build` in `frame_builder.rs` runs these stages in order. Each has
its own module, and this is the sequence to follow when tracing why something
renders wrongly:

1. **Picture graph passes** (`picture_graph.rs`) walk the picture tree in
   dependency order, assign off-screen surfaces (`surface.rs`) and propagate
   bounding rects.
2. **Visibility** (`visibility.rs`) culls primitives, resolves clip chains
   (`clip.rs`), snaps rects to the pixel grid, and updates picture-cache tile
   dependencies (`tile_cache/`, `invalidation/`).
3. **Prepare** (`prepare.rs`) walks each visible primitive by `PrimitiveKind`,
   builds its `Pattern` (`pattern/`), requests any render tasks it needs, and
   emits draw commands into command buffers (`command_buffer.rs`). Most
   primitives go through the quad path (`quad.rs`).
4. **Render task graph** (`render_task_graph.rs`) is finalized: tasks are
   assigned to passes and to render target allocations (`render_target.rs`).
5. **Batching** (`batch.rs`) replays the command buffers per pass, grouping
   draws into batches keyed by shader and textures.
6. **Compositing** (`composite.rs`) builds the list of picture-cache tiles to
   present, either drawn by us or handed to an OS compositor (`compositor/`).

The [`Renderer`] then submits the resulting `Frame`: it uploads resources,
executes the passes in order, and composites.

# Further reading

- `gfx/docs/RenderingOverview.md` — how WebRender fits into Gecko, plus the
  picture / spatial / clip / render-task trees. Note it predates the quad and
  pattern architecture described above and still describes the retired brush
  shaders.
- `gfx/wr/webrender/doc/coordinate-spaces.md` — the spatial tree, and the
  local / picture / raster / world / device spaces.
- `gfx/wr/webrender/doc/text-rendering.md`, `blob.md`,
  `CLIPPING_AND_POSITIONING.md`, `swizzling.md` — subsystem deep dives, in
  varying states of currency.

# External dependencies

WebRender depends on [FreeType](https://www.freetype.org/) for font rasterization
on some platforms.

[stacking_contexts]: https://developer.mozilla.org/en-US/docs/Web/CSS/CSS_Positioning/Understanding_z_index/The_stacking_context
*/

#![allow(
    clippy::unreadable_literal,
    clippy::new_without_default,
    clippy::too_many_arguments,
    unknown_lints,
    mismatched_lifetime_syntaxes
)]


// Cribbed from the |matches| crate, for simplicity.
macro_rules! matches {
    ($expression:expr, $($pattern:tt)+) => {
        match $expression {
            $($pattern)+ => true,
            _ => false
        }
    }
}

#[macro_use]
extern crate bitflags;
#[macro_use]
extern crate lazy_static;
#[macro_use]
extern crate log;
#[macro_use]
extern crate malloc_size_of_derive;
#[cfg(any(feature = "serde"))]
#[macro_use]
extern crate serde;
#[macro_use]
extern crate derive_more;
extern crate malloc_size_of;
extern crate svg_fmt;

#[macro_use]
mod profiler;
mod telemetry;

mod batch;
mod bezier;
mod path_tiler;
mod border;
mod border_image;
mod box_shadow;
#[cfg(any(feature = "capture", feature = "replay"))]
mod capture;
mod clip;
mod space;
mod spatial_tree;
mod command_buffer;
mod composite;
mod compositor;
mod debug_colors;
mod debug_font_data;
mod debug_item;
mod device;
mod dl_interner;
mod ellipse;
mod filterdata;
mod frame_builder;
mod freelist;
mod glyph_cache;
mod gpu_types;
mod hit_test;
mod internal_types;
mod lru_cache;
mod pattern;
mod picture;
mod picture_composite_mode;
mod picture_graph;
mod invalidation;
mod prepare;
mod prim_store;
mod print_tree;
mod quad;
mod quad_clip;
mod render_backend;
pub mod render_backend_pool;
mod render_target;
mod render_task_graph;
mod render_task_cache;
mod render_task;
#[cfg(feature = "debugger")]
mod renderdoc;
mod renderer;
mod resource_cache;
pub mod scene;
mod scene_builder_thread;
mod scene_building;
mod scene_debug;
mod screen_capture;
mod segment;
#[cfg(test)]
mod shutdown_test;
mod spatial_node;
mod surface;
mod texture_pack;
mod texture_cache;
mod transform;
mod tile_cache;
mod util;
mod visibility;
mod api_resources;
mod image_tiling;
mod image_source;
mod rectangle_occlusion;
mod picture_textures;
mod frame_allocator;
mod bump_allocator;
mod svg_filter;

///
pub mod intern;
///
pub mod render_api;

pub mod shader_source {
    include!(concat!(env!("OUT_DIR"), "/shaders.rs"));
}

extern crate bincode;
extern crate byteorder;
pub extern crate euclid;
extern crate rustc_hash;
extern crate num_traits;
extern crate plane_split;
extern crate rayon;
#[cfg(feature = "ron")]
extern crate ron;
#[macro_use]
extern crate smallvec;
#[cfg(all(feature = "capture", feature = "png"))]
extern crate png;
#[cfg(test)]
extern crate rand;

pub extern crate api;
extern crate webrender_build;

#[doc(hidden)]
pub use crate::composite::{CompositorInputConfig, CompositorSurfaceUsage, ClipRadius};
pub use crate::composite::{CompositorConfig, Compositor, CompositorCapabilities, CompositorSurfaceTransform};
pub use crate::composite::{NativeSurfaceId, NativeTileId, NativeSurfaceHandle, NativeSurfaceInfo, BufferDamageCompositor};
pub use crate::composite::{MappableCompositor, MappedTileInfo, SWGLCompositeSurfaceInfo, SwapchainMode, WindowVisibility, WindowProperties};
pub use crate::device::{UploadMethod, VertexUsageHint, get_unoptimized_shader_source};
pub use crate::device::{ProgramBinary, ProgramCache, ProgramCacheObserver, ShaderError};
pub use crate::device::{Device, DeviceOptions, GlBackendConfig, GpuBackendConfig, GraphicsApi, GraphicsApiInfo};
pub use crate::profiler::{ProfilerHooks, ProfileCounterValue, set_profiler_hooks};
pub use crate::renderer::{
    CpuProfile, DebugFlags, GpuProfile,
    PendingShadersToPrecache, PipelineInfo, Renderer, RendererError, RenderResults,
    RendererStats, Shaders, SharedShaders, ShaderPrecacheFlags,
    MAX_VERTEX_TEXTURE_WIDTH,
};
pub use crate::renderer::init::{WebRenderOptions, create_webrender_instance, AsyncPropertySampler, SceneBuilderHooks, RenderBackendHooks, ONE_TIME_USAGE_HINT};
pub use crate::hit_test::SharedHitTester;
pub use crate::internal_types::FastHashMap;
pub use crate::screen_capture::{AsyncScreenshotHandle, RecordedFrameHandle};
pub use crate::texture_cache::TextureCacheConfig;
pub use crate::frame_builder::FrameBuilderConfig;
pub use crate::composite::CompositorKind;
pub use api as webrender_api;
pub use webrender_build::shader::{ProgramSourceDigest, ShaderKind};
pub use crate::tile_cache::TileOffset;
pub use crate::intern::ItemUid;
pub use crate::render_api::*;
pub use crate::tile_cache::{PictureCacheDebugInfo, DirtyTileDebugInfo, TileDebugInfo, SliceDebugInfo, CompositorClipDebugInfo};
pub use glyph_rasterizer;
pub use bump_allocator::ChunkPool;

#[cfg(feature = "sw_compositor")]
pub use crate::compositor::sw_compositor;

#[cfg(feature = "debugger")]
mod debugger;
