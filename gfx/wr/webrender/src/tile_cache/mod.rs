/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Tile cache types and descriptors
//!
//! This module contains the core tile caching infrastructure including:
//! - Tile identification and coordinate types
//! - Tile descriptors that track primitive dependencies
//! - Comparison results for invalidation tracking

// Existing tile cache slice builder (was previously tile_cache.rs)
pub mod slice_builder;

use api::{AlphaType, BorderRadius, ClipMode, ColorF, ColorU, ColorDepth, DebugFlags, ImageKey, ImageRendering};
use api::{PropertyBinding, PropertyBindingId, PrimitiveFlags, YuvFormat, YuvRangedColorSpace};
use api::units::*;
use crate::clip::{clamped_radius, ClipNodeId, ClipItemKind, ClipSpaceConversion, ClipChainInstance, ClipStore, intersect_rounded_rects};
use crate::composite::{CompositorKind, CompositeState, CompositorSurfaceKind, ExternalSurfaceDescriptor};
use crate::composite::{ExternalSurfaceDependency, NativeSurfaceId, NativeTileId};
use crate::composite::{CompositorClipIndex, CompositorTransformIndex};
use crate::composite::{CompositeTileDescriptor, CompositeTile};
use crate::gpu_types::ZBufferId;
use crate::internal_types::{FastHashMap, FastHashSet, FrameId, Filter};
use crate::invalidation::{InvalidationReason, DirtyRegion, PrimitiveCompareResult};
use crate::invalidation::cached_surface::{CachedSurface, TileUpdateDirtyContext, TileUpdateDirtyState, PrimitiveDependencyInfo};
use crate::invalidation::vert_buffer::{CornersCache, VertRange};
use crate::invalidation::compare::{PrimitiveDependency, ImageDependency};
use crate::invalidation::compare::PrimitiveComparisonKey;
use crate::invalidation::compare::{OpacityBindingInfo, ColorBindingInfo};
use crate::picture::{SurfaceTextureDescriptor, clamp};
use crate::picture_composite_mode::PictureCompositeMode;
use crate::picture::{get_relative_scale_offset, PictureInstance};
use crate::picture::MAX_COMPOSITOR_SURFACES_SIZE;
use crate::prim_store::{ClipSnap, PrimitiveInstance, PrimitiveKind, PrimitiveScratchBuffer, PictureIndex};
use crate::print_tree::{PrintTreePrinter, PrintTree};
use crate::{profiler, render_backend::{DataStores, DlStores}};
use crate::profiler::TransactionProfile;
use crate::renderer::GpuBufferBuilderF;
use crate::resource_cache::{ResourceCache, ImageRequest};
use crate::scene_building::SliceFlags;
use crate::space::{SpaceMapper, SpaceSnapper};
use crate::render_task_graph::RenderTaskId;
use crate::spatial_tree::{SpatialNodeIndex, SpatialTree};
use crate::surface::{SubpixelMode, SurfaceIndex, SurfaceInfo};
use crate::util::{ScaleOffset, MatrixHelpers, MaxRect};
use crate::visibility::{BackdropCaptureIndex, BackdropCaptures, FrameVisibilityContext, FrameVisibilityState, DrawState, PrimitiveDrawIndex, PrimitiveVisibilityFlags};
use euclid::approxeq::ApproxEq;
use euclid::Box2D;
use peek_poke::{PeekPoke, ensure_red_zone};
use std::fmt::{Display, Error, Formatter};
use std::{marker, mem};
use std::sync::atomic::{AtomicUsize, Ordering};

pub use self::slice_builder::{
    TileCacheBuilder, TileCacheConfig,
    PictureCacheDebugInfo, SliceDebugInfo, DirtyTileDebugInfo, TileDebugInfo,
    CompositorClipDebugInfo,
};

pub use api::units::TileOffset;
pub use api::units::TileRange as TileRect;

/// The maximum number of compositor surfaces that are allowed per picture cache. This
/// is an arbitrary number that should be enough for common cases, but low enough to
/// prevent performance and memory usage drastically degrading in pathological cases.
pub const MAX_COMPOSITOR_SURFACES: usize = 4;

/// The maximum number of compositor underlay surfaces that are allowed per picture cache.
/// This is an arbitrary number that should be enough for most cases.
pub const MAX_COMPOSITOR_UNDERLAY_SURFACES: usize = 5;

/// Number of consecutive frames that a YUV primitive must remain at the same
/// picture-space coverage rect before it may be promoted to an OS compositor
/// overlay or underlay.
const YUV_SURFACE_STABLE_FRAMES: u32 = 15;

/// The size in device pixels of a normal cached tile.
pub const TILE_SIZE_DEFAULT: DeviceIntSize = DeviceIntSize {
    width: 1024,
    height: 512,
    _unit: marker::PhantomData,
};

/// The size in device pixels of a tile for horizontal scroll bars
pub const TILE_SIZE_SCROLLBAR_HORIZONTAL: DeviceIntSize = DeviceIntSize {
    width: 1024,
    height: 32,
    _unit: marker::PhantomData,
};

/// The size in device pixels of a tile for vertical scroll bars
pub const TILE_SIZE_SCROLLBAR_VERTICAL: DeviceIntSize = DeviceIntSize {
    width: 32,
    height: 1024,
    _unit: marker::PhantomData,
};

/// The smallest value `max_surface_size_for_screen` will return, in DevicePixel
/// coordinates. Render tasks larger than the limit are scaled down to fit,
/// which may cause some blurriness.
pub const MAX_SURFACE_SIZE: usize = 4096;

/// The maximum size per axis of a surface, in DevicePixel coordinates, for a
/// screen of the given device pixel size.
///
/// The limit tracks the screen because a surface that covers the screen is the
/// largest one that screen-space content can legitimately need, and scaling it
/// down to a fixed limit blurs the whole surface. Note that on a HiDPI screen
/// the device pixel size already includes the backing scale, which is where
/// the fixed limit used to be exceeded most often.
///
/// The value is rounded up to a power of two so that resizing a window does not
/// change the limit - and so reallocate every surface sitting at it - on each
/// pixel of a drag.
///
/// It is never below `MAX_SURFACE_SIZE`, since a surface can legitimately be
/// larger than the screen when raster scale is above one, and lowering the
/// limit on a small screen would newly scale down surfaces that render at full
/// resolution today. It is never above what the device can allocate.
pub fn max_surface_size_for_screen(
    screen_size: DeviceIntSize,
    max_target_size: i32,
) -> usize {
    let longest_edge = screen_size.width.max(screen_size.height).max(0) as u32;

    let rounded = longest_edge
        .checked_next_power_of_two()
        .map_or(usize::MAX, |size| size as usize);

    rounded
        .max(MAX_SURFACE_SIZE)
        .min(max_target_size.max(0) as usize)
}

/// Used to get unique tile IDs, even when the tile cache is
/// destroyed between display lists / scenes.
static NEXT_TILE_ID: AtomicUsize = AtomicUsize::new(0);

/// A unique identifier for a tile. These are stable across display lists and
/// scenes.
#[derive(Debug, Copy, Clone, PartialEq, PartialOrd, Ord, Eq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct TileId(pub usize);

impl TileId {
    pub fn new() -> TileId {
        TileId(NEXT_TILE_ID.fetch_add(1, Ordering::Relaxed))
    }
}

// Internal function used by picture.rs for creating TileIds
#[doc(hidden)]
pub fn next_tile_id() -> usize {
    NEXT_TILE_ID.fetch_add(1, Ordering::Relaxed)
}

/// Uniquely identifies a tile within a picture cache slice
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, Copy, Clone, PartialEq, Hash, Eq)]
pub struct TileKey {
    // Tile index (x,y)
    pub tile_offset: TileOffset,
    // Sub-slice (z)
    pub sub_slice_index: SubSliceIndex,
}

/// Defines which sub-slice (effectively a z-index) a primitive exists on within
/// a picture cache instance.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, Copy, Clone, PartialEq, Eq, Hash, PeekPoke)]
pub struct SubSliceIndex(pub u8);

impl SubSliceIndex {
    pub const DEFAULT: SubSliceIndex = SubSliceIndex(0);

    pub fn new(index: usize) -> Self {
        SubSliceIndex(index as u8)
    }

    /// Return true if this sub-slice is the primary sub-slice (for now, we assume
    /// that only the primary sub-slice may be opaque and support subpixel AA, for example).
    pub fn is_primary(&self) -> bool {
        self.0 == 0
    }

    /// Get an array index for this sub-slice
    pub fn as_usize(&self) -> usize {
        self.0 as usize
    }
}

/// The key that identifies a tile cache instance. For now, it's simple the index of
/// the slice as it was created during scene building.
#[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct SliceId(usize);

impl SliceId {
    pub fn new(index: usize) -> Self {
        SliceId(index)
    }
}

/// Information that is required to reuse or create a new tile cache. Created
/// during scene building and passed to the render backend / frame builder.
pub struct TileCacheParams {
    // The current debug flags for the system.
    pub debug_flags: DebugFlags,
    // Index of the slice (also effectively the key of the tile cache, though we use SliceId where that matters)
    pub slice: usize,
    // Flags describing content of this cache (e.g. scrollbars)
    pub slice_flags: SliceFlags,
    // The anchoring spatial node / scroll root
    pub spatial_node_index: SpatialNodeIndex,
    // Optional background color of this tilecache. If present, can be used as an optimization
    // to enable opaque blending and/or subpixel AA in more places.
    pub background_color: Option<ColorF>,
    // Node in the clip-tree that defines where we exclude clips from child prims
    pub shared_clip_node_id: ClipNodeId,
    // Clip leaf that is used to build the clip-chain for this tile cache.
    pub tile_clip_node_id: Option<ClipNodeId>,
    // The number of Image surfaces that are being requested for this tile cache.
    // This is only a suggestion - the tile cache will clamp this as a reasonable number
    // and only promote a limited number of surfaces.
    pub image_surface_count: usize,
    // The number of YuvImage surfaces that are being requested for this tile cache.
    // This is only a suggestion - the tile cache will clamp this as a reasonable number
    // and only promote a limited number of surfaces.
    pub yuv_image_surface_count: usize,
}

/// The backing surface for this tile.
#[derive(Debug)]
pub enum TileSurface {
    Texture {
        /// Descriptor for the surface that this tile draws into.
        descriptor: SurfaceTextureDescriptor,
    },
    Color {
        color: ColorF,
    },
}

impl TileSurface {
    pub fn kind(&self) -> &'static str {
        match *self {
            TileSurface::Color { .. } => "Color",
            TileSurface::Texture { .. } => "Texture",
        }
    }
}

/// Why a tile the visibility pass found visible isn't drawn when its slice is
/// prepared.
#[derive(Debug, Copy, Clone, PartialEq)]
enum TileCull {
    /// Off-screen, or empty in device space.
    Culled,
    /// Hidden by opaque tiles in front of it.
    Occluded,
}

/// Information about a cached tile.
pub struct Tile {
    /// The grid position of this tile within the picture cache
    pub tile_offset: TileOffset,
    /// The current device space rect of this tile.
    pub device_tile_rect: DeviceRect,
    /// The device space dirty rect for this tile.
    /// TODO(gw): We have multiple dirty rects available due to the quadtree above. In future,
    ///           expose these as multiple dirty rects, which will help in some cases.
    pub device_dirty_rect: DeviceRect,
    /// Device space rect that contains valid pixels region of this tile.
    pub device_valid_rect: DeviceRect,
    /// Handle to the backing surface for this tile.
    pub surface: Option<TileSurface>,
    /// If true, this tile intersects with the currently visible screen
    /// rect, and will be drawn.
    pub is_visible: bool,
    /// If true, this tile is hidden by opaque tiles in front of it this frame.
    pub is_occluded: bool,
    /// The tile id is stable between display lists and / or frames,
    /// if the tile is retained. Useful for debugging tile evictions.
    pub id: TileId,
    /// If true, the tile was determined to be opaque, which means blending
    /// can be disabled when drawing it.
    pub is_opaque: bool,
    /// z-buffer id for this tile
    pub z_id: ZBufferId,
    /// Cached surface state (content tracking, invalidation, dependencies)
    pub cached_surface: CachedSurface,
    /// Raster-space rect for this tile, cached to avoid recomputing per primitive.
    pub local_raster_rect: RasterRect,
}

impl Tile {
    /// Construct a new, invalid tile.
    fn new(tile_offset: TileOffset) -> Self {
        let id = TileId(crate::tile_cache::next_tile_id());

        Tile {
            tile_offset,
            device_tile_rect: DeviceRect::zero(),
            device_valid_rect: DeviceRect::zero(),
            device_dirty_rect: DeviceRect::zero(),
            surface: None,
            is_visible: false,
            is_occluded: false,
            id,
            is_opaque: false,
            z_id: ZBufferId::invalid(),
            cached_surface: CachedSurface::new(),
            local_raster_rect: RasterRect::zero(),
        }
    }

    /// Whether this tile, if visible, is culled when its slice is prepared.
    /// `device_pic_rect` is the slice's clip rect in device space, and
    /// `check_occlusion` whether the slice's tiles can be occluded.
    fn cull(
        &self,
        device_pic_rect: &DeviceRect,
        check_occlusion: bool,
        transform_index: CompositorTransformIndex,
        composite_state: &mut CompositeState,
    ) -> Option<TileCull> {
        // If the rect the tile occupies on screen is occluded by some set of
        // tiles in front of it, it isn't drawn.
        match device_pic_rect.intersection(&self.device_valid_rect) {
            Some(device_draw_rect) => {
                if check_occlusion && composite_state.occluders.is_tile_occluded(self.z_id, device_draw_rect) {
                    return Some(TileCull::Occluded);
                }
            }
            None => return Some(TileCull::Culled),
        }

        // In extreme zoom/offset cases, we may end up with a local scissor/valid rect
        // that becomes empty after transformation to device space (e.g. if the local
        // rect height is 0.00001 and the compositor transform has large scale + offset).
        // DirectComposition panics if we try to BeginDraw with an empty rect, so catch
        // that here and mark the tile non-visible. This is a bit of a hack - we should
        // ideally handle these in a more accurate way so we don't end up with an empty
        // rect here.
        let local_valid_rect = &self.cached_surface.current_descriptor.local_valid_rect;
        let local_dirty_rect = match self.cached_surface.local_dirty_rect.intersection(local_valid_rect) {
            Some(rect) if !self.cached_surface.is_valid => rect,
            _ => return None,
        };
        let valid_rect = composite_state.get_surface_rect(
            local_valid_rect,
            &self.cached_surface.local_rect,
            transform_index,
        ).to_i32();
        let scissor_rect = composite_state.get_surface_rect(
            &local_dirty_rect,
            &self.cached_surface.local_rect,
            transform_index,
        ).to_i32().intersection(&valid_rect).unwrap_or_else(|| { Box2D::zero() });
        if scissor_rect.is_empty() || valid_rect.is_empty() {
            return Some(TileCull::Culled);
        }

        None
    }

    /// Print debug information about this tile to a tree printer.
    fn print(&self, pt: &mut dyn PrintTreePrinter) {
        pt.new_level(format!("Tile {:?}", self.id));
        pt.add_item(format!("local_rect: {:?}", self.cached_surface.local_rect));
        self.cached_surface.print(pt);
        pt.end_level();
    }

    /// Invalidate a tile based on change in content. This
    /// must be called even if the tile is not currently
    /// visible on screen. We might be able to improve this
    /// later by changing how ComparableVec is used.
    fn update_content_validity(
        &mut self,
        ctx: &TileUpdateDirtyContext,
        state: &mut TileUpdateDirtyState,
        frame_context: &FrameVisibilityContext,
    ) {
        self.cached_surface.update_content_validity(
            ctx,
            state,
            frame_context,
        );
    }

    /// Invalidate this tile. If `invalidation_rect` is None, the entire
    /// tile is invalidated.
    pub fn invalidate(
        &mut self,
        invalidation_rect: Option<PictureRect>,
        reason: InvalidationReason,
    ) {
        self.cached_surface.invalidate(invalidation_rect, reason);
    }

    /// Called during pre_update of a tile cache instance. Allows the
    /// tile to setup state before primitive dependency calculations.
    fn pre_update(
        &mut self,
        ctx: &TilePreUpdateContext,
    ) {
        self.cached_surface.local_rect = PictureRect::new(
            PicturePoint::new(
                self.tile_offset.x as f32 * ctx.tile_size.width,
                self.tile_offset.y as f32 * ctx.tile_size.height,
            ),
            PicturePoint::new(
                (self.tile_offset.x + 1) as f32 * ctx.tile_size.width,
                (self.tile_offset.y + 1) as f32 * ctx.tile_size.height,
            ),
        );

        self.local_raster_rect = ctx.local_to_raster.map_rect(&self.cached_surface.local_rect);

        self.device_tile_rect = ctx.pic_to_device_mapper
            .map(&self.cached_surface.local_rect)
            .expect("bug: map local tile rect");

        // Check if this tile is currently on screen.
        self.is_visible = self.device_tile_rect.intersects(&ctx.global_screen_device_rect);

        // Delegate to CachedSurface for content tracking setup
        self.cached_surface.pre_update(
            ctx.background_color,
            self.cached_surface.local_rect,
            ctx.frame_id,
            self.is_visible,
        );
    }

    /// Add dependencies for a given primitive to this tile.
    fn add_prim_dependency(
        &mut self,
        info: &PrimitiveDependencyInfo,
        corners_cache: &CornersCache,
        prim_clamp_to_tile: bool,
    ) {
        // If this tile isn't currently visible, we don't want to update the dependencies
        // for this tile, as an optimization, since it won't be drawn anyway.
        if !self.is_visible {
            return;
        }

        let local_rect = self.cached_surface.local_rect;
        self.cached_surface.add_prim_dependency(
            info,
            corners_cache,
            prim_clamp_to_tile,
            &self.local_raster_rect,
            local_rect,
        );
    }

