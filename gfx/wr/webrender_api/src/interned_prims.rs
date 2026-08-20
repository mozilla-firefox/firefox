/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Interned primitive scene descriptions.
//!
//! These are the per-primitive "scene description" structs that `webrender`
//! interns (the `T` in its `PrimKey<T>` / `Internable` machinery). Their fields
//! are all api-resident, so they live here in `webrender_api` to let
//! content-process interning in the `DisplayListBuilder` hold them; `webrender`
//! re-exports each from its former home and keeps the `Internable` /
//! `InternablePrimitive` impls and per-frame templates (the trait and templates
//! are webrender-internal). Not part of the public API surface.

use crate::serde::{Serialize, Deserialize};
use crate::{
    AlphaType, BoxShadowClipMode, ColorDepth, ColorRange, ColorU, ExtendMode, ImageKey,
    ImageRendering, LineOrientation, LineStyle, PropertyBinding, YuvColorSpace, YuvFormat,
};
use crate::key_types::{
    BorderRadiusAu, ConicGradientParams, GradientStopKey, NinePatchDescriptor,
    NormalBorderAu, PointKey, PrimKeyCommonData, RadialGradientParams, SizeKey, StretchSizeKey,
    VectorKey,
};
use crate::units::{LayoutSideOffsetsAu, TileOffset};
use app_units::Au;
use malloc_size_of::MallocSizeOf;

/// Generic interning key for the `PrimKey<T>`-based primitives: the common
/// per-prim key data plus the interned value `T`. (Bespoke keys such as the
/// gradients don't use this.) `new` takes an already-built `PrimKeyCommonData`
/// so the `LayoutPrimitiveInfo -> common` conversion stays behind in
/// `webrender`'s `into_key`.
#[derive(Debug, Clone, Eq, MallocSizeOf, PartialEq, Hash, Serialize, Deserialize)]
pub struct PrimKey<T: MallocSizeOf> {
    pub common: PrimKeyCommonData,
    pub kind: T,
}

impl<T: MallocSizeOf> PrimKey<T> {
    pub fn new(common: PrimKeyCommonData, kind: T) -> Self {
        PrimKey { common, kind }
    }
}

#[derive(Debug, Clone, Eq, MallocSizeOf, PartialEq, Hash, Serialize, Deserialize)]
pub struct RectanglePrim {
    pub color: PropertyBinding<ColorU>,
}

#[derive(Debug, Clone, MallocSizeOf, Hash, Eq, PartialEq, Serialize, Deserialize)]
pub struct BoxShadow {
    pub color: ColorU,
    pub blur_radius: Au,
    pub clip_mode: BoxShadowClipMode,
    pub shadow_radius: BorderRadiusAu,
    pub element_radius: BorderRadiusAu,
    /// `box-shadow` offset of the shadow relative to the element, in
    /// local space.
    pub box_offset: VectorKey,
    /// Signed spread radius. Positive for Outset, negative for Inset
    /// (matches the convention in `add_box_shadow`).
    pub spread_amount: Au,
}

#[derive(Debug, Clone, Eq, PartialEq, MallocSizeOf, Hash, Serialize, Deserialize)]
pub struct Image {
    pub key: ImageKey,
    pub stretch_size: StretchSizeKey,
    pub tile_spacing: SizeKey,
    pub color: ColorU,
    pub image_rendering: ImageRendering,
    pub alpha_type: AlphaType,
}

#[derive(Debug, Clone, Eq, MallocSizeOf, PartialEq, Hash, Serialize, Deserialize)]
pub struct YuvImage {
    pub color_depth: ColorDepth,
    pub yuv_key: [ImageKey; 3],
    pub format: YuvFormat,
    pub color_space: YuvColorSpace,
    pub color_range: ColorRange,
    pub image_rendering: ImageRendering,
}

#[derive(Clone, Debug, Hash, MallocSizeOf, PartialEq, Eq, Serialize, Deserialize)]
pub struct LineDecoration {
    pub style: LineStyle,
    pub orientation: LineOrientation,
    pub wavy_line_thickness: Au,
    pub color: ColorU,
}

#[derive(Debug, Clone, Eq, MallocSizeOf, PartialEq, Hash, Serialize, Deserialize)]
pub struct NormalBorderPrim {
    pub border: NormalBorderAu,
    pub widths: LayoutSideOffsetsAu,
}

/// Interned representation of an image-source nine-patch border. The interned
/// key stores the image request's parts directly (rather than a webrender
/// `ImageRequest`) so the value is api-resident; the frame-time
/// `ImageBorderData` template rebuilds the `ImageRequest`.
#[derive(Debug, Clone, Eq, MallocSizeOf, PartialEq, Hash, Serialize, Deserialize)]
pub struct ImageBorder {
    pub key: ImageKey,
    pub rendering: ImageRendering,
    pub tile: Option<TileOffset>,
    pub nine_patch: NinePatchDescriptor,
}

#[derive(Debug, Clone, Eq, PartialEq, MallocSizeOf, Hash, Serialize, Deserialize)]
pub struct BackdropCapture {
}

#[derive(Debug, Clone, Eq, PartialEq, MallocSizeOf, Hash, Serialize, Deserialize)]
pub struct BackdropRender {
}

