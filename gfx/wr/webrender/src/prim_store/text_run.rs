/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{ColorF, FontInstanceFlags, GlyphInstance, RasterSpace};
use api::units::{LayoutToWorldTransform, DevicePixelScale};
use api::units::*;
use crate::scene_building::{IsVisible};
use glyph_rasterizer::{FontInstance, FontTransform, GlyphKey, SubpixelDirection, FONT_SIZE_LIMIT};
use crate::intern;
use crate::internal_types::LayoutPrimitiveInfo;
use crate::surface::SurfaceInfo;
use crate::prim_store::PrimitiveScratchBuffer;
use crate::prim_store::{PrimitiveStore, PrimKeyCommonData, PrimTemplateCommonData};
use crate::renderer::{GpuBufferAddress, GpuBufferBuilderF, MAX_VERTEX_TEXTURE_WIDTH};
use crate::resource_cache::ResourceCache;
use crate::util::MatrixHelpers;
use crate::prim_store::{InternablePrimitive, PrimitiveKind};
use crate::spatial_tree::{SpatialTree, SpatialNodeIndex};
use std::ops;

use super::storage;

// `GlyphInstanceAu` now lives in `webrender_api::key_types` so builder-side
// interning keys can reference it. Re-exported to keep existing references
// working.
pub use api::key_types::GlyphInstanceAu;

/// A run of glyphs, with associated font information.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, Clone, Eq, MallocSizeOf, PartialEq, Hash)]
pub struct TextRunKey {
    pub common: PrimKeyCommonData,
    pub font: FontInstance,
    /// Glyph pen positions, each relative to the *normalized* prim rect
    /// origin (`prim_info.rect.min`). Storing relative to the normalized
    /// origin keeps the intern key stable across pre-scroll offset changes,
    /// since the external scroll offset cancels: both the glyph position and
    /// the prim origin are normalized the same way (see `add_text`).
    pub glyphs: Vec<GlyphInstanceAu>,
    pub shadow: bool,
    pub requested_raster_space: RasterSpace,
}

impl TextRunKey {
    pub fn new(
        info: &LayoutPrimitiveInfo,
        text_run: TextRun,
    ) -> Self {
        let glyphs = text_run
            .glyphs
            .iter()
            .map(|glyph| {
                GlyphInstanceAu {
                    index: glyph.index,
                    point: glyph.point.to_au(),
                }
            })
            .collect();

        TextRunKey {
            common: info.into(),
            font: text_run.font,
            glyphs,
            shadow: text_run.shadow,
            requested_raster_space: text_run.requested_raster_space,
        }
    }
}

impl intern::InternDebug for TextRunKey {}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(MallocSizeOf)]
pub struct TextRunTemplate {
    pub common: PrimTemplateCommonData,
    pub font: FontInstance,
    /// Glyph pen positions, each relative to the normalized prim rect origin.
    /// See [`TextRunKey::glyphs`]. At frame time the normalized local glyph
    /// position is `prim_rect.min + glyph.point`; `request_resources` then
    /// transforms and device-snaps each glyph to produce the device-space
    /// offsets handed to the shader.
    pub glyphs: Vec<GlyphInstance>,
    pub shadow: bool,
    pub requested_raster_space: RasterSpace,
}

impl ops::Deref for TextRunTemplate {
    type Target = PrimTemplateCommonData;
    fn deref(&self) -> &Self::Target {
        &self.common
    }
}

impl ops::DerefMut for TextRunTemplate {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.common
    }
}

impl From<TextRunKey> for TextRunTemplate {
    fn from(item: TextRunKey) -> Self {
        let common = PrimTemplateCommonData::with_key_common(item.common);
        let glyphs = item
            .glyphs
            .iter()
            .map(|glyph| {
                GlyphInstance {
                    index: glyph.index,
                    point: LayoutPoint::from_au(glyph.point),
                }
            })
            .collect();

        TextRunTemplate {
            common,
            font: item.font,
            glyphs,
            shadow: item.shadow,
            requested_raster_space: item.requested_raster_space,
        }
    }
}