    /// Called during tile cache instance post_update. Allows invalidation and dirty
    /// rect calculation after primitive dependencies have been updated.
    fn update_dirty_and_valid_rects(
        &mut self,
        ctx: &TileUpdateDirtyContext,
        state: &mut TileUpdateDirtyState,
        frame_context: &FrameVisibilityContext,
    ) {
        // Ensure peek-poke constraint is met, that `dep_data` is large enough
        ensure_red_zone::<PrimitiveDependency>(&mut self.cached_surface.current_descriptor.dep_data);

        // If tile is not visible, just early out from here - we don't update dependencies
        // so don't want to invalidate, merge, split etc. The tile won't need to be drawn
        // (and thus updated / invalidated) until it is on screen again.
        if !self.is_visible {
            return;
        }

        // Calculate the overall valid rect for this tile.
        self.cached_surface.current_descriptor.local_valid_rect = self.cached_surface.local_valid_rect;

        // TODO(gw): In theory, the local tile rect should always have an
        //           intersection with the overall picture rect. In practice,
        //           due to some accuracy issues with how fract_offset (and
        //           fp accuracy) are used in the calling method, this isn't
        //           always true. In this case, it's safe to set the local
        //           valid rect to zero, which means it will be clipped out
        //           and not affect the scene. In future, we should fix the
        //           accuracy issue above, so that this assumption holds, but
        //           it shouldn't have any noticeable effect on performance
        //           or memory usage (textures should never get allocated).
        self.cached_surface.current_descriptor.local_valid_rect = self.cached_surface.local_rect
            .intersection(&ctx.local_rect)
            .and_then(|r| r.intersection(&self.cached_surface.current_descriptor.local_valid_rect))
            .unwrap_or_else(PictureRect::zero);


        // The device rect is guaranteed to be aligned on a device pixel - the round
        // is just to deal with float accuracy. However, the valid rect is not
        // always aligned to a device pixel. To handle this, round out to get all
        // required pixels, and intersect with the tile device rect.
        let device_rect = self.device_tile_rect.round();

        // The device_valid_rect is referenced during `update_content_validity` so it
        // must be updated here first.
        self.device_valid_rect = ctx.pic_to_device_mapper
            .map(&self.cached_surface.current_descriptor.local_valid_rect)
            .expect("bug: map local valid rect")
            .round_out()
            .intersection(&device_rect)
            .unwrap_or_else(DeviceRect::zero);

        // Invalidate the tile based on the content changing.
        self.update_content_validity(ctx, state, frame_context);
    }

    /// Called during tile cache instance post_update. Allows invalidation and dirty
    /// rect calculation after primitive dependencies have been updated.
    fn post_update(
        &mut self,
        ctx: &TilePostUpdateContext,
        state: &mut TilePostUpdateState,
        frame_context: &FrameVisibilityContext,
    ) {
        // If tile is not visible, just early out from here - we don't update dependencies
        // so don't want to invalidate, merge, split etc. The tile won't need to be drawn
        // (and thus updated / invalidated) until it is on screen again.
        if !self.is_visible {
            return;
        }

        // If there are no primitives there is no need to draw or cache it.
        // Bug 1719232 - The final device valid rect does not always describe a non-empty
        // region. Cull the tile as a workaround.
        if self.cached_surface.current_descriptor.prims.is_empty() || self.device_valid_rect.is_empty() {
            // If there is a native compositor surface allocated for this (now empty) tile
            // it must be freed here, otherwise the stale tile with previous contents will
            // be composited. If the tile subsequently gets new primitives added to it, the
            // surface will be re-allocated when it's added to the composite draw list.
            if let Some(TileSurface::Texture { descriptor: SurfaceTextureDescriptor::Native { mut id, .. }, .. }) = self.surface.take() {
                if let Some(id) = id.take() {
                    state.resource_cache.destroy_compositor_tile(id);
                }
            }

            self.is_visible = false;
            return;
        }

        // Check if this tile can be considered opaque. Opacity state must be updated only
        // after all early out checks have been performed. Otherwise, we might miss updating
        // the native surface next time this tile becomes visible.
        let clipped_rect = self.cached_surface.current_descriptor.local_valid_rect
            .intersection(&ctx.local_clip_rect)
            .unwrap_or_else(PictureRect::zero);

        let has_opaque_bg_color = self.cached_surface.background_color.map_or(false, |c| c.a >= 1.0);
        let has_opaque_backdrop = ctx.backdrop.map_or(false, |b| b.opaque_rect.contains_box(&clipped_rect));
        let mut is_opaque = has_opaque_bg_color || has_opaque_backdrop;

        // If this tile intersects with any underlay surfaces, we need to consider it
        // translucent, since it will contain an alpha cutout
        for underlay in ctx.underlays {
            if clipped_rect.intersects(&underlay.local_rect) {
                is_opaque = false;
                break;
            }
        }

        // Set the correct z_id for this tile
        self.z_id = ctx.z_id;

        if is_opaque != self.is_opaque {
            // If opacity changed, the native compositor surface and all tiles get invalidated.
            // (this does nothing if not using native compositor mode).
            // TODO(gw): This property probably changes very rarely, so it is OK to invalidate
            //           everything in this case. If it turns out that this isn't true, we could
            //           consider other options, such as per-tile opacity (natively supported
            //           on CoreAnimation, and supported if backed by non-virtual surfaces in
            //           DirectComposition).
            if let Some(TileSurface::Texture { descriptor: SurfaceTextureDescriptor::Native { ref mut id, .. }, .. }) = self.surface {
                if let Some(id) = id.take() {
                    state.resource_cache.destroy_compositor_tile(id);
                }
            }

            // Invalidate the entire tile to force a redraw.
            self.invalidate(None, InvalidationReason::SurfaceOpacityChanged);
            self.is_opaque = is_opaque;
        }

        // Check if the selected composite mode supports dirty rect updates. For Draw composite
        // mode, we can always update the content with smaller dirty rects, unless there is a
        // driver bug to workaround. For native composite mode, we can only use dirty rects if
        // the compositor supports partial surface updates.
        let supports_dirty_rects = supports_dirty_rects(
            &state.composite_state.compositor_kind,
            frame_context.config.gpu_supports_render_target_partial_update,
        );
        let supports_simple_prims = !state.composite_state.compositor_kind.uses_native_tiles();

        // TODO(gw): Consider using smaller tiles and/or tile splits for
        //           native compositors that don't support dirty rects.
        if supports_dirty_rects {
            // Only allow splitting for normal content sized tiles
            if ctx.current_tile_size == state.resource_cache.picture_textures.default_tile_size() {
                let max_split_level = 3;

                // Consider splitting / merging dirty regions
                self.cached_surface.root.maybe_merge_or_split(
                    0,
                    &self.cached_surface.current_descriptor.prims,
                    max_split_level,
                );
            }
        }

        // The dirty rect will be set correctly by now. If the underlying platform
        // doesn't support partial updates, and this tile isn't valid, force the dirty
        // rect to be the size of the entire tile.
        if !self.cached_surface.is_valid && !supports_dirty_rects {
            self.cached_surface.local_dirty_rect = self.cached_surface.local_rect;
        }

        // See if this tile is a simple color, in which case we can just draw
        // it as a rect, and avoid allocating a texture surface and drawing it.
        // TODO(gw): Initial native compositor interface doesn't support simple
        //           color tiles. We can definitely support this in DC, so this
        //           should be added as a follow up.
        let is_simple_prim =
            ctx.backdrop.map_or(false, |b| b.kind.is_some()) &&
            self.cached_surface.current_descriptor.prims.len() == 1 &&
            self.is_opaque &&
            supports_simple_prims;

        // Set up the backing surface for this tile.
        let surface = if is_simple_prim {
            // If we determine the tile can be represented by a color, set the
            // surface unconditionally (this will drop any previously used
            // texture cache backing surface).
            match ctx.backdrop.unwrap().kind {
                Some(BackdropKind::Color { color }) => {
                    TileSurface::Color {
                        color,
                    }
                }
                None => {
                    // This should be prevented by the is_simple_prim check above.
                    unreachable!();
                }
            }
        } else {
            // If this tile will be backed by a surface, we want to retain
            // the texture handle from the previous frame, if possible. If
            // the tile was previously a color, or not set, then just set
            // up a new texture cache handle.
            match self.surface.take() {
                Some(TileSurface::Texture { descriptor }) => {
                    // Reuse the existing descriptor and vis mask
                    TileSurface::Texture {
                        descriptor,
                    }
                }
                Some(TileSurface::Color { .. }) | None => {
                    // This is the case where we are constructing a tile surface that
                    // involves drawing to a texture. Create the correct surface
                    // descriptor depending on the compositing mode that will read
                    // the output.
                    let descriptor = if state.composite_state.compositor_kind.uses_native_tiles() {
                        // Create a native surface surface descriptor, but don't allocate
                        // a surface yet. The surface is allocated *after* occlusion
                        // culling occurs, so that only visible tiles allocate GPU memory.
                        SurfaceTextureDescriptor::Native {
                            id: None,
                        }
                    } else {
                        // For a texture cache entry, create an invalid handle that
                        // will be allocated when update_picture_cache is called.
                        SurfaceTextureDescriptor::TextureCache {
                            handle: None,
                        }
                    };

                    TileSurface::Texture {
                        descriptor,
                    }
                }
            }
        };

        // Store the current surface backing info for use during batching.
        self.surface = Some(surface);
    }
}

// TODO(gw): Tidy this up by:
//      - Add an Other variant for things like opaque gradient backdrops
#[derive(Debug, Copy, Clone)]
pub enum BackdropKind {
    Color {
        color: ColorF,
    },
}

/// Stores information about the calculated opaque backdrop of this slice.
#[derive(Debug, Copy, Clone)]
pub struct BackdropInfo {
    /// The picture space rectangle that is known to be opaque. This is used
    /// to determine where subpixel AA can be used, and where alpha blending
    /// can be disabled.
    pub opaque_rect: PictureRect,
    /// If the backdrop covers the entire slice with an opaque color, this
    /// will be set and can be used as a clear color for the slice's tiles.
    pub spanning_opaque_color: Option<ColorF>,
    /// Kind of the backdrop
    pub kind: Option<BackdropKind>,
    /// The picture space rectangle of the backdrop, if kind is set.
    pub backdrop_rect: PictureRect,
}

impl BackdropInfo {
    fn empty() -> Self {
        BackdropInfo {
            opaque_rect: PictureRect::zero(),
            spanning_opaque_color: None,
            kind: None,
            backdrop_rect: PictureRect::zero(),
        }
    }
}

/// Represents the native surfaces created for a picture cache, if using
/// a native compositor. An opaque and alpha surface is always created,
/// but tiles are added to a surface based on current opacity. If the
/// calculated opacity of a tile changes, the tile is invalidated and
/// attached to a different native surface. This means that we don't
/// need to invalidate the entire surface if only some tiles are changing
/// opacity. It also means we can take advantage of opaque tiles on cache
/// slices where only some of the tiles are opaque. There is an assumption
/// that creating a native surface is cheap, and only when a tile is added
/// to a surface is there a significant cost. This assumption holds true
/// for the current native compositor implementations on Windows and Mac.
pub struct NativeSurface {
    /// Native surface for opaque tiles
    pub opaque: NativeSurfaceId,
    /// Native surface for alpha tiles
    pub alpha: NativeSurfaceId,
}

/// Hash key for an external native compositor surface
#[derive(PartialEq, Eq, Hash)]
pub struct ExternalNativeSurfaceKey {
    /// The YUV/RGB image keys that are used to draw this surface.
    pub image_keys: [ImageKey; 3],
    /// If this is not an 'external' compositor surface created via
    /// Compositor::create_external_surface, this is set to the
    /// current device size of the surface.
    pub size: Option<DeviceIntSize>,
}

/// Information about a native compositor surface cached between frames.
pub struct ExternalNativeSurface {
    /// If true, the surface was used this frame. Used for a simple form
    /// of GC to remove old surfaces.
    pub used_this_frame: bool,
    /// The native compositor surface handle
    pub native_surface_id: NativeSurfaceId,
    /// List of image keys, and current image generations, that are drawn in this surface.
    /// The image generations are used to check if the compositor surface is dirty and
    /// needs to be updated.
    pub image_dependencies: [ImageDependency; 3],
}

/// Wrapper struct around an external surface descriptor with a little more information
/// that the picture caching code needs.
pub struct CompositorSurface {
    // External surface descriptor used by compositing logic
    pub descriptor: ExternalSurfaceDescriptor,
    // The compositor surface rect + any intersecting prims. Later prims that intersect
    // with this must be added to the next sub-slice.
    prohibited_rect: PictureRect,
    // If the compositor surface content is opaque.
    pub is_opaque: bool,
}

pub struct BackdropSurface {
    pub id: NativeSurfaceId,
    pub color: ColorF,
    pub device_rect: DeviceRect,
}

/// In some cases, we need to know the dirty rect of all tiles in order
/// to correctly invalidate a primitive.
#[derive(Debug)]
pub struct DeferredDirtyTest {
    /// The tile rect that the primitive being checked affects
    pub tile_rect: TileRect,
    /// The picture-cache local rect of the primitive being checked
    pub prim_rect: PictureRect,
}

struct YuvSurfaceStability {
    pic_coverage_rect: PictureRect,
    stable_frames: u32,
    seen_this_frame: bool,
}

/// Represents a cache of tiles that make up a picture primitives.
pub struct TileCacheInstance {
    // The current debug flags for the system.
    pub debug_flags: DebugFlags,
    /// Index of the tile cache / slice for this frame builder. It's determined
    /// by the setup_picture_caching method during flattening, which splits the
    /// picture tree into multiple slices. It's used as a simple input to the tile
    /// keys. It does mean we invalidate tiles if a new layer gets inserted / removed
    /// between display lists - this seems very unlikely to occur on most pages, but
    /// can be revisited if we ever notice that.
    pub slice: usize,
    /// Propagated information about the slice
    pub slice_flags: SliceFlags,
    /// The currently selected tile size to use for this cache
    pub current_tile_size: DeviceIntSize,
    /// The list of sub-slices in this tile cache
    pub sub_slices: Vec<SubSlice>,
    /// The positioning node for this tile cache.
    pub spatial_node_index: SpatialNodeIndex,
    /// The coordinate space to do visibility/clipping/invalidation in, resolved
    /// each frame in `pre_update` from the tile cache's surface. `INVALID`
    /// before the first `pre_update` of a frame.
    pub visibility_node_index: SpatialNodeIndex,
    /// List of opacity bindings, with some extra information
    /// about whether they changed since last frame.
    opacity_bindings: FastHashMap<PropertyBindingId, OpacityBindingInfo>,
    /// Switch back and forth between old and new bindings hashmaps to avoid re-allocating.
    old_opacity_bindings: FastHashMap<PropertyBindingId, OpacityBindingInfo>,
    /// List of color bindings, with some extra information
    /// about whether they changed since last frame.
    color_bindings: FastHashMap<PropertyBindingId, ColorBindingInfo>,
    /// Switch back and forth between old and new bindings hashmaps to avoid re-allocating.
    old_color_bindings: FastHashMap<PropertyBindingId, ColorBindingInfo>,
    /// The current dirty region tracker for this picture.
    pub dirty_region: DirtyRegion,
    /// Current size of tiles in picture units.
    tile_size: PictureSize,
    /// Tile coords of the currently allocated grid.
    tile_rect: TileRect,
    /// Pre-calculated versions of the tile_rect above, used to speed up the
    /// calculations in get_tile_coords_for_rect.
    tile_bounds_p0: TileOffset,
    tile_bounds_p1: TileOffset,
    /// Local rect (unclipped) of the picture this cache covers.
    pub local_rect: PictureRect,
    /// The local clip rect, from the shared clips of this picture.
    pub local_clip_rect: PictureRect,
    /// Registered clip in CompositeState for this picture cache
    pub compositor_clip: Option<CompositorClipIndex>,
    /// The rounded-rect clips `compositor_clip` was built from, in their own
    /// spatial node's space, for drawing this slice's content elsewhere with
    /// the clip it is composited with.
    pub compositor_clip_shapes: Vec<CompositorClipShape>,
    /// The screen rect, transformed to local picture space.
    pub screen_rect_in_pic_space: PictureRect,
    /// The surface index that this tile cache will be drawn into.
    surface_index: SurfaceIndex,
    /// The background color from the renderer. If this is set opaque, we know it's
    /// fine to clear the tiles to this and allow subpixel text on the first slice.
    pub background_color: Option<ColorF>,
    /// Information about the calculated backdrop content of this cache.
    pub backdrop: BackdropInfo,
    /// The allowed subpixel mode for this surface, which depends on the detected
    /// opacity of the background.
    pub subpixel_mode: SubpixelMode,
    // Node in the clip-tree that defines where we exclude clips from child prims
    pub shared_clip_node_id: ClipNodeId,
    // Clip leaf that is used to build the clip-chain for this tile cache.
    pub tile_clip_node_id: Option<ClipNodeId>,
    /// The number of frames until this cache next evaluates what tile size to use.
    /// If a picture rect size is regularly changing just around a size threshold,
    /// we don't want to constantly invalidate and reallocate different tile size
    /// configuration each frame.
    frames_until_size_eval: usize,
    /// keep around the hash map used as compare_cache to avoid reallocating it each
    /// frame.
    compare_cache: FastHashMap<PrimitiveComparisonKey, PrimitiveCompareResult>,
    /// The currently considered tile size override. Used to check if we should
    /// re-evaluate tile size, even if the frame timer hasn't expired.
    tile_size_override: Option<DeviceIntSize>,
    /// A cache of compositor surfaces that are retained between frames
    pub external_native_surface_cache: FastHashMap<ExternalNativeSurfaceKey, ExternalNativeSurface>,
    /// Current frame ID of this tile cache instance. Used for book-keeping / garbage collecting
    frame_id: FrameId,
    /// Registered transform in CompositeState for this picture cache
    pub transform_index: CompositorTransformIndex,
    /// Current transform mapping local picture space to compositor surface raster space
    local_to_raster: ScaleOffset,
    /// Current transform mapping compositor surface raster space to final device space
    raster_to_device: ScaleOffset,
    /// If true, we need to invalidate all tiles during `post_update`
    invalidate_all_tiles: bool,
    /// The current raster scale for tiles in this cache
    pub current_raster_scale: f32,
    /// Depth of off-screen surfaces that are currently pushed during dependency updates
    current_surface_traversal_depth: usize,
    /// A list of extra dirty invalidation tests that can only be checked once we
    /// know the dirty rect of all tiles
    deferred_dirty_tests: Vec<DeferredDirtyTest>,
    /// A list of pic_coverage_rect of Picture with mix blend that could affect to underlays.
    pub mix_blend_pic_rects: Vec<PictureRect>,
    /// Is there a backdrop associated with this cache
    pub found_prims_after_backdrop: bool,
    pub backdrop_surface: Option<BackdropSurface>,
    /// List of underlay compositor surfaces that exist in this picture cache
    pub underlays: Vec<ExternalSurfaceDescriptor>,
    /// "Region" (actually a spanning rect) containing all overlay promoted surfaces
    pub overlay_region: PictureRect,
    /// The number YuvImage prims in this cache, provided in our TileCacheParams.
    pub yuv_images_count: usize,
    /// The remaining number of YuvImage prims we will see this frame. We prioritize
    /// promoting these before promoting any Image prims.
    pub yuv_images_remaining: usize,
    /// Per-YUV-primitive stability state retained across frames.
    yuv_surface_stability: FastHashMap<crate::intern::ItemUid, YuvSurfaceStability>,
    /// Persistent cache for computing and storing raster-space primitive corners.
    corners_cache: CornersCache,
    /// What each backdrop-filter capture this slice is the backdrop root of
    /// read from the slices below it last frame, by its chain.
    backdrop_capture_keys: FastHashMap<PictureIndex, BackdropCaptureKey>,
    /// The areas of this slice that backdrop-filters in it, and drawn ones in
    /// slices above it, sample this frame, in increasing capture order, known
    /// before visibility. Compositor surfaces aren't drawn into a backdrop, so
    /// nothing is promoted to one in these areas.
    pub backdrop_samples: Vec<BackdropSample>,
}