#[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq, Hash, Serialize, Deserialize)]
pub struct LinearGradient {
    pub extend_mode: ExtendMode,
    pub start_point: PointKey,
    pub end_point: PointKey,
    /// Per-axis tile size encoded as a fraction of the prim's size. See the
    /// matching `stretch_ratio` field on `LinearGradientKey`.
    pub stretch_ratio: SizeKey,
    pub tile_spacing: SizeKey,
    pub stops: Vec<GradientStopKey>,
    pub reverse_stops: bool,
    pub nine_patch: Option<Box<NinePatchDescriptor>>,
}

#[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq, Hash, Serialize, Deserialize)]
pub struct RadialGradient {
    pub extend_mode: ExtendMode,
    pub center: PointKey,
    pub params: RadialGradientParams,
    /// Per-axis tile size encoded as a fraction of the prim's size. See the
    /// matching `stretch_ratio` field on `RadialGradientKey`.
    pub stretch_ratio: SizeKey,
    pub stops: Vec<GradientStopKey>,
    pub tile_spacing: SizeKey,
    pub nine_patch: Option<Box<NinePatchDescriptor>>,
}

#[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq, Hash, Serialize, Deserialize)]
pub struct ConicGradient {
    pub extend_mode: ExtendMode,
    pub center: PointKey,
    pub params: ConicGradientParams,
    /// Per-axis tile size encoded as a fraction of the prim's size. See the
    /// matching `stretch_ratio` field on `ConicGradientKey`.
    pub stretch_ratio: SizeKey,
    pub stops: Vec<GradientStopKey>,
    pub tile_spacing: SizeKey,
    pub nine_patch: Option<Box<NinePatchDescriptor>>,
}

// Interning keys for the gradient primitives. Unlike the `PrimKey<T>`-based
// prims, gradients use bespoke key structs. The constructors take an
// already-built `PrimKeyCommonData` so the `LayoutPrimitiveInfo -> common`
// conversion can stay behind in `webrender`'s `into_key`. The matching
// `*Template` structs and `Internable` impls also stay in `webrender`.

#[derive(Debug, Clone, Eq, PartialEq, Hash, MallocSizeOf, Serialize, Deserialize)]
pub struct LinearGradientKey {
    pub common: PrimKeyCommonData,
    pub extend_mode: ExtendMode,
    pub start_point: PointKey,
    pub end_point: PointKey,
    /// Per-axis tile size encoded as a fraction of `common.prim_size`. The
    /// runtime `stretch_size` is `stretch_ratio * common.prim_size`.
    pub stretch_ratio: SizeKey,
    pub tile_spacing: SizeKey,
    pub stops: Vec<GradientStopKey>,
    pub reverse_stops: bool,
    pub nine_patch: Option<Box<NinePatchDescriptor>>,
}

impl LinearGradientKey {
    pub fn new(common: PrimKeyCommonData, linear_grad: LinearGradient) -> Self {
        LinearGradientKey {
            common,
            extend_mode: linear_grad.extend_mode,
            start_point: linear_grad.start_point,
            end_point: linear_grad.end_point,
            stretch_ratio: linear_grad.stretch_ratio,
            tile_spacing: linear_grad.tile_spacing,
            stops: linear_grad.stops,
            reverse_stops: linear_grad.reverse_stops,
            nine_patch: linear_grad.nine_patch,
        }
    }
}

#[derive(Debug, Clone, Eq, PartialEq, Hash, MallocSizeOf, Serialize, Deserialize)]
pub struct RadialGradientKey {
    pub common: PrimKeyCommonData,
    pub extend_mode: ExtendMode,
    pub center: PointKey,
    pub params: RadialGradientParams,
    /// Per-axis tile size encoded as a fraction of `common.prim_size`. The
    /// runtime `stretch_size` is `stretch_ratio * common.prim_size`.
    pub stretch_ratio: SizeKey,
    pub stops: Vec<GradientStopKey>,
    pub tile_spacing: SizeKey,
    pub nine_patch: Option<Box<NinePatchDescriptor>>,
}

impl RadialGradientKey {
    pub fn new(common: PrimKeyCommonData, radial_grad: RadialGradient) -> Self {
        RadialGradientKey {
            common,
            extend_mode: radial_grad.extend_mode,
            center: radial_grad.center,
            params: radial_grad.params,
            stretch_ratio: radial_grad.stretch_ratio,
            stops: radial_grad.stops,
            tile_spacing: radial_grad.tile_spacing,
            nine_patch: radial_grad.nine_patch,
        }
    }
}

#[derive(Debug, Clone, Eq, PartialEq, Hash, MallocSizeOf, Serialize, Deserialize)]
pub struct ConicGradientKey {
    pub common: PrimKeyCommonData,
    pub extend_mode: ExtendMode,
    pub center: PointKey,
    pub params: ConicGradientParams,
    /// Per-axis tile size encoded as a fraction of `common.prim_size`. The
    /// runtime `stretch_size` is `stretch_ratio * common.prim_size`.
    pub stretch_ratio: SizeKey,
    pub stops: Vec<GradientStopKey>,
    pub tile_spacing: SizeKey,
    pub nine_patch: Option<Box<NinePatchDescriptor>>,
}

impl ConicGradientKey {
    pub fn new(common: PrimKeyCommonData, conic_grad: ConicGradient) -> Self {
        ConicGradientKey {
            common,
            extend_mode: conic_grad.extend_mode,
            center: conic_grad.center,
            params: conic_grad.params,
            stretch_ratio: conic_grad.stretch_ratio,
            stops: conic_grad.stops,
            tile_spacing: conic_grad.tile_spacing,
            nine_patch: conic_grad.nine_patch,
        }
    }
}