impl TextRunTemplate {
    /// Write the per-instance GPU blocks for this run: the premultiplied
    /// font color followed by the per-glyph offsets (two glyphs packed per
    /// block). The offsets are device-space in device mode and raster-space in
    /// local-raster mode (see `request_resources`). Corresponds to
    /// `fetch_glyph` / `fetch_text_run` in the shader.
    fn write_prim_gpu_blocks(
        &self,
        glyph_offsets: &[DeviceVector2D],
        gpu_buffer: &mut GpuBufferBuilderF,
    ) -> GpuBufferAddress {
        let num_blocks = (glyph_offsets.len() + 1) / 2 + 1;
        assert!(num_blocks <= MAX_VERTEX_TEXTURE_WIDTH);
        let mut writer = gpu_buffer.write_blocks(num_blocks);
        writer.push_one(ColorF::from(self.font.color).premultiplied());

        let mut gpu_block = [0.0; 4];
        for (i, src) in glyph_offsets.iter().enumerate() {
            // Two glyphs are packed per GPU block.
            if (i & 1) == 0 {
                gpu_block[0] = src.x;
                gpu_block[1] = src.y;
            } else {
                gpu_block[2] = src.x;
                gpu_block[3] = src.y;
                writer.push_one(gpu_block);
            }
        }

        // Ensure the last block is added in the case
        // of an odd number of glyphs.
        if (glyph_offsets.len() & 1) != 0 {
            writer.push_one(gpu_block);
        }

        writer.finish()
    }
}

pub type TextRunDataHandle = intern::Handle<TextRun>;

#[derive(Debug, MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct TextRun {
    pub font: FontInstance,
    /// Glyph pen positions, each relative to the normalized prim rect origin.
    /// See [`TextRunKey::glyphs`].
    pub glyphs: Vec<GlyphInstance>,
    pub shadow: bool,
    pub requested_raster_space: RasterSpace,
}

impl intern::Internable for TextRun {
    type Key = TextRunKey;
    type StoreData = TextRunTemplate;
    type InternData = ();
    const PROFILE_COUNTER: usize = crate::profiler::INTERNED_TEXT_RUNS;
}

impl InternablePrimitive for TextRun {
    // Text renders in device space; its clips must not snap (bug 2050692).
    const SNAP_CLIPS: bool = false;

    fn into_key(
        self,
        info: &LayoutPrimitiveInfo,
    ) -> TextRunKey {
        TextRunKey::new(
            info,
            self,
        )
    }

    fn make_instance_kind(
        _key: TextRunKey,
        data_handle: TextRunDataHandle,
        _prim_store: &mut PrimitiveStore,
    ) -> PrimitiveKind {
        PrimitiveKind::TextRun {
            data_handle,
        }
    }
}


impl IsVisible for TextRun {
    fn is_visible(&self) -> bool {
        self.font.color.a > 0
    }
}

/// Per-frame scratch data for a TextRun primitive. Holds the snapshot
/// of font + glyph state captured each frame in `request_resources` and
/// read by batching. Pushed once per visible TextRun per frame.
#[derive(Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
pub struct TextRunScratch {
    /// Per-frame font instance derived from the specified font + this
    /// frame's transform + raster space. Carries subpixel direction,
    /// flags, and the device-space size.
    pub used_font: FontInstance,
    /// Range of glyph keys allocated for this run this frame, indexing
    /// into PrimitiveFrameScratch.glyph_keys.
    pub glyph_keys_range: storage::Range<GlyphKey>,
    /// Normalized prim local rect for this run. `.min` is the run anchor:
    /// the shader transforms it to device space and adds the per-glyph
    /// device offsets. Stored here so batching emits the identical anchor
    /// in `PrimitiveHeader.pattern_rect` that `request_resources` used to
    /// compute those offsets.
    pub pattern_rect: LayoutRect,
    /// Per-instance GPU buffer address for the color block followed by the
    /// per-glyph offset blocks (two glyphs per block). In device mode these are
    /// glyph pen positions snapped to the device grid, relative to the
    /// transformed anchor; in local-raster mode they are absolute snapped
    /// raster-space positions. Per-instance because they depend on this frame's
    /// transform.
    pub gpu_address: GpuBufferAddress,
    /// Raster scale used when rasterizing the glyphs (1.0 in device mode; the
    /// local/zoom scale or oversize-clamp scale in local-raster mode). Passed
    /// to the shader so it can map raster space back to local.
    pub raster_scale: f32,
    /// Whether this run uses local-raster mode (see `request_resources`).
    pub local_raster: bool,
}