/// A rounded-rect clip a slice is composited with.
#[derive(Debug, Clone, PartialEq)]
pub struct CompositorClipShape {
    pub rect: LayoutRect,
    pub radius: BorderRadius,
    pub spatial_node_index: SpatialNodeIndex,
    pub uid: u64,
}

/// What a backdrop-filter capture reads from the slices below its own. When it
/// changes, so does the backdrop, even if none of those slices' tiles do.
#[derive(Debug, Default, PartialEq)]
pub struct BackdropCaptureKey {
    region: PictureRect,
    sources: Vec<BackdropSourceKey>,
}

/// What a backdrop-filter capture reads from one slice below its own.
#[derive(Debug, PartialEq)]
struct BackdropSourceKey {
    slice_id: SliceId,
    /// Maps the slice's picture space to the capture's.
    slice_to_capture: ScaleOffset,
    /// The sampled part of the slice, in its picture space.
    rect: PictureRect,
    compositor_clip_shapes: Vec<CompositorClipShape>,
    background_color: Option<ColorF>,
}

/// An area of a slice that a backdrop-filter in it or in a slice above samples.
pub struct BackdropSample {
    /// The capture that samples it.
    pub capture_index: BackdropCaptureIndex,
    /// The sampled area, in this slice's picture space.
    pub rect: PictureRect,
    /// What this slice draws for the capture this frame, set once visibility
    /// is done if the capture is in a slice above and its backdrop is redrawn.
    pub source: Option<BackdropSource>,
}

/// The part of a slice drawn for a backdrop-filter in a slice above it.
pub struct BackdropSource {
    /// The sampled area clipped to the slice, in its picture space.
    pub rect: PictureRect,
    /// The task this slice's primitives in `rect` are drawn into, and the
    /// task's rect in this slice's picture space, once this slice is prepared.
    pub task: Option<(RenderTaskId, PictureRect)>,
}

impl TileCacheInstance {
    pub fn new(params: TileCacheParams) -> Self {
        // Determine how many sub-slices we need. Clamp to an arbitrary limit to ensure
        // we don't create a huge number of OS compositor tiles and sub-slices.
        let sub_slice_count = (params.image_surface_count + params.yuv_image_surface_count).min(MAX_COMPOSITOR_SURFACES) + 1;

        let mut sub_slices = Vec::with_capacity(sub_slice_count);
        for _ in 0 .. sub_slice_count {
            sub_slices.push(SubSlice::new());
        }

        TileCacheInstance {
            debug_flags: params.debug_flags,
            slice: params.slice,
            slice_flags: params.slice_flags,
            spatial_node_index: params.spatial_node_index,
            visibility_node_index: SpatialNodeIndex::INVALID,
            sub_slices,
            opacity_bindings: FastHashMap::default(),
            old_opacity_bindings: FastHashMap::default(),
            color_bindings: FastHashMap::default(),
            old_color_bindings: FastHashMap::default(),
            // Re-targeted every frame by `post_update` before anything reads it.
            dirty_region: DirtyRegion::new(SpatialNodeIndex::INVALID, params.spatial_node_index),
            tile_size: PictureSize::zero(),
            tile_rect: TileRect::zero(),
            tile_bounds_p0: TileOffset::zero(),
            tile_bounds_p1: TileOffset::zero(),
            local_rect: PictureRect::zero(),
            local_clip_rect: PictureRect::zero(),
            compositor_clip: None,
            compositor_clip_shapes: Vec::new(),
            screen_rect_in_pic_space: PictureRect::zero(),
            surface_index: SurfaceIndex(0),
            background_color: params.background_color,
            backdrop: BackdropInfo::empty(),
            subpixel_mode: SubpixelMode::Allow,
            shared_clip_node_id: params.shared_clip_node_id,
            tile_clip_node_id: params.tile_clip_node_id,
            current_tile_size: DeviceIntSize::zero(),
            frames_until_size_eval: 0,
            compare_cache: FastHashMap::default(),
            tile_size_override: None,
            external_native_surface_cache: FastHashMap::default(),
            frame_id: FrameId::INVALID,
            transform_index: CompositorTransformIndex::INVALID,
            raster_to_device: ScaleOffset::identity(),
            local_to_raster: ScaleOffset::identity(),
            invalidate_all_tiles: true,
            current_raster_scale: 1.0,
            current_surface_traversal_depth: 0,
            deferred_dirty_tests: Vec::new(),
            mix_blend_pic_rects: Vec::new(),
            found_prims_after_backdrop: false,
            backdrop_surface: None,
            underlays: Vec::new(),
            overlay_region: PictureRect::zero(),
            yuv_images_count: params.yuv_image_surface_count,
            yuv_images_remaining: 0,
            yuv_surface_stability: FastHashMap::default(),
            corners_cache: CornersCache::new(),
            backdrop_capture_keys: FastHashMap::default(),
            backdrop_samples: Vec::new(),
        }
    }

    /// The position in `backdrop_samples` of the area of this slice the
    /// capture at `capture_index` samples, if it is in this slice, or drawn and
    /// in a slice above.
    fn backdrop_sample_position(&self, capture_index: BackdropCaptureIndex) -> Option<usize> {
        self.backdrop_samples
            .binary_search_by_key(&capture_index, |sample| sample.capture_index)
            .ok()
    }

    /// The part of this slice drawn for the capture at `capture_index`, if any.
    pub fn backdrop_source(&self, capture_index: BackdropCaptureIndex) -> Option<&BackdropSource> {
        let position = self.backdrop_sample_position(capture_index)?;
        self.backdrop_samples[position].source.as_ref()
    }

    /// The slice's clip rect in device space, which its tiles are drawn within.
    fn device_pic_rect(
        &self,
        root_spatial_node_index: SpatialNodeIndex,
        global_screen_device_rect: DeviceRect,
        spatial_tree: &SpatialTree,
    ) -> DeviceRect {
        let map_pic_to_device: SpaceMapper<PicturePixel, DevicePixel> = SpaceMapper::new_with_target(
            root_spatial_node_index,
            self.spatial_node_index,
            global_screen_device_rect,
            spatial_tree,
        );
        map_pic_to_device
            .map(&self.local_clip_rect)
            .expect("bug: unable to map clip rect")
            .round()
    }

    /// Hide the visible tiles that are off-screen, empty in device space, or
    /// occluded, and invalidate the ones left that have lost their backing
    /// surface: a texture evicted on a previous frame, or a native tile not
    /// allocated yet or freed while occluded. Runs once every slice's tiles
    /// and occluders are known, before which backdrop-filters are drawn is
    /// decided from the tiles that are drawn and dirty.
    pub fn cull_tiles(
        &mut self,
        resource_cache: &mut ResourceCache,
        composite_state: &mut CompositeState,
        root_spatial_node_index: SpatialNodeIndex,
        global_screen_device_rect: DeviceRect,
        spatial_tree: &SpatialTree,
    ) {
        let device_pic_rect = self.device_pic_rect(
            root_spatial_node_index,
            global_screen_device_rect,
            spatial_tree,
        );
        let check_occluded_tiles = self.check_occluded_tiles(
            &composite_state.compositor_kind,
            root_spatial_node_index,
        );
        let transform_index = self.transform_index;
        for sub_slice in &mut self.sub_slices {
            for tile in sub_slice.tiles.values_mut() {
                tile.is_occluded = false;
                if !tile.is_visible {
                    continue;
                }
                match tile.cull(&device_pic_rect, check_occluded_tiles, transform_index, composite_state) {
                    Some(TileCull::Occluded) => {
                        // If this tile has an allocated native surface, free it, since it's completely
                        // occluded. We will need to re-allocate this surface if it becomes visible,
                        // but that's likely to be rare (e.g. when there is no content display list
                        // for a frame or two during a tab switch).
                        if let Some(TileSurface::Texture { descriptor: SurfaceTextureDescriptor::Native { id } }) = tile.surface.as_mut() {
                            if let Some(id) = id.take() {
                                resource_cache.destroy_compositor_tile(id);
                            }
                        }
                        tile.is_visible = false;
                        tile.is_occluded = true;
                        continue;
                    }
                    Some(TileCull::Culled) => {
                        tile.is_visible = false;
                        continue;
                    }
                    None => {}
                }
                let reason = match tile.surface {
                    Some(TileSurface::Texture { descriptor: SurfaceTextureDescriptor::TextureCache { ref handle } }) => {
                        let exists = handle.as_ref().map_or(false, |handle| {
                            resource_cache.picture_textures.entry_exists(handle)
                        });
                        (!exists).then_some(InvalidationReason::NoTexture)
                    }
                    Some(TileSurface::Texture { descriptor: SurfaceTextureDescriptor::Native { id } }) => {
                        id.is_none().then_some(InvalidationReason::NoSurface)
                    }
                    Some(TileSurface::Color { .. }) | None => None,
                };
                if let Some(reason) = reason {
                    tile.invalidate(None, reason);
                }
            }
        }
    }

    /// Whether the slice's tiles can be occluded by the slices in front of it.
    fn check_occluded_tiles(
        &self,
        compositor_kind: &CompositorKind,
        root_spatial_node_index: SpatialNodeIndex,
    ) -> bool {
        compositor_kind.culls_all_tiles() ||
            // Only check for occlusion on visible tiles that are fixed position.
            self.spatial_node_index == root_spatial_node_index
    }

    /// Whether a backdrop-filter in this slice or a slice above may sample `rect`.
    fn is_sampled_by_backdrop(&self, rect: &PictureRect) -> bool {
        self.backdrop_samples.iter().any(|sample| sample.rect.intersects(rect))
    }

    /// Record the areas of this slice, at `position` in the slice order, that
    /// the backdrop-filters in it and the drawn ones in the slices above it
    /// sample. The slices above have already found which backdrop-filter chains
    /// are drawn. This slice's own are only found as it is visited, after the
    /// primitives behind them, so those count whether drawn or not.
    pub fn update_backdrop_sample_rects(
        &mut self,
        position: usize,
        root_regions: &[Option<(usize, LayoutRect)>],
        captures: &BackdropCaptures,
        required_backdrop_chains: &FastHashSet<PictureIndex>,
        spatial_tree: &SpatialTree,
    ) {
        self.backdrop_samples.clear();
        if captures.is_empty() {
            return;
        }
        let pic_to_root = get_relative_scale_offset(
            self.spatial_node_index,
            spatial_tree.root_reference_frame_index(),
            spatial_tree,
        );
        for ((capture_index, capture), root_region) in captures.iter().zip(root_regions) {
            match root_region {
                Some((top, _)) if *top == position => {
                    self.backdrop_samples.push(BackdropSample {
                        capture_index,
                        rect: capture.region,
                        source: None,
                    });
                }
                Some((top, root_region)) if *top > position &&
                    required_backdrop_chains.contains(&capture.chain_pic_index) => {
                    self.backdrop_samples.push(BackdropSample {
                        capture_index,
                        rect: pic_to_root.unmap_rect(root_region),
                        source: None,
                    });
                }
                _ => {}
            }
        }
    }

    /// Return the total number of tiles allocated by this tile cache
    pub fn tile_count(&self) -> usize {
        self.tile_rect.area() as usize * self.sub_slices.len()
    }

    /// Trims memory held by the tile cache, such as native surfaces.
    pub fn memory_pressure(&mut self, resource_cache: &mut ResourceCache) {
        for sub_slice in &mut self.sub_slices {
            for tile in sub_slice.tiles.values_mut() {
                if let Some(TileSurface::Texture { descriptor: SurfaceTextureDescriptor::Native { ref mut id, .. }, .. }) = tile.surface {
                    // Reseting the id to None with take() ensures that a new
                    // tile will be allocated during the next frame build.
                    if let Some(id) = id.take() {
                        resource_cache.destroy_compositor_tile(id);
                    }
                }
            }
            if let Some(native_surface) = sub_slice.native_surface.take() {
                resource_cache.destroy_compositor_surface(native_surface.opaque);
                resource_cache.destroy_compositor_surface(native_surface.alpha);
            }
        }
    }

    /// Reset this tile cache with the updated parameters from a new scene
    /// that has arrived. This allows the tile cache to be retained across
    /// new scenes.
    pub fn prepare_for_new_scene(
        &mut self,
        params: TileCacheParams,
        resource_cache: &mut ResourceCache,
    ) {
        // We should only receive updated state for matching slice key
        assert_eq!(self.slice, params.slice);

        // Determine how many sub-slices we need, based on how many compositor surface prims are
        // in the supplied primitive list.
        let required_sub_slice_count = (params.image_surface_count + params.yuv_image_surface_count).min(MAX_COMPOSITOR_SURFACES) + 1;

        if self.sub_slices.len() != required_sub_slice_count {
            self.tile_rect = TileRect::zero();

            if self.sub_slices.len() > required_sub_slice_count {
                let old_sub_slices = self.sub_slices.split_off(required_sub_slice_count);

                for mut sub_slice in old_sub_slices {
                    for tile in sub_slice.tiles.values_mut() {
                        if let Some(TileSurface::Texture { descriptor: SurfaceTextureDescriptor::Native { ref mut id, .. }, .. }) = tile.surface {
                            if let Some(id) = id.take() {
                                resource_cache.destroy_compositor_tile(id);
                            }
                        }
                    }

                    if let Some(native_surface) = sub_slice.native_surface {
                        resource_cache.destroy_compositor_surface(native_surface.opaque);
                        resource_cache.destroy_compositor_surface(native_surface.alpha);
                    }
                }
            } else {
                while self.sub_slices.len() < required_sub_slice_count {
                    self.sub_slices.push(SubSlice::new());
                }
            }
        }

        // Store the parameters from the scene builder for this slice. Other
        // params in the tile cache are retained and reused, or are always
        // updated during pre/post_update.
        self.slice_flags = params.slice_flags;
        self.spatial_node_index = params.spatial_node_index;
        self.background_color = params.background_color;
        self.tile_clip_node_id = params.tile_clip_node_id;
        self.shared_clip_node_id = params.shared_clip_node_id;

        // Since the slice flags may have changed, ensure we re-evaluate the
        // appropriate tile size for this cache next update.
        self.frames_until_size_eval = 0;

        // Update the number of YuvImage prims we have in the scene.
        self.yuv_images_count = params.yuv_image_surface_count;
    }

    /// Destroy any manually managed resources before this picture cache is
    /// destroyed, such as native compositor surfaces.
    pub fn destroy(
        self,
        resource_cache: &mut ResourceCache,
    ) {
        for sub_slice in self.sub_slices {
            if let Some(native_surface) = sub_slice.native_surface {
                resource_cache.destroy_compositor_surface(native_surface.opaque);
                resource_cache.destroy_compositor_surface(native_surface.alpha);
            }
        }

        for (_, external_surface) in self.external_native_surface_cache {
            resource_cache.destroy_compositor_surface(external_surface.native_surface_id)
        }

        if let Some(backdrop_surface) = &self.backdrop_surface {
            resource_cache.destroy_compositor_surface(backdrop_surface.id);
        }
    }

    /// Get the tile coordinates for a given rectangle.
    fn get_tile_coords_for_rect(
        &self,
        rect: &PictureRect,
    ) -> (TileOffset, TileOffset) {
        // Get the tile coordinates in the picture space.
        let mut p0 = TileOffset::new(
            (rect.min.x / self.tile_size.width).floor() as i32,
            (rect.min.y / self.tile_size.height).floor() as i32,
        );

        let mut p1 = TileOffset::new(
            (rect.max.x / self.tile_size.width).ceil() as i32,
            (rect.max.y / self.tile_size.height).ceil() as i32,
        );

        // Clamp the tile coordinates here to avoid looping over irrelevant tiles later on.
        p0.x = clamp(p0.x, self.tile_bounds_p0.x, self.tile_bounds_p1.x);
        p0.y = clamp(p0.y, self.tile_bounds_p0.y, self.tile_bounds_p1.y);
        p1.x = clamp(p1.x, self.tile_bounds_p0.x, self.tile_bounds_p1.x);
        p1.y = clamp(p1.y, self.tile_bounds_p0.y, self.tile_bounds_p1.y);

        (p0, p1)
    }

    /// Update transforms, opacity, color bindings and tile rects.
    pub fn pre_update(
        &mut self,
        surface_index: SurfaceIndex,
        frame_context: &FrameVisibilityContext,
        frame_state: &mut FrameVisibilityState,
    ) {
        let surface = &frame_state.surfaces[surface_index.0];
        let pic_rect = surface.unclipped_local_rect;

        self.surface_index = surface_index;
        self.visibility_node_index = surface.raster_spatial_node_index;
        self.local_rect = pic_rect;
        self.local_clip_rect = PictureRect::max_rect();
        self.deferred_dirty_tests.clear();
        self.mix_blend_pic_rects.clear();
        self.underlays.clear();
        self.overlay_region = PictureRect::zero();
        self.yuv_images_remaining = self.yuv_images_count;
        for stability in self.yuv_surface_stability.values_mut() {
            stability.seen_this_frame = false;
        }

        for sub_slice in &mut self.sub_slices {
            sub_slice.reset();
        }

        // Reset the opaque rect + subpixel mode, as they are calculated
        // during the prim dependency checks.
        self.backdrop = BackdropInfo::empty();

        // Calculate the screen rect in picture space, for later comparison against
        // backdrops, and prims potentially covering backdrops.
        let pic_to_root_mapper = SpaceMapper::new_with_target(
            frame_context.root_spatial_node_index,
            self.spatial_node_index,
            frame_context.global_screen_device_rect,
            frame_context.spatial_tree,
        );
        self.screen_rect_in_pic_space = pic_to_root_mapper
            .unmap(&frame_context.global_screen_device_rect)
            .expect("unable to unmap screen rect");

        let pic_to_raster_mapper = SpaceMapper::new_with_target(
            surface.raster_spatial_node_index,
            self.spatial_node_index,
            surface.culling_rect,
            frame_context.spatial_tree,
        );

        // If there is a valid set of shared clips, build a clip chain instance for this,
        // which will provide a local clip rect. This is useful for establishing things
        // like whether the backdrop rect supplied by Gecko can be considered opaque.
        if let Some(tile_clip_node_id) = self.tile_clip_node_id {
            let map_local_to_picture = SpaceMapper::new(
                self.spatial_node_index,
                pic_rect,
            );

            let mut clip_snapper = SpaceSnapper::new(surface, frame_context.spatial_tree);

            // A tile cache leaves its shared clip chain exact, like any other
            // device-space content.
            let clip_snap = ClipSnap::Exact;

            let clip_root = frame_state.current_clip_root();

            frame_state.clip_store.set_active_clips(
                self.spatial_node_index,
                map_local_to_picture.ref_spatial_node_index,
                surface.raster_spatial_node_index,
                &mut clip_snapper,
                clip_snap,
                tile_clip_node_id,
                clip_root,
                // A tile cache has no local clip rect of its own.
                LayoutRect::max_rect(),
                frame_context.spatial_tree,
                &frame_state.data_stores.clip,
                &frame_state.clip_tree,
            );

            let clip_chain_instance = frame_state.clip_store.build_clip_chain_instance(
                pic_rect.cast_unit(),
                &map_local_to_picture,
                &pic_to_raster_mapper,
                &mut frame_state.frame_gpu_data.f32,
                frame_state.resource_cache,
                &surface.culling_rect,
                &frame_state.data_stores.clip,
                frame_state.rg_builder,
                true,
            );

            // Ensure that if the entire picture cache is clipped out, the local
            // clip rect is zero. This makes sure we don't register any occluders
            // that are actually off-screen.
            self.local_clip_rect = PictureRect::zero();
            self.compositor_clip = None;
            self.compositor_clip_shapes.clear();

            if let Some(clip_chain) = clip_chain_instance {
                self.local_clip_rect = clip_chain.pic_coverage_rect;
                self.compositor_clip = None;

                if clip_chain.needs_mask {
                    let mut combined: Option<(DeviceRect, BorderRadius)> = None;

                    for i in 0 .. clip_chain.clips_range.count {
                        let clip_instance = frame_state
                            .clip_store
                            .get_instance_from_range(&clip_chain.clips_range, i);
                        let clip_node = &frame_state.data_stores.clip[clip_instance.handle];

                        if let ClipItemKind::RoundedRectangle { radius, inset: _, mode } = clip_node.item.kind {
                            assert_eq!(mode, ClipMode::Clip);

                            let radius = clamped_radius(&radius, clip_instance.clip_rect.size());

                            // Map to device space. All shared rounded-rect clips are in the
                            // root coordinate system (is_rcs), so only a 2D axis-aligned
                            // transform can apply (e.g. pinch-zoom).
                            let map = ClipSpaceConversion::new(
                                frame_context.root_spatial_node_index,
                                clip_instance.spatial_node_index,
                                frame_context.root_spatial_node_index,
                                frame_context.spatial_tree,
                            );

                            let (device_rect, device_radius) = match map {
                                ClipSpaceConversion::Local => (clip_instance.clip_rect.cast_unit(), radius),
                                ClipSpaceConversion::ScaleOffset(so) => (
                                    so.map_rect(&clip_instance.clip_rect),
                                    BorderRadius {
                                        top_left: so.map_size(&radius.top_left),
                                        top_right: so.map_size(&radius.top_right),
                                        bottom_left: so.map_size(&radius.bottom_left),
                                        bottom_right: so.map_size(&radius.bottom_right),
                                        shape_top_left: radius.shape_top_left,
                                        shape_top_right: radius.shape_top_right,
                                        shape_bottom_left: radius.shape_bottom_left,
                                        shape_bottom_right: radius.shape_bottom_right,
                                    },
                                ),
                                ClipSpaceConversion::Transform(..) |
                                ClipSpaceConversion::Indeterminate => unreachable!(),
                            };

                            // A clip that can't be intersected with the previous ones
                            // is left out of the compositor clip, and of its shapes.
                            let next = match combined {
                                None => Some((device_rect, device_radius)),
                                Some((prev_rect, prev_radius)) => {
                                    intersect_rounded_rects(
                                        prev_rect.cast_unit(), prev_radius,
                                        device_rect.cast_unit(), device_radius,
                                    )
                                    .map(|(r, rad)| (r.cast_unit(), rad))
                                }
                            };
                            if let Some(next) = next {
                                combined = Some(next);
                                self.compositor_clip_shapes.push(CompositorClipShape {
                                    rect: clip_instance.clip_rect,
                                    radius,
                                    spatial_node_index: clip_instance.spatial_node_index,
                                    uid: clip_instance.handle.uid().get_uid(),
                                });
                            }
                        }
                    }

                    if let Some((rect, radius)) = combined {
                        self.compositor_clip = Some(frame_state.composite_state.register_clip(
                            rect,
                            radius,
                        ));
                    }
                }
            }
        }

        // Advance the current frame ID counter for this picture cache (must be done
        // after any retained prev state is taken above).
        self.frame_id.advance();

        // At the start of the frame, step through each current compositor surface
        // and mark it as unused. Later, this is used to free old compositor surfaces.
        // TODO(gw): In future, we might make this more sophisticated - for example,
        //           retaining them for >1 frame if unused, or retaining them in some
        //           kind of pool to reduce future allocations.
        for external_native_surface in self.external_native_surface_cache.values_mut() {
            external_native_surface.used_this_frame = false;
        }

        // Only evaluate what tile size to use fairly infrequently, so that we don't end
        // up constantly invalidating and reallocating tiles if the picture rect size is
        // changing near a threshold value.
        if self.frames_until_size_eval == 0 ||
           self.tile_size_override != frame_context.config.tile_size_override {

            // Work out what size tile is appropriate for this picture cache.
            let desired_tile_size = match frame_context.config.tile_size_override {
                Some(tile_size_override) => {
                    tile_size_override
                }
                None => {
                    if self.slice_flags.contains(SliceFlags::IS_SCROLLBAR) {
                        if pic_rect.width() <= pic_rect.height() {
                            TILE_SIZE_SCROLLBAR_VERTICAL
                        } else {
                            TILE_SIZE_SCROLLBAR_HORIZONTAL
                        }
                    } else {
                        frame_state.resource_cache.picture_textures.default_tile_size()
                    }
                }
            };

            // If the desired tile size has changed, then invalidate and drop any
            // existing tiles.
            if desired_tile_size != self.current_tile_size {
                for sub_slice in &mut self.sub_slices {
                    // Destroy any native surfaces on the tiles that will be dropped due
                    // to resizing.
                    if let Some(native_surface) = sub_slice.native_surface.take() {
                        frame_state.resource_cache.destroy_compositor_surface(native_surface.opaque);
                        frame_state.resource_cache.destroy_compositor_surface(native_surface.alpha);
                    }
                    sub_slice.tiles.clear();
                }
                self.tile_rect = TileRect::zero();
                self.current_tile_size = desired_tile_size;
            }

            // Reset counter until next evaluating the desired tile size. This is an
            // arbitrary value.
            self.frames_until_size_eval = 120;
            self.tile_size_override = frame_context.config.tile_size_override;
        }

        // Get the complete scale-offset from local space to device space
        let local_to_device = get_relative_scale_offset(
            self.spatial_node_index,
            frame_context.root_spatial_node_index,
            frame_context.spatial_tree,
        );

        // Get the compositor transform, which depends on pinch-zoom mode
        let mut raster_to_device = local_to_device;

        if frame_context.config.low_quality_pinch_zoom {
            raster_to_device.scale.x /= self.current_raster_scale;
            raster_to_device.scale.y /= self.current_raster_scale;
        } else {
            raster_to_device.scale.x = 1.0;
            raster_to_device.scale.y = 1.0;
        }

        // Use that compositor transform to calculate a relative local to surface
        let local_to_raster = local_to_device.then(&raster_to_device.inverse());

        const EPSILON: f32 = 0.001;
        let compositor_translation_changed =
            !raster_to_device.offset.x.approx_eq_eps(&self.raster_to_device.offset.x, &EPSILON) ||
            !raster_to_device.offset.y.approx_eq_eps(&self.raster_to_device.offset.y, &EPSILON);
        let compositor_scale_changed =
            !raster_to_device.scale.x.approx_eq_eps(&self.raster_to_device.scale.x, &EPSILON) ||
            !raster_to_device.scale.y.approx_eq_eps(&self.raster_to_device.scale.y, &EPSILON);
        let surface_scale_changed =
            !local_to_raster.scale.x.approx_eq_eps(&self.local_to_raster.scale.x, &EPSILON) ||
            !local_to_raster.scale.y.approx_eq_eps(&self.local_to_raster.scale.y, &EPSILON);

        if compositor_translation_changed ||
           compositor_scale_changed ||
           surface_scale_changed ||
           frame_context.config.force_invalidation {
            frame_state.composite_state.dirty_rects_are_valid = false;
        }

        self.raster_to_device = raster_to_device;
        self.local_to_raster = local_to_raster;
        self.invalidate_all_tiles = surface_scale_changed || frame_context.config.force_invalidation;

        // Do a hacky diff of opacity binding values from the last frame. This is
        // used later on during tile invalidation tests.
        let current_properties = frame_context.scene_properties.float_properties();
        mem::swap(&mut self.opacity_bindings, &mut self.old_opacity_bindings);

        self.opacity_bindings.clear();
        for (id, value) in current_properties {
            let changed = match self.old_opacity_bindings.get(id) {
                Some(old_property) => !old_property.value.approx_eq(value),
                None => true,
            };
            self.opacity_bindings.insert(*id, OpacityBindingInfo {
                value: *value,
                changed,
            });
        }

        // Do a hacky diff of color binding values from the last frame. This is
        // used later on during tile invalidation tests.
        let current_properties = frame_context.scene_properties.color_properties();
        mem::swap(&mut self.color_bindings, &mut self.old_color_bindings);

        self.color_bindings.clear();
        for (id, value) in current_properties {
            let changed = match self.old_color_bindings.get(id) {
                Some(old_property) => old_property.value != (*value).into(),
                None => true,
            };
            self.color_bindings.insert(*id, ColorBindingInfo {
                value: (*value).into(),
                changed,
            });
        }

        self.tile_size = PictureSize::new(
            self.current_tile_size.width as f32 / self.local_to_raster.scale.x,
            self.current_tile_size.height as f32 / self.local_to_raster.scale.y,
        );

        // Inflate the needed rect a bit, so that we retain tiles that we have drawn
        // but have just recently gone off-screen. This means that we avoid re-drawing
        // tiles if the user is scrolling up and down small amounts, at the cost of
        // a bit of extra texture memory.
        let desired_rect_in_pic_space = self.screen_rect_in_pic_space
            .inflate(0.0, 1.0 * self.tile_size.height);

        let needed_rect_in_pic_space = desired_rect_in_pic_space
            .intersection(&pic_rect)
            .unwrap_or_else(Box2D::zero);

        let p0 = needed_rect_in_pic_space.min;
        let p1 = needed_rect_in_pic_space.max;

        let x0 = (p0.x / self.tile_size.width).floor() as i32;
        let x1 = (p1.x / self.tile_size.width).ceil() as i32;

        let y0 = (p0.y / self.tile_size.height).floor() as i32;
        let y1 = (p1.y / self.tile_size.height).ceil() as i32;

        let new_tile_rect = TileRect {
            min: TileOffset::new(x0, y0),
            max: TileOffset::new(x1, y1),
        };

        // Rebuild the tile grid if the picture cache rect has changed.
        if new_tile_rect != self.tile_rect {
            for sub_slice in &mut self.sub_slices {
                let mut old_tiles = sub_slice.resize(new_tile_rect);

                // When old tiles that remain after the loop, dirty rects are not valid.
                if !old_tiles.is_empty() {
                    frame_state.composite_state.dirty_rects_are_valid = false;
                }

                // Any old tiles that remain after the loop above are going to be dropped. For
                // simple composite mode, the texture cache handle will expire and be collected
                // by the texture cache. For native compositor mode, we need to explicitly
                // invoke a callback to the client to destroy that surface.
                if frame_state.composite_state.compositor_kind.uses_native_tiles() {
                    for tile in old_tiles.values_mut() {
                        // Only destroy native surfaces that have been allocated. It's
                        // possible for display port tiles to be created that never
                        // come on screen, and thus never get a native surface allocated.
                        if let Some(TileSurface::Texture { descriptor: SurfaceTextureDescriptor::Native { ref mut id, .. }, .. }) = tile.surface {
                            if let Some(id) = id.take() {
                                frame_state.resource_cache.destroy_compositor_tile(id);
                            }
                        }
                    }
                }
            }
        }

        // This is duplicated information from tile_rect, but cached here to avoid
        // redundant calculations during get_tile_coords_for_rect
        self.tile_bounds_p0 = TileOffset::new(x0, y0);
        self.tile_bounds_p1 = TileOffset::new(x1, y1);
        self.tile_rect = new_tile_rect;

        let mut ctx = TilePreUpdateContext {
            pic_to_device_mapper: pic_to_root_mapper,
            background_color: self.background_color,
            global_screen_device_rect: frame_context.global_screen_device_rect,
            tile_size: self.tile_size,
            frame_id: self.frame_id,
            local_to_raster: self.local_to_raster,
        };

        self.corners_cache.pre_update();

        // Pre-update each tile
        for sub_slice in &mut self.sub_slices {
            for tile in sub_slice.tiles.values_mut() {
                tile.pre_update(&ctx);
            }

            // The background color can only be applied to the first sub-slice.
            ctx.background_color = None;
        }
    }

    fn can_promote_to_surface(
        &mut self,
        prim_clip_chain: &ClipChainInstance,
        prim_spatial_node_index: SpatialNodeIndex,
        is_root_tile_cache: bool,
        sub_slice_index: usize,
        surface_kind: CompositorSurfaceKind,
        pic_coverage_rect: PictureRect,
        frame_context: &FrameVisibilityContext,
        data_stores: &DataStores,
        clip_store: &ClipStore,
        composite_state: &CompositeState,
        color_depth: Option<ColorDepth>,
    ) -> Result<CompositorSurfaceKind, SurfacePromotionFailure> {
        use SurfacePromotionFailure::*;

        // Each strategy has different restrictions on whether we can promote
        match surface_kind {
            CompositorSurfaceKind::Overlay => {
                // For now, only support a small (arbitrary) number of compositor surfaces.
                // Non-opaque compositor surfaces require sub-slices, as they are drawn
                // as overlays.
                if sub_slice_index == self.sub_slices.len() - 1 {
                    return Err(OverlaySurfaceLimit);
                }

                // If a complex clip is being applied to this primitive, it can't be
                // promoted directly to a compositor surface.
                if prim_clip_chain.needs_mask {
                    let mut is_supported_rounded_rect = false;
                    if composite_state.compositor_kind.supports_rounded_clip_promotion() {
                        if prim_clip_chain.clips_range.count == 1 && self.compositor_clip.is_none() {
                            let clip_instance = clip_store.get_instance_from_range(&prim_clip_chain.clips_range, 0);
                            let clip_node = &data_stores.clip[clip_instance.handle];

                            if let ClipItemKind::RoundedRectangle { ref radius, mode: ClipMode::Clip, .. } = clip_node.item.kind {
                                // Compositor rounded clips only support the default round
                                // corner shape. Other shapes require a WebRender clip mask.
                                if !radius.shapes_all_round() {
                                    return Err(OverlayNeedsMask);
                                }

                                // The compositor clip is mapped to device space with a 2d
                                // scale and offset, which isn't possible if the clip is in
                                // a different coordinate system than the root.
                                let map = ClipSpaceConversion::new(
                                    frame_context.root_spatial_node_index,
                                    clip_instance.spatial_node_index,
                                    frame_context.root_spatial_node_index,
                                    frame_context.spatial_tree,
                                );
                                if !matches!(map, ClipSpaceConversion::Local | ClipSpaceConversion::ScaleOffset(..)) {
                                    return Err(OverlayNeedsMask);
                                }

                                let size = clip_instance.clip_rect.size();
                                let radius = clamped_radius(radius, size);
                                let max_corner_width = radius.top_left.width
                                                            .max(radius.bottom_left.width)
                                                            .max(radius.top_right.width)
                                                            .max(radius.bottom_right.width);
                                let max_corner_height = radius.top_left.height
                                                            .max(radius.bottom_left.height)
                                                            .max(radius.top_right.height)
                                                            .max(radius.bottom_right.height);

                                if max_corner_width <= 0.5 * size.width &&
                                    max_corner_height <= 0.5 * size.height {
                                    is_supported_rounded_rect = true;
                                }
                            }
                        }
                    }

                    if !is_supported_rounded_rect {
                        return Err(OverlayNeedsMask);
                    }
                }
            }
            CompositorSurfaceKind::Underlay => {
                // Should we force the promotion of this surface? We'll force it if promotion
                // is necessary for correct color display of HDR.
                let force_for_hdr = matches!(color_depth, Some(color_depth) if color_depth.bit_depth() > 8);

                if !force_for_hdr && self.is_sampled_by_backdrop(&pic_coverage_rect) {
                    return Err(UnderBackdropFilter);
                }

                // If a mask is needed, there are some restrictions.
                if prim_clip_chain.needs_mask {
                    // Need an opaque region behind this prim. The opaque region doesn't
                    // need to span the entire visible region of the TileCacheInstance,
                    // which would set self.backdrop.kind, but that also qualifies.
                    if !self.backdrop.opaque_rect.contains_box(&pic_coverage_rect) {
                        let result = Err(UnderlayAlphaBackdrop);
                        // If we aren't forcing, give up and return Err.
                        if !force_for_hdr {
                            return result;
                        }

                        // Log this but don't return an error.
                        self.report_promotion_failure(result, pic_coverage_rect, true);
                    }

                    if !self.underlays.is_empty() {
                        // If we aren't forcing, we limit the number of masked underlays
                        // Permit more underlays when forced.
                        // XXX WebRender does not support full HDR yet.
                        // HDR requires external composite to show correct colors.
                        if !force_for_hdr || self.underlays.len() > MAX_COMPOSITOR_UNDERLAY_SURFACES {
                            return Err(UnderlaySurfaceLimit);
                        }
                    }
                }

                // Underlays can't appear on top of overlays, because they can't punch
                // through the existing overlay.
                if self.overlay_region.intersects(&pic_coverage_rect) {
                    let result = Err(UnderlayIntersectsOverlay);
                    // If we aren't forcing, give up and return Err.
                    if !force_for_hdr {
                        return result;
                    }

                    // Log this but don't return an error.
                    self.report_promotion_failure(result, pic_coverage_rect, true);
                }

                // Underlay cutouts are difficult to align with compositor surfaces when
                // compositing during low-quality zoom, and the required invalidation
                // whilst zooming would prevent low-quality zoom from working efficiently.
                if frame_context.config.low_quality_pinch_zoom &&
                    frame_context.spatial_tree.get_spatial_node(prim_spatial_node_index).is_ancestor_or_self_zooming
                {
                    return Err(UnderlayLowQualityZoom);
                }
            }
            CompositorSurfaceKind::Blit => unreachable!(),
        }

        // If not on the root picture cache, it has some kind of
        // complex effect (such as a filter, mix-blend-mode or 3d transform).
        if !is_root_tile_cache {
            return Err(NotRootTileCache);
        }

        let mapper : SpaceMapper<PicturePixel, DevicePixel> = SpaceMapper::new_with_target(
            frame_context.root_spatial_node_index,
            prim_spatial_node_index,
            frame_context.global_screen_device_rect,
            &frame_context.spatial_tree);
        let transform = mapper.get_transform();
        if !transform.is_2d_scale_translation() {
            let result = Err(ComplexTransform);
            // Unfortunately, ComplexTransform absolutely prevents proper
				    // functioning of surface promotion. Treating this as a warning
            // instead of an error will cause a crash in get_relative_scale_offset.
            return result;
        }

        if surface_kind != CompositorSurfaceKind::Underlay {
            if self.slice_flags.contains(SliceFlags::IS_ATOMIC) {
                return Err(SliceAtomic);
            }
            if self.is_sampled_by_backdrop(&pic_coverage_rect) {
                return Err(UnderBackdropFilter);
            }
        }

        Ok(surface_kind)
    }