impl TextRunTemplate {
    /// Build a per-frame `(used_font, raster_scale)` pair for this text run.
    /// The result is fresh per frame; nothing persists on the template.
    fn compute_font_instance(
        specified_font: &FontInstance,
        surface: &SurfaceInfo,
        transform: &LayoutToWorldTransform,
        allow_subpixel: bool,
        raster_space: RasterSpace,
        has_bitmap_strikes: bool,
    ) -> (FontInstance, f32) {
        // If local raster space is specified, include that in the scale
        // of the glyphs that get rasterized.
        // TODO(gw): Once we support proper local space raster modes, this
        //           will implicitly be part of the device pixel ratio for
        //           the (cached) local space surface, and so this code
        //           will no longer be required.
        // A bitmap-strike run rasterizes with an identity glyph shape (see below),
        // so the only way the transform's scale can reach the rasterizer is through
        // the raster scale. Without it the glyph is rendered at its untransformed
        // size and the shader scales that, which on the backends that resample at
        // rasterization time (Core Text, DirectWrite) is visibly soft when scaling
        // up (bug 2064316). Only screen raster space needs this: a requested - or
        // zoom/animation derived - local raster space carries its own scale, which
        // is deliberately decoupled from the current transform.
        let raster_scale_input = if has_bitmap_strikes && raster_space == RasterSpace::Screen {
            transform
                .coplanar_scale_factors()
                .map_or(1.0, |(sx, sy)| sx.max(sy))
        } else {
            raster_space.local_scale().unwrap_or(1.0)
        }
        .max(0.001);

        let dps = surface.device_pixel_scale.0;
        let font_size = specified_font.size.to_f32_px();

        // Small floating point error can accumulate in the raster * device_pixel scale.
        // Round that to the nearest 100th of a scale factor to remove this error while
        // still allowing reasonably accurate scale factors when a pinch-zoom is stopped
        // at a fractional amount.
        let quantized_scale = (dps * raster_scale_input * 100.0).round() / 100.0;
        let mut device_font_size = font_size * quantized_scale;

        // Check there is a valid transform that doesn't exceed the font size limit.
        // Ensure the font is supposed to be rasterized in screen-space.
        // Only support transforms that can be coerced to simple 2D transforms.
        // Add texture padding to the rasterized glyph buffer when one anticipates
        // the glyph will need to be scaled when rendered.
        // Glyphs are coplanar, so the device path only needs the transform to be
        // 2D on their z=0 plane rather than 2D outright: `is_2d_on_z_plane` keeps
        // a flat `perspective` ancestor sharp there (bug 2052019) while sending
        // the transforms whose device round-trip the shader can't invert - a
        // `translateZ` under that perspective, or a 3D rotation - back to local
        // raster, instead of displacing every glyph and shaving a slice off it
        // (bug 2060342).
        // Color bitmap glyphs (Apple Color Emoji and any CBDT/sbix font with
        // embedded bitmap strikes) can't have any part of the transform baked into
        // their rasterization: the platform backends force an identity glyph shape
        // for bitmap fonts, so a rotation or skew is dropped outright and the scale
        // survives only as the scalar `RasterizedGlyph::scale`, which the device
        // path multiplies into the atlas footprint. That scalar cannot express a
        // transform scale at all, so on the device path a scaled emoji is painted
        // at its untransformed size (too small when scaling up, too big and clipped
        // when scaling down - bugs 2061411, 2062234), and a rotated one is drawn
        // axis-aligned at its transformed pen (bug 2055177). Before bug 2044211 the
        // GPU routed these by glyph format - `GlyphFormat::Bitmap`/`ColorBitmap`
        // have no `Transformed` variant, so bitmap glyphs never reached the
        // transform shader and always had the transform applied in local space.
        // `has_bitmap_strikes` restores that: the caller computes it from the real
        // font face (the EMBEDDED_BITMAPS instance flag alone is unreliable - on
        // unix it only reflects a fontconfig preference and is set even for
        // ordinary outline fonts) and it is true only for a genuine bitmap font
        // under a transform that isn't a pure translation. Route those through the
        // local-raster fallback so the shader applies the full transform to the
        // emoji image, the same way vector glyphs and the manual
        // filter/isolated-surface workaround do. A pure translation is fine on the
        // device path, where the scalar scale already lands the glyph at the right
        // size, so that - the common case - is left untouched.
        let (use_subpixel_aa, transform_glyphs, texture_padding, oversized) = if raster_space != RasterSpace::Screen ||
            !transform.is_2d_on_z_plane() || !transform.has_2d_inverse() ||
            has_bitmap_strikes
        {
            (false, false, true, device_font_size > FONT_SIZE_LIMIT)
        } else if transform.exceeds_2d_scale((FONT_SIZE_LIMIT / device_font_size) as f64) {
            (false, false, true, true)
        } else {
            (true, !transform.is_simple_2d_translation(), false, false)
        };

        let mut raster_scale = raster_scale_input;
        let font_transform = if transform_glyphs {
            // Get the font transform matrix (skew / scale) from the complete transform.
            // Fold in the device pixel scale.
            raster_scale = 1.0;
            FontTransform::from(transform)
        } else {
            if oversized {
                // Font sizes larger than the limit need to be scaled, thus can't use subpixels.
                // In this case we adjust the font size and raster space to ensure
                // we rasterize at the limit, to minimize the amount of scaling.
                raster_scale = FONT_SIZE_LIMIT / (font_size * dps);
                device_font_size = FONT_SIZE_LIMIT;
            }
            // else: keep raster_scale = raster_scale_input. We may have
            // changed from RasterSpace::Screen due to a transform with
            // perspective or without a 2D inverse, or it may have been
            // RasterSpace::Local all along.

            // Rasterize the glyph without any transform.
            FontTransform::identity()
        };

        let mut flags = specified_font.flags;
        if transform_glyphs {
            flags |= FontInstanceFlags::TRANSFORM_GLYPHS;
        }
        if texture_padding {
            flags |= FontInstanceFlags::TEXTURE_PADDING;
        }

        // Construct used font instance from the specified font instance
        let mut used_font = FontInstance {
            transform: font_transform,
            size: device_font_size.into(),
            flags,
            ..specified_font.clone()
        };

        // If using local space glyphs, we don't want subpixel AA.
        if !allow_subpixel || !use_subpixel_aa {
            used_font.disable_subpixel_aa();

            // Disable subpixel positioning for oversized glyphs to avoid
            // thrashing the glyph cache with many subpixel variations of
            // big glyph textures. A possible subpixel positioning error
            // is small relative to the maximum font size and thus should
            // not be very noticeable.
            if oversized {
                used_font.disable_subpixel_position();
            }
        }

        (used_font, raster_scale)
    }