    fn setup_compositor_surfaces_yuv(
        &mut self,
        draw_index: PrimitiveDrawIndex,
        sub_slice_index: usize,
        prim_info: &mut PrimitiveDependencyInfo,
        flags: PrimitiveFlags,
        local_prim_rect: LayoutRect,
        prim_clip_chain: &ClipChainInstance,
        prim_spatial_node_index: SpatialNodeIndex,
        pic_coverage_rect: PictureRect,
        frame_context: &FrameVisibilityContext,
        data_stores: &DataStores,
        clip_store: &ClipStore,
        image_dependencies: &[ImageDependency;3],
        api_keys: &[ImageKey; 3],
        resource_cache: &mut ResourceCache,
        composite_state: &mut CompositeState,
        gpu_buffer: &mut GpuBufferBuilderF,
        image_rendering: ImageRendering,
        color_depth: ColorDepth,
        color_space: YuvRangedColorSpace,
        format: YuvFormat,
        surface_kind: CompositorSurfaceKind,
    ) -> Result<CompositorSurfaceKind, SurfacePromotionFailure> {
        for &key in api_keys {
            if key != ImageKey::DUMMY {
                // TODO: See comment in setup_compositor_surfaces_rgb.
                resource_cache.request_image(ImageRequest {
                        key,
                        rendering: image_rendering,
                        tile: None,
                    },
                    gpu_buffer,
                );
            }
        }

        self.setup_compositor_surfaces_impl(
            draw_index,
            sub_slice_index,
            prim_info,
            flags,
            local_prim_rect,
            prim_clip_chain,
            prim_spatial_node_index,
            pic_coverage_rect,
            frame_context,
            data_stores,
            clip_store,
            ExternalSurfaceDependency::Yuv {
                image_dependencies: *image_dependencies,
                color_space,
                format,
                channel_bit_depth: color_depth.bit_depth(),
            },
            api_keys,
            resource_cache,
            composite_state,
            image_rendering,
            true,
            surface_kind,
        )
    }

    fn setup_compositor_surfaces_rgb(
        &mut self,
        draw_index: PrimitiveDrawIndex,
        sub_slice_index: usize,
        prim_info: &mut PrimitiveDependencyInfo,
        flags: PrimitiveFlags,
        local_prim_rect: LayoutRect,
        prim_clip_chain: &ClipChainInstance,
        prim_spatial_node_index: SpatialNodeIndex,
        pic_coverage_rect: PictureRect,
        frame_context: &FrameVisibilityContext,
        data_stores: &DataStores,
        clip_store: &ClipStore,
        image_dependency: ImageDependency,
        api_key: ImageKey,
        resource_cache: &mut ResourceCache,
        composite_state: &mut CompositeState,
        gpu_buffer: &mut GpuBufferBuilderF,
        image_rendering: ImageRendering,
        is_opaque: bool,
        surface_kind: CompositorSurfaceKind,
    ) -> Result<CompositorSurfaceKind, SurfacePromotionFailure> {
        let mut api_keys = [ImageKey::DUMMY; 3];
        api_keys[0] = api_key;

        // TODO: The picture compositing code requires images promoted
        // into their own picture cache slices to be requested every
        // frame even if they are not visible. However the image updates
        // are only reached on the prepare pass for visible primitives.
        // So we make sure to trigger an image request when promoting
        // the image here.
        resource_cache.request_image(ImageRequest {
                key: api_key,
                rendering: image_rendering,
                tile: None,
            },
            gpu_buffer,
        );

        self.setup_compositor_surfaces_impl(
            draw_index,
            sub_slice_index,
            prim_info,
            flags,
            local_prim_rect,
            prim_clip_chain,
            prim_spatial_node_index,
            pic_coverage_rect,
            frame_context,
            data_stores,
            clip_store,
            ExternalSurfaceDependency::Rgb {
                image_dependency,
            },
            &api_keys,
            resource_cache,
            composite_state,
            image_rendering,
            is_opaque,
            surface_kind,
        )
    }

    // returns false if composition is not available for this surface,
    // and the non-compositor path should be used to draw it instead.
    fn setup_compositor_surfaces_impl(
        &mut self,
        draw_index: PrimitiveDrawIndex,
        sub_slice_index: usize,
        prim_info: &mut PrimitiveDependencyInfo,
        flags: PrimitiveFlags,
        local_prim_rect: LayoutRect,
        prim_clip_chain: &ClipChainInstance,
        prim_spatial_node_index: SpatialNodeIndex,
        pic_coverage_rect: PictureRect,
        frame_context: &FrameVisibilityContext,
        data_stores: &DataStores,
        clip_store: &ClipStore,
        dependency: ExternalSurfaceDependency,
        api_keys: &[ImageKey; 3],
        resource_cache: &mut ResourceCache,
        composite_state: &mut CompositeState,
        image_rendering: ImageRendering,
        is_opaque: bool,
        surface_kind: CompositorSurfaceKind,
    ) -> Result<CompositorSurfaceKind, SurfacePromotionFailure> {
        use SurfacePromotionFailure::*;

        let map_local_to_picture = SpaceMapper::new_with_target(
            self.spatial_node_index,
            prim_spatial_node_index,
            self.local_rect,
            frame_context.spatial_tree,
        );

        // Map the primitive local rect into picture space.
        let prim_rect = match map_local_to_picture.map(&local_prim_rect) {
            Some(rect) => rect,
            None => return Ok(surface_kind),
        };

        // If the rect is invalid, no need to create dependencies.
        if prim_rect.is_empty() {
            return Ok(surface_kind);
        }

        let pic_to_root_mapper = SpaceMapper::new_with_target(
            frame_context.root_spatial_node_index,
            self.spatial_node_index,
            frame_context.global_screen_device_rect,
            frame_context.spatial_tree,
        );

        let device_clip_rect = pic_to_root_mapper
            .map(&prim_info.prim_clip_box)
            .expect("bug: unable to map clip to device space");

        let is_visible = device_clip_rect.intersects(&frame_context.global_screen_device_rect);
        if !is_visible {
            return Ok(surface_kind);
        }

        let prim_offset = ScaleOffset::from_offset(local_prim_rect.min.to_vector().cast_unit());

        let local_prim_to_device = get_relative_scale_offset(
            prim_spatial_node_index,
            frame_context.root_spatial_node_index,
            frame_context.spatial_tree,
        );

        let normalized_prim_to_device = prim_offset.then(&local_prim_to_device);

        let local_to_raster = ScaleOffset::identity();
        let raster_to_device = normalized_prim_to_device;

        // If this primitive is an external image, and supports being used
        // directly by a native compositor, then lookup the external image id
        // so we can pass that through.
        let external_image_id = if flags.contains(PrimitiveFlags::SUPPORTS_EXTERNAL_COMPOSITOR_SURFACE)
            && image_rendering == ImageRendering::Auto {
            resource_cache.get_image_properties(api_keys[0])
                .and_then(|properties| properties.external_image)
                .and_then(|image| Some(image.id))
        } else {
            None
        };

        let compositor_transform_index = composite_state.register_transform(
            local_to_raster,
            raster_to_device,
        );

        let surface_size = composite_state.get_surface_rect(
            &local_prim_rect,
            &local_prim_rect,
            compositor_transform_index,
        ).size();

        let clip_rect = device_clip_rect.round();

        let mut compositor_clip_index = None;

        if surface_kind == CompositorSurfaceKind::Overlay &&
            prim_clip_chain.needs_mask {
            assert!(prim_clip_chain.clips_range.count == 1);
            assert!(self.compositor_clip.is_none());

            let clip_instance = clip_store.get_instance_from_range(&prim_clip_chain.clips_range, 0);
            let clip_node = &data_stores.clip[clip_instance.handle];
            if let ClipItemKind::RoundedRectangle { radius, mode: ClipMode::Clip, .. } = clip_node.item.kind {
                let radius = clamped_radius(&radius, clip_instance.clip_rect.size());

                // Map the clip in to device space. can_promote_to_surface only
                // accepts clips in the root coord system, so only a 2d
                // axis-aligned transform can apply. For example, in the case
                // of a pinch-zoom effect.
                let map = ClipSpaceConversion::new(
                    frame_context.root_spatial_node_index,
                    clip_instance.spatial_node_index,
                    frame_context.root_spatial_node_index,
                    frame_context.spatial_tree,
                );

                let (rect, radius) = match map {
                    ClipSpaceConversion::Local => {
                        (clip_instance.clip_rect.cast_unit(), radius)
                    }
                    ClipSpaceConversion::ScaleOffset(scale_offset) => {
                        (
                            scale_offset.map_rect(&clip_instance.clip_rect),
                            BorderRadius {
                                top_left: scale_offset.map_size(&radius.top_left),
                                top_right: scale_offset.map_size(&radius.top_right),
                                bottom_left: scale_offset.map_size(&radius.bottom_left),
                                bottom_right: scale_offset.map_size(&radius.bottom_right),
                                shape_top_left: radius.shape_top_left,
                                shape_top_right: radius.shape_top_right,
                                shape_bottom_left: radius.shape_bottom_left,
                                shape_bottom_right: radius.shape_bottom_right,
                            },
                        )
                    }
                    ClipSpaceConversion::Transform(..) |
                    ClipSpaceConversion::Indeterminate => {
                        unreachable!();
                    }
                };

                compositor_clip_index = Some(composite_state.register_clip(
                    rect,
                    radius,
                ));
            } else {
                unreachable!();
            }
        }

        if surface_size.width >= MAX_COMPOSITOR_SURFACES_SIZE ||
           surface_size.height >= MAX_COMPOSITOR_SURFACES_SIZE {
           return Err(SizeTooLarge);
        }

        // When using native compositing, we need to find an existing native surface
        // handle to use, or allocate a new one. For existing native surfaces, we can
        // also determine whether this needs to be updated, depending on whether the
        // image generation(s) of the planes have changed since last composite.
        let (native_surface_id, update_params) = if composite_state.compositor_kind.uses_native_tiles() {
            let native_surface_size = surface_size.to_i32();

                let key = ExternalNativeSurfaceKey {
                    image_keys: *api_keys,
                    size: if external_image_id.is_some() { None } else { Some(native_surface_size) },
                };

                let native_surface = self.external_native_surface_cache
                    .entry(key)
                    .or_insert_with(|| {
                        // No existing surface, so allocate a new compositor surface.
                        let native_surface_id = match external_image_id {
                            Some(_external_image) => {
                                // If we have a suitable external image, then create an external
                                // surface to attach to.
                                resource_cache.create_compositor_external_surface(is_opaque)
                            }
                            None => {
                                // Otherwise create a normal compositor surface and a single
                                // compositor tile that covers the entire surface.
                                let native_surface_id =
                                resource_cache.create_compositor_surface(
                                    native_surface_size,
                                    is_opaque,
                                );

                                let tile_id = NativeTileId {
                                    surface_id: native_surface_id,
                                    x: 0,
                                    y: 0,
                                };
                                resource_cache.create_compositor_tile(tile_id);

                                native_surface_id
                            }
                        };

                        ExternalNativeSurface {
                            used_this_frame: true,
                            native_surface_id,
                            image_dependencies: [ImageDependency::INVALID; 3],
                        }
                    });

                // Mark that the surface is referenced this frame so that the
                // backing native surface handle isn't freed.
                native_surface.used_this_frame = true;

                let update_params = match external_image_id {
                    Some(external_image) => {
                        // If this is an external image surface, then there's no update
                        // to be done. Just attach the current external image to the surface
                        // and we're done.
                        resource_cache.attach_compositor_external_image(
                            native_surface.native_surface_id,
                            external_image,
                        );
                        None
                    }
                    None => {
                        // If the image dependencies match, there is no need to update
                        // the backing native surface.
                        match dependency {
                            ExternalSurfaceDependency::Yuv{ image_dependencies, .. } => {
                                if image_dependencies == native_surface.image_dependencies {
                                    None
                                } else {
                                    Some(native_surface_size)
                                }
                            },
                            ExternalSurfaceDependency::Rgb{ image_dependency, .. } => {
                                if image_dependency == native_surface.image_dependencies[0] {
                                    None
                                } else {
                                    Some(native_surface_size)
                                }
                            },
                        }
                    }
                };

                (Some(native_surface.native_surface_id), update_params)
        } else {
            (None, None)
        };

        let descriptor = ExternalSurfaceDescriptor {
            local_surface_size: local_prim_rect.size(),
            local_rect: prim_rect,
            local_clip_rect: prim_info.prim_clip_box,
            dependency,
            image_rendering,
            clip_rect,
            transform_index: compositor_transform_index,
            compositor_clip_index: compositor_clip_index,
            z_id: ZBufferId::invalid(),
            native_surface_id,
            update_params,
            external_image_id,
            draw_index,
        };

        // If the surface is opaque, we can draw it an an underlay (which avoids
        // additional sub-slice surfaces, and supports clip masks)
        match surface_kind {
            CompositorSurfaceKind::Underlay => {
                self.underlays.push(descriptor);
            }
            CompositorSurfaceKind::Overlay => {
                // For compositor surfaces, if we didn't find an earlier sub-slice to add to,
                // we know we can append to the current slice.
                assert!(sub_slice_index < self.sub_slices.len() - 1);
                let sub_slice = &mut self.sub_slices[sub_slice_index];

                // Each compositor surface allocates a unique z-id
                sub_slice.compositor_surfaces.push(CompositorSurface {
                    prohibited_rect: pic_coverage_rect,
                    is_opaque,
                    descriptor,
                });

                // Add the pic_coverage_rect to the overlay region. This prevents
                // future promoted surfaces from becoming underlays if they would
                // intersect with the overlay region.
                self.overlay_region = self.overlay_region.union(&pic_coverage_rect);
            }
            CompositorSurfaceKind::Blit => unreachable!(),
        }

        Ok(surface_kind)
    }

    /// Push an estimated rect for an off-screen surface during dependency updates. This is
    /// a workaround / hack that allows the picture cache code to know when it should be
    /// processing primitive dependencies as a single atomic unit. In future, we aim to remove
    /// this hack by having the primitive dependencies stored _within_ each owning picture.
    /// This is part of the work required to support child picture caching anyway!
    pub fn push_surface(
        &mut self,
        estimated_local_rect: LayoutRect,
        surface_spatial_node_index: SpatialNodeIndex,
        spatial_tree: &SpatialTree,
    ) {
        // Only need to evaluate sub-slice regions if we have compositor surfaces present
        if self.current_surface_traversal_depth == 0 && self.sub_slices.len() > 1 {
            let map_local_to_picture = SpaceMapper::new_with_target(
                self.spatial_node_index,
                surface_spatial_node_index,
                self.local_rect,
                spatial_tree,
            );

            if let Some(pic_rect) = map_local_to_picture.map(&estimated_local_rect) {
                // Find the first sub-slice we can add this primitive to (we want to add
                // prims to the primary surface if possible, so they get subpixel AA).
                for sub_slice in &mut self.sub_slices {
                    let mut intersects_prohibited_region = false;

                    for surface in &mut sub_slice.compositor_surfaces {
                        if pic_rect.intersects(&surface.prohibited_rect) {
                            surface.prohibited_rect = surface.prohibited_rect.union(&pic_rect);

                            intersects_prohibited_region = true;
                        }
                    }

                    if !intersects_prohibited_region {
                        break;
                    }
                }
            }
        }

        self.current_surface_traversal_depth += 1;
    }

    /// Pop an off-screen surface off the stack during dependency updates
    pub fn pop_surface(&mut self) {
        self.current_surface_traversal_depth -= 1;
    }

    fn report_promotion_failure(&self,
                                result: Result<CompositorSurfaceKind, SurfacePromotionFailure>,
                                rect: PictureRect,
                                ignored: bool) {
        if !self.debug_flags.contains(DebugFlags::SURFACE_PROMOTION_LOGGING) || result.is_ok() {
            return;
        }

        // Report this as a warning.
        // TODO: Find a way to expose this to web authors.
        let outcome = if ignored { "failure ignored" } else { "failed" };
        warn!("Surface promotion of prim at {:?} {outcome} with: {}.", rect, result.unwrap_err());
    }

    /// Update the dependencies for each tile for a given primitive instance.
    pub fn update_prim_dependencies(
        &mut self,
        draw_index: PrimitiveDrawIndex,
        prim_instance: &mut PrimitiveInstance,
        prim_spatial_node_index: SpatialNodeIndex,
        local_prim_rect: LayoutRect,
        frame_context: &FrameVisibilityContext,
        data_stores: &DataStores,
        dl_stores: &DlStores,
        clip_store: &ClipStore,
        pictures: &[PictureInstance],
        resource_cache: &mut ResourceCache,
        surface_stack: &[(PictureIndex, SurfaceIndex)],
        composite_state: &mut CompositeState,
        gpu_buffer: &mut GpuBufferBuilderF,
        scratch: &mut PrimitiveScratchBuffer,
        is_root_tile_cache: bool,
        surfaces: &mut [SurfaceInfo],
        profile: &mut TransactionProfile,
    ) -> DrawState {
        use SurfacePromotionFailure::*;

        // This primitive exists on the last element on the current surface stack.
        tracy_rs::profile_scope!("update_prim_dependencies");
        let prim_surface_index = surface_stack.last().unwrap().1;
        let prim_clip_chain = scratch.frame.draw(draw_index).clip_chain;
        let prim_clip_chain = &prim_clip_chain;

        // If the primitive is directly drawn onto this picture cache surface, then
        // the pic_coverage_rect is in the same space. If not, we need to map it from
        // the intermediate picture space into the picture cache space.
        let on_picture_surface = prim_surface_index == self.surface_index;
        let pic_coverage_rect = if on_picture_surface {
            prim_clip_chain.pic_coverage_rect
        } else {
            // We want to get the rect in the tile cache picture space that this primitive
            // occupies, in order to enable correct invalidation regions. Each surface
            // that exists in the chain between this primitive and the tile cache surface
            // may have an arbitrary inflation factor (for example, in the case of a series
            // of nested blur elements). To account for this, step through the current
            // surface stack, mapping the primitive rect into each picture space, including
            // the inflation factor from each intermediate surface.
            let mut current_pic_coverage_rect = prim_clip_chain.pic_coverage_rect;
            let mut current_spatial_node_index = surfaces[prim_surface_index.0]
                .surface_spatial_node_index;

            for (pic_index, surface_index) in surface_stack.iter().rev() {
                let surface = &surfaces[surface_index.0];
                let pic = &pictures[pic_index.0 as usize];

                let map_local_to_parent = SpaceMapper::new_with_target(
                    surface.surface_spatial_node_index,
                    current_spatial_node_index,
                    surface.unclipped_local_rect,
                    frame_context.spatial_tree,
                );

                // Map the rect into the parent surface, and inflate if this surface requires
                // it. If the rect can't be mapping (e.g. due to an invalid transform) then
                // just bail out from the dependencies and cull this primitive.
                current_pic_coverage_rect = match map_local_to_parent.map(&current_pic_coverage_rect) {
                    Some(rect) => {
                        // TODO(gw): The casts here are a hack. We have some interface inconsistencies
                        //           between layout/picture rects which don't really work with the
                        //           current unit system, since sometimes the local rect of a picture
                        //           is a LayoutRect, and sometimes it's a PictureRect. Consider how
                        //           we can improve this?
                        pic.composite_mode.as_ref().unwrap().get_coverage(
                            surface,
                            Some(rect.cast_unit()),
                        ).cast_unit()
                    }
                    None => {
                        return DrawState::Culled;
                    }
                };

                current_spatial_node_index = surface.surface_spatial_node_index;
            }

            current_pic_coverage_rect
        };

        // Get the tile coordinates in the picture space.
        let (p0, p1) = self.get_tile_coords_for_rect(&pic_coverage_rect);

        // If the primitive is outside the tiling rects, it's known to not
        // be visible.
        if p0.x == p1.x || p0.y == p1.y {
            return DrawState::Culled;
        }

        // Build the list of resources that this primitive has dependencies on.
        let mut prim_info = PrimitiveDependencyInfo::new(prim_instance.uid(dl_stores), pic_coverage_rect);
        // Compute once here so it's available for both prim_info and the tile loop.
        let prim_clamp_to_tile = matches!(
            prim_instance.kind,
            PrimitiveKind::Rectangle { .. }
        );

        let mut sub_slice_index = self.sub_slices.len() - 1;

        // Only need to evaluate sub-slice regions if we have compositor surfaces present
        if sub_slice_index > 0 {
            // Find the first sub-slice we can add this primitive to (we want to add
            // prims to the primary surface if possible, so they get subpixel AA).
            for (i, sub_slice) in self.sub_slices.iter_mut().enumerate() {
                let mut intersects_prohibited_region = false;

                for surface in &mut sub_slice.compositor_surfaces {
                    if pic_coverage_rect.intersects(&surface.prohibited_rect) {
                        surface.prohibited_rect = surface.prohibited_rect.union(&pic_coverage_rect);

                        intersects_prohibited_region = true;
                    }
                }

                if !intersects_prohibited_region {
                    sub_slice_index = i;
                    break;
                }
            }
        }

        // Spatial node and clip deps are no longer added; vert corners (computed per
        // tile below) capture transform and clip position changes directly in raster space.

        // Gather clip data needed for the per-tile vert push below.
        let clip_instances = &clip_store
            .clip_node_instances[prim_clip_chain.clips_range.to_range()];

        // Certain primitives may select themselves to be a backdrop candidate, which is
        // then applied below.
        let mut backdrop_candidate = None;

        // For pictures, we don't (yet) know the valid clip rect, so we can't correctly
        // use it to calculate the local bounding rect for the tiles. If we include them
        // then we may calculate a bounding rect that is too large, since it won't include
        // the clip bounds of the picture. Excluding them from the bounding rect here
        // fixes any correctness issues (the clips themselves are considered when we
        // consider the bounds of the primitives that are *children* of the picture),
        // however it does potentially result in some un-necessary invalidations of a
        // tile (in cases where the picture local rect affects the tile, but the clip
        // rect eventually means it doesn't affect that tile).
        // TODO(gw): Get picture clips earlier (during the initial picture traversal
        //           pass) so that we can calculate these correctly.
        match prim_instance.kind {
            PrimitiveKind::Picture { pic_index,.. } => {
                // Pictures can depend on animated opacity bindings.
                let pic = &pictures[pic_index.0 as usize];
                if let Some(PictureCompositeMode::Filter(Filter::Opacity(binding, _))) = pic.composite_mode {
                    prim_info.opacity_bindings.push(binding.into());
                }
            }
            PrimitiveKind::Rectangle { handle } => {
                // Rectangles can only form a backdrop candidate if they are known opaque.
                // TODO(gw): We could resolve the opacity binding here, but the common
                //           case for background rects is that they don't have animated opacity.
                let prim_color = dl_stores.rectangles[handle].kind.color;
                let resolved = frame_context.scene_properties.resolve_color(&prim_color);
                if resolved.a >= 1.0 {
                    backdrop_candidate = Some(BackdropInfo {
                        opaque_rect: pic_coverage_rect,
                        spanning_opaque_color: None,
                        kind: Some(BackdropKind::Color { color: resolved }),
                        backdrop_rect: pic_coverage_rect,
                    });
                }

                if matches!(prim_color, PropertyBinding::Binding(..)) {
                    let color_u: PropertyBinding<ColorU> = prim_color.into();
                    prim_info.color_binding = Some(color_u.into());
                }
            }
            PrimitiveKind::Image { handle } => {
                let image_key = &dl_stores.images[handle];
                let image_data = &image_key.kind;

                // For now, assume that for compositor surface purposes, any RGBA image may be
                // translucent. See the comment in `add_prim` in this source file for more
                // details. We'll leave the `is_opaque` code branches here, but disabled, as
                // in future we will want to support this case correctly.
                let mut is_opaque = false;

                if let Some(image_properties) = resource_cache.get_image_properties(image_data.key) {
                    // For an image to be a possible opaque backdrop, it must:
                    // - Have a valid, opaque image descriptor
                    // - Not use tiling (since they can fail to draw)
                    // - Not having any spacing / padding
                    // - Have opaque alpha in the instance (flattened) color
                    if image_properties.descriptor.is_opaque() &&
                       image_properties.tiling.is_none() &&
                       image_data.tile_spacing == LayoutSize::zero() &&
                       image_data.color.a >= 1.0 {
                        backdrop_candidate = Some(BackdropInfo {
                            opaque_rect: pic_coverage_rect,
                            spanning_opaque_color: None,
                            kind: None,
                            backdrop_rect: PictureRect::zero(),
                        });
                    }

                    is_opaque = image_properties.descriptor.is_opaque();
                }

                let mut promotion_result: Result<CompositorSurfaceKind, SurfacePromotionFailure> = Ok(CompositorSurfaceKind::Blit);
                if image_key.common.flags.contains(PrimitiveFlags::PREFER_COMPOSITOR_SURFACE) {
                    // Only consider promoting Images if all of our YuvImages have been
                    // processed (whether they were promoted or not).
                    if self.yuv_images_remaining > 0 {
                        promotion_result = Err(ImageWaitingOnYuvImage);
                    } else {
                        promotion_result = self.can_promote_to_surface(prim_clip_chain,
                                                          prim_spatial_node_index,
                                                          is_root_tile_cache,
                                                          sub_slice_index,
                                                          CompositorSurfaceKind::Overlay,
                                                          pic_coverage_rect,
                                                          frame_context,
                                                          data_stores,
                                                          clip_store,
                                                          composite_state,
                                                          None);
                    }

                    // Native OS compositors (DC and CA, at least) support premultiplied alpha
                    // only. If we have an image that's not pre-multiplied alpha, we can't promote it.
                    if image_data.alpha_type == AlphaType::Alpha {
                        promotion_result = Err(NotPremultipliedAlpha);
                    }

                    if let Ok(kind) = promotion_result {
                        promotion_result = self.setup_compositor_surfaces_rgb(
                            draw_index,
                            sub_slice_index,
                            &mut prim_info,
                            image_key.common.flags,
                            local_prim_rect,
                            prim_clip_chain,
                            prim_spatial_node_index,
                            pic_coverage_rect,
                            frame_context,
                            data_stores,
                            clip_store,
                            ImageDependency {
                                key: image_data.key,
                                generation: resource_cache.get_image_generation(image_data.key),
                            },
                            image_data.key,
                            resource_cache,
                            composite_state,
                            gpu_buffer,
                            image_data.image_rendering,
                            is_opaque,
                            kind,
                        );
                    }
                }

                    if let Ok(kind) = promotion_result {
                    scratch.frame.draw_mut(draw_index).compositor_surface_kind = kind;

                    if kind == CompositorSurfaceKind::Overlay {
                        profile.inc(profiler::COMPOSITOR_SURFACE_OVERLAYS);
                        return DrawState::Culled;
                    }

                    assert!(kind == CompositorSurfaceKind::Blit, "Image prims should either be overlays or blits.");
                } else {
                    // In Err case, we handle as a blit, and proceed.
                    self.report_promotion_failure(promotion_result, pic_coverage_rect, false);
                    scratch.frame.draw_mut(draw_index).compositor_surface_kind = CompositorSurfaceKind::Blit;
                }

                if image_key.common.flags.contains(PrimitiveFlags::PREFER_COMPOSITOR_SURFACE) {
                    profile.inc(profiler::COMPOSITOR_SURFACE_BLITS);
                }

                prim_info.images.push(ImageDependency {
                    key: image_data.key,
                    generation: resource_cache.get_image_generation(image_data.key),
                });
            }
            PrimitiveKind::YuvImage { handle } => {
                let prim_uid = prim_instance.uid(dl_stores);
                let prim_data = &dl_stores.yuv_images[handle];

                let mut promotion_result: Result<CompositorSurfaceKind, SurfacePromotionFailure> = Ok(CompositorSurfaceKind::Blit);
                if prim_data.common.flags.contains(PrimitiveFlags::PREFER_COMPOSITOR_SURFACE) {
                    // Note if this is one of the YuvImages we were considering for
                    // surface promotion. We only care for primitives that were added
                    // to us, indicated by is_root_tile_cache. Those are the only ones
                    // that were added to the TileCacheParams that configured the
                    // current scene.
                    if is_root_tile_cache {
                        self.yuv_images_remaining -= 1;
                    }

                    // Wide-color / HDR YUV images must keep the existing immediate
                    // promotion behavior. WebRender cannot faithfully flatten these
                    // into 8-bit content, so only apply the stability delay to Color8.
                    let apply_stability_delay =
                        frame_context.config.enable_yuv_overlay_stability &&
                        prim_data.kind.color_depth == ColorDepth::Color8;
                    let allow_promotion = !apply_stability_delay || {
                        let stability = self.yuv_surface_stability
                            .entry(prim_uid)
                            .or_insert(YuvSurfaceStability {
                                pic_coverage_rect,
                                stable_frames: 0,
                                seen_this_frame: true,
                            });

                        stability.seen_this_frame = true;
                        if stability.pic_coverage_rect == pic_coverage_rect {
                            stability.stable_frames = stability.stable_frames.saturating_add(1);
                        } else {
                            stability.pic_coverage_rect = pic_coverage_rect;
                            stability.stable_frames = 1;
                        }

                        stability.stable_frames >= YUV_SURFACE_STABLE_FRAMES
                    };

                    if !allow_promotion {
                        promotion_result = Err(YuvImageNotStable);
                    } else {
                        let promotion_attempts =
                            [CompositorSurfaceKind::Overlay, CompositorSurfaceKind::Underlay];
                        for kind in promotion_attempts {
                            // Since this might be an attempt after an earlier error, clear the flag
                            // so that we are allowed to report another error.
                            promotion_result = self.can_promote_to_surface(
                                                        prim_clip_chain,
                                                        prim_spatial_node_index,
                                                        is_root_tile_cache,
                                                        sub_slice_index,
                                                        kind,
                                                        pic_coverage_rect,
                                                        frame_context,
                                                        data_stores,
                                                        clip_store,
                                                        composite_state,
                                                        Some(prim_data.kind.color_depth));
                            if promotion_result.is_ok() {
                                break;
                            }
                            // We couldn't promote, but did we give up because the slice is marked
                            // atomic? If that was the reason, and the YuvImage is wide color,
                            // failing to promote will flatten the colors and look terrible. Let's
                            // ignore the atomic slice restriction in such a case.
                            if let Err(SliceAtomic) = promotion_result {
                                if prim_data.kind. color_depth != ColorDepth::Color8 {
                                    // Let's promote with the attempted kind.
                                    promotion_result = Ok(kind);
                                    break;
                                }
                            }
                        }
                    }

                    // TODO(gw): When we support RGBA images for external surfaces, we also
                    //           need to check if opaque (YUV images are implicitly opaque).

                    // If this primitive is being promoted to a surface, construct an external
                    // surface descriptor for use later during batching and compositing. We only
                    // add the image keys for this primitive as a dependency if this is _not_
                    // a promoted surface, since we don't want the tiles to invalidate when the
                    // video content changes, if it's a compositor surface!
                    if let Ok(kind) = promotion_result {
                        // Build dependency for each YUV plane, with current image generation for
                        // later detection of when the composited surface has changed.
                        let mut image_dependencies = [ImageDependency::INVALID; 3];
                        for (key, dep) in prim_data.kind.yuv_key.iter().cloned().zip(image_dependencies.iter_mut()) {
                            *dep = ImageDependency {
                                key,
                                generation: resource_cache.get_image_generation(key),
                            }
                        }

                        promotion_result = self.setup_compositor_surfaces_yuv(
                            draw_index,
                            sub_slice_index,
                            &mut prim_info,
                            prim_data.common.flags,
                            local_prim_rect,
                            prim_clip_chain,
                            prim_spatial_node_index,
                            pic_coverage_rect,
                            frame_context,
                            data_stores,
                            clip_store,
                            &image_dependencies,
                            &prim_data.kind.yuv_key,
                            resource_cache,
                            composite_state,
                            gpu_buffer,
                            prim_data.kind.image_rendering,
                            prim_data.kind.color_depth,
                            prim_data.kind.color_space.with_range(prim_data.kind.color_range),
                            prim_data.kind.format,
                            kind,
                        );
                    }
                }

                // Store on the YUV primitive instance whether this is a promoted surface.
                // This is used by the batching code to determine whether to draw the
                // image to the content tiles, or just a transparent z-write.
                    if let Ok(kind) = promotion_result {
                    scratch.frame.draw_mut(draw_index).compositor_surface_kind = kind;
                    if kind == CompositorSurfaceKind::Overlay {
                        profile.inc(profiler::COMPOSITOR_SURFACE_OVERLAYS);
                        return DrawState::Culled;
                    }

                    profile.inc(profiler::COMPOSITOR_SURFACE_UNDERLAYS);
                } else {
                    // In Err case, we handle as a blit, and proceed.
                    self.report_promotion_failure(promotion_result, pic_coverage_rect, false);
                    scratch.frame.draw_mut(draw_index).compositor_surface_kind = CompositorSurfaceKind::Blit;
                    if prim_data.common.flags.contains(PrimitiveFlags::PREFER_COMPOSITOR_SURFACE) {
                        profile.inc(profiler::COMPOSITOR_SURFACE_BLITS);
                    }
                }

                // Underlay with SliceFlags::IS_ATOMIC adds extra invalidation.
                // It is for handling cases where underlay is disabled later.
                let kind = scratch.frame.draw(draw_index).compositor_surface_kind;
                if kind == CompositorSurfaceKind::Blit ||
                    kind == CompositorSurfaceKind::Underlay &&
                    self.slice_flags.contains(SliceFlags::IS_ATOMIC) {
                    prim_info.images.extend(
                        prim_data.kind.yuv_key.iter().map(|key| {
                            ImageDependency {
                                key: *key,
                                generation: resource_cache.get_image_generation(*key),
                            }
                        })
                    );
                }
            }
            PrimitiveKind::ImageBorder { data_handle, .. } => {
                let border_data = &data_stores.image_border[data_handle].kind;
                prim_info.images.push(ImageDependency {
                    key: border_data.request.key,
                    generation: resource_cache.get_image_generation(border_data.request.key),
                });
            }
            PrimitiveKind::LinearGradient { data_handle, .. } => {
                let gradient_data = &data_stores.linear_grad[data_handle];
                if gradient_data.stops_opacity.is_opaque
                    && gradient_data.tile_spacing == LayoutSize::zero()
                {
                    backdrop_candidate = Some(BackdropInfo {
                        opaque_rect: pic_coverage_rect,
                        spanning_opaque_color: None,
                        kind: None,
                        backdrop_rect: PictureRect::zero(),
                    });
                }
            }
            PrimitiveKind::ConicGradient { data_handle, .. } => {
                let gradient_data = &data_stores.conic_grad[data_handle];
                if gradient_data.stops_opacity.is_opaque
                    && gradient_data.tile_spacing == LayoutSize::zero()
                {
                    backdrop_candidate = Some(BackdropInfo {
                        opaque_rect: pic_coverage_rect,
                        spanning_opaque_color: None,
                        kind: None,
                        backdrop_rect: PictureRect::zero(),
                    });
                }
            }
            PrimitiveKind::RadialGradient { data_handle, .. } => {
                let gradient_data = &data_stores.radial_grad[data_handle];
                if gradient_data.stops_opacity.is_opaque
                    && gradient_data.tile_spacing == LayoutSize::zero()
                {
                    backdrop_candidate = Some(BackdropInfo {
                        opaque_rect: pic_coverage_rect,
                        spanning_opaque_color: None,
                        kind: None,
                        backdrop_rect: PictureRect::zero(),
                    });
                }
            }
            PrimitiveKind::BackdropCapture { .. } => {}
            PrimitiveKind::BackdropRender { pic_index, .. } => {
                // If the area that the backdrop covers in the space of the surface it draws on
                // is empty, skip any backdrop-filter chain processing. This is not just a performance win,
                // it also ensures that we don't do a deferred dirty test that invalidates a tile
                // even if the tile isn't actually dirty, which can cause panics later in the
                // WR pipeline.
                if !pic_coverage_rect.is_empty() {
                    // Mark that we need the backdrop-filter chain this render depends on so
                    // that we don't skip it during the prepare pass
                    scratch.frame.required_backdrop_chains.insert(pic_index);

                    // A backdrop drawn by its own primitives depends on everything
                    // in the region its filters sample, not just what's under the
                    // element. Those primitives are recorded in their own tiles,
                    // so only this test can make a change to them redraw the
                    // backdrop. Invalidating that whole region also keeps any
                    // surface behind the element from being clipped to a part of
                    // it. Only a tiled surface is drawn within dirty rects, and the
                    // region is in the backdrop root's space, so this is only done
                    // when the root is this slice.
                    let collected_region = scratch.frame.backdrop_captures.for_chain(pic_index)
                        .filter(|capture| capture.root_surface_index == self.surface_index)
                        .map(|capture| capture.region);

                    let (dirty_test_rect, dirty_test_p0, dirty_test_p1) = match collected_region {
                        Some(region) => {
                            let rect = region.union(&pic_coverage_rect);
                            let (p0, p1) = self.get_tile_coords_for_rect(&rect);
                            (rect, p0, p1)
                        }
                        None => (pic_coverage_rect, p0, p1),
                    };

                    // For backdrop-filter, we need to check if any of the dirty rects
                    // in tiles that are affected by the filter primitive are dirty.
                    self.deferred_dirty_tests.push(DeferredDirtyTest {
                        tile_rect: TileRect::new(dirty_test_p0, dirty_test_p1),
                        prim_rect: dirty_test_rect,
                    });
                }
            }
            PrimitiveKind::LineDecoration { .. } |
            PrimitiveKind::NormalBorder { .. } |
            PrimitiveKind::BoxShadow { .. } => {
                // These don't contribute dependencies
            }
            PrimitiveKind::TextRun { .. } => {
                // A text run under an animated transform is rasterized in local
                // space (see TextRun::get_raster_space_for_prim, bug
                // 2053638). Record that as a dependency so the tile invalidates
                // when the animation ends and the text returns to the crisp
                // device path - the raster-space flip alone changes neither
                // prim_uid nor the vert corners, so nothing else would catch it
                // (bug 2056306).
                prim_info.raster_space_animating = frame_context.spatial_tree
                    .get_spatial_node(prim_spatial_node_index)
                    .is_ancestor_or_self_animating;
            }
        };

        // Calculate the screen rect in local space. When we calculate backdrops, we
        // care only that they cover the visible rect (based off the local clip), and
        // don't have any overlapping prims in the visible rect.
        let visible_local_clip_rect = self.local_clip_rect.intersection(&self.screen_rect_in_pic_space).unwrap_or_default();
        if pic_coverage_rect.intersects(&visible_local_clip_rect) {
            self.found_prims_after_backdrop = true;
        }

        // If this primitive considers itself a backdrop candidate, apply further
        // checks to see if it matches all conditions to be a backdrop.
        let mut vis_flags = PrimitiveVisibilityFlags::empty();
        let sub_slice = &mut self.sub_slices[sub_slice_index];
        if let Some(mut backdrop_candidate) = backdrop_candidate {
            // Update whether the surface that this primitive exists on
            // can be considered opaque. Any backdrop kind other than
            // a clear primitive (e.g. color, gradient, image) can be
            // considered.
            match backdrop_candidate.kind {
                Some(BackdropKind::Color { .. }) | None => {
                    let surface = &mut surfaces[prim_surface_index.0];

                    let is_same_coord_system = frame_context.spatial_tree.is_matching_coord_system(
                        prim_spatial_node_index,
                        surface.surface_spatial_node_index,
                    );

                    // To be an opaque backdrop, it must:
                    // - Be the same coordinate system (axis-aligned)
                    // - Have no clip mask
                    // - Have a rect that covers the surface local rect
                    if is_same_coord_system &&
                       !prim_clip_chain.needs_mask &&
                       prim_clip_chain.pic_coverage_rect.contains_box(&surface.unclipped_local_rect)
                    {
                        // Note that we use `prim_clip_chain.pic_clip_rect` here rather
                        // than `backdrop_candidate.opaque_rect`. The former is in the
                        // local space of the surface, the latter is in the local space
                        // of the top level tile-cache.
                        surface.is_opaque = true;
                    }
                }
            }

            // Check a number of conditions to see if we can consider this
            // primitive as an opaque backdrop rect. Several of these are conservative
            // checks and could be relaxed in future. However, these checks
            // are quick and capture the common cases of background rects and images.
            // Specifically, we currently require:
            //  - The primitive is on the main picture cache surface.
            //  - Same coord system as picture cache (ensures rects are axis-aligned).
            //  - No clip masks exist.
            let same_coord_system = frame_context.spatial_tree.is_matching_coord_system(
                prim_spatial_node_index,
                self.spatial_node_index,
            );

            let is_suitable_backdrop = same_coord_system && on_picture_surface;

            if sub_slice_index == 0 &&
               is_suitable_backdrop &&
               sub_slice.compositor_surfaces.is_empty() {

                // If the backdrop candidate has a clip-mask, try to extract an opaque inner
                // rect that is safe to use for subpixel rendering
                if prim_clip_chain.needs_mask {
                    backdrop_candidate.opaque_rect = clip_store
                        .get_inner_rect_for_clip_chain(
                            prim_clip_chain,
                            &data_stores.clip,
                            frame_context.spatial_tree,
                        )
                        .unwrap_or(PictureRect::zero());
                }

                // We set the backdrop opaque_rect here, indicating the coverage area, which
                // is useful for calculate_subpixel_mode. We will only set the backdrop kind
                // if it covers the visible rect.
                if backdrop_candidate.opaque_rect.contains_box(&self.backdrop.opaque_rect) {
                    self.backdrop.opaque_rect = backdrop_candidate.opaque_rect;
                }

                if let Some(kind) = backdrop_candidate.kind {
                    if backdrop_candidate.opaque_rect.contains_box(&visible_local_clip_rect) {
                        self.found_prims_after_backdrop = false;
                        self.backdrop.kind = Some(kind);
                        self.backdrop.backdrop_rect = backdrop_candidate.opaque_rect;

                        // If we have a color backdrop that spans the entire local rect, mark
                        // the visibility flags of the primitive so it is skipped during batching
                        // (and also clears any previous primitives). Additionally, update our
                        // background color to match the backdrop color, which will ensure that
                        // our tiles are cleared to this color.
                        let BackdropKind::Color { color } = kind;
                        if backdrop_candidate.opaque_rect.contains_box(&self.local_rect) {
                            vis_flags |= PrimitiveVisibilityFlags::IS_BACKDROP;
                            self.backdrop.spanning_opaque_color = Some(color);
                        }
                    }
                }
            }
        }

        // coverage_rect is the visible portion of the primitive in local space.
        // Used for coverage_corners: detects when clipping changes the visible area
        // without over-invalidating when the clip changes outside the prim extent.
        let coverage_rect = local_prim_rect
            .intersection(&prim_clip_chain.local_clip_rect)
            .unwrap_or_default();

        // Compute raster-space corners once, outside the tile loop.
        // Transform + unquantized results land in corners_cache scratch (amortised alloc).
        // The per-prim spatial-node transform is cached across consecutive same-node prims.
        self.corners_cache.clear_scratch();
        prim_info.prim_scratch = self.corners_cache.compute_to_scratch(
            local_prim_rect,
            prim_spatial_node_index,
            self.spatial_node_index,
            self.local_to_raster,
            frame_context.spatial_tree,
        );
        prim_info.cov_scratch = self.corners_cache.compute_to_scratch(
            coverage_rect,
            prim_spatial_node_index,
            self.spatial_node_index,
            self.local_to_raster,
            frame_context.spatial_tree,
        );

        // Compute scratch ranges for clips once, outside the tile loop.
        // Actual quantization into per-tile vert_data happens inside add_prim_dependency.
        for clip_instance in clip_instances {
            let clip = &data_stores.clip[clip_instance.handle];
            let clip_local_rect = match clip.item.kind {
                ClipItemKind::Rectangle { .. }
                | ClipItemKind::RoundedRectangle { .. }
                | ClipItemKind::Border { .. }
                | ClipItemKind::Image { .. }
                | ClipItemKind::Path { .. } => Some(clip_instance.clip_rect),
            };
            let clip_scratch = match clip_local_rect {
                Some(rect) => self.corners_cache.compute_to_scratch(
                    rect,
                    clip_instance.spatial_node_index,
                    self.spatial_node_index,
                    self.local_to_raster,
                    frame_context.spatial_tree,
                ),
                None => VertRange::INVALID,
            };
            prim_info.clips.push((clip_instance.handle.uid(), clip_scratch));
        }

        // For unclamped primitives, push prim + coverage into curr_verts once.
        // All tiles share the same VertRange.
        //
        // For clamped primitives (Rectangle), push per-tile clamped corners into
        // curr_verts inside the tile loop. The VertRange is tile-specific but still
        // indexes into the same single buffer.
        //
        // clamp_to_tile = true  (coverage-only, currently Rectangle):
        //   A primitive growing/shrinking while still covering the tile does not
        //   change the tile's visual output — same coverage, same uniform color.
        //   Clamping the corners to tile bounds means such a resize compares
        //   equal and avoids a spurious invalidation.
        //
        //   NOTE: this optimisation does not yet fire in practice. prim_uid is
        //   the full intern uid, which includes prim_rect in the key; if the
        //   Rectangle's bounds change the uid changes and the prim_uid check in
        //   compare_prim invalidates the tile before the clamped-corners check
        //   is ever reached. The clamp_to_tile path is correct and ready; it
        //   will become effective once prim_uid is derived from a true
        //   content-only key (excluding prim_rect).
        //
        // clamp_to_tile = false (UV-mapped):
        //   The pixels sampled from the primitive depend on UV coordinates
        //   (tile_pos - prim_min) / prim_size. Any position or size change
        //   shifts the UV mapping even if the tile stays fully covered.

        // For each affected tile, record the primitive dependencies.
        for y in p0.y .. p1.y {
            for x in p0.x .. p1.x {
                // TODO(gw): Convert to 2d array temporarily to avoid hash lookups per-tile?
                let key = TileOffset::new(x, y);
                let tile = sub_slice.tiles.get_mut(&key).expect("bug: no tile");

                tile.add_prim_dependency(
                    &prim_info,
                    &self.corners_cache,
                    prim_clamp_to_tile,
                );
            }
        }

        DrawState::Visible {
            vis_flags,
            sub_slice_index: SubSliceIndex::new(sub_slice_index),
        }
    }