    /// Gets the raster space to use when rendering this primitive.
    /// Usually this would be the requested raster space. However, if
    /// the primitive's spatial node or one of its ancestors is being pinch zoomed
    /// then we round it. This prevents us rasterizing glyphs for every minor
    /// change in zoom level, as that would be too expensive.
    fn get_raster_space_for_prim(
        &self,
        prim_spatial_node_index: SpatialNodeIndex,
        low_quality_pinch_zoom: bool,
        device_pixel_scale: DevicePixelScale,
        spatial_tree: &SpatialTree,
    ) -> RasterSpace {
        let prim_spatial_node = spatial_tree.get_spatial_node(prim_spatial_node_index);
        if prim_spatial_node.is_ancestor_or_self_zooming && low_quality_pinch_zoom {
            // In low-quality mode, we set the scale to be 1.0. However, the device-pixel
            // scale selected for the zoom will be taken into account in the caller to this
            // function when it's converted from local -> device pixels. Since in this mode
            // the device-pixel scale is constant during the zoom, this gives the desired
            // performance while also allowing the scale to be adjusted to a new factor at
            // the end of a pinch-zoom.
            RasterSpace::Local(1.0)
        } else if prim_spatial_node.is_ancestor_or_self_zooming
            || prim_spatial_node.is_ancestor_or_self_animating
        {
            // High-quality pinch zoom, or an animated (property-bound) transform.
            // In both cases the transform changes continuously without a new
            // display list, so rasterize the glyphs in local space and let the
            // shader apply the current transform each frame. For zoom this avoids
            // re-rasterizing glyphs for every minor scale change; for animation it
            // avoids device-snapping a per-frame-sampled transform, which makes the
            // glyphs jitter as they cross pixel boundaries (bug 637852 - the device
            // text path added in bug 2044211 otherwise misses that policy). Quantize
            // the scale up to the nearest power of 2 (capped at 8) so the glyphs
            // aren't re-rasterized as the scale sweeps through fractional values,
            // and undo the device-pixel scale since the picture cache tiles are
            // raster roots.
            let root_spatial_node_index = spatial_tree.root_reference_frame_index();
            let scale_factors = spatial_tree
                .get_relative_transform(prim_spatial_node_index, root_spatial_node_index)
                .scale_factors();

            let scale = scale_factors.0.max(scale_factors.1).min(8.0).max(1.0);
            let rounded_up = 2.0f32.powf(scale.log2().ceil());

            RasterSpace::Local(rounded_up / device_pixel_scale.0)
        } else {
            // Assume that if we have a RasterSpace::Local, it is frequently changing, in which
            // case we want to undo the device-pixel scale, as we do above.
            match self.requested_raster_space {
                RasterSpace::Local(scale) => RasterSpace::Local(scale / device_pixel_scale.0),
                RasterSpace::Screen => RasterSpace::Screen,
            }
        }
    }