    /// Print debug information about this picture cache to a tree printer.
    pub fn print(&self) {
        // TODO(gw): This initial implementation is very basic - just printing
        //           the picture cache state to stdout. In future, we can
        //           make this dump each frame to a file, and produce a report
        //           stating which frames had invalidations. This will allow
        //           diff'ing the invalidation states in a visual tool.
        let mut pt = PrintTree::new("Picture Cache");

        pt.new_level(format!("Slice {:?}", self.slice));

        pt.add_item(format!("background_color: {:?}", self.background_color));

        for (sub_slice_index, sub_slice) in self.sub_slices.iter().enumerate() {
            pt.new_level(format!("SubSlice {:?}", sub_slice_index));

            for y in self.tile_bounds_p0.y .. self.tile_bounds_p1.y {
                for x in self.tile_bounds_p0.x .. self.tile_bounds_p1.x {
                    let key = TileOffset::new(x, y);
                    let tile = &sub_slice.tiles[&key];
                    tile.print(&mut pt);
                }
            }

            pt.end_level();
        }

        pt.end_level();
    }

    fn calculate_subpixel_mode(&self) -> SubpixelMode {
        // We can only consider the full opaque cases if there's no underlays
        if self.underlays.is_empty() {
            let has_opaque_bg_color = self.background_color.map_or(false, |c| c.a >= 1.0);

            // If the overall tile cache is known opaque, subpixel AA is allowed everywhere
            if has_opaque_bg_color {
                return SubpixelMode::Allow;
            }

            // If the opaque backdrop rect covers the entire tile cache surface,
            // we can allow subpixel AA anywhere, skipping the per-text-run tests
            // later on during primitive preparation.
            // Use the intersection with local_clip_rect to only consider the visible
            // portion - content extending beyond the clip doesn't affect subpixel AA.
            let clipped_local_rect = self.local_rect
                .intersection(&self.local_clip_rect)
                .unwrap_or(PictureRect::zero());
            if self.backdrop.opaque_rect.contains_box(&clipped_local_rect) {
                return SubpixelMode::Allow;
            }
        }

        // If we didn't find any valid opaque backdrop, no subpixel AA allowed
        if self.backdrop.opaque_rect.is_empty() {
            return SubpixelMode::Deny;
        }

        // Calculate a prohibited rect where we won't allow subpixel AA.
        // TODO(gw): This is conservative - it will disallow subpixel AA if there
        // are two underlay surfaces with text placed in between them. That's
        // probably unlikely to be an issue in practice, but maybe we should support
        // an array of prohibted rects?
        let prohibited_rect = self
            .underlays
            .iter()
            .fold(
                PictureRect::zero(),
                |acc, underlay| {
                    acc.union(&underlay.local_rect)
                }
            );

        // If none of the simple cases above match, we need test where we can support subpixel AA.
        // TODO(gw): In future, it may make sense to have > 1 inclusion rect,
        //           but this handles the common cases.
        // TODO(gw): If a text run gets animated such that it's moving in a way that is
        //           sometimes intersecting with the video rect, this can result in subpixel
        //           AA flicking on/off for that text run. It's probably very rare, but
        //           something we should handle in future.
        SubpixelMode::Conditional {
            allowed_rect: self.backdrop.opaque_rect,
            prohibited_rect,
        }
    }

    /// Apply any updates after prim dependency updates. This applies
    /// any late tile invalidations, and sets up the dirty rect and
    /// set of tile blits.
    pub fn post_update(
        &mut self,
        frame_context: &FrameVisibilityContext,
        prim_instances: &mut [PrimitiveInstance],
        composite_state: &mut CompositeState,
        resource_cache: &mut ResourceCache,
        scratch: &mut PrimitiveScratchBuffer,
    ) {
        assert!(self.current_surface_traversal_depth == 0);

        self.yuv_surface_stability
            .retain(|_, stability| stability.seen_this_frame);
        self.dirty_region.reset(self.visibility_node_index, self.spatial_node_index);
        self.subpixel_mode = self.calculate_subpixel_mode();

        self.transform_index = composite_state.register_transform(
            self.local_to_raster,
            // TODO(gw): Once we support scaling of picture cache tiles during compositing,
            //           that transform gets plugged in here!
            self.raster_to_device,
        );

        let map_pic_to_root = SpaceMapper::new_with_target(
            frame_context.root_spatial_node_index,
            self.spatial_node_index,
            frame_context.global_screen_device_rect,
            frame_context.spatial_tree,
        );

        // A simple GC of the native external surface cache, to remove and free any
        // surfaces that were not referenced during the update_prim_dependencies pass.
        self.external_native_surface_cache.retain(|_, surface| {
            if !surface.used_this_frame {
                // If we removed an external surface, we need to mark the dirty rects as
                // invalid so a full composite occurs on the next frame.
                composite_state.dirty_rects_are_valid = false;

                resource_cache.destroy_compositor_surface(surface.native_surface_id);
            }

            surface.used_this_frame
        });

        if !self.underlays.is_empty() && (!self.deferred_dirty_tests.is_empty() || !self.mix_blend_pic_rects.is_empty()) {
            let is_yuv_8bit = |desc: &ExternalSurfaceDescriptor| {
                matches!(
                    desc.dependency,
                    ExternalSurfaceDependency::Yuv {
                        channel_bit_depth: 8,
                        ..
                    }
                )
            };

            let intersects_with_dirty_tests = |desc: &ExternalSurfaceDescriptor| {
                self.deferred_dirty_tests
                    .iter()
                    .any(|dirty_test| dirty_test.prim_rect.intersects(&desc.local_rect))
            };

            let intersects_with_mix_blend = |desc: &ExternalSurfaceDescriptor| {
                self.mix_blend_pic_rects
                    .iter()
                    .any(|rect| rect.intersects(&desc.local_rect))
            };

            // Cancel underlay if underlay intersects with backdrop filter or mix-blend-mode and bit depth is 8 bits
            // XXX WebRender does not support full HDR yet. HDR requires external composite to show correct colors.
            let (underlays, cancel_underlays): (Vec<_>, Vec<_>) =
                self.underlays
                    .iter()
                    .partition(|desc| {
                        !is_yuv_8bit(desc) ||
                        (!intersects_with_dirty_tests(desc) && !intersects_with_mix_blend(desc))
                    });

            if !cancel_underlays.is_empty() {
                for desc in cancel_underlays {
                    // Change underlay to blit.
                    let draw = scratch.frame.draw_mut(desc.draw_index);
                    debug_assert!(matches!(
                        prim_instances[draw.prim_instance_index.0 as usize].kind,
                        PrimitiveKind::YuvImage { .. }
                    ));
                    draw.compositor_surface_kind = CompositorSurfaceKind::Blit;

                    let (p0, p1) = self.get_tile_coords_for_rect(&desc.local_rect);

                    for sub_slice in &mut self.sub_slices {
                        for y in p0.y .. p1.y {
                            for x in p0.x .. p1.x {
                                let key = TileOffset::new(x, y);

                                let tile = sub_slice
                                    .tiles
                                    .get_mut(&key)
                                    .expect("bug: no tile for cancelled underlay");

                                tile.invalidate(
                                    Some(desc.local_rect),
                                    InvalidationReason::CancelUnderlay,
                                );

                            }
                        }
                    }
                }

                let mut underlays: Vec<ExternalSurfaceDescriptor> = underlays
                    .iter()
                    .cloned()
                    .cloned()
                    .collect();

                mem::swap(&mut self.underlays, &mut underlays);
            }
        }

        let pic_to_root_mapper = SpaceMapper::new_with_target(
            frame_context.root_spatial_node_index,
            self.spatial_node_index,
            frame_context.global_screen_device_rect,
            frame_context.spatial_tree,
        );

        let ctx = TileUpdateDirtyContext {
            pic_to_device_mapper: pic_to_root_mapper,
            opacity_bindings: &self.opacity_bindings,
            color_bindings: &self.color_bindings,
            local_rect: self.local_rect,
            invalidate_all: self.invalidate_all_tiles,
        };

        let mut state = TileUpdateDirtyState {
            resource_cache,
            composite_state,
            compare_cache: &mut self.compare_cache,
        };

        // Step through each tile and invalidate if the dependencies have changed. Determine
        // the current opacity setting and whether it's changed.
        for sub_slice in &mut self.sub_slices {
            for tile in sub_slice.tiles.values_mut() {
                tile.update_dirty_and_valid_rects(&ctx, &mut state, frame_context);
            }
        }

        // Process any deferred dirty checks. The primitive being checked
        // depends on content in every sub-slice, so a change in any of them
        // invalidates the affected tiles of all of them.
        for dirty_test in self.deferred_dirty_tests.drain(..) {
            // Calculate the total dirty rect from all tiles that this primitive affects
            let mut total_dirty_rect = PictureRect::zero();

            for sub_slice in &self.sub_slices {
                for y in dirty_test.tile_rect.min.y .. dirty_test.tile_rect.max.y {
                    for x in dirty_test.tile_rect.min.x .. dirty_test.tile_rect.max.x {
                        let key = TileOffset::new(x, y);
                        let tile = sub_slice.tiles.get(&key).expect("bug: no tile");
                        // A tile that isn't drawn, such as one outside the clip,
                        // keeps its dirty rect from an earlier frame, which was
                        // already tested then. Only changes this frame count.
                        if tile.cached_surface.invalidation_reason.is_some() {
                            total_dirty_rect = total_dirty_rect.union(&tile.cached_surface.local_dirty_rect);
                        }
                    }
                }
            }

            // If that dirty rect intersects with the local rect of the primitive
            // being checked, invalidate that region in all of the affected tiles.
            // TODO(gw): This is somewhat conservative, we could be more clever
            //           here and avoid invalidating every tile when this changes.
            //           We could also store the dirty rect only when the prim
            //           is encountered, so that we don't invalidate if something
            //           *after* the query in the rendering order affects invalidation.
            if total_dirty_rect.intersects(&dirty_test.prim_rect) {
                for sub_slice in &mut self.sub_slices {
                    for y in dirty_test.tile_rect.min.y .. dirty_test.tile_rect.max.y {
                        for x in dirty_test.tile_rect.min.x .. dirty_test.tile_rect.max.x {
                            let key = TileOffset::new(x, y);
                            let tile = sub_slice.tiles.get_mut(&key).expect("bug: no tile");
                            tile.invalidate(
                                Some(dirty_test.prim_rect),
                                InvalidationReason::SurfaceContentChanged,
                            );
                        }
                    }
                }
            }
        }

        let mut ctx = TilePostUpdateContext {
            local_clip_rect: self.local_clip_rect,
            backdrop: None,
            current_tile_size: self.current_tile_size,
            z_id: ZBufferId::invalid(),
            underlays: &self.underlays,
        };

        let mut state = TilePostUpdateState {
            resource_cache,
            composite_state,
        };

        for (i, sub_slice) in self.sub_slices.iter_mut().enumerate().rev() {
            // The backdrop is only relevant for the first sub-slice
            if i == 0 {
                ctx.backdrop = Some(self.backdrop);
            }

            for compositor_surface in sub_slice.compositor_surfaces.iter_mut().rev() {
                compositor_surface.descriptor.z_id = state.composite_state.z_generator.next();
            }

            ctx.z_id = state.composite_state.z_generator.next();

            for tile in sub_slice.tiles.values_mut() {
                tile.post_update(&ctx, &mut state, frame_context);
            }
        }

        // Assign z-order for each underlay
        for underlay in self.underlays.iter_mut().rev() {
            underlay.z_id = state.composite_state.z_generator.next();
        }

        // Register any opaque external compositor surfaces as potential occluders. This
        // is especially useful when viewing video in full-screen mode, as it is
        // able to occlude every background tile (avoiding allocation, rasterizion
        // and compositing).

        // Register any underlays as occluders where possible
        for underlay in &self.underlays {
            if let Some(occluder_rect) = underlay.get_occluder_rect(
                &self.local_clip_rect,
                &map_pic_to_root,
            ) {
                composite_state.register_occluder(
                    underlay.z_id,
                    occluder_rect,
                    self.compositor_clip,
                );
            }
        }

        for sub_slice in &self.sub_slices {
            for compositor_surface in &sub_slice.compositor_surfaces {
                if compositor_surface.is_opaque {
                    if let Some(occluder_rect) = compositor_surface.descriptor.get_occluder_rect(
                        &self.local_clip_rect,
                        &map_pic_to_root,
                    ) {
                        composite_state.register_occluder(
                            compositor_surface.descriptor.z_id,
                            occluder_rect,
                            self.compositor_clip,
                        );
                    }
                }
            }
        }

        // Register the opaque region of this tile cache as an occluder, which
        // is used later in the frame to occlude other tiles.
        if !self.backdrop.opaque_rect.is_empty() {
            let z_id_backdrop = composite_state.z_generator.next();

            let backdrop_rect = self.backdrop.opaque_rect
                .intersection(&self.local_rect)
                .and_then(|r| {
                    r.intersection(&self.local_clip_rect)
                });

            if let Some(backdrop_rect) = backdrop_rect {
                let device_backdrop_rect = map_pic_to_root
                    .map(&backdrop_rect)
                    .expect("bug: unable to map backdrop to device space");

                // Since we register the entire backdrop rect, use the opaque z-id for the
                // picture cache slice.
                composite_state.register_occluder(
                    z_id_backdrop,
                    device_backdrop_rect,
                    self.compositor_clip,
                );
            }
        }
    }
}


/// A SubSlice represents a potentially overlapping set of tiles within a picture cache. Most
/// picture cache instances will have only a single sub-slice. The exception to this is when
/// a picture cache has compositor surfaces, in which case sub slices are used to interleave
/// content under or order the compositor surface(s).
pub struct SubSlice {
    /// Hash of tiles present in this picture.
    pub tiles: FastHashMap<TileOffset, Box<Tile>>,
    /// The allocated compositor surfaces for this picture cache. May be None if
    /// not using native compositor, or if the surface was destroyed and needs
    /// to be reallocated next time this surface contains valid tiles.
    pub native_surface: Option<NativeSurface>,
    /// List of compositor surfaces that have been promoted from primitives
    /// in this tile cache.
    pub compositor_surfaces: Vec<CompositorSurface>,
    /// List of visible tiles to be composited for this subslice
    pub composite_tiles: Vec<CompositeTile>,
    /// Compositor descriptors of visible, opaque tiles (used by composite_state.push_surface)
    pub opaque_tile_descriptors: Vec<CompositeTileDescriptor>,
    /// Compositor descriptors of visible, alpha tiles (used by composite_state.push_surface)
    pub alpha_tile_descriptors: Vec<CompositeTileDescriptor>,
}

impl SubSlice {
    /// Construct a new sub-slice
    fn new() -> Self {
        SubSlice {
            tiles: FastHashMap::default(),
            native_surface: None,
            compositor_surfaces: Vec::new(),
            composite_tiles: Vec::new(),
            opaque_tile_descriptors: Vec::new(),
            alpha_tile_descriptors: Vec::new(),
        }
    }

    /// Reset the list of compositor surfaces that follow this sub-slice.
    /// Built per-frame, since APZ may change whether an image is suitable to be a compositor surface.
    fn reset(&mut self) {
        self.compositor_surfaces.clear();
        self.composite_tiles.clear();
        self.opaque_tile_descriptors.clear();
        self.alpha_tile_descriptors.clear();
    }

    /// Resize the tile grid to match a new tile bounds
    fn resize(&mut self, new_tile_rect: TileRect) -> FastHashMap<TileOffset, Box<Tile>> {
        let mut old_tiles = mem::replace(&mut self.tiles, FastHashMap::default());
        self.tiles.reserve(new_tile_rect.area() as usize);

        for y in new_tile_rect.min.y .. new_tile_rect.max.y {
            for x in new_tile_rect.min.x .. new_tile_rect.max.x {
                let key = TileOffset::new(x, y);
                let tile = old_tiles
                    .remove(&key)
                    .unwrap_or_else(|| {
                        Box::new(Tile::new(key))
                    });
                self.tiles.insert(key, tile);
            }
        }

        old_tiles
    }
}

#[derive(Clone, Copy, Debug)]
enum SurfacePromotionFailure {
    ImageWaitingOnYuvImage,
    YuvImageNotStable,
    NotPremultipliedAlpha,
    OverlaySurfaceLimit,
    OverlayNeedsMask,
    UnderlayAlphaBackdrop,
    UnderlaySurfaceLimit,
    UnderlayIntersectsOverlay,
    UnderlayLowQualityZoom,
    NotRootTileCache,
    ComplexTransform,
    SliceAtomic,
    UnderBackdropFilter,
    SizeTooLarge,
}

impl Display for SurfacePromotionFailure {
    fn fmt(&self, f: &mut Formatter) -> Result<(), Error> {
        write!(
            f,
            "{}",
            match *self {
                SurfacePromotionFailure::ImageWaitingOnYuvImage => "Image prim waiting for all YuvImage prims to be considered for promotion",
                SurfacePromotionFailure::YuvImageNotStable => "YuvImage has not remained spatially stable long enough for surface promotion",
                SurfacePromotionFailure::NotPremultipliedAlpha => "does not use premultiplied alpha",
                SurfacePromotionFailure::OverlaySurfaceLimit => "hit the overlay surface limit",
                SurfacePromotionFailure::OverlayNeedsMask => "overlay not allowed for prim with mask",
                SurfacePromotionFailure::UnderlayAlphaBackdrop => "underlay requires an opaque backdrop",
                SurfacePromotionFailure::UnderlaySurfaceLimit => "hit the underlay surface limit",
                SurfacePromotionFailure::UnderlayIntersectsOverlay => "underlay intersects already-promoted overlay",
                SurfacePromotionFailure::UnderlayLowQualityZoom => "underlay not allowed during low-quality pinch zoom",
                SurfacePromotionFailure::NotRootTileCache => "is not on a root tile cache",
                SurfacePromotionFailure::ComplexTransform => "has a complex transform",
                SurfacePromotionFailure::SliceAtomic => "slice is atomic",
                SurfacePromotionFailure::UnderBackdropFilter => "sampled by a backdrop-filter",
                SurfacePromotionFailure::SizeTooLarge => "surface is too large for compositor",
            }.to_owned()
        )
    }
}

// Immutable context passed to picture cache tiles during pre_update
struct TilePreUpdateContext {
    /// Maps from picture cache coords -> device space coords.
    pic_to_device_mapper: SpaceMapper<PicturePixel, DevicePixel>,

    /// The optional background color of the picture cache instance
    background_color: Option<ColorF>,

    /// The visible part of the screen in device coords.
    global_screen_device_rect: DeviceRect,

    /// Current size of tiles in picture units.
    tile_size: PictureSize,

    /// The current frame id for this picture cache
    frame_id: FrameId,

    /// Maps picture-space coords to raster space, for caching per-tile raster rects.
    local_to_raster: ScaleOffset,
}

// Immutable context passed to picture cache tiles during post_update
struct TilePostUpdateContext<'a> {
    /// The local clip rect (in picture space) of the entire picture cache
    local_clip_rect: PictureRect,

    /// The calculated backdrop information for this cache instance.
    backdrop: Option<BackdropInfo>,

    /// Current size in device pixels of tiles for this cache
    current_tile_size: DeviceIntSize,

    /// Pre-allocated z-id to assign to tiles during post_update.
    z_id: ZBufferId,

    /// The list of compositor underlays for this picture cache
    underlays: &'a [ExternalSurfaceDescriptor],
}

// Mutable state passed to picture cache tiles during post_update
struct TilePostUpdateState<'a> {
    /// Allow access to the texture cache for requesting tiles
    resource_cache: &'a mut ResourceCache,

    /// Current configuration and setup for compositing all the picture cache tiles in renderer.
    composite_state: &'a mut CompositeState,
}

#[test]
fn test_max_surface_size_for_screen() {
    const DEVICE_LIMIT: i32 = 16384;
    let for_screen = |w, h| max_surface_size_for_screen(DeviceIntSize::new(w, h), DEVICE_LIMIT);

    // Screens at or below the floor leave the limit exactly where it was, so
    // the common case sees no change at all. 4k at 1x is included: 3840 rounds
    // up to 4096, which is already the floor.
    assert_eq!(for_screen(1065, 665), MAX_SURFACE_SIZE);
    assert_eq!(for_screen(1920, 1080), MAX_SURFACE_SIZE);
    assert_eq!(for_screen(3840, 2160), MAX_SURFACE_SIZE);

    // Rounding up to a power of two keeps the limit stable across a resize, so
    // surfaces sitting at it are not reallocated on every pixel of a drag.
    assert_eq!(for_screen(1265, 665), for_screen(1268, 666));
    assert_eq!(for_screen(4865, 765), for_screen(5865, 1665));

    // Above the floor the limit grows to cover the screen, including a HiDPI
    // screen whose device size already has the backing scale applied.
    assert_eq!(for_screen(4865, 765), 8192);
    assert_eq!(for_screen(5865, 1665), 8192);
    assert_eq!(for_screen(6016, 3384), 8192);

    // The longest edge decides, in either orientation.
    assert_eq!(for_screen(1080, 5000), 8192);

    // Never above what the device can allocate, even when that is below the
    // floor.
    assert_eq!(max_surface_size_for_screen(DeviceIntSize::new(6016, 3384), 4096), 4096);
    assert_eq!(max_surface_size_for_screen(DeviceIntSize::new(1920, 1080), 2048), 2048);

    // Degenerate sizes fall back to the floor rather than underflowing.
    assert_eq!(for_screen(0, 0), MAX_SURFACE_SIZE);
    assert_eq!(for_screen(-1, -1), MAX_SURFACE_SIZE);
}

/// Whether an invalid tile can be redrawn in part, rather than in full.
pub fn supports_dirty_rects(
    compositor_kind: &CompositorKind,
    gpu_supports_render_target_partial_update: bool,
) -> bool {
    match compositor_kind {
        CompositorKind::Draw { .. } | CompositorKind::Layer { .. } => gpu_supports_render_target_partial_update,
        CompositorKind::Native { capabilities, .. } => capabilities.max_update_rects > 0,
    }
}

/// Find the parts of lower slices that each backdrop-filter in a slice samples,
/// and redraw the backdrop when what it reads from them changed. Runs once every
/// tile cache's dirty rects are known, before prepare.
pub fn update_cross_slice_backdrops(
    slice_ids: &[SliceId],
    tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
    captures: &mut BackdropCaptures,
    required_backdrop_chains: &FastHashSet<PictureIndex>,
    supports_dirty_rects: bool,
    spatial_tree: &SpatialTree,
) {
    let root = spatial_tree.root_reference_frame_index();
    let mut pic_to_root = Vec::with_capacity(slice_ids.len());
    let mut prev_keys = Vec::with_capacity(slice_ids.len());

    for slice_id in slice_ids {
        let tile_cache = tile_caches.get_mut(slice_id).expect("bug: no tile cache");
        prev_keys.push(mem::take(&mut tile_cache.backdrop_capture_keys));

        // The same transform the slice's tiles are composited with.
        pic_to_root.push(get_relative_scale_offset(tile_cache.spatial_node_index, root, spatial_tree));
    }

    for index in 0 .. captures.len() {
        let capture_index = BackdropCaptureIndex(index as u32);
        let capture = &captures[capture_index];
        let Some(top) = capture.root_slice.and_then(|id| slice_ids.iter().position(|s| *s == id)) else {
            continue;
        };
        // A chain that isn't drawn, such as one under an invisible picture,
        // samples nothing.
        if !required_backdrop_chains.contains(&capture.chain_pic_index) {
            continue;
        }
        // The slices below took what drawn backdrop-filters sample before their
        // visibility pass, so each chain has to be found to be drawn by then.
        assert!(
            slice_ids[.. top].iter().all(|slice_id| {
                tile_caches[slice_id].backdrop_sample_position(capture_index).is_some()
            }),
            "bug: backdrop-filter chain found to be drawn after the slices below it",
        );
        let root_to_capture = pic_to_root[top].inverse();
        let mut sources = Vec::new();
        let mut key = BackdropCaptureKey { region: capture.region, sources: Vec::new() };
        let mut changed = false;

        for lower in 0 .. top {
            let tile_cache = &tile_caches[&slice_ids[lower]];
            let position = tile_cache.backdrop_sample_position(capture_index).unwrap();
            let Some(rect) = tile_cache.backdrop_samples[position].rect
                .intersection(&tile_cache.local_clip_rect)
                .and_then(|rect| rect.intersection(&tile_cache.local_rect)) else {
                continue;
            };

            changed |= tile_cache.sub_slices.iter().any(|sub_slice| {
                sub_slice.tiles.values().any(|tile| {
                    tile.is_visible &&
                        !tile.cached_surface.is_valid &&
                        tile.cached_surface.local_dirty_rect.intersects(&rect)
                })
            });
            key.sources.push(BackdropSourceKey {
                slice_id: slice_ids[lower],
                slice_to_capture: pic_to_root[lower].then(&root_to_capture),
                rect,
                compositor_clip_shapes: tile_cache.compositor_clip_shapes.clone(),
                background_color: tile_cache.background_color,
            });
            sources.push((lower, position, rect));
        }

        changed |= prev_keys[top].get(&capture.chain_pic_index).map_or(true, |prev| *prev != key);
        tile_caches.get_mut(&slice_ids[top]).unwrap()
            .backdrop_capture_keys
            .insert(capture.chain_pic_index, key);
        // Nothing below to read, now or last frame.
        if sources.is_empty() && !changed {
            continue;
        }

        let top_cache = tile_caches.get_mut(&slice_ids[top]).unwrap();
        // Tiles outside the slice's clip aren't drawn, and would keep any
        // invalidation until they are.
        let Some(visible_region) = capture.region.intersection(&top_cache.local_clip_rect) else {
            continue;
        };
        if changed {
            for sub_slice in &mut top_cache.sub_slices {
                for tile in sub_slice.tiles.values_mut() {
                    if tile.is_visible && tile.cached_surface.local_rect.intersects(&visible_region) {
                        // This runs after the tiles' own post-update, which makes
                        // an invalid tile dirty in full where it can't be redrawn
                        // in part.
                        let dirty_rect = supports_dirty_rects.then_some(visible_region);
                        tile.invalidate(dirty_rect, InvalidationReason::SurfaceContentChanged);
                    }
                }
            }
        }

        // The backdrop is only drawn with tiles that are, which leaves out any
        // that are dirty but outside the slice's clip or valid rect, culled or
        // occluded, or drawn as a color. The parts of the slices below drawn
        // for it are only used by those tiles.
        let drawn = top_cache.sub_slices.iter().any(|sub_slice| {
            sub_slice.tiles.values().any(|tile| {
                tile.is_visible &&
                    !tile.cached_surface.is_valid &&
                    matches!(tile.surface, Some(TileSurface::Texture { .. })) &&
                    tile.cached_surface.local_dirty_rect
                        .intersection(&tile.cached_surface.current_descriptor.local_valid_rect)
                        .is_some_and(|rect| rect.intersects(&visible_region))
            })
        });
        if !drawn {
            continue;
        }

        for &(lower, position, rect) in &sources {
            tile_caches.get_mut(&slice_ids[lower]).unwrap().backdrop_samples[position].source = Some(BackdropSource {
                rect,
                task: None,
            });
        }
        captures[capture_index].source_slices = sources.iter().map(|(lower, _, _)| slice_ids[*lower]).collect();
    }
}

/// Each backdrop-filter capture's region in the root space, with the position
/// of its backdrop root's slice in `slice_ids`.
pub fn backdrop_root_regions(
    slice_ids: &[SliceId],
    tile_caches: &FastHashMap<SliceId, Box<TileCacheInstance>>,
    captures: &BackdropCaptures,
    spatial_tree: &SpatialTree,
) -> Vec<Option<(usize, LayoutRect)>> {
    let root = spatial_tree.root_reference_frame_index();
    captures
        .iter()
        .map(|(_, capture)| {
            let slice_id = capture.root_slice?;
            let top = slice_ids.iter().position(|s| *s == slice_id)?;
            let pic_to_root = get_relative_scale_offset(tile_caches[&slice_id].spatial_node_index, root, spatial_tree);
            Some((top, pic_to_root.map_rect(&capture.region)))
        })
        .collect()
}