    pub fn request_resources(
        &self,
        pattern_rect: LayoutRect,
        transform: &LayoutToWorldTransform,
        surface: &SurfaceInfo,
        spatial_node_index: SpatialNodeIndex,
        allow_subpixel: bool,
        low_quality_pinch_zoom: bool,
        resource_cache: &mut ResourceCache,
        gpu_buffer: &mut GpuBufferBuilderF,
        spatial_tree: &SpatialTree,
        scratch: &mut PrimitiveScratchBuffer,
    ) -> storage::Index<TextRunScratch> {
        let raster_space = self.get_raster_space_for_prim(
            spatial_node_index,
            low_quality_pinch_zoom,
            surface.device_pixel_scale,
            spatial_tree,
        );

        // Bitmap fonts need the local-raster fallback under any transform that
        // isn't a pure translation - a scale as much as a rotation or skew
        // (bugs 2055177, 2061411). Gate on the cheap transform and instance-flag
        // checks first so that untransformed text - the common case - does no
        // extra work, and only then consult the cached per-font bitmap-strike
        // info (a lock-free lookup populated when the font was added).
        let has_bitmap_strikes = !transform.is_simple_2d_translation()
            && self.font.flags.contains(FontInstanceFlags::EMBEDDED_BITMAPS)
            && resource_cache.font_has_bitmap_strikes(self.font.font_key);

        let (used_font, raster_scale) = Self::compute_font_instance(
            &self.font,
            surface,
            transform,
            allow_subpixel,
            raster_space,
            has_bitmap_strikes,
        );

        let subpx_dir = used_font.get_subpx_dir();
        let dps = surface.device_pixel_scale;

        // Two glyph-positioning modes:
        //
        // * Device mode (screen raster space, axis-aligned or 2D rotated/skewed
        //   `TRANSFORM_GLYPHS`): the glyph is rasterized at the final device
        //   scale and positioned by snapping its device position to the device
        //   grid. The per-glyph offsets handed to the shader are device-space.
        //
        // * Local-raster mode (everything `compute_font_instance` marks with
        //   `TEXTURE_PADDING` — local raster space / pinch-zoom, oversized
        //   glyphs, perspective — and any non-screen raster space): the glyph is
        //   rasterized at `raster_scale` with an identity transform and the
        //   shader scales/positions it in local space, letting `write_vertex`
        //   apply the (possibly animated/perspective) transform. Device snapping
        //   is intentionally avoided here to prevent glyphs wiggling under
        //   animation. The per-glyph offsets are absolute snapped *raster-space*
        //   positions.
        //
        // Transposed / flipped (vertical writing-mode) glyphs need no special
        // handling: the transpose/flip is baked into the glyph's rasterization
        // transform (so the bitmap, `res.offset` and uv rect are already
        // oriented) and the pen positions are laid out by the caller, so they
        // ride the device path like any other run.
        let local_raster = raster_space != RasterSpace::Screen
            || used_font.flags.contains(FontInstanceFlags::TEXTURE_PADDING);

        // Only the local-raster branch below snaps on the CPU; device mode hands
        // the exact pen to the shader, which applies the matching bias itself.
        // `Mixed` never reaches local-raster mode (that path rasterizes with an
        // identity `FontTransform`, so its subpx dir is always `Horizontal`).
        let snap_bias = match subpx_dir {
            SubpixelDirection::None => DeviceVector2D::new(0.5, 0.5),
            SubpixelDirection::Horizontal => DeviceVector2D::new(0.125, 0.5),
            SubpixelDirection::Vertical => DeviceVector2D::new(0.5, 0.125),
            SubpixelDirection::Mixed => DeviceVector2D::new(0.125, 0.125),
        };

        // World-space run anchor (device mode only).
        let anchor_world = transform.transform_point2d(pattern_rect.min);

        let mut glyph_offsets: Vec<DeviceVector2D> = Vec::new();
        let glyph_keys_range = if local_raster {
            // Local-raster mode: snap each glyph in raster space (no device
            // snap), store the absolute snapped raster position. The shader maps
            // raster space -> local (by `res.scale / (raster_scale * dps)`) and
            // `write_vertex` applies the transform.
            let glyph_raster_scale = raster_scale * dps.0;
            glyph_offsets.reserve(self.glyphs.len());

            scratch.frame.glyph_keys.extend(self.glyphs.iter().map(|src| {
                let pos = pattern_rect.min + src.point.to_vector();
                let raster_pos = DevicePoint::new(pos.x * glyph_raster_scale, pos.y * glyph_raster_scale);
                let snapped = (raster_pos + snap_bias).floor();
                glyph_offsets.push(snapped.to_vector());
                GlyphKey::new(src.index, raster_pos, subpx_dir)
            }))
        } else if let Some(anchor_world) = anchor_world {
            // Device mode.
            //
            // No run-level snap. Each glyph is placed at its exact device
            // position; the per-glyph snap + subpixel GlyphKey carry the
            // fractional part, so the glyph renders exactly where Gecko put it
            // (bug 2050692). The run has no single snapped anchor to fold into the
            // glyphs, so a run never shifts relative to its clip/box. Stability
            // under scrolling comes from the spatial tree, which already snaps
            // scroll offsets (and should_snap frame transforms) to the device grid.
            //
            // Store the *unsnapped* absolute device pen and let the shader snap
            // it. Which bias to use isn't known here: a glyph that rasterizes
            // from an embedded bitmap strike ignores the sub-pixel offset the
            // key asks for and lands on the grid, so it must round to nearest
            // rather than floor with the sub-pixel bias (bug 2056856). That is
            // only known once the glyph is rasterized, which happens after this
            // point. Snapping a value the shader receives verbatim keeps the
            // arithmetic exact - re-deriving the pen from the transform GPU-side
            // would risk landing on the wrong side of a `floor` boundary at the
            // exactly-representable fractions layout produces (x.5, x.875).
            glyph_offsets.reserve(self.glyphs.len());

            scratch.frame.glyph_keys.extend(self.glyphs.iter().map(|src| {
                // Exact glyph pen position in absolute device space.
                let glyph_world = transform
                    .transform_point2d(pattern_rect.min + src.point.to_vector())
                    .unwrap_or(anchor_world);
                let device_pen = glyph_world * dps;

                glyph_offsets.push(device_pen.to_vector());

                GlyphKey::new(src.index, device_pen, subpx_dir)
            }))
        } else {
            // Degenerate transform (no 2D inverse for the anchor): draw nothing.
            scratch.frame.glyph_keys.extend(std::iter::empty())
        };

        resource_cache.request_glyphs(
            used_font.clone(),
            &scratch.frame.glyph_keys[glyph_keys_range],
            gpu_buffer,
        );

        let gpu_address = self.write_prim_gpu_blocks(&glyph_offsets, gpu_buffer);

        scratch.frame.text_runs.push(TextRunScratch {
            used_font,
            glyph_keys_range,
            pattern_rect,
            gpu_address,
            raster_scale,
            local_raster,
        })
    }
}

/// These are linux only because FontInstancePlatformOptions varies in size by platform.
#[test]
#[cfg(target_os = "linux")]
fn test_struct_sizes() {
    use std::mem;
    // The sizes of these structures are critical for performance on a number of
    // talos stress tests. If you get a failure here on CI, there's two possibilities:
    // (a) You made a structure smaller than it currently is. Great work! Update the
    //     test expectations and move on.
    // (b) You made a structure larger. This is not necessarily a problem, but should only
    //     be done with care, and after checking if talos performance regresses badly.
    assert_eq!(mem::size_of::<TextRun>(), 80, "TextRun size changed");
    assert_eq!(mem::size_of::<TextRunTemplate>(), 80, "TextRunTemplate size changed");
    assert_eq!(mem::size_of::<TextRunKey>(), 80, "TextRunKey size changed");
}
